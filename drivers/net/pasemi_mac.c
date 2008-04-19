/*
 * Copyright (C) 2006-2007 PA Semi, Inc
 *
 * Driver for the PA Semi PWRficient onchip 1G/10G Ethernet MACs
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/dmaengine.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <asm/dma-mapping.h>
#include <linux/in.h>
#include <linux/skbuff.h>

#include <linux/ip.h>
#include <linux/tcp.h>
#include <net/checksum.h>
#include <linux/inet_lro.h>

#include <asm/irq.h>
#include <asm/firmware.h>
#include <asm/pasemi_dma.h>

#include "pasemi_mac.h"

/* We have our own align, since ppc64 in general has it at 0 because
 * of design flaws in some of the server bridge chips. However, for
 * PWRficient doing the unaligned copies is more expensive than doing
 * unaligned DMA, so make sure the data is aligned instead.
 */
#define LOCAL_SKB_ALIGN	2

/* TODO list
 *
 * - Multicast support
 * - Large MTU support
 * - SW LRO
 * - Multiqueue RX/TX
 */


/* Must be a power of two */
#define RX_RING_SIZE 2048
#define TX_RING_SIZE 4096

#define LRO_MAX_AGGR 64

#define PE_MIN_MTU	64
#define PE_MAX_MTU	1500
#define PE_DEF_MTU	ETH_DATA_LEN

#define DEFAULT_MSG_ENABLE	  \
	(NETIF_MSG_DRV		| \
	 NETIF_MSG_PROBE	| \
	 NETIF_MSG_LINK		| \
	 NETIF_MSG_TIMER	| \
	 NETIF_MSG_IFDOWN	| \
	 NETIF_MSG_IFUP		| \
	 NETIF_MSG_RX_ERR	| \
	 NETIF_MSG_TX_ERR)

#define TX_DESC(tx, num)	((tx)->chan.ring_virt[(num) & (TX_RING_SIZE-1)])
#define TX_DESC_INFO(tx, num)	((tx)->ring_info[(num) & (TX_RING_SIZE-1)])
#define RX_DESC(rx, num)	((rx)->chan.ring_virt[(num) & (RX_RING_SIZE-1)])
#define RX_DESC_INFO(rx, num)	((rx)->ring_info[(num) & (RX_RING_SIZE-1)])
#define RX_BUFF(rx, num)	((rx)->buffers[(num) & (RX_RING_SIZE-1)])

#define RING_USED(ring)		(((ring)->next_to_fill - (ring)->next_to_clean) \
				 & ((ring)->size - 1))
#define RING_AVAIL(ring)	((ring->size) - RING_USED(ring))

MODULE_LICENSE("GPL");
MODULE_AUTHOR ("Olof Johansson <olof@lixom.net>");
MODULE_DESCRIPTION("PA Semi PWRficient Ethernet driver");

static int debug = -1;	/* -1 == use DEFAULT_MSG_ENABLE as value */
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "PA Semi MAC bitmapped debugging message enable value");

static int translation_enabled(void)
{
#if defined(CONFIG_PPC_PASEMI_IOMMU_DMA_FORCE)
	return 1;
#else
	return firmware_has_feature(FW_FEATURE_LPAR);
#endif
}

static void write_iob_reg(unsigned int reg, unsigned int val)
{
	pasemi_write_iob_reg(reg, val);
}

static unsigned int read_mac_reg(const struct pasemi_mac *mac, unsigned int reg)
{
	return pasemi_read_mac_reg(mac->dma_if, reg);
}

static void write_mac_reg(const struct pasemi_mac *mac, unsigned int reg,
			  unsigned int val)
{
	pasemi_write_mac_reg(mac->dma_if, reg, val);
}

static unsigned int read_dma_reg(unsigned int reg)
{
	return pasemi_read_dma_reg(reg);
}

static void write_dma_reg(unsigned int reg, unsigned int val)
{
	pasemi_write_dma_reg(reg, val);
}

static struct pasemi_mac_rxring *rx_ring(const struct pasemi_mac *mac)
{
	return mac->rx;
}

static struct pasemi_mac_txring *tx_ring(const struct pasemi_mac *mac)
{
	return mac->tx;
}

static inline void prefetch_skb(const struct sk_buff *skb)
{
	const void *d = skb;

	prefetch(d);
	prefetch(d+64);
	prefetch(d+128);
	prefetch(d+192);
}

static int mac_to_intf(struct pasemi_mac *mac)
{
	struct pci_dev *pdev = mac->pdev;
	u32 tmp;
	int nintf, off, i, j;
	int devfn = pdev->devfn;

	tmp = read_dma_reg(PAS_DMA_CAP_IFI);
	nintf = (tmp & PAS_DMA_CAP_IFI_NIN_M) >> PAS_DMA_CAP_IFI_NIN_S;
	off = (tmp & PAS_DMA_CAP_IFI_IOFF_M) >> PAS_DMA_CAP_IFI_IOFF_S;

	/* IOFF contains the offset to the registers containing the
	 * DMA interface-to-MAC-pci-id mappings, and NIN contains number
	 * of total interfaces. Each register contains 4 devfns.
	 * Just do a linear search until we find the devfn of the MAC
	 * we're trying to look up.
	 */

	for (i = 0; i < (nintf+3)/4; i++) {
		tmp = read_dma_reg(off+4*i);
		for (j = 0; j < 4; j++) {
			if (((tmp >> (8*j)) & 0xff) == devfn)
				return i*4 + j;
		}
	}
	return -1;
}

static void pasemi_mac_intf_disable(struct pasemi_mac *mac)
{
	unsigned int flags;

	flags = read_mac_reg(mac, PAS_MAC_CFG_PCFG);
	flags &= ~PAS_MAC_CFG_PCFG_PE;
	write_mac_reg(mac, PAS_MAC_CFG_PCFG, flags);
}

static void pasemi_mac_intf_enable(struct pasemi_mac *mac)
{
	unsigned int flags;

	flags = read_mac_reg(mac, PAS_MAC_CFG_PCFG);
	flags |= PAS_MAC_CFG_PCFG_PE;
	write_mac_reg(mac, PAS_MAC_CFG_PCFG, flags);
}

static int pasemi_get_mac_addr(struct pasemi_mac *mac)
{
	struct pci_dev *pdev = mac->pdev;
	struct device_node *dn = pci_device_to_OF_node(pdev);
	int len;
	const u8 *maddr;
	u8 addr[6];

	if (!dn) {
		dev_dbg(&pdev->dev,
			  "No device node for mac, not configuring\n");
		return -ENOENT;
	}

	maddr = of_get_property(dn, "local-mac-address", &len);

	if (maddr && len == 6) {
		memcpy(mac->mac_addr, maddr, 6);
		return 0;
	}

	/* Some old versions of firmware mistakenly uses mac-address
	 * (and as a string) instead of a byte array in local-mac-address.
	 */

	if (maddr == NULL)
		maddr = of_get_property(dn, "mac-address", NULL);

	if (maddr == NULL) {
		dev_warn(&pdev->dev,
			 "no mac address in device tree, not configuring\n");
		return -ENOENT;
	}

	if (sscanf(maddr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &addr[0],
		   &addr[1], &addr[2], &addr[3], &addr[4], &addr[5]) != 6) {
		dev_warn(&pdev->dev,
			 "can't parse mac address, not configuring\n");
		return -EINVAL;
	}

	memcpy(mac->mac_addr, addr, 6);

	return 0;
}

