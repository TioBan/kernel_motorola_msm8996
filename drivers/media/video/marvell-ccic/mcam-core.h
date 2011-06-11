/*
 * Marvell camera core structures.
 *
 * Copyright 2011 Jonathan Corbet corbet@lwn.net
 */

/*
 * Tracking of streaming I/O buffers.
 * FIXME doesn't belong in this file
 */
struct mcam_sio_buffer {
	struct list_head list;
	struct v4l2_buffer v4lbuf;
	char *buffer;   /* Where it lives in kernel space */
	int mapcount;
	struct mcam_camera *cam;
};

enum mcam_state {
	S_NOTREADY,	/* Not yet initialized */
	S_IDLE,		/* Just hanging around */
	S_FLAKED,	/* Some sort of problem */
	S_SINGLEREAD,	/* In read() */
	S_SPECREAD,	/* Speculative read (for future read()) */
	S_STREAMING	/* Streaming data */
};
#define MAX_DMA_BUFS 3

/*
 * A description of one of our devices.
 * Locking: controlled by s_mutex.  Certain fields, however, require
 *          the dev_lock spinlock; they are marked as such by comments.
 *          dev_lock is also required for access to device registers.
 */
struct mcam_camera {
	/*
	 * These fields should be set by the platform code prior to
	 * calling mcam_register().
	 */
	struct i2c_adapter i2c_adapter;
	unsigned char __iomem *regs;
	spinlock_t dev_lock;
	struct device *dev; /* For messages, dma alloc */
	unsigned int chip_id;
	short int clock_speed;	/* Sensor clock speed, default 30 */
	short int use_smbus;	/* SMBUS or straight I2c? */

	/*
	 * Callbacks from the core to the platform code.
	 */
	void (*plat_power_up) (struct mcam_camera *cam);
	void (*plat_power_down) (struct mcam_camera *cam);

	/*
	 * Everything below here is private to the mcam core and
	 * should not be touched by the platform code.
	 */
	struct v4l2_device v4l2_dev;
	enum mcam_state state;
	unsigned long flags;		/* Buffer status, mainly (dev_lock) */
	int users;			/* How many open FDs */
	struct file *owner;		/* Who has data access (v4l2) */

	/*
	 * Subsystem structures.
	 */
	struct video_device vdev;
	struct v4l2_subdev *sensor;
	unsigned short sensor_addr;

	struct list_head dev_list;	/* link to other devices */

	/* DMA buffers */
	unsigned int nbufs;		/* How many are alloc'd */
	int next_buf;			/* Next to consume (dev_lock) */
	unsigned int dma_buf_size;	/* allocated size */
	void *dma_bufs[MAX_DMA_BUFS];	/* Internal buffer addresses */
	dma_addr_t dma_handles[MAX_DMA_BUFS]; /* Buffer bus addresses */
	unsigned int specframes;	/* Unconsumed spec frames (dev_lock) */
	unsigned int sequence;		/* Frame sequence number */
	unsigned int buf_seq[MAX_DMA_BUFS]; /* Sequence for individual buffers */

	/* Streaming buffers */
	unsigned int n_sbufs;		/* How many we have */
	struct mcam_sio_buffer *sb_bufs; /* The array of housekeeping structs */
	struct list_head sb_avail;	/* Available for data (we own) (dev_lock) */
	struct list_head sb_full;	/* With data (user space owns) (dev_lock) */
	struct tasklet_struct s_tasklet;

	/* Current operating parameters */
	u32 sensor_type;		/* Currently ov7670 only */
	struct v4l2_pix_format pix_format;
	enum v4l2_mbus_pixelcode mbus_code;

	/* Locks */
	struct mutex s_mutex; /* Access to this structure */

	/* Misc */
	wait_queue_head_t iowait;	/* Waiting on frame data */
};


/*
 * Register I/O functions.  These are here because the platform code
 * may legitimately need to mess with the register space.
 */
/*
 * Device register I/O
 */
