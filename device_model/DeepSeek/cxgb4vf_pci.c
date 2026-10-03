#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/pci/msix.h"
#include "qemu/log.h"
#include "net/net.h"
#include "qapi/error.h"

/* ------------------------------------------------------------------------
 * Constants from Chelsio T4/T5 Virtual Function driver
 * Supplementary source provides:
 *   #define FW_RESET_CMD        0xDF
 *   #define FW_CMD_OP_V(x)      ((x) << FW_CMD_OP_S)
 *   #define FW_CMD_REQUEST_F    FW_CMD_REQUEST_V(1U)
 *   #define FW_CMD_READ_F       FW_CMD_READ_V(1U)
 * We assume FW_CMD_OP_S, FW_CMD_REQUEST_V, FW_CMD_READ_V are defined
 * elsewhere or we derive them. Since driver uses them, we must define.
 * The driver code would typically have:
 *   #define FW_CMD_OP_S         24
 *   #define FW_CMD_REQUEST_V(x) ((x) << 28)
 *   #define FW_CMD_READ_V(x)    ((x) << 26)
 * Let's define the required constants to make it compile.
 * ------------------------------------------------------------------------ */

#define FW_CMD_OP_S         24
#define FW_CMD_REQUEST_V(x) ((x) << 28)
#define FW_CMD_READ_V(x)    ((x) << 26)

#define FW_RESET_CMD        0xDF
#define FW_CMD_OP_V(x)      ((x) << FW_CMD_OP_S)
#define FW_CMD_REQUEST_F    FW_CMD_REQUEST_V(1U)
#define FW_CMD_READ_F       FW_CMD_READ_V(1U)

/* Placeholder for other FW commands */
#define FW_UNKNOWN_CMD      0xFF

/* ------------------------------------------------------------------------
 * PCI vendor / device IDs (taken from the first entry in driver's table)
 * We don't have the driver source at hand, so we use known Chelsio IDs.
 * cxgb4vf usually uses vendor 0x1425, device 0x4800 (T4 VF) or 0x5800 (T5 VF).
 * We'll choose 0x4800 as a typical value.
 * ------------------------------------------------------------------------ */
#define CXGB4VF_VENDOR_ID   0x1425
#define CXGB4VF_DEVICE_ID   0x4800

/* ------------------------------------------------------------------------
 * Type definitions and state structure
 * ------------------------------------------------------------------------ */
#define TYPE_PCIBASE_DEVICE "pcibase"
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

typedef struct PCIBaseState {
    PCIDevice pdev;
    MemoryRegion bar0;               /* MMIO bar 0 */
    uint32_t reg[64];                /* internal registers */
    /* MSI-X support */
} PCIBaseState;

/* Register offsets (simplified) */
enum {
    REG_MBOX_CMD = 0x00,
    REG_MBOX_DATA0 = 0x04,
    REG_MBOX_DATA1 = 0x08,
    REG_MBOX_DATA2 = 0x0C,
    REG_MBOX_DATA3 = 0x10,
    REG_MBOX_STATUS = 0x14,
    /* ... more registers */
};

/* ------------------------------------------------------------------------
 * Helper: process a mailbox command (simplified)
 * The driver writes a command into the mailbox and expects a response.
 * We implement a minimal set of commands, including FW_RESET_CMD.
 * ------------------------------------------------------------------------ */
static void process_mbox_cmd(PCIBaseState *s)
{
    uint32_t cmd = s->reg[REG_MBOX_CMD];
    uint32_t *mbox_data = &s->reg[REG_MBOX_DATA0];

    switch (cmd) {
    case FW_RESET_CMD:
        /* Respond with a completion message */
        mbox_data[0] = cpu_to_be32(FW_CMD_OP_V(FW_RESET_CMD) |
                                   FW_CMD_REQUEST_F | FW_CMD_READ_F);
        break;
    default:
        /* Unknown command: return error status */
        mbox_data[0] = cpu_to_be32(0x80000000); /* error bit */
        break;
    }

    /* Set status register to indicate completion */
    s->reg[REG_MBOX_STATUS] = 0x1; /* ready */

    /* Raise an interrupt to notify the driver */
    if (msix_enabled(&s->pdev)) {
        msix_notify(&s->pdev, 0);
    } else {
        pci_set_irq(&s->pdev, 1);
    }
}

/* ------------------------------------------------------------------------
 * MMIO read callback
 * ------------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    if (addr < sizeof(s->reg)) {
        switch (addr >> 2) {
        case REG_MBOX_CMD:
        case REG_MBOX_DATA0:
        case REG_MBOX_DATA1:
        case REG_MBOX_DATA2:
        case REG_MBOX_DATA3:
        case REG_MBOX_STATUS:
            val = s->reg[addr >> 2];
            break;
        default:
            /* Return ~0 to mimic a hardware read (e.g., unpopulated BAR area) */
            val = (uint32_t)~0U;  /* Fixed overflow warning */
            break;
        }
    } else {
        val = (uint32_t)~0U;
    }

    return val;
}

/* ------------------------------------------------------------------------
 * MMIO write callback
 * ------------------------------------------------------------------------ */
static void pcibase_mmio_write(void *opaque, hwaddr addr,
                                uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < sizeof(s->reg)) {
        uint32_t idx = addr >> 2;
        s->reg[idx] = val;

        /* Trigger command processing on write to the command register */
        if (idx == REG_MBOX_CMD) {
            process_mbox_cmd(s);
        }
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

/* ------------------------------------------------------------------------
 * PCI realize: allocate BARs, set up MSI-X
 * ------------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_conf[PCI_INTERRUPT_PIN] = 1; /* INTA */

    memory_region_init_io(&s->bar0, OBJECT(s), &pcibase_mmio_ops, s,
                          "cxgb4vf-mmio", 256 * 1024); /* 256 KB */
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);

    /* Initialize MSI-X with 16 vectors, table and PBA in the same BAR0. */
    if (msix_init(pdev, 16,
                  &s->bar0, 0, 0,                    /* table bar, nr, offset */
                  &s->bar0, 0, 0x8000,               /* pba bar, nr, offset */
                  0,                                  /* cap_pos = 0 for auto-assign */
                  errp)) {
        error_setg(errp, "Failed to initialize MSI-X");
        return;
    }

    /* Optionally enable MSI-X (driver will do it) */
    msix_set_vector_notifiers(pdev, NULL, NULL, &error_abort);
}

/* ------------------------------------------------------------------------
 * PCI uninit: clean up MSI-X
 * ------------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    /* The local variable 's' was removed to silence unused warning. */
    msix_uninit(pdev, NULL, 0);
}

/* ------------------------------------------------------------------------
 * QEMU device registration
 * ------------------------------------------------------------------------ */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit = pcibase_uninit;
    k->vendor_id = CXGB4VF_VENDOR_ID;
    k->device_id = CXGB4VF_DEVICE_ID;
    k->revision = 0x00;
    k->class_id = PCI_CLASS_NETWORK_ETHERNET;
    dc->desc = "Chelsio T4/T5 Virtual Function (CXGB4VF)";
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static const TypeInfo pcibase_info = {
    .name = TYPE_PCIBASE_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIBaseState),
    .class_init = pcibase_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pcibase_register_types(void)
{
    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types)
