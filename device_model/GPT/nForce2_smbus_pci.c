/*
 * QEMU PCI device model for NVIDIA nForce2 SMBus controller
 * Implements minimal behavior required by linux/drivers/i2c/busses/i2c-nforce2.c
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
/* Removed linux/pci_ids.h as it is not available in QEMU build environment */

#define TYPE_PCIBASE_DEVICE "nForce2_smbus_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define NFORCE_PCI_SMB1                 0x50
#define NFORCE_PCI_SMB2                 0x54

#define NVIDIA_SMB_PRTCL_OFFSET         0x00
#define NVIDIA_SMB_STS_OFFSET           0x01
#define NVIDIA_SMB_ADDR_OFFSET          0x02
#define NVIDIA_SMB_CMD_OFFSET           0x03
#define NVIDIA_SMB_DATA_OFFSET          0x04
#define NVIDIA_SMB_BCNT_OFFSET          0x24
#define NVIDIA_SMB_STATUS_ABRT_OFFSET   0x3c
#define NVIDIA_SMB_CTRL_OFFSET          0x3e

#define NVIDIA_SMB_STATUS_ABRT_STS      0x01
#define NVIDIA_SMB_CTRL_ABORT           0x20
#define NVIDIA_SMB_STS_DONE             0x80
#define NVIDIA_SMB_STS_ALRM             0x40
#define NVIDIA_SMB_STS_RES              0x20
#define NVIDIA_SMB_STS_STATUS           0x1f

#define NVIDIA_SMB_PRTCL_WRITE          0x00
#define NVIDIA_SMB_PRTCL_READ           0x01
#define NVIDIA_SMB_PRTCL_QUICK          0x02
#define NVIDIA_SMB_PRTCL_BYTE           0x04
#define NVIDIA_SMB_PRTCL_BYTE_DATA      0x06
#define NVIDIA_SMB_PRTCL_WORD_DATA      0x08
#define NVIDIA_SMB_PRTCL_BLOCK_DATA     0x0a
#define NVIDIA_SMB_PRTCL_PEC            0x80

/* Local definitions replacing linux/pci_ids.h usage, matching driver IDs */
#define PCIBASE_VENDOR_ID               0x10de  /* PCI_VENDOR_ID_NVIDIA */
#define PCIBASE_DEVICE_ID               0x0064  /* PCI_DEVICE_ID_NVIDIA_NFORCE2_SMBUS */
#define PCIBASE_CLASS_ID                0x0c05  /* PCI_CLASS_SERIAL_BUS_SM */

/* max block count per SMBus spec used by driver */
#define PCIBASE_SMB_MAX_BLOCK           32

/* emulate MAX_TIMEOUT from driver (in ms) */
#define PCIBASE_SMB_MAX_TIMEOUT         100

/* Simple completion delay in ms to emulate asynchronous SMBus operation */
#define PCIBASE_SMB_COMPLETE_DELAY_MS   1

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t regs[0x40];

    /* Simple model state for SMBus emulation */
    QEMUTimer *xfer_timer;
    bool xfer_in_progress;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The provided driver never uses interrupts for SMBus completion.
     * Keep IRQ line deasserted.
     */
    pci_set_irq(pdev, 0);
}

/* Device-initiated DMA logic based on driver access patterns
 * The nforce2 SMBus driver does not set up or use DMA, so this is unused.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)pdev;
    (void)is_write;
}

/* Forward declaration of helper for starting SMBus transaction */
static void pcibase_start_transaction(PCIBaseState *s);

static void pcibase_xfer_complete(void *opaque)
{
    PCIBaseState *s = opaque;

    if (!s->xfer_in_progress) {
        return;
    }

    s->xfer_in_progress = false;

    /* Set status to DONE with no error bits */
    s->regs[NVIDIA_SMB_STS_OFFSET] |= NVIDIA_SMB_STS_DONE;
    s->regs[NVIDIA_SMB_STS_OFFSET] &= ~NVIDIA_SMB_STS_STATUS;

    /* If read protocol, synthesize dummy data */
    if (s->regs[NVIDIA_SMB_PRTCL_OFFSET] & NVIDIA_SMB_PRTCL_READ) {
        uint8_t proto = s->regs[NVIDIA_SMB_PRTCL_OFFSET];

        if (proto & NVIDIA_SMB_PRTCL_BLOCK_DATA) {
            /* For block read, set a small length and fill bytes */
            uint8_t len = 4;
            int i;
            if (len > PCIBASE_SMB_MAX_BLOCK) {
                len = PCIBASE_SMB_MAX_BLOCK;
            }
            s->regs[NVIDIA_SMB_BCNT_OFFSET] = len;
            for (i = 0; i < len; i++) {
                s->regs[NVIDIA_SMB_DATA_OFFSET + i] = (uint8_t)(i + 1);
            }
        } else if (proto & NVIDIA_SMB_PRTCL_WORD_DATA) {
            /* word read: 2 bytes */
            s->regs[NVIDIA_SMB_DATA_OFFSET] = 0x34;
            s->regs[NVIDIA_SMB_DATA_OFFSET + 1] = 0x12;
        } else {
            /* byte/quick read */
            s->regs[NVIDIA_SMB_DATA_OFFSET] = 0x55;
        }
    }
}

