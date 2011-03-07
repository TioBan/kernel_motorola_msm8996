/******************************************************************************
*                                                                             *
*  easycap_sound.c                                                            *
*                                                                             *
*  Audio driver for EasyCAP USB2.0 Video Capture Device DC60                  *
*                                                                             *
*                                                                             *
******************************************************************************/
/*
 *
 *  Copyright (C) 2010 R.M. Thomas  <rmthomas@sciolus.org>
 *
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
*/
/*****************************************************************************/

#include "easycap.h"
#include "easycap_debug.h"
#include "easycap_sound.h"

/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  ON COMPLETION OF AN AUDIO URB ITS DATA IS COPIED TO THE AUDIO BUFFERS
 *  PROVIDED peasycap->audio_idle IS ZERO.  REGARDLESS OF THIS BEING TRUE,
 *  IT IS RESUBMITTED PROVIDED peasycap->audio_isoc_streaming IS NOT ZERO.
 */
/*---------------------------------------------------------------------------*/
void
easysnd_complete(struct urb *purb)
{
struct easycap *peasycap;
struct data_buffer *paudio_buffer;
__u8 *p1, *p2;
__s16 s16;
int i, j, more, much, leap, rc;
#if defined(UPSAMPLE)
int k;
__s16 oldaudio, newaudio, delta;
#endif /*UPSAMPLE*/

JOT(16, "\n");

if (NULL == purb) {
	SAY("ERROR: purb is NULL\n");
	return;
}
peasycap = purb->context;
if (NULL == peasycap) {
	SAY("ERROR: peasycap is NULL\n");
	return;
}
if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
	SAY("ERROR: bad peasycap\n");
	return;
}

much = 0;

if (peasycap->audio_idle) {
	JOM(16, "%i=audio_idle  %i=audio_isoc_streaming\n", \
			peasycap->audio_idle, peasycap->audio_isoc_streaming);
	if (peasycap->audio_isoc_streaming) {
		rc = usb_submit_urb(purb, GFP_ATOMIC);
		if (0 != rc) {
			if (-ENODEV != rc)
				SAM("ERROR: while %i=audio_idle, " \
					"usb_submit_urb() failed with rc:\n", \
							peasycap->audio_idle);
			switch (rc) {
			case -ENOMEM: {
				SAM("-ENOMEM\n");
				break;
			}
			case -ENODEV: {
				break;
			}
			case -ENXIO: {
				SAM("-ENXIO\n");
				break;
			}
			case -EINVAL: {
				SAM("-EINVAL\n");
				break;
			}
			case -EAGAIN: {
				SAM("-EAGAIN\n");
				break;
			}
			case -EFBIG: {
				SAM("-EFBIG\n");
				break;
			}
			case -EPIPE: {
				SAM("-EPIPE\n");
				break;
			}
			case -EMSGSIZE: {
				SAM("-EMSGSIZE\n");
				break;
			}
			case -ENOSPC: {
				SAM("-ENOSPC\n");
				break;
			}
			default: {
				SAM("unknown error: 0x%08X\n", rc);
				break;
			}
			}
		}
	}
return;
}
/*---------------------------------------------------------------------------*/
if (purb->status) {
	if ((-ESHUTDOWN == purb->status) || (-ENOENT == purb->status)) {
		JOM(16, "urb status -ESHUTDOWN or -ENOENT\n");
		return;
	}
	SAM("ERROR: non-zero urb status:\n");
	switch (purb->status) {
	case -EINPROGRESS: {
		SAM("-EINPROGRESS\n");
		break;
	}
	case -ENOSR: {
		SAM("-ENOSR\n");
		break;
	}
	case -EPIPE: {
		SAM("-EPIPE\n");
		break;
	}
	case -EOVERFLOW: {
		SAM("-EOVERFLOW\n");
		break;
	}
	case -EPROTO: {
		SAM("-EPROTO\n");
		break;
	}
	case -EILSEQ: {
		SAM("-EILSEQ\n");
		break;
	}
	case -ETIMEDOUT: {
		SAM("-ETIMEDOUT\n");
		break;
	}
	case -EMSGSIZE: {
		SAM("-EMSGSIZE\n");
		break;
	}
	case -EOPNOTSUPP: {
		SAM("-EOPNOTSUPP\n");
		break;
	}
	case -EPFNOSUPPORT: {
		SAM("-EPFNOSUPPORT\n");
		break;
	}
	case -EAFNOSUPPORT: {
		SAM("-EAFNOSUPPORT\n");
		break;
	}
	case -EADDRINUSE: {
		SAM("-EADDRINUSE\n");
		break;
	}
	case -EADDRNOTAVAIL: {
		SAM("-EADDRNOTAVAIL\n");
		break;
	}
	case -ENOBUFS: {
		SAM("-ENOBUFS\n");
		break;
	}
	case -EISCONN: {
		SAM("-EISCONN\n");
		break;
	}
	case -ENOTCONN: {
		SAM("-ENOTCONN\n");
		break;
	}
	case -ESHUTDOWN: {
		SAM("-ESHUTDOWN\n");
		break;
	}
	case -ENOENT: {
		SAM("-ENOENT\n");
		break;
	}
	case -ECONNRESET: {
		SAM("-ECONNRESET\n");
		break;
	}
	case -ENOSPC: {
		SAM("ENOSPC\n");
		break;
	}
	default: {
		SAM("unknown error code 0x%08X\n", purb->status);
		break;
	}
	}
/*---------------------------------------------------------------------------*/
/*
 *  RESUBMIT THIS URB AFTER AN ERROR
 *
 *  (THIS IS DUPLICATE CODE TO REDUCE INDENTATION OF THE NO-ERROR PATH)
 */
/*---------------------------------------------------------------------------*/
	if (peasycap->audio_isoc_streaming) {
		rc = usb_submit_urb(purb, GFP_ATOMIC);
		if (0 != rc) {
			SAM("ERROR: while %i=audio_idle, usb_submit_urb() "
				"failed with rc:\n", peasycap->audio_idle);
			switch (rc) {
			case -ENOMEM: {
				SAM("-ENOMEM\n");
				break;
			}
			case -ENODEV: {
				SAM("-ENODEV\n");
				break;
			}
			case -ENXIO: {
				SAM("-ENXIO\n");
				break;
			}
			case -EINVAL: {
				SAM("-EINVAL\n");
				break;
			}
			case -EAGAIN: {
				SAM("-EAGAIN\n");
				break;
			}
			case -EFBIG: {
				SAM("-EFBIG\n");
				break;
			}
			case -EPIPE: {
				SAM("-EPIPE\n");
				break;
			}
			case -EMSGSIZE: {
				SAM("-EMSGSIZE\n");
				break;
			}
			default: {
				SAM("0x%08X\n", rc); break;
			}
			}
		}
	}
	return;
}
/*---------------------------------------------------------------------------*/
/*
 *  PROCEED HERE WHEN NO ERROR
 */
