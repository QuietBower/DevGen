/*
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
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
#include "qemu/bswap.h"

/* Additional include files retrieved from driver context */

#define TYPE_PCIBASE_DEVICE "CAFE_NAND_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */

/* CAFE NAND registers */
#define CAFE_NAND_CTRL1         0x00
#define CAFE_NAND_CTRL2         0x04
#define CAFE_NAND_CTRL3         0x08
#define CAFE_NAND_STATUS        0x0c
#define CAFE_NAND_IRQ           0x10
#define CAFE_NAND_IRQ_MASK      0x14
#define CAFE_NAND_DATA_LEN      0x18
#define CAFE_NAND_ADDR1         0x1c
#define CAFE_NAND_ADDR2         0x20
#define CAFE_NAND_TIMING1       0x24
#define CAFE_NAND_TIMING2       0x28
#define CAFE_NAND_TIMING3       0x2c
#define CAFE_NAND_NONMEM        0x30
#define CAFE_NAND_ECC_RESULT    0x3C
#define CAFE_NAND_DMA_CTRL      0x40
#define CAFE_NAND_DMA_ADDR0     0x44
#define CAFE_NAND_DMA_ADDR1     0x48
#define CAFE_NAND_ECC_SYN01     0x50
#define CAFE_NAND_ECC_SYN23     0x54
#define CAFE_NAND_ECC_SYN45     0x58
#define CAFE_NAND_ECC_SYN67     0x5c
#define CAFE_NAND_READ_DATA     0x1000
#define CAFE_NAND_WRITE_DATA    0x2000
#define CAFE_GLOBAL_CTRL        0x3004
#define CAFE_GLOBAL_IRQ         0x3008
#define CAFE_GLOBAL_IRQ_MASK    0x300c
#define CAFE_NAND_RESET         0x3034

#define CTRL1_CHIPSELECT        (1<<19)

/* PCI IDs from Linux driver: drivers/mtd/nand/raw/cafe_nand.c */
#define CAFE_VENDOR_ID          0x11ab
#define CAFE_DEVICE_ID          0x4100
#define CAFE_CLASS_ID           0x0501

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

#define BAR0_SIZE 0x4000

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t regs[BAR0_SIZE / sizeof(uint32_t)];

    /* DMA Context */
    struct {
        uint32_t ctrl;
        uint32_t addr0;
        uint32_t addr1;
        dma_addr_t current_addr;
        uint8_t dmabuf[8192];
        bool active;
    } dma;

};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint8_t *mem = (uint8_t *)s->regs;

    if (addr + size > sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read out of bounds: addr=0x%" HWADDR_PRIx " size=%u\n",
                      __func__, addr, size);
        return 0;
    }

    switch (size) {
    case 1:
        val = mem[addr];
        break;
    case 2:
        val = lduw_le_p(mem + addr);
        break;
    case 4:
        val = ldl_le_p(mem + addr);
        break;
    case 8:
        val = ldq_le_p(mem + addr);
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t *mem = (uint8_t *)s->regs;

    if (addr + size > sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write out of bounds: addr=0x%" HWADDR_PRIx " size=%u val=%" PRIx64 "\n",
                      __func__, addr, size, val);
        return;
    }

    /* Handle Write-1-to-Clear for NAND_IRQ at offset 0x10 */
    if (addr == 0x10 && size == 4) {
        uint32_t curr = ldl_le_p(mem + addr);
        curr &= ~(uint32_t)val;
        stl_le_p(mem + addr, curr);
        return;
    }

    /* Write the value to regs */
    switch (size) {
    case 1:
        mem[addr] = val;
        break;
    case 2:
        stw_le_p(mem + addr, val);
        break;
    case 4:
        stl_le_p(mem + addr, val);
        break;
    case 8:
        stq_le_p(mem + addr, val);
        break;
    }

    /* Side effects for specific registers */
    if (addr == 0x00 && (val & 0x80000000)) {
        /* Command issued in NAND_CTRL1 */
        uint32_t command = val & 0xFF;
        /* Handle READID */
        if (command == 0x90) { /* NAND_CMD_READID */
            uint8_t id[] = {0xEC, 0xF1, 0x00, 0x95};
            memcpy(&mem[0x1000], id, sizeof(id));
        }
        /* Set completion IRQ */
        {
            uint32_t irq = ldl_le_p(mem + CAFE_NAND_IRQ);
            irq |= 0x80000000; /* doneint for non-DMA */
            stl_le_p(mem + CAFE_NAND_IRQ, irq);
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Clear register area (0x0000-0x0FFF) */
    memset(s->regs, 0, 0x1000);
    /* Set data area (0x1000-0x2FFF) to 0xFF (blank NAND) */
    memset((uint8_t *)s->regs + 0x1000, 0xFF, 0x2000);
    /* Clear global regs area (0x3000-0x3FFF) */
    memset((uint8_t *)s->regs + 0x3000, 0, 0x1000);

    /* Initialize specific register defaults */
    s->regs[CAFE_NAND_STATUS / 4] = 0x40000000; /* NAND device ready */
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
        error_setg(errp, "PIO BARs not supported");
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  CAFE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  CAFE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CAFE_CLASS_ID );
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
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "cafe-nand-mmio";
    
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize regs with default values */
    memset(s->regs, 0, 0x1000);
    memset((uint8_t *)s->regs + 0x1000, 0xFF, 0x2000);
    memset((uint8_t *)s->regs + 0x3000, 0, 0x1000);
    s->regs[CAFE_NAND_STATUS / 4] = 0x40000000;

    /* No MSI/MSI-X, no DMA, no timers needed */
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
    .name = "CAFE_NAND_pci",
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