/*
 * Roccat Arvo driver for Linux
 *
 * Copyright (c) 2011 Stefan Achatz <erazor_de@users.sourceforge.net>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

/*
 * Roccat Arvo is a gamer keyboard with 5 macro keys that can be configured in
 * 5 profiles.
 */

#include <linux/device.h>
#include <linux/input.h>
#include <linux/hid.h>
#include <linux/usb.h>
#include <linux/module.h>
#include <linux/slab.h>
#include "hid-ids.h"
#include "hid-roccat.h"
#include "hid-roccat-arvo.h"

static struct class *arvo_class;

static int arvo_receive(struct usb_device *usb_dev, uint usb_command,
		void *buf, uint size)
{
	int len;

	len = usb_control_msg(usb_dev, usb_rcvctrlpipe(usb_dev, 0),
			USB_REQ_CLEAR_FEATURE,
			USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_IN,
			usb_command, 0, buf, size, USB_CTRL_SET_TIMEOUT);

	return (len != size) ? -EIO : 0;
}

static int arvo_send(struct usb_device *usb_dev, uint usb_command,
		void const *buf, uint size)
{
	int len;

	len = usb_control_msg(usb_dev, usb_sndctrlpipe(usb_dev, 0),
			USB_REQ_SET_CONFIGURATION,
			USB_TYPE_CLASS | USB_RECIP_INTERFACE | USB_DIR_OUT,
			usb_command, 0, (void *)buf, size, USB_CTRL_SET_TIMEOUT);

	return (len != size) ? -EIO : 0;
}

static ssize_t arvo_sysfs_show_mode_key(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct arvo_device *arvo =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));
	struct usb_device *usb_dev =
			interface_to_usbdev(to_usb_interface(dev->parent->parent));
	struct arvo_mode_key *temp_buf;
	int retval;

	temp_buf = kmalloc(sizeof(struct arvo_mode_key), GFP_KERNEL);
	if (!temp_buf)
		return -ENOMEM;

	mutex_lock(&arvo->arvo_lock);
	retval = arvo_receive(usb_dev, ARVO_USB_COMMAND_MODE_KEY,
			temp_buf, sizeof(struct arvo_mode_key));
	mutex_unlock(&arvo->arvo_lock);
	if (retval)
		goto out;

	retval = snprintf(buf, PAGE_SIZE, "%d\n", temp_buf->state);
out:
	kfree(temp_buf);
	return retval;
}

static ssize_t arvo_sysfs_set_mode_key(struct device *dev,
		struct device_attribute *attr, char const *buf, size_t size)
{
	struct arvo_device *arvo =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));
	struct usb_device *usb_dev =
			interface_to_usbdev(to_usb_interface(dev->parent->parent));
	struct arvo_mode_key *temp_buf;
	unsigned long state;
	int retval;

	temp_buf = kmalloc(sizeof(struct arvo_mode_key), GFP_KERNEL);
	if (!temp_buf)
		return -ENOMEM;

	retval = strict_strtoul(buf, 10, &state);
	if (retval)
		goto out;

	temp_buf->command = ARVO_COMMAND_MODE_KEY;
	temp_buf->state = state;

	mutex_lock(&arvo->arvo_lock);
	retval = arvo_send(usb_dev, ARVO_USB_COMMAND_MODE_KEY,
			temp_buf, sizeof(struct arvo_mode_key));
	mutex_unlock(&arvo->arvo_lock);
	if (retval)
		goto out;

	retval = size;
out:
	kfree(temp_buf);
	return retval;
}

