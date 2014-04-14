/*
 * OMAP L3 Interconnect error handling driver
 *
 * Copyright (C) 2011-2014 Texas Instruments Incorporated - http://www.ti.com/
 *	Santosh Shilimkar <santosh.shilimkar@ti.com>
 *	Sricharan <r.sricharan@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include "omap_l3_noc.h"

/*
 * Interrupt Handler for L3 error detection.
 *	1) Identify the L3 clockdomain partition to which the error belongs to.
 *	2) Identify the slave where the error information is logged
 *	3) Print the logged information.
 *	4) Add dump stack to provide kernel trace.
 *
 * Two Types of errors :
 *	1) Custom errors in L3 :
 *		Target like DMM/FW/EMIF generates SRESP=ERR error
 *	2) Standard L3 error:
 *		- Unsupported CMD.
 *			L3 tries to access target while it is idle
 *		- OCP disconnect.
 *		- Address hole error:
 *			If DSS/ISS/FDIF/USBHOSTFS access a target where they
 *			do not have connectivity, the error is logged in
 *			their default target which is DMM2.
 *
 *	On High Secure devices, firewall errors are possible and those
 *	can be trapped as well. But the trapping is implemented as part
 *	secure software and hence need not be implemented here.
 */
static irqreturn_t l3_interrupt_handler(int irq, void *_l3)
{

	struct omap_l3 *l3 = _l3;
	int inttype, i, k;
	int err_src = 0;
	u32 std_err_main, err_reg, clear, masterid;
	void __iomem *base, *l3_targ_base;
	void __iomem *l3_targ_stderr, *l3_targ_slvofslsb, *l3_targ_mstaddr;
	char *target_name, *master_name = "UN IDENTIFIED";
	struct l3_target_data *l3_targ_inst;
	struct l3_flagmux_data *flag_mux;
	struct l3_masters_data *master;

	/* Get the Type of interrupt */
	inttype = irq == l3->app_irq ? L3_APPLICATION_ERROR : L3_DEBUG_ERROR;

	for (i = 0; i < l3->num_modules; i++) {
		/*
		 * Read the regerr register of the clock domain
		 * to determine the source
		 */
		base = l3->l3_base[i];
		flag_mux = l3->l3_flagmux[i];
		err_reg = readl_relaxed(base + flag_mux->offset +
					L3_FLAGMUX_REGERR0 + (inttype << 3));

		/* Get the corresponding error and analyse */
		if (err_reg) {
			/* Identify the source from control status register */
			err_src = __ffs(err_reg);

			/* We DONOT expect err_src to go out of bounds */
			BUG_ON(err_src > MAX_CLKDM_TARGETS);

			if (err_src < flag_mux->num_targ_data) {
				l3_targ_inst = &flag_mux->l3_targ[err_src];
				target_name = l3_targ_inst->name;
				l3_targ_base = base + l3_targ_inst->offset;
			} else {
				target_name = L3_TARGET_NOT_SUPPORTED;
			}

			/*
			 * If we do not know of a register offset to decode
			 * and clear, then mask.
			 */
			if (target_name == L3_TARGET_NOT_SUPPORTED) {
				u32 mask_val;
				void __iomem *mask_reg;

				/*
				 * Certain plaforms may have "undocumented"
				 * status pending on boot.. So dont generate
				 * a severe warning here.
				 */
				dev_err(l3->dev,
					"L3 %s error: target %d mod:%d %s\n",
					inttype ? "debug" : "application",
					err_src, i, "(unclearable)");

				mask_reg = base + flag_mux->offset +
					   L3_FLAGMUX_MASK0 + (inttype << 3);
				mask_val = readl_relaxed(mask_reg);
				mask_val &= ~(1 << err_src);
				writel_relaxed(mask_val, mask_reg);

				break;
			}

			/* Read the stderrlog_main_source from clk domain */
			l3_targ_stderr = l3_targ_base + L3_TARG_STDERRLOG_MAIN;
			l3_targ_slvofslsb = l3_targ_base +
					    L3_TARG_STDERRLOG_SLVOFSLSB;
			l3_targ_mstaddr = l3_targ_base +
					  L3_TARG_STDERRLOG_MSTADDR;

			std_err_main = readl_relaxed(l3_targ_stderr);
			masterid = readl_relaxed(l3_targ_mstaddr);

			switch (std_err_main & CUSTOM_ERROR) {
			case STANDARD_ERROR:
				WARN(true, "L3 standard error: TARGET:%s at address 0x%x\n",
					target_name,
					readl_relaxed(l3_targ_slvofslsb));
				/* clear the std error log*/
				clear = std_err_main | CLEAR_STDERR_LOG;
				writel_relaxed(clear, l3_targ_stderr);
				break;

			case CUSTOM_ERROR:
				for (k = 0, master = l3->l3_masters;
				     k < l3->num_masters; k++, master++) {
					if (masterid == master->id) {
						master_name = master->name;
						break;
					}
				}
				WARN(true, "L3 custom error: MASTER:%s TARGET:%s\n",
					master_name, target_name);
				/* clear the std error log*/
				clear = std_err_main | CLEAR_STDERR_LOG;
				writel_relaxed(clear, l3_targ_stderr);
				break;

			default:
				/* Nothing to be handled here as of now */
				break;
			}
		/* Error found so break the for loop */
		break;
		}
	}
	return IRQ_HANDLED;
}

