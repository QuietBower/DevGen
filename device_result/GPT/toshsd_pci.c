/*
 * QEMU Toshiba SD Host Controller Emulation (toshsd_pci)
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
/* Removed non-existent "pci_ids.h" include to fix compilation error */

#define TOSHIBA_VENDOR_ID 0x1179

#define TYPE_PCIBASE_DEVICE "toshsd_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define TOSHSD_VENDOR_ID TOSHIBA_VENDOR_ID
#define TOSHSD_DEVICE_ID 0x0805
#define TOSHSD_CLASS_ID  PCI_CLASS_SYSTEM_OTHER

#define SD_ERRORSTATUS0                 0x2c
#define SD_BUF_WRITE_ENABLE             (1U << 25)
#define SD_PCICFG_CARDDETECT            0x4c
#define SD_PCICFG_SDLED_ENABLE2         0xfe
#define SD_PCICFG_SDLED_ENABLE1         0xfa
#define SDIO_BASE                       0x100
#define SD_BUF_CMD_TIMEOUT              (1U << 22)
#define SD_CARD_RESP_END                (1U << 0)
#define SD_CARDCLOCKCTRL                0x24
#define SD_PCICFG_CLKSTOP               0x40
#define SD_PCICFG_LED_ENABLE1_START     0x12
#define SDIO_CLOCKNWAITCTRL             0x38
#define SD_STOPINTERNAL                 0x08
#define SD_INTMASKCARD                  0x20
#define SD_TRANSACTIONCTRL              0x34
#define SD_CARD_RW_END                  (1U << 2)
#define SD_PCICFG_LED_ENABLE2_START     0x80
#define SD_CARDSTATUS                   0x1c
#define SD_CARD_CARD_INSERTED_0         (1U << 4)
#define SD_CARD_CARD_REMOVED_0          (1U << 3)
#define SD_BUF_READ_ENABLE              (1U << 24)
#define SD_PCICFG_CLKSTOP_ENABLE_ALL    0x1f
#define SD_SOFTWARERESET                0xe0
#define SD_PCICFG_PWR1_33V              0x08
#define SD_CARDCLK_DIV_DISABLE          (1U << 15)
#define SD_PCICFG_PWR1_OFF              0x00
#define SD_PCICFG_POWER2                0x49
#define SD_CARDOPT_REQUIRED             0x000e
#define SD_PCICFG_CLKMODE               0x42
#define SD_PCICFG_CLKMODE_DIV_DISABLE   (1U << 0)
#define SD_CARDOPTIONSETUP              0x28
#define SD_CARDOPT_DATA_XFR_WIDTH_1     (1 << 15)
#define SD_PCICFG_POWER1                0x48
#define SD_CARDOPT_DATA_RESP_TIMEOUT(x) (((x) & 0x0f) << 4)
#define HCLK                            33000000
#define SD_CARDOPT_DATA_XFR_WIDTH_4     (0 << 15)
#define SD_PCICFG_PWR2_AUTO             0x02
#define SD_CARDCLK_ENABLE_CLOCK         (1U << 8)
#define SD_CARDOPT_C2_MODULE_ABSENT     (1U << 14)
#define SDIO_LEDCTRL                    0x3e
#define SD_DATAPORT                     0x30
#define SD_RESPONSE3                    0x12
#define SD_RESPONSE7                    0x1a
#define SD_RESPONSE1                    0x0e
#define SD_RESPONSE2                    0x10
#define SD_RESPONSE5                    0x16
#define SD_RESPONSE4                    0x14
#define SD_RESPONSE6                    0x18
#define SD_RESPONSE0                    0x0c
#define SD_ERR1_TIMEOUT_READ_DATA       (1U << 20)
#define SD_ERR0_READ_DATA_CRC_ERR       (1U << 10)
#define SD_ERR1_NO_CMD_RESP             (1U << 16)
#define SD_ERR0_RESP_CMD_ERR            (1U << 0)
#define SD_ERR0_WRITE_CRC_STATUS_END_BIT_ERR (1U << 5)
#define SD_ERR0_READ_DATA_END_BIT_ERR   (1U << 4)
#define SD_ERR1_TIMEOUT_CRS_STATUS      (1U << 21)
#define SD_BUF_CMD_INDEX_ERR            (1U << 16)
#define SD_BUF_STOP_BIT_END_ERR         (1U << 18)
#define SD_ERR0_WRITE_CMD_CRC_ERR       (1U << 11)
#define SD_ERR0_RESP_CMD12_END_BIT_ERR  (1U << 3)
#define SD_BUF_OVERFLOW                 (1U << 20)
#define SD_BUF_UNDERFLOW                (1U << 21)
#define SD_ERR0_RESP_NON_CMD12_END_BIT_ERR (1U << 2)
#define SD_BUF_ILLEGAL_ACCESS           (1U << 31)
#define SD_BUF_DATA_TIMEOUT             (1U << 19)
#define SD_BUF_CRC_ERR                  (1U << 17)
#define SD_ERR1_TIMEOUT_CRC_BUSY        (1U << 22)
#define SD_ERR0_RESP_CMD12_CRC_ERR      (1U << 9)
#define SD_ERR0_RESP_NON_CMD12_CRC_ERR  (1U << 8)
#define SD_CMD_MULTI_BLOCK              (1U << 13)
#define SD_CMD                          0x00
#define SD_CMD_TRANSFER_READ            (1U << 12)
#define SD_CMD_TYPE_ACMD                (1 << 6)
#define SD_CMD_DATA_PRESENT             (1U << 11)
#define SD_ARG0                         0x04
#define SD_STOPINT_ISSUE_CMD12          (1U << 0)
#define SD_CMD_RESP_TYPE_EXT_R1         (4 << 8)
#define SD_CMD_RESP_TYPE_EXT_R1B        (5 << 8)
#define SD_STOPINT_AUTO_ISSUE_CMD12     (1U << 8)
#define SD_CMD_RESP_TYPE_NONE           (3 << 8)
#define SD_CMD_RESP_TYPE_EXT_R2         (6 << 8)
#define SD_CMD_RESP_TYPE_EXT_R3         (7 << 8)
#define SD_CARDXFERDATALEN              0x26
#define SD_BLOCKCOUNT                   0x0a
#define SD_CARD_PRESENT_0               (1U << 5)
#define SD_CARD_WRITE_PROTECT           (1U << 7)
#define SD_CARD_UNK6                    (1U << 6)
#define SD_CARD_PRESENT_3               (1U << 10)
#define SD_BUF_UNK7                     (1U << 23)
#define SD_BUF_CMD_BUSY                 (1U << 30)

