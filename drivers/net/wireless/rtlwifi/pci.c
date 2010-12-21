/******************************************************************************
 *
 * Copyright(c) 2009-2010  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#include "core.h"
#include "wifi.h"
#include "pci.h"
#include "base.h"
#include "ps.h"

static const u16 pcibridge_vendors[PCI_BRIDGE_VENDOR_MAX] = {
	INTEL_VENDOR_ID,
	ATI_VENDOR_ID,
	AMD_VENDOR_ID,
	SIS_VENDOR_ID
};

/* Update PCI dependent default settings*/
static void _rtl_pci_update_default_setting(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u8 pcibridge_vendor = pcipriv->ndis_adapter.pcibridge_vendor;

	ppsc->reg_rfps_level = 0;
	ppsc->b_support_aspm = 0;

	/*Update PCI ASPM setting */
	ppsc->const_amdpci_aspm = rtlpci->const_amdpci_aspm;
	switch (rtlpci->const_pci_aspm) {
	case 0:
		/*No ASPM */
		break;

	case 1:
		/*ASPM dynamically enabled/disable. */
		ppsc->reg_rfps_level |= RT_RF_LPS_LEVEL_ASPM;
		break;

	case 2:
		/*ASPM with Clock Req dynamically enabled/disable. */
		ppsc->reg_rfps_level |= (RT_RF_LPS_LEVEL_ASPM |
					 RT_RF_OFF_LEVL_CLK_REQ);
		break;

	case 3:
		/*
		 * Always enable ASPM and Clock Req
		 * from initialization to halt.
		 * */
		ppsc->reg_rfps_level &= ~(RT_RF_LPS_LEVEL_ASPM);
		ppsc->reg_rfps_level |= (RT_RF_PS_LEVEL_ALWAYS_ASPM |
					 RT_RF_OFF_LEVL_CLK_REQ);
		break;

	case 4:
		/*
		 * Always enable ASPM without Clock Req
		 * from initialization to halt.
		 * */
		ppsc->reg_rfps_level &= ~(RT_RF_LPS_LEVEL_ASPM |
					  RT_RF_OFF_LEVL_CLK_REQ);
		ppsc->reg_rfps_level |= RT_RF_PS_LEVEL_ALWAYS_ASPM;
		break;
	}

	ppsc->reg_rfps_level |= RT_RF_OFF_LEVL_HALT_NIC;

	/*Update Radio OFF setting */
	switch (rtlpci->const_hwsw_rfoff_d3) {
	case 1:
		if (ppsc->reg_rfps_level & RT_RF_LPS_LEVEL_ASPM)
			ppsc->reg_rfps_level |= RT_RF_OFF_LEVL_ASPM;
		break;

	case 2:
		if (ppsc->reg_rfps_level & RT_RF_LPS_LEVEL_ASPM)
			ppsc->reg_rfps_level |= RT_RF_OFF_LEVL_ASPM;
		ppsc->reg_rfps_level |= RT_RF_OFF_LEVL_HALT_NIC;
		break;

	case 3:
		ppsc->reg_rfps_level |= RT_RF_OFF_LEVL_PCI_D3;
		break;
	}

	/*Set HW definition to determine if it supports ASPM. */
	switch (rtlpci->const_support_pciaspm) {
	case 0:{
			/*Not support ASPM. */
			bool b_support_aspm = false;
			ppsc->b_support_aspm = b_support_aspm;
			break;
		}
	case 1:{
			/*Support ASPM. */
			bool b_support_aspm = true;
			bool b_support_backdoor = true;
			ppsc->b_support_aspm = b_support_aspm;

			/*if(priv->oem_id == RT_CID_TOSHIBA &&
			   !priv->ndis_adapter.amd_l1_patch)
			   b_support_backdoor = false; */

			ppsc->b_support_backdoor = b_support_backdoor;

			break;
		}
	case 2:
		/*ASPM value set by chipset. */
		if (pcibridge_vendor == PCI_BRIDGE_VENDOR_INTEL) {
			bool b_support_aspm = true;
			ppsc->b_support_aspm = b_support_aspm;
		}
		break;
	default:
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 ("switch case not process\n"));
		break;
	}
}

static bool _rtl_pci_platform_switch_device_pci_aspm(
			struct ieee80211_hw *hw,
			u8 value)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	bool bresult = false;

	value |= 0x40;

	pci_write_config_byte(rtlpci->pdev, 0x80, value);

	return bresult;
}

/*When we set 0x01 to enable clk request. Set 0x0 to disable clk req.*/
static bool _rtl_pci_switch_clk_req(struct ieee80211_hw *hw, u8 value)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u8 buffer;
	bool bresult = false;

	buffer = value;

	pci_write_config_byte(rtlpci->pdev, 0x81, value);
	bresult = true;

	return bresult;
}

/*Disable RTL8192SE ASPM & Disable Pci Bridge ASPM*/
static void rtl_pci_disable_aspm(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u8 pcibridge_vendor = pcipriv->ndis_adapter.pcibridge_vendor;
	u32 pcicfg_addrport = pcipriv->ndis_adapter.pcicfg_addrport;
	u8 num4bytes = pcipriv->ndis_adapter.num4bytes;
	/*Retrieve original configuration settings. */
	u8 linkctrl_reg = pcipriv->ndis_adapter.linkctrl_reg;
	u16 pcibridge_linkctrlreg = pcipriv->ndis_adapter.
				pcibridge_linkctrlreg;
	u16 aspmlevel = 0;

	if (pcibridge_vendor == PCI_BRIDGE_VENDOR_UNKNOWN) {
		RT_TRACE(rtlpriv, COMP_POWER, DBG_TRACE,
			 ("PCI(Bridge) UNKNOWN.\n"));

		return;
	}

	if (ppsc->reg_rfps_level & RT_RF_OFF_LEVL_CLK_REQ) {
		RT_CLEAR_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_CLK_REQ);
		_rtl_pci_switch_clk_req(hw, 0x0);
	}

	if (1) {
		/*for promising device will in L0 state after an I/O. */
		u8 tmp_u1b;
		pci_read_config_byte(rtlpci->pdev, 0x80, &tmp_u1b);
	}

	/*Set corresponding value. */
	aspmlevel |= BIT(0) | BIT(1);
	linkctrl_reg &= ~aspmlevel;
	pcibridge_linkctrlreg &= ~(BIT(0) | BIT(1));

	_rtl_pci_platform_switch_device_pci_aspm(hw, linkctrl_reg);
	udelay(50);

	/*4 Disable Pci Bridge ASPM */
	rtl_pci_raw_write_port_ulong(PCI_CONF_ADDRESS,
				     pcicfg_addrport + (num4bytes << 2));
	rtl_pci_raw_write_port_uchar(PCI_CONF_DATA, pcibridge_linkctrlreg);

	udelay(50);

}

/*
 *Enable RTL8192SE ASPM & Enable Pci Bridge ASPM for
 *power saving We should follow the sequence to enable
 *RTL8192SE first then enable Pci Bridge ASPM
 *or the system will show bluescreen.
 */
