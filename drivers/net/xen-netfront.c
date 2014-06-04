/*
 * Virtual network driver for conversing with remote driver backends.
 *
 * Copyright (c) 2002-2005, K A Fraser
 * Copyright (c) 2005, XenSource Ltd
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation; or, when distributed
 * separately from the Linux kernel or incorporated into other
 * software packages, subject to the following license:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this source file (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <linux/if_ether.h>
#include <net/tcp.h>
#include <linux/udp.h>
#include <linux/moduleparam.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <net/ip.h>

#include <asm/xen/page.h>
#include <xen/xen.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include <xen/page.h>
#include <xen/platform_pci.h>
#include <xen/grant_table.h>

#include <xen/interface/io/netif.h>
#include <xen/interface/memory.h>
#include <xen/interface/grant_table.h>

static const struct ethtool_ops xennet_ethtool_ops;

struct netfront_cb {
	int pull_to;
};

#define NETFRONT_SKB_CB(skb)	((struct netfront_cb *)((skb)->cb))

#define RX_COPY_THRESHOLD 256

#define GRANT_INVALID_REF	0

#define NET_TX_RING_SIZE __CONST_RING_SIZE(xen_netif_tx, PAGE_SIZE)
#define NET_RX_RING_SIZE __CONST_RING_SIZE(xen_netif_rx, PAGE_SIZE)
#define TX_MAX_TARGET min_t(int, NET_TX_RING_SIZE, 256)

/* Queue name is interface name with "-qNNN" appended */
#define QUEUE_NAME_SIZE (IFNAMSIZ + 6)

/* IRQ name is queue name with "-tx" or "-rx" appended */
#define IRQ_NAME_SIZE (QUEUE_NAME_SIZE + 3)

struct netfront_stats {
	u64			rx_packets;
	u64			tx_packets;
	u64			rx_bytes;
	u64			tx_bytes;
	struct u64_stats_sync	syncp;
};

struct netfront_info;

struct netfront_queue {
	unsigned int id; /* Queue ID, 0-based */
	char name[QUEUE_NAME_SIZE]; /* DEVNAME-qN */
	struct netfront_info *info;

	struct napi_struct napi;

	/* Split event channels support, tx_* == rx_* when using
	 * single event channel.
	 */
	unsigned int tx_evtchn, rx_evtchn;
	unsigned int tx_irq, rx_irq;
	/* Only used when split event channels support is enabled */
	char tx_irq_name[IRQ_NAME_SIZE]; /* DEVNAME-qN-tx */
	char rx_irq_name[IRQ_NAME_SIZE]; /* DEVNAME-qN-rx */

	spinlock_t   tx_lock;
	struct xen_netif_tx_front_ring tx;
	int tx_ring_ref;

	/*
	 * {tx,rx}_skbs store outstanding skbuffs. Free tx_skb entries
	 * are linked from tx_skb_freelist through skb_entry.link.
	 *
	 *  NB. Freelist index entries are always going to be less than
	 *  PAGE_OFFSET, whereas pointers to skbs will always be equal or
	 *  greater than PAGE_OFFSET: we use this property to distinguish
	 *  them.
	 */
	union skb_entry {
		struct sk_buff *skb;
		unsigned long link;
	} tx_skbs[NET_TX_RING_SIZE];
	grant_ref_t gref_tx_head;
	grant_ref_t grant_tx_ref[NET_TX_RING_SIZE];
	struct page *grant_tx_page[NET_TX_RING_SIZE];
	unsigned tx_skb_freelist;

	spinlock_t   rx_lock ____cacheline_aligned_in_smp;
	struct xen_netif_rx_front_ring rx;
	int rx_ring_ref;

	/* Receive-ring batched refills. */
#define RX_MIN_TARGET 8
#define RX_DFL_MIN_TARGET 64
#define RX_MAX_TARGET min_t(int, NET_RX_RING_SIZE, 256)
	unsigned rx_min_target, rx_max_target, rx_target;
	struct sk_buff_head rx_batch;

	struct timer_list rx_refill_timer;

	struct sk_buff *rx_skbs[NET_RX_RING_SIZE];
	grant_ref_t gref_rx_head;
	grant_ref_t grant_rx_ref[NET_RX_RING_SIZE];

	unsigned long rx_pfn_array[NET_RX_RING_SIZE];
	struct multicall_entry rx_mcl[NET_RX_RING_SIZE+1];
	struct mmu_update rx_mmu[NET_RX_RING_SIZE];
};

struct netfront_info {
	struct list_head list;
	struct net_device *netdev;

	struct xenbus_device *xbdev;

	/* Multi-queue support */
	struct netfront_queue *queues;

	/* Statistics */
	struct netfront_stats __percpu *stats;

	atomic_t rx_gso_checksum_fixup;
};

struct netfront_rx_info {
	struct xen_netif_rx_response rx;
	struct xen_netif_extra_info extras[XEN_NETIF_EXTRA_TYPE_MAX - 1];
};

static void skb_entry_set_link(union skb_entry *list, unsigned short id)
{
	list->link = id;
}

static int skb_entry_is_link(const union skb_entry *list)
{
	BUILD_BUG_ON(sizeof(list->skb) != sizeof(list->link));
	return (unsigned long)list->skb < PAGE_OFFSET;
}

/*
 * Access macros for acquiring freeing slots in tx_skbs[].
 */

static void add_id_to_freelist(unsigned *head, union skb_entry *list,
			       unsigned short id)
{
	skb_entry_set_link(&list[id], *head);
	*head = id;
}

static unsigned short get_id_from_freelist(unsigned *head,
					   union skb_entry *list)
{
	unsigned int id = *head;
	*head = list[id].link;
	return id;
}

static int xennet_rxidx(RING_IDX idx)
{
	return idx & (NET_RX_RING_SIZE - 1);
}

static struct sk_buff *xennet_get_rx_skb(struct netfront_queue *queue,
					 RING_IDX ri)
{
	int i = xennet_rxidx(ri);
	struct sk_buff *skb = queue->rx_skbs[i];
	queue->rx_skbs[i] = NULL;
	return skb;
}

static grant_ref_t xennet_get_rx_ref(struct netfront_queue *queue,
					    RING_IDX ri)
{
	int i = xennet_rxidx(ri);
	grant_ref_t ref = queue->grant_rx_ref[i];
	queue->grant_rx_ref[i] = GRANT_INVALID_REF;
	return ref;
}

#ifdef CONFIG_SYSFS
static int xennet_sysfs_addif(struct net_device *netdev);
static void xennet_sysfs_delif(struct net_device *netdev);
#else /* !CONFIG_SYSFS */
#define xennet_sysfs_addif(dev) (0)
#define xennet_sysfs_delif(dev) do { } while (0)
#endif

static bool xennet_can_sg(struct net_device *dev)
{
	return dev->features & NETIF_F_SG;
}


static void rx_refill_timeout(unsigned long data)
{
	struct netfront_queue *queue = (struct netfront_queue *)data;
	napi_schedule(&queue->napi);
}

static int netfront_tx_slot_available(struct netfront_queue *queue)
{
	return (queue->tx.req_prod_pvt - queue->tx.rsp_cons) <
		(TX_MAX_TARGET - MAX_SKB_FRAGS - 2);
}

static void xennet_maybe_wake_tx(struct netfront_queue *queue)
{
	struct net_device *dev = queue->info->netdev;
	struct netdev_queue *dev_queue = netdev_get_tx_queue(dev, queue->id);

	if (unlikely(netif_tx_queue_stopped(dev_queue)) &&
	    netfront_tx_slot_available(queue) &&
	    likely(netif_running(dev)))
		netif_tx_wake_queue(netdev_get_tx_queue(dev, queue->id));
}

