#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "maxon.h"
#include "fsl_adc16.h"
#include "pressuresensor.h"
#include "ammdk-carrier/solenoid.h"
#include "air_tank.h"

struct pid_ctl {
	float p;
	float i;
	float d;
	float target;
	
	float isum; // current value
	float last;
	float last_diff;
};

float
pi_supply(struct pid_ctl *p, float reading)
{
	float diff = reading - p->last;
	p->last = reading;
	p->last_diff = diff;
	float oset = p->target - reading;
	
	p->isum += oset * p->i;
	
	return p->isum + p->p*oset + p->d*diff;
}

struct pid_ctl pid;

uint32_t stall_val = 0x100;
//PSI (atmospheric is 0)
float operating_pressure = 5.0;

volatile bool should_pid_run = true;
float ret;
uint32_t val;
void
air_reservoir_control_task(void *params)
{
	solenoid::off(solenoids[0]); // A
	solenoid::on(solenoids[1]); // B
	
	pid.p = 24;
	pid.i = 1.0/1024;
	pid.d = 1.0/16;
	pid.isum = 0;
	
	should_24v_be_on = 1;
	should_motor_run = 1;
	
	//voltage divider
	//TODO not currently used. Once we figure out why the equation from the datasheet was not working use it again.
	//float r1 = 1200;
	//float r2 = 2200;
	//float voldiv = r2/(r1+r2);
	
	for (;;) {
		pid.target = operating_pressure;
		if (should_pid_run) {
			//don't update if motor isn't running as it will run too far off
			//TODO also don't update if solenoid 2 is open
			float hold_isum = pid.isum;
			uint32_t adcRead = carrier_sensors[0].raw_pressure;
			float psi = ((float)adcRead)*(3.0/10280.0*16.0) - 15.0/8.0;
		
			ret = pi_supply(&pid, psi);
		
			//convert back to 0-2^12 range for DAC
			val = (uint32_t) (ret*1000.0);
			should_motor_run = stall_val < val;
			if (!should_motor_run) {
				pid.isum = hold_isum;
			}
			dacVal = val > 0xfff ? 0xfff : val;
		}
		vTaskDelay(50);
	}
	vTaskSuspend(NULL);
}

