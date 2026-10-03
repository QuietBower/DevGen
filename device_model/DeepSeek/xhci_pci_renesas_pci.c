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
#include "qemu/bswap.h"
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
#include <string.h>

#define TYPE_PCIBASE_DEVICE "xhci_pci_renesas_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, TYPE_PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1912 /* PCI_VENDOR_ID_RENESAS */
#define DEVICE_ID 0x0014
#define CLASS_ID  0x0c0330 /* PCI_CLASS_SERIAL_USB_XHCI */

#define BIT(nr) (1UL << (nr))
#define GENMASK(msb, lsb) (((1UL << ((msb) - (lsb) + 1)) - 1) << (lsb))

/* Renesas firmware registers (config space) */
#define RENESAS_FW_VERSION          0x6C
#define RENESAS_ROM_CONFIG          0xF0
#define RENESAS_FW_STATUS           0xF4
#define RENESAS_FW_STATUS_MSB       0xF5
#define RENESAS_ROM_STATUS          0xF6
#define RENESAS_ROM_STATUS_MSB      0xF7
#define RENESAS_DATA0               0xF8
#define RENESAS_DATA1               0xFC
#define RENESAS_FW_VERSION_FIELD            GENMASK(23, 7)
#define RENESAS_FW_VERSION_OFFSET           8
#define RENESAS_FW_STATUS_DOWNLOAD_ENABLE   BIT(0)
#define RENESAS_FW_STATUS_LOCK             BIT(1)
#define RENESAS_FW_STATUS_RESULT           GENMASK(6, 4)
#define RENESAS_FW_STATUS_INVALID           0
#define RENESAS_FW_STATUS_SUCCESS          BIT(4)
#define RENESAS_FW_STATUS_ERROR            BIT(5)
#define RENESAS_FW_STATUS_SET_DATA0         BIT(8)
#define RENESAS_FW_STATUS_SET_DATA1         BIT(9)
#define RENESAS_ROM_STATUS_ACCESS           BIT(0)
#define RENESAS_ROM_STATUS_ERASE            BIT(1)
#define RENESAS_ROM_STATUS_RELOAD           BIT(2)
#define RENESAS_ROM_STATUS_RESULT           GENMASK(6, 4)
#define RENESAS_ROM_STATUS_NO_RESULT        0
#define RENESAS_ROM_STATUS_SUCCESS          BIT(4)
#define RENESAS_ROM_STATUS_ERROR            BIT(5)
#define RENESAS_ROM_STATUS_SET_DATA0         BIT(8)
#define RENESAS_ROM_STATUS_SET_DATA1         BIT(9)
#define RENESAS_ROM_STATUS_ROM_EXISTS        BIT(15)
#define RENESAS_ROM_ERASE_MAGIC            0x5A65726F
#define RENESAS_ROM_WRITE_MAGIC            0x53524F4D
#define RENESAS_DELAY                       10

/* XHCI standard registers (MMIO) - offsets from driver */
#define XHCI_HCC_PARAMS_OFFSET  0x10
#define XHCI_HCSPARAMS1         0x4

/* Used in driver probe to check HCC parameters */
#define XHCI_HCC_EXT_CAPS(p)    (((p)>>16)&0xffff)
#define XHCI_EXT_CAPS_ID(p)     (((p)>>0)&0xff)
#define XHCI_EXT_CAPS_NEXT(p)   (((p)>>8)&0xff)

