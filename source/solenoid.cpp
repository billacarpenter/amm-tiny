/*
 * solenoid.cpp
 *
 *  Created on: Jul 7, 2017
 *      Author: gus
 */
#include "board.h"
#include "fsl_gpio.h"

#include "FreeRTOS.h"
#include "task.h"

#include "heartrate.h"
#include "spi_proto_slave.h"
#include "solenoid.h"


int
solenoids_toggle(void)
{
	//TODO toggle all solenoids

	GPIO_TogglePinsOutput(GPIOA, 1 << 25U);
	GPIO_TogglePinsOutput(GPIOB, 1 << 18U);
	GPIO_TogglePinsOutput(GPIOB, 1 << 19U);
	//GPIO_TogglePinsOutput(GPIOC, 1 << 10U);
	//GPIO_TogglePinsOutput(GPIOC, 1 << 11U);

	return 0;
}

void
hemorrhage_on(void) {GPIO_SetPinsOutput(GPIOC, 1 << 10U);}
void
hemorrhage_off(void) {GPIO_ClearPinsOutput(GPIOC, 1 << 10U);}
void
hemorrhage_toggle(void) {GPIO_TogglePinsOutput(GPIOC, 1 << 10U);}

void
solenoid_task(void *params)
{
	for (;;) {
		//solenoids_toggle();
		if (spi_proto::spi_transactions > 0) {
			tourniquet_on ? hemorrhage_off() : hemorrhage_toggle();
		}
		vTaskDelay(led_delay_time > 0 ? led_delay_time : 1000);
		//vTaskDelay(3000);
	}
	vTaskSuspend(NULL);
}

