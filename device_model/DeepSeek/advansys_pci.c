/*
 * QEMU AdvanSys PCI SCSI device model
 * Based on Linux driver advansys.c for narrow PCI board (ASC-1000)
 *
 * This emulates the hardware registers and internal LRAM needed
 * for the driver to probe and load microcode successfully.
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

#define TYPE_PCIBASE_DEVICE "advansys_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs: first entry of advansys_pci_tbl (narrow ASC-1200A) */
#define VENDOR_ID 0x10cd
#define DEVICE_ID 0x1000
#define CLASS_ID  0x0100  /* SCSI controller */

/* Narrow ASC chip I/O port register offsets (from driver) */
#define IOP_SIG_WORD     0x00
#define IOP_SIG_BYTE     0x01
#define IOP_VERSION      0x03
#define IOP_CONFIG_LOW   0x02
#define IOP_CONFIG_HIGH  0x04
#define IOP_CTRL         0x0F
#define IOP_STATUS       0x0E
#define IOP_INT_ACK      IOP_STATUS
#define IOP_REG_IFC      0x0D
#define IOP_EXTRA_CONTROL 0x0D
#define IOP_REG_PC       0x0C
#define IOP_RAM_ADDR     0x0A
#define IOP_RAM_DATA     0x08
#define IOP_EEP_DATA     0x06
#define IOP_EEP_CMD      0x07
#define IOP_SYN_OFFSET   0x0B

/* Status register bits */
#define CSW_INT_PENDING        0x0001
#define CSW_SCSI_RESET_LATCH   0x0002
#define CSW_HALTED             0x0010
#define CSW_EEP_READ_DONE      0x0020

/* Control register bits */
#define CIW_INT_ACK            0x0100
#define CC_CHIP_RESET          0x80
#define CC_HALT                0x20
#define CC_SINGLE_STEP         0x10
#define CC_DIAG                0x01
#define CC_BANK_ONE            0x02
#define CC_TEST                0x04
#define CC_SCSI_RESET          0x40

/* EEPROM commands */
#define ASC_EEP_CMD_READ        0x80
#define ASC_EEP_CMD_WRITE       0xC0
#define ASC_EEP_CMD_WRITE_ABLE  0x40
#define ASC_EEP_CMD_WRITE_DISABLE 0x00

/* Chip version and signature */
#define ASC_CHIP_VER_PCI        0x08
#define ASC_1000_ID0W           0x04C1
#define ASC_1000_ID1B           0x25

#define LRAM_SIZE 0x8000   /* 32K words (64KB) */

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int     index;
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware register shadows */
    uint16_t sig_word;
    uint8_t  sig_byte;
    uint8_t  chip_version;
    uint16_t cfg_lsw;
    uint16_t cfg_msw;
    uint16_t status_reg;
    uint8_t  ctrl_reg;

    /* Emulated LRAM */
    uint16_t *lram;
    uint16_t lram_addr;

    /* EEPROM emulation */
    uint16_t eeprom[64];
    uint8_t  eep_cmd;
    uint16_t eep_data;
    bool     eep_write_enable;

    /* Extra registers */
    uint8_t extra_ctrl;
    uint16_t pc;

    /* Bank-switched memory */
    uint8_t bank_mem[2][16];
    uint8_t current_bank;
};