static int pasemi_mac_set_mac_addr(struct net_device *dev, void *p)
{
	struct pasemi_mac *mac = netdev_priv(dev);
	struct sockaddr *addr = p;
	unsigned int adr0, adr1;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EINVAL;

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);

	adr0 = dev->dev_addr[2] << 24 |
	       dev->dev_addr[3] << 16 |
	       dev->dev_addr[4] << 8 |
	       dev->dev_addr[5];
	adr1 = read_mac_reg(mac, PAS_MAC_CFG_ADR1);
	adr1 &= ~0xffff;
	adr1 |= dev->dev_addr[0] << 8 | dev->dev_addr[1];

	pasemi_mac_intf_disable(mac);
	write_mac_reg(mac, PAS_MAC_CFG_ADR0, adr0);
	write_mac_reg(mac, PAS_MAC_CFG_ADR1, adr1);
	pasemi_mac_intf_enable(mac);

	return 0;
}

static int get_skb_hdr(struct sk_buff *skb, void **iphdr,
		       void **tcph, u64 *hdr_flags, void *data)
{
	u64 macrx = (u64) data;
	unsigned int ip_len;
	struct iphdr *iph;

	/* IPv4 header checksum failed */
	if ((macrx & XCT_MACRX_HTY_M) != XCT_MACRX_HTY_IPV4_OK)
		return -1;

	/* non tcp packet */
	skb_reset_network_header(skb);
	iph = ip_hdr(skb);
	if (iph->protocol != IPPROTO_TCP)
		return -1;

	ip_len = ip_hdrlen(skb);
	skb_set_transport_header(skb, ip_len);
	*tcph = tcp_hdr(skb);

	/* check if ip header and tcp header are complete */
	if (iph->tot_len < ip_len + tcp_hdrlen(skb))
		return -1;

	*hdr_flags = LRO_IPV4 | LRO_TCP;
	*iphdr = iph;

	return 0;
}

static int pasemi_mac_unmap_tx_skb(struct pasemi_mac *mac,
				    const int nfrags,
				    struct sk_buff *skb,
				    const dma_addr_t *dmas)
{
	int f;
	struct pci_dev *pdev = mac->dma_pdev;

	pci_unmap_single(pdev, dmas[0], skb_headlen(skb), PCI_DMA_TODEVICE);

	for (f = 0; f < nfrags; f++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[f];

		pci_unmap_page(pdev, dmas[f+1], frag->size, PCI_DMA_TODEVICE);
	}
	dev_kfree_skb_irq(skb);

	/* Freed descriptor slot + main SKB ptr + nfrags additional ptrs,
	 * aligned up to a power of 2
	 */
	return (nfrags + 3) & ~1;
}

static int pasemi_mac_setup_rx_resources(const struct net_device *dev)
{
	struct pasemi_mac_rxring *ring;
	struct pasemi_mac *mac = netdev_priv(dev);
	int chno;
	unsigned int cfg;

	ring = pasemi_dma_alloc_chan(RXCHAN, sizeof(struct pasemi_mac_rxring),
				     offsetof(struct pasemi_mac_rxring, chan));

	if (!ring) {
		dev_err(&mac->pdev->dev, "Can't allocate RX channel\n");
		goto out_chan;
	}
	chno = ring->chan.chno;

	spin_lock_init(&ring->lock);

	ring->size = RX_RING_SIZE;
	ring->ring_info = kzalloc(sizeof(struct pasemi_mac_buffer) *
				  RX_RING_SIZE, GFP_KERNEL);

	if (!ring->ring_info)
		goto out_ring_info;

	/* Allocate descriptors */
	if (pasemi_dma_alloc_ring(&ring->chan, RX_RING_SIZE))
		goto out_ring_desc;

	ring->buffers = dma_alloc_coherent(&mac->dma_pdev->dev,
					   RX_RING_SIZE * sizeof(u64),
					   &ring->buf_dma, GFP_KERNEL);
	if (!ring->buffers)
		goto out_ring_desc;

	memset(ring->buffers, 0, RX_RING_SIZE * sizeof(u64));

	write_dma_reg(PAS_DMA_RXCHAN_BASEL(chno),
		      PAS_DMA_RXCHAN_BASEL_BRBL(ring->chan.ring_dma));

	write_dma_reg(PAS_DMA_RXCHAN_BASEU(chno),
		      PAS_DMA_RXCHAN_BASEU_BRBH(ring->chan.ring_dma >> 32) |
		      PAS_DMA_RXCHAN_BASEU_SIZ(RX_RING_SIZE >> 3));

	cfg = PAS_DMA_RXCHAN_CFG_HBU(2);

	if (translation_enabled())
		cfg |= PAS_DMA_RXCHAN_CFG_CTR;

	write_dma_reg(PAS_DMA_RXCHAN_CFG(chno), cfg);

	write_dma_reg(PAS_DMA_RXINT_BASEL(mac->dma_if),
		      PAS_DMA_RXINT_BASEL_BRBL(ring->buf_dma));

	write_dma_reg(PAS_DMA_RXINT_BASEU(mac->dma_if),
		      PAS_DMA_RXINT_BASEU_BRBH(ring->buf_dma >> 32) |
		      PAS_DMA_RXINT_BASEU_SIZ(RX_RING_SIZE >> 3));

	cfg = PAS_DMA_RXINT_CFG_DHL(2) | PAS_DMA_RXINT_CFG_L2 |
	      PAS_DMA_RXINT_CFG_LW | PAS_DMA_RXINT_CFG_RBP |
	      PAS_DMA_RXINT_CFG_HEN;

	if (translation_enabled())
		cfg |= PAS_DMA_RXINT_CFG_ITRR | PAS_DMA_RXINT_CFG_ITR;

	write_dma_reg(PAS_DMA_RXINT_CFG(mac->dma_if), cfg);

	ring->next_to_fill = 0;
	ring->next_to_clean = 0;
	ring->mac = mac;
	mac->rx = ring;

	return 0;

out_ring_desc:
	kfree(ring->ring_info);
out_ring_info:
	pasemi_dma_free_chan(&ring->chan);
out_chan:
	return -ENOMEM;
}

static struct pasemi_mac_txring *
pasemi_mac_setup_tx_resources(const struct net_device *dev)
{
	struct pasemi_mac *mac = netdev_priv(dev);
	u32 val;
	struct pasemi_mac_txring *ring;
	unsigned int cfg;
	int chno;

	ring = pasemi_dma_alloc_chan(TXCHAN, sizeof(struct pasemi_mac_txring),
				     offsetof(struct pasemi_mac_txring, chan));

	if (!ring) {
		dev_err(&mac->pdev->dev, "Can't allocate TX channel\n");
		goto out_chan;
	}

	chno = ring->chan.chno;

	spin_lock_init(&ring->lock);

	ring->size = TX_RING_SIZE;
	ring->ring_info = kzalloc(sizeof(struct pasemi_mac_buffer) *
				  TX_RING_SIZE, GFP_KERNEL);
	if (!ring->ring_info)
		goto out_ring_info;

	/* Allocate descriptors */
	if (pasemi_dma_alloc_ring(&ring->chan, TX_RING_SIZE))
		goto out_ring_desc;

	write_dma_reg(PAS_DMA_TXCHAN_BASEL(chno),
		      PAS_DMA_TXCHAN_BASEL_BRBL(ring->chan.ring_dma));
	val = PAS_DMA_TXCHAN_BASEU_BRBH(ring->chan.ring_dma >> 32);
	val |= PAS_DMA_TXCHAN_BASEU_SIZ(TX_RING_SIZE >> 3);

	write_dma_reg(PAS_DMA_TXCHAN_BASEU(chno), val);

	cfg = PAS_DMA_TXCHAN_CFG_TY_IFACE |
	      PAS_DMA_TXCHAN_CFG_TATTR(mac->dma_if) |
	      PAS_DMA_TXCHAN_CFG_UP |
	      PAS_DMA_TXCHAN_CFG_WT(2);

	if (translation_enabled())
		cfg |= PAS_DMA_TXCHAN_CFG_TRD | PAS_DMA_TXCHAN_CFG_TRR;

	write_dma_reg(PAS_DMA_TXCHAN_CFG(chno), cfg);

	ring->next_to_fill = 0;
	ring->next_to_clean = 0;
	ring->mac = mac;

	return ring;

out_ring_desc:
	kfree(ring->ring_info);
out_ring_info:
	pasemi_dma_free_chan(&ring->chan);
out_chan:
	return NULL;
}