static void rtl_pci_enable_aspm(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u8 pcibridge_busnum = pcipriv->ndis_adapter.pcibridge_busnum;
	u8 pcibridge_devnum = pcipriv->ndis_adapter.pcibridge_devnum;
	u8 pcibridge_funcnum = pcipriv->ndis_adapter.pcibridge_funcnum;
	u8 pcibridge_vendor = pcipriv->ndis_adapter.pcibridge_vendor;
	u32 pcicfg_addrport = pcipriv->ndis_adapter.pcicfg_addrport;
	u8 num4bytes = pcipriv->ndis_adapter.num4bytes;
	u16 aspmlevel;
	u8 u_pcibridge_aspmsetting;
	u8 u_device_aspmsetting;

	if (pcibridge_vendor == PCI_BRIDGE_VENDOR_UNKNOWN) {
		RT_TRACE(rtlpriv, COMP_POWER, DBG_TRACE,
			 ("PCI(Bridge) UNKNOWN.\n"));
		return;
	}

	/*4 Enable Pci Bridge ASPM */
	rtl_pci_raw_write_port_ulong(PCI_CONF_ADDRESS,
				     pcicfg_addrport + (num4bytes << 2));

	u_pcibridge_aspmsetting =
	    pcipriv->ndis_adapter.pcibridge_linkctrlreg |
	    rtlpci->const_hostpci_aspm_setting;

	if (pcibridge_vendor == PCI_BRIDGE_VENDOR_INTEL)
		u_pcibridge_aspmsetting &= ~BIT(0);

	rtl_pci_raw_write_port_uchar(PCI_CONF_DATA, u_pcibridge_aspmsetting);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 ("PlatformEnableASPM():PciBridge busnumber[%x], "
		  "DevNumbe[%x], funcnumber[%x], Write reg[%x] = %x\n",
		  pcibridge_busnum, pcibridge_devnum, pcibridge_funcnum,
		  (pcipriv->ndis_adapter.pcibridge_pciehdr_offset + 0x10),
		  u_pcibridge_aspmsetting));

	udelay(50);

	/*Get ASPM level (with/without Clock Req) */
	aspmlevel = rtlpci->const_devicepci_aspm_setting;
	u_device_aspmsetting = pcipriv->ndis_adapter.linkctrl_reg;

	/*_rtl_pci_platform_switch_device_pci_aspm(dev,*/
	/*(priv->ndis_adapter.linkctrl_reg | ASPMLevel)); */

	u_device_aspmsetting |= aspmlevel;

	_rtl_pci_platform_switch_device_pci_aspm(hw, u_device_aspmsetting);

	if (ppsc->reg_rfps_level & RT_RF_OFF_LEVL_CLK_REQ) {
		_rtl_pci_switch_clk_req(hw, (ppsc->reg_rfps_level &
					     RT_RF_OFF_LEVL_CLK_REQ) ? 1 : 0);
		RT_SET_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_CLK_REQ);
	}
	udelay(200);
}

static bool rtl_pci_get_amd_l1_patch(struct ieee80211_hw *hw)
{
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	u32 pcicfg_addrport = pcipriv->ndis_adapter.pcicfg_addrport;

	bool status = false;
	u8 offset_e0;
	unsigned offset_e4;

	rtl_pci_raw_write_port_ulong(PCI_CONF_ADDRESS,
			pcicfg_addrport + 0xE0);
	rtl_pci_raw_write_port_uchar(PCI_CONF_DATA, 0xA0);

	rtl_pci_raw_write_port_ulong(PCI_CONF_ADDRESS,
			pcicfg_addrport + 0xE0);
	rtl_pci_raw_read_port_uchar(PCI_CONF_DATA, &offset_e0);

	if (offset_e0 == 0xA0) {
		rtl_pci_raw_write_port_ulong(PCI_CONF_ADDRESS,
					     pcicfg_addrport + 0xE4);
		rtl_pci_raw_read_port_ulong(PCI_CONF_DATA, &offset_e4);
		if (offset_e4 & BIT(23))
			status = true;
	}

	return status;
}

static void rtl_pci_get_linkcontrol_field(struct ieee80211_hw *hw)
{
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	u8 capabilityoffset = pcipriv->ndis_adapter.pcibridge_pciehdr_offset;
	u32 pcicfg_addrport = pcipriv->ndis_adapter.pcicfg_addrport;
	u8 linkctrl_reg;
	u8 num4bBytes;

	num4bBytes = (capabilityoffset + 0x10) / 4;

	/*Read  Link Control Register */
	rtl_pci_raw_write_port_ulong(PCI_CONF_ADDRESS,
				     pcicfg_addrport + (num4bBytes << 2));
	rtl_pci_raw_read_port_uchar(PCI_CONF_DATA, &linkctrl_reg);

	pcipriv->ndis_adapter.pcibridge_linkctrlreg = linkctrl_reg;
}

static void rtl_pci_parse_configuration(struct pci_dev *pdev,
		struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);

	u8 tmp;
	int pos;
	u8 linkctrl_reg;

	/*Link Control Register */
	pos = pci_find_capability(pdev, PCI_CAP_ID_EXP);
	pci_read_config_byte(pdev, pos + PCI_EXP_LNKCTL, &linkctrl_reg);
	pcipriv->ndis_adapter.linkctrl_reg = linkctrl_reg;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_TRACE,
		 ("Link Control Register =%x\n",
		  pcipriv->ndis_adapter.linkctrl_reg));

	pci_read_config_byte(pdev, 0x98, &tmp);
	tmp |= BIT(4);
	pci_write_config_byte(pdev, 0x98, tmp);

	tmp = 0x17;
	pci_write_config_byte(pdev, 0x70f, tmp);
}

static void _rtl_pci_initialize_adapter_common(struct ieee80211_hw *hw)
{
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));

	_rtl_pci_update_default_setting(hw);

	if (ppsc->reg_rfps_level & RT_RF_PS_LEVEL_ALWAYS_ASPM) {
		/*Always enable ASPM & Clock Req. */
		rtl_pci_enable_aspm(hw);
		RT_SET_PS_LEVEL(ppsc, RT_RF_PS_LEVEL_ALWAYS_ASPM);
	}

}

static void rtl_pci_init_aspm(struct ieee80211_hw *hw)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	/*close ASPM for AMD defaultly */
	rtlpci->const_amdpci_aspm = 0;

	/*
	 * ASPM PS mode.
	 * 0 - Disable ASPM,
	 * 1 - Enable ASPM without Clock Req,
	 * 2 - Enable ASPM with Clock Req,
	 * 3 - Alwyas Enable ASPM with Clock Req,
	 * 4 - Always Enable ASPM without Clock Req.
	 * set defult to RTL8192CE:3 RTL8192E:2
	 * */
	rtlpci->const_pci_aspm = 3;

	/*Setting for PCI-E device */
	rtlpci->const_devicepci_aspm_setting = 0x03;

	/*Setting for PCI-E bridge */
	rtlpci->const_hostpci_aspm_setting = 0x02;

	/*
	 * In Hw/Sw Radio Off situation.
	 * 0 - Default,
	 * 1 - From ASPM setting without low Mac Pwr,
	 * 2 - From ASPM setting with low Mac Pwr,
	 * 3 - Bus D3
	 * set default to RTL8192CE:0 RTL8192SE:2
	 */
	rtlpci->const_hwsw_rfoff_d3 = 0;

	/*
	 * This setting works for those device with
	 * backdoor ASPM setting such as EPHY setting.
	 * 0 - Not support ASPM,
	 * 1 - Support ASPM,
	 * 2 - According to chipset.
	 */
	rtlpci->const_support_pciaspm = 1;

	_rtl_pci_initialize_adapter_common(hw);
}

static void _rtl_pci_io_handler_init(struct device *dev,
				     struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);

	rtlpriv->io.dev = dev;

	rtlpriv->io.write8_async = pci_write8_async;
	rtlpriv->io.write16_async = pci_write16_async;
	rtlpriv->io.write32_async = pci_write32_async;

	rtlpriv->io.read8_sync = pci_read8_sync;
	rtlpriv->io.read16_sync = pci_read16_sync;
	rtlpriv->io.read32_sync = pci_read32_sync;

}

static void _rtl_pci_io_handler_release(struct ieee80211_hw *hw)
{
}

static void _rtl_pci_tx_isr(struct ieee80211_hw *hw, int prio)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	struct rtl8192_tx_ring *ring = &rtlpci->tx_ring[prio];

	while (skb_queue_len(&ring->queue)) {
		struct rtl_tx_desc *entry = &ring->desc[ring->idx];
		struct sk_buff *skb;
		struct ieee80211_tx_info *info;

		u8 own = (u8) rtlpriv->cfg->ops->get_desc((u8 *) entry, true,
							  HW_DESC_OWN);

		/*
		 *beacon packet will only use the first
		 *descriptor defautly,and the own may not
		 *be cleared by the hardware
		 */
		if (own)
			return;
		ring->idx = (ring->idx + 1) % ring->entries;

		skb = __skb_dequeue(&ring->queue);
		pci_unmap_single(rtlpci->pdev,
				 le32_to_cpu(rtlpriv->cfg->ops->
					     get_desc((u8 *) entry, true,
						      HW_DESC_TXBUFF_ADDR)),
				 skb->len, PCI_DMA_TODEVICE);

		RT_TRACE(rtlpriv, (COMP_INTR | COMP_SEND), DBG_TRACE,
			 ("new ring->idx:%d, "
			  "free: skb_queue_len:%d, free: seq:%x\n",
			  ring->idx,
			  skb_queue_len(&ring->queue),
			  *(u16 *) (skb->data + 22)));

		info = IEEE80211_SKB_CB(skb);
		ieee80211_tx_info_clear_status(info);

		info->flags |= IEEE80211_TX_STAT_ACK;
		/*info->status.rates[0].count = 1; */

		ieee80211_tx_status_irqsafe(hw, skb);

		if ((ring->entries - skb_queue_len(&ring->queue))
				== 2) {

			RT_TRACE(rtlpriv, COMP_ERR, DBG_LOUD,
					("more desc left, wake"
					 "skb_queue@%d,ring->idx = %d,"
					 "skb_queue_len = 0x%d\n",
					 prio, ring->idx,
					 skb_queue_len(&ring->queue)));

			ieee80211_wake_queue(hw,
					skb_get_queue_mapping
					(skb));
		}

		skb = NULL;
	}

	if (((rtlpriv->link_info.num_rx_inperiod +
		rtlpriv->link_info.num_tx_inperiod) > 8) ||
		(rtlpriv->link_info.num_rx_inperiod > 2)) {
		rtl_lps_leave(hw);
	}
}

