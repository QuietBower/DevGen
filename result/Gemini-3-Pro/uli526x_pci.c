/*
 * Generic QEMU PCI device template (QEMU 8.2.x)
 *
 * Phase 1: fill register macros, BAR list, BAR sizes, structs, enums.
 * Phase 2: implement MMIO/PIO read/write, DMA init, IRQ init, reset, config hooks
 * Phase 3 (Repair Phase)：correct syntax, types, includes, missing symbols, remove dead code
 * Phase 4 (Debug & Update Phase)：Based on actual kernel logs from QEMU boot, repair the virtual hardware behavior
 *
 * Replace #PLACEHOLDER# blocks with device-specific code/data.
 * NOTE:
 * - Only essential fixes added (TYPE macro, PIO BAR handling, MMIO impl, DMA init, IRQ).
 * - No optional or advanced features added.
 * - Do not directly fill this file with the Linux kernel source code
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
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

/* ------------------------------------------------------------------ */
/* Additional include files (Stage1 optional)                          */
/* ------------------------------------------------------------------ */
#include "net/net.h"

#define TYPE_PCIBASE_DEVICE "uli526x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver (Stage1) */
#define DRV_NAME	"uli526x"
#define RX_DESC_CNT     0x30
#define TX_DESC_CNT     0x20
#define MAX_PACKET_SIZE 1514
#define ULI526X_IO_SIZE 0x100

enum uli526x_offsets {
	DCR0 = 0x00, DCR1 = 0x08, DCR2 = 0x10, DCR3 = 0x18, DCR4 = 0x20,
	DCR5 = 0x28, DCR6 = 0x30, DCR7 = 0x38, DCR8 = 0x40, DCR9 = 0x48,
	DCR10 = 0x50, DCR11 = 0x58, DCR12 = 0x60, DCR13 = 0x68, DCR14 = 0x70,
	DCR15 = 0x78
};

/* ------------------------------------------------------------------ */
/* BAR metadata definition                                             */
/* ------------------------------------------------------------------ */
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

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions (MMIO/PIO unified handling fix) */
    MemoryRegion bar_regions[6];

    /* optional linear backing */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt state feature flags */
    bool has_msi;
    bool has_msix;
    
    NICState *nic;
    NICConf conf;

    /* DMA info placeholder */
    struct tx_desc {
        uint32_t tdes0, tdes1, tdes2, tdes3;
    } tx_desc_template;

    struct rx_desc {
        uint32_t rdes0, rdes1, rdes2, rdes3;
    } rx_desc_template;

    /* Register shadows */
    uint32_t cr0_data;
    uint32_t cr1_data;
    uint32_t cr3_rx_base;
    uint32_t cr4_tx_base;
    uint32_t cr5_data;
    uint32_t cr6_data;
    uint32_t cr7_data;
    uint32_t cr9_data;
    uint32_t cr10_data;
    uint32_t cr13_data;
    uint32_t cr14_data;
    uint32_t cr15_data;
    
    uint16_t phy_regs[32];
    uint8_t mac_read_index;
};



/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* ------------------------------------------------------------------ */
/* MemoryRegionOps                                                      */
/* ------------------------------------------------------------------ */
static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* Helper: register a BAR (MMIO or PIO)                                 */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE)
        return;

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    }else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }

}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers (device-specific code goes into placeholders)   */
/* ------------------------------------------------------------------ */

static void uli526x_update_irq(PCIBaseState *s)
{
    /* Update NIS (Normal Interrupt Summary, Bit 16) based on TI(0) and RI(6) */
    if (s->cr5_data & 0x41) {
        s->cr5_data |= 0x10000;
    } else {
        s->cr5_data &= ~0x10000;
    }

    /* CR7 is Interrupt Mask, CR5 is Status */
    if (s->cr5_data & s->cr7_data) {
        pci_set_irq(&s->parent_obj, 1);
    } else {
        pci_set_irq(&s->parent_obj, 0);
    }
}

