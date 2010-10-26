/*
 * apds9802als.c - apds9802  ALS Driver
 *
 * Copyright (C) 2009 Intel Corp
 *
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
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
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/hwmon-sysfs.h>

#define ALS_MIN_RANGE_VAL 1
#define ALS_MAX_RANGE_VAL 2
#define POWER_STA_ENABLE 1
#define POWER_STA_DISABLE 0
#define APDS9802ALS_I2C_ADDR 0x29

#define DRIVER_NAME "apds9802als"

struct als_data {
	struct device *hwmon_dev;
	bool needresume;
	struct mutex mutex;
};

static ssize_t als_sensing_range_show(struct device *dev,
			struct device_attribute *attr,  char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	int  val;

	val = i2c_smbus_read_byte_data(client, 0x81);
	if (val < 0)
		return val;
	if (val & 1)
		return sprintf(buf, "4095\n");
	else
		return sprintf(buf, "65535\n");
}

static ssize_t als_lux0_input_data_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct als_data *data = i2c_get_clientdata(client);
	unsigned int ret_val;
	int temp;

	/* Protect against parallel reads */
	mutex_lock(&data->mutex);
	temp = i2c_smbus_read_byte_data(client, 0x8C);/*LSB data*/
	if (temp < 0) {
		ret_val = temp;
		goto failed;
	}
	ret_val = i2c_smbus_read_byte_data(client, 0x8D);/*MSB data*/
	if (ret_val < 0)
		goto failed;
	mutex_unlock(&data->mutex);
	ret_val = (ret_val << 8) | temp;
	return sprintf(buf, "%d\n", ret_val);
failed:
	mutex_unlock(&data->mutex);
	return ret_val;
}

static ssize_t als_sensing_range_store(struct device *dev,
		struct device_attribute *attr, const  char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct als_data *data = i2c_get_clientdata(client);
	unsigned int ret_val;
	unsigned long val;

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	if (val < 4096)
		val = 1;
	else if (val < 65536)
		val = 2;
	else
		return -ERANGE;

	/* Make sure nobody else reads/modifies/writes 0x81 while we
	   are active */

	mutex_lock(&data->mutex);

	ret_val = i2c_smbus_read_byte_data(client, 0x81);
	if (ret_val < 0)
		goto fail;

	/* Reset the bits before setting them */
	ret_val = ret_val & 0xFA;

	if (val == 1) /* Setting the continous measurement up to 4k LUX */
		ret_val = (ret_val | 0x05);
	else /* Setting the continous measurement up to 64k LUX*/
		ret_val = (ret_val | 0x04);

	ret_val = i2c_smbus_write_byte_data(client, 0x81, ret_val);
	if (ret_val >= 0) {
		/* All OK */
		mutex_unlock(&data->mutex);
		return count;
	}
fail:
	mutex_unlock(&data->mutex);
	return ret_val;
}

static ssize_t als_power_status_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	int ret_val;
	ret_val = i2c_smbus_read_byte_data(client, 0x80);
	if (ret_val < 0)
		return ret_val;
	ret_val = ret_val & 0x01;
	return sprintf(buf, "%d\n", ret_val);
}

static int als_set_power_state(struct i2c_client *client, bool on_off)
{
	int ret_val;
	struct als_data *data = i2c_get_clientdata(client);

	mutex_lock(&data->mutex);
	ret_val = i2c_smbus_read_byte_data(client, 0x80);
	if (ret_val < 0)
		goto fail;
	if (on_off)
		ret_val = ret_val | 0x01;
	else
		ret_val = ret_val & 0xFE;
	ret_val = i2c_smbus_write_byte_data(client, 0x80, ret_val);
fail:
	mutex_unlock(&data->mutex);
	return ret_val;
}

