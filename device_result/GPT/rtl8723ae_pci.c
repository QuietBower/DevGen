/*
 * QEMU PCI device model for rtl8723ae_pci
 * Phase 2: Basic behavioral logic implemented based strictly on provided driver snippets.
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

/* Note: Including kernel headers in QEMU is not valid; keep defines local instead. */
/* Re-declare only required PCI IDs locally to avoid build issues. */
#ifndef PCI_VENDOR_ID_REALTEK
#define PCI_VENDOR_ID_REALTEK 0x10ec
#endif
#ifndef PCI_CLASS_NETWORK_OTHER
#define PCI_CLASS_NETWORK_OTHER 0x0280
#endif

#define TYPE_PCIBASE_DEVICE "rtl8723ae_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define RTL8723AE_VENDOR_ID    PCI_VENDOR_ID_REALTEK
#define RTL8723AE_DEVICE_ID    0x8723
#define RTL8723AE_CLASS_ID     PCI_CLASS_NETWORK_OTHER

/* BAR index used by driver (rtl_hal_cfg.bar_id) */
#define RTL8723AE_PCI_BAR_ID   2

/* Static register offsets and bit definitions from driver */
#define REG_SYS_ISO_CTRL            0x0000
#define REG_SYS_FUNC_EN             0x0002
#define REG_SYS_CLKR                0x0008
#define REG_HIMR                    0x00B0
#define REG_HIMRE                   0x00B8
#define REG_EFUSE_CTRL              0x0030
#define REG_EFUSE_TEST              0x0034
#define REG_CAMCMD                  0x0670
#define REG_CAMWRITE                0x0674
#define REG_CAMREAD                 0x0678
#define REG_CAMDBG                  0x067C
#define REG_SECCFG                  0x0680

#define SYS_CLK                     0x03
#define EFUSE_TEST                  REG_EFUSE_TEST
#define EFUSE_CTRL                  REG_EFUSE_CTRL
#define RWCAM                       REG_CAMCMD
#define WCAMI                       REG_CAMWRITE

#ifndef BIT
#define BIT(nr) (1U << (nr))
#endif

#define PWC_EV12V                   BIT(15)
#define FEN_ELDR                    BIT(12)
#define LOADER_CLK_EN               BIT(5)
#define ANA8M                       BIT(1)

#define HWSET_MAX_SIZE              512
#define EFUSE_MAX_SECTION           64
#define EFUSE_REAL_CONTENT_LEN      256
#define EFUSE_OOB_PROTECT_BYTES     18

#define CAM_NONE                    0x0
#define CAM_WEP40                   0x01
#define CAM_TKIP                    0x02
#define CAM_AES                     0x04
#define CAM_WEP104                  0x05

#define IMR_BCNDMAINT6              BIT(26)
#define IMR_BCNDMAINT5              BIT(25)
#define IMR_BCNDMAINT4              BIT(24)
#define IMR_BCNDMAINT3              BIT(23)
#define IMR_BCNDMAINT2              BIT(22)
#define IMR_BCNDMAINT1              BIT(21)
#define IMR_BCNDMAINT0              BIT(20)

#define IMR_BCNDOK8                 BIT(25)
#define IMR_BCNDOK7                 BIT(20)
#define IMR_BCNDOK6                 BIT(19)
#define IMR_BCNDOK5                 BIT(18)
#define IMR_BCNDOK4                 BIT(17)
#define IMR_BCNDOK3                 BIT(16)
#define IMR_BCNDOK2                 BIT(15)
#define IMR_BCNDOK1                 BIT(14)
#define IMR_BCNDOK0                 BIT(16)

#define IMR_TIMEOUT2                BIT(17)
#define IMR_TIMEOUT1                BIT(16)
#define IMR_TXFOVW                  BIT(9)
#define IMR_PSTIMEOUT               BIT(29)
#define IMR_BCNINT                  BIT(13)
#define IMR_RXFOVW                  BIT(8)
#define IMR_RDU                     BIT(1)
#define IMR_ATIMEND                 BIT(12)
#define IMR_BDOK                    BIT(9)
#define IMR_MGNTDOK                 BIT(6)
#define IMR_TBDER                   BIT(26)
#define IMR_HIGHDOK                 BIT(7)
#define IMR_TBDOK                   BIT(25)
#define IMR_BKDOK                   BIT(5)
#define IMR_BEDOK                   BIT(4)
#define IMR_VIDOK                   BIT(3)
#define IMR_VODOK                   BIT(2)
#define IMR_ROK                     BIT(0)

