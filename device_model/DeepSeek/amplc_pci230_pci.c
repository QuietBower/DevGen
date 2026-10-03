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

/* Additional include files retrieved from driver context */
/* No additional includes */

#define TYPE_PCIBASE_DEVICE "amplc_pci230_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* Driver: amplc_pci230.c */
/* PCI Vendor/Device IDs */
#define PCI_VENDOR_ID_AMPLICON 0x14dc
#define PCI_DEVICE_ID_PCI230 0x0000
#define PCI_DEVICE_ID_PCI260 0x0006

/* Clock source defines */
#define CLK_EXT      7
#define CLK_CLK      0
#define CLK_10MHZ    1
#define CLK_1MHZ     2
#define CLK_100KHZ   3
#define CLK_10KHZ    4
#define CLK_1KHZ     5
#define CLK_OUTNM1   6

/* Gate source defines */
#define GAT_VCC      0
#define GAT_GND      1
#define GAT_EXT      2
#define GAT_NOUTNM2  3

/* Helper macro */
#define COMBINE(old, new, mask) (((old) & ~(mask)) | ((new) & (mask)))

/* Register offsets */
#define PCI230_PPI_X_BASE    0x00
#define PCI230_PPI_X_A       0x00
#define PCI230_PPI_X_B       0x01
#define PCI230_PPI_X_C       0x02
#define PCI230_PPI_X_CMD     0x03

#define PCI230_Z2_CT_BASE    0x14

#define PCI230_ZCLK_SCE      0x1A
#define PCI230_ZGAT_SCE      0x1D

#define PCI230_INT_SCE       0x1E
#define PCI230_INT_STAT      0x1E

/* DAC registers */
#define PCI230_DACCON        0x00
#define PCI230_DACOUT1       0x02
#define PCI230_DACOUT2       0x04

/* ADC registers */
#define PCI230_ADCDATA       0x08
#define PCI230_ADCSWTRIG     0x08
#define PCI230_ADCCON        0x0A
#define PCI230_ADCEN         0x0C
#define PCI230_ADCG          0x0E

/* PCI230+ ADC specific registers */
#define PCI230P_ADCTRIG      0x10
#define PCI230P_ADCTH        0x12
#define PCI230P_ADCFFTH      0x14
#define PCI230P_ADCFFLEV     0x16
#define PCI230P_ADCPTSC      0x18
#define PCI230P_ADCHYST      0x1A

#define PCI230P_EXTFUNC      0x1C
#define PCI230P_HWVER        0x1E

/* PCI230P2 DAC specific registers and bits */
#define PCI230P2_DACDATA     0x02
#define PCI230P2_DACSWTRIG   0x02
#define PCI230P2_DACEN       0x06

/* DAC control bits */
#define PCI230_DAC_OR(x)          (((x) & 0x1) << 0)
#define PCI230_DAC_OR_UNI         PCI230_DAC_OR(0)
#define PCI230_DAC_OR_BIP         PCI230_DAC_OR(1)
#define PCI230_DAC_OR_MASK        PCI230_DAC_OR(1)

#define PCI230P2_DAC_FIFO_EN      BIT(8)
#define PCI230P2_DAC_TRIG(x)      (((x) & 0x7) << 2)
#define PCI230P2_DAC_TRIG_NONE    PCI230P2_DAC_TRIG(0)
#define PCI230P2_DAC_TRIG_SW      PCI230P2_DAC_TRIG(1)
#define PCI230P2_DAC_TRIG_EXTP    PCI230P2_DAC_TRIG(2)
#define PCI230P2_DAC_TRIG_EXTN    PCI230P2_DAC_TRIG(3)
#define PCI230P2_DAC_TRIG_Z2CT0   PCI230P2_DAC_TRIG(4)
#define PCI230P2_DAC_TRIG_Z2CT1   PCI230P2_DAC_TRIG(5)
#define PCI230P2_DAC_TRIG_Z2CT2   PCI230P2_DAC_TRIG(6)
#define PCI230P2_DAC_TRIG_MASK    PCI230P2_DAC_TRIG(7)