static void pasemi_mac_free_tx_resources(struct pasemi_mac *mac)
{
	struct pasemi_mac_txring *txring = tx_ring(mac);
	unsigned int i, j;
	struct pasemi_mac_buffer *info;
	dma_addr_t dmas[MAX_SKB_FRAGS+1];
	int freed, nfrags;
	int start, limit;

	start = txring->next_to_clean;
	limit = txring->next_to_fill;

	/* Compensate for when fill has wrapped and clean has not */
	if (start > limit)
		limit += TX_RING_SIZE;

	for (i = start; i < limit; i += freed) {
		info = &txring->ring_info[(i+1) & (TX_RING_SIZE-1)];
		if (info->dma && info->skb) {
			nfrags = skb_shinfo(info->skb)->nr_frags;
			for (j = 0; j <= nfrags; j++)
				dmas[j] = txring->ring_info[(i+1+j) &
						(TX_RING_SIZE-1)].dma;
			freed = pasemi_mac_unmap_tx_skb(mac, nfrags,
							info->skb, dmas);
		} else
			freed = 2;
	}

	kfree(txring->ring_info);
	pasemi_dma_free_chan(&txring->chan);

}

static void pasemi_mac_free_rx_buffers(struct pasemi_mac *mac)
{
	struct pasemi_mac_rxring *rx = rx_ring(mac);
	unsigned int i;
	struct pasemi_mac_buffer *info;

	for (i = 0; i < RX_RING_SIZE; i++) {
		info = &RX_DESC_INFO(rx, i);
		if (info->skb && info->dma) {
			pci_unmap_single(mac->dma_pdev,
					 info->dma,
					 info->skb->len,
					 PCI_DMA_FROMDEVICE);
			dev_kfree_skb_any(info->skb);
		}
		info->dma = 0;
		info->skb = NULL;
	}

	for (i = 0; i < RX_RING_SIZE; i++)
		RX_BUFF(rx, i) = 0;
}

static void pasemi_mac_free_rx_resources(struct pasemi_mac *mac)
{
	pasemi_mac_free_rx_buffers(mac);

	dma_free_coherent(&mac->dma_pdev->dev, RX_RING_SIZE * sizeof(u64),
			  rx_ring(mac)->buffers, rx_ring(mac)->buf_dma);

	kfree(rx_ring(mac)->ring_info);
	pasemi_dma_free_chan(&rx_ring(mac)->chan);
	mac->rx = NULL;
}

static void pasemi_mac_replenish_rx_ring(const struct net_device *dev,
					 const int limit)
{
	const struct pasemi_mac *mac = netdev_priv(dev);
	struct pasemi_mac_rxring *rx = rx_ring(mac);
	int fill, count;

	if (limit <= 0)
		return;

	fill = rx_ring(mac)->next_to_fill;
	for (count = 0; count < limit; count++) {
		struct pasemi_mac_buffer *info = &RX_DESC_INFO(rx, fill);
		u64 *buff = &RX_BUFF(rx, fill);
		struct sk_buff *skb;
		dma_addr_t dma;

		/* Entry in use? */
		WARN_ON(*buff);

		skb = dev_alloc_skb(mac->bufsz);
		skb_reserve(skb, LOCAL_SKB_ALIGN);

		if (unlikely(!skb))
			break;

		dma = pci_map_single(mac->dma_pdev, skb->data,
				     mac->bufsz - LOCAL_SKB_ALIGN,
				     PCI_DMA_FROMDEVICE);

		if (unlikely(dma_mapping_error(dma))) {
			dev_kfree_skb_irq(info->skb);
			break;
		}

		info->skb = skb;
		info->dma = dma;
		*buff = XCT_RXB_LEN(mac->bufsz) | XCT_RXB_ADDR(dma);
		fill++;
	}

	wmb();

	write_dma_reg(PAS_DMA_RXINT_INCR(mac->dma_if), count);

	rx_ring(mac)->next_to_fill = (rx_ring(mac)->next_to_fill + count) &
				(RX_RING_SIZE - 1);
}

static void pasemi_mac_restart_rx_intr(const struct pasemi_mac *mac)
{
	struct pasemi_mac_rxring *rx = rx_ring(mac);
	unsigned int reg, pcnt;
	/* Re-enable packet count interrupts: finally
	 * ack the packet count interrupt we got in rx_intr.
	 */

	pcnt = *rx->chan.status & PAS_STATUS_PCNT_M;

	reg = PAS_IOB_DMA_RXCH_RESET_PCNT(pcnt) | PAS_IOB_DMA_RXCH_RESET_PINTC;

	if (*rx->chan.status & PAS_STATUS_TIMER)
		reg |= PAS_IOB_DMA_RXCH_RESET_TINTC;

	write_iob_reg(PAS_IOB_DMA_RXCH_RESET(mac->rx->chan.chno), reg);
}

static void pasemi_mac_restart_tx_intr(const struct pasemi_mac *mac)
{
	unsigned int reg, pcnt;

	/* Re-enable packet count interrupts */
	pcnt = *tx_ring(mac)->chan.status & PAS_STATUS_PCNT_M;

	reg = PAS_IOB_DMA_TXCH_RESET_PCNT(pcnt) | PAS_IOB_DMA_TXCH_RESET_PINTC;

	write_iob_reg(PAS_IOB_DMA_TXCH_RESET(tx_ring(mac)->chan.chno), reg);
}


static inline void pasemi_mac_rx_error(const struct pasemi_mac *mac,
				       const u64 macrx)
{
	unsigned int rcmdsta, ccmdsta;
	struct pasemi_dmachan *chan = &rx_ring(mac)->chan;

	if (!netif_msg_rx_err(mac))
		return;

	rcmdsta = read_dma_reg(PAS_DMA_RXINT_RCMDSTA(mac->dma_if));
	ccmdsta = read_dma_reg(PAS_DMA_RXCHAN_CCMDSTA(chan->chno));

	printk(KERN_ERR "pasemi_mac: rx error. macrx %016lx, rx status %lx\n",
		macrx, *chan->status);

	printk(KERN_ERR "pasemi_mac: rcmdsta %08x ccmdsta %08x\n",
		rcmdsta, ccmdsta);
}

static inline void pasemi_mac_tx_error(const struct pasemi_mac *mac,
				       const u64 mactx)
{
	unsigned int cmdsta;
	struct pasemi_dmachan *chan = &tx_ring(mac)->chan;

	if (!netif_msg_tx_err(mac))
		return;

	cmdsta = read_dma_reg(PAS_DMA_TXCHAN_TCMDSTA(chan->chno));

	printk(KERN_ERR "pasemi_mac: tx error. mactx 0x%016lx, "\
		"tx status 0x%016lx\n", mactx, *chan->status);

	printk(KERN_ERR "pasemi_mac: tcmdsta 0x%08x\n", cmdsta);
}