/*---------------------------------------------------------------------------*/
#if defined(UPSAMPLE)
oldaudio = peasycap->oldaudio;
#endif /*UPSAMPLE*/

for (i = 0;  i < purb->number_of_packets; i++) {
	switch (purb->iso_frame_desc[i].status) {
	case  0: {
		break;
	}
	case -ENOENT: {
		SAM("-ENOENT\n");
		break;
	}
	case -EINPROGRESS: {
		SAM("-EINPROGRESS\n");
		break;
	}
	case -EPROTO: {
		SAM("-EPROTO\n");
		break;
	}
	case -EILSEQ: {
		SAM("-EILSEQ\n");
		break;
	}
	case -ETIME: {
		SAM("-ETIME\n");
		break;
	}
	case -ETIMEDOUT: {
		SAM("-ETIMEDOUT\n");
		break;
	}
	case -EPIPE: {
		SAM("-EPIPE\n");
		break;
	}
	case -ECOMM: {
		SAM("-ECOMM\n");
		break;
	}
	case -ENOSR: {
		SAM("-ENOSR\n");
		break;
	}
	case -EOVERFLOW: {
		SAM("-EOVERFLOW\n");
		break;
	}
	case -EREMOTEIO: {
		SAM("-EREMOTEIO\n");
		break;
	}
	case -ENODEV: {
		SAM("-ENODEV\n");
		break;
	}
	case -EXDEV: {
		SAM("-EXDEV\n");
		break;
	}
	case -EINVAL: {
		SAM("-EINVAL\n");
		break;
	}
	case -ECONNRESET: {
		SAM("-ECONNRESET\n");
		break;
	}
	case -ENOSPC: {
		SAM("-ENOSPC\n");
		break;
	}
	case -ESHUTDOWN: {
		SAM("-ESHUTDOWN\n");
		break;
	}
	default: {
		SAM("unknown error:0x%08X\n", purb->iso_frame_desc[i].status);
		break;
	}
	}
	if (!purb->iso_frame_desc[i].status) {
		more = purb->iso_frame_desc[i].actual_length;

#if defined(TESTTONE)
		if (!more)
			more = purb->iso_frame_desc[i].length;
#endif

		if (!more)
			peasycap->audio_mt++;
		else {
			if (peasycap->audio_mt) {
				JOM(16, "%4i empty audio urb frames\n", \
							peasycap->audio_mt);
				peasycap->audio_mt = 0;
			}

			p1 = (__u8 *)(purb->transfer_buffer + \
					purb->iso_frame_desc[i].offset);

			leap = 0;
			p1 += leap;
			more -= leap;
/*---------------------------------------------------------------------------*/
/*
 *  COPY more BYTES FROM ISOC BUFFER TO AUDIO BUFFER,
 *  CONVERTING 8-BIT MONO TO 16-BIT SIGNED LITTLE-ENDIAN SAMPLES IF NECESSARY
 */
/*---------------------------------------------------------------------------*/
			while (more) {
				if (0 > more) {
					SAM("easysnd_complete: MISTAKE: " \
							"more is negative\n");
					return;
				}
				if (peasycap->audio_buffer_page_many <= \
							peasycap->audio_fill) {
					SAM("ERROR: bad " \
						"peasycap->audio_fill\n");
					return;
				}

				paudio_buffer = &peasycap->audio_buffer\
							[peasycap->audio_fill];
				if (PAGE_SIZE < (paudio_buffer->pto - \
						paudio_buffer->pgo)) {
					SAM("ERROR: bad paudio_buffer->pto\n");
					return;
				}
				if (PAGE_SIZE == (paudio_buffer->pto - \
							paudio_buffer->pgo)) {

#if defined(TESTTONE)
					easysnd_testtone(peasycap, \
							peasycap->audio_fill);
#endif /*TESTTONE*/

					paudio_buffer->pto = \
							paudio_buffer->pgo;
					(peasycap->audio_fill)++;
					if (peasycap->\
						audio_buffer_page_many <= \
							peasycap->audio_fill)
						peasycap->audio_fill = 0;

					JOM(12, "bumped peasycap->" \
							"audio_fill to %i\n", \
							peasycap->audio_fill);

					paudio_buffer = &peasycap->\
							audio_buffer\
							[peasycap->audio_fill];
					paudio_buffer->pto = \
							paudio_buffer->pgo;

					if (!(peasycap->audio_fill % \
						peasycap->\
						audio_pages_per_fragment)) {
						JOM(12, "wakeup call on wq_" \
						"audio, %i=frag reading  %i" \
						"=fragment fill\n", \
						(peasycap->audio_read / \
						peasycap->\
						audio_pages_per_fragment), \
						(peasycap->audio_fill / \
						peasycap->\
						audio_pages_per_fragment));
						wake_up_interruptible\
						(&(peasycap->wq_audio));
					}
				}

				much = PAGE_SIZE - (int)(paudio_buffer->pto -\
							 paudio_buffer->pgo);

				if (false == peasycap->microphone) {
					if (much > more)
						much = more;

					memcpy(paudio_buffer->pto, p1, much);
					p1 += much;
					more -= much;
				} else {
#if defined(UPSAMPLE)
					if (much % 16)
						JOM(8, "MISTAKE? much" \
						" is not divisible by 16\n");
					if (much > (16 * \
							more))
						much = 16 * \
							more;
					p2 = (__u8 *)paudio_buffer->pto;

					for (j = 0;  j < (much/16);  j++) {
						newaudio =  ((int) *p1) - 128;
						newaudio = 128 * \
								newaudio;

						delta = (newaudio - oldaudio) \
									/ 4;
						s16 = oldaudio + delta;

						for (k = 0;  k < 4;  k++) {
							*p2 = (0x00FF & s16);
							*(p2 + 1) = (0xFF00 & \
								s16) >> 8;
							p2 += 2;
							*p2 = (0x00FF & s16);
							*(p2 + 1) = (0xFF00 & \
								s16) >> 8;
							p2 += 2;

							s16 += delta;
						}
						p1++;
						more--;
						oldaudio = s16;
					}
#else
					if (much > (2 * more))
						much = 2 * more;
					p2 = (__u8 *)paudio_buffer->pto;

					for (j = 0;  j < (much / 2);  j++) {
						s16 =  ((int) *p1) - 128;
						s16 = 128 * \
								s16;
						*p2 = (0x00FF & s16);
						*(p2 + 1) = (0xFF00 & s16) >> \
									8;
						p1++;  p2 += 2;
						more--;
					}
#endif /*UPSAMPLE*/
				}
				(paudio_buffer->pto) += much;
			}
		}
	} else {
		JOM(12, "discarding audio samples because " \
			"%i=purb->iso_frame_desc[i].status\n", \
				purb->iso_frame_desc[i].status);
	}

#if defined(UPSAMPLE)
peasycap->oldaudio = oldaudio;
#endif /*UPSAMPLE*/

}
/*---------------------------------------------------------------------------*/
/*
 *  RESUBMIT THIS URB AFTER NO ERROR
 */
