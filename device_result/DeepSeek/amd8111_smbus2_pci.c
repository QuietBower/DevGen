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
/* No extra includes required */

#define TYPE_PCIBASE_DEVICE "amd8111_smbus2_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* Vendor and Device IDs are requested from supplementary sources */
#define VENDOR_ID PCI_VENDOR_ID_AMD
#define PCI_DEVICE_ID_AMD_8111_SMBUS2 0x746a
#define DEVICE_ID PCI_DEVICE_ID_AMD_8111_SMBUS2
#define CLASS_ID 0x0c05  /* SMBus */

/* Register offsets from driver */
#define AMD_EC_DATA 0x00
#define AMD_EC_SC   0x04
#define AMD_EC_CMD  0x04
#define AMD_EC_ICR  0x08
#define AMD_EC_SC_SMI    0x04
#define AMD_EC_SC_SCI    0x02
#define AMD_EC_SC_BURST  0x01
#define AMD_EC_SC_CMD    0x08
#define AMD_EC_SC_IBF    0x02
#define AMD_EC_SC_OBF    0x01
#define AMD_EC_CMD_RD    0x80
#define AMD_EC_CMD_WR    0x81
#define AMD_EC_CMD_BE    0x82
#define AMD_EC_CMD_BD    0x83
#define AMD_EC_CMD_QR    0x84
#define AMD_SMB_PRTCL    0x00
#define AMD_SMB_STS      0x01
#define AMD_SMB_ADDR     0x02
#define AMD_SMB_CMD      0x03
#define AMD_SMB_DATA     0x04
#define AMD_SMB_BCNT     0x24
#define AMD_SMB_ALRM_A   0x25
#define AMD_SMB_ALRM_D   0x26
#define AMD_SMB_STS_DONE      0x80
#define AMD_SMB_STS_ALRM      0x40
#define AMD_SMB_STS_RES       0x20
#define AMD_SMB_STS_STATUS    0x1f
#define AMD_SMB_STATUS_OK         0x00
#define AMD_SMB_STATUS_FAIL       0x07
#define AMD_SMB_STATUS_DNAK       0x10
#define AMD_SMB_STATUS_DERR       0x11
#define AMD_SMB_STATUS_CMD_DENY   0x12
#define AMD_SMB_STATUS_UNKNOWN    0x13
#define AMD_SMB_STATUS_ACC_DENY   0x17
#define AMD_SMB_STATUS_TIMEOUT    0x18
#define AMD_SMB_STATUS_NOTSUP     0x19
#define AMD_SMB_STATUS_BUSY       0x1A
#define AMD_SMB_STATUS_PEC        0x1F
#define AMD_SMB_PRTCL_WRITE           0x00
#define AMD_SMB_PRTCL_READ            0x01
#define AMD_SMB_PRTCL_QUICK           0x02
#define AMD_SMB_PRTCL_BYTE            0x04
#define AMD_SMB_PRTCL_BYTE_DATA       0x06
#define AMD_SMB_PRTCL_WORD_DATA       0x08
#define AMD_SMB_PRTCL_BLOCK_DATA      0x0a
#define AMD_SMB_PRTCL_PROC_CALL       0x0c
#define AMD_SMB_PRTCL_BLOCK_PROC_CALL  0x0d
#define AMD_SMB_PRTCL_I2C_BLOCK_DATA  0x4a
#define AMD_SMB_PRTCL_PEC             0x80
#define AMD_PCI_MISC 0x48

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

    /* Shadow storage for BAR0 register space (includes SMB and EC registers) */
    uint8_t bar0_space[0x100];

    /* EC interface state */
    uint8_t sc_value;   /* AMD_EC_SC register */
    uint8_t ec_cmd;
    uint8_t ec_data_out;
    uint8_t ec_addr;
    enum { EC_IDLE, EC_RD_ADDR_WAIT, EC_RD_DONE, EC_WR_ADDR_WAIT, EC_WR_DATA_WAIT } ec_state;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* Interrupt logic not used by driver */
}

/* MMIO handlers - not used, store zeros */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Nothing */
}

/* PIO handlers for BAR0 */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0x00: /* AMD_EC_DATA */
        if (size == 1) {
            val = s->ec_data_out;
            /* reading DATA clears OBF */
            s->sc_value &= ~AMD_EC_SC_OBF;
            if (s->ec_state == EC_RD_DONE) {
                s->ec_state = EC_IDLE;
            }
        }
        break;
    case 0x04: /* AMD_EC_SC */
        if (size == 1) {
            val = s->sc_value;
        }
        break;
    default:
        /* other addresses return 0 */
        val = 0;
        break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0x00: /* AMD_EC_DATA */
        if (size == 1) {
            switch (s->ec_state) {
            case EC_RD_ADDR_WAIT:
                s->ec_addr = val;
                /* perform read from internal space */
                s->ec_data_out = s->bar0_space[val];
                /* set OBF to indicate data ready */
                s->sc_value |= AMD_EC_SC_OBF;
                s->ec_state = EC_RD_DONE;
                break;
            case EC_WR_ADDR_WAIT:
                s->ec_addr = val;
                s->ec_state = EC_WR_DATA_WAIT;
                break;
            case EC_WR_DATA_WAIT:
                /* write data to internal space */
                s->bar0_space[s->ec_addr] = val;
                /* if writing to AMD_SMB_PRTCL (offset 0x00), set STS to DONE */
                if (s->ec_addr == 0x00) { /* AMD_SMB_PRTCL */
                    s->bar0_space[0x01] = AMD_SMB_STS_DONE; /* STS offset */
                }
                s->ec_state = EC_IDLE;
                break;
            default:
                /* unexpected: ignore */
                break;
            }
        }
        break;
    case 0x04: /* AMD_EC_CMD */
        if (size == 1) {
            s->ec_cmd = val;
            if (val == AMD_EC_CMD_RD) {
                s->ec_state = EC_RD_ADDR_WAIT;
            } else if (val == AMD_EC_CMD_WR) {
                s->ec_state = EC_WR_ADDR_WAIT;
            } else {
                /* unrecognized command - ignore */
                s->ec_state = EC_IDLE;
            }
        }
        break;
    default:
        /* ignore writes to other addresses */
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

    /* Reset EC state */
    s->ec_state = EC_IDLE;
    s->sc_value = 0;
    s->ec_cmd = 0;
    s->ec_data_out = 0;
    s->ec_addr = 0;
    /* Clear the internal register space */
    memset(s->bar0_space, 0, sizeof(s->bar0_space));
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
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    /* BAR0: I/O ports, size 0x100 (256 bytes), used for SMBus and EC registers */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "amd8111_smbus2_io";
    s->num_bars = 1;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize EC state */
    s->ec_state = EC_IDLE;
    s->sc_value = 0;
    s->ec_cmd = 0;
    s->ec_data_out = 0;
    s->ec_addr = 0;
    memset(s->bar0_space, 0, sizeof(s->bar0_space));

    /* MSI/MSI-X not used by driver; placeholder removed */

    /* DMA not used by driver; placeholder removed */

    /* Timers not used by driver; placeholder removed */
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

    /* No additional uninitialization needed */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "amd8111_smbus2_pci",
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