#define PCI230P2_DAC_FIFO_WRAP    BIT(7)
#define PCI230P2_DAC_INT_FIFO(x)  (((x) & 7) << 9)
#define PCI230P2_DAC_INT_FIFO_EMPTY   PCI230P2_DAC_INT_FIFO(0)
#define PCI230P2_DAC_INT_FIFO_NEMPTY  PCI230P2_DAC_INT_FIFO(1)
#define PCI230P2_DAC_INT_FIFO_NHALF   PCI230P2_DAC_INT_FIFO(2)
#define PCI230P2_DAC_INT_FIFO_HALF    PCI230P2_DAC_INT_FIFO(3)
#define PCI230P2_DAC_INT_FIFO_NFULL   PCI230P2_DAC_INT_FIFO(4)
#define PCI230P2_DAC_INT_FIFO_FULL    PCI230P2_DAC_INT_FIFO(5)
#define PCI230P2_DAC_INT_FIFO_MASK    PCI230P2_DAC_INT_FIFO(7)

#define PCI230_DAC_BUSY           BIT(1)
#define PCI230P2_DAC_FIFO_UNDERRUN_LATCHED BIT(5)
#define PCI230P2_DAC_FIFO_EMPTY   BIT(13)
#define PCI230P2_DAC_FIFO_FULL    BIT(14)
#define PCI230P2_DAC_FIFO_HALF    BIT(15)
#define PCI230P2_DAC_FIFO_UNDERRUN_CLEAR BIT(5)
#define PCI230P2_DAC_FIFO_RESET   BIT(12)
#define PCI230P2_DAC_FIFOLEVEL_HALF 512
#define PCI230P2_DAC_FIFOLEVEL_FULL 1024
#define PCI230P2_DAC_FIFOROOM_EMPTY       PCI230P2_DAC_FIFOLEVEL_FULL
#define PCI230P2_DAC_FIFOROOM_ONETOHALF   (PCI230P2_DAC_FIFOLEVEL_FULL - PCI230P2_DAC_FIFOLEVEL_HALF)
#define PCI230P2_DAC_FIFOROOM_HALFTOFULL  1
#define PCI230P2_DAC_FIFOROOM_FULL        0

/* ADC control bits */
#define PCI230_ADC_TRIG(x)          (((x) & 0x7) << 0)
#define PCI230_ADC_TRIG_NONE        PCI230_ADC_TRIG(0)
#define PCI230_ADC_TRIG_SW          PCI230_ADC_TRIG(1)
#define PCI230_ADC_TRIG_EXTP        PCI230_ADC_TRIG(2)
#define PCI230_ADC_TRIG_EXTN        PCI230_ADC_TRIG(3)
#define PCI230_ADC_TRIG_Z2CT0       PCI230_ADC_TRIG(4)
#define PCI230_ADC_TRIG_Z2CT1       PCI230_ADC_TRIG(5)
#define PCI230_ADC_TRIG_Z2CT2       PCI230_ADC_TRIG(6)
#define PCI230_ADC_TRIG_MASK        PCI230_ADC_TRIG(7)

#define PCI230_ADC_IR(x)            (((x) & 0x1) << 3)
#define PCI230_ADC_IR_UNI           PCI230_ADC_IR(0)
#define PCI230_ADC_IR_BIP           PCI230_ADC_IR(1)
#define PCI230_ADC_IR_MASK          PCI230_ADC_IR(1)

#define PCI230_ADC_IM(x)            (((x) & 0x1) << 4)
#define PCI230_ADC_IM_SE            PCI230_ADC_IM(0)
#define PCI230_ADC_IM_DIF           PCI230_ADC_IM(1)
#define PCI230_ADC_IM_MASK          PCI230_ADC_IM(1)

#define PCI230_ADC_FIFO_EN          BIT(8)
#define PCI230_ADC_INT_FIFO(x)      (((x) & 0x7) << 9)
#define PCI230_ADC_INT_FIFO_EMPTY   PCI230_ADC_INT_FIFO(0)
#define PCI230_ADC_INT_FIFO_NEMPTY  PCI230_ADC_INT_FIFO(1)
#define PCI230_ADC_INT_FIFO_NHALF   PCI230_ADC_INT_FIFO(2)
#define PCI230_ADC_INT_FIFO_HALF    PCI230_ADC_INT_FIFO(3)
#define PCI230_ADC_INT_FIFO_NFULL   PCI230_ADC_INT_FIFO(4)
#define PCI230_ADC_INT_FIFO_FULL    PCI230_ADC_INT_FIFO(5)
#define PCI230P_ADC_INT_FIFO_THRESH PCI230_ADC_INT_FIFO(7)
#define PCI230_ADC_INT_FIFO_MASK    PCI230_ADC_INT_FIFO(7)