static ssize_t als_power_status_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct als_data *data = i2c_get_clientdata(client);
	unsigned long val;
	int ret_val;

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;
	if (val == POWER_STA_ENABLE) {
		ret_val = als_set_power_state(client, true);
		data->needresume = true;
	} else if (val == POWER_STA_DISABLE) {
		ret_val = als_set_power_state(client, false);
		data->needresume = false;
	} else
		return -EINVAL;
	if (ret_val < 0)
		return ret_val;
	return count;
}

static DEVICE_ATTR(lux0_sensor_range, S_IRUGO | S_IWUSR,
	als_sensing_range_show, als_sensing_range_store);
static DEVICE_ATTR(lux0_input, S_IRUGO, als_lux0_input_data_show, NULL);
static DEVICE_ATTR(power_state, S_IRUGO | S_IWUSR,
	als_power_status_show, als_power_status_store);

static struct attribute *mid_att_als[] = {
	&dev_attr_lux0_sensor_range.attr,
	&dev_attr_lux0_input.attr,
	&dev_attr_power_state.attr,
	NULL
};

static struct attribute_group m_als_gr = {
	.name = "apds9802als",
	.attrs = mid_att_als
};

static int als_set_default_config(struct i2c_client *client)
{
	int ret_val;
	/* Write the command and then switch on */
	ret_val = i2c_smbus_write_byte_data(client, 0x80, 0x01);
	if (ret_val < 0) {
		dev_err(&client->dev, "failed default switch on write\n");
		return ret_val;
	}
	/* Continous from 1Lux to 64k Lux */
	ret_val = i2c_smbus_write_byte_data(client, 0x81, 0x04);
	if (ret_val < 0)
		dev_err(&client->dev, "failed default LUX on write\n");
	return ret_val;
}

static int  apds9802als_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	int res;
	struct als_data *data;

	data = kzalloc(sizeof(struct als_data), GFP_KERNEL);
	if (data == NULL) {
		dev_err(&client->dev, "Memory allocation failed\n");
		return -ENOMEM;
	}
	i2c_set_clientdata(client, data);
	res = sysfs_create_group(&client->dev.kobj, &m_als_gr);
	if (res) {
		dev_err(&client->dev, "device create file failed\n");
		goto als_error1;
	}
	dev_info(&client->dev,
		"%s apds9802als: ALS chip found\n", client->name);
	als_set_default_config(client);
	data->needresume = true;
	mutex_init(&data->mutex);
	return res;
als_error1:
	i2c_set_clientdata(client, NULL);
	kfree(data);
	return res;
}

static int apds9802als_remove(struct i2c_client *client)
{
	struct als_data *data = i2c_get_clientdata(client);
	sysfs_remove_group(&client->dev.kobj, &m_als_gr);
	kfree(data);
	return 0;
}

static int apds9802als_suspend(struct i2c_client *client, pm_message_t mesg)
{
	als_set_power_state(client, false);
	return 0;
}

static int apds9802als_resume(struct i2c_client *client)
{
	struct als_data *data = i2c_get_clientdata(client);

	if (data->needresume == true)
		als_set_power_state(client, true);
	return 0;
}

static struct i2c_device_id apds9802als_id[] = {
	{ DRIVER_NAME, 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, apds9802als_id);

static struct i2c_driver apds9802als_driver = {
	.driver = {
	.name = DRIVER_NAME,
	.owner = THIS_MODULE,
	},
	.probe = apds9802als_probe,
	.remove = apds9802als_remove,
	.suspend = apds9802als_suspend,
	.resume = apds9802als_resume,
	.id_table = apds9802als_id,
};

static int __init sensor_apds9802als_init(void)
{
	return i2c_add_driver(&apds9802als_driver);
}

static void  __exit sensor_apds9802als_exit(void)
{
	i2c_del_driver(&apds9802als_driver);
}
module_init(sensor_apds9802als_init);
module_exit(sensor_apds9802als_exit);

MODULE_AUTHOR("Anantha Narayanan <Anantha.Narayanan@intel.com");
MODULE_DESCRIPTION("Avago apds9802als ALS Driver");
MODULE_LICENSE("GPL v2");
