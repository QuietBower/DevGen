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
#include "qemu/bitops.h"
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

/* Additional include files retrieved from driver context */


#define TYPE_PCIBASE_DEVICE "ipu3_cio2_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define CIO2_PCI_ID 0x9d32
#define CIO2_PCI_BAR 0
#define CIO2_NUM_DMA_CHAN 20U
#define CIO2_MAX_LOPS 8

#define CIO2_REG_GPREG_BASE 0x1000
#define CIO2_REG_CSIRX_BASE 0x000
#define CIO2_REG_MIPIBE_BASE 0x100
#define CIO2_REG_IRQCTRL_BASE 0x300

#define CIO2_REG_CGC 0x1400
#define CIO2_REG_D0I3C 0x1408
#define CIO2_REG_INT_STS 0x1414
#define CIO2_REG_INT_EN 0x1420
#define CIO2_REG_INT_STS_EXT_OE 0x1418
#define CIO2_REG_INT_EN_EXT_OE 0x1424
#define CIO2_REG_PBM_ARB_CTRL 0x1460
#define CIO2_REG_PBM_WMCTRL1 0x1464
#define CIO2_REG_PBM_WMCTRL2 0x1468
#define CIO2_REG_PBM_FOPN_ABORT 0x1474
#define CIO2_REG_LTRCTRL 0x1480
#define CIO2_REG_LTRVAL23 0x1484
#define CIO2_REG_LTRVAL01 0x1488
#define CIO2_REG_INT_STS_EXT_IE 0x17e4
#define CIO2_REG_INT_EN_EXT_IE 0x17e8

#define CIO2_REG_CDMABA(n) (0x1500 + 0x10 * (n))
#define CIO2_REG_CDMARI(n) (0x1504 + 0x10 * (n))
#define CIO2_REG_CDMAC0(n) (0x1508 + 0x10 * (n))
#define CIO2_REG_CDMAC1(n) (0x150c + 0x10 * (n))

#define CIO2_REG_IRQCTRL_EDGE (CIO2_REG_IRQCTRL_BASE + 0x00)
#define CIO2_REG_IRQCTRL_MASK (CIO2_REG_IRQCTRL_BASE + 0x04)
#define CIO2_REG_IRQCTRL_STATUS (CIO2_REG_IRQCTRL_BASE + 0x08)
#define CIO2_REG_IRQCTRL_CLEAR (CIO2_REG_IRQCTRL_BASE + 0x0c)
#define CIO2_REG_IRQCTRL_ENABLE (CIO2_REG_IRQCTRL_BASE + 0x10)
#define CIO2_REG_IRQCTRL_LEVEL_NOT_PULSE (CIO2_REG_IRQCTRL_BASE + 0x14)

#define CIO2_CDMAC0_DMA_HALTED				BIT(31)
#define CIO2_CDMAC0_DMA_EN				BIT(30)
#define CIO2_DMA_CHAN					0U
#define CIO2_REG_PIPE_BASE(n)			((n) * 0x0400)
#define CIO2_NUM_PORTS					4U
#define CIO2_INT_IOC(dma)	(1U << ((dma) < 4U ? (dma) : ((dma) >> 1U) + 2U))
#define CIO2_INT_IOS_IOLN(dma)		(1U << (((dma) >> 1U) + 12U))
#define CIO2_INT_IOC_SHIFT				0
#define CIO2_INT_IOS_IOLN_SHIFT				12
#define CIO2_INT_EXT_OE_OES_SHIFT			24U
#define CIO2_INT_IOOE					BIT(23)
#define CIO2_INT_IOIE					BIT(22)
#define CIO2_INT_IOIRQ					BIT(24)
#define CIO2_INT_EXT_OE_DMAOE_MASK			0x7ffff
#define CIO2_INT_EXT_IE_IRQ(n)				(0x80 << (8U * (n)))
#define CIO2_CDMARI_FBPT_RP_SHIFT			0U
#define CIO2_CDMARI_FBPT_RP_MASK			0xff

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#define CIO2_MAX_BUFFERS			(PAGE_SIZE / 16 / CIO2_MAX_LOPS)
#define CIO2_LOP_ENTRIES			(PAGE_SIZE / sizeof(uint32_t))
#define CIO2_FBPT_CTRL_VALID		BIT(0)
#define CIO2_FBPT_CTRL_IOC		BIT(1)
#define CIO2_FBPT_CTRL_IOS		BIT(2)

