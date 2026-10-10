/*
 * QEMU PCI device emulating a basic PC-style parallel port (parport_pc)
 *
 * This model is intentionally minimal: it exposes a legacy ISA-style
 * parallel port through PCI I/O BARs so that the Linux parport_pc driver
 * can successfully probe and bind.
 *
 * It implements basic data/control/status registers in PIO space and
 * a small subset of ECR/CONFIG-style registers accessed via the
 * "high" I/O base (base_hi) so that ECR/ECP feature probing does not
 * fail catastrophically.
 *
 * No real device-side behaviour or DMA is implemented; only simple
 * shadow registers and fixed status bits that are sufficient for the
 * driver's capability detection and normal non-DMA operation paths.
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

#define TYPE_PCIBASE_DEVICE "parport_pc_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Use first PCI ID from parport_pc_pci_tbl for VIA 686A */
#define PARPORT_PC_PCI_VENDOR_ID 0x1106
#define PARPORT_PC_PCI_DEVICE_ID 0x0686
#define PARPORT_PC_PCI_CLASS_ID  PCI_CLASS_OTHERS

#define PARPORT_MAX  16
#ifndef PCI_CLASS_OTHERS
#define PCI_CLASS_OTHERS 0xff
#endif

#define PARPORT_PC_MAX_PORTS PARPORT_MAX
#define ECR_SPP 0x00
#define ECR_PS2 0x01
#define ECR_PPF 0x02
#define ECR_ECP 0x03
#define ECR_EPP 0x04
#define ECR_VND 0x05
#define ECR_TST 0x06
#define ECR_CNF 0x07
#define ECR_MODE_MASK 0xe0
#define NR_SUPERIOS 3

/* Parallel port ISA register layout relative to base */
#define LPT_REG_DATA      0x0  /* Data register */
#define LPT_REG_STATUS    0x1  /* Status register */
#define LPT_REG_CONTROL   0x2  /* Control register */

/* ECP/ECR related registers relative to base_hi */
#define LPT_REG_ECR       0x2  /* ECR at base_hi + 2 (ECONTROL) */
#define LPT_REG_CONFIGA   0x0  /* CONFIGA at base_hi + 0 */
#define LPT_REG_CONFIGB   0x1  /* CONFIGB at base_hi + 1 */
#define LPT_REG_FIFO      0x0  /* FIFO share base_hi + 0 in TST mode */

/* BAR description */
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

    /* Parallel port shadow registers */
    uint8_t data_reg;      /* base + 0 */
    uint8_t status_reg;    /* base + 1 (read-only in HW, R/O here) */
    uint8_t control_reg;   /* base + 2 */

    /* ECP/ECR/EPP related registers on base_hi */
    uint8_t ecr_reg;       /* ECR (ECONTROL) */
    uint8_t configa_reg;   /* CONFIGA */
    uint8_t configb_reg;   /* CONFIGB */

    /* Simple FIFO buffer for ECP tests (size small, just enough so
     * that the driver's probing loops terminate deterministically). */
    uint8_t fifo[16];
    uint8_t fifo_level;    /* number of bytes currently in FIFO */
};

/* Internal helper for status-triggered signaling. Currently no real
 * interrupt generation is required for driver probe and basic use, so
 * this is a stub. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    (void)s;
}

/* No device-initiated DMA is implemented; driver will typically use
 * ISA DMA or PIO on real hardware. This model does not emulate DMA. */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

/* Helpers to map an address in the PIO region to either base or base_hi
 * portions. In real hardware base and base_hi are separate I/O ranges
 * (e.g., 0x378 and 0x778). We model them as a single BAR with size
 * 0x800, where 0x000-0x3ff represent base, and 0x400-0x7ff represent
 * base_hi. The driver will request_region for exact ranges, but QEMU
 * does not enforce sub-IO granularity per guest request.
 */

static bool pcibase_is_base_hi(hwaddr addr)
{
    return addr >= 0x400;
}

static hwaddr pcibase_base_offset(hwaddr addr)
{
    /* Map addr in [0,0x3ff] to base-relative 0-... */
    return addr & 0x3ff;
}

static hwaddr pcibase_basehi_offset(hwaddr addr)
{
    /* Map addr in [0x400,0x7ff] to base_hi-relative */
    return addr & 0x3ff;
}