static void _rtl_pci_rx_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	int rx_queue_idx = RTL_PCI_RX_MPDU_QUEUE;

	struct ieee80211_rx_status rx_status = { 0 };
	unsigned int count = rtlpci->rxringcount;
	u8 own;
	u8 tmp_one;
	u32 bufferaddress;
	bool unicast = false;

	struct rtl_stats stats = {
		.signal = 0,
		.noise = -98,
		.rate = 0,
	};

	/*RX NORMAL PKT */
	while (count--) {
		/*rx descriptor */
		struct rtl_rx_desc *pdesc = &rtlpci->rx_ring[rx_queue_idx].desc[
				rtlpci->rx_ring[rx_queue_idx].idx];
		/*rx pkt */
		struct sk_buff *skb = rtlpci->rx_ring[rx_queue_idx].rx_buf[
				rtlpci->rx_ring[rx_queue_idx].idx];

		own = (u8) rtlpriv->cfg->ops->get_desc((u8 *) pdesc,
						       false, HW_DESC_OWN);

		if (own) {
			/*wait data to be filled by hardware */
			return;
		} else {
			struct ieee80211_hdr *hdr;
			u16 fc;
			struct sk_buff *new_skb = NULL;

			rtlpriv->cfg->ops->query_rx_desc(hw, &stats,
							 &rx_status,
							 (u8 *) pdesc, skb);

			pci_unmap_single(rtlpci->pdev,
					 *((dma_addr_t *) skb->cb),
					 rtlpci->rxbuffersize,
					 PCI_DMA_FROMDEVICE);

			skb_put(skb, rtlpriv->cfg->ops->get_desc((u8 *) pdesc,
							 false,
							 HW_DESC_RXPKT_LEN));
			skb_reserve(skb,
				    stats.rx_drvinfo_size + stats.rx_bufshift);

			/*
			 *NOTICE This can not be use for mac80211,
			 *this is done in mac80211 code,
			 *if you done here sec DHCP will fail
			 *skb_trim(skb, skb->len - 4);
			 */

			hdr = (struct ieee80211_hdr *)(skb->data);
			fc = le16_to_cpu(hdr->frame_control);

			if (!stats.b_crc) {
				memcpy(IEEE80211_SKB_RXCB(skb), &rx_status,
				       sizeof(rx_status));

				if (is_broadcast_ether_addr(hdr->addr1))
					;/*TODO*/
				else {
					if (is_multicast_ether_addr(hdr->addr1))
						;/*TODO*/
					else {
						unicast = true;
						rtlpriv->stats.rxbytesunicast +=
						    skb->len;
					}
				}

				rtl_is_special_data(hw, skb, false);

				if (ieee80211_is_data(fc)) {
					rtlpriv->cfg->ops->led_control(hw,
							       LED_CTL_RX);

					if (unicast)
						rtlpriv->link_info.
						    num_rx_inperiod++;
				}

				if (unlikely(!rtl_action_proc(hw, skb, false)))
					dev_kfree_skb_any(skb);
				else
					ieee80211_rx_irqsafe(hw, skb);
			} else {
				dev_kfree_skb_any(skb);
			}

			if (((rtlpriv->link_info.num_rx_inperiod +
				rtlpriv->link_info.num_tx_inperiod) > 8) ||
				(rtlpriv->link_info.num_rx_inperiod > 2)) {
				rtl_lps_leave(hw);
			}

			new_skb = dev_alloc_skb(rtlpci->rxbuffersize);
			if (unlikely(!new_skb)) {
				RT_TRACE(rtlpriv, (COMP_INTR | COMP_RECV),
					 DBG_DMESG,
					 ("can't alloc skb for rx\n"));
				goto done;
			}
			skb = new_skb;
			/*skb->dev = dev; */

			rtlpci->rx_ring[rx_queue_idx].rx_buf[rtlpci->
							     rx_ring
							     [rx_queue_idx].
							     idx] = skb;
			*((dma_addr_t *) skb->cb) =
			    pci_map_single(rtlpci->pdev, skb_tail_pointer(skb),
					   rtlpci->rxbuffersize,
					   PCI_DMA_FROMDEVICE);

		}
done:
		bufferaddress = cpu_to_le32(*((dma_addr_t *) skb->cb));
		tmp_one = 1;
		rtlpriv->cfg->ops->set_desc((u8 *) pdesc, false,
					    HW_DESC_RXBUFF_ADDR,
					    (u8 *)&bufferaddress);
		rtlpriv->cfg->ops->set_desc((u8 *)pdesc, false, HW_DESC_RXOWN,
					    (u8 *)&tmp_one);
		rtlpriv->cfg->ops->set_desc((u8 *)pdesc, false,
					    HW_DESC_RXPKT_LEN,
					    (u8 *)&rtlpci->rxbuffersize);

		if (rtlpci->rx_ring[rx_queue_idx].idx ==
		    rtlpci->rxringcount - 1)
			rtlpriv->cfg->ops->set_desc((u8 *)pdesc, false,
						    HW_DESC_RXERO,
						    (u8 *)&tmp_one);

		rtlpci->rx_ring[rx_queue_idx].idx =
		    (rtlpci->rx_ring[rx_queue_idx].idx + 1) %
		    rtlpci->rxringcount;
	}

}

void _rtl_pci_tx_interrupt(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	int prio;

	for (prio = 0; prio < RTL_PCI_MAX_TX_QUEUE_COUNT; prio++) {
		struct rtl8192_tx_ring *ring = &rtlpci->tx_ring[prio];

		while (skb_queue_len(&ring->queue)) {
			struct rtl_tx_desc *entry = &ring->desc[ring->idx];
			struct sk_buff *skb;
			struct ieee80211_tx_info *info;
			u8 own;

			/*
			 *beacon packet will only use the first
			 *descriptor defautly, and the own may not
			 *be cleared by the hardware, and
			 *beacon will free in prepare beacon
			 */
			if (prio == BEACON_QUEUE || prio == TXCMD_QUEUE ||
			    prio == HCCA_QUEUE)
				break;

			own = (u8)rtlpriv->cfg->ops->get_desc((u8 *)entry,
							       true,
							       HW_DESC_OWN);

			if (own)
				break;

			skb = __skb_dequeue(&ring->queue);
			pci_unmap_single(rtlpci->pdev,
					 le32_to_cpu(rtlpriv->cfg->ops->
						     get_desc((u8 *) entry,
						     true,
						     HW_DESC_TXBUFF_ADDR)),
					 skb->len, PCI_DMA_TODEVICE);

			ring->idx = (ring->idx + 1) % ring->entries;

			info = IEEE80211_SKB_CB(skb);
			ieee80211_tx_info_clear_status(info);

			info->flags |= IEEE80211_TX_STAT_ACK;
			/*info->status.rates[0].count = 1; */

			ieee80211_tx_status_irqsafe(hw, skb);

			if ((ring->entries - skb_queue_len(&ring->queue))
			    == 2 && prio != BEACON_QUEUE) {
				RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
					 ("more desc left, wake "
					  "skb_queue@%d,ring->idx = %d,"
					  "skb_queue_len = 0x%d\n",
					  prio, ring->idx,
					  skb_queue_len(&ring->queue)));

				ieee80211_wake_queue(hw,
						     skb_get_queue_mapping
						     (skb));
			}

			skb = NULL;
		}
	}
}

