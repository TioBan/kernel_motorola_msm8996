/*
 * Copyright 2008 Sascha Hauer, kernel@pengutronix.de
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <mach/common.h>

int __init mxc_register_device(struct platform_device *pdev, void *data)
{
	int ret;

	pdev->dev.platform_data = data;

	ret = platform_device_register(pdev);
	if (ret)
		pr_debug("Unable to register platform device '%s': %d\n",
			 pdev->name, ret);

	return ret;
}

struct device mxc_aips_bus = {
	.init_name	= "mxc_aips",
	.parent		= &platform_bus,
};

struct device mxc_ahb_bus = {
	.init_name	= "mxc_ahb",
	.parent		= &platform_bus,
};

static int __init mxc_device_init(void)
{
	int ret;

	ret = device_register(&mxc_aips_bus);
	if (IS_ERR_VALUE(ret))
		goto done;

	ret = device_register(&mxc_ahb_bus);

done:
	return ret;
}
core_initcall(mxc_device_init);
