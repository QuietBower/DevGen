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

#define TYPE_PCIBASE_DEVICE "rtl8188ee_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs (first entry in pci_device_id table) */
#define VENDOR_ID 0x10EC
#define DEVICE_ID 0x8179
#define CLASS_ID 0x0280

/* BAR Definitions */
#define RTL_MMIO_BAR 2

/* Linux BIT() macro for register bit definitions */
#define BIT(n) (1U << (n))

/* Register Offsets (from driver source) */
#define REG_SYS_ISO_CTRL     0x0000
#define REG_SYS_FUNC_EN      0x0002
#define REG_SYS_CLKR         0x0008
#define REG_HIMR             0x0120
#define REG_HIMRE            0x0128
#define REG_HSISR            0x005c
#define REG_EFUSE_ACCESS     0x00CF
#define REG_EFUSE_TEST       0x0034
#define REG_EFUSE_CTRL       0x0030
#define REG_CAMCMD           0x0670
#define REG_CAMWRITE         0x0674
#define REG_CAMREAD          0x0678
#define REG_CAMDBG           0x067C
#define REG_SECCFG           0x0680

/* Bit definitions for registers */
#define AM                   BIT(2)
#define AB                   BIT(3)
#define ACRC32               BIT(8)
#define ACF                  BIT(12)
#define AAP                  BIT(0)
#define PWC_EV12V            BIT(15)
#define FEN_ELDR             BIT(12)
#define LOADER_CLK_EN        BIT(5)
#define ANA8M                BIT(1)

/* Interrupt Mask bits */
#define IMR_BCNDMAINT6       BIT(31)
#define IMR_BCNDMAINT5       BIT(30)
#define IMR_BCNDMAINT4       BIT(29)
#define IMR_BCNDMAINT3       BIT(28)
#define IMR_BCNDMAINT2       BIT(27)
#define IMR_BCNDMAINT1       BIT(26)
#define IMR_BCNDOK7          BIT(24)
#define IMR_BCNDOK6          BIT(23)
#define IMR_BCNDOK5          BIT(22)
#define IMR_BCNDOK4          BIT(21)
#define IMR_BCNDOK3          BIT(20)
#define IMR_BCNDOK2          BIT(19)
#define IMR_BCNDOK1          BIT(18)
#define IMR_TXFOVW           BIT(15)
#define IMR_PSTIMEOUT        BIT(14)
#define IMR_BCNDMAINT0       BIT(20)
#define IMR_RXFOVW           BIT(12)
#define IMR_RDU               BIT(11)
#define IMR_ATIMEND          BIT(10)
#define IMR_BCNDOK0          BIT(16)
#define IMR_MGNTDOK           BIT(6)
#define IMR_TBDER             BIT(5)
#define IMR_HIGHDOK          BIT(8)
#define IMR_TBDOK             BIT(7)
#define IMR_BKDOK             BIT(4)
#define IMR_BEDOK             BIT(3)
#define IMR_VIDOK             BIT(2)
#define IMR_VODOK             BIT(1)
#define IMR_ROK               BIT(0)
#define IMR_HSISR_IND_ON_INT  BIT(15)

/* CAM related */
#define CAM_NONE             0x0
#define CAM_WEP40            0x01
#define CAM_TKIP             0x02
#define CAM_AES              0x04
#define CAM_WEP104           0x05