static ssize_t arvo_sysfs_show_key_mask(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct arvo_device *arvo =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));
	struct usb_device *usb_dev =
			interface_to_usbdev(to_usb_interface(dev->parent->parent));
	struct arvo_key_mask *temp_buf;
	int retval;

	temp_buf = kmalloc(sizeof(struct arvo_key_mask), GFP_KERNEL);
	if (!temp_buf)
		return -ENOMEM;

	mutex_lock(&arvo->arvo_lock);
	retval = arvo_receive(usb_dev, ARVO_USB_COMMAND_KEY_MASK,
			temp_buf, sizeof(struct arvo_key_mask));
	mutex_unlock(&arvo->arvo_lock);
	if (retval)
		goto out;

	retval = snprintf(buf, PAGE_SIZE, "%d\n", temp_buf->key_mask);
out:
	kfree(temp_buf);
	return retval;
}

static ssize_t arvo_sysfs_set_key_mask(struct device *dev,
		struct device_attribute *attr, char const *buf, size_t size)
{
	struct arvo_device *arvo =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));
	struct usb_device *usb_dev =
			interface_to_usbdev(to_usb_interface(dev->parent->parent));
	struct arvo_key_mask *temp_buf;
	unsigned long key_mask;
	int retval;

	temp_buf = kmalloc(sizeof(struct arvo_key_mask), GFP_KERNEL);
	if (!temp_buf)
		return -ENOMEM;

	retval = strict_strtoul(buf, 10, &key_mask);
	if (retval)
		goto out;

	temp_buf->command = ARVO_COMMAND_KEY_MASK;
	temp_buf->key_mask = key_mask;

	mutex_lock(&arvo->arvo_lock);
	retval = arvo_send(usb_dev, ARVO_USB_COMMAND_KEY_MASK,
			temp_buf, sizeof(struct arvo_key_mask));
	mutex_unlock(&arvo->arvo_lock);
	if (retval)
		goto out;

	retval = size;
out:
	kfree(temp_buf);
	return retval;
}

/* retval is 1-5 on success, < 0 on error */
static int arvo_get_actual_profile(struct usb_device *usb_dev)
{
	struct arvo_actual_profile *temp_buf;
	int retval;

	temp_buf = kmalloc(sizeof(struct arvo_actual_profile), GFP_KERNEL);
	if (!temp_buf)
		return -ENOMEM;

	retval = arvo_receive(usb_dev, ARVO_USB_COMMAND_ACTUAL_PROFILE,
			temp_buf, sizeof(struct arvo_actual_profile));

	if (!retval)
		retval = temp_buf->actual_profile;

	kfree(temp_buf);
	return retval;
}

static ssize_t arvo_sysfs_show_actual_profile(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct arvo_device *arvo =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));

	return snprintf(buf, PAGE_SIZE, "%d\n", arvo->actual_profile);
}

static ssize_t arvo_sysfs_set_actual_profile(struct device *dev,
		struct device_attribute *attr, char const *buf, size_t size)
{
	struct arvo_device *arvo =
			hid_get_drvdata(dev_get_drvdata(dev->parent->parent));
	struct usb_device *usb_dev =
			interface_to_usbdev(to_usb_interface(dev->parent->parent));
	struct arvo_actual_profile *temp_buf;
	unsigned long profile;
	int retval;

	temp_buf = kmalloc(sizeof(struct arvo_actual_profile), GFP_KERNEL);
	if (!temp_buf)
		return -ENOMEM;

	retval = strict_strtoul(buf, 10, &profile);
	if (retval)
		goto out;

	temp_buf->command = ARVO_COMMAND_ACTUAL_PROFILE;
	temp_buf->actual_profile = profile;

	mutex_lock(&arvo->arvo_lock);
	retval = arvo_send(usb_dev, ARVO_USB_COMMAND_ACTUAL_PROFILE,
			temp_buf, sizeof(struct arvo_actual_profile));
	if (!retval) {
		arvo->actual_profile = profile;
		retval = size;
	}
	mutex_unlock(&arvo->arvo_lock);

out:
	kfree(temp_buf);
	return retval;
}