#define PCI230_ADC_FIFO_RESET       BIT(12)
#define PCI230_ADC_GLOB_RESET       BIT(13)
#define PCI230_ADC_BUSY             BIT(15)
#define PCI230_ADC_FIFO_EMPTY       BIT(12)
#define PCI230_ADC_FIFO_FULL        BIT(13)
#define PCI230_ADC_FIFO_HALF        BIT(14)
#define PCI230_ADC_FIFO_FULL_LATCHED BIT(5)

#define PCI230_ADC_FIFOLEVEL_HALFFULL 2049
#define PCI230_ADC_FIFOLEVEL_FULL    4096

/* Extended function bits */
#define PCI230P_EXTFUNC_GAT_EXTTRIG BIT(0)
#define PCI230P2_EXTFUNC_DACFIFO    BIT(1)

/* Interrupt source bits */
#define PCI230_INT_DISABLE   0
#define PCI230_INT_PPI_C0    BIT(0)
#define PCI230_INT_PPI_C3    BIT(1)
#define PCI230_INT_ADC       BIT(2)
#define PCI230_INT_ZCLK_CT1  BIT(5)
#define PCI230P2_INT_DAC     BIT(4)

/* Speed limits (not directly used in hardware strapping) */
#define MAX_SPEED_AO         8000
#define MIN_SPEED_AO         4294967295u
#define MAX_SPEED_AI_SE      3200
#define MAX_SPEED_AI_DIFF    8000
#define MAX_SPEED_AI_PLUS    4000
#define MIN_SPEED_AI         4294967295u

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int     index;
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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows */
    uint16_t adccon;
    uint16_t daccon;
    uint16_t adcg;
    uint8_t ier;          /* interrupt enable register (PCI230_INT_SCE) */
    uint16_t int_status;  /* interrupt status register (PCI230_INT_STAT) */
    
    /* Additional registers */
    uint16_t dacout[2];   /* Last written DAC output values */
    uint16_t dacen;       /* DAC channel enable (PCI230P2_DACEN) */
    uint16_t adcen;       /* ADC channel enable (PCI230_ADCEN) */
    uint16_t adcfifothresh; /* ADC FIFO threshold (PCI230P_ADCFFTH) */
    uint16_t extfunc;     /* Extended function register */
    uint16_t hwver;       /* Hardware version register (PCI230P_HWVER) */
    uint16_t adcfflev;    /* ADC FIFO level (PCI230P_ADCFFLEV) */
    uint8_t zclk[4];      /* Clock source config per counter */
    uint8_t zgat[4];      /* Gate source config per counter */
    uint8_t ppi_regs[4];  /* PPI registers (byte access) */
    uint8_t ct_regs[4];   /* 8254 registers at 0x14-0x17, simple shadow */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    pci_set_irq(pdev, (s->ier & s->int_status) ? 1 : 0);
}

/* BAR2 PIO handlers (iobase) */
static uint64_t pcibase_bar2_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint32_t offset = addr;

    if (size != 1) {
        return ~0ULL;
    }

    switch (offset) {
    case 0x14 ... 0x17: /* 8254 registers */
        val = s->ct_regs[offset - 0x14];
        break;
    case 0x1E: /* INT_STAT */
        val = s->int_status;
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_bar2_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t offset = addr;
    uint8_t byte_val = (uint8_t)val;

    if (size != 1) {
        return;
    }

    switch (offset) {
    case 0x14 ... 0x17: /* 8254 registers */
        s->ct_regs[offset - 0x14] = byte_val;
        break;
    case 0x1A: /* ZCLK_SCE */
        {
            unsigned int chan = (byte_val >> 3) & 3;
            unsigned int src = byte_val & 7;
            s->zclk[chan] = src;
        }
        break;
    case 0x1D: /* ZGAT_SCE */
        {
            unsigned int chan = (byte_val >> 3) & 3;
            unsigned int src = byte_val & 7;
            s->zgat[chan] = src;
        }
        break;
    case 0x1E: /* INT_SCE */
        s->ier = byte_val;
        pcibase_update_irq(s);
        break;
    default:
        /* ignore */
        break;
    }
}