#define TOSHSD_BAR0_INDEX 0
#define TOSHSD_BAR0_SIZE  0x1000

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
    uint8_t cfg_space[256];

    uint16_t sd_softwarereset;
    uint16_t sd_cardclockctrl;
    uint32_t sd_cardstatus;
    uint32_t sd_errorstatus0;
    uint16_t sd_stopinternal;
    uint32_t sd_intmaskcard;
    uint16_t sd_transactionctrl;
    uint16_t sd_cardoptionsetup;
    uint16_t sd_blockcount;
    uint16_t sd_cardxferdatalen;

    uint16_t sd_response[8];

    uint16_t sdio_clocknwaitctrl;
    uint16_t sdio_ledctrl;

    uint32_t sd_arg0;
    uint16_t sd_cmd;

    uint32_t *data_buf;
    uint32_t data_buf_len;   /* in bytes */
    uint32_t data_buf_pos;   /* in bytes */

    bool irq_asserted;
};


static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->sd_cardstatus & ~s->sd_intmaskcard) {
        if (!s->irq_asserted) {
            pci_set_irq(pdev, 1);
            s->irq_asserted = true;
        }
    } else {
        if (s->irq_asserted) {
            pci_set_irq(pdev, 0);
            s->irq_asserted = false;
        }
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case SD_CMD:
        if (size == 2) {
            val = s->sd_cmd;
        }
        break;
    case SD_ARG0:
        if (size == 4) {
            val = s->sd_arg0;
        }
        break;
    case SD_CARDSTATUS:
        if (size == 2) {
            val = (uint16_t)(s->sd_cardstatus & 0xFFFF);
        } else if (size == 4) {
            val = s->sd_cardstatus;
        }
        break;
    case SD_ERRORSTATUS0:
        if (size == 4) {
            val = s->sd_errorstatus0;
        }
        break;
    case SD_INTMASKCARD:
        if (size == 4) {
            val = s->sd_intmaskcard;
        }
        break;
    case SD_STOPINTERNAL:
        if (size == 2) {
            val = s->sd_stopinternal;
        }
        break;
    case SD_SOFTWARERESET:
        if (size == 2) {
            val = s->sd_softwarereset;
        }
        break;
    case SD_CARDCLOCKCTRL:
        if (size == 2) {
            val = s->sd_cardclockctrl;
        }
        break;
    case SD_TRANSACTIONCTRL:
        if (size == 2) {
            val = s->sd_transactionctrl;
        }
        break;
    case SD_CARDOPTIONSETUP:
        if (size == 2) {
            val = s->sd_cardoptionsetup;
        }
        break;
    case SD_BLOCKCOUNT:
        if (size == 2) {
            val = s->sd_blockcount;
        }
        break;
    case SD_CARDXFERDATALEN:
        if (size == 2) {
            val = s->sd_cardxferdatalen;
        }
        break;

    case SD_RESPONSE0:
    case SD_RESPONSE1:
    case SD_RESPONSE2:
    case SD_RESPONSE3:
    case SD_RESPONSE4:
    case SD_RESPONSE5:
    case SD_RESPONSE6:
    case SD_RESPONSE7: {
        int idx = (addr - SD_RESPONSE0) / 2;
        if (idx >= 0 && idx < 8 && size == 2) {
            val = s->sd_response[idx];
        }
        break;
    }

    case SDIO_BASE + SDIO_CLOCKNWAITCTRL:
        if (size == 2) {
            val = s->sdio_clocknwaitctrl;
        }
        break;
    case SDIO_BASE + SDIO_LEDCTRL:
        if (size == 2) {
            val = s->sdio_ledctrl;
        }
        break;

    case SD_DATAPORT:
        /* ioread32_rep reads 32-bit words from this port */
        if (size == 4) {
            uint32_t word = 0;
            if (s->data_buf && s->data_buf_pos + 4 <= s->data_buf_len) {
                word = s->data_buf[s->data_buf_pos / 4];
                s->data_buf_pos += 4;
                /* When buffer fully read, clear BUF_READ_ENABLE and raise RW_END */
                if (s->data_buf_pos >= s->data_buf_len) {
                    s->sd_cardstatus &= ~(SD_BUF_READ_ENABLE);
                    s->sd_cardstatus |= SD_CARD_RW_END;
                    pcibase_update_irq(s);
                }
            }
            val = word;
        }
        break;

    default:
        /* unmapped offsets return 0 */
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case SD_CMD:
        if (size == 2) {
            s->sd_cmd = (uint16_t)val;

            /* Very simple command "completion" model:
             * - Populate a fixed non-zero response pattern.
             * - Set CARD_RESP_END and possibly BUF_READ/WRITE_ENABLE.
             */
            s->sd_response[0] = 0x1234;
            s->sd_response[1] = 0x5678;
            s->sd_response[2] = 0x9abc;
            s->sd_response[3] = 0xdef0;
            s->sd_response[4] = 0x1111;
            s->sd_response[5] = 0x2222;
            s->sd_response[6] = 0x3333;
            s->sd_response[7] = 0x4444;

            s->sd_cardstatus |= SD_CARD_RESP_END;

            if (s->sd_cmd & SD_CMD_DATA_PRESENT) {
                if (s->sd_cmd & SD_CMD_TRANSFER_READ) {
                    s->sd_cardstatus |= SD_BUF_READ_ENABLE;
                } else {
                    s->sd_cardstatus |= SD_BUF_WRITE_ENABLE;
                }
            }

            pcibase_update_irq(s);
        }
        break;
    case SD_ARG0:
        if (size == 4) {
            s->sd_arg0 = (uint32_t)val;
        }
        break;
    case SD_CARDSTATUS:
        if (size == 4) {
            uint32_t clr = (uint32_t)val;
            /* Write-1-to-clear model used by driver */
            s->sd_cardstatus &= ~clr;
            pcibase_update_irq(s);
        }
        break;
    case SD_ERRORSTATUS0:
        if (size == 4) {
            s->sd_errorstatus0 = (uint32_t)val;
        }
        break;
    case SD_INTMASKCARD:
        if (size == 4) {
            s->sd_intmaskcard = (uint32_t)val;
            pcibase_update_irq(s);
        }
        break;
    case SD_STOPINTERNAL:
        if (size == 2) {
            s->sd_stopinternal = (uint16_t)val;
        }
        break;
    case SD_SOFTWARERESET:
        if (size == 2) {
            s->sd_softwarereset = (uint16_t)val;
            /* When deasserted to 1 the driver expects registers cleared;
             * we roughly match toshsd_init behavior for card regs.
             */
            if (s->sd_softwarereset == 1) {
                s->sd_cardclockctrl = 0;
                s->sd_cardstatus &= ~(SD_CARD_RESP_END | SD_CARD_RW_END |
                                       SD_BUF_READ_ENABLE | SD_BUF_WRITE_ENABLE |
                                       SD_BUF_CMD_TIMEOUT |
                                       SD_CARD_CARD_INSERTED_0 |
                                       SD_CARD_CARD_REMOVED_0);
                s->sd_errorstatus0 = 0;
                s->sd_stopinternal = 0;
                pcibase_update_irq(s);
            }
        }
        break;
    case SD_CARDCLOCKCTRL:
        if (size == 2) {
            s->sd_cardclockctrl = (uint16_t)val;
        }
        break;
    case SD_TRANSACTIONCTRL:
        if (size == 2) {
            s->sd_transactionctrl = (uint16_t)val;
        }
        break;
    case SD_CARDOPTIONSETUP:
        if (size == 2) {
            s->sd_cardoptionsetup = (uint16_t)val;
        }
        break;
    case SD_BLOCKCOUNT:
        if (size == 2) {
            s->sd_blockcount = (uint16_t)val;
        }
        break;
    case SD_CARDXFERDATALEN:
        if (size == 2) {
            s->sd_cardxferdatalen = (uint16_t)val;
            /* Prepare simple internal data buffer matching requested size */
            s->data_buf_len = (uint32_t)s->sd_blockcount * (uint32_t)s->sd_cardxferdatalen;
            if (s->data_buf_len == 0) {
                s->data_buf_len = s->sd_cardxferdatalen;
            }
            s->data_buf_pos = 0;
            if (s->data_buf) {
                g_free(s->data_buf);
            }
            s->data_buf = g_malloc0((s->data_buf_len + 3) & ~3U);
        }
        break;

    case SD_RESPONSE0:
    case SD_RESPONSE1:
    case SD_RESPONSE2:
    case SD_RESPONSE3:
    case SD_RESPONSE4:
    case SD_RESPONSE5:
    case SD_RESPONSE6:
    case SD_RESPONSE7: {
        int idx = (addr - SD_RESPONSE0) / 2;
        if (idx >= 0 && idx < 8 && size == 2) {
            s->sd_response[idx] = (uint16_t)val;
        }
        break;
    }

    case SDIO_BASE + SDIO_CLOCKNWAITCTRL:
        if (size == 2) {
            s->sdio_clocknwaitctrl = (uint16_t)val;
        }
        break;
    case SDIO_BASE + SDIO_LEDCTRL:
        if (size == 2) {
            s->sdio_ledctrl = (uint16_t)val;
        }
        break;

    case SD_DATAPORT:
        /* iowrite32_rep writes 32-bit words */
        if (size == 4) {
            if (s->data_buf && s->data_buf_pos + 4 <= s->data_buf_len) {
                s->data_buf[s->data_buf_pos / 4] = (uint32_t)val;
                s->data_buf_pos += 4;
                if (s->data_buf_pos >= s->data_buf_len) {
                    s->sd_cardstatus &= ~(SD_BUF_WRITE_ENABLE);
                    s->sd_cardstatus |= SD_CARD_RW_END;
                    pcibase_update_irq(s);
                }
            }
        }
        break;

    default:
        /* ignore writes to undefined locations */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;
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

    memset(s->cfg_space, 0, sizeof(s->cfg_space));

    s->sd_softwarereset = 0;
    s->sd_cardclockctrl = 0;
    s->sd_cardstatus = 0;
    s->sd_errorstatus0 = 0;
    s->sd_stopinternal = 0;
    s->sd_intmaskcard = 0xffffffffU;
    s->sd_transactionctrl = 0;
    s->sd_cardoptionsetup = 0;
    s->sd_blockcount = 0;
    s->sd_cardxferdatalen = 0;
    memset(s->sd_response, 0, sizeof(s->sd_response));
    s->sdio_clocknwaitctrl = 0;
    s->sdio_ledctrl = 0;
    s->sd_arg0 = 0;
    s->sd_cmd = 0;

    if (s->data_buf) {
        g_free(s->data_buf);
        s->data_buf = NULL;
    }
    s->data_buf_len = 0;
    s->data_buf_pos = 0;

    s->irq_asserted = false;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  TOSHSD_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  TOSHSD_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, TOSHSD_CLASS_ID );
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
    s->bar_info[0].index = TOSHSD_BAR0_INDEX;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = TOSHSD_BAR0_SIZE;
    s->bar_info[0].name  = "toshsd-bar0";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    s->data_buf = NULL;
    s->data_buf_len = 0;
    s->data_buf_pos = 0;
    s->irq_asserted = false;
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

    if (s->data_buf) {
        g_free(s->data_buf);
        s->data_buf = NULL;
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