static ssize_t arvo_sysfs_write(struct file *fp,
		struct kobject *kobj, void const *buf,
		loff_t off, size_t count, size_t real_size, uint command)
{
	struct device *dev =
			container_of(kobj, struct device, kobj)->parent->parent;
	struct arvo_device *arvo = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	int retval;

	if (off != 0 || count != real_size)
		return -EINVAL;

	mutex_lock(&arvo->arvo_lock);
	retval = arvo_send(usb_dev, command, buf, real_size);
	mutex_unlock(&arvo->arvo_lock);

	return (retval ? retval : real_size);
}

static ssize_t arvo_sysfs_read(struct file *fp,
		struct kobject *kobj, void *buf, loff_t off,
		size_t count, size_t real_size, uint command)
{
	struct device *dev =
			container_of(kobj, struct device, kobj)->parent->parent;
	struct arvo_device *arvo = hid_get_drvdata(dev_get_drvdata(dev));
	struct usb_device *usb_dev = interface_to_usbdev(to_usb_interface(dev));
	int retval;

	if (off >= real_size)
		return 0;

	if (off != 0 || count != real_size)
		return -EINVAL;

	mutex_lock(&arvo->arvo_lock);
	retval = arvo_receive(usb_dev, command, buf, real_size);
	mutex_unlock(&arvo->arvo_lock);

	return (retval ? retval : real_size);
}

static ssize_t arvo_sysfs_write_button(struct file *fp,
		struct kobject *kobj, struct bin_attribute *attr, char *buf,
		loff_t off, size_t count)
{
	return arvo_sysfs_write(fp, kobj, buf, off, count,
			sizeof(struct arvo_button), ARVO_USB_COMMAND_BUTTON);
}

static ssize_t arvo_sysfs_read_info(struct file *fp,
		struct kobject *kobj, struct bin_attribute *attr, char *buf,
		loff_t off, size_t count)
{
	return arvo_sysfs_read(fp, kobj, buf, off, count,
			sizeof(struct arvo_info), ARVO_USB_COMMAND_INFO);
}


static struct device_attribute arvo_attributes[] = {
	__ATTR(mode_key, 0660,
			arvo_sysfs_show_mode_key, arvo_sysfs_set_mode_key),
	__ATTR(key_mask, 0660,
			arvo_sysfs_show_key_mask, arvo_sysfs_set_key_mask),
	__ATTR(actual_profile, 0660,
			arvo_sysfs_show_actual_profile,
			arvo_sysfs_set_actual_profile),
	__ATTR_NULL
};

static struct bin_attribute arvo_bin_attributes[] = {
	{
		.attr = { .name = "button", .mode = 0220 },
		.size = sizeof(struct arvo_button),
		.write = arvo_sysfs_write_button
	},
	{
		.attr = { .name = "info", .mode = 0440 },
		.size = sizeof(struct arvo_info),
		.read = arvo_sysfs_read_info
	},
	__ATTR_NULL
};

static int arvo_init_arvo_device_struct(struct usb_device *usb_dev,
		struct arvo_device *arvo)
{
	int retval;

	mutex_init(&arvo->arvo_lock);

	retval = arvo_get_actual_profile(usb_dev);
	if (retval < 0)
		return retval;
	arvo->actual_profile = retval;

	return 0;
}

static int arvo_init_specials(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct arvo_device *arvo;
	int retval;

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			== USB_INTERFACE_PROTOCOL_KEYBOARD) {
		hid_set_drvdata(hdev, NULL);
		return 0;
	}

	arvo = kzalloc(sizeof(*arvo), GFP_KERNEL);
	if (!arvo) {
		dev_err(&hdev->dev, "can't alloc device descriptor\n");
		return -ENOMEM;
	}
	hid_set_drvdata(hdev, arvo);

	retval = arvo_init_arvo_device_struct(usb_dev, arvo);
	if (retval) {
		dev_err(&hdev->dev,
				"couldn't init struct arvo_device\n");
		goto exit_free;
	}

	retval = roccat_connect(arvo_class, hdev);
	if (retval < 0) {
		dev_err(&hdev->dev, "couldn't init char dev\n");
	} else {
		arvo->chrdev_minor = retval;
		arvo->roccat_claimed = 1;
	}

	return 0;