/*---------------------------------------------------------------------------*/
if (peasycap->audio_isoc_streaming) {
	rc = usb_submit_urb(purb, GFP_ATOMIC);
	if (0 != rc) {
		if (-ENODEV != rc) {
			SAM("ERROR: while %i=audio_idle, " \
					"usb_submit_urb() failed " \
					"with rc:\n", peasycap->audio_idle);
		}
		switch (rc) {
		case -ENOMEM: {
			SAM("-ENOMEM\n");
			break;
		}
		case -ENODEV: {
			break;
		}
		case -ENXIO: {
			SAM("-ENXIO\n");
			break;
		}
		case -EINVAL: {
			SAM("-EINVAL\n");
			break;
		}
		case -EAGAIN: {
			SAM("-EAGAIN\n");
			break;
		}
		case -EFBIG: {
			SAM("-EFBIG\n");
			break;
		}
		case -EPIPE: {
			SAM("-EPIPE\n");
			break;
		}
		case -EMSGSIZE: {
			SAM("-EMSGSIZE\n");
			break;
		}
		case -ENOSPC: {
			SAM("-ENOSPC\n");
			break;
		}
		default: {
			SAM("unknown error: 0x%08X\n", rc);
			break;
		}
		}
	}
}
return;
}
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  THE AUDIO URBS ARE SUBMITTED AT THIS EARLY STAGE SO THAT IT IS POSSIBLE TO
 *  STREAM FROM /dev/easysnd1 WITH SIMPLE PROGRAMS SUCH AS cat WHICH DO NOT
 *  HAVE AN IOCTL INTERFACE.
 */