static void xennet_alloc_rx_buffers(struct netfront_queue *queue)
{
	unsigned short id;
	struct sk_buff *skb;
	struct page *page;
	int i, batch_target, notify;
	RING_IDX req_prod = queue->rx.req_prod_pvt;
	grant_ref_t ref;
	unsigned long pfn;
	void *vaddr;
	struct xen_netif_rx_request *req;

	if (unlikely(!netif_carrier_ok(queue->info->netdev)))
		return;

	/*
	 * Allocate skbuffs greedily, even though we batch updates to the
	 * receive ring. This creates a less bursty demand on the memory
	 * allocator, so should reduce the chance of failed allocation requests
	 * both for ourself and for other kernel subsystems.
	 */
	batch_target = queue->rx_target - (req_prod - queue->rx.rsp_cons);
	for (i = skb_queue_len(&queue->rx_batch); i < batch_target; i++) {
		skb = __netdev_alloc_skb(queue->info->netdev,
					 RX_COPY_THRESHOLD + NET_IP_ALIGN,
					 GFP_ATOMIC | __GFP_NOWARN);
		if (unlikely(!skb))
			goto no_skb;

		/* Align ip header to a 16 bytes boundary */
		skb_reserve(skb, NET_IP_ALIGN);

		page = alloc_page(GFP_ATOMIC | __GFP_NOWARN);
		if (!page) {
			kfree_skb(skb);
no_skb:
			/* Could not allocate any skbuffs. Try again later. */
			mod_timer(&queue->rx_refill_timer,
				  jiffies + (HZ/10));

			/* Any skbuffs queued for refill? Force them out. */
			if (i != 0)
				goto refill;
			break;
		}

		skb_add_rx_frag(skb, 0, page, 0, 0, PAGE_SIZE);
		__skb_queue_tail(&queue->rx_batch, skb);
	}

	/* Is the batch large enough to be worthwhile? */
	if (i < (queue->rx_target/2)) {
		if (req_prod > queue->rx.sring->req_prod)
			goto push;
		return;
	}

	/* Adjust our fill target if we risked running out of buffers. */
	if (((req_prod - queue->rx.sring->rsp_prod) < (queue->rx_target / 4)) &&
	    ((queue->rx_target *= 2) > queue->rx_max_target))
		queue->rx_target = queue->rx_max_target;

 refill:
	for (i = 0; ; i++) {
		skb = __skb_dequeue(&queue->rx_batch);
		if (skb == NULL)
			break;

		skb->dev = queue->info->netdev;

		id = xennet_rxidx(req_prod + i);

		BUG_ON(queue->rx_skbs[id]);
		queue->rx_skbs[id] = skb;

		ref = gnttab_claim_grant_reference(&queue->gref_rx_head);
		BUG_ON((signed short)ref < 0);
		queue->grant_rx_ref[id] = ref;

		pfn = page_to_pfn(skb_frag_page(&skb_shinfo(skb)->frags[0]));
		vaddr = page_address(skb_frag_page(&skb_shinfo(skb)->frags[0]));

		req = RING_GET_REQUEST(&queue->rx, req_prod + i);
		gnttab_grant_foreign_access_ref(ref,
						queue->info->xbdev->otherend_id,
						pfn_to_mfn(pfn),
						0);

		req->id = id;
		req->gref = ref;
	}

	wmb();		/* barrier so backend seens requests */

	/* Above is a suitable barrier to ensure backend will see requests. */
	queue->rx.req_prod_pvt = req_prod + i;
 push:
	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&queue->rx, notify);
	if (notify)
		notify_remote_via_irq(queue->rx_irq);
}

static int xennet_open(struct net_device *dev)
{
	struct netfront_info *np = netdev_priv(dev);
	unsigned int num_queues = dev->real_num_tx_queues;
	unsigned int i = 0;
	struct netfront_queue *queue = NULL;

	for (i = 0; i < num_queues; ++i) {
		queue = &np->queues[i];
		napi_enable(&queue->napi);

		spin_lock_bh(&queue->rx_lock);
		if (netif_carrier_ok(dev)) {
			xennet_alloc_rx_buffers(queue);
			queue->rx.sring->rsp_event = queue->rx.rsp_cons + 1;
			if (RING_HAS_UNCONSUMED_RESPONSES(&queue->rx))
				napi_schedule(&queue->napi);
		}
		spin_unlock_bh(&queue->rx_lock);
	}

	netif_tx_start_all_queues(dev);

	return 0;
}

static void xennet_tx_buf_gc(struct netfront_queue *queue)
{
	RING_IDX cons, prod;
	unsigned short id;
	struct sk_buff *skb;

	BUG_ON(!netif_carrier_ok(queue->info->netdev));

	do {
		prod = queue->tx.sring->rsp_prod;
		rmb(); /* Ensure we see responses up to 'rp'. */

		for (cons = queue->tx.rsp_cons; cons != prod; cons++) {
			struct xen_netif_tx_response *txrsp;

			txrsp = RING_GET_RESPONSE(&queue->tx, cons);
			if (txrsp->status == XEN_NETIF_RSP_NULL)
				continue;

			id  = txrsp->id;
			skb = queue->tx_skbs[id].skb;
			if (unlikely(gnttab_query_foreign_access(
				queue->grant_tx_ref[id]) != 0)) {
				pr_alert("%s: warning -- grant still in use by backend domain\n",
					 __func__);
				BUG();
			}
			gnttab_end_foreign_access_ref(
				queue->grant_tx_ref[id], GNTMAP_readonly);
			gnttab_release_grant_reference(
				&queue->gref_tx_head, queue->grant_tx_ref[id]);
			queue->grant_tx_ref[id] = GRANT_INVALID_REF;
			queue->grant_tx_page[id] = NULL;
			add_id_to_freelist(&queue->tx_skb_freelist, queue->tx_skbs, id);
			dev_kfree_skb_irq(skb);
		}

		queue->tx.rsp_cons = prod;

		/*
		 * Set a new event, then check for race with update of tx_cons.
		 * Note that it is essential to schedule a callback, no matter
		 * how few buffers are pending. Even if there is space in the
		 * transmit ring, higher layers may be blocked because too much
		 * data is outstanding: in such cases notification from Xen is
		 * likely to be the only kick that we'll get.
		 */
		queue->tx.sring->rsp_event =
			prod + ((queue->tx.sring->req_prod - prod) >> 1) + 1;
		mb();		/* update shared area */
	} while ((cons == prod) && (prod != queue->tx.sring->rsp_prod));

	xennet_maybe_wake_tx(queue);
}

static void xennet_make_frags(struct sk_buff *skb, struct netfront_queue *queue,
			      struct xen_netif_tx_request *tx)
{
	char *data = skb->data;
	unsigned long mfn;
	RING_IDX prod = queue->tx.req_prod_pvt;
	int frags = skb_shinfo(skb)->nr_frags;
	unsigned int offset = offset_in_page(data);
	unsigned int len = skb_headlen(skb);
	unsigned int id;
	grant_ref_t ref;
	int i;

	/* While the header overlaps a page boundary (including being
	   larger than a page), split it it into page-sized chunks. */
	while (len > PAGE_SIZE - offset) {
		tx->size = PAGE_SIZE - offset;
		tx->flags |= XEN_NETTXF_more_data;
		len -= tx->size;
		data += tx->size;
		offset = 0;

		id = get_id_from_freelist(&queue->tx_skb_freelist, queue->tx_skbs);
		queue->tx_skbs[id].skb = skb_get(skb);
		tx = RING_GET_REQUEST(&queue->tx, prod++);
		tx->id = id;
		ref = gnttab_claim_grant_reference(&queue->gref_tx_head);
		BUG_ON((signed short)ref < 0);

		mfn = virt_to_mfn(data);
		gnttab_grant_foreign_access_ref(ref, queue->info->xbdev->otherend_id,
						mfn, GNTMAP_readonly);

		queue->grant_tx_page[id] = virt_to_page(data);
		tx->gref = queue->grant_tx_ref[id] = ref;
		tx->offset = offset;
		tx->size = len;
		tx->flags = 0;
	}

	/* Grant backend access to each skb fragment page. */
	for (i = 0; i < frags; i++) {
		skb_frag_t *frag = skb_shinfo(skb)->frags + i;
		struct page *page = skb_frag_page(frag);

		len = skb_frag_size(frag);
		offset = frag->page_offset;

		/* Data must not cross a page boundary. */
		BUG_ON(len + offset > PAGE_SIZE<<compound_order(page));

		/* Skip unused frames from start of page */
		page += offset >> PAGE_SHIFT;
		offset &= ~PAGE_MASK;

		while (len > 0) {
			unsigned long bytes;

			BUG_ON(offset >= PAGE_SIZE);

			bytes = PAGE_SIZE - offset;
			if (bytes > len)
				bytes = len;

			tx->flags |= XEN_NETTXF_more_data;

			id = get_id_from_freelist(&queue->tx_skb_freelist,
						  queue->tx_skbs);
			queue->tx_skbs[id].skb = skb_get(skb);
			tx = RING_GET_REQUEST(&queue->tx, prod++);
			tx->id = id;
			ref = gnttab_claim_grant_reference(&queue->gref_tx_head);
			BUG_ON((signed short)ref < 0);

			mfn = pfn_to_mfn(page_to_pfn(page));
			gnttab_grant_foreign_access_ref(ref,
							queue->info->xbdev->otherend_id,
							mfn, GNTMAP_readonly);

			queue->grant_tx_page[id] = page;
			tx->gref = queue->grant_tx_ref[id] = ref;
			tx->offset = offset;
			tx->size = bytes;
			tx->flags = 0;

			offset += bytes;
			len -= bytes;

			/* Next frame */
			if (offset == PAGE_SIZE && len) {
				BUG_ON(!PageCompound(page));
				page++;
				offset = 0;
			}
		}
	}

	queue->tx.req_prod_pvt = prod;
}

/*
 * Count how many ring slots are required to send the frags of this
 * skb. Each frag might be a compound page.
 */
static int xennet_count_skb_frag_slots(struct sk_buff *skb)
{
	int i, frags = skb_shinfo(skb)->nr_frags;
	int pages = 0;

	for (i = 0; i < frags; i++) {
		skb_frag_t *frag = skb_shinfo(skb)->frags + i;
		unsigned long size = skb_frag_size(frag);
		unsigned long offset = frag->page_offset;

		/* Skip unused frames from start of page */
		offset &= ~PAGE_MASK;

		pages += PFN_UP(offset + size);
	}

	return pages;
}

static u16 xennet_select_queue(struct net_device *dev, struct sk_buff *skb)
{
	/* Stub for later implementation of queue selection */
	return 0;
}