static void pcibase_start_transaction(PCIBaseState *s)
{
    /* Start a new transaction when protocol register is written.
     * Model behavior similar to hardware: clear DONE, clear error bits,
     * and complete a bit later via timer.
     */
    s->xfer_in_progress = true;

    /* Clear previous status */
    s->regs[NVIDIA_SMB_STS_OFFSET] = 0x00;

    /* Schedule completion shortly */
    if (s->xfer_timer) {
        timer_mod(s->xfer_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) +
                                 PCIBASE_SMB_COMPLETE_DELAY_MS);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* For this device, we only use PIO BAR. MMIO is unused. */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Driver uses inb_p/outb_p: 1-byte accesses */
    if (size != 1) {
        return 0xff;
    }

    if (addr >= sizeof(s->regs)) {
        return 0xff;
    }

    return s->regs[addr];
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t v8 = (uint8_t)val;

    if (size != 1) {
        return;
    }

    if (addr >= sizeof(s->regs)) {
        return;
    }

    switch (addr) {
    case NVIDIA_SMB_PRTCL_OFFSET:
        /* Writing protocol starts a new SMBus transaction */
        s->regs[addr] = v8;
        pcibase_start_transaction(s);
        break;

    case NVIDIA_SMB_STS_OFFSET:
        /* Not explicitly used in driver, but implement W1C for DONE & errors */
        if (v8 & NVIDIA_SMB_STS_DONE) {
            s->regs[NVIDIA_SMB_STS_OFFSET] &= ~NVIDIA_SMB_STS_DONE;
        }
        if (v8 & NVIDIA_SMB_STS_STATUS) {
            s->regs[NVIDIA_SMB_STS_OFFSET] &= ~NVIDIA_SMB_STS_STATUS;
        }
        break;

    case NVIDIA_SMB_STATUS_ABRT_OFFSET:
        /* Driver writes NVIDIA_SMB_STATUS_ABRT_STS to clear abort status */
        if (v8 & NVIDIA_SMB_STATUS_ABRT_STS) {
            s->regs[NVIDIA_SMB_STATUS_ABRT_OFFSET] &= ~NVIDIA_SMB_STATUS_ABRT_STS;
        }
        break;

    case NVIDIA_SMB_CTRL_OFFSET:
        /* Abort control: when NVIDIA_SMB_CTRL_ABORT is written, set abort
         * status bit and clear in-progress transaction.
         */
        if (v8 & NVIDIA_SMB_CTRL_ABORT) {
            s->regs[NVIDIA_SMB_STATUS_ABRT_OFFSET] |= NVIDIA_SMB_STATUS_ABRT_STS;
            s->xfer_in_progress = false;
            if (s->xfer_timer) {
                timer_del(s->xfer_timer);
            }
        }
        s->regs[addr] = v8;
        break;

    default:
        /* All other registers behave as simple read/write shadows */
        s->regs[addr] = v8;
        break;
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

    memset(s->regs, 0, sizeof(s->regs));
    s->xfer_in_progress = false;
    if (s->xfer_timer) {
        timer_del(s->xfer_timer);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: driver probes bar 4 and 5; we model BAR0 as legacy IO
     * and let PCI config use alt regs to point to this IO base.
     */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x40; /* 64 bytes as used in driver when alt_reg */
    s->bar_info[0].name = "nforce2-smbus-io";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "unused";
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    memset(s->regs, 0, sizeof(s->regs));
    s->xfer_in_progress = false;

    /* Initialize xfer completion timer */
    s->xfer_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, pcibase_xfer_complete, s);

    /* Advertise legacy alternate BAR registers in PCI config space for
     * NFORCE_PCI_SMB1 (0x50) and NFORCE_PCI_SMB2 (0x54). We simply point
     * them to the same IO base for simplicity.
     */
    uint16_t iobase = 0x1000; /* arbitrary legacy IO base; QEMU will map BAR0 */
    pci_set_word(pci_conf + NFORCE_PCI_SMB1, iobase);
    pci_set_word(pci_conf + NFORCE_PCI_SMB2, iobase + 0x40);
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

    if (s->xfer_timer) {
        timer_del(s->xfer_timer);
        timer_free(s->xfer_timer);
        s->xfer_timer = NULL;
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "nForce2_smbus_pci",
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