static int pasemi_mac_clean_rx(struct pasemi_mac_rxring *rx,
			       const int limit)
{
	const struct pasemi_dmachan *chan = &rx->chan;
	struct pasemi_mac *mac = rx->mac;
	struct pci_dev *pdev = mac->dma_pdev;
	unsigned int n;
	int count, buf_index, tot_bytes, packets;
	struct pasemi_mac_buffer *info;
	struct sk_buff *skb;
	unsigned int len;
	u64 macrx, eval;
	dma_addr_t dma;

	tot_bytes = 0;
	packets = 0;

	spin_lock(&rx->lock);

	n = rx->next_to_clean;

	prefetch(&RX_DESC(rx, n));

	for (count = 0; count < limit; count++) {
		macrx = RX_DESC(rx, n);
		prefetch(&RX_DESC(rx, n+4));

		if ((macrx & XCT_MACRX_E) ||
		    (*chan->status & PAS_STATUS_ERROR))
			pasemi_mac_rx_error(mac, macrx);

		if (!(macrx & XCT_MACRX_O))
			break;

		info = NULL;

		BUG_ON(!(macrx & XCT_MACRX_RR_8BRES));

		eval = (RX_DESC(rx, n+1) & XCT_RXRES_8B_EVAL_M) >>
			XCT_RXRES_8B_EVAL_S;
		buf_index = eval-1;

		dma = (RX_DESC(rx, n+2) & XCT_PTR_ADDR_M);
		info = &RX_DESC_INFO(rx, buf_index);

		skb = info->skb;

		prefetch_skb(skb);

		len = (macrx & XCT_MACRX_LLEN_M) >> XCT_MACRX_LLEN_S;

		pci_unmap_single(pdev, dma, mac->bufsz - LOCAL_SKB_ALIGN,
				 PCI_DMA_FROMDEVICE);

		if (macrx & XCT_MACRX_CRC) {
			/* CRC error flagged */
			mac->netdev->stats.rx_errors++;
			mac->netdev->stats.rx_crc_errors++;
			/* No need to free skb, it'll be reused */
			goto next;
		}

		info->skb = NULL;
		info->dma = 0;

		if (likely((macrx & XCT_MACRX_HTY_M) == XCT_MACRX_HTY_IPV4_OK)) {
			skb->ip_summed = CHECKSUM_UNNECESSARY;
			skb->csum = (macrx & XCT_MACRX_CSUM_M) >>
					   XCT_MACRX_CSUM_S;
		} else
			skb->ip_summed = CHECKSUM_NONE;

		packets++;
		tot_bytes += len;

		/* Don't include CRC */
		skb_put(skb, len-4);

		skb->protocol = eth_type_trans(skb, mac->netdev);
		lro_receive_skb(&mac->lro_mgr, skb, (void *)macrx);

next:
		RX_DESC(rx, n) = 0;
		RX_DESC(rx, n+1) = 0;

		/* Need to zero it out since hardware doesn't, since the
		 * replenish loop uses it to tell when it's done.
		 */
		RX_BUFF(rx, buf_index) = 0;

		n += 4;
	}

	if (n > RX_RING_SIZE) {
		/* Errata 5971 workaround: L2 target of headers */
		write_iob_reg(PAS_IOB_COM_PKTHDRCNT, 0);
		n &= (RX_RING_SIZE-1);
	}

	rx_ring(mac)->next_to_clean = n;

	lro_flush_all(&mac->lro_mgr);

	/* Increase is in number of 16-byte entries, and since each descriptor
	 * with an 8BRES takes up 3x8 bytes (padded to 4x8), increase with
	 * count*2.
	 */
	write_dma_reg(PAS_DMA_RXCHAN_INCR(mac->rx->chan.chno), count << 1);

	pasemi_mac_replenish_rx_ring(mac->netdev, count);

	mac->netdev->stats.rx_bytes += tot_bytes;
	mac->netdev->stats.rx_packets += packets;

	spin_unlock(&rx_ring(mac)->lock);

	return count;
}

/* Can't make this too large or we blow the kernel stack limits */
#define TX_CLEAN_BATCHSIZE (128/MAX_SKB_FRAGS)

static int pasemi_mac_clean_tx(struct pasemi_mac_txring *txring)
{
	struct pasemi_dmachan *chan = &txring->chan;
	struct pasemi_mac *mac = txring->mac;
	int i, j;
	unsigned int start, descr_count, buf_count, batch_limit;
	unsigned int ring_limit;
	unsigned int total_count;
	unsigned long flags;
	struct sk_buff *skbs[TX_CLEAN_BATCHSIZE];
	dma_addr_t dmas[TX_CLEAN_BATCHSIZE][MAX_SKB_FRAGS+1];
	int nf[TX_CLEAN_BATCHSIZE];
	int nr_frags;

	total_count = 0;
	batch_limit = TX_CLEAN_BATCHSIZE;
restart:
	spin_lock_irqsave(&txring->lock, flags);

	start = txring->next_to_clean;
	ring_limit = txring->next_to_fill;

	prefetch(&TX_DESC_INFO(txring, start+1).skb);

	/* Compensate for when fill has wrapped but clean has not */
	if (start > ring_limit)
		ring_limit += TX_RING_SIZE;

	buf_count = 0;
	descr_count = 0;

	for (i = start;
	     descr_count < batch_limit && i < ring_limit;
	     i += buf_count) {
		u64 mactx = TX_DESC(txring, i);
		struct sk_buff *skb;

		skb = TX_DESC_INFO(txring, i+1).skb;
		nr_frags = TX_DESC_INFO(txring, i).dma;

		if ((mactx  & XCT_MACTX_E) ||
		    (*chan->status & PAS_STATUS_ERROR))
			pasemi_mac_tx_error(mac, mactx);

		if (unlikely(mactx & XCT_MACTX_O))
			/* Not yet transmitted */
			break;

		buf_count = 2 + nr_frags;
		/* Since we always fill with an even number of entries, make
		 * sure we skip any unused one at the end as well.
		 */
		if (buf_count & 1)
			buf_count++;

		for (j = 0; j <= nr_frags; j++)
			dmas[descr_count][j] = TX_DESC_INFO(txring, i+1+j).dma;

		skbs[descr_count] = skb;
		nf[descr_count] = nr_frags;

		TX_DESC(txring, i) = 0;
		TX_DESC(txring, i+1) = 0;

		descr_count++;
	}
	txring->next_to_clean = i & (TX_RING_SIZE-1);

	spin_unlock_irqrestore(&txring->lock, flags);
	netif_wake_queue(mac->netdev);

	for (i = 0; i < descr_count; i++)
		pasemi_mac_unmap_tx_skb(mac, nf[i], skbs[i], dmas[i]);

	total_count += descr_count;

	/* If the batch was full, try to clean more */
	if (descr_count == batch_limit)
		goto restart;

	return total_count;
}


static irqreturn_t pasemi_mac_rx_intr(int irq, void *data)
{
	const struct pasemi_mac_rxring *rxring = data;
	struct pasemi_mac *mac = rxring->mac;
	struct net_device *dev = mac->netdev;
	const struct pasemi_dmachan *chan = &rxring->chan;
	unsigned int reg;

	if (!(*chan->status & PAS_STATUS_CAUSE_M))
		return IRQ_NONE;

	/* Don't reset packet count so it won't fire again but clear
	 * all others.
	 */

	reg = 0;
	if (*chan->status & PAS_STATUS_SOFT)
		reg |= PAS_IOB_DMA_RXCH_RESET_SINTC;
	if (*chan->status & PAS_STATUS_ERROR)
		reg |= PAS_IOB_DMA_RXCH_RESET_DINTC;

	netif_rx_schedule(dev, &mac->napi);

	write_iob_reg(PAS_IOB_DMA_RXCH_RESET(chan->chno), reg);

	return IRQ_HANDLED;
}