static int xennet_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	unsigned short id;
	struct netfront_info *np = netdev_priv(dev);
	struct netfront_stats *stats = this_cpu_ptr(np->stats);
	struct xen_netif_tx_request *tx;
	char *data = skb->data;
	RING_IDX i;
	grant_ref_t ref;
	unsigned long mfn;
	int notify;
	int slots;
	unsigned int offset = offset_in_page(data);
	unsigned int len = skb_headlen(skb);
	unsigned long flags;
	struct netfront_queue *queue = NULL;
	unsigned int num_queues = dev->real_num_tx_queues;
	u16 queue_index;

	/* Drop the packet if no queues are set up */
	if (num_queues < 1)
		goto drop;
	/* Determine which queue to transmit this SKB on */
	queue_index = skb_get_queue_mapping(skb);
	queue = &np->queues[queue_index];

	/* If skb->len is too big for wire format, drop skb and alert
	 * user about misconfiguration.
	 */
	if (unlikely(skb->len > XEN_NETIF_MAX_TX_SIZE)) {
		net_alert_ratelimited(
			"xennet: skb->len = %u, too big for wire format\n",
			skb->len);
		goto drop;
	}

	slots = DIV_ROUND_UP(offset + len, PAGE_SIZE) +
		xennet_count_skb_frag_slots(skb);
	if (unlikely(slots > MAX_SKB_FRAGS + 1)) {
		net_alert_ratelimited(
			"xennet: skb rides the rocket: %d slots\n", slots);
		goto drop;
	}

	spin_lock_irqsave(&queue->tx_lock, flags);

	if (unlikely(!netif_carrier_ok(dev) ||
		     (slots > 1 && !xennet_can_sg(dev)) ||
		     netif_needs_gso(skb, netif_skb_features(skb)))) {
		spin_unlock_irqrestore(&queue->tx_lock, flags);
		goto drop;
	}

	i = queue->tx.req_prod_pvt;

	id = get_id_from_freelist(&queue->tx_skb_freelist, queue->tx_skbs);
	queue->tx_skbs[id].skb = skb;

	tx = RING_GET_REQUEST(&queue->tx, i);

	tx->id   = id;
	ref = gnttab_claim_grant_reference(&queue->gref_tx_head);
	BUG_ON((signed short)ref < 0);
	mfn = virt_to_mfn(data);
	gnttab_grant_foreign_access_ref(
		ref, queue->info->xbdev->otherend_id, mfn, GNTMAP_readonly);
	queue->grant_tx_page[id] = virt_to_page(data);
	tx->gref = queue->grant_tx_ref[id] = ref;
	tx->offset = offset;
	tx->size = len;

	tx->flags = 0;
	if (skb->ip_summed == CHECKSUM_PARTIAL)
		/* local packet? */
		tx->flags |= XEN_NETTXF_csum_blank | XEN_NETTXF_data_validated;
	else if (skb->ip_summed == CHECKSUM_UNNECESSARY)
		/* remote but checksummed. */
		tx->flags |= XEN_NETTXF_data_validated;

	if (skb_shinfo(skb)->gso_size) {
		struct xen_netif_extra_info *gso;

		gso = (struct xen_netif_extra_info *)
			RING_GET_REQUEST(&queue->tx, ++i);

		tx->flags |= XEN_NETTXF_extra_info;

		gso->u.gso.size = skb_shinfo(skb)->gso_size;
		gso->u.gso.type = (skb_shinfo(skb)->gso_type & SKB_GSO_TCPV6) ?
			XEN_NETIF_GSO_TYPE_TCPV6 :
			XEN_NETIF_GSO_TYPE_TCPV4;
		gso->u.gso.pad = 0;
		gso->u.gso.features = 0;

		gso->type = XEN_NETIF_EXTRA_TYPE_GSO;
		gso->flags = 0;
	}

	queue->tx.req_prod_pvt = i + 1;

	xennet_make_frags(skb, queue, tx);
	tx->size = skb->len;

	RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&queue->tx, notify);
	if (notify)
		notify_remote_via_irq(queue->tx_irq);

	u64_stats_update_begin(&stats->syncp);
	stats->tx_bytes += skb->len;
	stats->tx_packets++;
	u64_stats_update_end(&stats->syncp);

	/* Note: It is not safe to access skb after xennet_tx_buf_gc()! */
	xennet_tx_buf_gc(queue);

	if (!netfront_tx_slot_available(queue))
		netif_tx_stop_queue(netdev_get_tx_queue(dev, queue->id));

	spin_unlock_irqrestore(&queue->tx_lock, flags);

	return NETDEV_TX_OK;

 drop:
	dev->stats.tx_dropped++;
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

static int xennet_close(struct net_device *dev)
{
	struct netfront_info *np = netdev_priv(dev);
	unsigned int num_queues = dev->real_num_tx_queues;
	unsigned int i;
	struct netfront_queue *queue;
	netif_tx_stop_all_queues(np->netdev);
	for (i = 0; i < num_queues; ++i) {
		queue = &np->queues[i];
		napi_disable(&queue->napi);
	}
	return 0;
}

static void xennet_move_rx_slot(struct netfront_queue *queue, struct sk_buff *skb,
				grant_ref_t ref)
{
	int new = xennet_rxidx(queue->rx.req_prod_pvt);

	BUG_ON(queue->rx_skbs[new]);
	queue->rx_skbs[new] = skb;
	queue->grant_rx_ref[new] = ref;
	RING_GET_REQUEST(&queue->rx, queue->rx.req_prod_pvt)->id = new;
	RING_GET_REQUEST(&queue->rx, queue->rx.req_prod_pvt)->gref = ref;
	queue->rx.req_prod_pvt++;
}

static int xennet_get_extras(struct netfront_queue *queue,
			     struct xen_netif_extra_info *extras,
			     RING_IDX rp)

{
	struct xen_netif_extra_info *extra;
	struct device *dev = &queue->info->netdev->dev;
	RING_IDX cons = queue->rx.rsp_cons;
	int err = 0;

	do {
		struct sk_buff *skb;
		grant_ref_t ref;

		if (unlikely(cons + 1 == rp)) {
			if (net_ratelimit())
				dev_warn(dev, "Missing extra info\n");
			err = -EBADR;
			break;
		}

		extra = (struct xen_netif_extra_info *)
			RING_GET_RESPONSE(&queue->rx, ++cons);

		if (unlikely(!extra->type ||
			     extra->type >= XEN_NETIF_EXTRA_TYPE_MAX)) {
			if (net_ratelimit())
				dev_warn(dev, "Invalid extra type: %d\n",
					extra->type);
			err = -EINVAL;
		} else {
			memcpy(&extras[extra->type - 1], extra,
			       sizeof(*extra));
		}

		skb = xennet_get_rx_skb(queue, cons);
		ref = xennet_get_rx_ref(queue, cons);
		xennet_move_rx_slot(queue, skb, ref);
	} while (extra->flags & XEN_NETIF_EXTRA_FLAG_MORE);

	queue->rx.rsp_cons = cons;
	return err;
}

static int xennet_get_responses(struct netfront_queue *queue,
				struct netfront_rx_info *rinfo, RING_IDX rp,
				struct sk_buff_head *list)
{
	struct xen_netif_rx_response *rx = &rinfo->rx;
	struct xen_netif_extra_info *extras = rinfo->extras;
	struct device *dev = &queue->info->netdev->dev;
	RING_IDX cons = queue->rx.rsp_cons;
	struct sk_buff *skb = xennet_get_rx_skb(queue, cons);
	grant_ref_t ref = xennet_get_rx_ref(queue, cons);
	int max = MAX_SKB_FRAGS + (rx->status <= RX_COPY_THRESHOLD);
	int slots = 1;
	int err = 0;
	unsigned long ret;

	if (rx->flags & XEN_NETRXF_extra_info) {
		err = xennet_get_extras(queue, extras, rp);
		cons = queue->rx.rsp_cons;
	}

	for (;;) {
		if (unlikely(rx->status < 0 ||
			     rx->offset + rx->status > PAGE_SIZE)) {
			if (net_ratelimit())
				dev_warn(dev, "rx->offset: %x, size: %u\n",
					 rx->offset, rx->status);
			xennet_move_rx_slot(queue, skb, ref);
			err = -EINVAL;
			goto next;
		}

		/*
		 * This definitely indicates a bug, either in this driver or in
		 * the backend driver. In future this should flag the bad
		 * situation to the system controller to reboot the backend.
		 */
		if (ref == GRANT_INVALID_REF) {
			if (net_ratelimit())
				dev_warn(dev, "Bad rx response id %d.\n",
					 rx->id);
			err = -EINVAL;
			goto next;
		}

		ret = gnttab_end_foreign_access_ref(ref, 0);
		BUG_ON(!ret);

		gnttab_release_grant_reference(&queue->gref_rx_head, ref);

		__skb_queue_tail(list, skb);

next:
		if (!(rx->flags & XEN_NETRXF_more_data))
			break;

		if (cons + slots == rp) {
			if (net_ratelimit())
				dev_warn(dev, "Need more slots\n");
			err = -ENOENT;
			break;
		}

		rx = RING_GET_RESPONSE(&queue->rx, cons + slots);
		skb = xennet_get_rx_skb(queue, cons + slots);
		ref = xennet_get_rx_ref(queue, cons + slots);
		slots++;
	}

