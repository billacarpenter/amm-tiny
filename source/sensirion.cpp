/*  Standard C Included Files */
#include <string.h>

/* FreeRTOS kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
#include "semphr.h"

/*  SDK Included Files */
#include "board.h"
#include "fsl_debug_console.h"
#include "fsl_i2c.h"
#include "fsl_i2c_freertos.h"

#include "pin_mux.h"
#include "clock_config.h"

#include "spi_proto.h"
#include "spi_proto_slave.h"
#include <stdio.h>
/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define I2C_MASTER_BASE I2C1_BASE

#define I2C_MASTER ((I2C_Type *)I2C_MASTER_BASE)

#define I2C_MASTER_CLK_SRC (I2C1_CLK_SRC)

#define I2C_BAUDRATE (100000) /* 100K */
#define I2C_DATA_LENGTH (32)  /* MAX is 256 */

#define SENSIRION_I2C_ADDRESS 0x40U

uint8_t g_master_buff[I2C_DATA_LENGTH];

i2c_master_handle_t g_m_handle;
uint16_t user_reg, read_only_reg_2;
uint16_t offset;
uint8_t eeps[3*22];
uint8_t fixed_eeps[2*22];
uint16_t
sensirion_read_address(i2c_rtos_handle_t &master_rtos_handle, uint8_t addr);
uint16_t
sensirion_read_eeprom_address(i2c_rtos_handle_t &master_rtos_handle, uint16_t addr, uint8_t *buf, size_t n);

//for explanation of odd decisions here see freescale semiconductor FRDM SDK
void sensirion_task(void *pvParameters)
{
	i2c_rtos_handle_t master_rtos_handle;
	i2c_master_config_t masterConfig;
	uint32_t sourceClock;
	status_t status;

	if (__CORTEX_M >= 0x03)
	    NVIC_SetPriority(I2C1_IRQn, 6);
	else
	    NVIC_SetPriority(I2C1_IRQn, 3);

	/*
	 * masterConfig.baudRate_Bps = 100000U;
	 * masterConfig.enableHighDrive = false;
	 * masterConfig.enableStopHold = false;
	 * masterConfig.glitchFilterWidth = 0U;
	 * masterConfig.enableMaster = true;
	 */
	I2C_MasterGetDefaultConfig(&masterConfig);
	masterConfig.baudRate_Bps = I2C_BAUDRATE;
	sourceClock = CLOCK_GetFreq(I2C_MASTER_CLK_SRC);

	status = I2C_RTOS_Init(&master_rtos_handle, I2C_MASTER, &masterConfig, sourceClock);
	if (status != kStatus_Success)
	{
		//TODO log error over SPI somehow
		//PRINTF("I2C master: error during init, %d", status);
	}
	
	user_reg = sensirion_read_address(master_rtos_handle, 0xe3);
	read_only_reg_2 = sensirion_read_address(master_rtos_handle, 0xe9);
	
	//starting at offset 0x2e4, read values until 0x2f9
	//but add offset, which is 0x300 * (read only register 2 bits 2:0)
	offset = 0x300 * (read_only_reg_2&7);
	
	
	sensirion_read_eeprom_address(master_rtos_handle, offset + 0x2e4, eeps, 22);
	//TODO this loop is discarding the checksum. Instead check it, mark dirty, and refetch dirty ones. In general always check sensirion checksums
	for (int i = 0; i < 22; i++) {
		fixed_eeps[(2*i)+0] = eeps[(3*i)+0];
		fixed_eeps[(2*i)+1] = eeps[(3*i)+1];
	}
	
	while (1) {
		

		
		char msg[32];
		snprintf(msg, 32, "sensirion: got %d, %d", g_master_buff[0], g_master_buff[1]);
		
		slave_send_message(spi_proto::p, (unsigned char*) msg, 32);
		vTaskDelay(1000);
	}
    vTaskSuspend(NULL);
}

