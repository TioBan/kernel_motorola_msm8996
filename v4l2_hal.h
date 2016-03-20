/*
 * Copyright (C) 2016 Motorola Mobility LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __V4L2_HAL_H__
#define __V4L2_HAL_H__

#include <linux/ioctl.h>
#include <linux/videodev2.h>

/* structure used for misc -> v4l2_hal direction */
struct misc_stream_resp {
	__u32 stream;
	__s32 result_code;
} __packed;

enum misc_buffer_state {
	MISC_BUFFER_STATE_DONE,
	MISC_BUFFER_STATE_ERROR,
};

struct misc_dequeue_cmd {
	__u32 stream;
	__u32 index;
	__u32 length;
	enum misc_buffer_state state;
} __packed;

struct misc_ioctl_resp {
	__u32 stream;
	__u32 cmd;
	__s32 result_code;
	__u32 pad;
	__u64 data;
} __packed;

/* stream handler associates its file with stream id */
struct misc_set_handler {
	__u32 stream;
} __packed;

#define VIOC_HAL_IFACE_START	_IO('H', 0)
#define VIOC_HAL_IFACE_STOP	_IO('H', 1)
#define VIOC_HAL_STREAM_OPENED	_IOW('H', 2, struct misc_stream_resp)
#define VIOC_HAL_STREAM_CLOSED	_IOW('H', 3, struct misc_stream_resp)
#define VIOC_HAL_STREAM_ON	_IOW('H', 4, struct misc_stream_resp)
#define VIOC_HAL_STREAM_OFF	_IOW('H', 5, struct misc_stream_resp)
#define VIOC_HAL_STREAM_REQBUFS	_IOW('H', 6, struct misc_stream_resp)
#define VIOC_HAL_STREAM_QBUF	_IOW('H', 7, struct misc_stream_resp)
#define VIOC_HAL_STREAM_DQBUF	_IOW('H', 8, struct misc_dequeue_cmd)
#define VIOC_HAL_V4L2_CMD	_IOW('H', 9, struct misc_ioctl_resp)
#define VIOC_HAL_SET_STREAM_HANDLER _IOW('H', 10, struct misc_set_handler)

#define V4L2_HAL_MAX_STREAMS	6

/* Used in capabilities field in v4l2_input structure */
#define V4L2_HAL_IN_STREAM_TYPE_MASK	0x00000007
#define V4L2_HAL_IN_PREVIEW_STREAM	1
#define V4L2_HAL_IN_VIDEO_STREAM	2
#define V4L2_HAL_IN_SNAPSHOT_STREAM	3
#define V4L2_HAL_IN_METADATA_STREAM	4

/* Below are the CIDs used in G_CTRL/S_CTRL to pass around
   ION memory mapped content */
enum {
	V4L2_HAL_EXT_CTRLS,
	V4L2_HAL_MAX_NUM_MMAP_CID,
};

#define V4L2_HAL_CID_SET_EXT_CTRLS_MEM	(V4L2_CID_PRIVATE_BASE + 0)

#define V4L2_HAL_CID_MMAP_MIN		(V4L2_HAL_CID_SET_EXT_CTRLS_MEM + 1)

#define V4L2_HAL_CID_EXT_CTRLS		(V4L2_HAL_CID_MMAP_MIN + \
					 V4L2_HAL_EXT_CTRLS)

#define V4L2_HAL_CID_MMAP_MAX	V4L2_HAL_CID_EXT_CTRLS

static inline bool v4l2_hal_is_set_mapping_cid(__u32 id) {
	if (id == V4L2_HAL_CID_SET_EXT_CTRLS_MEM)
		return true;

	return false;
}

static inline bool v4l2_hal_is_mmap_cid(__u32 id) {
	if (id < V4L2_HAL_CID_MMAP_MIN || id > V4L2_HAL_CID_MMAP_MAX)
		return false;

	return true;
}

/* structure used for V4L2_hal -> misc direction */
struct misc_read_cmd {
	unsigned int stream;
	unsigned int cmd;
	char data[0];
};

struct v4l2_hal_reqbufs_data {
	unsigned int count;
};

struct v4l2_hal_qbuf_data {
	unsigned int index;
	int fd;
	unsigned int length;
};

#endif /* __V4L2_HAL_H__ */