#define AM                          BIT(2)
#define AB                          BIT(3)
#define ACRC32                      BIT(8)
#define ACF                         BIT(12)
#define AAP                         BIT(0)

#define PHIMR_TXFOVW                BIT(9)
#define PHIMR_PSTIMEOUT             BIT(29)
#define PHIMR_BCNDMAINT0            BIT(20)
#define PHIMR_RXFOVW                BIT(8)
#define PHIMR_RDU                   BIT(1)
#define PHIMR_ATIMEND_E             BIT(13)
#define PHIMR_BCNDOK0               BIT(16)
#define PHIMR_MGNTDOK               BIT(6)
#define PHIMR_TXBCNERR              BIT(26)
#define PHIMR_HIGHDOK               BIT(7)
#define PHIMR_TXBCNOK               BIT(25)
#define PHIMR_BKDOK                 BIT(5)
#define PHIMR_BEDOK                 BIT(4)
#define PHIMR_VIDOK                 BIT(3)
#define PHIMR_VODOK                 BIT(2)
#define PHIMR_ROK                   BIT(0)
#define PHIMR_C2HCMD                BIT(10)

/* Simplified interrupt mask array indices (from rtlpci->irq_mask[0/1]) */
#define RTL_IRQ_MASK0_DEFAULT (PHIMR_ROK | PHIMR_RDU | PHIMR_VODOK | \
                               PHIMR_VIDOK | PHIMR_BEDOK | PHIMR_BKDOK | \
                               PHIMR_MGNTDOK | PHIMR_HIGHDOK | \
                               PHIMR_C2HCMD | PHIMR_TXBCNOK | \
                               PHIMR_PSTIMEOUT)
#define RTL_IRQ_MASK1_DEFAULT (PHIMR_RXFOVW)


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
    uint8_t  regs8[0x1000];   /* Simple backing store for MMIO space up to BAR size */

    /* Interrupt-related registers */
    uint32_t himr;      /* Interrupt Mask Register (REG_HIMR) */
    uint32_t himre;     /* Interrupt Mask Extension (REG_HIMRE) */
    uint32_t hisr;      /* Interrupt Status (not explicitly defined but implied) */
    uint32_t hisre;     /* Interrupt Status Extension (implied) */

    /* EFUSE / security related shadows */
    uint32_t efuse_ctrl;
    uint32_t efuse_test;

    uint16_t sys_iso_ctrl;
    uint16_t sys_func_en;
    uint8_t  sys_clkr;

    uint32_t camcmd;
    uint32_t camwrite;
    uint32_t camread;
    uint32_t camdbg;
    uint32_t seccfg;
};