static irqreturn_t _rtl_pci_interrupt(int irq, void *dev_id)
{
	struct ieee80211_hw *hw = dev_id;
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	unsigned long flags;
	u32 inta = 0;
	u32 intb = 0;

	if (rtlpci->irq_enabled == 0)
		return IRQ_HANDLED;

	spin_lock_irqsave(&rtlpriv->locks.irq_th_lock, flags);

	/*read ISR: 4/8bytes */
	rtlpriv->cfg->ops->interrupt_recognized(hw, &inta, &intb);

	/*Shared IRQ or HW disappared */
	if (!inta || inta == 0xffff)
		goto done;

	/*<1> beacon related */
	if (inta & rtlpriv->cfg->maps[RTL_IMR_TBDOK]) {
		RT_TRACE(rtlpriv, COMP_INTR, DBG_TRACE,
			 ("beacon ok interrupt!\n"));
	}

	if (unlikely(inta & rtlpriv->cfg->maps[RTL_IMR_TBDER])) {
		RT_TRACE(rtlpriv, COMP_INTR, DBG_TRACE,
			 ("beacon err interrupt!\n"));
	}

	if (inta & rtlpriv->cfg->maps[RTL_IMR_BDOK]) {
		RT_TRACE(rtlpriv, COMP_INTR, DBG_TRACE,
			 ("beacon interrupt!\n"));
	}

	if (inta & rtlpriv->cfg->maps[RTL_IMR_BcnInt]) {
		RT_TRACE(rtlpriv, COMP_INTR, DBG_TRACE,
			 ("prepare beacon for interrupt!\n"));
		tasklet_schedule(&rtlpriv->works.irq_prepare_bcn_tasklet);
	}

	/*<3> Tx related */
	if (unlikely(inta & rtlpriv->cfg->maps[RTL_IMR_TXFOVW]))
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING, ("IMR_TXFOVW!\n"));

	if (inta & rtlpriv->cfg->maps[RTL_IMR_MGNTDOK]) {
		RT_TRACE(rtlpriv, COMP_INTR, DBG_TRACE,
			 ("Manage ok interrupt!\n"));
		_rtl_pci_tx_isr(hw, MGNT_QUEUE);
	}

	if (inta & rtlpriv->cfg->maps[RTL_IMR_HIGHDOK]) {
		RT_TRACE(rtlpriv, COMP_INTR, DBG_TRACE,
			 ("HIGH_QUEUE ok interrupt!\n"));
		_rtl_pci_tx_isr(hw, HIGH_QUEUE);
	}

	if (inta & rtlpriv->cfg->maps[RTL_IMR_BKDOK]) {
		rtlpriv->link_info.num_tx_inperiod++;

		RT_TRACE(rtlpriv, COMP_INTR, DBG_TRACE,
			 ("BK Tx OK interrupt!\n"));
		_rtl_pci_tx_isr(hw, BK_QUEUE);
	}

	if (inta & rtlpriv->cfg->maps[RTL_IMR_BEDOK]) {
		rtlpriv->link_info.num_tx_inperiod++;

		RT_TRACE(rtlpriv, COMP_INTR, DBG_TRACE,
			 ("BE TX OK interrupt!\n"));
		_rtl_pci_tx_isr(hw, BE_QUEUE);
	}

	if (inta & rtlpriv->cfg->maps[RTL_IMR_VIDOK]) {
		rtlpriv->link_info.num_tx_inperiod++;

		RT_TRACE(rtlpriv, COMP_INTR, DBG_TRACE,
			 ("VI TX OK interrupt!\n"));
		_rtl_pci_tx_isr(hw, VI_QUEUE);
	}

	if (inta & rtlpriv->cfg->maps[RTL_IMR_VODOK]) {
		rtlpriv->link_info.num_tx_inperiod++;

		RT_TRACE(rtlpriv, COMP_INTR, DBG_TRACE,
			 ("Vo TX OK interrupt!\n"));
		_rtl_pci_tx_isr(hw, VO_QUEUE);
	}

	/*<2> Rx related */
	if (inta & rtlpriv->cfg->maps[RTL_IMR_ROK]) {
		RT_TRACE(rtlpriv, COMP_INTR, DBG_TRACE, ("Rx ok interrupt!\n"));
		tasklet_schedule(&rtlpriv->works.irq_tasklet);
	}

	if (unlikely(inta & rtlpriv->cfg->maps[RTL_IMR_RDU])) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 ("rx descriptor unavailable!\n"));
		tasklet_schedule(&rtlpriv->works.irq_tasklet);
	}

	if (unlikely(inta & rtlpriv->cfg->maps[RTL_IMR_RXFOVW])) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING, ("rx overflow !\n"));
		tasklet_schedule(&rtlpriv->works.irq_tasklet);
	}

	spin_unlock_irqrestore(&rtlpriv->locks.irq_th_lock, flags);
	return IRQ_HANDLED;

done:
	spin_unlock_irqrestore(&rtlpriv->locks.irq_th_lock, flags);
	return IRQ_HANDLED;
}

static void _rtl_pci_irq_tasklet(struct ieee80211_hw *hw)
{
	_rtl_pci_rx_interrupt(hw);
}

static void _rtl_pci_prepare_bcn_tasklet(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl8192_tx_ring *ring = &rtlpci->tx_ring[BEACON_QUEUE];
	struct ieee80211_hdr *hdr = NULL;
	struct ieee80211_tx_info *info = NULL;
	struct sk_buff *pskb = NULL;
	struct rtl_tx_desc *pdesc = NULL;
	unsigned int queue_index;
	u8 temp_one = 1;

	ring = &rtlpci->tx_ring[BEACON_QUEUE];
	pskb = __skb_dequeue(&ring->queue);
	if (pskb)
		kfree_skb(pskb);

	/*NB: the beacon data buffer must be 32-bit aligned. */
	pskb = ieee80211_beacon_get(hw, mac->vif);
	if (pskb == NULL)
		return;
	hdr = (struct ieee80211_hdr *)(pskb->data);
	info = IEEE80211_SKB_CB(pskb);

	queue_index = BEACON_QUEUE;

	pdesc = &ring->desc[0];
	rtlpriv->cfg->ops->fill_tx_desc(hw, hdr, (u8 *) pdesc,
					info, pskb, queue_index);

	__skb_queue_tail(&ring->queue, pskb);

	rtlpriv->cfg->ops->set_desc((u8 *) pdesc, true, HW_DESC_OWN,
				    (u8 *)&temp_one);

	return;
}

static void _rtl_pci_init_trx_var(struct ieee80211_hw *hw)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u8 i;

	for (i = 0; i < RTL_PCI_MAX_TX_QUEUE_COUNT; i++)
		rtlpci->txringcount[i] = RT_TXDESC_NUM;

	/*
	 *we just alloc 2 desc for beacon queue,
	 *because we just need first desc in hw beacon.
	 */
	rtlpci->txringcount[BEACON_QUEUE] = 2;

	/*
	 *BE queue need more descriptor for performance
	 *consideration or, No more tx desc will happen,
	 *and may cause mac80211 mem leakage.
	 */
	rtlpci->txringcount[BE_QUEUE] = RT_TXDESC_NUM_BE_QUEUE;

	rtlpci->rxbuffersize = 9100;	/*2048/1024; */
	rtlpci->rxringcount = RTL_PCI_MAX_RX_COUNT;	/*64; */
}

static void _rtl_pci_init_struct(struct ieee80211_hw *hw,
		struct pci_dev *pdev)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));

	rtlpci->up_first_time = true;
	rtlpci->being_init_adapter = false;

	rtlhal->hw = hw;
	rtlpci->pdev = pdev;

	ppsc->b_inactiveps = false;
	ppsc->b_leisure_ps = true;
	ppsc->b_fwctrl_lps = true;
	ppsc->b_reg_fwctrl_lps = 3;
	ppsc->reg_max_lps_awakeintvl = 5;

	if (ppsc->b_reg_fwctrl_lps == 1)
		ppsc->fwctrl_psmode = FW_PS_MIN_MODE;
	else if (ppsc->b_reg_fwctrl_lps == 2)
		ppsc->fwctrl_psmode = FW_PS_MAX_MODE;
	else if (ppsc->b_reg_fwctrl_lps == 3)
		ppsc->fwctrl_psmode = FW_PS_DTIM_MODE;

	/*Tx/Rx related var */
	_rtl_pci_init_trx_var(hw);

	 /*IBSS*/ mac->beacon_interval = 100;

	 /*AMPDU*/ mac->min_space_cfg = 0;
	mac->max_mss_density = 0;
	/*set sane AMPDU defaults */
	mac->current_ampdu_density = 7;
	mac->current_ampdu_factor = 3;

	 /*QOS*/ rtlpci->acm_method = eAcmWay2_SW;

	/*task */
	tasklet_init(&rtlpriv->works.irq_tasklet,
		     (void (*)(unsigned long))_rtl_pci_irq_tasklet,
		     (unsigned long)hw);
	tasklet_init(&rtlpriv->works.irq_prepare_bcn_tasklet,
		     (void (*)(unsigned long))_rtl_pci_prepare_bcn_tasklet,
		     (unsigned long)hw);
}

