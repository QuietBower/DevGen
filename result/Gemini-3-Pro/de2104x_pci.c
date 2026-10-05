/*
 * QEMU DEC 21040/21041 "Tulip" Ethernet Emulation
 *
 * Based on Linux driver: drivers/net/ethernet/dec/tulip/de2104x.c
 *
 * Copyright (c) 2024 QEMU Contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/bswap.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"
#include "net/net.h"

#define DRV_NAME		"de2104x"
#define PKT_BUF_SZ		1536
#define DE_RX_RING_SIZE		128
#define DE_TX_RING_SIZE		64
#define DE_SETUP_FRAME_WORDS	96
#define DE_EEPROM_WORDS		256
#define DE_EEPROM_SIZE		(DE_EEPROM_WORDS * sizeof(uint16_t))
#define DE_MAX_MEDIA		5
#define DE_NUM_REGS		16
#define DE_REGS_SIZE		0x80

/* CSR Offsets (Stride 8) */
#define CSR0_BUSMODE    0x00
#define CSR1_TXPOLL     0x08
#define CSR2_RXPOLL     0x10
#define CSR3_RXRING     0x18
#define CSR4_TXRING     0x20
#define CSR5_STATUS     0x28
#define CSR6_MACMODE    0x30
#define CSR7_INTRMASK   0x38
#define CSR8_RXMISSED   0x40
#define CSR9_ROMCMD     0x48
#define CSR11_TIMER     0x58
#define CSR12_SIASTATUS 0x60
#define CSR13_SIACONN   0x68
#define CSR14_SIATX     0x70
#define CSR15_SIAGEN    0x78

/* CSR5 Status Bits */
#define S_TI        0x00000001 /* Transmit Interrupt */
#define S_TPS       0x00000002 /* Transmit Process Stopped */
#define S_TU        0x00000004 /* Transmit Buffer Unavailable */
#define S_LNKPASS   0x00000010 /* Link Pass */
#define S_RI        0x00000040 /* Receive Interrupt */
#define S_RU        0x00000080 /* Receive Buffer Unavailable */
#define S_RPS       0x00000100 /* Receive Process Stopped */
#define S_LNKFAIL   0x00001000 /* Link Fail */
#define S_PCIERR    0x00002000 /* PCI Error */
#define S_AIS       0x00008000 /* Abnormal Interrupt Summary */
#define S_NIS       0x00010000 /* Normal Interrupt Summary */

/* CSR6 Mode Bits */
#define MODE_RESET  0x00000001
#define MODE_RXTX   0x00002000 /* Start/Stop Rx/Tx */

/* Descriptor Bits */
#define DESC_OWN    0x80000000 /* Owned by DMA */
#define DESC_LAST   0x40000000 /* Last Segment */
#define DESC_FIRST  0x20000000 /* First Segment */
#define DESC_SETUP  0x08000000 /* Setup Frame */
#define DESC_RER    0x02000000 /* Ring End */
#define DESC_CH     0x01000000 /* Chain */

struct de_desc {
    uint32_t opts1;
    uint32_t opts2;
    uint32_t addr1;
    uint32_t addr2;
};

#define TYPE_PCIBASE_DEVICE "de2104x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_DEC 0x1011
#define PCI_DEVICE_ID_DEC_TULIP 0x0002

#define VENDOR_ID PCI_VENDOR_ID_DEC
#define DEVICE_ID PCI_DEVICE_ID_DEC_TULIP
#define CLASS_ID  PCI_CLASS_NETWORK_ETHERNET

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    NICState *nic;
    NICConf conf;

    /* Registers */
    uint32_t regs[DE_NUM_REGS];

    /* Internal State */
    uint32_t tx_desc_addr;
    uint32_t rx_desc_addr;
    
    /* EEPROM Emulation for MAC Address */
    uint8_t rom_buf[128];
    int rom_idx;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    uint32_t status = s->regs[CSR5_STATUS >> 3];
    uint32_t mask = s->regs[CSR7_INTRMASK >> 3];

    /* Calculate summaries */
    if (status & (S_TI | S_TPS | S_TU))
        status |= S_NIS;
    if (status & (S_RI | S_RU | S_RPS))
        status |= S_NIS;
    if (status & (S_LNKFAIL | S_PCIERR))
        status |= S_AIS;

    /* Update status register with summaries */
    s->regs[CSR5_STATUS >> 3] = status;

    /* Trigger IRQ if any unmasked bit is set */
    pci_set_irq(&s->parent_obj, (status & mask) ? 1 : 0);
}