/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Determine if any enabled interrupt is pending. The driver uses
     * rtl8723e_interrupt_recognized/rtl8723e_update_interrupt_mask, but
     * their implementation is not provided, so we only implement a minimal
     * level-sensitive IRQ line based on local status/mask shadows.
     */

    uint32_t pending0 = s->hisr & s->himr;
    uint32_t pending1 = s->hisre & s->himre;

    if (pending0 || pending1) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns - not used here
 * because no explicit DMA register programming is shown in provided snippet.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Basic bounds check to avoid out-of-range access. */
    if (addr + size > sizeof(s->regs8)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "rtl8723ae_pci: mmio_read out of range addr=0x%" HWADDR_PRIx " size=%u\n",
                      addr, size);
        return 0;
    }

    /* Handle some registers with specific semantics first */
    switch (addr) {
    case REG_SYS_ISO_CTRL: /* 0x0000, 16-bit */
        if (size == 1) {
            uint16_t v = s->sys_iso_ctrl;
            return (addr & 1) ? (v >> 8) & 0xff : v & 0xff;
        } else if (size == 2 || size == 4) {
            return s->sys_iso_ctrl;
        }
        break;
    case REG_SYS_FUNC_EN: /* 0x0002, 16-bit */
        if (size == 1) {
            uint16_t v = s->sys_func_en;
            return (addr & 1) ? (v >> 8) & 0xff : v & 0xff;
        } else if (size == 2 || size == 4) {
            return s->sys_func_en;
        }
        break;
    case REG_SYS_CLKR: /* 0x0008, 8-bit */
        if (size == 1) {
            return s->sys_clkr;
        }
        break;
    case REG_HIMR: /* Interrupt mask */
        if (size == 4) {
            return s->himr;
        }
        break;
    case REG_HIMRE: /* Interrupt mask extension */
        if (size == 4) {
            return s->himre;
        }
        break;
    case REG_EFUSE_CTRL:
        if (size == 4) {
            return s->efuse_ctrl;
        }
        break;
    case REG_EFUSE_TEST:
        if (size == 4) {
            return s->efuse_test;
        }
        break;
    case REG_CAMCMD:
        if (size == 4) {
            return s->camcmd;
        }
        break;
    case REG_CAMWRITE:
        if (size == 4) {
            return s->camwrite;
        }
        break;
    case REG_CAMREAD:
        if (size == 4) {
            return s->camread;
        }
        break;
    case REG_CAMDBG:
        if (size == 4) {
            return s->camdbg;
        }
        break;
    case REG_SECCFG:
        if (size == 4) {
            return s->seccfg;
        }
        break;
    default:
        break;
    }

    /* Generic register array access for others */
    uint64_t val = 0;
    switch (size) {
    case 1:
        val = s->regs8[addr];
        break;
    case 2:
        val = s->regs8[addr] | ((uint16_t)s->regs8[addr + 1] << 8);
        break;
    case 4:
        val = s->regs8[addr] |
              ((uint32_t)s->regs8[addr + 1] << 8) |
              ((uint32_t)s->regs8[addr + 2] << 16) |
              ((uint32_t)s->regs8[addr + 3] << 24);
        break;
    default:
        /* Unsupported size - return 0 */
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > sizeof(s->regs8)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "rtl8723ae_pci: mmio_write out of range addr=0x%" HWADDR_PRIx " size=%u\n",
                      addr, size);
        return;
    }

    /* Handle registers with explicit semantics first */
    switch (addr) {
    case REG_SYS_ISO_CTRL: /* 16-bit */
        if (size == 1) {
            if (addr & 1) {
                s->sys_iso_ctrl = (s->sys_iso_ctrl & 0x00ff) | ((uint16_t)(val & 0xff) << 8);
            } else {
                s->sys_iso_ctrl = (s->sys_iso_ctrl & 0xff00) | (uint16_t)(val & 0xff);
            }
        } else if (size == 2 || size == 4) {
            s->sys_iso_ctrl = (uint16_t)val;
        }
        break;
    case REG_SYS_FUNC_EN: /* 16-bit */
        if (size == 1) {
            if (addr & 1) {
                s->sys_func_en = (s->sys_func_en & 0x00ff) | ((uint16_t)(val & 0xff) << 8);
            } else {
                s->sys_func_en = (s->sys_func_en & 0xff00) | (uint16_t)(val & 0xff);
            }
        } else if (size == 2 || size == 4) {
            s->sys_func_en = (uint16_t)val;
        }
        break;
    case REG_SYS_CLKR: /* 8-bit */
        if (size == 1) {
            s->sys_clkr = (uint8_t)val;
        } else if (size == 2 || size == 4) {
            s->sys_clkr = (uint8_t)val;
        }
        break;
    case REG_HIMR: /* Interrupt mask */
        if (size == 4) {
            s->himr = (uint32_t)val;
            pcibase_update_irq(s);
        }
        break;
    case REG_HIMRE: /* Interrupt mask extension */
        if (size == 4) {
            s->himre = (uint32_t)val;
            pcibase_update_irq(s);
        }
        break;
    case REG_EFUSE_CTRL:
        if (size == 4) {
            s->efuse_ctrl = (uint32_t)val;
        }
        break;
    case REG_EFUSE_TEST:
        if (size == 4) {
            s->efuse_test = (uint32_t)val;
        }
        break;
    case REG_CAMCMD:
        if (size == 4) {
            s->camcmd = (uint32_t)val;
        }
        break;
    case REG_CAMWRITE:
        if (size == 4) {
            s->camwrite = (uint32_t)val;
        }
        break;
    case REG_CAMREAD:
        if (size == 4) {
            s->camread = (uint32_t)val;
        }
        break;
    case REG_CAMDBG:
        if (size == 4) {
            s->camdbg = (uint32_t)val;
        }
        break;
    case REG_SECCFG:
        if (size == 4) {
            s->seccfg = (uint32_t)val;
        }
        break;
    default:
        break;
    }

    /* Update generic register backing store */
    switch (size) {
    case 1:
        s->regs8[addr] = (uint8_t)val;
        break;
    case 2:
        s->regs8[addr]     = (uint8_t)(val & 0xff);
        s->regs8[addr + 1] = (uint8_t)((val >> 8) & 0xff);
        break;
    case 4:
        s->regs8[addr]     = (uint8_t)(val & 0xff);
        s->regs8[addr + 1] = (uint8_t)((val >> 8) & 0xff);
        s->regs8[addr + 2] = (uint8_t)((val >> 16) & 0xff);
        s->regs8[addr + 3] = (uint8_t)((val >> 24) & 0xff);
        break;
    default:
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
    /* No legacy port I/O defined in provided driver snippet. */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;
    /* No legacy port I/O defined in provided driver snippet. */
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

    /* Revert registers to power-on defaults that are implied by driver usage. */

    memset(s->regs8, 0, sizeof(s->regs8));

    s->sys_iso_ctrl = 0;
    s->sys_func_en = 0;
    s->sys_clkr = 0;

    s->efuse_ctrl = 0;
    s->efuse_test = 0;

    s->camcmd = 0;
    s->camwrite = 0;
    s->camread = 0;
    s->camdbg = 0;
    s->seccfg = 0;

    /* Interrupt masks initialized to defaults used in rtl8723e_init_sw_vars */
    s->himr = RTL_IRQ_MASK0_DEFAULT;
    s->himre = RTL_IRQ_MASK1_DEFAULT;
    s->hisr = 0;
    s->hisre = 0;

    /* Mirror into generic register space */
    *(uint32_t *)&s->regs8[REG_HIMR] = s->himr;
    *(uint32_t *)&s->regs8[REG_HIMRE] = s->himre;

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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  RTL8723AE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  RTL8723AE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, RTL8723AE_CLASS_ID );
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
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }
    s->bar_info[0].index = RTL8723AE_PCI_BAR_ID;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    /* BAR size is not explicitly specified in sw.c; use minimal placeholder 4 KiB. */
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "rtl8723ae-mmio";
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI support is controlled by module parameter rtl8723e_mod_params.msi_support.
     * The driver will query rtlpci->msi_support and then request MSI from PCI core,
     * but from device side we can just expose MSI capability unconditionally.
     */
    Error *local_err = NULL;
    if (msi_init(pdev, 0, 1, true, false, &local_err) == 0) {
        s->has_msi = true;
    } else {
        s->has_msi = false;
        if (local_err) {
            error_propagate(errp, local_err);
        }
    }

    /* No MSIX is used by driver snippet; keep disabled. */
    s->has_msix = false;

    /* Initialize internal state */
    pcibase_reset(DEVICE(pdev));
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

    (void)s;
     /* No dynamic allocations in this model. */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "rtl8723ae_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(himr, PCIBaseState),
        VMSTATE_UINT32(himre, PCIBaseState),
        VMSTATE_UINT32(hisr, PCIBaseState),
        VMSTATE_UINT32(hisre, PCIBaseState),
        VMSTATE_UINT16(sys_iso_ctrl, PCIBaseState),
        VMSTATE_UINT16(sys_func_en, PCIBaseState),
        VMSTATE_UINT8(sys_clkr, PCIBaseState),
        VMSTATE_UINT32(efuse_ctrl, PCIBaseState),
        VMSTATE_UINT32(efuse_test, PCIBaseState),
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
