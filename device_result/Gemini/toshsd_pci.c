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
#include "qemu/bitops.h"

#define TYPE_PCIBASE_DEVICE "toshsd_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#ifndef PCI_VENDOR_ID_TOSHIBA
#define PCI_VENDOR_ID_TOSHIBA 0x1179
#endif

#define SD_CMD			0x00
#define SD_ARG0			0x04
#define SD_BLOCKCOUNT		0x0a
#define SD_RESPONSE0		0x0c
#define SD_RESPONSE1		0x0e
#define SD_RESPONSE2		0x10
#define SD_RESPONSE3		0x12
#define SD_RESPONSE4		0x14
#define SD_RESPONSE5		0x16
#define SD_RESPONSE6		0x18
#define SD_RESPONSE7		0x1a
#define SD_CARDSTATUS		0x1c
#define SD_INTMASKCARD		0x20
#define SD_CARDCLOCKCTRL	0x24
#define SD_CARDXFERDATALEN	0x26
#define SD_CARDOPTIONSETUP	0x28
#define SD_ERRORSTATUS0		0x2c
#define SD_DATAPORT		0x30
#define SD_TRANSACTIONCTRL	0x34
#define SDIO_CLOCKNWAITCTRL	0x38
#define SDIO_LEDCTRL		0x3e
#define SD_PCICFG_CLKSTOP	0x40
#define SD_PCICFG_CLKMODE	0x42
#define SD_PCICFG_POWER1	0x48
#define SD_PCICFG_POWER2	0x49
#define SD_PCICFG_CARDDETECT	0x4c
#define SD_SOFTWARERESET	0xe0
#define SD_PCICFG_SDLED_ENABLE1	0xfa
#define SD_PCICFG_SDLED_ENABLE2	0xfe

#define SD_BUF_CMD_TIMEOUT	BIT(22)
#define SD_BUF_CRC_ERR		BIT(17)
#define SD_BUF_ILLEGAL_ACCESS	BIT(31)
#define SD_BUF_CMD_INDEX_ERR	BIT(16)
#define SD_BUF_STOP_BIT_END_ERR	BIT(18)
#define SD_BUF_OVERFLOW		BIT(20)
#define SD_BUF_UNDERFLOW	BIT(21)
#define SD_BUF_DATA_TIMEOUT	BIT(19)

#define SD_ERR0_RESP_CMD_ERR			BIT(0)
#define SD_ERR0_RESP_NON_CMD12_END_BIT_ERR	BIT(2)
#define SD_ERR0_RESP_CMD12_END_BIT_ERR		BIT(3)
#define SD_ERR0_READ_DATA_END_BIT_ERR		BIT(4)
#define SD_ERR0_WRITE_CRC_STATUS_END_BIT_ERR	BIT(5)
#define SD_ERR0_RESP_NON_CMD12_CRC_ERR		BIT(8)
#define SD_ERR0_RESP_CMD12_CRC_ERR		BIT(9)
#define SD_ERR0_READ_DATA_CRC_ERR		BIT(10)
#define SD_ERR0_WRITE_CMD_CRC_ERR		BIT(11)

#define SD_ERR1_NO_CMD_RESP		BIT(16)
#define SD_ERR1_TIMEOUT_READ_DATA	BIT(20)
#define SD_ERR1_TIMEOUT_CRS_STATUS	BIT(21)
#define SD_ERR1_TIMEOUT_CRC_BUSY	BIT(22)

#define SD_CARD_RESP_END	BIT(0)
#define SD_CARD_CARD_INSERTED_0	BIT(4)
#define SD_CARD_CARD_REMOVED_0	BIT(3)

#define SD_BUF_READ_ENABLE	BIT(24)
#define SD_BUF_WRITE_ENABLE	BIT(25)

#define SD_CARD_RW_END		BIT(2)

#define SD_STOPINT_ISSUE_CMD12		BIT(0)
#define SD_STOPINTERNAL		0x08

#define SD_CMD_RESP_TYPE_NONE		(3 << 8)
#define SD_CMD_RESP_TYPE_EXT_R1		(4 << 8)
#define SD_CMD_RESP_TYPE_EXT_R1B	(5 << 8)
#define SD_CMD_RESP_TYPE_EXT_R2		(6 << 8)
#define SD_CMD_RESP_TYPE_EXT_R3		(7 << 8)

#define SD_CMD_TYPE_ACMD		(1 << 6)
#define SD_CMD_DATA_PRESENT		BIT(11)
#define SD_STOPINT_AUTO_ISSUE_CMD12	BIT(8)
#define SD_CMD_MULTI_BLOCK		BIT(13)
#define SD_CMD_TRANSFER_READ		BIT(12)