	if (unlikely(slots > max)) {
		if (net_ratelimit())
			dev_warn(dev, "Too many slots\n");
		err = -E2BIG;
	}

	if (unlikely(err))
		queue->rx.rsp_cons = cons + slots;

	return err;
}

static int xennet_set_skb_gso(struct sk_buff *skb,
			      struct xen_netif_extra_info *gso)
{
	if (!gso->u.gso.size) {
		if (net_ratelimit())
			pr_warn("GSO size must not be zero\n");
		return -EINVAL;
	}

	if (gso->u.gso.type != XEN_NETIF_GSO_TYPE_TCPV4 &&
	    gso->u.gso.type != XEN_NETIF_GSO_TYPE_TCPV6) {
		if (net_ratelimit())
			pr_warn("Bad GSO type %d\n", gso->u.gso.type);
		return -EINVAL;
	}

	skb_shinfo(skb)->gso_size = gso->u.gso.size;
	skb_shinfo(skb)->gso_type =
		(gso->u.gso.type == XEN_NETIF_GSO_TYPE_TCPV4) ?
		SKB_GSO_TCPV4 :
		SKB_GSO_TCPV6;

	/* Header must be checked, and gso_segs computed. */
	skb_shinfo(skb)->gso_type |= SKB_GSO_DODGY;
	skb_shinfo(skb)->gso_segs = 0;

	return 0;
}

static RING_IDX xennet_fill_frags(struct netfront_queue *queue,
				  struct sk_buff *skb,
				  struct sk_buff_head *list)
{
	struct skb_shared_info *shinfo = skb_shinfo(skb);
	RING_IDX cons = queue->rx.rsp_cons;
	struct sk_buff *nskb;

	while ((nskb = __skb_dequeue(list))) {
		struct xen_netif_rx_response *rx =
			RING_GET_RESPONSE(&queue->rx, ++cons);
		skb_frag_t *nfrag = &skb_shinfo(nskb)->frags[0];

		if (shinfo->nr_frags == MAX_SKB_FRAGS) {
			unsigned int pull_to = NETFRONT_SKB_CB(skb)->pull_to;

			BUG_ON(pull_to <= skb_headlen(skb));
			__pskb_pull_tail(skb, pull_to - skb_headlen(skb));
		}
		BUG_ON(shinfo->nr_frags >= MAX_SKB_FRAGS);

		skb_add_rx_frag(skb, shinfo->nr_frags, skb_frag_page(nfrag),
				rx->offset, rx->status, PAGE_SIZE);

		skb_shinfo(nskb)->nr_frags = 0;
		kfree_skb(nskb);
	}

	return cons;
}

static int checksum_setup(struct net_device *dev, struct sk_buff *skb)
{
	bool recalculate_partial_csum = false;

	/*
	 * A GSO SKB must be CHECKSUM_PARTIAL. However some buggy
	 * peers can fail to set NETRXF_csum_blank when sending a GSO
	 * frame. In this case force the SKB to CHECKSUM_PARTIAL and
	 * recalculate the partial checksum.
	 */
	if (skb->ip_summed != CHECKSUM_PARTIAL && skb_is_gso(skb)) {
		struct netfront_info *np = netdev_priv(dev);
		atomic_inc(&np->rx_gso_checksum_fixup);
		skb->ip_summed = CHECKSUM_PARTIAL;
		recalculate_partial_csum = true;
	}

	/* A non-CHECKSUM_PARTIAL SKB does not require setup. */
	if (skb->ip_summed != CHECKSUM_PARTIAL)
		return 0;

	return skb_checksum_setup(skb, recalculate_partial_csum);
}

static int handle_incoming_queue(struct netfront_queue *queue,
				 struct sk_buff_head *rxq)
{
	struct netfront_stats *stats = this_cpu_ptr(queue->info->stats);
	int packets_dropped = 0;
	struct sk_buff *skb;

	while ((skb = __skb_dequeue(rxq)) != NULL) {
		int pull_to = NETFRONT_SKB_CB(skb)->pull_to;

		if (pull_to > skb_headlen(skb))
			__pskb_pull_tail(skb, pull_to - skb_headlen(skb));

		/* Ethernet work: Delayed to here as it peeks the header. */
		skb->protocol = eth_type_trans(skb, queue->info->netdev);
		skb_reset_network_header(skb);

		if (checksum_setup(queue->info->netdev, skb)) {
			kfree_skb(skb);
			packets_dropped++;
			queue->info->netdev->stats.rx_errors++;
			continue;
		}

		u64_stats_update_begin(&stats->syncp);
		stats->rx_packets++;
		stats->rx_bytes += skb->len;
		u64_stats_update_end(&stats->syncp);

		/* Pass it up. */
		napi_gro_receive(&queue->napi, skb);
	}

	return packets_dropped;
}

static int xennet_poll(struct napi_struct *napi, int budget)
{
	struct netfront_queue *queue = container_of(napi, struct netfront_queue, napi);
	struct net_device *dev = queue->info->netdev;
	struct sk_buff *skb;
	struct netfront_rx_info rinfo;
	struct xen_netif_rx_response *rx = &rinfo.rx;
	struct xen_netif_extra_info *extras = rinfo.extras;
	RING_IDX i, rp;
	int work_done;
	struct sk_buff_head rxq;
	struct sk_buff_head errq;
	struct sk_buff_head tmpq;
	unsigned long flags;
	int err;

	spin_lock(&queue->rx_lock);

	skb_queue_head_init(&rxq);
	skb_queue_head_init(&errq);
	skb_queue_head_init(&tmpq);

	rp = queue->rx.sring->rsp_prod;
	rmb(); /* Ensure we see queued responses up to 'rp'. */

	i = queue->rx.rsp_cons;
	work_done = 0;
	while ((i != rp) && (work_done < budget)) {
		memcpy(rx, RING_GET_RESPONSE(&queue->rx, i), sizeof(*rx));
		memset(extras, 0, sizeof(rinfo.extras));

		err = xennet_get_responses(queue, &rinfo, rp, &tmpq);

		if (unlikely(err)) {
err:
			while ((skb = __skb_dequeue(&tmpq)))
				__skb_queue_tail(&errq, skb);
			dev->stats.rx_errors++;
			i = queue->rx.rsp_cons;
			continue;
		}

		skb = __skb_dequeue(&tmpq);

		if (extras[XEN_NETIF_EXTRA_TYPE_GSO - 1].type) {
			struct xen_netif_extra_info *gso;
			gso = &extras[XEN_NETIF_EXTRA_TYPE_GSO - 1];

			if (unlikely(xennet_set_skb_gso(skb, gso))) {
				__skb_queue_head(&tmpq, skb);
				queue->rx.rsp_cons += skb_queue_len(&tmpq);
				goto err;
			}
		}

		NETFRONT_SKB_CB(skb)->pull_to = rx->status;
		if (NETFRONT_SKB_CB(skb)->pull_to > RX_COPY_THRESHOLD)
			NETFRONT_SKB_CB(skb)->pull_to = RX_COPY_THRESHOLD;

		skb_shinfo(skb)->frags[0].page_offset = rx->offset;
		skb_frag_size_set(&skb_shinfo(skb)->frags[0], rx->status);
		skb->data_len = rx->status;
		skb->len += rx->status;

		i = xennet_fill_frags(queue, skb, &tmpq);

		if (rx->flags & XEN_NETRXF_csum_blank)
			skb->ip_summed = CHECKSUM_PARTIAL;
		else if (rx->flags & XEN_NETRXF_data_validated)
			skb->ip_summed = CHECKSUM_UNNECESSARY;

		__skb_queue_tail(&rxq, skb);

		queue->rx.rsp_cons = ++i;
		work_done++;
	}

	__skb_queue_purge(&errq);

	work_done -= handle_incoming_queue(queue, &rxq);

	/* If we get a callback with very few responses, reduce fill target. */
	/* NB. Note exponential increase, linear decrease. */
	if (((queue->rx.req_prod_pvt - queue->rx.sring->rsp_prod) >
	     ((3*queue->rx_target) / 4)) &&
	    (--queue->rx_target < queue->rx_min_target))
		queue->rx_target = queue->rx_min_target;

	xennet_alloc_rx_buffers(queue);

	if (work_done < budget) {
		int more_to_do = 0;

		napi_gro_flush(napi, false);

		local_irq_save(flags);

		RING_FINAL_CHECK_FOR_RESPONSES(&queue->rx, more_to_do);
		if (!more_to_do)
			__napi_complete(napi);

		local_irq_restore(flags);
	}

	spin_unlock(&queue->rx_lock);

	return work_done;
}

static int xennet_change_mtu(struct net_device *dev, int mtu)
{
	int max = xennet_can_sg(dev) ?
		XEN_NETIF_MAX_TX_SIZE - MAX_TCP_HEADER : ETH_DATA_LEN;

	if (mtu > max)
		return -EINVAL;
	dev->mtu = mtu;
	return 0;
}

static struct rtnl_link_stats64 *xennet_get_stats64(struct net_device *dev,
						    struct rtnl_link_stats64 *tot)
{
	struct netfront_info *np = netdev_priv(dev);
	int cpu;