/*---------------------------------------------------------------------------*/
int
easysnd_open(struct inode *inode, struct file *file)
{
struct usb_interface *pusb_interface;
struct easycap *peasycap;
int subminor, rc;
/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/
#if defined(EASYCAP_IS_VIDEODEV_CLIENT)
#if defined(EASYCAP_NEEDS_V4L2_DEVICE_H)
struct v4l2_device *pv4l2_device;
#endif /*EASYCAP_NEEDS_V4L2_DEVICE_H*/
#endif /*EASYCAP_IS_VIDEODEV_CLIENT*/
/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/

JOT(4, "begins\n");

subminor = iminor(inode);

pusb_interface = usb_find_interface(&easycap_usb_driver, subminor);
if (NULL == pusb_interface) {
	SAY("ERROR: pusb_interface is NULL\n");
	SAY("ending unsuccessfully\n");
	return -1;
}
peasycap = usb_get_intfdata(pusb_interface);
if (NULL == peasycap) {
	SAY("ERROR: peasycap is NULL\n");
	SAY("ending unsuccessfully\n");
	return -1;
}
/*---------------------------------------------------------------------------*/
#if (!defined(EASYCAP_IS_VIDEODEV_CLIENT))
#
/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/
#else
#if defined(EASYCAP_NEEDS_V4L2_DEVICE_H)
/*---------------------------------------------------------------------------*/
/*
 *  SOME VERSIONS OF THE videodev MODULE OVERWRITE THE DATA WHICH HAS
 *  BEEN WRITTEN BY THE CALL TO usb_set_intfdata() IN easycap_usb_probe(),
 *  REPLACING IT WITH A POINTER TO THE EMBEDDED v4l2_device STRUCTURE.
 *  TO DETECT THIS, THE STRING IN THE easycap.telltale[] BUFFER IS CHECKED.
*/
/*---------------------------------------------------------------------------*/
if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
	pv4l2_device = usb_get_intfdata(pusb_interface);
	if ((struct v4l2_device *)NULL == pv4l2_device) {
		SAY("ERROR: pv4l2_device is NULL\n");
		return -EFAULT;
	}
	peasycap = (struct easycap *) \
		container_of(pv4l2_device, struct easycap, v4l2_device);
}
#endif /*EASYCAP_NEEDS_V4L2_DEVICE_H*/
#
#endif /*EASYCAP_IS_VIDEODEV_CLIENT*/
/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
/*---------------------------------------------------------------------------*/
if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
	SAY("ERROR: bad peasycap: 0x%08lX\n", (unsigned long int) peasycap);
	return -EFAULT;
}
/*---------------------------------------------------------------------------*/

file->private_data = peasycap;

/*---------------------------------------------------------------------------*/
/*
 *  INITIALIZATION
 */
/*---------------------------------------------------------------------------*/
JOM(4, "starting initialization\n");

if ((struct usb_device *)NULL == peasycap->pusb_device) {
	SAM("ERROR: peasycap->pusb_device is NULL\n");
	return -ENODEV;
}
JOM(16, "0x%08lX=peasycap->pusb_device\n", (long int)peasycap->pusb_device);

rc = audio_setup(peasycap);
if (0 <= rc)
	JOM(8, "audio_setup() returned %i\n", rc);
else
	JOM(8, "easysnd open(): ERROR: audio_setup() returned %i\n", rc);

if ((struct usb_device *)NULL == peasycap->pusb_device) {
	SAM("ERROR: peasycap->pusb_device has become NULL\n");
	return -ENODEV;
}
/*---------------------------------------------------------------------------*/
if ((struct usb_device *)NULL == peasycap->pusb_device) {
	SAM("ERROR: peasycap->pusb_device has become NULL\n");
	return -ENODEV;
}
rc = usb_set_interface(peasycap->pusb_device, peasycap->audio_interface, \
					peasycap->audio_altsetting_on);
JOM(8, "usb_set_interface(.,%i,%i) returned %i\n", peasycap->audio_interface, \
					peasycap->audio_altsetting_on, rc);

rc = wakeup_device(peasycap->pusb_device);
if (0 == rc)
	JOM(8, "wakeup_device() returned %i\n", rc);
else
	JOM(8, "ERROR: wakeup_device() returned %i\n", rc);

peasycap->audio_eof = 0;
peasycap->audio_idle = 0;