/* Other constants */
#define HWSET_MAX_SIZE       128
#define EFUSE_MAX_SECTION    16
#define EFUSE_REAL_CONTENT_LEN 512
#define EFUSE_OOB_PROTECT_BYTES 15

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

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    uint32_t himr;      /* Interrupt Mask Register */
    uint32_t himre;     /* Interrupt Mask Extension Register */
    uint32_t hisr;      /* Interrupt Status Register */
    bool irq_raised;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t regs[0x4000];  /* BAR size */

    /* EFUSE emulation */
    uint8_t efuse_data[EFUSE_REAL_CONTENT_LEN];
    uint16_t efuse_addr;
    uint16_t efuse_ctrl;
    uint16_t efuse_test;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool pending = (s->hisr & s->himr) != 0;
    if (pending) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            if (!s->irq_raised) {
                pci_set_irq(pdev, 1);
                s->irq_raised = true;
            }
        }
    } else {
        if (!msi_enabled(pdev) && s->irq_raised) {
            pci_set_irq(pdev, 0);
            s->irq_raised = false;
        }
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr + size > sizeof(s->regs)) return 0;

    /* Handle known special registers */
    if (addr == REG_HSISR && size == 4) {
        val = s->hisr;
    } else if (addr == REG_HIMR && size == 4) {
        val = s->himr;
    } else if (addr == REG_HIMRE && size == 4) {
        val = s->himre;
    } else if (addr == REG_EFUSE_ACCESS && size == 1) {
        if (s->efuse_addr < EFUSE_REAL_CONTENT_LEN) {
            val = s->efuse_data[s->efuse_addr];
            s->efuse_addr++;
        }
    } else if (addr == REG_EFUSE_CTRL && size == 2) {
        val = s->efuse_ctrl;
    } else if (addr == REG_EFUSE_TEST && size == 2) {
        val = s->efuse_test;
    } else {
        /* Generic read from register shadow array */
        switch (size) {
        case 1: val = ldub_p(s->regs + addr); break;
        case 2: val = lduw_le_p(s->regs + addr); break;
        case 4: val = ldl_le_p(s->regs + addr); break;
        case 8: val = ldq_le_p(s->regs + addr); break;
        default: break;
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr + size > sizeof(s->regs)) return;

    /* Handle known special registers */
    if (addr == REG_HSISR && size == 4) {
        /* W1C: write 1 to clear bits */
        s->hisr &= ~((uint32_t)val);
        pcibase_update_irq(s);
    } else if (addr == REG_HIMR && size == 4) {
        s->himr = (uint32_t)val;
        pcibase_update_irq(s);
    } else if (addr == REG_HIMRE && size == 4) {
        s->himre = (uint32_t)val;
        pcibase_update_irq(s);
    } else if (addr == REG_EFUSE_CTRL && size == 2) {
        s->efuse_ctrl = val;
        /* Set EFUSE read address from lower 9 bits */
        s->efuse_addr = val & 0x1FF;
    } else if (addr == REG_EFUSE_TEST && size == 2) {
        s->efuse_test = val;
    } else if (addr == REG_EFUSE_ACCESS && size == 1) {
        /* Ignore writes to EFUSE_ACCESS (OTP) */
        s->regs[addr] = val;
    } else {
        /* Generic write to register shadow array */
        switch (size) {
        case 1: stb_p(s->regs + addr, val); break;
        case 2: stw_le_p(s->regs + addr, val); break;
        case 4: stl_le_p(s->regs + addr, val); break;
        case 8: stq_le_p(s->regs + addr, val); break;
        default: break;
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
    memset(s->regs, 0, sizeof(s->regs));

    /* Reset EFUSE state and program default content */
    s->efuse_addr = 0;
    s->efuse_ctrl = 0;
    s->efuse_test = 0;
    memset(s->efuse_data, 0, sizeof(s->efuse_data));
    /* Set a valid EEPROM signature at offset 0 */
    *(uint16_t *)s->efuse_data = cpu_to_le16(0x8129);
    /* Assign a dummy MAC address at offset 0xd0 */
    uint8_t *mac = s->efuse_data + 0xd0;
    mac[0] = 0x00; mac[1] = 0x11; mac[2] = 0x22; mac[3] = 0x33; mac[4] = 0x44; mac[5] = 0x55;

    /* Reset interrupt state */
    s->himr = s->himre = s->hisr = 0;
    s->irq_raised = false;
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
        /* PIO not used by this driver, but keep for completeness */
        /* No actual PIO ops, so skip */
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

    /* Initialize MSI */
    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        error_report("Failed to initialize MSI");
        return;
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = RTL_MMIO_BAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x4000;
    s->bar_info[0].name = "rtl-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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

static const VMStateDescription vmstate_pcibase = {
    .name = "rtl8188ee_pci",
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