	for_each_possible_cpu(cpu) {
		struct netfront_stats *stats = per_cpu_ptr(np->stats, cpu);
		u64 rx_packets, rx_bytes, tx_packets, tx_bytes;
		unsigned int start;

		do {
			start = u64_stats_fetch_begin_irq(&stats->syncp);

			rx_packets = stats->rx_packets;
			tx_packets = stats->tx_packets;
			rx_bytes = stats->rx_bytes;
			tx_bytes = stats->tx_bytes;
		} while (u64_stats_fetch_retry_irq(&stats->syncp, start));

		tot->rx_packets += rx_packets;
		tot->tx_packets += tx_packets;
		tot->rx_bytes   += rx_bytes;
		tot->tx_bytes   += tx_bytes;
	}

	tot->rx_errors  = dev->stats.rx_errors;
	tot->tx_dropped = dev->stats.tx_dropped;

	return tot;
}

static void xennet_release_tx_bufs(struct netfront_queue *queue)
{
	struct sk_buff *skb;
	int i;

	for (i = 0; i < NET_TX_RING_SIZE; i++) {
		/* Skip over entries which are actually freelist references */
		if (skb_entry_is_link(&queue->tx_skbs[i]))
			continue;

		skb = queue->tx_skbs[i].skb;
		get_page(queue->grant_tx_page[i]);
		gnttab_end_foreign_access(queue->grant_tx_ref[i],
					  GNTMAP_readonly,
					  (unsigned long)page_address(queue->grant_tx_page[i]));
		queue->grant_tx_page[i] = NULL;
		queue->grant_tx_ref[i] = GRANT_INVALID_REF;
		add_id_to_freelist(&queue->tx_skb_freelist, queue->tx_skbs, i);
		dev_kfree_skb_irq(skb);
	}
}

static void xennet_release_rx_bufs(struct netfront_queue *queue)
{
	int id, ref;

	spin_lock_bh(&queue->rx_lock);

	for (id = 0; id < NET_RX_RING_SIZE; id++) {
		struct sk_buff *skb;
		struct page *page;

		skb = queue->rx_skbs[id];
		if (!skb)
			continue;

		ref = queue->grant_rx_ref[id];
		if (ref == GRANT_INVALID_REF)
			continue;

		page = skb_frag_page(&skb_shinfo(skb)->frags[0]);

		/* gnttab_end_foreign_access() needs a page ref until
		 * foreign access is ended (which may be deferred).
		 */
		get_page(page);
		gnttab_end_foreign_access(ref, 0,
					  (unsigned long)page_address(page));
		queue->grant_rx_ref[id] = GRANT_INVALID_REF;

		kfree_skb(skb);
	}

	spin_unlock_bh(&queue->rx_lock);
}

static void xennet_uninit(struct net_device *dev)
{
	struct netfront_info *np = netdev_priv(dev);
	unsigned int num_queues = dev->real_num_tx_queues;
	struct netfront_queue *queue;
	unsigned int i;

	for (i = 0; i < num_queues; ++i) {
		queue = &np->queues[i];
		xennet_release_tx_bufs(queue);
		xennet_release_rx_bufs(queue);
		gnttab_free_grant_references(queue->gref_tx_head);
		gnttab_free_grant_references(queue->gref_rx_head);
	}
}

static netdev_features_t xennet_fix_features(struct net_device *dev,
	netdev_features_t features)
{
	struct netfront_info *np = netdev_priv(dev);
	int val;

	if (features & NETIF_F_SG) {
		if (xenbus_scanf(XBT_NIL, np->xbdev->otherend, "feature-sg",
				 "%d", &val) < 0)
			val = 0;

		if (!val)
			features &= ~NETIF_F_SG;
	}

	if (features & NETIF_F_IPV6_CSUM) {
		if (xenbus_scanf(XBT_NIL, np->xbdev->otherend,
				 "feature-ipv6-csum-offload", "%d", &val) < 0)
			val = 0;

		if (!val)
			features &= ~NETIF_F_IPV6_CSUM;
	}

	if (features & NETIF_F_TSO) {
		if (xenbus_scanf(XBT_NIL, np->xbdev->otherend,
				 "feature-gso-tcpv4", "%d", &val) < 0)
			val = 0;

		if (!val)
			features &= ~NETIF_F_TSO;
	}

	if (features & NETIF_F_TSO6) {
		if (xenbus_scanf(XBT_NIL, np->xbdev->otherend,
				 "feature-gso-tcpv6", "%d", &val) < 0)
			val = 0;

		if (!val)
			features &= ~NETIF_F_TSO6;
	}

	return features;
}

static int xennet_set_features(struct net_device *dev,
	netdev_features_t features)
{
	if (!(features & NETIF_F_SG) && dev->mtu > ETH_DATA_LEN) {
		netdev_info(dev, "Reducing MTU because no SG offload");
		dev->mtu = ETH_DATA_LEN;
	}

	return 0;
}

static irqreturn_t xennet_tx_interrupt(int irq, void *dev_id)
{
	struct netfront_queue *queue = dev_id;
	unsigned long flags;

	spin_lock_irqsave(&queue->tx_lock, flags);
	xennet_tx_buf_gc(queue);
	spin_unlock_irqrestore(&queue->tx_lock, flags);

	return IRQ_HANDLED;
}

static irqreturn_t xennet_rx_interrupt(int irq, void *dev_id)
{
	struct netfront_queue *queue = dev_id;
	struct net_device *dev = queue->info->netdev;

	if (likely(netif_carrier_ok(dev) &&
		   RING_HAS_UNCONSUMED_RESPONSES(&queue->rx)))
			napi_schedule(&queue->napi);

	return IRQ_HANDLED;
}

