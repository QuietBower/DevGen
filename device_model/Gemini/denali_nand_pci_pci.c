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

#define TYPE_PCIBASE_DEVICE "denali_nand_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL 0x8086
#define DENALI_DEVICE_ID 0x0701
#define DENALI_CLASS_ID 0x0501

#define TRANSFER_SPARE_REG 0x10
#define RB_PIN_ENABLED 0x60
#define CHIP_ENABLE_DONT_CARE 0xd0
#define ECC_ENABLE 0xe0
#define GLOBAL_INT_ENABLE 0xf0
#define TWHR2_AND_WE_2_RE 0x100
#define TCWAW_AND_ADDR_2_DATA 0x110
#define RE_2_WE 0x120
#define ACC_CLKS 0x130
#define PAGES_PER_BLOCK 0x150
#define DEVICE_WIDTH 0x160
#define DEVICE_MAIN_AREA_SIZE 0x170
#define DEVICE_SPARE_AREA_SIZE 0x180
#define TWO_ROW_ADDR_CYCLES 0x190
#define ECC_CORRECTION 0x1b0
#define RDWR_EN_LO_CNT 0x1f0
#define RDWR_EN_HI_CNT 0x200
#define CS_SETUP_CNT 0x220
#define SPARE_AREA_SKIP_BYTES 0x230
#define SPARE_AREA_MARKER 0x240
#define DEVICES_CONNECTED 0x250
#define WRITE_PROTECT 0x280
#define RE_2_RE 0x290
#define REVISION 0x370
#define FEATURES 0x3f0
#define INTR_STATUS(bank) (0x410 + (bank) * 0x50)
#define INTR_EN(bank) (0x420 + (bank) * 0x50)
#define ECC_ERROR_ADDRESS 0x630
#define ERR_CORRECTION_INFO 0x640
#define ECC_COR_INFO(bank) (0x650 + (bank) / 2 * 0x10)
#define CFG_DATA_BLOCK_SIZE 0x6b0
#define CFG_LAST_DATA_BLOCK_SIZE 0x6c0
#define CFG_NUM_DATA_BLOCKS 0x6d0
#define DMA_ENABLE 0x700

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
    uint32_t irq_status;
    uint32_t irq_mask;
    uint32_t global_int_enable;
    uint32_t intr_status[8];
    uint32_t intr_en[8];

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t transfer_spare_reg;
    uint32_t rb_pin_enabled;
    uint32_t chip_enable_dont_care;
    uint32_t ecc_enable;
    uint32_t twhr2_and_we_2_re;
    uint32_t tcwaw_and_addr_2_data;
    uint32_t re_2_we;
    uint32_t acc_clks;
    uint32_t pages_per_block;
    uint32_t device_width;
    uint32_t device_main_area_size;
    uint32_t device_spare_area_size;
    uint32_t two_row_addr_cycles;
    uint32_t ecc_correction;
    uint32_t rdwr_en_lo_cnt;
    uint32_t rdwr_en_hi_cnt;
    uint32_t cs_setup_cnt;
    uint32_t spare_area_skip_bytes;
    uint32_t spare_area_marker;
    uint32_t devices_connected;
    uint32_t write_protect;
    uint32_t re_2_re;
    uint32_t revision;
    uint32_t features;
    uint32_t ecc_error_address;
    uint32_t err_correction_info;
    uint32_t ecc_cor_info[8];
    uint32_t cfg_data_block_size;
    uint32_t cfg_last_data_block_size;
    uint32_t cfg_num_data_blocks;
    uint32_t dma_enable;

    /* DMA Context */
    bool dma_avail;
    dma_addr_t dma_addr;

    int active_bank;
    int nbanks;
    int id_idx;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    bool raise = false;
    if (s->global_int_enable & 1) {
        for (int i = 0; i < s->nbanks; i++) {
            if (s->intr_status[i] & s->intr_en[i]) {
                raise = true;
                break;
            }
        }
    }
    pci_set_irq(&s->parent_obj, raise ? 1 : 0);
}

static uint64_t pcibase_host_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    /* Valid Toshiba 1GB NAND ID to pass "No NAND device found" */
    uint8_t nand_id[] = { 0x98, 0xd3, 0x90, 0x15, 0x76, 0x14, 0x00, 0x00 };
    uint64_t val = 0;

    for (unsigned i = 0; i < size; i++) {
        val |= ((uint64_t)nand_id[s->id_idx % sizeof(nand_id)]) << (i * 8);
        s->id_idx++;
    }
    return val;
}