#define TX_CLEAN_INTERVAL HZ

static void pasemi_mac_tx_timer(unsigned long data)
{
	struct pasemi_mac_txring *txring = (struct pasemi_mac_txring *)data;
	struct pasemi_mac *mac = txring->mac;

	pasemi_mac_clean_tx(txring);

	mod_timer(&txring->clean_timer, jiffies + TX_CLEAN_INTERVAL);

	pasemi_mac_restart_tx_intr(mac);
}

static irqreturn_t pasemi_mac_tx_intr(int irq, void *data)
{
	struct pasemi_mac_txring *txring = data;
	const struct pasemi_dmachan *chan = &txring->chan;
	struct pasemi_mac *mac = txring->mac;
	unsigned int reg;

	if (!(*chan->status & PAS_STATUS_CAUSE_M))
		return IRQ_NONE;

	reg = 0;

	if (*chan->status & PAS_STATUS_SOFT)
		reg |= PAS_IOB_DMA_TXCH_RESET_SINTC;
	if (*chan->status & PAS_STATUS_ERROR)
		reg |= PAS_IOB_DMA_TXCH_RESET_DINTC;

	mod_timer(&txring->clean_timer, jiffies + (TX_CLEAN_INTERVAL)*2);

	netif_rx_schedule(mac->netdev, &mac->napi);

	if (reg)
		write_iob_reg(PAS_IOB_DMA_TXCH_RESET(chan->chno), reg);

	return IRQ_HANDLED;
}

static void pasemi_adjust_link(struct net_device *dev)
{
	struct pasemi_mac *mac = netdev_priv(dev);
	int msg;
	unsigned int flags;
	unsigned int new_flags;

	if (!mac->phydev->link) {
		/* If no link, MAC speed settings don't matter. Just report
		 * link down and return.
		 */
		if (mac->link && netif_msg_link(mac))
			printk(KERN_INFO "%s: Link is down.\n", dev->name);

		netif_carrier_off(dev);
		pasemi_mac_intf_disable(mac);
		mac->link = 0;

		return;
	} else {
		pasemi_mac_intf_enable(mac);
		netif_carrier_on(dev);
	}

	flags = read_mac_reg(mac, PAS_MAC_CFG_PCFG);
	new_flags = flags & ~(PAS_MAC_CFG_PCFG_HD | PAS_MAC_CFG_PCFG_SPD_M |
			      PAS_MAC_CFG_PCFG_TSR_M);

	if (!mac->phydev->duplex)
		new_flags |= PAS_MAC_CFG_PCFG_HD;

	switch (mac->phydev->speed) {
	case 1000:
		new_flags |= PAS_MAC_CFG_PCFG_SPD_1G |
			     PAS_MAC_CFG_PCFG_TSR_1G;
		break;
	case 100:
		new_flags |= PAS_MAC_CFG_PCFG_SPD_100M |
			     PAS_MAC_CFG_PCFG_TSR_100M;
		break;
	case 10:
		new_flags |= PAS_MAC_CFG_PCFG_SPD_10M |
			     PAS_MAC_CFG_PCFG_TSR_10M;
		break;
	default:
		printk("Unsupported speed %d\n", mac->phydev->speed);
	}

	/* Print on link or speed/duplex change */
	msg = mac->link != mac->phydev->link || flags != new_flags;

	mac->duplex = mac->phydev->duplex;
	mac->speed = mac->phydev->speed;
	mac->link = mac->phydev->link;

	if (new_flags != flags)
		write_mac_reg(mac, PAS_MAC_CFG_PCFG, new_flags);

	if (msg && netif_msg_link(mac))
		printk(KERN_INFO "%s: Link is up at %d Mbps, %s duplex.\n",
		       dev->name, mac->speed, mac->duplex ? "full" : "half");
}

static int pasemi_mac_phy_init(struct net_device *dev)
{
	struct pasemi_mac *mac = netdev_priv(dev);
	struct device_node *dn, *phy_dn;
	struct phy_device *phydev;
	unsigned int phy_id;
	const phandle *ph;
	const unsigned int *prop;
	struct resource r;
	int ret;

	dn = pci_device_to_OF_node(mac->pdev);
	ph = of_get_property(dn, "phy-handle", NULL);
	if (!ph)
		return -ENODEV;
	phy_dn = of_find_node_by_phandle(*ph);

	prop = of_get_property(phy_dn, "reg", NULL);
	ret = of_address_to_resource(phy_dn->parent, 0, &r);
	if (ret)
		goto err;

	phy_id = *prop;
	snprintf(mac->phy_id, BUS_ID_SIZE, "%x:%02x", (int)r.start, phy_id);

	of_node_put(phy_dn);

	mac->link = 0;
	mac->speed = 0;
	mac->duplex = -1;

	phydev = phy_connect(dev, mac->phy_id, &pasemi_adjust_link, 0, PHY_INTERFACE_MODE_SGMII);

	if (IS_ERR(phydev)) {
		printk(KERN_ERR "%s: Could not attach to phy\n", dev->name);
		return PTR_ERR(phydev);
	}

	mac->phydev = phydev;

	return 0;

err:
	of_node_put(phy_dn);
	return -ENODEV;
}


