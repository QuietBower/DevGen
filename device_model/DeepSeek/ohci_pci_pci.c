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
#include "hw/usb/hcd-ohci.h"

#define TYPE_PCIBASE_DEVICE "ohci_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x106b
#define DEVICE_ID 0x003f
#define CLASS_ID  0x0c0310

/* OHCI Register Offsets (from struct ohci_regs) */
#define REG_REVISION         0x00
#define REG_CONTROL          0x04
#define REG_CMDSTATUS        0x08
#define REG_INTRSTATUS       0x0c
#define REG_INTRENABLE       0x10
#define REG_INTRDISABLE      0x14
#define REG_HCCA             0x18
#define REG_ED_PERIODCUR     0x1c
#define REG_ED_CONTROLHEAD   0x20
#define REG_ED_CONTROLCUR    0x24
#define REG_ED_BULKHEAD      0x28
#define REG_ED_BULKCUR       0x2c
#define REG_DONEHEAD         0x30
#define REG_FMINTERVAL       0x34
#define REG_FMREMAINING      0x38
#define REG_FMNUMBER         0x3c
#define REG_PERIODICSTART    0x40
#define REG_LSTHRESH         0x44
#define REG_ROOTHUB_A        0x48
#define REG_ROOTHUB_B        0x4c
#define REG_ROOTHUB_STATUS   0x50
#define REG_PORTSTATUS(i)    (0x54 + (i) * 4)
#define MAX_ROOT_PORTS       15

/* OHCI Register Bit Definitions */
#define OHCI_HCR             (1 << 0)  /* HostControllerReset */
#define OHCI_CTRL_HCFS_MASK  0xC0     /* HostControllerFunctionalState */
#define OHCI_CTRL_HCFS_USBOPERATIONAL (2 << 6)
#define OHCI_CTRL_HCFS_USBSUSPEND    (3 << 6)
#define OHCI_CTRL_HCFS_USBRESET      (0 << 6)

/* Interrupt bits */
#define OHCI_INTR_SO         (1 << 0)
#define OHCI_INTR_WDH        (1 << 1)
#define OHCI_INTR_SF         (1 << 2)
#define OHCI_INTR_RD         (1 << 3)
#define OHCI_INTR_UE         (1 << 4)
#define OHCI_INTR_FNO        (1 << 5)
#define OHCI_INTR_RHSC       (1 << 6)
#define OHCI_INTR_OC         (1 << 7)

/* Port status bits */
#define OHCI_PORT_CCS        (1 << 0)
#define OHCI_PORT_PES        (1 << 1)
#define OHCI_PORT_PSS        (1 << 2)
#define OHCI_PORT_OCC        (1 << 3)
#define OHCI_PORT_PRSC       (1 << 4)
#define OHCI_PORT_PPS        (1 << 8)
#define OHCI_PORT_LSDA       (1 << 9)
#define OHCI_PORT_CSC        (1 << 16)
#define OHCI_PORT_PESC       (1 << 17)
#define OHCI_PORT_PSSC       (1 << 18)
#define OHCI_PORT_OCIC       (1 << 19)

/* Rohub A */
#define OHCI_RH_A_NDP        0x000000FF
#define OHCI_RH_A_POTPGT     0xFF000000

#define DEFAULT_RH_A         0x01000200 /* NDP=2, POTPGT=1 (2ms) */
#define DEFAULT_FMINTERVAL   0x2EDF

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
    uint32_t intr_status;
    uint32_t intr_enable;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t revision;
    uint32_t control;
    uint32_t cmdstatus;
    uint32_t intrstatus;
    uint32_t intrenable;
    uint32_t intrdisable;
    uint32_t hcca;
    uint32_t ed_periodcurrent;
    uint32_t ed_controlhead;
    uint32_t ed_controlcurrent;
    uint32_t ed_bulkhead;
    uint32_t ed_bulkcurrent;
    uint32_t donehead;
    uint32_t fminterval;
    uint32_t fmremaining;
    uint32_t fmnumber;
    uint32_t periodicstart;
    uint32_t lsthresh;
    uint32_t roothub_a;
    uint32_t roothub_b;
    uint32_t roothub_status;
    uint32_t portstatus[MAX_ROOT_PORTS];

    /* DMA Context */
    dma_addr_t hcca_dma;

    /* Operational status flags */
    uint32_t rh_state;   /* OHCI_RH_HALTED, etc. */
    uint32_t flags;      /* quirk flags */

    /* State used to handle reset sequences */
    bool reset_active;

    /* Power management state (D0-D3) */
    uint8_t power_state;

    /* Frame timer */
    QEMUTimer frame_timer;
};