peasycap->timeval1.tv_sec  = 0;
peasycap->timeval1.tv_usec = 0;

submit_audio_urbs(peasycap);

JOM(4, "finished initialization\n");
return 0;
}
/*****************************************************************************/
int
easysnd_release(struct inode *inode, struct file *file)
{
struct easycap *peasycap;

JOT(4, "begins\n");

peasycap = file->private_data;
if (NULL == peasycap) {
	SAY("ERROR:  peasycap is NULL.\n");
	return -EFAULT;
}
if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
	SAY("ERROR: bad peasycap: 0x%08lX\n", (unsigned long int) peasycap);
	return -EFAULT;
}
if (0 != kill_audio_urbs(peasycap)) {
	SAM("ERROR: kill_audio_urbs() failed\n");
	return -EFAULT;
}
JOM(4, "ending successfully\n");
return 0;
}
/*****************************************************************************/
ssize_t
easysnd_read(struct file *file, char __user *puserspacebuffer, \
						size_t kount, loff_t *poff)
{
struct timeval timeval;
long long int above, below, mean;
struct signed_div_result sdr;
unsigned char *p0;
long int kount1, more, rc, l0, lm;
int fragment, kd;
struct easycap *peasycap;
struct data_buffer *pdata_buffer;
size_t szret;

/*---------------------------------------------------------------------------*/
/*
 *  DO A BLOCKING READ TO TRANSFER DATA TO USER SPACE.
 *
 ******************************************************************************
 *****  N.B.  IF THIS FUNCTION RETURNS 0, NOTHING IS SEEN IN USER SPACE. ******
 *****        THIS CONDITION SIGNIFIES END-OF-FILE.                      ******
 ******************************************************************************
 */
/*---------------------------------------------------------------------------*/

JOT(8, "===== easysnd_read(): kount=%i, *poff=%i\n", (int)kount, (int)(*poff));

if (NULL == file) {
	SAY("ERROR:  file is NULL\n");
	return -ERESTARTSYS;
}
peasycap = file->private_data;
if (NULL == peasycap) {
	SAY("ERROR in easysnd_read(): peasycap is NULL\n");
	return -EFAULT;
}
if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
	SAY("ERROR: bad peasycap: 0x%08lX\n", (unsigned long int) peasycap);
	return -EFAULT;
}
if (NULL == peasycap->pusb_device) {
	SAY("ERROR in easysnd_read(): peasycap->pusb_device is NULL\n");
	return -EFAULT;
}
kd = isdongle(peasycap);
if (0 <= kd && DONGLE_MANY > kd) {
	if (mutex_lock_interruptible(&(easycap_dongle[kd].mutex_audio))) {
		SAY("ERROR: cannot lock easycap_dongle[%i].mutex_audio\n", kd);
		return -ERESTARTSYS;
	}
	JOM(4, "locked easycap_dongle[%i].mutex_audio\n", kd);
/*---------------------------------------------------------------------------*/
/*
 *  MEANWHILE, easycap_usb_disconnect() MAY HAVE FREED POINTER peasycap,
 *  IN WHICH CASE A REPEAT CALL TO isdongle() WILL FAIL.
 *  IF NECESSARY, BAIL OUT.
*/
/*---------------------------------------------------------------------------*/
	if (kd != isdongle(peasycap))
		return -ERESTARTSYS;
	if (NULL == file) {
		SAY("ERROR:  file is NULL\n");
		mutex_unlock(&easycap_dongle[kd].mutex_audio);
		return -ERESTARTSYS;
	}
	peasycap = file->private_data;
	if (NULL == peasycap) {
		SAY("ERROR:  peasycap is NULL\n");
		mutex_unlock(&easycap_dongle[kd].mutex_audio);
		return -ERESTARTSYS;
	}
	if (memcmp(&peasycap->telltale[0], TELLTALE, strlen(TELLTALE))) {
		SAY("ERROR: bad peasycap: 0x%08lX\n", \
						(unsigned long int) peasycap);
		mutex_unlock(&easycap_dongle[kd].mutex_audio);
		return -ERESTARTSYS;
	}
	if (NULL == peasycap->pusb_device) {
		SAM("ERROR: peasycap->pusb_device is NULL\n");
		mutex_unlock(&easycap_dongle[kd].mutex_audio);
		return -ERESTARTSYS;
	}
} else {
/*---------------------------------------------------------------------------*/
/*
 *  IF easycap_usb_disconnect() HAS ALREADY FREED POINTER peasycap BEFORE THE
 *  ATTEMPT TO ACQUIRE THE SEMAPHORE, isdongle() WILL HAVE FAILED.  BAIL OUT.
*/
/*---------------------------------------------------------------------------*/
	return -ERESTARTSYS;
}
/*---------------------------------------------------------------------------*/
if (file->f_flags & O_NONBLOCK)
	JOT(16, "NONBLOCK  kount=%i, *poff=%i\n", (int)kount, (int)(*poff));
else
	JOT(8, "BLOCKING  kount=%i, *poff=%i\n", (int)kount, (int)(*poff));