exit_free:
	kfree(arvo);
	return retval;
}

static void arvo_remove_specials(struct hid_device *hdev)
{
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	struct arvo_device *arvo;

	if (intf->cur_altsetting->desc.bInterfaceProtocol
			== USB_INTERFACE_PROTOCOL_KEYBOARD)
		return;

	arvo = hid_get_drvdata(hdev);
	if (arvo->roccat_claimed)
		roccat_disconnect(arvo->chrdev_minor);
	kfree(arvo);
}

static int arvo_probe(struct hid_device *hdev,
		const struct hid_device_id *id)
{
	int retval;

	retval = hid_parse(hdev);
	if (retval) {
		dev_err(&hdev->dev, "parse failed\n");
		goto exit;
	}

	retval = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (retval) {
		dev_err(&hdev->dev, "hw start failed\n");
		goto exit;
	}

	retval = arvo_init_specials(hdev);
	if (retval) {
		dev_err(&hdev->dev, "couldn't install keyboard\n");
		goto exit_stop;
	}

	return 0;

exit_stop:
	hid_hw_stop(hdev);
exit:
	return retval;
}

static void arvo_remove(struct hid_device *hdev)
{
	arvo_remove_specials(hdev);
	hid_hw_stop(hdev);
}

static void arvo_report_to_chrdev(struct arvo_device const *arvo,
		u8 const *data)
{
	struct arvo_special_report const *special_report;
	struct arvo_roccat_report roccat_report;

	special_report = (struct arvo_special_report const *)data;

	roccat_report.profile = arvo->actual_profile;
	roccat_report.button = special_report->event &
			ARVO_SPECIAL_REPORT_EVENT_MASK_BUTTON;
	if ((special_report->event & ARVO_SPECIAL_REPORT_EVENT_MASK_ACTION) ==
			ARVO_SPECIAL_REPORT_EVENT_ACTION_PRESS)
		roccat_report.action = ARVO_ROCCAT_REPORT_ACTION_PRESS;
	else
		roccat_report.action = ARVO_ROCCAT_REPORT_ACTION_RELEASE;

	roccat_report_event(arvo->chrdev_minor, (uint8_t const *)&roccat_report,
			sizeof(struct arvo_roccat_report));
}

static int arvo_raw_event(struct hid_device *hdev,
		struct hid_report *report, u8 *data, int size)
{
	struct arvo_device *arvo = hid_get_drvdata(hdev);

	if (size != 3)
		return 0;

	if (arvo->roccat_claimed)
		arvo_report_to_chrdev(arvo, data);

	return 0;
}

static const struct hid_device_id arvo_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_ROCCAT, USB_DEVICE_ID_ROCCAT_ARVO) },
	{ }
};

MODULE_DEVICE_TABLE(hid, arvo_devices);

static struct hid_driver arvo_driver = {
	.name = "arvo",
	.id_table = arvo_devices,
	.probe = arvo_probe,
	.remove = arvo_remove,
	.raw_event = arvo_raw_event
};

static int __init arvo_init(void)
{
	int retval;

	arvo_class = class_create(THIS_MODULE, "arvo");
	if (IS_ERR(arvo_class))
		return PTR_ERR(arvo_class);
	arvo_class->dev_attrs = arvo_attributes;
	arvo_class->dev_bin_attrs = arvo_bin_attributes;

	retval = hid_register_driver(&arvo_driver);
	if (retval)
		class_destroy(arvo_class);
	return retval;
}

static void __exit arvo_exit(void)
{
	class_destroy(arvo_class);
	hid_unregister_driver(&arvo_driver);
}

module_init(arvo_init);
module_exit(arvo_exit);

MODULE_AUTHOR("Stefan Achatz");
MODULE_DESCRIPTION("USB Roccat Arvo driver");
MODULE_LICENSE("GPL v2");