static irqreturn_t xennet_interrupt(int irq, void *dev_id)
{
	xennet_tx_interrupt(irq, dev_id);
	xennet_rx_interrupt(irq, dev_id);
	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void xennet_poll_controller(struct net_device *dev)
{
	/* Poll each queue */
	struct netfront_info *info = netdev_priv(dev);
	unsigned int num_queues = dev->real_num_tx_queues;
	unsigned int i;
	for (i = 0; i < num_queues; ++i)
		xennet_interrupt(0, &info->queues[i]);
}
#endif

static const struct net_device_ops xennet_netdev_ops = {
	.ndo_open            = xennet_open,
	.ndo_uninit          = xennet_uninit,
	.ndo_stop            = xennet_close,
	.ndo_start_xmit      = xennet_start_xmit,
	.ndo_change_mtu	     = xennet_change_mtu,
	.ndo_get_stats64     = xennet_get_stats64,
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_validate_addr   = eth_validate_addr,
	.ndo_fix_features    = xennet_fix_features,
	.ndo_set_features    = xennet_set_features,
	.ndo_select_queue    = xennet_select_queue,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = xennet_poll_controller,
#endif
};

static struct net_device *xennet_create_dev(struct xenbus_device *dev)
{
	int err;
	struct net_device *netdev;
	struct netfront_info *np;

	netdev = alloc_etherdev_mq(sizeof(struct netfront_info), 1);
	if (!netdev)
		return ERR_PTR(-ENOMEM);

	np                   = netdev_priv(netdev);
	np->xbdev            = dev;

	/* No need to use rtnl_lock() before the call below as it
	 * happens before register_netdev().
	 */
	netif_set_real_num_tx_queues(netdev, 0);
	np->queues = NULL;

	err = -ENOMEM;
	np->stats = netdev_alloc_pcpu_stats(struct netfront_stats);
	if (np->stats == NULL)
		goto exit;

	netdev->netdev_ops	= &xennet_netdev_ops;

	netdev->features        = NETIF_F_IP_CSUM | NETIF_F_RXCSUM |
				  NETIF_F_GSO_ROBUST;
	netdev->hw_features	= NETIF_F_SG |
				  NETIF_F_IPV6_CSUM |
				  NETIF_F_TSO | NETIF_F_TSO6;

	/*
         * Assume that all hw features are available for now. This set
         * will be adjusted by the call to netdev_update_features() in
         * xennet_connect() which is the earliest point where we can
         * negotiate with the backend regarding supported features.
         */
	netdev->features |= netdev->hw_features;

	netdev->ethtool_ops = &xennet_ethtool_ops;
	SET_NETDEV_DEV(netdev, &dev->dev);

	netif_set_gso_max_size(netdev, XEN_NETIF_MAX_TX_SIZE - MAX_TCP_HEADER);

	np->netdev = netdev;

	netif_carrier_off(netdev);

	return netdev;

 exit:
	free_netdev(netdev);
	return ERR_PTR(err);
}

/**
 * Entry point to this code when a new device is created.  Allocate the basic
 * structures and the ring buffers for communication with the backend, and
 * inform the backend of the appropriate details for those.
 */
static int netfront_probe(struct xenbus_device *dev,
			  const struct xenbus_device_id *id)
{
	int err;
	struct net_device *netdev;
	struct netfront_info *info;

	netdev = xennet_create_dev(dev);
	if (IS_ERR(netdev)) {
		err = PTR_ERR(netdev);
		xenbus_dev_fatal(dev, err, "creating netdev");
		return err;
	}

	info = netdev_priv(netdev);
	dev_set_drvdata(&dev->dev, info);

	err = register_netdev(info->netdev);
	if (err) {
		pr_warn("%s: register_netdev err=%d\n", __func__, err);
		goto fail;
	}

	err = xennet_sysfs_addif(info->netdev);
	if (err) {
		unregister_netdev(info->netdev);
		pr_warn("%s: add sysfs failed err=%d\n", __func__, err);
		goto fail;
	}

	return 0;

 fail:
	free_netdev(netdev);
	dev_set_drvdata(&dev->dev, NULL);
	return err;
}

static void xennet_end_access(int ref, void *page)
{
	/* This frees the page as a side-effect */
	if (ref != GRANT_INVALID_REF)
		gnttab_end_foreign_access(ref, 0, (unsigned long)page);
}

static void xennet_disconnect_backend(struct netfront_info *info)
{
	unsigned int i = 0;
	struct netfront_queue *queue = NULL;
	unsigned int num_queues = info->netdev->real_num_tx_queues;

	for (i = 0; i < num_queues; ++i) {
		/* Stop old i/f to prevent errors whilst we rebuild the state. */
		spin_lock_bh(&queue->rx_lock);
		spin_lock_irq(&queue->tx_lock);
		netif_carrier_off(queue->info->netdev);
		spin_unlock_irq(&queue->tx_lock);
		spin_unlock_bh(&queue->rx_lock);

		if (queue->tx_irq && (queue->tx_irq == queue->rx_irq))
			unbind_from_irqhandler(queue->tx_irq, queue);
		if (queue->tx_irq && (queue->tx_irq != queue->rx_irq)) {
			unbind_from_irqhandler(queue->tx_irq, queue);
			unbind_from_irqhandler(queue->rx_irq, queue);
		}
		queue->tx_evtchn = queue->rx_evtchn = 0;
		queue->tx_irq = queue->rx_irq = 0;

		/* End access and free the pages */
		xennet_end_access(queue->tx_ring_ref, queue->tx.sring);
		xennet_end_access(queue->rx_ring_ref, queue->rx.sring);

		queue->tx_ring_ref = GRANT_INVALID_REF;
		queue->rx_ring_ref = GRANT_INVALID_REF;
		queue->tx.sring = NULL;
		queue->rx.sring = NULL;
	}
}

/**
 * We are reconnecting to the backend, due to a suspend/resume, or a backend
 * driver restart.  We tear down our netif structure and recreate it, but
 * leave the device-layer structures intact so that this is transparent to the
 * rest of the kernel.
 */
static int netfront_resume(struct xenbus_device *dev)
{
	struct netfront_info *info = dev_get_drvdata(&dev->dev);

	dev_dbg(&dev->dev, "%s\n", dev->nodename);

	xennet_disconnect_backend(info);
	return 0;
}

static int xen_net_read_mac(struct xenbus_device *dev, u8 mac[])
{
	char *s, *e, *macstr;
	int i;

	macstr = s = xenbus_read(XBT_NIL, dev->nodename, "mac", NULL);
	if (IS_ERR(macstr))
		return PTR_ERR(macstr);

	for (i = 0; i < ETH_ALEN; i++) {
		mac[i] = simple_strtoul(s, &e, 16);
		if ((s == e) || (*e != ((i == ETH_ALEN-1) ? '\0' : ':'))) {
			kfree(macstr);
			return -ENOENT;
		}
		s = e+1;
	}

	kfree(macstr);
	return 0;
}

static int setup_netfront_single(struct netfront_queue *queue)
{
	int err;

	err = xenbus_alloc_evtchn(queue->info->xbdev, &queue->tx_evtchn);
	if (err < 0)
		goto fail;

	err = bind_evtchn_to_irqhandler(queue->tx_evtchn,
					xennet_interrupt,
					0, queue->info->netdev->name, queue);
	if (err < 0)
		goto bind_fail;
	queue->rx_evtchn = queue->tx_evtchn;
	queue->rx_irq = queue->tx_irq = err;

	return 0;

bind_fail:
	xenbus_free_evtchn(queue->info->xbdev, queue->tx_evtchn);
	queue->tx_evtchn = 0;
fail:
	return err;
}

static int setup_netfront_split(struct netfront_queue *queue)
{
	int err;

	err = xenbus_alloc_evtchn(queue->info->xbdev, &queue->tx_evtchn);
	if (err < 0)
		goto fail;
	err = xenbus_alloc_evtchn(queue->info->xbdev, &queue->rx_evtchn);
	if (err < 0)
		goto alloc_rx_evtchn_fail;

	snprintf(queue->tx_irq_name, sizeof(queue->tx_irq_name),
		 "%s-tx", queue->name);
	err = bind_evtchn_to_irqhandler(queue->tx_evtchn,
					xennet_tx_interrupt,
					0, queue->tx_irq_name, queue);
	if (err < 0)
		goto bind_tx_fail;
	queue->tx_irq = err;

	snprintf(queue->rx_irq_name, sizeof(queue->rx_irq_name),
		 "%s-rx", queue->name);
	err = bind_evtchn_to_irqhandler(queue->rx_evtchn,
					xennet_rx_interrupt,
					0, queue->rx_irq_name, queue);
	if (err < 0)
		goto bind_rx_fail;
	queue->rx_irq = err;

	return 0;

bind_rx_fail:
	unbind_from_irqhandler(queue->tx_irq, queue);
	queue->tx_irq = 0;
bind_tx_fail:
	xenbus_free_evtchn(queue->info->xbdev, queue->rx_evtchn);
	queue->rx_evtchn = 0;
alloc_rx_evtchn_fail:
	xenbus_free_evtchn(queue->info->xbdev, queue->tx_evtchn);
	queue->tx_evtchn = 0;
fail:
	return err;
}

static int setup_netfront(struct xenbus_device *dev,
			struct netfront_queue *queue, unsigned int feature_split_evtchn)
{
	struct xen_netif_tx_sring *txs;
	struct xen_netif_rx_sring *rxs;
	int err;

	queue->tx_ring_ref = GRANT_INVALID_REF;
	queue->rx_ring_ref = GRANT_INVALID_REF;
	queue->rx.sring = NULL;
	queue->tx.sring = NULL;

	txs = (struct xen_netif_tx_sring *)get_zeroed_page(GFP_NOIO | __GFP_HIGH);
	if (!txs) {
		err = -ENOMEM;
		xenbus_dev_fatal(dev, err, "allocating tx ring page");
		goto fail;
	}
	SHARED_RING_INIT(txs);
	FRONT_RING_INIT(&queue->tx, txs, PAGE_SIZE);

	err = xenbus_grant_ring(dev, virt_to_mfn(txs));
	if (err < 0)
		goto grant_tx_ring_fail;
	queue->tx_ring_ref = err;

	rxs = (struct xen_netif_rx_sring *)get_zeroed_page(GFP_NOIO | __GFP_HIGH);
	if (!rxs) {
		err = -ENOMEM;
		xenbus_dev_fatal(dev, err, "allocating rx ring page");
		goto alloc_rx_ring_fail;
	}
	SHARED_RING_INIT(rxs);
	FRONT_RING_INIT(&queue->rx, rxs, PAGE_SIZE);

	err = xenbus_grant_ring(dev, virt_to_mfn(rxs));
	if (err < 0)
		goto grant_rx_ring_fail;
	queue->rx_ring_ref = err;

	if (feature_split_evtchn)
		err = setup_netfront_split(queue);
	/* setup single event channel if
	 *  a) feature-split-event-channels == 0
	 *  b) feature-split-event-channels == 1 but failed to setup
	 */
	if (!feature_split_evtchn || (feature_split_evtchn && err))
		err = setup_netfront_single(queue);

	if (err)
		goto alloc_evtchn_fail;

	return 0;

	/* If we fail to setup netfront, it is safe to just revoke access to
	 * granted pages because backend is not accessing it at this point.
	 */
alloc_evtchn_fail:
	gnttab_end_foreign_access_ref(queue->rx_ring_ref, 0);
grant_rx_ring_fail:
	free_page((unsigned long)rxs);
alloc_rx_ring_fail:
	gnttab_end_foreign_access_ref(queue->tx_ring_ref, 0);
grant_tx_ring_fail:
	free_page((unsigned long)txs);
fail:
	return err;
}

/* Queue-specific initialisation
 * This used to be done in xennet_create_dev() but must now
 * be run per-queue.
 */
static int xennet_init_queue(struct netfront_queue *queue)
{
	unsigned short i;
	int err = 0;

	spin_lock_init(&queue->tx_lock);
	spin_lock_init(&queue->rx_lock);

	skb_queue_head_init(&queue->rx_batch);
	queue->rx_target     = RX_DFL_MIN_TARGET;
	queue->rx_min_target = RX_DFL_MIN_TARGET;
	queue->rx_max_target = RX_MAX_TARGET;

	init_timer(&queue->rx_refill_timer);
	queue->rx_refill_timer.data = (unsigned long)queue;
	queue->rx_refill_timer.function = rx_refill_timeout;

	/* Initialise tx_skbs as a free chain containing every entry. */
	queue->tx_skb_freelist = 0;
	for (i = 0; i < NET_TX_RING_SIZE; i++) {
		skb_entry_set_link(&queue->tx_skbs[i], i+1);
		queue->grant_tx_ref[i] = GRANT_INVALID_REF;
		queue->grant_tx_page[i] = NULL;
	}

	/* Clear out rx_skbs */
	for (i = 0; i < NET_RX_RING_SIZE; i++) {
		queue->rx_skbs[i] = NULL;
		queue->grant_rx_ref[i] = GRANT_INVALID_REF;
	}

	/* A grant for every tx ring slot */
	if (gnttab_alloc_grant_references(TX_MAX_TARGET,
					  &queue->gref_tx_head) < 0) {
		pr_alert("can't alloc tx grant refs\n");
		err = -ENOMEM;
		goto exit;
	}

	/* A grant for every rx ring slot */
	if (gnttab_alloc_grant_references(RX_MAX_TARGET,
					  &queue->gref_rx_head) < 0) {
		pr_alert("can't alloc rx grant refs\n");
		err = -ENOMEM;
		goto exit_free_tx;
	}

	netif_napi_add(queue->info->netdev, &queue->napi, xennet_poll, 64);

	return 0;

 exit_free_tx:
	gnttab_free_grant_references(queue->gref_tx_head);
 exit:
	return err;
}

/* Common code used when first setting up, and when resuming. */
static int talk_to_netback(struct xenbus_device *dev,
			   struct netfront_info *info)
{
	const char *message;
	struct xenbus_transaction xbt;
	int err;
	unsigned int feature_split_evtchn;
	unsigned int i = 0;
	struct netfront_queue *queue = NULL;
	unsigned int num_queues = 1;

	info->netdev->irq = 0;

	/* Check feature-split-event-channels */
	err = xenbus_scanf(XBT_NIL, info->xbdev->otherend,
			   "feature-split-event-channels", "%u",
			   &feature_split_evtchn);
	if (err < 0)
		feature_split_evtchn = 0;

	/* Read mac addr. */
	err = xen_net_read_mac(dev, info->netdev->dev_addr);
	if (err) {
		xenbus_dev_fatal(dev, err, "parsing %s/mac", dev->nodename);
		goto out;
	}

	/* Allocate array of queues */
	info->queues = kcalloc(num_queues, sizeof(struct netfront_queue), GFP_KERNEL);
	if (!info->queues) {
		err = -ENOMEM;
		goto out;
	}
	rtnl_lock();
	netif_set_real_num_tx_queues(info->netdev, num_queues);
	rtnl_unlock();

	/* Create shared ring, alloc event channel -- for each queue */
	for (i = 0; i < num_queues; ++i) {
		queue = &info->queues[i];
		queue->id = i;
		queue->info = info;
		err = xennet_init_queue(queue);
		if (err) {
			/* xennet_init_queue() cleans up after itself on failure,
			 * but we still have to clean up any previously initialised
			 * queues. If i > 0, set num_queues to i, then goto
			 * destroy_ring, which calls xennet_disconnect_backend()
			 * to tidy up.
			 */
			if (i > 0) {
				rtnl_lock();
				netif_set_real_num_tx_queues(info->netdev, i);
				rtnl_unlock();
				goto destroy_ring;
			} else {
				goto out;
			}
		}
		err = setup_netfront(dev, queue, feature_split_evtchn);
		if (err) {
			/* As for xennet_init_queue(), setup_netfront() will tidy
			 * up the current queue on error, but we need to clean up
			 * those already allocated.
			 */
			if (i > 0) {
				rtnl_lock();
				netif_set_real_num_tx_queues(info->netdev, i);
				rtnl_unlock();
				goto destroy_ring;
			} else {
				goto out;
			}
		}
	}

again:
	queue = &info->queues[0]; /* Use first queue only */

	err = xenbus_transaction_start(&xbt);
	if (err) {
		xenbus_dev_fatal(dev, err, "starting transaction");
		goto destroy_ring;
	}

	err = xenbus_printf(xbt, dev->nodename, "tx-ring-ref", "%u",
			    queue->tx_ring_ref);
	if (err) {
		message = "writing tx ring-ref";
		goto abort_transaction;
	}
	err = xenbus_printf(xbt, dev->nodename, "rx-ring-ref", "%u",
			    queue->rx_ring_ref);
	if (err) {
		message = "writing rx ring-ref";
		goto abort_transaction;
	}

	if (queue->tx_evtchn == queue->rx_evtchn) {
		err = xenbus_printf(xbt, dev->nodename,
				    "event-channel", "%u", queue->tx_evtchn);
		if (err) {
			message = "writing event-channel";
			goto abort_transaction;
		}
	} else {
		err = xenbus_printf(xbt, dev->nodename,
				    "event-channel-tx", "%u", queue->tx_evtchn);
		if (err) {
			message = "writing event-channel-tx";
			goto abort_transaction;
		}
		err = xenbus_printf(xbt, dev->nodename,
				    "event-channel-rx", "%u", queue->rx_evtchn);
		if (err) {
			message = "writing event-channel-rx";
			goto abort_transaction;
		}
	}

	err = xenbus_printf(xbt, dev->nodename, "request-rx-copy", "%u",
			    1);
	if (err) {
		message = "writing request-rx-copy";
		goto abort_transaction;
	}

	err = xenbus_printf(xbt, dev->nodename, "feature-rx-notify", "%d", 1);
	if (err) {
		message = "writing feature-rx-notify";
		goto abort_transaction;
	}

	err = xenbus_printf(xbt, dev->nodename, "feature-sg", "%d", 1);
	if (err) {
		message = "writing feature-sg";
		goto abort_transaction;
	}

	err = xenbus_printf(xbt, dev->nodename, "feature-gso-tcpv4", "%d", 1);
	if (err) {
		message = "writing feature-gso-tcpv4";
		goto abort_transaction;
	}

	err = xenbus_write(xbt, dev->nodename, "feature-gso-tcpv6", "1");
	if (err) {
		message = "writing feature-gso-tcpv6";
		goto abort_transaction;
	}

	err = xenbus_write(xbt, dev->nodename, "feature-ipv6-csum-offload",
			   "1");
	if (err) {
		message = "writing feature-ipv6-csum-offload";
		goto abort_transaction;
	}

	err = xenbus_transaction_end(xbt, 0);
	if (err) {
		if (err == -EAGAIN)
			goto again;
		xenbus_dev_fatal(dev, err, "completing transaction");
		goto destroy_ring;
	}

	return 0;

 abort_transaction:
	xenbus_transaction_end(xbt, 1);
	xenbus_dev_fatal(dev, err, "%s", message);
 destroy_ring:
	xennet_disconnect_backend(info);
	kfree(info->queues);
	info->queues = NULL;
	rtnl_lock();
	netif_set_real_num_tx_queues(info->netdev, 0);
	rtnl_lock();
 out:
	return err;
}

static int xennet_connect(struct net_device *dev)
{
	struct netfront_info *np = netdev_priv(dev);
	unsigned int num_queues = 0;
	int i, requeue_idx, err;
	struct sk_buff *skb;
	grant_ref_t ref;
	struct xen_netif_rx_request *req;
	unsigned int feature_rx_copy;
	unsigned int j = 0;
	struct netfront_queue *queue = NULL;

	err = xenbus_scanf(XBT_NIL, np->xbdev->otherend,
			   "feature-rx-copy", "%u", &feature_rx_copy);
	if (err != 1)
		feature_rx_copy = 0;

	if (!feature_rx_copy) {
		dev_info(&dev->dev,
			 "backend does not support copying receive path\n");
		return -ENODEV;
	}

	err = talk_to_netback(np->xbdev, np);
	if (err)
		return err;

	/* talk_to_netback() sets the correct number of queues */
	num_queues = dev->real_num_tx_queues;

	rtnl_lock();
	netdev_update_features(dev);
	rtnl_unlock();

	/* By now, the queue structures have been set up */
	for (j = 0; j < num_queues; ++j) {
		queue = &np->queues[j];
		spin_lock_bh(&queue->rx_lock);
		spin_lock_irq(&queue->tx_lock);

		/* Step 1: Discard all pending TX packet fragments. */
		xennet_release_tx_bufs(queue);

		/* Step 2: Rebuild the RX buffer freelist and the RX ring itself. */
		for (requeue_idx = 0, i = 0; i < NET_RX_RING_SIZE; i++) {
			skb_frag_t *frag;
			const struct page *page;
			if (!queue->rx_skbs[i])
				continue;

			skb = queue->rx_skbs[requeue_idx] = xennet_get_rx_skb(queue, i);
			ref = queue->grant_rx_ref[requeue_idx] = xennet_get_rx_ref(queue, i);
			req = RING_GET_REQUEST(&queue->rx, requeue_idx);

			frag = &skb_shinfo(skb)->frags[0];
			page = skb_frag_page(frag);
			gnttab_grant_foreign_access_ref(
				ref, queue->info->xbdev->otherend_id,
				pfn_to_mfn(page_to_pfn(page)),
				0);
			req->gref = ref;
			req->id   = requeue_idx;

			requeue_idx++;
		}

		queue->rx.req_prod_pvt = requeue_idx;
	}

	/*
	 * Step 3: All public and private state should now be sane.  Get
	 * ready to start sending and receiving packets and give the driver
	 * domain a kick because we've probably just requeued some
	 * packets.
	 */
	netif_carrier_on(np->netdev);
	for (j = 0; j < num_queues; ++j) {
		queue = &np->queues[j];
		notify_remote_via_irq(queue->tx_irq);
		if (queue->tx_irq != queue->rx_irq)
			notify_remote_via_irq(queue->rx_irq);
		xennet_tx_buf_gc(queue);
		xennet_alloc_rx_buffers(queue);

		spin_unlock_irq(&queue->tx_lock);
		spin_unlock_bh(&queue->rx_lock);
	}

	return 0;
}

/**
 * Callback received when the backend's state changes.
 */
static void netback_changed(struct xenbus_device *dev,
			    enum xenbus_state backend_state)
{
	struct netfront_info *np = dev_get_drvdata(&dev->dev);
	struct net_device *netdev = np->netdev;

	dev_dbg(&dev->dev, "%s\n", xenbus_strstate(backend_state));

	switch (backend_state) {
	case XenbusStateInitialising:
	case XenbusStateInitialised:
	case XenbusStateReconfiguring:
	case XenbusStateReconfigured:
	case XenbusStateUnknown:
		break;

	case XenbusStateInitWait:
		if (dev->state != XenbusStateInitialising)
			break;
		if (xennet_connect(netdev) != 0)
			break;
		xenbus_switch_state(dev, XenbusStateConnected);
		break;

	case XenbusStateConnected:
		netdev_notify_peers(netdev);
		break;

	case XenbusStateClosed:
		if (dev->state == XenbusStateClosed)
			break;
		/* Missed the backend's CLOSING state -- fallthrough */
	case XenbusStateClosing:
		xenbus_frontend_closed(dev);
		break;
	}
}

static const struct xennet_stat {
	char name[ETH_GSTRING_LEN];
	u16 offset;
} xennet_stats[] = {
	{
		"rx_gso_checksum_fixup",
		offsetof(struct netfront_info, rx_gso_checksum_fixup)
	},
};

static int xennet_get_sset_count(struct net_device *dev, int string_set)
{
	switch (string_set) {
	case ETH_SS_STATS:
		return ARRAY_SIZE(xennet_stats);
	default:
		return -EINVAL;
	}
}

static void xennet_get_ethtool_stats(struct net_device *dev,
				     struct ethtool_stats *stats, u64 * data)
{
	void *np = netdev_priv(dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(xennet_stats); i++)
		data[i] = atomic_read((atomic_t *)(np + xennet_stats[i].offset));
}

static void xennet_get_strings(struct net_device *dev, u32 stringset, u8 * data)
{
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < ARRAY_SIZE(xennet_stats); i++)
			memcpy(data + i * ETH_GSTRING_LEN,
			       xennet_stats[i].name, ETH_GSTRING_LEN);
		break;
	}
}