static int _rtl_pci_init_tx_ring(struct ieee80211_hw *hw,
				 unsigned int prio, unsigned int entries)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_tx_desc *ring;
	dma_addr_t dma;
	u32 nextdescaddress;
	int i;

	ring = pci_alloc_consistent(rtlpci->pdev,
				    sizeof(*ring) * entries, &dma);

	if (!ring || (unsigned long)ring & 0xFF) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 ("Cannot allocate TX ring (prio = %d)\n", prio));
		return -ENOMEM;
	}

	memset(ring, 0, sizeof(*ring) * entries);
	rtlpci->tx_ring[prio].desc = ring;
	rtlpci->tx_ring[prio].dma = dma;
	rtlpci->tx_ring[prio].idx = 0;
	rtlpci->tx_ring[prio].entries = entries;
	skb_queue_head_init(&rtlpci->tx_ring[prio].queue);

	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD,
		 ("queue:%d, ring_addr:%p\n", prio, ring));

	for (i = 0; i < entries; i++) {
		nextdescaddress = cpu_to_le32((u32) dma +
					      ((i + 1) % entries) *
					      sizeof(*ring));

		rtlpriv->cfg->ops->set_desc((u8 *)&(ring[i]),
					    true, HW_DESC_TX_NEXTDESC_ADDR,
					    (u8 *)&nextdescaddress);
	}

	return 0;
}

static int _rtl_pci_init_rx_ring(struct ieee80211_hw *hw)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_rx_desc *entry = NULL;
	int i, rx_queue_idx;
	u8 tmp_one = 1;

	/*
	 *rx_queue_idx 0:RX_MPDU_QUEUE
	 *rx_queue_idx 1:RX_CMD_QUEUE
	 */
	for (rx_queue_idx = 0; rx_queue_idx < RTL_PCI_MAX_RX_QUEUE;
	     rx_queue_idx++) {
		rtlpci->rx_ring[rx_queue_idx].desc =
		    pci_alloc_consistent(rtlpci->pdev,
					 sizeof(*rtlpci->rx_ring[rx_queue_idx].
						desc) * rtlpci->rxringcount,
					 &rtlpci->rx_ring[rx_queue_idx].dma);

		if (!rtlpci->rx_ring[rx_queue_idx].desc ||
		    (unsigned long)rtlpci->rx_ring[rx_queue_idx].desc & 0xFF) {
			RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
				 ("Cannot allocate RX ring\n"));
			return -ENOMEM;
		}

		memset(rtlpci->rx_ring[rx_queue_idx].desc, 0,
		       sizeof(*rtlpci->rx_ring[rx_queue_idx].desc) *
		       rtlpci->rxringcount);

		rtlpci->rx_ring[rx_queue_idx].idx = 0;

		for (i = 0; i < rtlpci->rxringcount; i++) {
			struct sk_buff *skb =
			    dev_alloc_skb(rtlpci->rxbuffersize);
			u32 bufferaddress;
			entry = &rtlpci->rx_ring[rx_queue_idx].desc[i];
			if (!skb)
				return 0;

			/*skb->dev = dev; */

			rtlpci->rx_ring[rx_queue_idx].rx_buf[i] = skb;

			/*
			 *just set skb->cb to mapping addr
			 *for pci_unmap_single use
			 */
			*((dma_addr_t *) skb->cb) =
			    pci_map_single(rtlpci->pdev, skb_tail_pointer(skb),
					   rtlpci->rxbuffersize,
					   PCI_DMA_FROMDEVICE);

			bufferaddress = cpu_to_le32(*((dma_addr_t *)skb->cb));
			rtlpriv->cfg->ops->set_desc((u8 *)entry, false,
						    HW_DESC_RXBUFF_ADDR,
						    (u8 *)&bufferaddress);
			rtlpriv->cfg->ops->set_desc((u8 *)entry, false,
						    HW_DESC_RXPKT_LEN,
						    (u8 *)&rtlpci->
						    rxbuffersize);
			rtlpriv->cfg->ops->set_desc((u8 *) entry, false,
						    HW_DESC_RXOWN,
						    (u8 *)&tmp_one);
		}

		rtlpriv->cfg->ops->set_desc((u8 *) entry, false,
					    HW_DESC_RXERO, (u8 *)&tmp_one);
	}
	return 0;
}

static void _rtl_pci_free_tx_ring(struct ieee80211_hw *hw,
		unsigned int prio)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl8192_tx_ring *ring = &rtlpci->tx_ring[prio];

	while (skb_queue_len(&ring->queue)) {
		struct rtl_tx_desc *entry = &ring->desc[ring->idx];
		struct sk_buff *skb = __skb_dequeue(&ring->queue);

		pci_unmap_single(rtlpci->pdev,
				 le32_to_cpu(rtlpriv->cfg->
					     ops->get_desc((u8 *) entry, true,
						   HW_DESC_TXBUFF_ADDR)),
				 skb->len, PCI_DMA_TODEVICE);
		kfree_skb(skb);
		ring->idx = (ring->idx + 1) % ring->entries;
	}

	pci_free_consistent(rtlpci->pdev,
			    sizeof(*ring->desc) * ring->entries,
			    ring->desc, ring->dma);
	ring->desc = NULL;
}

static void _rtl_pci_free_rx_ring(struct rtl_pci *rtlpci)
{
	int i, rx_queue_idx;

	/*rx_queue_idx 0:RX_MPDU_QUEUE */
	/*rx_queue_idx 1:RX_CMD_QUEUE */
	for (rx_queue_idx = 0; rx_queue_idx < RTL_PCI_MAX_RX_QUEUE;
	     rx_queue_idx++) {
		for (i = 0; i < rtlpci->rxringcount; i++) {
			struct sk_buff *skb =
			    rtlpci->rx_ring[rx_queue_idx].rx_buf[i];
			if (!skb)
				continue;

			pci_unmap_single(rtlpci->pdev,
					 *((dma_addr_t *) skb->cb),
					 rtlpci->rxbuffersize,
					 PCI_DMA_FROMDEVICE);
			kfree_skb(skb);
		}

		pci_free_consistent(rtlpci->pdev,
				    sizeof(*rtlpci->rx_ring[rx_queue_idx].
					   desc) * rtlpci->rxringcount,
				    rtlpci->rx_ring[rx_queue_idx].desc,
				    rtlpci->rx_ring[rx_queue_idx].dma);
		rtlpci->rx_ring[rx_queue_idx].desc = NULL;
	}
}

static int _rtl_pci_init_trx_ring(struct ieee80211_hw *hw)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	int ret;
	int i;

	ret = _rtl_pci_init_rx_ring(hw);
	if (ret)
		return ret;

	for (i = 0; i < RTL_PCI_MAX_TX_QUEUE_COUNT; i++) {
		ret = _rtl_pci_init_tx_ring(hw, i,
				 rtlpci->txringcount[i]);
		if (ret)
			goto err_free_rings;
	}

	return 0;

err_free_rings:
	_rtl_pci_free_rx_ring(rtlpci);

	for (i = 0; i < RTL_PCI_MAX_TX_QUEUE_COUNT; i++)
		if (rtlpci->tx_ring[i].desc)
			_rtl_pci_free_tx_ring(hw, i);

	return 1;
}

static int _rtl_pci_deinit_trx_ring(struct ieee80211_hw *hw)
{
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	u32 i;

	/*free rx rings */
	_rtl_pci_free_rx_ring(rtlpci);

	/*free tx rings */
	for (i = 0; i < RTL_PCI_MAX_TX_QUEUE_COUNT; i++)
		_rtl_pci_free_tx_ring(hw, i);

	return 0;
}