static const struct of_device_id l3_noc_match[] = {
	{.compatible = "ti,omap4-l3-noc", .data = &omap_l3_data},
	{},
};
MODULE_DEVICE_TABLE(of, l3_noc_match);

static int omap_l3_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id;
	static struct omap_l3 *l3;
	int ret, i;

	of_id = of_match_device(l3_noc_match, &pdev->dev);
	if (!of_id) {
		dev_err(&pdev->dev, "OF data missing\n");
		return -EINVAL;
	}

	l3 = devm_kzalloc(&pdev->dev, sizeof(*l3), GFP_KERNEL);
	if (!l3)
		return -ENOMEM;

	memcpy(l3, of_id->data, sizeof(*l3));
	l3->dev = &pdev->dev;
	platform_set_drvdata(pdev, l3);

	/* Get mem resources */
	for (i = 0; i < l3->num_modules; i++) {
		struct resource	*res = platform_get_resource(pdev,
							     IORESOURCE_MEM, i);

		l3->l3_base[i] = devm_ioremap_resource(&pdev->dev, res);
		if (IS_ERR(l3->l3_base[i])) {
			dev_err(l3->dev, "ioremap %d failed\n", i);
			return PTR_ERR(l3->l3_base[i]);
		}
	}

	/*
	 * Setup interrupt Handlers
	 */
	l3->debug_irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(l3->dev, l3->debug_irq, l3_interrupt_handler,
			       IRQF_DISABLED, "l3-dbg-irq", l3);
	if (ret) {
		dev_err(l3->dev, "request_irq failed for %d\n",
			l3->debug_irq);
		return ret;
	}

	l3->app_irq = platform_get_irq(pdev, 1);
	ret = devm_request_irq(l3->dev, l3->app_irq, l3_interrupt_handler,
			       IRQF_DISABLED, "l3-app-irq", l3);
	if (ret)
		dev_err(l3->dev, "request_irq failed for %d\n", l3->app_irq);

	return ret;
}

static struct platform_driver omap_l3_driver = {
	.probe		= omap_l3_probe,
	.driver		= {
		.name		= "omap_l3_noc",
		.owner		= THIS_MODULE,
		.of_match_table = of_match_ptr(l3_noc_match),
	},
};

static int __init omap_l3_init(void)
{
	return platform_driver_register(&omap_l3_driver);
}
postcore_initcall_sync(omap_l3_init);

static void __exit omap_l3_exit(void)
{
	platform_driver_unregister(&omap_l3_driver);
}
module_exit(omap_l3_exit);
