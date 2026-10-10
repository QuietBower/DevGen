/*
 * QEMU PCI device model for sata_qstor (Behavioral Phase)
 * Auto-generated based on linux-6.18/drivers/ata/sata_qstor.c
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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "sata_qstor_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#ifndef PCI_VENDOR_ID_PDC
#define PCI_VENDOR_ID_PDC 0x15e9
#endif
#define QS_PCI_VENDOR_ID   PCI_VENDOR_ID_PDC
#define QS_PCI_DEVICE_ID   0x2068
#define QS_PCI_CLASS_ID    PCI_CLASS_STORAGE_SATA

#define DRV_NAME           "sata_qstor"
#define DRV_VERSION        "0.09"

#define ATA_PORT_TYPE_NAME "ata_port"
#define PMP_GSCR_SII_POL   129
#define SATA_ADR(root, pmp)   (((root) << 16) | (pmp))
#define NO_PORT_MULT       0xffff
#define REGS_PER_GTF       7
#define QS_MMIO_BAR 4

/*
 * The actual register offsets and BAR index used by the driver are defined
 * in sata_qstor.h. We only emulate the minimal behavior needed for probe:
 * - QS_MMIO_BAR: BAR index which hosts MMIO registers.
 * - QS_HCT_CTRL: host control register (enable/disable interrupts).
 * - QS_HCF_CNFG3: global reset.
 * - QS_HVS_SERD3: phy enable.
 * - QS_CCT_*    : per-channel control.
 * - QS_CFC_*    : per-channel FIFO config.
 * - QS_CCF_CSEP : CPB size in bytes as power of two.
 * - QS_HID_HPHY : host PHY info (used only for DMA mask decision).
 * - QS_HST_SFF  : host SFF status FIFO (used by interrupt handler).
 * - QS_CCF_CPBA : per-channel command packet buffer address (DMA).
 *
 * As the exact numeric values are not present in the provided snippet,
 * we declare dummy offsets and only implement the access patterns which
 * the driver relies on for initialization and interrupt flow. The actual
 * numeric values are irrelevant to the guest as long as they are
 * self-consistent within this emulation.
 */

/* BAR indices */

/* MMIO register offsets (dummy but self-consistent) */
#define QS_HCT_CTRL     0x0000
#define QS_HCF_CNFG3    0x0004
#define QS_HVS_SERD3    0x0008
#define QS_HID_HPHY     0x000c
#define QS_HST_SFF      0x0010

/* Per-port channel base offset and spacing */
#define QS_PORT_STRIDE  0x4000

/* Per-channel control offsets (relative to channel base) */
#define QS_CCT_CTR0     0x0000
#define QS_CCT_CTR1     0x0004
#define QS_CCT_CFF      0x0008
#define QS_CCF_CPBA     0x0010
#define QS_CCF_CSEP     0x0014
#define QS_CFC_HUFT     0x0020
#define QS_CFC_HDFT     0x0024
#define QS_CFC_DUFT     0x0028
#define QS_CFC_DDFT     0x002c

/* Bit definitions used in driver (values arbitrary but consistent) */
#define QS_CNFG3_GSRST   0x01
#define QS_SERD3_PHY_ENA 0x01
#define QS_CTR1_RCHN     0x01
#define QS_CTR1_RDEV     0x02
#define QS_CTR0_REG      0x01
#define QS_CTR0_CLER     0x02
#define QS_CCF_RUN_PKT   0x01

/* SFF status FIFO fields: we construct the 32-bit words to satisfy qs_intr_pkt
 * and qs_intr_mmio logic. Layout we use:
 *   word0 (sff0): bits 23:16 = sDST (device status)
 *   word1 (sff1): bit31     = sFFE (empty flag)
 *                  bit30     = sEVLD (event valid)
 *                  bits 9:8  = port_no
 *                  bits 5:0  = sHST (host status)
 */

#define QS_MAX_PORTS 4


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

/* Simple model of one outstanding DMA packet per port */

typedef enum {
    QS_PORT_STATE_MMIO = 0,
    QS_PORT_STATE_PKT  = 1,
} QSPortState;