int rtl_pci_reset_trx_ring(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	int i, rx_queue_idx;
	unsigned long flags;
	u8 tmp_one = 1;

	/*rx_queue_idx 0:RX_MPDU_QUEUE */
	/*rx_queue_idx 1:RX_CMD_QUEUE */
	for (rx_queue_idx = 0; rx_queue_idx < RTL_PCI_MAX_RX_QUEUE;
	     rx_queue_idx++) {
		/*
		 *force the rx_ring[RX_MPDU_QUEUE/
		 *RX_CMD_QUEUE].idx to the first one
		 */
		if (rtlpci->rx_ring[rx_queue_idx].desc) {
			struct rtl_rx_desc *entry = NULL;

			for (i = 0; i < rtlpci->rxringcount; i++) {
				entry = &rtlpci->rx_ring[rx_queue_idx].desc[i];
				rtlpriv->cfg->ops->set_desc((u8 *) entry,
							    false,
							    HW_DESC_RXOWN,
							    (u8 *)&tmp_one);
			}
			rtlpci->rx_ring[rx_queue_idx].idx = 0;
		}
	}

	/*
	 *after reset, release previous pending packet,
	 *and force the  tx idx to the first one
	 */
	spin_lock_irqsave(&rtlpriv->locks.irq_th_lock, flags);
	for (i = 0; i < RTL_PCI_MAX_TX_QUEUE_COUNT; i++) {
		if (rtlpci->tx_ring[i].desc) {
			struct rtl8192_tx_ring *ring = &rtlpci->tx_ring[i];

			while (skb_queue_len(&ring->queue)) {
				struct rtl_tx_desc *entry =
				    &ring->desc[ring->idx];
				struct sk_buff *skb =
				    __skb_dequeue(&ring->queue);

				pci_unmap_single(rtlpci->pdev,
						 le32_to_cpu(rtlpriv->cfg->ops->
							 get_desc((u8 *)
							 entry,
							 true,
							 HW_DESC_TXBUFF_ADDR)),
						 skb->len, PCI_DMA_TODEVICE);
				kfree_skb(skb);
				ring->idx = (ring->idx + 1) % ring->entries;
			}
			ring->idx = 0;
		}
	}

	spin_unlock_irqrestore(&rtlpriv->locks.irq_th_lock, flags);

	return 0;
}

unsigned int _rtl_mac_to_hwqueue(u16 fc,
		unsigned int mac80211_queue_index)
{
	unsigned int hw_queue_index;

	if (unlikely(ieee80211_is_beacon(fc))) {
		hw_queue_index = BEACON_QUEUE;
		goto out;
	}

	if (ieee80211_is_mgmt(fc)) {
		hw_queue_index = MGNT_QUEUE;
		goto out;
	}

	switch (mac80211_queue_index) {
	case 0:
		hw_queue_index = VO_QUEUE;
		break;
	case 1:
		hw_queue_index = VI_QUEUE;
		break;
	case 2:
		hw_queue_index = BE_QUEUE;;
		break;
	case 3:
		hw_queue_index = BK_QUEUE;
		break;
	default:
		hw_queue_index = BE_QUEUE;
		RT_ASSERT(false, ("QSLT_BE queue, skb_queue:%d\n",
				  mac80211_queue_index));
		break;
	}

out:
	return hw_queue_index;
}

int rtl_pci_tx(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_mac *mac = rtl_mac(rtl_priv(hw));
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct rtl8192_tx_ring *ring;
	struct rtl_tx_desc *pdesc;
	u8 idx;
	unsigned int queue_index, hw_queue;
	unsigned long flags;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *)(skb->data);
	u16 fc = le16_to_cpu(hdr->frame_control);
	u8 *pda_addr = hdr->addr1;
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	/*ssn */
	u8 *qc = NULL;
	u8 tid = 0;
	u16 seq_number = 0;
	u8 own;
	u8 temp_one = 1;

	if (ieee80211_is_mgmt(fc))
		rtl_tx_mgmt_proc(hw, skb);
	rtl_action_proc(hw, skb, true);

	queue_index = skb_get_queue_mapping(skb);
	hw_queue = _rtl_mac_to_hwqueue(fc, queue_index);

	if (is_multicast_ether_addr(pda_addr))
		rtlpriv->stats.txbytesmulticast += skb->len;
	else if (is_broadcast_ether_addr(pda_addr))
		rtlpriv->stats.txbytesbroadcast += skb->len;
	else
		rtlpriv->stats.txbytesunicast += skb->len;

	spin_lock_irqsave(&rtlpriv->locks.irq_th_lock, flags);

	ring = &rtlpci->tx_ring[hw_queue];
	if (hw_queue != BEACON_QUEUE)
		idx = (ring->idx + skb_queue_len(&ring->queue)) %
				ring->entries;
	else
		idx = 0;

	pdesc = &ring->desc[idx];
	own = (u8) rtlpriv->cfg->ops->get_desc((u8 *) pdesc,
			true, HW_DESC_OWN);

	if ((own == 1) && (hw_queue != BEACON_QUEUE)) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 ("No more TX desc@%d, ring->idx = %d,"
			  "idx = %d, skb_queue_len = 0x%d\n",
			  hw_queue, ring->idx, idx,
			  skb_queue_len(&ring->queue)));

		spin_unlock_irqrestore(&rtlpriv->locks.irq_th_lock, flags);
		return skb->len;
	}

	/*
	 *if(ieee80211_is_nullfunc(fc)) {
	 *      spin_unlock_irqrestore(&rtlpriv->locks.irq_th_lock, flags);
	 *      return 1;
	 *}
	 */

	if (ieee80211_is_data_qos(fc)) {
		qc = ieee80211_get_qos_ctl(hdr);
		tid = qc[0] & IEEE80211_QOS_CTL_TID_MASK;

		seq_number = mac->tids[tid].seq_number;
		seq_number &= IEEE80211_SCTL_SEQ;
		/*
		 *hdr->seq_ctrl = hdr->seq_ctrl &
		 *cpu_to_le16(IEEE80211_SCTL_FRAG);
		 *hdr->seq_ctrl |= cpu_to_le16(seq_number);
		 */

		seq_number += 1;
	}

	if (ieee80211_is_data(fc))
		rtlpriv->cfg->ops->led_control(hw, LED_CTL_TX);

	rtlpriv->cfg->ops->fill_tx_desc(hw, hdr, (u8 *) pdesc,
					info, skb, hw_queue);

	__skb_queue_tail(&ring->queue, skb);

	rtlpriv->cfg->ops->set_desc((u8 *) pdesc, true,
				    HW_DESC_OWN, (u8 *)&temp_one);

	if (!ieee80211_has_morefrags(hdr->frame_control)) {
		if (qc)
			mac->tids[tid].seq_number = seq_number;
	}

	if ((ring->entries - skb_queue_len(&ring->queue)) < 2 &&
	    hw_queue != BEACON_QUEUE) {

		RT_TRACE(rtlpriv, COMP_ERR, DBG_LOUD,
			 ("less desc left, stop skb_queue@%d, "
			  "ring->idx = %d,"
			  "idx = %d, skb_queue_len = 0x%d\n",
			  hw_queue, ring->idx, idx,
			  skb_queue_len(&ring->queue)));

		ieee80211_stop_queue(hw, skb_get_queue_mapping(skb));
	}

	spin_unlock_irqrestore(&rtlpriv->locks.irq_th_lock, flags);

	rtlpriv->cfg->ops->tx_polling(hw, hw_queue);

	return 0;
}

void rtl_pci_deinit(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));

	_rtl_pci_deinit_trx_ring(hw);

	synchronize_irq(rtlpci->pdev->irq);
	tasklet_kill(&rtlpriv->works.irq_tasklet);

	flush_workqueue(rtlpriv->works.rtl_wq);
	destroy_workqueue(rtlpriv->works.rtl_wq);

}

int rtl_pci_init(struct ieee80211_hw *hw, struct pci_dev *pdev)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	int err;

	_rtl_pci_init_struct(hw, pdev);

	err = _rtl_pci_init_trx_ring(hw);
	if (err) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 ("tx ring initialization failed"));
		return err;
	}

	return 1;
}

int rtl_pci_start(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));

	int err;

	rtl_pci_reset_trx_ring(hw);

	rtlpci->driver_is_goingto_unload = false;
	err = rtlpriv->cfg->ops->hw_init(hw);
	if (err) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
			 ("Failed to config hardware!\n"));
		return err;
	}

	rtlpriv->cfg->ops->enable_interrupt(hw);
	RT_TRACE(rtlpriv, COMP_INIT, DBG_LOUD, ("enable_interrupt OK\n"));

	rtl_init_rx_config(hw);

	/*should after adapter start and interrupt enable. */
	set_hal_start(rtlhal);

	RT_CLEAR_PS_LEVEL(ppsc, RT_RF_OFF_LEVL_HALT_NIC);

	rtlpci->up_first_time = false;

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG, ("OK\n"));
	return 0;
}