static uint8_t pcibase_status_default(void)
{
    /* The driver expects reasonable status bits. We'll emulate a
     * printer that is always present and ready: BUSY=0, PAPEROUT=0,
     * SELECT=1, ERROR=0, ACK=0. Bits:
     *  bit7: nBUSY (inverted)
     *  bit6: ACK
     *  bit5: PAPEROUT
     *  bit4: SELECT
     *  bit3: ERROR (nFAULT)
     *  bit0: EPP timeout
     * Common "idle" value used by many devices is 0x80 or 0x90; we
     * just ensure BUSY=0 by setting nBUSY=1, and SELECT=1. */
    return 0x90; /* 1001 0000: nBUSY=1, SELECT=1 */
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    uint64_t val = 0;

    /* This device does not expose MMIO; all accesses are via PIO BAR.
     * Return 0xFF for undefined MMIO locations. */
    switch (addr) {
    default:
        switch (size) {
        case 1: val = 0xff; break;
        case 2: val = 0xffff; break;
        case 4: val = 0xffffffffU; break;
        default: val = 0; break;
        }
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
    /* No MMIO side effects. */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0xff;

    if (size != 1) {
        /* parport_pc only uses byte accesses */
        return 0xff;
    }

    if (!pcibase_is_base_hi(addr)) {
        hwaddr off = pcibase_base_offset(addr);
        switch (off) {
        case LPT_REG_DATA: /* Data */
            val = s->data_reg;
            break;
        case LPT_REG_STATUS: /* Status */
            /* Low bit is EPP timeout; we always keep it 0 (no timeout). */
            val = s->status_reg & ~0x01u;
            break;
        case LPT_REG_CONTROL: /* Control */
            val = s->control_reg;
            break;
        default:
            /* EPPDATA/EPPADDR/FIFO are accessed at base+3.. etc.
             * For simplicity, return 0xff for all other offsets. */
            val = 0xff;
            break;
        }
    } else {
        hwaddr off = pcibase_basehi_offset(addr);
        switch (off) {
        case LPT_REG_ECR: /* ECONTROL / ECR */
            val = s->ecr_reg;
            break;
        case LPT_REG_CONFIGA: /* CONFIGA */
            val = s->configa_reg;
            break;
        case LPT_REG_CONFIGB: /* CONFIGB */
            val = s->configb_reg;
            break;
        default:
            /* For FIFO reads during tests, driver uses FIFO(pb) which
             * is usually base_hi + 0. That is handled via CONFIGA
             * aliasing in test modes, but we keep it simple. */
            val = 0xff;
            break;
        }
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t v = (uint8_t)val;

    if (size != 1) {
        return;
    }

    if (!pcibase_is_base_hi(addr)) {
        hwaddr off = pcibase_base_offset(addr);
        switch (off) {
        case LPT_REG_DATA:
            s->data_reg = v;
            break;
        case LPT_REG_CONTROL:
            s->control_reg = v;
            break;
        case LPT_REG_STATUS:
            /* Some chips allow writing bit0 to clear EPP timeout.
             * We always keep timeout clear and ignore writes. */
            break;
        default:
            /* Other base offsets (EPP, FIFO etc.): ignore */
            break;
        }
    } else {
        hwaddr off = pcibase_basehi_offset(addr);
        switch (off) {
        case LPT_REG_ECR:
            /* ECR layout: bits 7:5 mode, bits 2/1 FIFO & serviceIntr.
             * We store value and manage FIFO depth heuristically so
             * that probing loops eventually see FIFO_FULL/EMPTY. */
            s->ecr_reg = v;

            /* Mode bits */
            switch ((v >> 5) & 0x7) {
            case ECR_SPP:
                /* Reset FIFO in SPP mode */
                s->fifo_level = 0;
                break;
            case ECR_TST:
                /* In test mode, FIFO writes/reads are used. We do not
                 * implement precise semantics, only keep level. */
                break;
            default:
                break;
            }
            break;
        case LPT_REG_CONFIGA:
            s->configa_reg = v;
            break;
        case LPT_REG_CONFIGB:
            s->configb_reg = v;
            break;
        default:
            /* Writes to FIFO (base_hi+0) when in test mode: we can
             * treat CONFIGA as FIFO alias for the probing loops.
             * If ECR_TST, increment fifo_level until buffer is full. */
            if (((s->ecr_reg >> 5) & 0x7) == ECR_TST) {
                if (s->fifo_level < sizeof(s->fifo)) {
                    s->fifo[s->fifo_level++] = v;
                }
                /* When FIFO is full, some chips set bit1 (full) */
                if (s->fifo_level >= sizeof(s->fifo)) {
                    s->ecr_reg |= 0x02; /* FIFO full */
                }
            }
            break;
        }
    }
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

    /* Initialize parallel port registers to power-on defaults similar
     * to a basic SPP/ECP-capable port. */
    s->data_reg = 0x00;
    s->control_reg = 0x0c;    /* nINIT=1, SELECT_IN=1 */
    s->status_reg = pcibase_status_default();

    /* ECR default used by driver in parport_pc_init_state: 0x34
     * which is PS2 mode with serviceIntr and nErrIntrEn bits set. */
    s->ecr_reg = 0x34;

    /* CONFIGA / CONFIGB defaults are largely used for informational
     * printing; we set sane IRQ level/pulse indication and no DMA. */
    s->configa_reg = 0x10; /* pword=1 (8-bit) and level IRQs default */
    s->configb_reg = 0x00; /* irq/dma fields zero -> none */

    s->fifo_level = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PARPORT_PC_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PARPORT_PC_PCI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PARPORT_PC_PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization */
    s->num_bars = 0;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }

    /* BAR0: I/O space for base and base_hi.
     * Size 0x800 so guest can map base (e.g., 0x378) and base_hi
     * (e.g., 0x778) within same BAR region. */
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_PIO;
    s->bar_info[0].size  = 0x800; /* 2 x 0x400 segments */
    s->bar_info[0].name  = "parport-io";
    s->num_bars = 1;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* We do not expose MSI/MSI-X as the legacy driver does not
     * require them, and interrupts are not strictly necessary for
     * successful probing and basic polled operation. */

    /* Initialize register contents */
    pcibase_reset(DEVICE(pdev));
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

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "parport_pc_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(data_reg, PCIBaseState),
        VMSTATE_UINT8(status_reg, PCIBaseState),
        VMSTATE_UINT8(control_reg, PCIBaseState),
        VMSTATE_UINT8(ecr_reg, PCIBaseState),
        VMSTATE_UINT8(configa_reg, PCIBaseState),
        VMSTATE_UINT8(configb_reg, PCIBaseState),
        VMSTATE_UINT8(fifo_level, PCIBaseState),
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

    /* Default IDs; these can also be overridden by machine config. */
    k->vendor_id = PARPORT_PC_PCI_VENDOR_ID;
    k->device_id = PARPORT_PC_PCI_DEVICE_ID;
    k->class_id  = PARPORT_PC_PCI_CLASS_ID;
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