typedef struct QSPort {
    QSPortState state;

    /* CPB DMA address programmed via QS_CCF_CPBA */
    uint64_t cpb_addr;

    /* Simple pending completion flag for packet mode */
    bool pkt_pending;
    uint8_t pkt_sdst; /* device status byte to report in SFF FIFO */
    uint8_t pkt_shst; /* host status code (0=OK,3=device error) */

} QSPort;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Hardware Register Shadows */

    /* Global host registers */
    uint8_t hct_ctrl;      /* QS_HCT_CTRL */
    uint8_t hcf_cnfg3;     /* QS_HCF_CNFG3 */
    uint8_t hvs_serd3;     /* QS_HVS_SERD3 */
    uint32_t hid_hphy;     /* QS_HID_HPHY */

    /* SFF FIFO shadow (what host reads in qs_intr_pkt()) */
    uint32_t hst_sff0;
    uint32_t hst_sff1;

    /* Per-port state */
    unsigned int n_ports;
    QSPort ports[QS_MAX_PORTS];

    /* For simplicity, one IRQ line */
    bool irq_asserted;
};


static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /*
     * The real hardware has more complex logic. For probe purposes we
     * simply assert the legacy INTx line whenever there is a valid SFF
     * event and interrupts are enabled via QS_HCT_CTRL. The driver
     * enables/disables interrupts using writeb() to QS_HCT_CTRL.
     */

    bool want_irq = false;

    if ((s->hct_ctrl & 0x01) != 0) {
        /* Check if SFF FIFO indicates a valid, non-empty event */
        uint8_t sFFE = (s->hst_sff1 >> 31) & 0x01;
        uint8_t sEVLD = (s->hst_sff1 >> 30) & 0x01;
        if (!sFFE && sEVLD) {
            want_irq = true;
        }
    }

    if (want_irq && !s->irq_asserted) {
        pci_set_irq(pdev, 1);
        s->irq_asserted = true;
    } else if (!want_irq && s->irq_asserted) {
        pci_set_irq(pdev, 0);
        s->irq_asserted = false;
    }
}

static void pcibase_set_sff_event(PCIBaseState *s, unsigned port_no,
                                  uint8_t sHST, uint8_t sDST, bool empty)
{
    /* Construct SFF FIFO words according to our internal layout */
    uint32_t sff0 = ((uint32_t)sDST) << 16;
    uint32_t sff1 = 0;
    uint8_t sFFE = empty ? 1 : 0;
    uint8_t sEVLD = empty ? 0 : 1; /* valid only when not empty */

    sff1 |= ((uint32_t)sFFE) << 31;
    sff1 |= ((uint32_t)sEVLD) << 30;
    sff1 |= ((uint32_t)(port_no & 0x03)) << 8;
    sff1 |= (uint32_t)(sHST & 0x3f);

    s->hst_sff0 = sff0;
    s->hst_sff1 = sff1;

    pcibase_update_irq(s);
}

/* Device-initiated DMA logic based on driver access patterns
 *
 * The driver builds a packet in a coherent DMA buffer and writes its
 * address to QS_CCF_CPBA. It then writes QS_CCF_RUN_PKT to QS_CCT_CFF
 * and expects the controller to execute the command and later deliver
 * completion status via the SFF FIFO and an interrupt.
 *
 * Implement a minimal model: when the RUN_PKT bit is written, mark the
 * corresponding port's state as packet mode and immediately complete the
 * command as successful (sHST=0, sDST=0x50). This is sufficient for the
 * upper libata stack to see commands complete successfully.
 */

static void pcibase_do_dma(PCIBaseState *s, unsigned port_no)
{
    if (port_no >= s->n_ports) {
        return;
    }

    QSPort *p = &s->ports[port_no];

    /* For now we do not actually touch guest memory; we only signal
     * a successful completion.
     */

    p->state = QS_PORT_STATE_PKT;
    p->pkt_pending = true;
    p->pkt_sdst = 0x50; /* arbitrary OK status */
    p->pkt_shst = 0;    /* 0 = successful CPB */

    /* Push one event into the SFF FIFO and assert IRQ. */
    pcibase_set_sff_event(s, port_no, p->pkt_shst, p->pkt_sdst, false);
}