static uint16_t uli526x_phy_read(PCIBaseState *s, uint8_t phy_addr, uint8_t reg)
{
    if (phy_addr != 1) return 0xFFFF;
    if (reg > 31) return 0;
    return s->phy_regs[reg];
}

static void uli526x_phy_write(PCIBaseState *s, uint8_t phy_addr, uint8_t reg, uint16_t val)
{
    if (phy_addr != 1) return;
    if (reg > 31) return;
    
    if (reg == 0) {
        if (val & 0x8000) {
            s->phy_regs[0] = 0x3100;
            s->phy_regs[1] = 0x782D;
        } else {
            s->phy_regs[0] = val;
        }
    } else {
        s->phy_regs[reg] = val;
    }
}

static void uli526x_process_tx(PCIBaseState *s)
{
    uint32_t desc_addr = s->cr4_tx_base;
    struct tx_desc desc;
    uint8_t buf[2048];
    int max_loops = 64;

    while (max_loops--) {
        pci_dma_read(&s->parent_obj, desc_addr, &desc, sizeof(desc));
        
        if (!(desc.tdes0 & 0x80000000)) {
            break;
        }

        uint32_t len = desc.tdes1 & 0x7FF;
        if (len > sizeof(buf)) len = sizeof(buf);
        
        pci_dma_read(&s->parent_obj, desc.tdes2, buf, len);
        qemu_send_packet(qemu_get_queue(s->nic), buf, len);
        
        /* Clear OWN bit (31) to indicate host ownership. Do not set error bits. */
        desc.tdes0 &= ~0x80000000;
        
        pci_dma_write(&s->parent_obj, desc_addr, &desc, sizeof(desc));
        
        desc_addr = desc.tdes3;
        s->cr4_tx_base = desc_addr;
        
        s->cr5_data |= 0x01; /* TI - Transmit Interrupt */
    }
    uli526x_update_irq(s);
}

