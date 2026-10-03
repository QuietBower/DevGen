/*
 * QEMU PCI device emulation for RTL8192SE wireless LAN controller
 * Based on Linux driver rtl8192se/sw.c and partial headers.
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

#define TYPE_PCIBASE_DEVICE "rtl8192se_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x10ec   /* PCI_VENDOR_ID_REALTEK */
#define DEVICE_ID 0x8192
#define CLASS_ID PCI_CLASS_NETWORK_OTHER

/* Register offsets (from sw.c and associated headers) */
#define REG_SYS_ISO_CTRL        0x0000
#define REG_SYS_FUNC_EN         0x0002
#define PMC_FSM                 0x0004
#define SYS_CLKR                0x0008
#define EPROM_CMD               0x000A
#define SPS1_CTRL               0x0018
#define RF_CTRL                 0x001F
#define AFE_XTAL_CTRL           0x0026
#define REG_EFUSE_CTRL          0x0030
#define REG_EFUSE_TEST          0x0034
#define CMDR                    0x0040
#define TXPAUSE                 0x0042
#define RCR                     0x0048
#define BSSIDR                  0x0058
#define SIFS_OFDM               0x008E
#define SLOT_TIME               0x0089
#define BCN_INTERVAL            0x0094
#define ATIMWND                 0x0096
#define BCN_DRV_EARLY_INT       0x0098
#define BCN_DMATIME             0x009A
#define BCN_ERR_THRESH          0x009C
#define TXDESC_MSK              0x00DC
#define FW_RSVD_PG_CRTL         0x00D8
#define INIRTSMCS_SEL           0x0180
#define RRSR                    0x0181
#define ARFR0                   0x0184
#define AGGLEN_LMT_H            0x01A7
#define AGGLEN_LMT_L            0x01A8
#define ACMHWCTRL               0x01E7
#define RETRY_LIMIT             0x01F4
#define SG_RATE                 0x01F6
#define BW_OPMODE               0x0203
#define REG_RWCAM               0x0240
#define REG_WCAMI               0x0244
#define REG_RCAMO               0x0248
#define REG_SECR                0x0250
#define WFM5                    0x02C0
#define WFM3                    0x02A0
#define MAC_PINMUX_CFG          0x02F1
#define REG_EFUSE_CLK           0x02F8
#define INTA_MASK               0x0300
#define REG_CAMDBG              0x067C
#define PHY_CCA                 0x803
#define AMPDU_MIN_SPACE         0x0237
#define REG_MACID               0x0610

/* EFUSE constants */
#define EFUSE_MAX_SECTION             16
#define EFUSE_REAL_CONTENT_LEN        512
#define EFUSE_OOB_PROTECT_BYTES       15

/* RCR bit masks */
#define RCR_AAP                 BIT(0)
#define RCR_AM                  BIT(2)
#define RCR_AB                  BIT(3)
#define RCR_ACRC32              BIT(8)
#define RCR_ACF                 BIT(12)
#define RCR_APP_PHYST_STAFF     BIT(24)

/* SCR bit masks */
#define SCR_RXENCENABLE         BIT(3)

/* GPIO mux bit */
#define GPIOMUX_EN              BIT(3)

/* Interrupt mask bits */
#define IMR_BCNDMAINT6          BIT(31)
#define IMR_BCNDMAINT5          BIT(30)
#define IMR_BCNDMAINT4          BIT(29)
#define IMR_BCNDMAINT3          BIT(28)
#define IMR_BCNDMAINT2          BIT(27)
#define IMR_BCNDMAINT1          BIT(26)
#define IMR_BCNDOK8             BIT(25)
#define IMR_BCNDOK7             BIT(24)
#define IMR_BCNDOK6             BIT(23)
#define IMR_BCNDOK5             BIT(22)
#define IMR_BCNDOK4             BIT(21)
#define IMR_BCNDOK3             BIT(20)
#define IMR_BCNDOK2             BIT(19)
#define IMR_BCNDOK1             BIT(18)
#define IMR_ATIMEND             BIT(17)
#define IMR_BDOK                BIT(16)
#define IMR_HIGHDOK             BIT(15)
#define IMR_MGNTDOK             BIT(14)
#define IMR_TIMEOUT2            BIT(13)
#define IMR_TIMEOUT1            BIT(12)
#define IMR_PSTIMEOUT           BIT(11)
#define IMR_TXFOVW              BIT(10)
#define IMR_RXFOVW              BIT(9)
#define IMR_RDU                 BIT(8)
#define IMR_ROK                 BIT(7)
#define IMR_VODOK               BIT(6)
#define IMR_VIDOK               BIT(5)
#define IMR_BEDOK               BIT(4)
#define IMR_BKDOK               BIT(3)
#define IMR_TBDOK               BIT(2)
#define IMR_TBDER               BIT(1)
#define IMR_BCNINT              BIT(0)