static int pasemi_mac_open(struct net_device *dev)
{
	struct pasemi_mac *mac = netdev_priv(dev);
	unsigned int flags;
	int ret;

	/* enable rx section */
	write_dma_reg(PAS_DMA_COM_RXCMD, PAS_DMA_COM_RXCMD_EN);

	/* enable tx section */
	write_dma_reg(PAS_DMA_COM_TXCMD, PAS_DMA_COM_TXCMD_EN);

	flags = PAS_MAC_CFG_TXP_FCE | PAS_MAC_CFG_TXP_FPC(3) |
		PAS_MAC_CFG_TXP_SL(3) | PAS_MAC_CFG_TXP_COB(0xf) |
		PAS_MAC_CFG_TXP_TIFT(8) | PAS_MAC_CFG_TXP_TIFG(12);

	write_mac_reg(mac, PAS_MAC_CFG_TXP, flags);

	ret = pasemi_mac_setup_rx_resources(dev);
	if (ret)
		goto out_rx_resources;

	mac->tx = pasemi_mac_setup_tx_resources(dev);

	if (!mac->tx)
		goto out_tx_ring;

	/* 0x3ff with 33MHz clock is about 31us */
	write_iob_reg(PAS_IOB_DMA_COM_TIMEOUTCFG,
		      PAS_IOB_DMA_COM_TIMEOUTCFG_TCNT(0x3ff));

	write_iob_reg(PAS_IOB_DMA_RXCH_CFG(mac->rx->chan.chno),
		      PAS_IOB_DMA_RXCH_CFG_CNTTH(256));

	write_iob_reg(PAS_IOB_DMA_TXCH_CFG(mac->tx->chan.chno),
		      PAS_IOB_DMA_TXCH_CFG_CNTTH(32));

	write_mac_reg(mac, PAS_MAC_IPC_CHNL,
		      PAS_MAC_IPC_CHNL_DCHNO(mac->rx->chan.chno) |
		      PAS_MAC_IPC_CHNL_BCH(mac->rx->chan.chno));

	/* enable rx if */
	write_dma_reg(PAS_DMA_RXINT_RCMDSTA(mac->dma_if),
		      PAS_DMA_RXINT_RCMDSTA_EN |
		      PAS_DMA_RXINT_RCMDSTA_DROPS_M |
		      PAS_DMA_RXINT_RCMDSTA_BP |
		      PAS_DMA_RXINT_RCMDSTA_OO |
		      PAS_DMA_RXINT_RCMDSTA_BT);

	/* enable rx channel */
	pasemi_dma_start_chan(&rx_ring(mac)->chan, PAS_DMA_RXCHAN_CCMDSTA_DU |
						   PAS_DMA_RXCHAN_CCMDSTA_OD |
						   PAS_DMA_RXCHAN_CCMDSTA_FD |
						   PAS_DMA_RXCHAN_CCMDSTA_DT);

	/* enable tx channel */
	pasemi_dma_start_chan(&tx_ring(mac)->chan, PAS_DMA_TXCHAN_TCMDSTA_SZ |
						   PAS_DMA_TXCHAN_TCMDSTA_DB |
						   PAS_DMA_TXCHAN_TCMDSTA_DE |
						   PAS_DMA_TXCHAN_TCMDSTA_DA);

	pasemi_mac_replenish_rx_ring(dev, RX_RING_SIZE);

	write_dma_reg(PAS_DMA_RXCHAN_INCR(rx_ring(mac)->chan.chno),
		      RX_RING_SIZE>>1);

	/* Clear out any residual packet count state from firmware */
	pasemi_mac_restart_rx_intr(mac);
	pasemi_mac_restart_tx_intr(mac);

	flags = PAS_MAC_CFG_PCFG_S1 | PAS_MAC_CFG_PCFG_PR | PAS_MAC_CFG_PCFG_CE;

	if (mac->type == MAC_TYPE_GMAC)
		flags |= PAS_MAC_CFG_PCFG_TSR_1G | PAS_MAC_CFG_PCFG_SPD_1G;
	else
		flags |= PAS_MAC_CFG_PCFG_TSR_10G | PAS_MAC_CFG_PCFG_SPD_10G;

	/* Enable interface in MAC */
	write_mac_reg(mac, PAS_MAC_CFG_PCFG, flags);

	ret = pasemi_mac_phy_init(dev);
	if (ret) {
		/* Since we won't get link notification, just enable RX */
		pasemi_mac_intf_enable(mac);
		if (mac->type == MAC_TYPE_GMAC) {
			/* Warn for missing PHY on SGMII (1Gig) ports */
			dev_warn(&mac->pdev->dev,
				 "PHY init failed: %d.\n", ret);
			dev_warn(&mac->pdev->dev,
				 "Defaulting to 1Gbit full duplex\n");
		}
	}

	netif_start_queue(dev);
	napi_enable(&mac->napi);

	snprintf(mac->tx_irq_name, sizeof(mac->tx_irq_name), "%s tx",
		 dev->name);

	ret = request_irq(mac->tx->chan.irq, &pasemi_mac_tx_intr, IRQF_DISABLED,
			  mac->tx_irq_name, mac->tx);
	if (ret) {
		dev_err(&mac->pdev->dev, "request_irq of irq %d failed: %d\n",
			mac->tx->chan.irq, ret);
		goto out_tx_int;
	}

	snprintf(mac->rx_irq_name, sizeof(mac->rx_irq_name), "%s rx",
		 dev->name);

	ret = request_irq(mac->rx->chan.irq, &pasemi_mac_rx_intr, IRQF_DISABLED,
			  mac->rx_irq_name, mac->rx);
	if (ret) {
		dev_err(&mac->pdev->dev, "request_irq of irq %d failed: %d\n",
			mac->rx->chan.irq, ret);
		goto out_rx_int;
	}

	if (mac->phydev)
		phy_start(mac->phydev);

	init_timer(&mac->tx->clean_timer);
	mac->tx->clean_timer.function = pasemi_mac_tx_timer;
	mac->tx->clean_timer.data = (unsigned long)mac->tx;
	mac->tx->clean_timer.expires = jiffies+HZ;
	add_timer(&mac->tx->clean_timer);

	return 0;

out_rx_int:
	free_irq(mac->tx->chan.irq, mac->tx);
out_tx_int:
	napi_disable(&mac->napi);
	netif_stop_queue(dev);
out_tx_ring:
	if (mac->tx)
		pasemi_mac_free_tx_resources(mac);
	pasemi_mac_free_rx_resources(mac);
out_rx_resources:

	return ret;
}

#define MAX_RETRIES 5000

static void pasemi_mac_pause_txchan(struct pasemi_mac *mac)
{
	unsigned int sta, retries;
	int txch = tx_ring(mac)->chan.chno;

	write_dma_reg(PAS_DMA_TXCHAN_TCMDSTA(txch),
		      PAS_DMA_TXCHAN_TCMDSTA_ST);

	for (retries = 0; retries < MAX_RETRIES; retries++) {
		sta = read_dma_reg(PAS_DMA_TXCHAN_TCMDSTA(txch));
		if (!(sta & PAS_DMA_TXCHAN_TCMDSTA_ACT))
			break;
		cond_resched();
	}

	if (sta & PAS_DMA_TXCHAN_TCMDSTA_ACT)
		dev_err(&mac->dma_pdev->dev,
			"Failed to stop tx channel, tcmdsta %08x\n", sta);

	write_dma_reg(PAS_DMA_TXCHAN_TCMDSTA(txch), 0);
}

static void pasemi_mac_pause_rxchan(struct pasemi_mac *mac)
{
	unsigned int sta, retries;
	int rxch = rx_ring(mac)->chan.chno;

	write_dma_reg(PAS_DMA_RXCHAN_CCMDSTA(rxch),
		      PAS_DMA_RXCHAN_CCMDSTA_ST);
	for (retries = 0; retries < MAX_RETRIES; retries++) {
		sta = read_dma_reg(PAS_DMA_RXCHAN_CCMDSTA(rxch));
		if (!(sta & PAS_DMA_RXCHAN_CCMDSTA_ACT))
			break;
		cond_resched();
	}

	if (sta & PAS_DMA_RXCHAN_CCMDSTA_ACT)
		dev_err(&mac->dma_pdev->dev,
			"Failed to stop rx channel, ccmdsta 08%x\n", sta);
	write_dma_reg(PAS_DMA_RXCHAN_CCMDSTA(rxch), 0);
}

static void pasemi_mac_pause_rxint(struct pasemi_mac *mac)
{
	unsigned int sta, retries;

	write_dma_reg(PAS_DMA_RXINT_RCMDSTA(mac->dma_if),
		      PAS_DMA_RXINT_RCMDSTA_ST);
	for (retries = 0; retries < MAX_RETRIES; retries++) {
		sta = read_dma_reg(PAS_DMA_RXINT_RCMDSTA(mac->dma_if));
		if (!(sta & PAS_DMA_RXINT_RCMDSTA_ACT))
			break;
		cond_resched();
	}

	if (sta & PAS_DMA_RXINT_RCMDSTA_ACT)
		dev_err(&mac->dma_pdev->dev,
			"Failed to stop rx interface, rcmdsta %08x\n", sta);
	write_dma_reg(PAS_DMA_RXINT_RCMDSTA(mac->dma_if), 0);
}