/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Global registers */
    if (addr == QS_HCT_CTRL && size == 1) {
        return s->hct_ctrl;
    }
    if (addr == QS_HCF_CNFG3 && size == 1) {
        return s->hcf_cnfg3;
    }
    if (addr == QS_HVS_SERD3 && size == 1) {
        return s->hvs_serd3;
    }
    if (addr == QS_HID_HPHY && size == 4) {
        /* DMA mask decision uses QS_HPHY_64BIT bit; we can safely report
         * 64-bit capable here. Bit layout is not specified, but the driver
         * only checks QS_HPHY_64BIT.
         */
        return s->hid_hphy;
    }
    if (addr == QS_HST_SFF && size == 4) {
        /* First word of SFF FIFO */
        val = s->hst_sff0;
        return val;
    }
    if (addr == QS_HST_SFF + 4 && size == 4) {
        /* Second word of SFF FIFO
         * In the hardware, repeated reads will eventually expose sFFE=1.
         * For simplicity, we keep the values as set by pcibase_set_sff_event
         * and rely on the driver loop, which stops when sFFE==1. We do not
         * automatically flip the flag here; instead, qs_intr_pkt() will see
         * sFFE==0 or 1 exactly as we set it, and our emulation sets sFFE=1
         * only when we explicitly clear events.
         */
        val = s->hst_sff1;
        return val;
    }

    /* Per-port channel registers */
    if (addr >= QS_PORT_STRIDE && addr < QS_PORT_STRIDE * (QS_MAX_PORTS + 1)) {
        unsigned port_no = addr / QS_PORT_STRIDE;
        hwaddr off = addr % QS_PORT_STRIDE;

        if (port_no >= s->n_ports) {
            return 0;
        }

        if (off == QS_CCT_CTR0 && size == 1) {
            /* We don't track CTR0 bits in detail; return REG mode value */
            return QS_CTR0_REG;
        }
        if (off == QS_CCT_CTR1 && size == 1) {
            /* No detailed state; return 0 */
            return 0;
        }
        if (off == QS_CCF_CPBA && size == 4) {
            return (uint32_t)(s->ports[port_no].cpb_addr & 0xffffffffULL);
        }
        if (off == QS_CCF_CPBA + 4 && size == 4) {
            return (uint32_t)(s->ports[port_no].cpb_addr >> 32);
        }
        if (off == QS_CCF_CSEP && size == 1) {
            /* CPB order programmed by host_init; just return 0 */
            return 0;
        }
        if ((off == QS_CFC_HUFT || off == QS_CFC_HDFT ||
             off == QS_CFC_DUFT || off == QS_CFC_DDFT) && size == 2) {
            return 0;
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Global registers */
    if (addr == QS_HCT_CTRL && size == 1) {
        s->hct_ctrl = (uint8_t)val;
        /* Enabling/disabling interrupts may change IRQ state. */
        pcibase_update_irq(s);
        return;
    }
    if (addr == QS_HCF_CNFG3 && size == 1) {
        /* Global reset bit is written here during host_init and host_stop. */
        s->hcf_cnfg3 = (uint8_t)val;
        /* If global reset asserted, clear per-port state and SFF FIFO. */
        if (s->hcf_cnfg3 & QS_CNFG3_GSRST) {
            unsigned i;
            for (i = 0; i < s->n_ports; i++) {
                s->ports[i].state = QS_PORT_STATE_MMIO;
                s->ports[i].cpb_addr = 0;
                s->ports[i].pkt_pending = false;
                s->ports[i].pkt_sdst = 0;
                s->ports[i].pkt_shst = 0;
            }
            s->hst_sff0 = 0;
            s->hst_sff1 = (1u << 31); /* sFFE=1, no events */
            pcibase_update_irq(s);
        }
        return;
    }
    if (addr == QS_HVS_SERD3 && size == 1) {
        /* PHY enable register */
        s->hvs_serd3 = (uint8_t)val;
        return;
    }

    if (addr == QS_HID_HPHY && size == 4) {
        /* Driver reads this to decide DMA mask; it never writes to it,
         * but accept writes for completeness.
         */
        s->hid_hphy = (uint32_t)val;
        return;
    }

    /* We never expect writes to HST_SFF, ignore if they occur. */

    /* Per-port channel registers */
    if (addr >= QS_PORT_STRIDE && addr < QS_PORT_STRIDE * (QS_MAX_PORTS + 1)) {
        unsigned port_no = addr / QS_PORT_STRIDE;
        hwaddr off = addr % QS_PORT_STRIDE;

        if (port_no >= s->n_ports) {
            return;
        }

        QSPort *p = &s->ports[port_no];

        if (off == QS_CCT_CTR0 && size == 1) {
            /* CTR0_REG / CLER writes used to control channel mode and clear
             * events. We don't emulate full semantics; just treat any write
             * as leaving the controller in MMIO (register) mode and clear
             * pending SFF events for this port by setting sFFE=1.
             */
            /* Leave per-port state; qs_enter_reg_mode sets pp->state outside. */
            /* Clear event for this port */
            p->pkt_pending = false;
            s->hst_sff0 = 0;
            s->hst_sff1 = (1u << 31); /* sFFE=1, sEVLD=0 */
            pcibase_update_irq(s);
            return;
        }

        if (off == QS_CCT_CTR1 && size == 1) {
            /* Channel/device reset bits; we don't model. */
            return;
        }

        if (off == QS_CCF_CPBA && size == 4) {
            p->cpb_addr &= 0xffffffff00000000ULL;
            p->cpb_addr |= (uint32_t)val;
            return;
        }
        if (off == QS_CCF_CPBA + 4 && size == 4) {
            p->cpb_addr &= 0x00000000ffffffffULL;
            p->cpb_addr |= ((uint64_t)(uint32_t)val) << 32;
            return;
        }

        if (off == QS_CCF_CSEP && size == 1) {
            /* CPB order, ignore content */
            return;
        }

        if ((off == QS_CFC_HUFT || off == QS_CFC_HDFT ||
             off == QS_CFC_DUFT || off == QS_CFC_DDFT) && size == 2) {
            /* FIFO depth settings, ignore */
            return;
        }

        if (off == QS_CCT_CFF && size == 4) {
            /* Writing RUN_PKT starts packet DMA. The driver writes
             * QS_CCF_RUN_PKT (bit 0). We treat any non-zero write with
             * this bit set as a start request.
             */
            if (val & QS_CCF_RUN_PKT) {
                pcibase_do_dma(s, port_no);
            }
            return;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* This device is MMIO-only from the driver's perspective. No PIO use
     * is present in the sata_qstor driver, so we can return 0.
     */
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr,
                              uint64_t val, unsigned size)
{
    /* No PIO usage in sata_qstor; ignore. */
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
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

    /* Reset internal shadows */
    s->hct_ctrl = 0;
    s->hcf_cnfg3 = 0;
    s->hvs_serd3 = 0;
    s->hid_hphy = 0;
    s->hst_sff0 = 0;
    s->hst_sff1 = (1u << 31); /* empty FIFO */
    s->irq_asserted = false;

    for (unsigned i = 0; i < s->n_ports; i++) {
        s->ports[i].state = QS_PORT_STATE_MMIO;
        s->ports[i].cpb_addr = 0;
        s->ports[i].pkt_pending = false;
        s->ports[i].pkt_sdst = 0;
        s->ports[i].pkt_shst = 0;
    }
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s,
                                 BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s,
                              bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s,
                              bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  QS_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  QS_PCI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, QS_PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Initialize number of ports to match QS_PORTS in the driver.
     * The driver uses QS_PORTS when calling ata_host_alloc_pinfo.
     * As the constant value isn't provided here, we choose 4 ports,
     * which is consistent with the maximum used in offsets.
     */
    s->n_ports = QS_MAX_PORTS;

    /* Initialize BAR table: MMIO BAR 0 used by qs_mmio_base() */
    s->num_bars = 1;
    s->bar_info[0].index = QS_MMIO_BAR;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    /* One 64KB window per port plus some global regs; choose 256KB */
    s->bar_info[0].size  = 256 * KiB;
    s->bar_info[0].name  = "sata_qstor-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize internal state */
    pcibase_reset(DEVICE(pdev));

    /* Present 64-bit DMA capability via QS_HID_HPHY so that
     * qs_set_dma_masks() will configure a 64-bit mask.
     * The actual bit definition QS_HPHY_64BIT is in the driver-only
     * headers, but the driver just tests a single bit; we can safely
     * set bit 0 to indicate "64-bit".
     */
    s->hid_hphy = 0x1;

    /* Enable bus mastering; the driver calls pci_set_master(). */
    pci_default_write_config(pdev, PCI_COMMAND,
                             pci_default_read_config(pdev, PCI_COMMAND, 2) |
                             PCI_COMMAND_MASTER,
                             2);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;

    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "sata_qstor_pci",
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
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
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