/* Additional interrupt bits likely used by driver (not defined explicitly in sw.c but referenced) */
#define IMR_HCCADOK             BIT(8)   /* placeholder */
#define IMR_COMDOK              BIT(14)  /* placeholder */
#define IMR_RXCMDOK             BIT(7)   /* placeholder */

/* Descriptor rate constants */
#define DESC_RATE1M              0x00
#define DESC_RATE2M              0x01
#define DESC_RATE5_5M            0x02
#define DESC_RATE11M             0x03
#define DESC_RATE6M              0x04
#define DESC_RATE9M              0x05
#define DESC_RATE12M             0x06
#define DESC_RATE18M             0x07
#define DESC_RATE24M             0x08
#define DESC_RATE36M             0x09
#define DESC_RATE48M             0x0a
#define DESC_RATE54M             0x0b
#define DESC_RATEMCS7            0x13

/* Encryption type constants for CAM */
#define CAM_NONE                 0x0
#define CAM_WEP40                0x01
#define CAM_TKIP                 0x02
#define CAM_AES                  0x04
#define CAM_WEP104               0x05

/* Hardware settings maximum size */
#define HWSET_MAX_SIZE 128

/* BAR0 size: inferred from register offsets up to 0x1000, set to 0x4000 for safety */
#define BAR0_SIZE 0x4000

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

/* Precomputed EFUSE data with a valid MAC address and typical header */
static const uint8_t efuse_data[EFUSE_REAL_CONTENT_LEN] = {
    /* First 16 bytes: typical header for RTL8192SE EFUSE */
    [0x00] = 0x29, [0x01] = 0x81, /* Magic word */
    [0x02] = 0x00, [0x03] = 0x02, /* Version / layout? */
    [0x04] = 0x00, [0x05] = 0x00, [0x06] = 0x00, [0x07] = 0x00,
    [0x08] = 0x00, [0x09] = 0x00, [0x0A] = 0x00, [0x0B] = 0x00,
    [0x0C] = 0x00, [0x0D] = 0x00, [0x0E] = 0x00, [0x0F] = 0x00,
    [0x10] = 0x00, [0x11] = 0xe0, [0x12] = 0x4c, [0x13] = 0x81, /* MAC */
    [0x14] = 0x92, [0x15] = 0x00,                    /* MAC */
    /* Fill rest with 0xff for unused (EFUSE empty state) */
    /* We'll leave the remaining bytes as zeros, which is typical for unprogrammed */
    /* Alternatively we could explicitly set them to 0xff, but zeros are fine for now. */
};

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_mask_low;  /* Mask for lower 32 interrupts at offset 0x300 (INTA_MASK) */
    uint32_t intr_mask_high; /* Mask for upper interrupts (bits 32-37) at offset 0x304 */

    /* MMIO shadow: holds all register values */
    uint8_t *mmio;

    /* EFUSE state */
    uint16_t efuse_addr;    /* current address set by writing to EFUSE_CTRL (0x30) */

    uint32_t status;      /* Operational status flags */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* For now, no pending interrupts are generated, keep IRQ deasserted.
       This will be expanded in future iterations when interrupt sources are defined. */
    pci_set_irq(pdev, 0);
}