static int pasemi_mac_close(struct net_device *dev)
{
	struct pasemi_mac *mac = netdev_priv(dev);
	unsigned int sta;
	int rxch, txch;

	rxch = rx_ring(mac)->chan.chno;
	txch = tx_ring(mac)->chan.chno;

	if (mac->phydev) {
		phy_stop(mac->phydev);
		phy_disconnect(mac->phydev);
	}

	del_timer_sync(&mac->tx->clean_timer);

	netif_stop_queue(dev);
	napi_disable(&mac->napi);

	sta = read_dma_reg(PAS_DMA_RXINT_RCMDSTA(mac->dma_if));
	if (sta & (PAS_DMA_RXINT_RCMDSTA_BP |
		      PAS_DMA_RXINT_RCMDSTA_OO |
		      PAS_DMA_RXINT_RCMDSTA_BT))
		printk(KERN_DEBUG "pasemi_mac: rcmdsta error: 0x%08x\n", sta);

	sta = read_dma_reg(PAS_DMA_RXCHAN_CCMDSTA(rxch));
	if (sta & (PAS_DMA_RXCHAN_CCMDSTA_DU |
		     PAS_DMA_RXCHAN_CCMDSTA_OD |
		     PAS_DMA_RXCHAN_CCMDSTA_FD |
		     PAS_DMA_RXCHAN_CCMDSTA_DT))
		printk(KERN_DEBUG "pasemi_mac: ccmdsta error: 0x%08x\n", sta);

	sta = read_dma_reg(PAS_DMA_TXCHAN_TCMDSTA(txch));
	if (sta & (PAS_DMA_TXCHAN_TCMDSTA_SZ | PAS_DMA_TXCHAN_TCMDSTA_DB |
		      PAS_DMA_TXCHAN_TCMDSTA_DE | PAS_DMA_TXCHAN_TCMDSTA_DA))
		printk(KERN_DEBUG "pasemi_mac: tcmdsta error: 0x%08x\n", sta);

	/* Clean out any pending buffers */
	pasemi_mac_clean_tx(tx_ring(mac));
	pasemi_mac_clean_rx(rx_ring(mac), RX_RING_SIZE);

	pasemi_mac_pause_txchan(mac);
	pasemi_mac_pause_rxint(mac);
	pasemi_mac_pause_rxchan(mac);
	pasemi_mac_intf_disable(mac);

	free_irq(mac->tx->chan.irq, mac->tx);
	free_irq(mac->rx->chan.irq, mac->rx);

	/* Free resources */
	pasemi_mac_free_rx_resources(mac);
	pasemi_mac_free_tx_resources(mac);

	return 0;
}

static int pasemi_mac_start_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct pasemi_mac *mac = netdev_priv(dev);
	struct pasemi_mac_txring *txring;
	u64 dflags, mactx;
	dma_addr_t map[MAX_SKB_FRAGS+1];
	unsigned int map_size[MAX_SKB_FRAGS+1];
	unsigned long flags;
	int i, nfrags;
	int fill;

	dflags = XCT_MACTX_O | XCT_MACTX_ST | XCT_MACTX_CRC_PAD;

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		const unsigned char *nh = skb_network_header(skb);

		switch (ip_hdr(skb)->protocol) {
		case IPPROTO_TCP:
			dflags |= XCT_MACTX_CSUM_TCP;
			dflags |= XCT_MACTX_IPH(skb_network_header_len(skb) >> 2);
			dflags |= XCT_MACTX_IPO(nh - skb->data);
			break;
		case IPPROTO_UDP:
			dflags |= XCT_MACTX_CSUM_UDP;
			dflags |= XCT_MACTX_IPH(skb_network_header_len(skb) >> 2);
			dflags |= XCT_MACTX_IPO(nh - skb->data);
			break;
		}
	}

	nfrags = skb_shinfo(skb)->nr_frags;

	map[0] = pci_map_single(mac->dma_pdev, skb->data, skb_headlen(skb),
				PCI_DMA_TODEVICE);
	map_size[0] = skb_headlen(skb);
	if (dma_mapping_error(map[0]))
		goto out_err_nolock;

	for (i = 0; i < nfrags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		map[i+1] = pci_map_page(mac->dma_pdev, frag->page,
					frag->page_offset, frag->size,
					PCI_DMA_TODEVICE);
		map_size[i+1] = frag->size;
		if (dma_mapping_error(map[i+1])) {
			nfrags = i;
			goto out_err_nolock;
		}
	}

	mactx = dflags | XCT_MACTX_LLEN(skb->len);

	txring = tx_ring(mac);

	spin_lock_irqsave(&txring->lock, flags);

	fill = txring->next_to_fill;

	/* Avoid stepping on the same cache line that the DMA controller
	 * is currently about to send, so leave at least 8 words available.
	 * Total free space needed is mactx + fragments + 8
	 */
	if (RING_AVAIL(txring) < nfrags + 10) {
		/* no room -- stop the queue and wait for tx intr */
		netif_stop_queue(dev);
		goto out_err;
	}

	TX_DESC(txring, fill) = mactx;
	TX_DESC_INFO(txring, fill).dma = nfrags;
	fill++;
	TX_DESC_INFO(txring, fill).skb = skb;
	for (i = 0; i <= nfrags; i++) {
		TX_DESC(txring, fill+i) =
			XCT_PTR_LEN(map_size[i]) | XCT_PTR_ADDR(map[i]);
		TX_DESC_INFO(txring, fill+i).dma = map[i];
	}

	/* We have to add an even number of 8-byte entries to the ring
	 * even if the last one is unused. That means always an odd number
	 * of pointers + one mactx descriptor.
	 */
	if (nfrags & 1)
		nfrags++;

	txring->next_to_fill = (fill + nfrags + 1) & (TX_RING_SIZE-1);

	dev->stats.tx_packets++;
	dev->stats.tx_bytes += skb->len;

	spin_unlock_irqrestore(&txring->lock, flags);

	write_dma_reg(PAS_DMA_TXCHAN_INCR(txring->chan.chno), (nfrags+2) >> 1);

	return NETDEV_TX_OK;

out_err:
	spin_unlock_irqrestore(&txring->lock, flags);
out_err_nolock:
	while (nfrags--)
		pci_unmap_single(mac->dma_pdev, map[nfrags], map_size[nfrags],
				 PCI_DMA_TODEVICE);

	return NETDEV_TX_BUSY;
}

static void pasemi_mac_set_rx_mode(struct net_device *dev)
{
	const struct pasemi_mac *mac = netdev_priv(dev);
	unsigned int flags;

	flags = read_mac_reg(mac, PAS_MAC_CFG_PCFG);

	/* Set promiscuous */
	if (dev->flags & IFF_PROMISC)
		flags |= PAS_MAC_CFG_PCFG_PR;
	else
		flags &= ~PAS_MAC_CFG_PCFG_PR;

	write_mac_reg(mac, PAS_MAC_CFG_PCFG, flags);
}


static int pasemi_mac_poll(struct napi_struct *napi, int budget)
{
	struct pasemi_mac *mac = container_of(napi, struct pasemi_mac, napi);
	struct net_device *dev = mac->netdev;
	int pkts;

	pasemi_mac_clean_tx(tx_ring(mac));
	pkts = pasemi_mac_clean_rx(rx_ring(mac), budget);
	if (pkts < budget) {
		/* all done, no more packets present */
		netif_rx_complete(dev, napi);

		pasemi_mac_restart_rx_intr(mac);
		pasemi_mac_restart_tx_intr(mac);
	}
	return pkts;
}

