/*
 * Power button driver for Medfield.
 *
 * Copyright (C) 2010 Intel Corp
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/input.h>

#include <asm/intel_scu_ipc.h>

#define DRIVER_NAME "msic_power_btn"

#define MSIC_PB_STATUS	0x3f
#define MSIC_PB_LEVEL	(1 << 3) /* 1 - release, 0 - press */

static irqreturn_t mfld_pb_isr(int irq, void *dev_id)
{
	struct input_dev *input = dev_id;
	int ret;
	u8 pbstat;

	ret = intel_scu_ipc_ioread8(MSIC_PB_STATUS, &pbstat);
	if (ret < 0) {
		dev_err(input->dev.parent, "Read error %d while reading"
			       " MSIC_PB_STATUS\n", ret);
	} else {
		input_event(input, EV_KEY, KEY_POWER,
			       !(pbstat & MSIC_PB_LEVEL));
		input_sync(input);
	}

	return IRQ_HANDLED;
}

static int __devinit mfld_pb_probe(struct platform_device *pdev)
{
	struct input_dev *input;
	int irq = platform_get_irq(pdev, 0);
	int error;

	if (irq < 0)
		return -EINVAL;

	input = input_allocate_device();
	if (!input) {
		dev_err(&pdev->dev, "Input device allocation error\n");
		return -ENOMEM;
	}

	input->name = pdev->name;
	input->phys = "power-button/input0";
	input->id.bustype = BUS_HOST;
	input->dev.parent = &pdev->dev;

	input_set_capability(input, EV_KEY, KEY_POWER);

	error = request_threaded_irq(irq, NULL, mfld_pb_isr, 0,
			DRIVER_NAME, input);
	if (error) {
		dev_err(&pdev->dev, "Unable to request irq %d for mfld power"
				"button\n", irq);
		goto err_free_input;
	}

	error = input_register_device(input);
	if (error) {
		dev_err(&pdev->dev, "Unable to register input dev, error "
				"%d\n", error);
		goto err_free_irq;
	}

	platform_set_drvdata(pdev, input);
	return 0;

err_free_irq:
	free_irq(irq, input);
err_free_input:
	input_free_device(input);
	return error;
}

static int __devexit mfld_pb_remove(struct platform_device *pdev)
{
	struct input_dev *input = platform_get_drvdata(pdev);
	int irq = platform_get_irq(pdev, 0);

	free_irq(irq, input);
	input_unregister_device(input);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static struct platform_driver mfld_pb_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
	.probe	= mfld_pb_probe,
	.remove	= __devexit_p(mfld_pb_remove),
};

static int __init mfld_pb_init(void)
{
	return platform_driver_register(&mfld_pb_driver);
}
module_init(mfld_pb_init);

static void __exit mfld_pb_exit(void)
{
	platform_driver_unregister(&mfld_pb_driver);
}
module_exit(mfld_pb_exit);

MODULE_AUTHOR("Hong Liu <hong.liu@intel.com>");
MODULE_DESCRIPTION("Intel Medfield Power Button Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