void rtl_pci_stop(struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(rtl_pcipriv(hw));
	struct rtl_ps_ctl *ppsc = rtl_psc(rtl_priv(hw));
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	unsigned long flags;
	u8 RFInProgressTimeOut = 0;

	/*
	 *should before disable interrrupt&adapter
	 *and will do it immediately.
	 */
	set_hal_stop(rtlhal);

	rtlpriv->cfg->ops->disable_interrupt(hw);

	spin_lock_irqsave(&rtlpriv->locks.rf_ps_lock, flags);
	while (ppsc->rfchange_inprogress) {
		spin_unlock_irqrestore(&rtlpriv->locks.rf_ps_lock, flags);
		if (RFInProgressTimeOut > 100) {
			spin_lock_irqsave(&rtlpriv->locks.rf_ps_lock, flags);
			break;
		}
		mdelay(1);
		RFInProgressTimeOut++;
		spin_lock_irqsave(&rtlpriv->locks.rf_ps_lock, flags);
	}
	ppsc->rfchange_inprogress = true;
	spin_unlock_irqrestore(&rtlpriv->locks.rf_ps_lock, flags);

	rtlpci->driver_is_goingto_unload = true;
	rtlpriv->cfg->ops->hw_disable(hw);
	rtlpriv->cfg->ops->led_control(hw, LED_CTL_POWER_OFF);

	spin_lock_irqsave(&rtlpriv->locks.rf_ps_lock, flags);
	ppsc->rfchange_inprogress = false;
	spin_unlock_irqrestore(&rtlpriv->locks.rf_ps_lock, flags);

	rtl_pci_enable_aspm(hw);
}

static bool _rtl_pci_find_adapter(struct pci_dev *pdev,
		struct ieee80211_hw *hw)
{
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_hal *rtlhal = rtl_hal(rtl_priv(hw));
	struct pci_dev *bridge_pdev = pdev->bus->self;
	u16 venderid;
	u16 deviceid;
	u8 revisionid;
	u16 irqline;
	u8 tmp;

	venderid = pdev->vendor;
	deviceid = pdev->device;
	pci_read_config_byte(pdev, 0x8, &revisionid);
	pci_read_config_word(pdev, 0x3C, &irqline);

	if (deviceid == RTL_PCI_8192_DID ||
	    deviceid == RTL_PCI_0044_DID ||
	    deviceid == RTL_PCI_0047_DID ||
	    deviceid == RTL_PCI_8192SE_DID ||
	    deviceid == RTL_PCI_8174_DID ||
	    deviceid == RTL_PCI_8173_DID ||
	    deviceid == RTL_PCI_8172_DID ||
	    deviceid == RTL_PCI_8171_DID) {
		switch (revisionid) {
		case RTL_PCI_REVISION_ID_8192PCIE:
			RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
				 ("8192 PCI-E is found - "
				  "vid/did=%x/%x\n", venderid, deviceid));
			rtlhal->hw_type = HARDWARE_TYPE_RTL8192E;
			break;
		case RTL_PCI_REVISION_ID_8192SE:
			RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
				 ("8192SE is found - "
				  "vid/did=%x/%x\n", venderid, deviceid));
			rtlhal->hw_type = HARDWARE_TYPE_RTL8192SE;
			break;
		default:
			RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
				 ("Err: Unknown device - "
				  "vid/did=%x/%x\n", venderid, deviceid));
			rtlhal->hw_type = HARDWARE_TYPE_RTL8192SE;
			break;

		}
	} else if (deviceid == RTL_PCI_8192CET_DID ||
		   deviceid == RTL_PCI_8192CE_DID ||
		   deviceid == RTL_PCI_8191CE_DID ||
		   deviceid == RTL_PCI_8188CE_DID) {
		rtlhal->hw_type = HARDWARE_TYPE_RTL8192CE;
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
			 ("8192C PCI-E is found - "
			  "vid/did=%x/%x\n", venderid, deviceid));
	} else {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_WARNING,
			 ("Err: Unknown device -"
			  " vid/did=%x/%x\n", venderid, deviceid));

		rtlhal->hw_type = RTL_DEFAULT_HARDWARE_TYPE;
	}

	/*find bus info */
	pcipriv->ndis_adapter.busnumber = pdev->bus->number;
	pcipriv->ndis_adapter.devnumber = PCI_SLOT(pdev->devfn);
	pcipriv->ndis_adapter.funcnumber = PCI_FUNC(pdev->devfn);

	/*find bridge info */
	pcipriv->ndis_adapter.pcibridge_vendorid = bridge_pdev->vendor;
	for (tmp = 0; tmp < PCI_BRIDGE_VENDOR_MAX; tmp++) {
		if (bridge_pdev->vendor == pcibridge_vendors[tmp]) {
			pcipriv->ndis_adapter.pcibridge_vendor = tmp;
			RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
				 ("Pci Bridge Vendor is found index: %d\n",
				  tmp));
			break;
		}
	}

	if (pcipriv->ndis_adapter.pcibridge_vendor !=
		PCI_BRIDGE_VENDOR_UNKNOWN) {
		pcipriv->ndis_adapter.pcibridge_busnum =
		    bridge_pdev->bus->number;
		pcipriv->ndis_adapter.pcibridge_devnum =
		    PCI_SLOT(bridge_pdev->devfn);
		pcipriv->ndis_adapter.pcibridge_funcnum =
		    PCI_FUNC(bridge_pdev->devfn);
		pcipriv->ndis_adapter.pcibridge_pciehdr_offset =
		    pci_pcie_cap(bridge_pdev);
		pcipriv->ndis_adapter.pcicfg_addrport =
		    (pcipriv->ndis_adapter.pcibridge_busnum << 16) |
		    (pcipriv->ndis_adapter.pcibridge_devnum << 11) |
		    (pcipriv->ndis_adapter.pcibridge_funcnum << 8) | (1 << 31);
		pcipriv->ndis_adapter.num4bytes =
		    (pcipriv->ndis_adapter.pcibridge_pciehdr_offset + 0x10) / 4;

		rtl_pci_get_linkcontrol_field(hw);

		if (pcipriv->ndis_adapter.pcibridge_vendor ==
		    PCI_BRIDGE_VENDOR_AMD) {
			pcipriv->ndis_adapter.amd_l1_patch =
			    rtl_pci_get_amd_l1_patch(hw);
		}
	}

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
		 ("pcidev busnumber:devnumber:funcnumber:"
		  "vendor:link_ctl %d:%d:%d:%x:%x\n",
		  pcipriv->ndis_adapter.busnumber,
		  pcipriv->ndis_adapter.devnumber,
		  pcipriv->ndis_adapter.funcnumber,
		  pdev->vendor, pcipriv->ndis_adapter.linkctrl_reg));

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
		 ("pci_bridge busnumber:devnumber:funcnumber:vendor:"
		  "pcie_cap:link_ctl_reg:amd %d:%d:%d:%x:%x:%x:%x\n",
		  pcipriv->ndis_adapter.pcibridge_busnum,
		  pcipriv->ndis_adapter.pcibridge_devnum,
		  pcipriv->ndis_adapter.pcibridge_funcnum,
		  pcibridge_vendors[pcipriv->ndis_adapter.pcibridge_vendor],
		  pcipriv->ndis_adapter.pcibridge_pciehdr_offset,
		  pcipriv->ndis_adapter.pcibridge_linkctrlreg,
		  pcipriv->ndis_adapter.amd_l1_patch));

	rtl_pci_parse_configuration(pdev, hw);

	return true;
}