static inline void mcam_reg_write(struct mcam_camera *cam, unsigned int reg,
		unsigned int val)
{
	iowrite32(val, cam->regs + reg);
}

static inline unsigned int mcam_reg_read(struct mcam_camera *cam,
		unsigned int reg)
{
	return ioread32(cam->regs + reg);
}


static inline void mcam_reg_write_mask(struct mcam_camera *cam, unsigned int reg,
		unsigned int val, unsigned int mask)
{
	unsigned int v = mcam_reg_read(cam, reg);

	v = (v & ~mask) | (val & mask);
	mcam_reg_write(cam, reg, v);
}

static inline void mcam_reg_clear_bit(struct mcam_camera *cam,
		unsigned int reg, unsigned int val)
{
	mcam_reg_write_mask(cam, reg, 0, val);
}

static inline void mcam_reg_set_bit(struct mcam_camera *cam,
		unsigned int reg, unsigned int val)
{
	mcam_reg_write_mask(cam, reg, val, val);
}

/*
 * Functions for use by platform code.
 */
int mccic_register(struct mcam_camera *cam);
int mccic_irq(struct mcam_camera *cam, unsigned int irqs);
void mccic_shutdown(struct mcam_camera *cam);
#ifdef CONFIG_PM
void mccic_suspend(struct mcam_camera *cam);
int mccic_resume(struct mcam_camera *cam);
#endif

/*
 * Register definitions for the m88alp01 camera interface.  Offsets in bytes
 * as given in the spec.
 */
#define REG_Y0BAR	0x00
#define REG_Y1BAR	0x04
#define REG_Y2BAR	0x08
/* ... */

#define REG_IMGPITCH	0x24	/* Image pitch register */
#define   IMGP_YP_SHFT	  2		/* Y pitch params */
#define   IMGP_YP_MASK	  0x00003ffc	/* Y pitch field */
#define	  IMGP_UVP_SHFT	  18		/* UV pitch (planar) */
#define   IMGP_UVP_MASK   0x3ffc0000
#define REG_IRQSTATRAW	0x28	/* RAW IRQ Status */
#define   IRQ_EOF0	  0x00000001	/* End of frame 0 */
#define   IRQ_EOF1	  0x00000002	/* End of frame 1 */
#define   IRQ_EOF2	  0x00000004	/* End of frame 2 */
#define   IRQ_SOF0	  0x00000008	/* Start of frame 0 */
#define   IRQ_SOF1	  0x00000010	/* Start of frame 1 */
#define   IRQ_SOF2	  0x00000020	/* Start of frame 2 */
#define   IRQ_OVERFLOW	  0x00000040	/* FIFO overflow */
#define   IRQ_TWSIW	  0x00010000	/* TWSI (smbus) write */
#define   IRQ_TWSIR	  0x00020000	/* TWSI read */
#define   IRQ_TWSIE	  0x00040000	/* TWSI error */
#define   TWSIIRQS (IRQ_TWSIW|IRQ_TWSIR|IRQ_TWSIE)
#define   FRAMEIRQS (IRQ_EOF0|IRQ_EOF1|IRQ_EOF2|IRQ_SOF0|IRQ_SOF1|IRQ_SOF2)
#define   ALLIRQS (TWSIIRQS|FRAMEIRQS|IRQ_OVERFLOW)
#define REG_IRQMASK	0x2c	/* IRQ mask - same bits as IRQSTAT */
#define REG_IRQSTAT	0x30	/* IRQ status / clear */

#define REG_IMGSIZE	0x34	/* Image size */
#define  IMGSZ_V_MASK	  0x1fff0000
#define  IMGSZ_V_SHIFT	  16
#define	 IMGSZ_H_MASK	  0x00003fff
#define REG_IMGOFFSET	0x38	/* IMage offset */

#define REG_CTRL0	0x3c	/* Control 0 */
#define   C0_ENABLE	  0x00000001	/* Makes the whole thing go */

/* Mask for all the format bits */
#define   C0_DF_MASK	  0x00fffffc    /* Bits 2-23 */