static int pasemi_mac_change_mtu(struct net_device *dev, int new_mtu)
{
	struct pasemi_mac *mac = netdev_priv(dev);
	unsigned int reg;
	unsigned int rcmdsta;
	int running;

	if (new_mtu < PE_MIN_MTU || new_mtu > PE_MAX_MTU)
		return -EINVAL;

	running = netif_running(dev);

	if (running) {
		/* Need to stop the interface, clean out all already
		 * received buffers, free all unused buffers on the RX
		 * interface ring, then finally re-fill the rx ring with
		 * the new-size buffers and restart.
		 */

		napi_disable(&mac->napi);
		netif_tx_disable(dev);
		pasemi_mac_intf_disable(mac);

		rcmdsta = read_dma_reg(PAS_DMA_RXINT_RCMDSTA(mac->dma_if));
		pasemi_mac_pause_rxint(mac);
		pasemi_mac_clean_rx(rx_ring(mac), RX_RING_SIZE);
		pasemi_mac_free_rx_buffers(mac);
	}

	/* Change maxf, i.e. what size frames are accepted.
	 * Need room for ethernet header and CRC word
	 */
	reg = read_mac_reg(mac, PAS_MAC_CFG_MACCFG);
	reg &= ~PAS_MAC_CFG_MACCFG_MAXF_M;
	reg |= PAS_MAC_CFG_MACCFG_MAXF(new_mtu + ETH_HLEN + 4);
	write_mac_reg(mac, PAS_MAC_CFG_MACCFG, reg);

	dev->mtu = new_mtu;
	/* MTU + ETH_HLEN + VLAN_HLEN + 2 64B cachelines */
	mac->bufsz = new_mtu + ETH_HLEN + ETH_FCS_LEN + LOCAL_SKB_ALIGN + 128;

	if (running) {
		write_dma_reg(PAS_DMA_RXINT_RCMDSTA(mac->dma_if),
			      rcmdsta | PAS_DMA_RXINT_RCMDSTA_EN);

		rx_ring(mac)->next_to_fill = 0;
		pasemi_mac_replenish_rx_ring(dev, RX_RING_SIZE-1);

		napi_enable(&mac->napi);
		netif_start_queue(dev);
		pasemi_mac_intf_enable(mac);
	}

	return 0;
}

static int __devinit
pasemi_mac_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct net_device *dev;
	struct pasemi_mac *mac;
	int err;
	DECLARE_MAC_BUF(mac_buf);

	err = pci_enable_device(pdev);
	if (err)
		return err;

	dev = alloc_etherdev(sizeof(struct pasemi_mac));
	if (dev == NULL) {
		dev_err(&pdev->dev,
			"pasemi_mac: Could not allocate ethernet device.\n");
		err = -ENOMEM;
		goto out_disable_device;
	}

	pci_set_drvdata(pdev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	mac = netdev_priv(dev);

	mac->pdev = pdev;
	mac->netdev = dev;

	netif_napi_add(dev, &mac->napi, pasemi_mac_poll, 64);

	dev->features = NETIF_F_IP_CSUM | NETIF_F_LLTX | NETIF_F_SG |
			NETIF_F_HIGHDMA;

	mac->lro_mgr.max_aggr = LRO_MAX_AGGR;
	mac->lro_mgr.max_desc = MAX_LRO_DESCRIPTORS;
	mac->lro_mgr.lro_arr = mac->lro_desc;
	mac->lro_mgr.get_skb_header = get_skb_hdr;
	mac->lro_mgr.features = LRO_F_NAPI | LRO_F_EXTRACT_VLAN_ID;
	mac->lro_mgr.dev = mac->netdev;
	mac->lro_mgr.ip_summed = CHECKSUM_UNNECESSARY;
	mac->lro_mgr.ip_summed_aggr = CHECKSUM_UNNECESSARY;


	mac->dma_pdev = pci_get_device(PCI_VENDOR_ID_PASEMI, 0xa007, NULL);
	if (!mac->dma_pdev) {
		dev_err(&mac->pdev->dev, "Can't find DMA Controller\n");
		err = -ENODEV;
		goto out;
	}

	mac->iob_pdev = pci_get_device(PCI_VENDOR_ID_PASEMI, 0xa001, NULL);
	if (!mac->iob_pdev) {
		dev_err(&mac->pdev->dev, "Can't find I/O Bridge\n");
		err = -ENODEV;
		goto out;
	}

	/* get mac addr from device tree */
	if (pasemi_get_mac_addr(mac) || !is_valid_ether_addr(mac->mac_addr)) {
		err = -ENODEV;
		goto out;
	}
	memcpy(dev->dev_addr, mac->mac_addr, sizeof(mac->mac_addr));

	mac->dma_if = mac_to_intf(mac);
	if (mac->dma_if < 0) {
		dev_err(&mac->pdev->dev, "Can't map DMA interface\n");
		err = -ENODEV;
		goto out;
	}

	switch (pdev->device) {
	case 0xa005:
		mac->type = MAC_TYPE_GMAC;
		break;
	case 0xa006:
		mac->type = MAC_TYPE_XAUI;
		break;
	default:
		err = -ENODEV;
		goto out;
	}

	dev->open = pasemi_mac_open;
	dev->stop = pasemi_mac_close;
	dev->hard_start_xmit = pasemi_mac_start_tx;
	dev->set_multicast_list = pasemi_mac_set_rx_mode;
	dev->set_mac_address = pasemi_mac_set_mac_addr;
	dev->mtu = PE_DEF_MTU;
	/* 1500 MTU + ETH_HLEN + VLAN_HLEN + 2 64B cachelines */
	mac->bufsz = dev->mtu + ETH_HLEN + ETH_FCS_LEN + LOCAL_SKB_ALIGN + 128;

	dev->change_mtu = pasemi_mac_change_mtu;

	if (err)
		goto out;

	mac->msg_enable = netif_msg_init(debug, DEFAULT_MSG_ENABLE);

	/* Enable most messages by default */
	mac->msg_enable = (NETIF_MSG_IFUP << 1 ) - 1;

	err = register_netdev(dev);

	if (err) {
		dev_err(&mac->pdev->dev, "register_netdev failed with error %d\n",
			err);
		goto out;
	} else if netif_msg_probe(mac)
		printk(KERN_INFO "%s: PA Semi %s: intf %d, hw addr %s\n",
		       dev->name, mac->type == MAC_TYPE_GMAC ? "GMAC" : "XAUI",
		       mac->dma_if, print_mac(mac_buf, dev->dev_addr));

	return err;

out:
	if (mac->iob_pdev)
		pci_dev_put(mac->iob_pdev);
	if (mac->dma_pdev)
		pci_dev_put(mac->dma_pdev);

	free_netdev(dev);
out_disable_device:
	pci_disable_device(pdev);
	return err;

}

static void __devexit pasemi_mac_remove(struct pci_dev *pdev)
{
	struct net_device *netdev = pci_get_drvdata(pdev);
	struct pasemi_mac *mac;

	if (!netdev)
		return;

	mac = netdev_priv(netdev);

	unregister_netdev(netdev);

	pci_disable_device(pdev);
	pci_dev_put(mac->dma_pdev);
	pci_dev_put(mac->iob_pdev);

	pasemi_dma_free_chan(&mac->tx->chan);
	pasemi_dma_free_chan(&mac->rx->chan);

	pci_set_drvdata(pdev, NULL);
	free_netdev(netdev);
}

static struct pci_device_id pasemi_mac_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_PASEMI, 0xa005) },
	{ PCI_DEVICE(PCI_VENDOR_ID_PASEMI, 0xa006) },
	{ },
};

MODULE_DEVICE_TABLE(pci, pasemi_mac_pci_tbl);

static struct pci_driver pasemi_mac_driver = {
	.name		= "pasemi_mac",
	.id_table	= pasemi_mac_pci_tbl,
	.probe		= pasemi_mac_probe,
	.remove		= __devexit_p(pasemi_mac_remove),
};

static void __exit pasemi_mac_cleanup_module(void)
{
	pci_unregister_driver(&pasemi_mac_driver);
}

int pasemi_mac_init_module(void)
{
	int err;

	err = pasemi_dma_init();
	if (err)
		return err;

	return pci_register_driver(&pasemi_mac_driver);
}

module_init(pasemi_mac_init_module);
module_exit(pasemi_mac_cleanup_module);