struct cio2_fbpt_entry {
    union {
        struct {
            uint32_t ctrl;
            uint16_t cur_line_num;
            uint16_t frame_num;
            uint32_t first_page_offset;
        } first_entry;
        struct {
            uint32_t timestamp;
            uint32_t num_of_bytes;
            uint16_t last_page_available_bytes;
            uint16_t num_of_pages;
        } second_entry;
    };
    uint32_t lop_page_addr;
};

#define CIO2_FBPT_SIZE			(CIO2_MAX_BUFFERS * CIO2_MAX_LOPS * \
					 sizeof(struct cio2_fbpt_entry))

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
    uint32_t int_sts;
    uint32_t int_en;
    uint32_t int_sts_ext_oe;
    uint32_t int_en_ext_oe;
    uint32_t int_sts_ext_ie;
    uint32_t int_en_ext_ie;
    uint32_t irqctrl_edge;
    uint32_t irqctrl_mask;
    uint32_t irqctrl_status;
    uint32_t irqctrl_clear;
    uint32_t irqctrl_enable;
    uint32_t irqctrl_level_not_pulse;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t cgc;
    uint32_t pbm_arb_ctrl;
    uint32_t pbm_wmctrl1;
    uint32_t pbm_wmctrl2;
    uint32_t ltrctrl;
    uint32_t ltrval23;
    uint32_t ltrval01;

    /* DMA Context */
    uint32_t cdmaba[CIO2_NUM_DMA_CHAN];
    uint32_t cdmari[CIO2_NUM_DMA_CHAN];
    uint32_t cdmac0[CIO2_NUM_DMA_CHAN];
    uint32_t cdmac1[CIO2_NUM_DMA_CHAN];

    uint32_t d0i3c;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->int_sts & s->int_en) != 0;
    if (s->has_msi) {
        if (level) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, level);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case CIO2_REG_CGC:
        val = s->cgc;
        break;
    case CIO2_REG_D0I3C:
        val = s->d0i3c;
        break;
    case CIO2_REG_INT_STS:
        val = s->int_sts;
        break;
    case CIO2_REG_INT_EN:
        val = s->int_en;
        break;
    case CIO2_REG_INT_STS_EXT_OE:
        val = s->int_sts_ext_oe;
        break;
    case CIO2_REG_INT_EN_EXT_OE:
        val = s->int_en_ext_oe;
        break;
    case CIO2_REG_INT_STS_EXT_IE:
        val = s->int_sts_ext_ie;
        break;
    case CIO2_REG_INT_EN_EXT_IE:
        val = s->int_en_ext_ie;
        break;
    case CIO2_REG_PBM_ARB_CTRL:
        val = s->pbm_arb_ctrl;
        break;
    case CIO2_REG_PBM_WMCTRL1:
        val = s->pbm_wmctrl1;
        break;
    case CIO2_REG_PBM_WMCTRL2:
        val = s->pbm_wmctrl2;
        break;
    case CIO2_REG_LTRCTRL:
        val = s->ltrctrl;
        break;
    case CIO2_REG_LTRVAL23:
        val = s->ltrval23;
        break;
    case CIO2_REG_LTRVAL01:
        val = s->ltrval01;
        break;
    case CIO2_REG_IRQCTRL_EDGE:
        val = s->irqctrl_edge;
        break;
    case CIO2_REG_IRQCTRL_MASK:
        val = s->irqctrl_mask;
        break;
    case CIO2_REG_IRQCTRL_STATUS:
        val = s->irqctrl_status;
        break;
    case CIO2_REG_IRQCTRL_CLEAR:
        val = s->irqctrl_clear;
        break;
    case CIO2_REG_IRQCTRL_ENABLE:
        val = s->irqctrl_enable;
        break;
    case CIO2_REG_IRQCTRL_LEVEL_NOT_PULSE:
        val = s->irqctrl_level_not_pulse;
        break;
    default:
        if (addr >= CIO2_REG_CDMABA(0) && addr <= CIO2_REG_CDMAC1(CIO2_NUM_DMA_CHAN - 1)) {
            int n = (addr - CIO2_REG_CDMABA(0)) / 0x10;
            int reg = (addr - CIO2_REG_CDMABA(0)) % 0x10;
            if (n < CIO2_NUM_DMA_CHAN) {
                switch (reg) {
                case 0x00: val = s->cdmaba[n]; break;
                case 0x04: val = s->cdmari[n]; break;
                case 0x08: val = s->cdmac0[n]; break;
                case 0x0c: val = s->cdmac1[n]; break;
                }
            }
        }
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case CIO2_REG_CGC:
        s->cgc = val;
        break;
    case CIO2_REG_D0I3C:
        s->d0i3c = val;
        break;
    case CIO2_REG_INT_STS:
        s->int_sts &= ~val;
        pcibase_update_irq(s);
        break;
    case CIO2_REG_INT_EN:
        s->int_en = val;
        pcibase_update_irq(s);
        break;
    case CIO2_REG_INT_STS_EXT_OE:
        s->int_sts_ext_oe &= ~val;
        break;
    case CIO2_REG_INT_EN_EXT_OE:
        s->int_en_ext_oe = val;
        break;
    case CIO2_REG_INT_STS_EXT_IE:
        s->int_sts_ext_ie &= ~val;
        break;
    case CIO2_REG_INT_EN_EXT_IE:
        s->int_en_ext_ie = val;
        break;
    case CIO2_REG_PBM_ARB_CTRL:
        s->pbm_arb_ctrl = val;
        break;
    case CIO2_REG_PBM_WMCTRL1:
        s->pbm_wmctrl1 = val;
        break;
    case CIO2_REG_PBM_WMCTRL2:
        s->pbm_wmctrl2 = val;
        break;
    case CIO2_REG_LTRCTRL:
        s->ltrctrl = val;
        break;
    case CIO2_REG_LTRVAL23:
        s->ltrval23 = val;
        break;
    case CIO2_REG_LTRVAL01:
        s->ltrval01 = val;
        break;
    case CIO2_REG_IRQCTRL_EDGE:
        s->irqctrl_edge = val;
        break;
    case CIO2_REG_IRQCTRL_MASK:
        s->irqctrl_mask = val;
        break;
    case CIO2_REG_IRQCTRL_STATUS:
        s->irqctrl_status = val;
        break;
    case CIO2_REG_IRQCTRL_CLEAR:
        s->irqctrl_clear = val;
        s->irqctrl_status &= ~val;
        break;
    case CIO2_REG_IRQCTRL_ENABLE:
        s->irqctrl_enable = val;
        break;
    case CIO2_REG_IRQCTRL_LEVEL_NOT_PULSE:
        s->irqctrl_level_not_pulse = val;
        break;
    default:
        if (addr >= CIO2_REG_CDMABA(0) && addr <= CIO2_REG_CDMAC1(CIO2_NUM_DMA_CHAN - 1)) {
            int n = (addr - CIO2_REG_CDMABA(0)) / 0x10;
            int reg = (addr - CIO2_REG_CDMABA(0)) % 0x10;
            if (n < CIO2_NUM_DMA_CHAN) {
                switch (reg) {
                case 0x00: s->cdmaba[n] = val; break;
                case 0x04: s->cdmari[n] = val; break;
                case 0x08: 
                    s->cdmac0[n] = val; 
                    if (val & CIO2_CDMAC0_DMA_EN) {
                        s->cdmac0[n] &= ~CIO2_CDMAC0_DMA_HALTED;
                    } else {
                        s->cdmac0[n] |= CIO2_CDMAC0_DMA_HALTED;
                    }
                    break;
                case 0x0c: s->cdmac1[n] = val; break;
                }
            }
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

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

    s->cgc = 0;
    s->d0i3c = 0;
    s->int_sts = 0;
    s->int_en = 0;
    s->int_sts_ext_oe = 0;
    s->int_en_ext_oe = 0;
    s->int_sts_ext_ie = 0;
    s->int_en_ext_ie = 0;
    s->pbm_arb_ctrl = 0;
    s->pbm_wmctrl1 = 0;
    s->pbm_wmctrl2 = 0;
    s->ltrctrl = 0;
    s->ltrval23 = 0;
    s->ltrval01 = 0;
    s->irqctrl_edge = 0;
    s->irqctrl_mask = 0;
    s->irqctrl_status = 0;
    s->irqctrl_clear = 0;
    s->irqctrl_enable = 0;
    s->irqctrl_level_not_pulse = 0;

    for (int i = 0; i < CIO2_NUM_DMA_CHAN; i++) {
        s->cdmaba[i] = 0;
        s->cdmari[i] = 0;
        s->cdmac0[i] = CIO2_CDMAC0_DMA_HALTED;
        s->cdmac1[i] = 0;
    }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x9d32 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0480 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = CIO2_PCI_BAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x10000;
    s->bar_info[0].name = "cio2-mmio";  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
    s->has_msi = true;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ipu3_cio2_pci",
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
