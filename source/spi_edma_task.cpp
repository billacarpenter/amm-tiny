
#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "fsl_dmamux.h"
#include "fsl_dspi.h"
#include "fsl_dspi_edma.h"
#include "fsl_edma.h"
#include "fsl_gpio.h"

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "spi_proto.h"
#include "spi_proto_slave.h"
#include "spi_edma_task.h"

#define EXAMPLE_DSPI_SLAVE_BASEADDR SPI0
#define EXAMPLE_DSPI_SLAVE_DMA_MUX_BASEADDR DMAMUX
#define EXAMPLE_DSPI_SLAVE_DMA_BASEADDR DMA0
//page 473 in k66 sub family reference manual, the DMAMUX chapter
#define EXAMPLE_DSPI_SLAVE_DMA_TX_REQUEST_SOURCE 15U
#define EXAMPLE_DSPI_SLAVE_DMA_RX_REQUEST_SOURCE 14U
// #define TRANSFER_SIZE 256U        /*! Transfer dataSize */
//#define TRANSFER_BAUDRATE 500000U /*! Transfer baudrate - 500k */
//TODO centralize this definition
#define TRANSFER_BAUDRATE (8388608U)


//TODO consider architechtures other than a semaphore, if necessary (perhaps a flag and array that the callback can read from?)
extern "C" {

void
DSPI_SlaveUserCallback(SPI_Type *base, dspi_slave_edma_handle_t *handle, status_t status, void *userData);
volatile bool isTransferCompleted = false;
SemaphoreHandle_t dspi_sem;

typedef struct _callback_message_t
{
    status_t async_status;
    SemaphoreHandle_t sem;
} callback_message_t;

void
DSPI_SlaveUserCallback(SPI_Type *base, dspi_slave_edma_handle_t *handle, status_t status, void *userData)
{
	//if (status == kStatus_Success) {
		//PRINTF("This is DSPI slave edma transfer completed callback. \r\n\r\n");
	//}
	callback_message_t *cb_msg = (callback_message_t *)userData;
	BaseType_t reschedule;

	cb_msg->async_status = status;
	xSemaphoreGiveFromISR(cb_msg->sem, &reschedule);
	portYIELD_FROM_ISR(reschedule);
	//isTransferCompleted = true;
}

/* edma variables */

dspi_slave_edma_handle_t g_dspi_edma_s_handle;
edma_handle_t dspiEdmaSlaveRxRegToRxDataHandle;
edma_handle_t dspiEdmaSlaveTxDataToTxRegHandle;

//TODO tie into spi_proto system
//buffers for testing
uint8_t slaveRxData[TRANSFER_SIZE] = {0};
uint8_t slaveTxData[TRANSFER_SIZE] = {0};

void
spi_edma_task(void *pvParams)
{
	using namespace spi_proto;
	callback_message_t cb_msg;
	// init p, set up buffers and len
	spi_proto_slave_initialize(&spi_proto::p);
	p.buflen = TRANSFER_SIZE;
	p.sendbuf = slaveTxData;
	p.getbuf = slaveRxData;
	spi_proto::ready = true;
	
	NVIC_SetPriority(DMA3_DMA19_IRQn, 5); // hopefully resolves issue with semaphore in interrupt
	
	cb_msg.sem = xSemaphoreCreateBinary();
	dspi_sem = cb_msg.sem;
	if (cb_msg.sem == NULL) {
		PRINTF("DSPI slave: Error creating semaphore\r\n");
		vTaskSuspend(NULL);
	}
	//set up slave edma for spi, on SPI0
	
	/* DMA Mux setting and EDMA init */
	uint32_t slaveRxChannel, slaveTxChannel;
	edma_config_t userConfig;

	slaveRxChannel = 3U; // checked for SPI0
	slaveTxChannel = 4U; // checked for SPI0
	
	//TODO need DMAMUX settings
	DMAMUX_Init(EXAMPLE_DSPI_SLAVE_DMA_MUX_BASEADDR);
	
	DMAMUX_SetSource(EXAMPLE_DSPI_SLAVE_DMA_MUX_BASEADDR, slaveRxChannel, EXAMPLE_DSPI_SLAVE_DMA_RX_REQUEST_SOURCE); // TODO fixup names
	DMAMUX_EnableChannel(EXAMPLE_DSPI_SLAVE_DMA_MUX_BASEADDR, slaveRxChannel);
	
    DMAMUX_SetSource(EXAMPLE_DSPI_SLAVE_DMA_MUX_BASEADDR, slaveTxChannel, EXAMPLE_DSPI_SLAVE_DMA_TX_REQUEST_SOURCE);
	DMAMUX_EnableChannel(EXAMPLE_DSPI_SLAVE_DMA_MUX_BASEADDR, slaveTxChannel);
	
	EDMA_GetDefaultConfig(&userConfig);
	EDMA_Init(EXAMPLE_DSPI_SLAVE_DMA_BASEADDR, &userConfig);
	
	/*DSPI init*/
	uint32_t srcClock_Hz;
	uint32_t errorCount;
	uint32_t i;
	dspi_master_config_t masterConfig;
	dspi_slave_config_t slaveConfig;
	dspi_transfer_t masterXfer;
	dspi_transfer_t slaveXfer;

	/*Master config*/ // TODO confirm all these params, refactor to tear out master usage
	masterConfig.whichCtar = kDSPI_Ctar0;
	masterConfig.ctarConfig.baudRate = TRANSFER_BAUDRATE;
	masterConfig.ctarConfig.bitsPerFrame = 8U;
	masterConfig.ctarConfig.cpol = kDSPI_ClockPolarityActiveHigh;
	masterConfig.ctarConfig.cpha = kDSPI_ClockPhaseFirstEdge;
	masterConfig.ctarConfig.direction = kDSPI_MsbFirst;
	masterConfig.ctarConfig.pcsToSckDelayInNanoSec = 1000000000U / TRANSFER_BAUDRATE;
	masterConfig.ctarConfig.lastSckToPcsDelayInNanoSec = 1000000000U / TRANSFER_BAUDRATE;
	masterConfig.ctarConfig.betweenTransferDelayInNanoSec = 1000000000U / TRANSFER_BAUDRATE;

	masterConfig.enableContinuousSCK = false;
	masterConfig.enableRxFifoOverWrite = false;
	masterConfig.enableModifiedTimingFormat = false;
	masterConfig.samplePoint = kDSPI_SckToSin0Clock;

	/*Slave config*/
	slaveConfig.whichCtar = kDSPI_Ctar0;
	slaveConfig.ctarConfig.bitsPerFrame = masterConfig.ctarConfig.bitsPerFrame;
	slaveConfig.ctarConfig.cpol = masterConfig.ctarConfig.cpol;
	slaveConfig.ctarConfig.cpha = masterConfig.ctarConfig.cpha;
	slaveConfig.enableContinuousSCK = masterConfig.enableContinuousSCK;
	slaveConfig.enableRxFifoOverWrite = masterConfig.enableRxFifoOverWrite;
	slaveConfig.enableModifiedTimingFormat = masterConfig.enableModifiedTimingFormat;
	slaveConfig.samplePoint = masterConfig.samplePoint;

	DSPI_SlaveInit(EXAMPLE_DSPI_SLAVE_BASEADDR, &slaveConfig);
	
	{
		int k = 0;
		for (i = 0U; i < TRANSFER_SIZE; i++)
		{
			slaveTxData[i] = k;
			k += 3;
			slaveRxData[i] = 0U;
		}
	}
	
	/* Set up dspi slave first */
	memset(&(dspiEdmaSlaveRxRegToRxDataHandle), 0, sizeof(dspiEdmaSlaveRxRegToRxDataHandle));
	memset(&(dspiEdmaSlaveTxDataToTxRegHandle), 0, sizeof(dspiEdmaSlaveTxDataToTxRegHandle));
	EDMA_CreateHandle(&(dspiEdmaSlaveRxRegToRxDataHandle), EXAMPLE_DSPI_SLAVE_DMA_BASEADDR, slaveRxChannel);
	EDMA_CreateHandle(&(dspiEdmaSlaveTxDataToTxRegHandle), EXAMPLE_DSPI_SLAVE_DMA_BASEADDR, slaveTxChannel);

	isTransferCompleted = false;

	DSPI_SlaveTransferCreateHandleEDMA
		( EXAMPLE_DSPI_SLAVE_BASEADDR
		, &g_dspi_edma_s_handle
		, DSPI_SlaveUserCallback
		, &cb_msg
		, &dspiEdmaSlaveRxRegToRxDataHandle
		, &dspiEdmaSlaveTxDataToTxRegHandle
		);

	uint8_t blank_msg[SPI_MSG_PAYLOAD_LEN];
	for (;;) {
		//ensure there is at least 1 message to be sent. TODO alter protocol send logic to make this unnecessary
		if (p.proto.num_unsent == 0) {
			slave_send_message(p, blank_msg, 0); // TODO in theory middle arg can be NULL
		}
		slave_do_tick(p);
		slaveXfer.txData = slaveTxData;
		slaveXfer.rxData = slaveRxData;
		slaveXfer.dataSize = TRANSFER_SIZE;
		slaveXfer.configFlags = kDSPI_SlaveCtar0; // TODO check


		if (kStatus_Success != DSPI_SlaveTransferEDMA(EXAMPLE_DSPI_SLAVE_BASEADDR, &g_dspi_edma_s_handle, &slaveXfer))
		{
		    PRINTF("There is error when start DSPI_SlaveTransferEDMA \r\n");
			//TODO do something to mitigate this
			//TODO 2 determine how to mitigate a total communication failure at runtime
		}
		//PRINTF("waiting on semaphore\n");
		
		xSemaphoreTake(cb_msg.sem, portMAX_DELAY);
		
		//PRINTF("%d SPI transaction done!\n", xTaskGetTickCount());
		spi_proto::spi_transactions++;

		//handle the received message
		slave_spi_proto_rcv_msg(p, p.getbuf, p.buflen);

	}
	DSPI_Deinit(EXAMPLE_DSPI_SLAVE_BASEADDR);
	
	vTaskSuspend(NULL);
}
} // extern c