/* BAR size placeholder, need to be specified */
#define BAR_SIZE 0x1000
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

    /* Renesas-specific registers */
    uint8_t renesas_regs[0x100]; /* covers offsets 0x00-0xFF */
};

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size > sizeof(s->renesas_regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read out of range addr 0x%" HWADDR_PRIx " size %u\n",
                      __func__, addr, size);
        return 0;
    }

    /* Little-endian byte assembly */
    for (unsigned i = 0; i < size; i++) {
        val |= (uint64_t)s->renesas_regs[addr + i] << (i * 8);
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > sizeof(s->renesas_regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write out of range addr 0x%" HWADDR_PRIx " size %u\n",
                      __func__, addr, size);
        return;
    }

    /* Little-endian byte write */
    for (unsigned i = 0; i < size; i++) {
        s->renesas_regs[addr + i] = (uint8_t)(val >> (i * 8));
    }

    /* Emulate USBSTS.HCHalted side effect based on USBCMD.RS */
    /* if the write touched USBCMD (0x20-0x23) or USBSTS (0x24-0x27) */
    if (addr < 0x28 && addr + size > 0x20) {
        uint32_t usbcmd = ldl_le_p(&s->renesas_regs[0x20]);
        uint8_t hchalted = (usbcmd & 1) ? 0x00 : 0x01;
        s->renesas_regs[0x24] = (s->renesas_regs[0x24] & ~0x01) | hchalted;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset Renesas registers to default values */
    memset(s->renesas_regs, 0, sizeof(s->renesas_regs));
    /* Set USBSTS.HCHalted to 1 after reset */
    s->renesas_regs[0x24] = 0x01;

    /* Initialize xHCI capability registers in MMIO space */
    uint8_t *regs = s->renesas_regs;
    /* CAPLENGTH (0x20) and HCIVERSION (0x0100) at offset 0 */
    regs[0x00] = 0x20;
    regs[0x01] = 0x00;
    regs[0x02] = 0x00;
    regs[0x03] = 0x01;
    /* HCSPARAMS1: MaxPorts=4, MaxSlots=64 */
    regs[0x04] = 0x40;
    regs[0x05] = 0x00;
    regs[0x06] = 0x00;
    regs[0x07] = 0x04;
    /* HCSPARAMS2: 0 */
    /* HCSPARAMS3: 0 */
    /* HCCPARAMS: 0 (no ext caps) */
    /* DBOFF: 0x40 */
    regs[0x14] = 0x40;
    regs[0x15] = 0x00;
    regs[0x16] = 0x00;
    regs[0x17] = 0x00;
    /* RTSOFF: 0x80 */
    regs[0x18] = 0x80;
    regs[0x19] = 0x00;
    regs[0x1A] = 0x00;
    regs[0x1B] = 0x00;

    /* Re-initialize Renesas config space to indicate firmware already loaded */
    PCIDevice *pdev = PCI_DEVICE(dev);
    uint8_t *pci_conf = pdev->config;
    pci_set_long(pci_conf + RENESAS_FW_VERSION, 0x00010000); /* version 1.0 */
    pci_set_byte(pci_conf + RENESAS_FW_STATUS, RENESAS_FW_STATUS_LOCK | RENESAS_FW_STATUS_SUCCESS);
    pci_set_byte(pci_conf + RENESAS_FW_STATUS_MSB, 0);
    pci_set_byte(pci_conf + RENESAS_ROM_STATUS, 0); /* no ROM */
    pci_set_byte(pci_conf + RENESAS_ROM_STATUS_MSB, 0);
    pci_set_long(pci_conf + RENESAS_DATA0, 0);
    pci_set_long(pci_conf + RENESAS_DATA1, 0);
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
    } else {
        /* Not implemented for other types */
        error_setg(errp, "unsupported BAR type %d", bi->type);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize Renesas-specific config registers to indicate firmware is loaded */
    pci_set_long(pci_conf + RENESAS_FW_VERSION, 0x00010000); /* version 1.0 */
    pci_set_byte(pci_conf + RENESAS_FW_STATUS, RENESAS_FW_STATUS_LOCK | RENESAS_FW_STATUS_SUCCESS);
    pci_set_byte(pci_conf + RENESAS_FW_STATUS_MSB, 0);
    pci_set_byte(pci_conf + RENESAS_ROM_STATUS, 0); /* no ROM exists */
    pci_set_byte(pci_conf + RENESAS_ROM_STATUS_MSB, 0);
    pci_set_long(pci_conf + RENESAS_DATA0, 0);
    pci_set_long(pci_conf + RENESAS_DATA1, 0);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR_SIZE;
    s->bar_info[0].name = "renesas-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X, DMA, or timers for this driver */
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
    /* No additional cleanup required */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "xhci_pci_renesas_pci",
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