#define SDIO_BASE		0x100
#define SD_CARD_PRESENT_0	BIT(5)
#define SD_CARD_WRITE_PROTECT	BIT(7)
#define SD_CARD_PRESENT_3	BIT(10)
#define SD_CARD_UNK6		BIT(6)
#define SD_BUF_UNK7		BIT(23)
#define SD_BUF_CMD_BUSY		BIT(30)

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
    uint32_t intmaskcard;
    uint32_t cardstatus;
    uint32_t errorstatus0;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint16_t sd_cmd;
    uint32_t sd_arg0;
    uint16_t sd_blockcount;
    uint16_t sd_response[8];
    uint32_t sd_cardclockctrl;
    uint16_t sd_cardxferdatalen;
    uint16_t sd_cardoptionsetup;
    uint32_t sd_dataport;
    uint32_t sd_transactionctrl;
    uint32_t sdio_clocknwaitctrl;
    uint16_t sdio_ledctrl;
    uint16_t sd_pcicfg_clkstop;
    uint16_t sd_pcicfg_clkmode;
    uint8_t sd_pcicfg_power1;
    uint8_t sd_pcicfg_power2;
    uint32_t sd_pcicfg_carddetect;
    uint8_t sd_softwarereset;
    uint8_t sd_pcicfg_sdled_enable1;
    uint8_t sd_pcicfg_sdled_enable2;
    uint16_t sd_stopinternal;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t active = s->cardstatus & ~s->intmaskcard;

    if (active) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case SD_RESPONSE0:
    case SD_RESPONSE1:
    case SD_RESPONSE2:
    case SD_RESPONSE3:
    case SD_RESPONSE4:
    case SD_RESPONSE5:
    case SD_RESPONSE6:
    case SD_RESPONSE7:
        if (size == 2) {
            val = s->sd_response[(addr - SD_RESPONSE0) / 2];
        }
        break;
    case SD_CARDSTATUS:
        val = s->cardstatus;
        break;
    case SD_INTMASKCARD:
        val = s->intmaskcard;
        break;
    case SD_ERRORSTATUS0:
        val = s->errorstatus0;
        break;
    case SD_DATAPORT:
        val = s->sd_dataport;
        s->cardstatus |= SD_CARD_RW_END;
        pcibase_update_irq(s);
        break;
    default:
        break;
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case SD_CMD:
        s->sd_cmd = val;
        s->cardstatus |= SD_CARD_RESP_END;
        if (val & SD_CMD_DATA_PRESENT) {
            if (val & SD_CMD_TRANSFER_READ) {
                s->cardstatus |= SD_BUF_READ_ENABLE;
            } else {
                s->cardstatus |= SD_BUF_WRITE_ENABLE;
            }
        }
        pcibase_update_irq(s);
        break;
    case SD_ARG0:
        s->sd_arg0 = val;
        break;
    case SD_BLOCKCOUNT:
        s->sd_blockcount = val;
        break;
    case SD_CARDSTATUS:
        s->cardstatus &= val; /* W0C */
        pcibase_update_irq(s);
        break;
    case SD_INTMASKCARD:
        s->intmaskcard = val;
        pcibase_update_irq(s);
        break;
    case SD_CARDCLOCKCTRL:
        s->sd_cardclockctrl = val;
        break;
    case SD_CARDXFERDATALEN:
        s->sd_cardxferdatalen = val;
        break;
    case SD_CARDOPTIONSETUP:
        s->sd_cardoptionsetup = val;
        break;
    case SD_ERRORSTATUS0:
        s->errorstatus0 = val;
        break;
    case SD_DATAPORT:
        s->sd_dataport = val;
        s->cardstatus |= SD_CARD_RW_END;
        pcibase_update_irq(s);
        break;
    case SD_TRANSACTIONCTRL:
        s->sd_transactionctrl = val;
        break;
    case SD_SOFTWARERESET:
        s->sd_softwarereset = val;
        break;
    case SD_STOPINTERNAL:
        s->sd_stopinternal = val;
        if (val & SD_STOPINT_ISSUE_CMD12) {
            s->cardstatus |= SD_CARD_RESP_END;
            pcibase_update_irq(s);
        }
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

    s->cardstatus = SD_CARD_PRESENT_0;
    s->intmaskcard = 0xFFFFFFFF;
    s->errorstatus0 = 0;
    s->sd_cmd = 0;
    s->sd_arg0 = 0;
    s->sd_blockcount = 0;
    memset(s->sd_response, 0, sizeof(s->sd_response));
    s->sd_cardclockctrl = 0;
    s->sd_cardxferdatalen = 0;
    s->sd_cardoptionsetup = 0;
    s->sd_dataport = 0;
    s->sd_transactionctrl = 0;
    s->sdio_clocknwaitctrl = 0;
    s->sdio_ledctrl = 0;
    s->sd_pcicfg_clkstop = 0;
    s->sd_pcicfg_clkmode = 0;
    s->sd_pcicfg_power1 = 0;
    s->sd_pcicfg_power2 = 0;
    s->sd_pcicfg_carddetect = 0;
    s->sd_softwarereset = 0;
    s->sd_pcicfg_sdled_enable1 = 0;
    s->sd_pcicfg_sdled_enable2 = 0;
    s->sd_stopinternal = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_TOSHIBA );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0805 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
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
    s->bar_info[0].size = 0x1000; /* 4KB default */
    s->bar_info[0].name = "toshsd-mmio";
      
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "toshsd_pci",
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