static void pcibase_host_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* Reset ID index on any command write */
    s->id_idx = 0;

    /* Writing to host implies a command/data transfer. 
     * Set the 0x1000 interrupt bit to signal completion and prevent timeout. */
    for (int i = 0; i < s->nbanks; i++) {
        s->intr_status[i] |= 0x1000;
    }
    pcibase_update_irq(s);
}

static const MemoryRegionOps pcibase_host_ops = {
    .read = pcibase_host_read,
    .write = pcibase_host_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x410 && addr < 0x410 + 8 * 0x50) {
        int bank = (addr - 0x410) / 0x50;
        int offset = (addr - 0x410) % 0x50;
        if (offset == 0) return s->intr_status[bank];
        if (offset == 0x10) return s->intr_en[bank];
    }

    switch (addr) {
    case FEATURES:
        val = s->features;
        break;
    case REVISION:
        val = s->revision;
        break;
    case SPARE_AREA_SKIP_BYTES:
        val = s->spare_area_skip_bytes;
        break;
    case GLOBAL_INT_ENABLE:
        val = s->global_int_enable;
        break;
    case DEVICES_CONNECTED:
        val = s->devices_connected;
        break;
    case TRANSFER_SPARE_REG:
        val = s->transfer_spare_reg;
        break;
    case RB_PIN_ENABLED:
        val = s->rb_pin_enabled;
        break;
    case CHIP_ENABLE_DONT_CARE:
        val = s->chip_enable_dont_care;
        break;
    case ECC_ENABLE:
        val = s->ecc_enable;
        break;
    case SPARE_AREA_MARKER:
        val = s->spare_area_marker;
        break;
    case WRITE_PROTECT:
        val = s->write_protect;
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x410 && addr < 0x410 + 8 * 0x50) {
        int bank = (addr - 0x410) / 0x50;
        int offset = (addr - 0x410) % 0x50;
        if (offset == 0) {
            s->intr_status[bank] &= ~val; /* W1C */
            pcibase_update_irq(s);
        } else if (offset == 0x10) {
            s->intr_en[bank] = val;
            pcibase_update_irq(s);
        }
        return;
    }

    switch (addr) {
    case SPARE_AREA_SKIP_BYTES:
        s->spare_area_skip_bytes = val;
        break;
    case TRANSFER_SPARE_REG:
        s->transfer_spare_reg = val;
        break;
    case RB_PIN_ENABLED:
        s->rb_pin_enabled = val;
        break;
    case CHIP_ENABLE_DONT_CARE:
        s->chip_enable_dont_care = val;
        break;
    case ECC_ENABLE:
        s->ecc_enable = val;
        break;
    case SPARE_AREA_MARKER:
        s->spare_area_marker = val;
        break;
    case WRITE_PROTECT:
        s->write_protect = val;
        break;
    case GLOBAL_INT_ENABLE:
        s->global_int_enable = val;
        pcibase_update_irq(s);
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

    /* FEATURES__N_BANKS (bits 0-1) = 2 (4 banks), FEATURES__DMA (bit 6) */
    s->features = (1 << 6) | 2;
    s->revision = 0x0501;
    s->spare_area_skip_bytes = 0;
    s->transfer_spare_reg = 0;
    s->rb_pin_enabled = 0;
    s->chip_enable_dont_care = 0;
    s->ecc_enable = 0;
    s->spare_area_marker = 0;
    s->write_protect = 0;
    s->global_int_enable = 0;
    s->devices_connected = 1;
    s->nbanks = 4;
    s->active_bank = -1;
    s->id_idx = 0;
    
    for (int i = 0; i < 8; i++) {
        s->intr_status[i] = 0x1000; /* Set bit 12 to prevent timeout */
        s->intr_en[i] = 0;
    }
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    hwaddr aligned_size = pow2ceil(bi->size);
    if (aligned_size == 0) {
        aligned_size = 4096; /* Fallback to prevent QEMU crash on 0-sized BAR */
    }
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        if (bi->index == 0) {
            memory_region_init_io(mr, OBJECT(s), &pcibase_host_ops, s, bi->name, aligned_size);
        } else {
            memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        }
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0701 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0501 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x10000;
    s->bar_info[0].name = "denali-host";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x10000;
    s->bar_info[1].name = "denali-csr";
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

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
