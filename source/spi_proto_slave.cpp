/*
 * spi_proto_slave.c
 *
 *  Created on: Jul 6, 2017
 *      Author: gus
 */
#include "stdint.h"
#include <cstring>

#include "FreeRTOS.h" // required for heartrate.h

#include "heartrate.h"
#include "spi_proto_slave.h"

namespace spi_proto {

//this is a simple protocol, that doesn't support arbitrary length messages
int
slave_get_message(struct spi_proto *p, unsigned char *buf, int len)
{
	//TODO parses the message and does any required processing

	//if it's heartrate,
	//led_delay_time = 0.5/(((float)slaveReceiveBuffer[0]) * (1.0/60.0) * 0.001);
	led_delay_time = 0.5/(((float)(p->getbuf[0])) * (1.0/60.0) * 0.001);

	//TODO make this control solenoid stuff
	return 0;

}

//TODO good reason to believe this causes a hardfault
int
slave_send_message(struct spi_proto *p, unsigned char *buf, int len)
{
	//TODO assert len less than max spi msg len
	//TODO put the message in a queue

	msg m;
	//TODO but buf and len into m
	if (len == 1) { // TODO remove magic number (it's TRANSFER_SIZE, for reference)
		//TODO remove extra copy
		memcpy(m.buf, buf, len);
		push(&p->queue, &m);
		return 0;
	}

}

//do the things the normal interrupt handler did
//int
//slave_handle_spi_interrupt(){}

int
slave_do_tick(struct spi_proto *p)
{
	//handles once-per-message-cycle events such as moving data in and out of buffers.
	if (p->queue.occupancy) {
		//pop message and push into outgoing buffer to be sent
		msg m;
		pop(&p->queue, &m);
		if (p->buflen >= m.len) {
			//enough space, we can proceed
			memcpy(p->sendbuf, &m.buf, m.len);
			return 0;
		}
		return 1;

	} else {
		//no messages in queue, do nothing
		return 0;
	}
}

//PRE both parameters not null, all messages in q have valid lengths
int
pop(spi_proto::msg_queue *q, struct msg *m)
{
	//TODO
	if (!q->occupancy) return -1;
	//TODO lock
	//copy data
	memcpy(&m->buf, q->que[q->first_full].buf, q->que[q->first_full].len);
	//advance first_full, wrap it
	q->first_full = (q->first_full++) % q->capacity;
	//decrement occupancy
	q->occupancy--;
	//TODO unlock
	return 0;
}
int
push(spi_proto::msg_queue *q, struct msg *m)
{
	//TODO
	if (!(q->occupancy < q->capacity)) return -1;
	//TODO lock
	//check data length validity? no, precondition. TODO more principled look at message length management
	//copy data
	memcpy(q->que[q->first_empty].buf, &m->buf, q->que[q->first_empty].len);
	//advance first_empty, wrap it
	q->first_empty = (q->first_empty++) % q->capacity;
	//increment occupancy
	q->occupancy++;
	//TODO unlock
	return 0;
}

int
reset(spi_proto::msg_queue *q)
{
	q->capacity = 10;
	q->first_empty = 0;
	q->first_full = 0;
	q->occupancy = 0;
	return 0;
}
}