int __devinit rtl_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	struct ieee80211_hw *hw = NULL;

	struct rtl_priv *rtlpriv = NULL;
	struct rtl_pci_priv *pcipriv = NULL;
	struct rtl_pci *rtlpci;
	unsigned long pmem_start, pmem_len, pmem_flags;
	int err;

	err = pci_enable_device(pdev);
	if (err) {
		RT_ASSERT(false,
			  ("%s : Cannot enable new PCI device\n",
			   pci_name(pdev)));
		return err;
	}

	if (!pci_set_dma_mask(pdev, DMA_BIT_MASK(32))) {
		if (pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32))) {
			RT_ASSERT(false, ("Unable to obtain 32bit DMA "
					  "for consistent allocations\n"));
			pci_disable_device(pdev);
			return -ENOMEM;
		}
	}

	pci_set_master(pdev);

	hw = ieee80211_alloc_hw(sizeof(struct rtl_pci_priv) +
				sizeof(struct rtl_priv), &rtl_ops);
	if (!hw) {
		RT_ASSERT(false,
			  ("%s : ieee80211 alloc failed\n", pci_name(pdev)));
		err = -ENOMEM;
		goto fail1;
	}

	SET_IEEE80211_DEV(hw, &pdev->dev);
	pci_set_drvdata(pdev, hw);

	rtlpriv = hw->priv;
	pcipriv = (void *)rtlpriv->priv;
	pcipriv->dev.pdev = pdev;

	/*
	 *init dbgp flags before all
	 *other functions, because we will
	 *use it in other funtions like
	 *RT_TRACE/RT_PRINT/RTL_PRINT_DATA
	 *you can not use these macro
	 *before this
	 */
	rtl_dbgp_flag_init(hw);

	/* MEM map */
	err = pci_request_regions(pdev, KBUILD_MODNAME);
	if (err) {
		RT_ASSERT(false, ("Can't obtain PCI resources\n"));
		return err;
	}

	pmem_start = pci_resource_start(pdev, 2);
	pmem_len = pci_resource_len(pdev, 2);
	pmem_flags = pci_resource_flags(pdev, 2);

	/*shared mem start */
	rtlpriv->io.pci_mem_start =
			(unsigned long)pci_iomap(pdev, 2, pmem_len);
	if (rtlpriv->io.pci_mem_start == 0) {
		RT_ASSERT(false, ("Can't map PCI mem\n"));
		goto fail2;
	}

	RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
		 ("mem mapped space: start: 0x%08lx len:%08lx "
		  "flags:%08lx, after map:0x%08lx\n",
		  pmem_start, pmem_len, pmem_flags,
		  rtlpriv->io.pci_mem_start));

	/* Disable Clk Request */
	pci_write_config_byte(pdev, 0x81, 0);
	/* leave D3 mode */
	pci_write_config_byte(pdev, 0x44, 0);
	pci_write_config_byte(pdev, 0x04, 0x06);
	pci_write_config_byte(pdev, 0x04, 0x07);

	/* init cfg & intf_ops */
	rtlpriv->rtlhal.interface = INTF_PCI;
	rtlpriv->cfg = (struct rtl_hal_cfg *)(id->driver_data);
	rtlpriv->intf_ops = &rtl_pci_ops;

	/* find adapter */
	_rtl_pci_find_adapter(pdev, hw);

	/* Init IO handler */
	_rtl_pci_io_handler_init(&pdev->dev, hw);

	/*like read eeprom and so on */
	rtlpriv->cfg->ops->read_eeprom_info(hw);

	if (rtlpriv->cfg->ops->init_sw_vars(hw)) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 ("Can't init_sw_vars.\n"));
		goto fail3;
	}

	rtlpriv->cfg->ops->init_sw_leds(hw);

	/*aspm */
	rtl_pci_init_aspm(hw);

	/* Init mac80211 sw */
	err = rtl_init_core(hw);
	if (err) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 ("Can't allocate sw for mac80211.\n"));
		goto fail3;
	}

	/* Init PCI sw */
	err = !rtl_pci_init(hw, pdev);
	if (err) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 ("Failed to init PCI.\n"));
		goto fail3;
	}

	err = ieee80211_register_hw(hw);
	if (err) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 ("Can't register mac80211 hw.\n"));
		goto fail3;
	} else {
		rtlpriv->mac80211.mac80211_registered = 1;
	}

	err = sysfs_create_group(&pdev->dev.kobj, &rtl_attribute_group);
	if (err) {
		RT_TRACE(rtlpriv, COMP_ERR, DBG_EMERG,
			 ("failed to create sysfs device attributes\n"));
		goto fail3;
	}

	/*init rfkill */
	rtl_init_rfkill(hw);

	rtlpci = rtl_pcidev(pcipriv);
	err = request_irq(rtlpci->pdev->irq, &_rtl_pci_interrupt,
			  IRQF_SHARED, KBUILD_MODNAME, hw);
	if (err) {
		RT_TRACE(rtlpriv, COMP_INIT, DBG_DMESG,
			 ("%s: failed to register IRQ handler\n",
			  wiphy_name(hw->wiphy)));
		goto fail3;
	} else {
		rtlpci->irq_alloc = 1;
	}

	set_bit(RTL_STATUS_INTERFACE_START, &rtlpriv->status);
	return 0;

fail3:
	pci_set_drvdata(pdev, NULL);
	rtl_deinit_core(hw);
	_rtl_pci_io_handler_release(hw);
	ieee80211_free_hw(hw);

	if (rtlpriv->io.pci_mem_start != 0)
		pci_iounmap(pdev, (void *)rtlpriv->io.pci_mem_start);

fail2:
	pci_release_regions(pdev);

fail1:

	pci_disable_device(pdev);

	return -ENODEV;

}
EXPORT_SYMBOL(rtl_pci_probe);

void rtl_pci_disconnect(struct pci_dev *pdev)
{
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct rtl_pci_priv *pcipriv = rtl_pcipriv(hw);
	struct rtl_priv *rtlpriv = rtl_priv(hw);
	struct rtl_pci *rtlpci = rtl_pcidev(pcipriv);
	struct rtl_mac *rtlmac = rtl_mac(rtlpriv);

	clear_bit(RTL_STATUS_INTERFACE_START, &rtlpriv->status);

	sysfs_remove_group(&pdev->dev.kobj, &rtl_attribute_group);

	/*ieee80211_unregister_hw will call ops_stop */
	if (rtlmac->mac80211_registered == 1) {
		ieee80211_unregister_hw(hw);
		rtlmac->mac80211_registered = 0;
	} else {
		rtl_deinit_deferred_work(hw);
		rtlpriv->intf_ops->adapter_stop(hw);
	}

	/*deinit rfkill */
	rtl_deinit_rfkill(hw);

	rtl_pci_deinit(hw);
	rtl_deinit_core(hw);
	rtlpriv->cfg->ops->deinit_sw_leds(hw);
	_rtl_pci_io_handler_release(hw);
	rtlpriv->cfg->ops->deinit_sw_vars(hw);

	if (rtlpci->irq_alloc) {
		free_irq(rtlpci->pdev->irq, hw);
		rtlpci->irq_alloc = 0;
	}

	if (rtlpriv->io.pci_mem_start != 0) {
		pci_iounmap(pdev, (void *)rtlpriv->io.pci_mem_start);
		pci_release_regions(pdev);
	}

	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);

	ieee80211_free_hw(hw);
}
EXPORT_SYMBOL(rtl_pci_disconnect);

/***************************************
kernel pci power state define:
PCI_D0         ((pci_power_t __force) 0)
PCI_D1         ((pci_power_t __force) 1)
PCI_D2         ((pci_power_t __force) 2)
PCI_D3hot      ((pci_power_t __force) 3)
PCI_D3cold     ((pci_power_t __force) 4)
PCI_UNKNOWN    ((pci_power_t __force) 5)

This function is called when system
goes into suspend state mac80211 will
call rtl_mac_stop() from the mac80211
suspend function first, So there is
no need to call hw_disable here.
****************************************/
int rtl_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);

	return 0;
}
EXPORT_SYMBOL(rtl_pci_suspend);

int rtl_pci_resume(struct pci_dev *pdev)
{
	int ret;

	pci_set_power_state(pdev, PCI_D0);
	ret = pci_enable_device(pdev);
	if (ret) {
		RT_ASSERT(false, ("ERR: <======\n"));
		return ret;
	}

	pci_restore_state(pdev);

	return 0;
}
EXPORT_SYMBOL(rtl_pci_resume);

struct rtl_intf_ops rtl_pci_ops = {
	.adapter_start = rtl_pci_start,
	.adapter_stop = rtl_pci_stop,
	.adapter_tx = rtl_pci_tx,
	.reset_trx_ring = rtl_pci_reset_trx_ring,

	.disable_aspm = rtl_pci_disable_aspm,
	.enable_aspm = rtl_pci_enable_aspm,
};
