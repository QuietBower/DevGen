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

#define TYPE_PCIBASE_DEVICE "denali_nand_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_DENALI 0x0701
#define PCI_CLASS_ID 0x0501

/* Register offsets */
#define WRITE_PROTECT               0x280
#define SPARE_AREA_SKIP_BYTES       0x230
#define CHIP_ENABLE_DONT_CARE       0xd0
#define FEATURES                    0x3f0
#define ECC_ENABLE                  0xe0
#define REVISION                    0x370
#define RB_PIN_ENABLED              0x60
#define SPARE_AREA_MARKER           0x240
#define GLOBAL_INT_ENABLE           0xf0
#define INTR_EN_BASE                0x420
#define INTR_EN_STRIDE              0x50
#define INTR_STATUS_BASE            0x410
#define INTR_STATUS_STRIDE          0x50
#define ECC_ERROR_ADDRESS           0x630
#define ERR_CORRECTION_INFO         0x640
#define ECC_COR_INFO_BASE           0x650
#define ECC_COR_INFO_STRIDE         0x10
#define DEVICES_CONNECTED           0x250
#define TWO_ROW_ADDR_CYCLES         0x190
#define CFG_DATA_BLOCK_SIZE         0x6b0
#define ECC_CORRECTION              0x1b0
#define DEVICE_MAIN_AREA_SIZE       0x170
#define DEVICE_SPARE_AREA_SIZE      0x180
#define PAGES_PER_BLOCK             0x150
#define DEVICE_WIDTH                0x160
#define CFG_LAST_DATA_BLOCK_SIZE    0x6c0
#define CFG_NUM_DATA_BLOCKS         0x6d0
#define DMA_ENABLE                  0x700
#define CS_SETUP_CNT                0x220
#define TWHR2_AND_WE_2_RE           0x100
#define RDWR_EN_LO_CNT              0x1f0
#define RE_2_RE                     0x290
#define RDWR_EN_HI_CNT              0x200
#define ACC_CLKS                    0x130
#define RE_2_WE                     0x120
#define TCWAW_AND_ADDR_2_DATA       0x110

/* Bitfield definitions */
#define BIT(x) (1U << (x))
#define GENMASK(h, l) (((~0U) << (l)) & (~0U >> (31 - (h))))

#define WRITE_PROTECT__FLAG               BIT(0)
#define ECC_ENABLE__FLAG                  BIT(0)
#define TRANSFER_SPARE_REG                0x10
#define CHIP_EN_DONT_CARE__FLAG           BIT(0)
#define FEATURES__DMA                     BIT(6)
#define FEATURES__N_BANKS                 GENMASK(1, 0)
#define RB_PIN_ENABLED_FLAG               BIT(0)
#define GLOBAL_INT_EN_FLAG                BIT(0)
#define INTR__ECC_TRANSACTION_DONE        BIT(0)
#define INTR__ECC_UNCOR_ERR               BIT(0)
#define INTR__ERASED_PAGE                 BIT(16)
#define INTR__ECC_ERR                     BIT(1)
#define INTR__PROGRAM_FAIL                BIT(4)
#define INTR__DMA_CMD_COMP                BIT(2)
#define INTR__INT_ACT                     BIT(12)
#define INTR__PROGRAM_COMP                BIT(7)
#define INTR__PAGE_XFER_INC               BIT(15)
#define DMA_ENABLE__FLAG                  BIT(0)
#define TWO_ROW_ADDR_CYCLES__FLAG         BIT(0)
#define DENALI_CAP_HW_ECC_FIXUP           BIT(0)
#define DENALI_CAP_DMA_64BIT              BIT(1)
#define FEATURES__INDEX_ADDR              BIT(11)

#define ECC_CORRECTION__VALUE             GENMASK(4, 0)
#define ECC_CORRECTION__ERASE_THRESHOLD   GENMASK(31, 16)
#define ERR_CORRECTION_INFO__BYTE         GENMASK(7, 0)
#define ERR_CORRECTION_INFO__UNCOR        BIT(14)
#define ERR_CORRECTION_INFO__DEVICE       GENMASK(11, 8)
#define ERR_CORRECTION_INFO__LAST_ERR     BIT(15)
#define ECC_ERROR_ADDRESS__SECTOR         GENMASK(15, 12)
#define ECC_ERROR_ADDRESS__OFFSET         GENMASK(11, 0)
#define ECC_COR_INFO__MAX_ERRORS          GENMASK(6, 0)
#define ECC_COR_INFO__UNCOR_ERR           BIT(7)
#define RDWR_EN_HI_CNT__VALUE             GENMASK(4, 0)
#define TCWAW_AND_ADDR_2_DATA__ADDR_2_DATA GENMASK(6, 0)
#define CS_SETUP_CNT__VALUE               GENMASK(4, 0)
#define RE_2_WE__VALUE                    GENMASK(5, 0)
#define RE_2_RE__VALUE                    GENMASK(5, 0)
#define ACC_CLKS__VALUE                   GENMASK(3, 0)
#define RDWR_EN_LO_CNT__VALUE             GENMASK(4, 0)
#define TWHR2_AND_WE_2_RE__WE_2_RE        GENMASK(5, 0)

