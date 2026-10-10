/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
 * NOTE: Do not directly fill this file with the Linux kernel source code.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "txgbevf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#ifndef PCI_VENDOR_ID_WANGXUN
#define PCI_VENDOR_ID_WANGXUN 0x8088
#endif
#define TXGBEVF_DEV_ID_SP1000 0x1000

#define WX_VXSTATUS              0x4
#define WX_VXCTRL                0x8
#define WX_VXMRQC                0x78
#define WX_VXICR                 0x100
#define WX_VXIMS                 0x108
#define WX_VXIMC                 0x10C
#define WX_VX_PF_BME             0x4B8
#define WX_VXMAILBOX             0x600
#define WX_VXMBMEM               0x00C00
#define WX_CFG_PORT_ST           0x14404

#define WX_VXCTRL_RST            (1UL << 0)
#define WX_VF_BME_ENABLE         (1UL << 0)
#define WX_CFG_PORT_ST_LANID     (3UL << 8)
#define WX_VXMAILBOX_SIZE        15
#define WX_VF_RESET              0x01
#define WX_VT_MSGTYPE_ACK        (1UL << 31)
#define WX_VT_MSGTYPE_NACK       (1UL << 30)
#define WX_VT_MSGTYPE_CTS        (1UL << 29)
#define WX_VF_GET_QUEUES         0x09
#define WX_VF_TX_QUEUES          1
#define WX_VF_RX_QUEUES          2
#define WX_VF_TRANS_VLAN         3
#define WX_VF_DEF_QUEUE          4
#define WX_VF_GET_FW_VERSION     0x11

#define TXGBEVF_MAX_MSIX_VECTORS 2
#define WX_VF_MAX_RING_NUMS      8

#define TXGBEVF_MAX_TX_QUEUES                  4
#define TXGBEVF_MAX_RX_QUEUES                  4
#define TXGBEVF_DEFAULT_TXD                    128
#define TXGBEVF_DEFAULT_RXD                    128

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int     index;    /* BAR index 0-5 */
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t vxicr;
    uint32_t vxims;
    uint32_t vximc;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t vxstatus;
    uint32_t vxctrl;
    uint32_t vxmrqc;
    uint32_t vx_pf_bme;
    uint32_t vxmailbox;
    uint32_t vxmbmem[15];
    uint32_t vxitr[8];
    uint32_t vxivar[8];
    uint32_t vxivar_misc;
    uint32_t cfg_port_st;

    /* DMA Context */
    struct {
        uint32_t rdbal;
        uint32_t rdbah;
        uint32_t rdt;
        uint32_t rdh;
        uint32_t rxdctl;
    } rx_rings[WX_VF_MAX_RING_NUMS];

    struct {
        uint32_t tdbal;
        uint32_t tdbah;
        uint32_t tdt;
        uint32_t tdh;
        uint32_t txdctl;
        uint32_t txd_head_addrl;
        uint32_t txd_head_addrh;
    } tx_rings[WX_VF_MAX_RING_NUMS];

};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->vxicr & s->vxims) != 0;

    if (s->has_msix) {
        if (level) {
            msix_notify(pdev, 0);
        }
    } else if (s->has_msi) {
        if (level) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, level);
    }
}

static void pcibase_reset(DeviceState *dev);

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case WX_VXSTATUS:
        val = s->vxstatus;
        break;
    case WX_VXCTRL:
        val = s->vxctrl;
        break;
    case WX_VXMRQC:
        val = s->vxmrqc;
        break;
    case WX_VXICR:
        val = s->vxicr;
        s->vxicr = 0; /* Read to clear */
        pcibase_update_irq(s);
        break;
    case WX_VXIMS:
        val = s->vxims;
        break;
    case WX_VXIMC:
        val = s->vximc;
        break;
    case WX_VX_PF_BME:
        val = s->vx_pf_bme;
        break;
    case WX_VXMAILBOX:
        val = s->vxmailbox;
        break;
    case WX_CFG_PORT_ST:
        val = s->cfg_port_st;
        break;
    default:
        if (addr >= WX_VXMBMEM && addr < WX_VXMBMEM + sizeof(s->vxmbmem)) {
            val = s->vxmbmem[(addr - WX_VXMBMEM) / 4];
        }
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case WX_VXSTATUS:
        s->vxstatus = val;
        break;
    case WX_VXCTRL:
        s->vxctrl = val;
        if (val & WX_VXCTRL_RST) {
            pcibase_reset(DEVICE(s));
        }
        break;
    case WX_VXMRQC:
        s->vxmrqc = val;
        break;
    case WX_VXICR:
        s->vxicr &= ~val; /* Write 1 to clear */
        pcibase_update_irq(s);
        break;
    case WX_VXIMS:
        s->vxims |= val;
        pcibase_update_irq(s);
        break;
    case WX_VXIMC:
        s->vxims &= ~val;
        pcibase_update_irq(s);
        break;
    case WX_VX_PF_BME:
        s->vx_pf_bme = val;
        break;
    case WX_VXMAILBOX:
        s->vxmailbox = val;
        break;
    case WX_CFG_PORT_ST:
        s->cfg_port_st = val;
        break;
    default:
        if (addr >= WX_VXMBMEM && addr < WX_VXMBMEM + sizeof(s->vxmbmem)) {
            s->vxmbmem[(addr - WX_VXMBMEM) / 4] = val;
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->vxstatus = 0;
    s->vxctrl = 0;
    s->vxmrqc = 0;
    s->vxicr = 0;
    s->vxims = 0;
    s->vximc = 0;
    s->vx_pf_bme = 0;
    s->vxmailbox = 0;
    s->cfg_port_st = 0;
    memset(s->vxmbmem, 0, sizeof(s->vxmbmem));
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_WANGXUN );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  TXGBEVF_DEV_ID_SP1000 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    /* BAR 0 and 4 are requested by the driver as MEM */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_MMIO, 64 * KiB, "txgbevf-bar0"};
    s->bar_info[1] = (BARInfo){4, BAR_TYPE_MMIO, 16 * KiB, "txgbevf-bar4"};
    
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msix_init(pdev, TXGBEVF_MAX_MSIX_VECTORS, &s->bar_regions[0], 0, 0x2000, &s->bar_regions[0], 0, 0x3000, 0, errp) == 0) {
        s->has_msix = true;
    } else if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (s->has_msix) {
        msix_uninit(pdev, &s->bar_regions[0], &s->bar_regions[0]);
    }
    if (s->has_msi) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "txgbevf_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;
    dc->vmsd   = &vmstate_pcibase;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pcibase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_PCIE_DEVICE },
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