static void pcibase_fetch_desc(PCIBaseState *s, uint32_t addr, struct de_desc *desc)
{
    pci_dma_read(&s->parent_obj, addr, desc, sizeof(*desc));
    desc->opts1 = le32_to_cpu(desc->opts1);
    desc->opts2 = le32_to_cpu(desc->opts2);
    desc->addr1 = le32_to_cpu(desc->addr1);
    desc->addr2 = le32_to_cpu(desc->addr2);
}

static void pcibase_store_desc(PCIBaseState *s, uint32_t addr, struct de_desc *desc)
{
    struct de_desc d = *desc;
    d.opts1 = cpu_to_le32(d.opts1);
    d.opts2 = cpu_to_le32(d.opts2);
    d.addr1 = cpu_to_le32(d.addr1);
    d.addr2 = cpu_to_le32(d.addr2);
    pci_dma_write(&s->parent_obj, addr, &d, sizeof(d));
}

static void pcibase_transmit(PCIBaseState *s)
{
    struct de_desc desc;
    uint32_t current_addr = s->tx_desc_addr;
    int max_loop = DE_TX_RING_SIZE * 2; /* Safety break */

    while (max_loop-- > 0) {
        pcibase_fetch_desc(s, current_addr, &desc);

        /* If owned by host (0), stop */
        if (!(desc.opts1 & DESC_OWN)) {
            break;
        }

        /* Check for Setup Frame */
        if (desc.opts2 & DESC_SETUP) {
            /* Setup frame (multicast filter), just consume it */
            desc.opts1 &= ~DESC_OWN;
            pcibase_store_desc(s, current_addr, &desc);
        } else {
            /* Normal transmit */
            uint32_t len = desc.opts2 & 0x7FF;
            uint8_t buf[2048];

            if (len > sizeof(buf)) len = sizeof(buf);
            
            pci_dma_read(&s->parent_obj, desc.addr1, buf, len);
            qemu_send_packet(qemu_get_queue(s->nic), buf, len);

            desc.opts1 &= ~DESC_OWN;
            pcibase_store_desc(s, current_addr, &desc);
        }

        /* Update status */
        s->regs[CSR5_STATUS >> 3] |= S_TI;

        /* Advance ring */
        if (desc.opts2 & DESC_RER) {
            current_addr = s->regs[CSR4_TXRING >> 3];
        } else {
            current_addr += sizeof(desc);
        }
    }

    s->tx_desc_addr = current_addr;
    pcibase_update_irq(s);
}

