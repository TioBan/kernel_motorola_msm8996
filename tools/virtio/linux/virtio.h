#ifndef LINUX_VIRTIO_H
#define LINUX_VIRTIO_H
#include <linux/scatterlist.h>
#include <linux/kernel.h>

/* TODO: empty stubs for now. Broken but enough for virtio_ring.c */
#define list_add_tail(a, b) do {} while (0)
#define list_del(a) do {} while (0)

#define BIT_WORD(nr)		((nr) / BITS_PER_LONG)
#define BITS_PER_BYTE		8
#define BITS_PER_LONG (sizeof(long) * BITS_PER_BYTE)
#define BIT_MASK(nr)		(1UL << ((nr) % BITS_PER_LONG))

/* TODO: Not atomic as it should be:
 * we don't use this for anything important. */
static inline void clear_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);

	*p &= ~mask;
}

static inline int test_bit(int nr, const volatile unsigned long *addr)
{
        return 1UL & (addr[BIT_WORD(nr)] >> (nr & (BITS_PER_LONG-1)));
}
/* end of stubs */

struct virtio_device {
	void *dev;
	unsigned long features[1];
};

struct virtqueue {
	/* TODO: commented as list macros are empty stubs for now.
	 * Broken but enough for virtio_ring.c
	 * struct list_head list; */
	void (*callback)(struct virtqueue *vq);
	const char *name;
	struct virtio_device *vdev;
        unsigned int index;
        unsigned int num_free;
	void *priv;
};

#define MODULE_LICENSE(__MODULE_LICENSE_value) \
	const char *__MODULE_LICENSE_name = __MODULE_LICENSE_value

/* Interfaces exported by virtio_ring. */
int virtqueue_add_buf(struct virtqueue *vq,
		      struct scatterlist sg[],
		      unsigned int out_num,
		      unsigned int in_num,
		      void *data,
		      gfp_t gfp);

void virtqueue_kick(struct virtqueue *vq);

void *virtqueue_get_buf(struct virtqueue *vq, unsigned int *len);

void virtqueue_disable_cb(struct virtqueue *vq);

bool virtqueue_enable_cb(struct virtqueue *vq);
bool virtqueue_enable_cb_delayed(struct virtqueue *vq);

void *virtqueue_detach_unused_buf(struct virtqueue *vq);
struct virtqueue *vring_new_virtqueue(unsigned int index,
				      unsigned int num,
				      unsigned int vring_align,
				      struct virtio_device *vdev,
				      bool weak_barriers,
				      void *pages,
				      void (*notify)(struct virtqueue *vq),
				      void (*callback)(struct virtqueue *vq),
				      const char *name);
void vring_del_virtqueue(struct virtqueue *vq);

#endif