/* RGB ordering */
#define	  C0_RGB4_RGBX	  0x00000000
#define	  C0_RGB4_XRGB	  0x00000004
#define	  C0_RGB4_BGRX	  0x00000008
#define	  C0_RGB4_XBGR	  0x0000000c
#define	  C0_RGB5_RGGB	  0x00000000
#define	  C0_RGB5_GRBG	  0x00000004
#define	  C0_RGB5_GBRG	  0x00000008
#define	  C0_RGB5_BGGR	  0x0000000c

/* Spec has two fields for DIN and DOUT, but they must match, so
   combine them here. */
#define	  C0_DF_YUV	  0x00000000	/* Data is YUV	    */
#define	  C0_DF_RGB	  0x000000a0	/* ... RGB		    */
#define	  C0_DF_BAYER	  0x00000140	/* ... Bayer		    */
/* 8-8-8 must be missing from the below - ask */
#define	  C0_RGBF_565	  0x00000000
#define	  C0_RGBF_444	  0x00000800
#define	  C0_RGB_BGR	  0x00001000	/* Blue comes first */
#define	  C0_YUV_PLANAR	  0x00000000	/* YUV 422 planar format */
#define	  C0_YUV_PACKED	  0x00008000	/* YUV 422 packed	*/
#define	  C0_YUV_420PL	  0x0000a000	/* YUV 420 planar	*/
/* Think that 420 packed must be 111 - ask */
#define	  C0_YUVE_YUYV	  0x00000000	/* Y1CbY0Cr		*/
#define	  C0_YUVE_YVYU	  0x00010000	/* Y1CrY0Cb		*/
#define	  C0_YUVE_VYUY	  0x00020000	/* CrY1CbY0		*/
#define	  C0_YUVE_UYVY	  0x00030000	/* CbY1CrY0		*/
#define	  C0_YUVE_XYUV	  0x00000000	/* 420: .YUV		*/
#define	  C0_YUVE_XYVU	  0x00010000	/* 420: .YVU		*/
#define	  C0_YUVE_XUVY	  0x00020000	/* 420: .UVY		*/
#define	  C0_YUVE_XVUY	  0x00030000	/* 420: .VUY		*/
/* Bayer bits 18,19 if needed */
#define	  C0_HPOL_LOW	  0x01000000	/* HSYNC polarity active low */
#define	  C0_VPOL_LOW	  0x02000000	/* VSYNC polarity active low */
#define	  C0_VCLK_LOW	  0x04000000	/* VCLK on falling edge */
#define	  C0_DOWNSCALE	  0x08000000	/* Enable downscaler */
#define	  C0_SIFM_MASK	  0xc0000000	/* SIF mode bits */
#define	  C0_SIF_HVSYNC	  0x00000000	/* Use H/VSYNC */
#define	  CO_SOF_NOSYNC	  0x40000000	/* Use inband active signaling */


#define REG_CTRL1	0x40	/* Control 1 */
#define	  C1_444ALPHA	  0x00f00000	/* Alpha field in RGB444 */
#define	  C1_ALPHA_SHFT	  20
#define	  C1_DMAB32	  0x00000000	/* 32-byte DMA burst */
#define	  C1_DMAB16	  0x02000000	/* 16-byte DMA burst */
#define	  C1_DMAB64	  0x04000000	/* 64-byte DMA burst */
#define	  C1_DMAB_MASK	  0x06000000
#define	  C1_TWOBUFS	  0x08000000	/* Use only two DMA buffers */
#define	  C1_PWRDWN	  0x10000000	/* Power down */

#define REG_CLKCTRL	0x88	/* Clock control */
#define	  CLK_DIV_MASK	  0x0000ffff	/* Upper bits RW "reserved" */

/* This appears to be a Cafe-only register */
#define REG_UBAR	0xc4	/* Upper base address register */

/*
 * Useful stuff that probably belongs somewhere global.
 */
#define VGA_WIDTH	640
#define VGA_HEIGHT	480