static ssize_t uli526x_receive(NetClientState *nc, const uint8_t *buf, size_t size)
{
    PCIBaseState *s = qemu_get_nic_opaque(nc);
    uint32_t desc_addr = s->cr3_rx_base;
    struct rx_desc desc;
    
    if (!(s->cr6_data & 0x2)) {
        return -1;
    }

    pci_dma_read(&s->parent_obj, desc_addr, &desc, sizeof(desc));
    
    if (!(desc.rdes0 & 0x80000000)) {
        return 0;
    }
    
    uint32_t buf_len = size;
    if (buf_len > 1514) buf_len = 1514;
    
    pci_dma_write(&s->parent_obj, desc.rdes2, buf, buf_len);
    
    desc.rdes0 &= ~0x80000000;
    desc.rdes0 |= ((buf_len + 4) << 16);
    desc.rdes0 |= 0x300;
    
    pci_dma_write(&s->parent_obj, desc_addr, &desc, sizeof(desc));
    
    s->cr3_rx_base = desc.rdes3;
    
    s->cr5_data |= 0x40; /* RI - Receive Interrupt */
    uli526x_update_irq(s);
    
    return size;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case DCR0:
        val = s->cr0_data;
        break;
    case DCR1:
        val = s->cr1_data;
        break;
    case DCR3:
        val = s->cr3_rx_base;
        break;
    case DCR4:
        val = s->cr4_tx_base;
        break;
    case DCR5:
        val = s->cr5_data;
        break;
    case DCR6:
        val = s->cr6_data;
        break;
    case DCR7:
        val = s->cr7_data;
        break;
    case DCR9:
        val = 0; /* Force SROM failure to trigger ID table fallback */
        break;
    case DCR10:
        val = s->cr10_data;
        break;
    case DCR13:
        val = s->cr13_data;
        break;
    case DCR14:
        if (s->mac_read_index < 6) {
            val = s->conf.macaddr.a[s->mac_read_index++];
        } else {
            val = 0;
        }
        break;
    case DCR15:
        val = s->cr15_data;
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case DCR0:
        /* Bit 0 is Software Reset, self-clearing */
        s->cr0_data = val & ~1;
        if (val & 1) {
            s->cr5_data = 0;
            s->cr6_data = 0;
            s->cr7_data = 0;
            s->mac_read_index = 0;
        }
        break;
    case DCR1:
        s->cr1_data = val;
        if (val & 1) {
            uli526x_process_tx(s);
        }
        break;
    case DCR3:
        s->cr3_rx_base = val;
        break;
    case DCR4:
        s->cr4_tx_base = val;
        break;
    case DCR5:
        s->cr5_data &= ~val;
        uli526x_update_irq(s);
        break;
    case DCR6:
        s->cr6_data = val;
        if (val & 0x2) {
            qemu_flush_queued_packets(qemu_get_queue(s->nic));
        }
        break;
    case DCR7:
        s->cr7_data = val;
        uli526x_update_irq(s);
        break;
    case DCR9:
        break;
    case DCR10:
        {
            uint32_t phy_addr = (val >> 21) & 0x1F;
            uint32_t reg = (val >> 16) & 0x1F;
            uint32_t op = (val >> 26) & 0x3;
            
            if (op == 2) {
                uint16_t data = uli526x_phy_read(s, phy_addr, reg);
                s->cr10_data = val | 0x10000000 | (data & 0xFFFF);
            } else if (op == 1) {
                uli526x_phy_write(s, phy_addr, reg, val & 0xFFFF);
                s->cr10_data = val | 0x10000000;
            }
        }
        break;
    case DCR13:
        s->cr13_data = val;
        if (val == 0x10) {
             s->mac_read_index = 0;
        }
        break;
    case DCR14:
        if (val == 0x10) s->mac_read_index = 0;
        break;
    case DCR15:
        s->cr15_data = val;
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Reset       */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    /* core resets */
    pci_device_reset(pdev);

    s->cr0_data = 0;
    s->cr1_data = 0;
    s->cr5_data = 0;
    s->cr6_data = 0;
    s->cr7_data = 0;
    s->mac_read_index = 0;
    
    /* PHY Reset Defaults */
    s->phy_regs[0] = 0x3100;
    s->phy_regs[1] = 0x782D;
    s->phy_regs[2] = 0x0040;
    s->phy_regs[3] = 0x6120;
    s->phy_regs[4] = 0x01E1;
    s->phy_regs[5] = 0x45E1;

    if (s->mmio_backing && s->mmio_backing_size)
        memset(s->mmio_backing, 0, s->mmio_backing_size);
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    (void)s;
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    /* optional: override certain config offsets if needed */
    

    switch (len) {
        case 1: val &= 0xFF; break;
        case 2: val &= 0xFFFF; break;
        case 4: break;
        default: break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }
    pci_default_write_config(pdev, addr, val, len);
}

static NetClientInfo net_uli526x_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .receive = uli526x_receive,
};

/* ------------------------------------------------------------------ */
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x10B9 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x5263 ); /* Use 5263 to enable CR10 PHY access */
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "uli526x-io";

    /* register BARs */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Interrupt config (placeholders) */
    

    /* DMA init (called here so device can set DMA masks/etc) */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) return;

    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    /* Fix: qemu_new_nic expects MemReentrancyGuard* at arg 5 in this environment */
    s->nic = qemu_new_nic(&net_uli526x_info, &s->conf, object_get_typename(OBJECT(pdev)), pdev->qdev.id, NULL, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, NULL);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    if (s->nic) {
        qemu_del_nic(s->nic);
        s->nic = NULL;
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                      */
/* ------------------------------------------------------------------ */
static Property uli526x_properties[] = {
    DEFINE_NIC_PROPERTIES(PCIBaseState, conf),
    DEFINE_PROP_END_OF_LIST(),
};

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;
    device_class_set_props(dc, uli526x_properties);

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

type_init(pcibase_register_types);