if ((0 > peasycap->audio_read) || \
		(peasycap->audio_buffer_page_many <= peasycap->audio_read)) {
	SAM("ERROR: peasycap->audio_read out of range\n");
	mutex_unlock(&easycap_dongle[kd].mutex_audio);
	return -EFAULT;
}
pdata_buffer = &peasycap->audio_buffer[peasycap->audio_read];
if ((struct data_buffer *)NULL == pdata_buffer) {
	SAM("ERROR: pdata_buffer is NULL\n");
	mutex_unlock(&easycap_dongle[kd].mutex_audio);
	return -EFAULT;
}
JOM(12, "before wait, %i=frag read  %i=frag fill\n", \
		(peasycap->audio_read / peasycap->audio_pages_per_fragment), \
		(peasycap->audio_fill / peasycap->audio_pages_per_fragment));
fragment = (peasycap->audio_read / peasycap->audio_pages_per_fragment);
while ((fragment == (peasycap->audio_fill / \
				peasycap->audio_pages_per_fragment)) || \
		(0 == (PAGE_SIZE - (pdata_buffer->pto - pdata_buffer->pgo)))) {
	if (file->f_flags & O_NONBLOCK) {
		JOM(16, "returning -EAGAIN as instructed\n");
		mutex_unlock(&easycap_dongle[kd].mutex_audio);
		return -EAGAIN;
	}
	rc = wait_event_interruptible(peasycap->wq_audio, \
		(peasycap->audio_idle  || peasycap->audio_eof   || \
		((fragment != (peasycap->audio_fill / \
				peasycap->audio_pages_per_fragment)) && \
		(0 < (PAGE_SIZE - (pdata_buffer->pto - pdata_buffer->pgo))))));
	if (0 != rc) {
		SAM("aborted by signal\n");
		mutex_unlock(&easycap_dongle[kd].mutex_audio);
		return -ERESTARTSYS;
	}
	if (peasycap->audio_eof) {
		JOM(8, "returning 0 because  %i=audio_eof\n", \
							peasycap->audio_eof);
		kill_audio_urbs(peasycap);
		mutex_unlock(&easycap_dongle[kd].mutex_audio);
		return 0;
	}
	if (peasycap->audio_idle) {
		JOM(16, "returning 0 because  %i=audio_idle\n", \
							peasycap->audio_idle);
		mutex_unlock(&easycap_dongle[kd].mutex_audio);
		return 0;
	}
	if (!peasycap->audio_isoc_streaming) {
		JOM(16, "returning 0 because audio urbs not streaming\n");
		mutex_unlock(&easycap_dongle[kd].mutex_audio);
		return 0;
	}
}
JOM(12, "after  wait, %i=frag read  %i=frag fill\n", \
		(peasycap->audio_read / peasycap->audio_pages_per_fragment), \
		(peasycap->audio_fill / peasycap->audio_pages_per_fragment));
szret = (size_t)0;
while (fragment == (peasycap->audio_read / \
				peasycap->audio_pages_per_fragment)) {
	if (NULL == pdata_buffer->pgo) {
		SAM("ERROR: pdata_buffer->pgo is NULL\n");
		mutex_unlock(&easycap_dongle[kd].mutex_audio);
		return -EFAULT;
	}
	if (NULL == pdata_buffer->pto) {
		SAM("ERROR: pdata_buffer->pto is NULL\n");
		mutex_unlock(&easycap_dongle[kd].mutex_audio);
		return -EFAULT;
	}
	kount1 = PAGE_SIZE - (pdata_buffer->pto - pdata_buffer->pgo);
	if (0 > kount1) {
		SAM("easysnd_read: MISTAKE: kount1 is negative\n");
		mutex_unlock(&easycap_dongle[kd].mutex_audio);
		return -ERESTARTSYS;
	}
	if (!kount1) {
		(peasycap->audio_read)++;
		if (peasycap->audio_buffer_page_many <= peasycap->audio_read)
			peasycap->audio_read = 0;
		JOM(12, "bumped peasycap->audio_read to %i\n", \
						peasycap->audio_read);

		if (fragment != (peasycap->audio_read / \
					peasycap->audio_pages_per_fragment))
			break;

		if ((0 > peasycap->audio_read) || \
			(peasycap->audio_buffer_page_many <= \
					peasycap->audio_read)) {
			SAM("ERROR: peasycap->audio_read out of range\n");
			mutex_unlock(&easycap_dongle[kd].mutex_audio);
			return -EFAULT;
		}
		pdata_buffer = &peasycap->audio_buffer[peasycap->audio_read];
		if ((struct data_buffer *)NULL == pdata_buffer) {
			SAM("ERROR: pdata_buffer is NULL\n");
			mutex_unlock(&easycap_dongle[kd].mutex_audio);
			return -EFAULT;
		}
		if (NULL == pdata_buffer->pgo) {
			SAM("ERROR: pdata_buffer->pgo is NULL\n");
			mutex_unlock(&easycap_dongle[kd].mutex_audio);
			return -EFAULT;
		}
		if (NULL == pdata_buffer->pto) {
			SAM("ERROR: pdata_buffer->pto is NULL\n");
			mutex_unlock(&easycap_dongle[kd].mutex_audio);
			return -EFAULT;
		}
		kount1 = PAGE_SIZE - (pdata_buffer->pto - pdata_buffer->pgo);
	}
	JOM(12, "ready  to send %li bytes\n", (long int) kount1);
	JOM(12, "still  to send %li bytes\n", (long int) kount);
	more = kount1;
	if (more > kount)
		more = kount;
	JOM(12, "agreed to send %li bytes from page %i\n", \
						more, peasycap->audio_read);
	if (!more)
		break;

/*---------------------------------------------------------------------------*/
/*
 *  ACCUMULATE DYNAMIC-RANGE INFORMATION
 */
/*---------------------------------------------------------------------------*/
	p0 = (unsigned char *)pdata_buffer->pgo;  l0 = 0;  lm = more/2;
	while (l0 < lm) {
		SUMMER(p0, &peasycap->audio_sample, &peasycap->audio_niveau, \
				&peasycap->audio_square);  l0++;  p0 += 2;
	}
/*---------------------------------------------------------------------------*/
	rc = copy_to_user(puserspacebuffer, pdata_buffer->pto, more);
	if (0 != rc) {
		SAM("ERROR: copy_to_user() returned %li\n", rc);
		mutex_unlock(&easycap_dongle[kd].mutex_audio);
		return -EFAULT;
	}
	*poff += (loff_t)more;
	szret += (size_t)more;
	pdata_buffer->pto += more;
	puserspacebuffer += more;
	kount -= (size_t)more;
}
JOM(12, "after  read, %i=frag read  %i=frag fill\n", \
		(peasycap->audio_read / peasycap->audio_pages_per_fragment), \
		(peasycap->audio_fill / peasycap->audio_pages_per_fragment));