static void pcibase_update_irq(PCIBaseState *s);
static void pcibase_reset(DeviceState *dev);

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intrstatus & s->intrenable) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static void ohci_frame_timer(void *opaque)
{
    PCIBaseState *s = opaque;
    uint16_t frame_no;

    /* Only update if HCCA is set and controller is operational */
    if ((s->control & OHCI_CTRL_HCFS_MASK) != OHCI_CTRL_HCFS_USBOPERATIONAL) {
        return;
    }
    if (!s->hcca) {
        /* No HCCA assigned, skip */
        timer_mod(&s->frame_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
        return;
    }

    /* Read current frame number from HCCA (offset 0x80) */
    pci_dma_read(PCI_DEVICE(s), s->hcca_dma + 0x80, &frame_no, sizeof(frame_no));
    frame_no++;
    /* Write back */
    pci_dma_write(PCI_DEVICE(s), s->hcca_dma + 0x80, &frame_no, sizeof(frame_no));
    /* Update HcFmNumber shadow */
    s->fmnumber = frame_no;

    /* Optionally update DoneHead to 0 (no completed TDs) */
    uint32_t done = 0;
    pci_dma_write(PCI_DEVICE(s), s->hcca_dma + 0x84, &done, sizeof(done));

    /* Reschedule for next frame (1ms) */
    timer_mod(&s->frame_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
}

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        return 0;
    }

    switch (addr) {
    case REG_REVISION:
        val = s->revision;
        break;
    case REG_CONTROL:
        val = s->control;
        break;
    case REG_CMDSTATUS:
        val = s->cmdstatus;
        break;
    case REG_INTRSTATUS:
        val = s->intrstatus;
        break;
    case REG_INTRENABLE:
        val = s->intrenable;
        break;
    case REG_INTRDISABLE:
        val = s->intrdisable;
        break;
    case REG_HCCA:
        val = s->hcca;
        break;
    case REG_ED_PERIODCUR:
        val = s->ed_periodcurrent;
        break;
    case REG_ED_CONTROLHEAD:
        val = s->ed_controlhead;
        break;
    case REG_ED_CONTROLCUR:
        val = s->ed_controlcurrent;
        break;
    case REG_ED_BULKHEAD:
        val = s->ed_bulkhead;
        break;
    case REG_ED_BULKCUR:
        val = s->ed_bulkcurrent;
        break;
    case REG_DONEHEAD:
        val = s->donehead;
        break;
    case REG_FMINTERVAL:
        val = s->fminterval;
        break;
    case REG_FMREMAINING:
        /* Not implemented; return 0 */
        val = 0;
        break;
    case REG_FMNUMBER:
        val = s->fmnumber;
        break;
    case REG_PERIODICSTART:
        val = s->periodicstart;
        break;
    case REG_LSTHRESH:
        val = s->lsthresh;
        break;
    case REG_ROOTHUB_A:
        val = s->roothub_a;
        break;
    case REG_ROOTHUB_B:
        val = s->roothub_b;
        break;
    case REG_ROOTHUB_STATUS:
        val = s->roothub_status;
        break;
    default:
        if (addr >= REG_PORTSTATUS(0) && addr < REG_PORTSTATUS(0) + 4 * MAX_ROOT_PORTS) {
            int port = (addr - REG_PORTSTATUS(0)) / 4;
            if (port >= 0 && port < MAX_ROOT_PORTS) {
                val = s->portstatus[port];
            }
        }
        break;
    }

    return val;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case REG_CONTROL:
    {
        uint32_t old_hcfs = s->control & OHCI_CTRL_HCFS_MASK;
        s->control = val;
        uint32_t new_hcfs = s->control & OHCI_CTRL_HCFS_MASK;
        if (new_hcfs != old_hcfs) {
            if (new_hcfs == OHCI_CTRL_HCFS_USBOPERATIONAL) {
                /* Start frame timer */
                timer_mod(&s->frame_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
            } else {
                /* Stop timer */
                timer_del(&s->frame_timer);
            }
        }
        break;
    }
    case REG_CMDSTATUS:
        if (val & OHCI_HCR) {
            /* Trigger host controller reset */
            pcibase_reset(DEVICE(s));
            /* After reset, set HCFS to USBSUSPEND per OHCI spec */
            s->control = (s->control & ~OHCI_CTRL_HCFS_MASK) | OHCI_CTRL_HCFS_USBSUSPEND;
            /* Clear HCR (the reset routine sets cmdstatus to 0) */
        } else {
            s->cmdstatus = val;
        }
        break;
    case REG_INTRSTATUS:
        /* Write-1-to-clear */
        s->intrstatus &= ~val;
        pcibase_update_irq(s);
        break;
    case REG_INTRENABLE:
        s->intrenable = val;
        pcibase_update_irq(s);
        break;
    case REG_INTRDISABLE:
        s->intrenable &= ~val;
        pcibase_update_irq(s);
        break;
    case REG_HCCA:
        s->hcca = val;
        s->hcca_dma = val;
        break;
    case REG_ED_PERIODCUR:
        s->ed_periodcurrent = val;
        break;
    case REG_ED_CONTROLHEAD:
        s->ed_controlhead = val;
        break;
    case REG_ED_CONTROLCUR:
        s->ed_controlcurrent = val;
        break;
    case REG_ED_BULKHEAD:
        s->ed_bulkhead = val;
        break;
    case REG_ED_BULKCUR:
        s->ed_bulkcurrent = val;
        break;
    case REG_DONEHEAD:
        s->donehead = val;
        break;
    case REG_FMINTERVAL:
        s->fminterval = val;
        break;
    case REG_FMNUMBER:
        /* Normally read-only, but driver might attempt write; ignore */
        break;
    case REG_PERIODICSTART:
        s->periodicstart = val;
        break;
    case REG_LSTHRESH:
        s->lsthresh = val;
        break;
    case REG_ROOTHUB_A:
        /* Only allow writes to POTPGT; NDP is fixed */
        s->roothub_a = (val & OHCI_RH_A_POTPGT) | (s->roothub_a & OHCI_RH_A_NDP);
        break;
    case REG_ROOTHUB_B:
        s->roothub_b = val;
        break;
    case REG_ROOTHUB_STATUS:
        s->roothub_status = val;
        break;
    default:
        if (addr >= REG_PORTSTATUS(0) && addr < REG_PORTSTATUS(0) + 4 * MAX_ROOT_PORTS) {
            int port = (addr - REG_PORTSTATUS(0)) / 4;
            if (port >= 0 && port < MAX_ROOT_PORTS) {
                uint32_t old = s->portstatus[port];
                /* W1C for OCC and PRSC; W1S for PES, PSS, PPS */
                uint32_t clear_mask = OHCI_PORT_OCC | OHCI_PORT_PRSC;
                uint32_t set_mask = OHCI_PORT_PES | OHCI_PORT_PSS | OHCI_PORT_PPS;
                s->portstatus[port] = (old & ~(clear_mask & val)) | (val & set_mask);
            }
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    /* Not used for OHCI */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Not used for OHCI */
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

    /* Stop frame timer and reset registers to default OHCI state */
    timer_del(&s->frame_timer);

    s->revision = 0x10;
    s->control = 0;
    s->cmdstatus = 0;
    s->intrstatus = 0;
    s->intrenable = 0;
    s->intrdisable = 0;
    s->hcca = 0;
    s->hcca_dma = 0;
    s->ed_periodcurrent = 0;
    s->ed_controlhead = 0;
    s->ed_controlcurrent = 0;
    s->ed_bulkhead = 0;
    s->ed_bulkcurrent = 0;
    s->donehead = 0;
    s->fminterval = DEFAULT_FMINTERVAL;
    s->fmremaining = 0;
    s->fmnumber = 0;
    s->periodicstart = 0;
    s->lsthresh = 0x628;
    s->roothub_a = DEFAULT_RH_A;
    s->roothub_b = 0;
    s->roothub_status = 0;
    memset(s->portstatus, 0, sizeof(s->portstatus));
    s->rh_state = 0;
    s->flags = 0;
    s->reset_active = false;
    s->power_state = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_config_set_class(pci_conf, CLASS_ID);
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "ohci-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize frame timer */
    timer_init_ns(&s->frame_timer, QEMU_CLOCK_VIRTUAL, ohci_frame_timer, s);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    timer_del(&s->frame_timer);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ohci_pci_pci",
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