/* MMIO Read Handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = ~0ULL;

    if (addr + size > BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read beyond BAR size: addr=0x%" HWADDR_PRIx " size=%u\n",
                      __func__, addr, size);
        return 0;
    }

    /* Handle special registers with side effects */
    switch (addr) {
    case PMC_FSM:        /* 0x0004 - Version register, return VERSION_8192S_ACUT (0x0) (upper 16 bits) */
        if (size == 4) val = 0x00000000;
        else val = 0;
        break;
    case EPROM_CMD:      /* 0x000A - EPROM command/status, return autoload OK (BIT5) and no EEPROM (BIT4) */
        if (size == 2) val = 0x0020;
        else if (size == 4) val = 0x00000020;
        else val = 0;
        break;
    case INTA_MASK:      /* 0x300 - Interrupt Mask Register low 32 bits */
        if (size == 4) val = s->intr_mask_low;
        else val = 0;
        break;
    case INTA_MASK + 4:  /* 0x304 - Interrupt Mask Register high bits */
        if (size == 4) val = s->intr_mask_high;
        else val = 0;
        break;
    case REG_EFUSE_TEST: /* 0x34 - EFUSE data read */
        if (size == 4) {
            /* Return the byte from efuse_data at the current address, extended to 32 bits. */
            uint32_t idx = s->efuse_addr & (EFUSE_REAL_CONTENT_LEN - 1);
            if (idx < EFUSE_REAL_CONTENT_LEN) {
                val = efuse_data[idx];
            } else {
                val = 0;
            }
        } else {
            val = 0;
        }
        break;
    default:
        /* Generic read from shadow MMIO */
        if (size == 1) {
            val = s->mmio[addr];
        } else if (size == 2) {
            val = lduw_le_p(s->mmio + addr);
        } else if (size == 4) {
            val = ldl_le_p(s->mmio + addr);
        } else if (size == 8) {
            val = ldq_le_p(s->mmio + addr);
        } else {
            val = 0;
        }
        break;
    }

    return val;
}

/* MMIO Write Handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write beyond BAR size: addr=0x%" HWADDR_PRIx " size=%u val=0x%" PRIx64 "\n",
                      __func__, addr, size, val);
        return;
    }

    /* Handle special registers */
    switch (addr) {
    case INTA_MASK:      /* 0x300 - Interrupt Mask Register low 32 bits */
        if (size == 4) {
            s->intr_mask_low = val;
            pcibase_update_irq(s);
        }
        break;
    case INTA_MASK + 4:  /* 0x304 - Interrupt Mask Register high bits */
        if (size == 4) {
            s->intr_mask_high = val & 0x3F;
            pcibase_update_irq(s);
        }
        break;
    case REG_EFUSE_CTRL: /* 0x30 - EFUSE address/command */
        if (size == 4) {
            /* Simply store low 9 bits as address (up to 512 bytes) */
            s->efuse_addr = val & 0x1FF;
        }
        break;
    case REG_EFUSE_TEST: /* 0x34 - EFUSE write (unlikely but handled) */
        /* Ignore writes; EFUSE is read-only for us */
        break;
    default:
        /* Generic write to shadow MMIO, with some exceptions */
        if (addr == EPROM_CMD) {
            /* EPROM_CMD is read-only, ignore writes */
        } else if (size == 1) {
            s->mmio[addr] = (uint8_t)val;
        } else if (size == 2) {
            stw_le_p(s->mmio + addr, (uint16_t)val);
        } else if (size == 4) {
            stl_le_p(s->mmio + addr, (uint32_t)val);
        } else if (size == 8) {
            stq_le_p(s->mmio + addr, val);
        }
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset all registers to 0 */
    memset(s->mmio, 0, BAR0_SIZE);
    s->intr_mask_low = 0;
    s->intr_mask_high = 0;
    s->efuse_addr = 0;
    s->status = 0;
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

    /* Set subsystem vendor and device to match primary IDs (driver may expect this) */
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, DEVICE_ID);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize MSI capability to allow driver to use MSI interrupts */
    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        error_setg(errp, "MSI initialization failed");
        return;
    }

    /* Allocate MMIO shadow */
    s->mmio = g_malloc0(BAR0_SIZE);

    /* Initialize BAR0 (index 0) as MMIO */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "rtl8192se-mmio";
    s->num_bars = 1;

    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);
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

    g_free(s->mmio);
    s->mmio = NULL;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "rtl8192se_pci",
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