/* BAR3 PIO handlers (daqio) */
static uint64_t pcibase_bar3_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = ~0ULL;
    uint32_t offset = addr;

    if (size == 1) {
        if (offset < 4) {
            val = s->ppi_regs[offset];
        } else {
            val = 0;
        }
        return val;
    } else if (size == 2) {
        if (offset & 1) {
            return ~0ULL;
        }
        switch (offset & 0x1E) {
        case 0x00: val = s->daccon; break;
        case 0x02: val = s->dacout[0]; break;
        case 0x04: val = s->dacout[1]; break;
        case 0x06: val = s->dacen; break;
        case 0x08: val = 0x0000; break; /* ADCDATA */
        case 0x0A: val = s->adccon; break;
        case 0x0C: val = s->adcen; break;
        case 0x0E: val = s->adcg; break;
        case 0x10: val = 0; break; /* ADCTRIG */
        case 0x12: val = 0; break; /* ADCTH */
        case 0x14: val = s->adcfifothresh; break;
        case 0x16: val = s->adcfflev; break;
        case 0x18: val = 0; break; /* ADCPTSC */
        case 0x1A: val = 0; break; /* ADCHYST */
        case 0x1C: val = s->extfunc; break;
        case 0x1E: val = s->hwver; break;
        default: val = 0; break;
        }
        return val;
    } else {
        return ~0ULL;
    }
}

static void pcibase_bar3_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t offset = addr;

    if (size == 1) {
        if (offset < 4) {
            s->ppi_regs[offset] = (uint8_t)val;
        }
        return;
    } else if (size == 2) {
        if (offset & 1) {
            return;
        }
        uint16_t wval = (uint16_t)val;
        switch (offset & 0x1E) {
        case 0x00: s->daccon = wval; break;
        case 0x02: s->dacout[0] = wval; break;
        case 0x04: s->dacout[1] = wval; break;
        case 0x06: s->dacen = wval; break;
        case 0x08: /* write to ADCSWTRIG, ignored */ break;
        case 0x0A: s->adccon = wval; break;
        case 0x0C: s->adcen = wval; break;
        case 0x0E: s->adcg = wval; break;
        case 0x10: break; /* ADCTRIG */
        case 0x12: break; /* ADCTH */
        case 0x14: s->adcfifothresh = wval; break;
        case 0x16: break; /* read-only */
        case 0x18: break; /* ADCPTSC */
        case 0x1A: break; /* ADCHYST */
        case 0x1C: s->extfunc = wval; break;
        case 0x1E: break; /* read-only */
        default: break;
        }
    }
}

static const MemoryRegionOps pcibase_bar2_pio_ops = {
    .read = pcibase_bar2_pio_read,
    .write = pcibase_bar2_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl = { .min_access_size = 1, .max_access_size = 1 },
};

static const MemoryRegionOps pcibase_bar3_pio_ops = {
    .read = pcibase_bar3_pio_read,
    .write = pcibase_bar3_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 2 },
    .impl = { .min_access_size = 1, .max_access_size = 2 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    
    s->adccon = 0x0000;
    s->daccon = 0x0000;
    s->adcg = 0x0000;
    s->ier = 0;
    s->int_status = 0;
    s->dacout[0] = s->dacout[1] = 0;
    s->dacen = 0;
    s->adcen = 0;
    s->adcfifothresh = 0;
    s->extfunc = 0;
    s->hwver = 2; /* Emulate PCI230+ version 2 */
    s->adcfflev = 0;
    memset(s->zclk, 0, sizeof(s->zclk));
    memset(s->zgat, 0, sizeof(s->zgat));
    memset(s->ppi_regs, 0, sizeof(s->ppi_regs));
    memset(s->ct_regs, 0, sizeof(s->ct_regs));
    
    pci_set_irq(PCI_DEVICE(dev), 0);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_AMPLICON);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_PCI230);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Register BAR2 and BAR3 as I/O ports */
    memory_region_init_io(&s->bar_regions[2], OBJECT(s), &pcibase_bar2_pio_ops, s, "bar2", 32);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[2]);
    memory_region_init_io(&s->bar_regions[3], OBJECT(s), &pcibase_bar3_pio_ops, s, "bar3", 32);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[3]);
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

static const VMStateDescription vmstate_pcibase = {
    .name = "amplc_pci230_pci",
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
