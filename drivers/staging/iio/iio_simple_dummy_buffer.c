/**
 * Copyright (c) 2011 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Buffer handling elements of industrial I/O reference driver.
 * Uses the kfifo buffer.
 *
 * To test without hardware use the sysfs trigger.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/bitmap.h>

#include "iio.h"
#include "trigger_consumer.h"
#include "kfifo_buf.h"

#include "iio_simple_dummy.h"

/* Some fake data */

static const s16 fakedata[] = {
	[voltage0] = 7,
	[diffvoltage1m2] = -33,
	[diffvoltage3m4] = -2,
	[accelx] = 344,
};
/**
 * iio_simple_dummy_trigger_h() - the trigger handler function
 * @irq: the interrupt number
 * @p: private data - always a pointer to the poll func.
 *
 * This is the guts of buffered capture. On a trigger event occuring,
 * if the pollfunc is attached then this handler is called as a threaded
 * interrupt (and hence may sleep). It is responsible for grabbing data
 * from the device and pushing it into the associated buffer.
 */
static irqreturn_t iio_simple_dummy_trigger_h(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct iio_buffer *buffer = indio_dev->buffer;
	int len = 0;
	/*
	 * The datasize is obtained from the buffer. It was stored when
	 * the preenable setup function was called.
	 */
	size_t datasize = buffer->access->get_bytes_per_datum(buffer);
	u16 *data = kmalloc(datasize, GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	if (buffer->scan_count) {
		/*
		 * Three common options here:
		 * hardware scans: certain combinations of channels make
		 *   up a fast read.  The capture will consist of all of them.
		 *   Hence we just call the grab data function and fill the
		 *   buffer without processing.
		 * sofware scans: can be considered to be random access
		 *   so efficient reading is just a case of minimal bus
		 *   transactions.
		 * software culled hardware scans:
		 *   occasionally a driver may process the nearest hardware
		 *   scan to avoid storing elements that are not desired. This
		 *   is the fidliest option by far.
		 * Here lets pretend we have random access. And the values are
		 * in the constant table fakedata.
		 */
		int i, j;
		for (i = 0, j = 0; i < buffer->scan_count; i++) {
			j = find_next_bit(buffer->scan_mask,
					  indio_dev->masklength, j + 1);
			/* random access read form the 'device' */
			data[i] = fakedata[j];
			len += 2;
		}
	}
	/* Store a timestampe at an 8 byte boundary */
	if (buffer->scan_timestamp)
		*(s64 *)(((phys_addr_t)data + len
				+ sizeof(s64) - 1) & ~(sizeof(s64) - 1))
			= iio_get_time_ns();
	buffer->access->store_to(buffer, (u8 *)data, pf->timestamp);

	kfree(data);

	/*
	 * Tell the core we are done with this trigger and ready for the
	 * next one.
	 */
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static const struct iio_buffer_setup_ops iio_simple_dummy_buffer_setup_ops = {
	/*
	 * iio_sw_buffer_preenable:
	 * Generic function for equal sized ring elements + 64 bit timestamp
	 * Assumes that any combination of channels can be enabled.
	 * Typically replaced to implement restrictions on what combinations
	 * can be captured (hardware scan modes).
	 */
	.preenable = &iio_sw_buffer_preenable,
	/*
	 * iio_triggered_buffer_postenable:
	 * Generic function that simply attaches the pollfunc to the trigger.
	 * Replace this to mess with hardware state before we attach the
	 * trigger.
	 */
	.postenable = &iio_triggered_buffer_postenable,
	/*
	 * iio_triggered_buffer_predisable:
	 * Generic function that simple detaches the pollfunc from the trigger.
	 * Replace this to put hardware state back again after the trigger is
	 * detached but before userspace knows we have disabled the ring.
	 */
	.predisable = &iio_triggered_buffer_predisable,
};

int iio_simple_dummy_configure_buffer(struct iio_dev *indio_dev)
{
	int ret;
	struct iio_buffer *buffer;

	/* Allocate a buffer to use - here a kfifo */
	buffer = iio_kfifo_allocate(indio_dev);
	if (buffer == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	indio_dev->buffer = buffer;
	/* Tell the core how to access the buffer */
	buffer->access = &kfifo_access_funcs;

	/* Number of bytes per element */
	buffer->bpe = 2;
	/* Enable timestamps by default */
	buffer->scan_timestamp = true;

	/*
	 * Tell the core what device type specific functions should
	 * be run on either side of buffer capture enable / disable.
	 */
	buffer->setup_ops = &iio_simple_dummy_buffer_setup_ops;
	buffer->owner = THIS_MODULE;

	/*
	 * Configure a polling function.
	 * When a trigger event with this polling function connected
	 * occurs, this function is run. Typically this grabs data
	 * from the device.
	 *
	 * NULL for the top half. This is normally implemented only if we
	 * either want to ping a capture now pin (no sleeping) or grab
	 * a timestamp as close as possible to a data ready trigger firing.
	 *
	 * IRQF_ONESHOT ensures irqs are masked such that only one instance
	 * of the handler can run at a time.
	 *
	 * "iio_simple_dummy_consumer%d" formatting string for the irq 'name'
	 * as seen under /proc/interrupts. Remaining parameters as per printk.
	 */
	indio_dev->pollfunc = iio_alloc_pollfunc(NULL,
						 &iio_simple_dummy_trigger_h,
						 IRQF_ONESHOT,
						 indio_dev,
						 "iio_simple_dummy_consumer%d",
						 indio_dev->id);

	if (indio_dev->pollfunc == NULL) {
		ret = -ENOMEM;
		goto error_free_buffer;
	}

	/*
	 * Notify the core that this device is capable of buffered capture
	 * driven by a trigger.
	 */
	indio_dev->modes |= INDIO_BUFFER_TRIGGERED;
	return 0;

error_free_buffer:
	iio_kfifo_free(indio_dev->buffer);
error_ret:
	return ret;

}

/**
 * iio_simple_dummy_unconfigure_buffer() - release buffer resources
 * @indo_dev: device instance state
 */
void iio_simple_dummy_unconfigure_buffer(struct iio_dev *indio_dev)
{
	iio_dealloc_pollfunc(indio_dev->pollfunc);
	iio_kfifo_free(indio_dev->buffer);
}