static ssize_t pcibase_receive(NetClientState *nc, const uint8_t *buf, size_t size)
{
    PCIBaseState *s = qemu_get_nic_opaque(nc);
    struct de_desc desc;
    uint32_t current_addr = s->rx_desc_addr;

    if (!(s->regs[CSR6_MACMODE >> 3] & MODE_RXTX)) {
        return -1;
    }

    pcibase_fetch_desc(s, current_addr, &desc);

    /* If owned by host (0), we cannot receive (buffer unavailable) */
    if (!(desc.opts1 & DESC_OWN)) {
        s->regs[CSR5_STATUS >> 3] |= S_RU;
        pcibase_update_irq(s);
        return 0;
    }

    /* Check buffer size */
    uint32_t buf_size = desc.opts2 & 0x7FF;
    if (size > buf_size) {
        /* Truncate or drop? Driver logic implies we should fit. */
        /* For now, truncate to avoid overflow */
        size = buf_size;
    }

    /* Write packet to buffer */
    pci_dma_write(&s->parent_obj, desc.addr1, buf, size);

    /* Update descriptor */
    desc.opts1 &= ~DESC_OWN;
    desc.opts1 |= DESC_FIRST | DESC_LAST;
    /* Length is stored in status (opts1) bits 16-30 for Rx? 
     * Driver: len = ((status >> 16) & 0x7fff) - 4;
     * So we put (len + 4) << 16.
     */
    desc.opts1 |= ((size + 4) << 16);

    pcibase_store_desc(s, current_addr, &desc);

    /* Update status */
    s->regs[CSR5_STATUS >> 3] |= S_RI;

    /* Advance ring */
    if (desc.opts2 & DESC_RER) {
        s->rx_desc_addr = s->regs[CSR3_RXRING >> 3];
    } else {
        s->rx_desc_addr += sizeof(desc);
    }

    pcibase_update_irq(s);
    return size;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;
    int idx = addr >> 3;

    if (idx < DE_NUM_REGS) {
        val = s->regs[idx];
    }

    switch (addr) {
    case CSR5_STATUS:
        /* Status read returns current value (W1C handled in write) */
        break;
    case CSR9_ROMCMD:
        /* Emulate 21040 MAC address read sequence */
        if (s->rom_idx < sizeof(s->rom_buf)) {
            val = s->rom_buf[s->rom_idx++];
        } else {
            val = 0;
        }
        break;
    case CSR12_SIASTATUS:
        /* Fake Link Pass to satisfy driver check */
        val = 0; /* NetCxnErr (bit 2) = 0 means connected */
        /* Driver: carrier = (status & NetCxnErr) ? 0 : 1; */
        /* Also LinkFailStatus (bit 1?) */
        /* Let's return 0 which usually implies OK in SIA world for 10BaseT */
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int idx = addr >> 3;
    uint32_t v32 = (uint32_t)val;

    if (idx >= DE_NUM_REGS) return;

    switch (addr) {
    case CSR0_BUSMODE:
        s->regs[idx] = v32;
        if (v32 & MODE_RESET) {
            /* Soft Reset */
            memset(s->regs, 0, sizeof(s->regs));
            s->rom_idx = 0;
            s->regs[CSR0_BUSMODE >> 3] = 0xFE000000; /* Default bus mode? */
        }
        break;

    case CSR1_TXPOLL:
        s->regs[idx] = v32;
        if (v32 == 1) {
            pcibase_transmit(s);
        }
        break;

    case CSR2_RXPOLL:
        s->regs[idx] = v32;
        /* Rx Poll demand - check for descriptors */
        qemu_flush_queued_packets(qemu_get_queue(s->nic));
        break;

    case CSR3_RXRING:
        s->regs[idx] = v32;
        s->rx_desc_addr = v32 & ~3;
        break;

    case CSR4_TXRING:
        s->regs[idx] = v32;
        s->tx_desc_addr = v32 & ~3;
        break;

    case CSR5_STATUS:
        /* W1C (Write 1 to Clear) */
        s->regs[idx] &= ~v32;
        pcibase_update_irq(s);
        break;

    case CSR6_MACMODE:
        s->regs[idx] = v32;
        if (v32 & MODE_RXTX) {
            qemu_flush_queued_packets(qemu_get_queue(s->nic));
        }
        break;

    case CSR7_INTRMASK:
        s->regs[idx] = v32;
        pcibase_update_irq(s);
        break;

    case CSR9_ROMCMD:
        /* Write resets the ROM pointer */
        s->rom_idx = 0;
        break;

    default:
        s->regs[idx] = v32;
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_register_bar(PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE)
        return;

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(&s->parent_obj, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    
    memset(s->regs, 0, sizeof(s->regs));
    s->rom_idx = 0;
    s->tx_desc_addr = 0;
    s->rx_desc_addr = 0;
    
    /* Populate ROM buffer with MAC address */
    memset(s->rom_buf, 0, sizeof(s->rom_buf));
    memcpy(s->rom_buf, s->conf.macaddr.a, 6);
}

static NetClientInfo net_pcibase_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .receive = pcibase_receive,
};

static void pcibase_realize(PCIBaseState *s, Error **errp)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR 1: MMIO */
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = DE_REGS_SIZE;
    s->bar_info[1].name = "de2104x.mmio";
    
    pcibase_register_bar(s, &s->bar_info[1], errp);

    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&net_pcibase_info, &s->conf,
                          object_get_typename(OBJECT(s)), pdev->qdev.id,
                          &pdev->qdev.mem_reentrancy_guard, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);
}

static void pcibase_pci_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    pcibase_realize(s, errp);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    qemu_del_nic(s->nic);
}

static Property pcibase_properties[] = {
    DEFINE_NIC_PROPERTIES(PCIBaseState, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_pci_realize;
    k->exit    = pcibase_uninit;
    k->vendor_id = VENDOR_ID;
    k->device_id = DEVICE_ID;
    k->class_id = CLASS_ID;
    dc->reset  = pcibase_reset;
    device_class_set_props(dc, pcibase_properties);
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static void pcibase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };

    static const TypeInfo pcibase_info = {
        .name = TYPE_PCIBASE_DEVICE,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseState),
        .class_init = pcibase_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types)
