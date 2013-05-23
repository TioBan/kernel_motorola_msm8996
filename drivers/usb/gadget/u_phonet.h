/*
 * u_phonet.h - interface to Phonet
 *
 * Copyright (C) 2007-2008 by Nokia Corporation
 *
 * This software is distributed under the terms of the GNU General
 * Public License ("GPL") as published by the Free Software Foundation,
 * either version 2 of that License or (at your option) any later version.
 */

#ifndef __U_PHONET_H
#define __U_PHONET_H

#include <linux/usb/composite.h>
#include <linux/usb/cdc.h>

struct net_device *gphonet_setup(struct usb_gadget *gadget);
int phonet_bind_config(struct usb_configuration *c, struct net_device *dev);
void gphonet_cleanup(struct net_device *dev);

#endif /* __U_PHONET_H */