static const struct ethtool_ops xennet_ethtool_ops =
{
	.get_link = ethtool_op_get_link,

	.get_sset_count = xennet_get_sset_count,
	.get_ethtool_stats = xennet_get_ethtool_stats,
	.get_strings = xennet_get_strings,
};

#ifdef CONFIG_SYSFS
static ssize_t show_rxbuf_min(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct net_device *netdev = to_net_dev(dev);
	struct netfront_info *info = netdev_priv(netdev);
	unsigned int num_queues = netdev->real_num_tx_queues;

	if (num_queues)
		return sprintf(buf, "%u\n", info->queues[0].rx_min_target);
	else
		return sprintf(buf, "%u\n", RX_MIN_TARGET);
}

static ssize_t store_rxbuf_min(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t len)
{
	struct net_device *netdev = to_net_dev(dev);
	struct netfront_info *np = netdev_priv(netdev);
	unsigned int num_queues = netdev->real_num_tx_queues;
	char *endp;
	unsigned long target;
	unsigned int i;
	struct netfront_queue *queue;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	target = simple_strtoul(buf, &endp, 0);
	if (endp == buf)
		return -EBADMSG;

	if (target < RX_MIN_TARGET)
		target = RX_MIN_TARGET;
	if (target > RX_MAX_TARGET)
		target = RX_MAX_TARGET;

	for (i = 0; i < num_queues; ++i) {
		queue = &np->queues[i];
		spin_lock_bh(&queue->rx_lock);
		if (target > queue->rx_max_target)
			queue->rx_max_target = target;
		queue->rx_min_target = target;
		if (target > queue->rx_target)
			queue->rx_target = target;

		xennet_alloc_rx_buffers(queue);

		spin_unlock_bh(&queue->rx_lock);
	}
	return len;
}