if (kount < 0) {
	SAM("MISTAKE:  %li=kount  %li=szret\n", \
					(long int)kount, (long int)szret);
}
/*---------------------------------------------------------------------------*/
/*
 *  CALCULATE DYNAMIC RANGE FOR (VAPOURWARE) AUTOMATIC VOLUME CONTROL
 */
/*---------------------------------------------------------------------------*/
if (peasycap->audio_sample) {
	below = peasycap->audio_sample;
	above = peasycap->audio_square;
	sdr = signed_div(above, below);
	above = sdr.quotient;
	mean = peasycap->audio_niveau;
	sdr = signed_div(mean, peasycap->audio_sample);

	JOM(8, "%8lli=mean  %8lli=meansquare after %lli samples, =>\n", \
				sdr.quotient, above, peasycap->audio_sample);

	sdr = signed_div(above, 32768);
	JOM(8, "audio dynamic range is roughly %lli\n", sdr.quotient);
}
/*---------------------------------------------------------------------------*/
/*
 *  UPDATE THE AUDIO CLOCK
 */
/*---------------------------------------------------------------------------*/
do_gettimeofday(&timeval);
if (!peasycap->timeval1.tv_sec) {
	peasycap->audio_bytes = 0;
	peasycap->timeval3 = timeval;
	peasycap->timeval1 = peasycap->timeval3;
	sdr.quotient = 192000;
} else {
	peasycap->audio_bytes += (long long int) szret;
	below = ((long long int)(1000000)) * \
		((long long int)(timeval.tv_sec  - \
						peasycap->timeval3.tv_sec)) + \
		(long long int)(timeval.tv_usec - peasycap->timeval3.tv_usec);
	above = 1000000 * ((long long int) peasycap->audio_bytes);

	if (below)
		sdr = signed_div(above, below);
	else
		sdr.quotient = 192000;
}
JOM(8, "audio streaming at %lli bytes/second\n", sdr.quotient);
peasycap->dnbydt = sdr.quotient;

JOM(8, "returning %li\n", (long int)szret);
mutex_unlock(&easycap_dongle[kd].mutex_audio);
return szret;
}
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  SUBMIT ALL AUDIO URBS.
 */