/* Size of register block, adjusted to cover all known offsets */
#define PCIBASE_REGS_SIZE 0x1000

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
    uint32_t intr_status[4]; /* per-bank interrupt status */
    uint32_t intr_mask[4];   /* per-bank interrupt enable */
    uint32_t global_int_enable;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t regs[PCIBASE_REGS_SIZE / 4];

    /* DMA Context */
    struct {
        dma_addr_t addr;
        int page;
        bool write;
        bool running;
    } dma;

    /* Operational status flags */
    uint32_t features;            /* shadow of FEATURES register */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool irq_active = false;

    if (s->global_int_enable & GLOBAL_INT_EN_FLAG) {
        for (int bank = 0; bank < 4; bank++) {
            if (s->intr_status[bank] & s->intr_mask[bank]) {
                irq_active = true;
                break;
            }
        }
    }
    pci_set_irq(pdev, irq_active ? 1 : 0);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= PCIBASE_REGS_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read out-of-range addr 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return 0;
    }

    /* Driver uses 32-bit reads, but we handle any alignment by returning whole 32-bit value */
    uint32_t reg_offset = addr & ~0x3;
    val = s->regs[reg_offset / 4];
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= PCIBASE_REGS_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write out-of-range addr 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return;
    }

    uint32_t reg_offset = addr & ~0x3;
    uint32_t value = val; /* truncate to 32-bit */

    /* Handle special registers */
    if (reg_offset >= INTR_STATUS_BASE && reg_offset < INTR_STATUS_BASE + 4 * INTR_STATUS_STRIDE) {
        /* Interrupt status: Write-1-to-clear */
        int bank = (reg_offset - INTR_STATUS_BASE) / INTR_STATUS_STRIDE;
        if (bank < 4) {
            /* Bits set to 1 in value are cleared in status */
            s->intr_status[bank] &= ~value;
            s->regs[reg_offset / 4] = s->intr_status[bank];
            pcibase_update_irq(s);
        }
        return;
    } else if (reg_offset >= INTR_EN_BASE && reg_offset < INTR_EN_BASE + 4 * INTR_EN_STRIDE) {
        /* Interrupt enable */
        int bank = (reg_offset - INTR_EN_BASE) / INTR_EN_STRIDE;
        if (bank < 4) {
            s->intr_mask[bank] = value;
            s->regs[reg_offset / 4] = value;
            pcibase_update_irq(s);
        }
        return;
    } else if (reg_offset == GLOBAL_INT_ENABLE) {
        s->global_int_enable = value & GLOBAL_INT_EN_FLAG;
        s->regs[GLOBAL_INT_ENABLE / 4] = s->global_int_enable;
        pcibase_update_irq(s);
        return;
    } else if (reg_offset == FEATURES || reg_offset == REVISION || reg_offset == DEVICES_CONNECTED) {
        /* Read-only registers: ignore writes */
        return;
    } else {
        /* Generic write to register shadow */
        s->regs[reg_offset / 4] = value;
        /* Check if any other side effects needed; for now none */
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* PIO not used */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* PIO not used */
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

    /* Clear all register shadows */
    memset(s->regs, 0, sizeof(s->regs));
    s->global_int_enable = 0;
    for (int i = 0; i < 4; i++) {
        s->intr_status[i] = 0;
        s->intr_mask[i] = 0;
    }

    /* Set default hardware capabilities */
    s->features = FEATURES__DMA | 3; /* DMA enabled, 4 banks */
    s->regs[FEATURES / 4] = s->features;
    s->regs[DEVICES_CONNECTED / 4] = 4; /* 4 devices connected */
    s->regs[REVISION / 4] = 0x0100; /* Example revision */

    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_DENALI );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "csr" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_RAM, .size = 0x1000, .name = "host" };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize register defaults */
    memset(s->regs, 0, sizeof(s->regs));
    s->global_int_enable = 0;
    for (int i = 0; i < 4; i++) {
        s->intr_status[i] = 0;
        s->intr_mask[i] = 0;
    }

    /* Set default features: DMA enabled, 4 banks */
    s->features = FEATURES__DMA | 3;
    s->regs[FEATURES / 4] = s->features;
    s->regs[DEVICES_CONNECTED / 4] = 4;
    s->regs[REVISION / 4] = 0x0100;

    pcibase_update_irq(s);
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

    /* nothing to clean up yet */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "denali_nand_pci_pci",
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