static ssize_t show_rxbuf_max(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct net_device *netdev = to_net_dev(dev);
	struct netfront_info *info = netdev_priv(netdev);
	unsigned int num_queues = netdev->real_num_tx_queues;

	if (num_queues)
		return sprintf(buf, "%u\n", info->queues[0].rx_max_target);
	else
		return sprintf(buf, "%u\n", RX_MAX_TARGET);
}

static ssize_t store_rxbuf_max(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t len)
{
	struct net_device *netdev = to_net_dev(dev);
	struct netfront_info *np = netdev_priv(netdev);
	unsigned int num_queues = netdev->real_num_tx_queues;
	char *endp;
	unsigned long target;
	unsigned int i = 0;
	struct netfront_queue *queue = NULL;

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	target = simple_strtoul(buf, &endp, 0);
	if (endp == buf)
		return -EBADMSG;

	if (target < RX_MIN_TARGET)
		target = RX_MIN_TARGET;
	if (target > RX_MAX_TARGET)
		target = RX_MAX_TARGET;

	for (i = 0; i < num_queues; ++i) {
		queue = &np->queues[i];
		spin_lock_bh(&queue->rx_lock);
		if (target < queue->rx_min_target)
			queue->rx_min_target = target;
		queue->rx_max_target = target;
		if (target < queue->rx_target)
			queue->rx_target = target;

		xennet_alloc_rx_buffers(queue);

		spin_unlock_bh(&queue->rx_lock);
	}
	return len;
}

static ssize_t show_rxbuf_cur(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct net_device *netdev = to_net_dev(dev);
	struct netfront_info *info = netdev_priv(netdev);
	unsigned int num_queues = netdev->real_num_tx_queues;

	if (num_queues)
		return sprintf(buf, "%u\n", info->queues[0].rx_target);
	else
		return sprintf(buf, "0\n");
}

static struct device_attribute xennet_attrs[] = {
	__ATTR(rxbuf_min, S_IRUGO|S_IWUSR, show_rxbuf_min, store_rxbuf_min),
	__ATTR(rxbuf_max, S_IRUGO|S_IWUSR, show_rxbuf_max, store_rxbuf_max),
	__ATTR(rxbuf_cur, S_IRUGO, show_rxbuf_cur, NULL),
};

static int xennet_sysfs_addif(struct net_device *netdev)
{
	int i;
	int err;

	for (i = 0; i < ARRAY_SIZE(xennet_attrs); i++) {
		err = device_create_file(&netdev->dev,
					   &xennet_attrs[i]);
		if (err)
			goto fail;
	}
	return 0;

 fail:
	while (--i >= 0)
		device_remove_file(&netdev->dev, &xennet_attrs[i]);
	return err;
}

static void xennet_sysfs_delif(struct net_device *netdev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(xennet_attrs); i++)
		device_remove_file(&netdev->dev, &xennet_attrs[i]);
}

#endif /* CONFIG_SYSFS */

static const struct xenbus_device_id netfront_ids[] = {
	{ "vif" },
	{ "" }
};


static int xennet_remove(struct xenbus_device *dev)
{
	struct netfront_info *info = dev_get_drvdata(&dev->dev);
	unsigned int num_queues = info->netdev->real_num_tx_queues;
	struct netfront_queue *queue = NULL;
	unsigned int i = 0;

	dev_dbg(&dev->dev, "%s\n", dev->nodename);

	xennet_disconnect_backend(info);

	xennet_sysfs_delif(info->netdev);

	unregister_netdev(info->netdev);

	for (i = 0; i < num_queues; ++i) {
		queue = &info->queues[i];
		del_timer_sync(&queue->rx_refill_timer);
	}

	if (num_queues) {
		kfree(info->queues);
		info->queues = NULL;
	}

	free_percpu(info->stats);

	free_netdev(info->netdev);

	return 0;
}

static DEFINE_XENBUS_DRIVER(netfront, ,
	.probe = netfront_probe,
	.remove = xennet_remove,
	.resume = netfront_resume,
	.otherend_changed = netback_changed,
);

static int __init netif_init(void)
{
	if (!xen_domain())
		return -ENODEV;

	if (!xen_has_pv_nic_devices())
		return -ENODEV;

	pr_info("Initialising Xen virtual ethernet driver\n");

	return xenbus_register_frontend(&netfront_driver);
}
module_init(netif_init);


static void __exit netif_exit(void)
{
	xenbus_unregister_driver(&netfront_driver);
}
module_exit(netif_exit);

MODULE_DESCRIPTION("Xen virtual network device frontend");
MODULE_LICENSE("GPL");
MODULE_ALIAS("xen:vif");
MODULE_ALIAS("xennet");