static inline int get_bank(uint8_t ctrl)
{
    if ((ctrl & CC_DIAG) && (ctrl & CC_BANK_ONE)) {
        return 2;
    } else if (ctrl & CC_BANK_ONE) {
        return 1;
    }
    return 0;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint8_t bank = s->current_bank;

    if (bank != 0) {
        /* Bank 1 or 2 */
        if (addr >= 16) return 0;
        if (size == 1) {
            val = s->bank_mem[bank-1][addr];
        } else if (size == 2) {
            val = (s->bank_mem[bank-1][addr]) |
                  (s->bank_mem[bank-1][addr+1] << 8);
        } else if (size == 4) {
            val = (s->bank_mem[bank-1][addr]) |
                  (s->bank_mem[bank-1][addr+1] << 8) |
                  (s->bank_mem[bank-1][addr+2] << 16) |
                  (s->bank_mem[bank-1][addr+3] << 24);
        }
        return val;
    }

    /* Bank 0 */
    switch (addr) {
    case 0x00: /* IOP_SIG_WORD */
        if (size == 2) val = s->sig_word;
        else if (size == 1) val = (s->sig_word >> (8*(addr & 1))) & 0xFF;
        break;
    case 0x01: /* IOP_SIG_BYTE */
        if (size == 1) val = s->sig_byte;
        break;
    case 0x02: /* IOP_CONFIG_LOW */
        if (size == 2) val = s->cfg_lsw;
        break;
    case 0x03: /* IOP_VERSION */
        if (size == 1) val = s->chip_version;
        break;
    case 0x04: /* IOP_CONFIG_HIGH */
        if (size == 2) val = s->cfg_msw;
        break;
    case 0x06: /* IOP_EEP_DATA */
        if (size == 2) val = s->eep_data;
        break;
    case 0x07: /* IOP_EEP_CMD */
        if (size == 1) val = s->eep_cmd;
        break;
    case 0x08: /* IOP_RAM_DATA */
        if (s->lram_addr < LRAM_SIZE) {
            val = s->lram[s->lram_addr];
            s->lram_addr++;
        }
        break;
    case 0x0A: /* IOP_RAM_ADDR */
        if (size == 2) val = s->lram_addr;
        break;
    case 0x0B: /* IOP_SYN_OFFSET */
        val = 0;
        break;
    case 0x0C: /* IOP_REG_PC */
        if (size == 2) val = s->pc;
        break;
    case 0x0D: /* IOP_REG_IFC / IOP_EXTRA_CONTROL */
        if (size == 1) val = s->extra_ctrl;
        break;
    case 0x0E: /* IOP_STATUS */
        if (size == 2) val = s->status_reg;
        break;
    case 0x0F: /* IOP_CTRL */
        if (size == 1) val = s->ctrl_reg;
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t bank = s->current_bank;

    if (bank != 0) {
        /* Bank 1 or 2 */
        if (addr >= 16) return;
        if (size == 1) {
            s->bank_mem[bank-1][addr] = val & 0xFF;
        } else if (size == 2) {
            s->bank_mem[bank-1][addr] = val & 0xFF;
            s->bank_mem[bank-1][addr+1] = (val >> 8) & 0xFF;
        } else if (size == 4) {
            s->bank_mem[bank-1][addr] = val & 0xFF;
            s->bank_mem[bank-1][addr+1] = (val >> 8) & 0xFF;
            s->bank_mem[bank-1][addr+2] = (val >> 16) & 0xFF;
            s->bank_mem[bank-1][addr+3] = (val >> 24) & 0xFF;
        }
        return;
    }

    /* Bank 0 */
    switch (addr) {
    case 0x02: /* IOP_CONFIG_LOW */
        if (size == 2) s->cfg_lsw = val & 0xFFFF;
        break;
    case 0x04: /* IOP_CONFIG_HIGH */
        if (size == 2) s->cfg_msw = val & 0xFFFF;
        break;
    case 0x06: /* IOP_EEP_DATA */
        if (size == 2) s->eep_data = val & 0xFFFF;
        break;
    case 0x07: { /* IOP_EEP_CMD */
        if (size == 1) {
            uint8_t cmd = val & 0xFF;
            s->eep_cmd = cmd;
            if (cmd == ASC_EEP_CMD_WRITE_DISABLE) {
                s->eep_write_enable = false;
            } else if (cmd == ASC_EEP_CMD_WRITE_ABLE) {
                s->eep_write_enable = true;
            } else {
                uint8_t addr_eep = cmd & 0x3F;
                if (cmd & 0x80) { /* READ or WRITE */
                    if (cmd & 0x40) { /* WRITE */
                        if (s->eep_write_enable && addr_eep < 64) {
                            s->eeprom[addr_eep] = s->eep_data;
                        }
                    } else { /* READ */
                        if (addr_eep < 64) {
                            s->eep_data = s->eeprom[addr_eep];
                        }
                    }
                }
            }
        }
        break;
    }
    case 0x08: /* IOP_RAM_DATA */
        if (size == 2 && s->lram_addr < LRAM_SIZE) {
            s->lram[s->lram_addr] = val & 0xFFFF;
            s->lram_addr++;
        }
        break;
    case 0x0A: /* IOP_RAM_ADDR */
        if (size == 2) s->lram_addr = val & 0xFFFF;
        break;
    case 0x0C: /* IOP_REG_PC */
        if (size == 2) s->pc = val & 0xFFFF;
        break;
    case 0x0D: /* IOP_REG_IFC / IOP_EXTRA_CONTROL */
        if (size == 1) s->extra_ctrl = val & 0xFF;
        break;
    case 0x0E: /* IOP_STATUS */
        if (size == 2) {
            uint16_t wval = val & 0xFFFF;
            if (wval & CIW_INT_ACK) {
                s->status_reg &= ~CSW_INT_PENDING;
            }
            if (wval & 0x8000) {
                s->status_reg &= ~CSW_SCSI_RESET_LATCH;
            }
            /* other bits ignored */
        }
        break;
    case 0x0F: { /* IOP_CTRL */
        if (size == 1) {
            uint8_t bval = val & 0xFF;
            s->ctrl_reg = bval;
            /* Update halted status */
            if (bval & CC_HALT) {
                s->status_reg |= CSW_HALTED;
            } else {
                s->status_reg &= ~CSW_HALTED;
            }
            /* Recompute bank */
            s->current_bank = get_bank(bval);
        }
        break;
    }
    default:
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

    s->lram_addr = 0;
    memset(s->eeprom, 0, sizeof(s->eeprom));
    s->eep_cmd = 0;
    s->eep_data = 0;
    s->eep_write_enable = false;
    s->extra_ctrl = 0;
    s->pc = 0;
    memset(s->bank_mem, 0, sizeof(s->bank_mem));
    s->current_bank = 0;
    s->status_reg = CSW_HALTED;
    s->ctrl_reg = 0;
    s->cfg_lsw = 0;
    s->cfg_msw = 0;
    s->sig_word = ASC_1000_ID0W;
    s->sig_byte = ASC_1000_ID1B;
    s->chip_version = ASC_CHIP_VER_PCI;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
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
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x10;
    s->bar_info[0].name = "advansys-pio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Allocate internal LRAM */
    s->lram = g_malloc0(LRAM_SIZE * sizeof(uint16_t));
    s->lram_addr = 0;

    /* Initialize EEPROM */
    memset(s->eeprom, 0, sizeof(s->eeprom));
    s->eep_cmd = 0;
    s->eep_data = 0;
    s->eep_write_enable = false;

    /* Extra registers */
    s->extra_ctrl = 0;
    s->pc = 0;

    memset(s->bank_mem, 0, sizeof(s->bank_mem));
    s->current_bank = 0;

    /* Signature */
    s->sig_word = 0x04C1;
    s->sig_byte = 0x25;
    s->chip_version = ASC_CHIP_VER_PCI;

    /* Status: initially halted */
    s->status_reg = CSW_HALTED;
    s->ctrl_reg = 0;

    s->cfg_lsw = 0;
    s->cfg_msw = 0;
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

    g_free(s->lram);
}

static const VMStateDescription vmstate_pcibase = {
    .name = "advansys_pci",
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