/*---------------------------------------------------------------------------*/
int
submit_audio_urbs(struct easycap *peasycap)
{
struct data_urb *pdata_urb;
struct urb *purb;
struct list_head *plist_head;
int j, isbad, nospc, m, rc;
int isbuf;

if (NULL == peasycap) {
	SAY("ERROR: peasycap is NULL\n");
	return -EFAULT;
}
if ((struct list_head *)NULL == peasycap->purb_audio_head) {
	SAM("ERROR: peasycap->urb_audio_head uninitialized\n");
	return -EFAULT;
}
if ((struct usb_device *)NULL == peasycap->pusb_device) {
	SAM("ERROR: peasycap->pusb_device is NULL\n");
	return -EFAULT;
}
if (!peasycap->audio_isoc_streaming) {
	JOM(4, "initial submission of all audio urbs\n");
	rc = usb_set_interface(peasycap->pusb_device,
					peasycap->audio_interface, \
					peasycap->audio_altsetting_on);
	JOM(8, "usb_set_interface(.,%i,%i) returned %i\n", \
					peasycap->audio_interface, \
					peasycap->audio_altsetting_on, rc);

	isbad = 0;  nospc = 0;  m = 0;
	list_for_each(plist_head, (peasycap->purb_audio_head)) {
		pdata_urb = list_entry(plist_head, struct data_urb, list_head);
		if (NULL != pdata_urb) {
			purb = pdata_urb->purb;
			if (NULL != purb) {
				isbuf = pdata_urb->isbuf;

				purb->interval = 1;
				purb->dev = peasycap->pusb_device;
				purb->pipe = \
					usb_rcvisocpipe(peasycap->pusb_device,\
					peasycap->audio_endpointnumber);
				purb->transfer_flags = URB_ISO_ASAP;
				purb->transfer_buffer = \
					peasycap->audio_isoc_buffer[isbuf].pgo;
				purb->transfer_buffer_length = \
					peasycap->audio_isoc_buffer_size;
				purb->complete = easysnd_complete;
				purb->context = peasycap;
				purb->start_frame = 0;
				purb->number_of_packets = \
					peasycap->audio_isoc_framesperdesc;
				for (j = 0;  j < peasycap->\
						audio_isoc_framesperdesc; \
									j++) {
					purb->iso_frame_desc[j].offset = j * \
						peasycap->\
						audio_isoc_maxframesize;
					purb->iso_frame_desc[j].length = \
						peasycap->\
						audio_isoc_maxframesize;
				}

				rc = usb_submit_urb(purb, GFP_KERNEL);
				if (0 != rc) {
					isbad++;
					SAM("ERROR: usb_submit_urb() failed" \
							" for urb with rc:\n");
					switch (rc) {
					case -ENOMEM: {
						SAM("-ENOMEM\n");
						break;
					}
					case -ENODEV: {
						SAM("-ENODEV\n");
						break;
					}
					case -ENXIO: {
						SAM("-ENXIO\n");
						break;
					}
					case -EINVAL: {
						SAM("-EINVAL\n");
						break;
					}
					case -EAGAIN: {
						SAM("-EAGAIN\n");
						break;
					}
					case -EFBIG: {
						SAM("-EFBIG\n");
						break;
					}
					case -EPIPE: {
						SAM("-EPIPE\n");
						break;
					}
					case -EMSGSIZE: {
						SAM("-EMSGSIZE\n");
						break;
					}
					case -ENOSPC: {
						nospc++;
						break;
					}
					default: {
						SAM("unknown error code %i\n",\
								 rc);
						break;
					}
					}
				} else {
					 m++;
				}
			} else {
				isbad++;
			}
		} else {
			isbad++;
		}
	}
	if (nospc) {
		SAM("-ENOSPC=usb_submit_urb() for %i urbs\n", nospc);
		SAM(".....  possibly inadequate USB bandwidth\n");
		peasycap->audio_eof = 1;
	}
	if (isbad) {
		JOM(4, "attempting cleanup instead of submitting\n");
		list_for_each(plist_head, (peasycap->purb_audio_head)) {
			pdata_urb = list_entry(plist_head, struct data_urb, \
								list_head);
			if (NULL != pdata_urb) {
				purb = pdata_urb->purb;
				if (NULL != purb)
					usb_kill_urb(purb);
			}
		}
		peasycap->audio_isoc_streaming = 0;
	} else {
		peasycap->audio_isoc_streaming = 1;
		JOM(4, "submitted %i audio urbs\n", m);
	}
} else
	JOM(4, "already streaming audio urbs\n");

return 0;
}
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  KILL ALL AUDIO URBS.
 */
/*---------------------------------------------------------------------------*/
int
kill_audio_urbs(struct easycap *peasycap)
{
int m;
struct list_head *plist_head;
struct data_urb *pdata_urb;

if (NULL == peasycap) {
	SAY("ERROR: peasycap is NULL\n");
	return -EFAULT;
}
if (peasycap->audio_isoc_streaming) {
	if ((struct list_head *)NULL != peasycap->purb_audio_head) {
		peasycap->audio_isoc_streaming = 0;
		JOM(4, "killing audio urbs\n");
		m = 0;
		list_for_each(plist_head, (peasycap->purb_audio_head)) {
			pdata_urb = list_entry(plist_head, struct data_urb,
								list_head);
			if ((struct data_urb *)NULL != pdata_urb) {
				if ((struct urb *)NULL != pdata_urb->purb) {
					usb_kill_urb(pdata_urb->purb);
					m++;
				}
			}
		}
		JOM(4, "%i audio urbs killed\n", m);
	} else {
		SAM("ERROR: peasycap->purb_audio_head is NULL\n");
		return -EFAULT;
	}
} else {
	JOM(8, "%i=audio_isoc_streaming, no audio urbs killed\n", \
					peasycap->audio_isoc_streaming);
}
return 0;
}
/*****************************************************************************/