uint16_t
sensirion_read_voltage(i2c_rtos_handle_t &master_rtos_handle)
{
	i2c_master_transfer_t masterXfer;
	status_t status;
	
	//send 0xf1 to sensirion to enter measurement then read 2 bytes from it
	g_master_buff[0] = 0xf5;
	masterXfer.slaveAddress = SENSIRION_I2C_ADDRESS;
	masterXfer.direction = kI2C_Write;
	masterXfer.subaddress = 0;
	masterXfer.subaddressSize = 0;
	masterXfer.data = g_master_buff;
	masterXfer.dataSize = 1;
	masterXfer.flags = kI2C_TransferDefaultFlag;

	status = I2C_RTOS_Transfer(&master_rtos_handle, &masterXfer);
	if (status != kStatus_Success)
	{
		//TODO log error over SPI somehow
		//PRINTF("I2C master: error during write transaction, %d", status);
	}

	/* Set up master to receive data from slave. */

	memset(g_master_buff, 0, I2C_DATA_LENGTH);

	masterXfer.slaveAddress = SENSIRION_I2C_ADDRESS;
	masterXfer.direction = kI2C_Read;
	masterXfer.subaddress = 0;
	masterXfer.subaddressSize = 0;
	masterXfer.data = g_master_buff;
	masterXfer.dataSize = 2;
	masterXfer.flags = kI2C_TransferDefaultFlag;

	status = I2C_RTOS_Transfer(&master_rtos_handle, &masterXfer);
	if (status != kStatus_Success)
	{
		//TODO log error over spi somehow
		//PRINTF("I2C master: error during read transaction, %d", status);
	}
	
	return g_master_buff[1] + (g_master_buff[0]<<8);
}

//user register is 0xe3
uint16_t
sensirion_read_address(i2c_rtos_handle_t &master_rtos_handle, uint8_t addr)
{
	i2c_master_transfer_t masterXfer;
	status_t status;
	
	//send 0xf1 to sensirion to enter measurement then read 2 bytes from it
	g_master_buff[0] = addr;
	masterXfer.slaveAddress = SENSIRION_I2C_ADDRESS;
	masterXfer.direction = kI2C_Write;
	masterXfer.subaddress = 0;
	masterXfer.subaddressSize = 0;
	masterXfer.data = g_master_buff;
	masterXfer.dataSize = 1;
	masterXfer.flags = kI2C_TransferDefaultFlag;

	status = I2C_RTOS_Transfer(&master_rtos_handle, &masterXfer);
	if (status != kStatus_Success)
	{
		//TODO log error over SPI somehow
		//PRINTF("I2C master: error during write transaction, %d", status);
	}

	/* Set up master to receive data from slave. */

	memset(g_master_buff, 0, I2C_DATA_LENGTH);

	masterXfer.slaveAddress = SENSIRION_I2C_ADDRESS;
	masterXfer.direction = kI2C_Read;
	masterXfer.subaddress = 0;
	masterXfer.subaddressSize = 0;
	masterXfer.data = g_master_buff;
	masterXfer.dataSize = 2;
	masterXfer.flags = kI2C_TransferDefaultFlag;

	status = I2C_RTOS_Transfer(&master_rtos_handle, &masterXfer);
	if (status != kStatus_Success)
	{
		//TODO log error over spi somehow
		//PRINTF("I2C master: error during read transaction, %d", status);
	}
	
	return g_master_buff[1] + (g_master_buff[0]<<8);
}

uint16_t
sensirion_read_eeprom_address(i2c_rtos_handle_t &master_rtos_handle, uint16_t addr, uint8_t *buf, size_t n)
{
	i2c_master_transfer_t masterXfer;
	status_t status;
	
	uint8_t high_addr = addr>>4 & 0xff;
	uint8_t low_addr = addr<<4 & 0xf0;
	//send 0xf1 to sensirion to enter measurement then read 2 bytes from it
	g_master_buff[0] = 0xfa;
	g_master_buff[1] = high_addr;
	g_master_buff[2] = low_addr;
	masterXfer.slaveAddress = SENSIRION_I2C_ADDRESS;
	masterXfer.direction = kI2C_Write;
	masterXfer.subaddress = 0;
	masterXfer.subaddressSize = 0;
	masterXfer.data = g_master_buff;
	masterXfer.dataSize = 3;
	masterXfer.flags = kI2C_TransferDefaultFlag;

	status = I2C_RTOS_Transfer(&master_rtos_handle, &masterXfer);
	if (status != kStatus_Success)
	{
		//TODO log error over SPI somehow
		//PRINTF("I2C master: error during write transaction, %d", status);
	}

	/* Set up master to receive data from slave. */

	memset(g_master_buff, 0, I2C_DATA_LENGTH);

	masterXfer.slaveAddress = SENSIRION_I2C_ADDRESS;
	masterXfer.direction = kI2C_Read;
	masterXfer.subaddress = 0;
	masterXfer.subaddressSize = 0;
	masterXfer.data = buf;
	masterXfer.dataSize = 3*n;
	masterXfer.flags = kI2C_TransferDefaultFlag;

	status = I2C_RTOS_Transfer(&master_rtos_handle, &masterXfer);
	if (status != kStatus_Success)
	{
		//TODO log error over spi somehow
		//PRINTF("I2C master: error during read transaction, %d", status);
	}
	
	return g_master_buff[1] + (g_master_buff[0]<<8);
}
