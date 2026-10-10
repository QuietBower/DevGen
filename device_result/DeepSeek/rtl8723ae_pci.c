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

#define TYPE_PCIBASE_DEVICE "rtl8723ae_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x10ec
#define DEVICE_ID 0x8723
/* CLASS_ID not defined in driver source */

/* Interrupt mask bits extracted from driver PHIMR_* macros */
#define PHIMR_MGNTDOK       BIT(6)
#define PHIMR_PSTIMEOUT     BIT(29)
#define PHIMR_BEDOK         BIT(4)
#define PHIMR_VODOK         BIT(2)
#define PHIMR_TXBCNOK       BIT(25)
#define PHIMR_HISRE_IND     BIT(11)
#define PHIMR_ROK           BIT(0)
#define PHIMR_C2HCMD        BIT(10)
#define PHIMR_VIDOK         BIT(3)
#define PHIMR_RDU           BIT(1)
#define PHIMR_RXFOVW        BIT(8)
#define PHIMR_BKDOK         BIT(5)
#define PHIMR_TSF_BIT32_TOGGLE BIT(24)
#define PHIMR_HIGHDOK       BIT(7)
#define PHIMR_TXBCNERR      BIT(26)
#define PHIMR_TXFOVW        BIT(9)
#define PHIMR_BCNDOK0       BIT(16)
#define PHIMR_BCNDMAINT0    BIT(20)
#define PHIMR_ATIMEND_E     BIT(13)

/* Additional register offsets and masks from supplementary source */
#define REG_SYS_ISO_CTRL    0x0000
#define REG_SYS_FUNC_EN     0x0002
#define REG_SYS_CLKR        0x0008

#define AM                  BIT(2)
#define AB                  BIT(3)
#define ACRC32              BIT(8)
#define ACF                 BIT(12)
#define AAP                 BIT(0)

#define REG_HIMR            0x0120
#define REG_HIMRE           0x0128

#define REG_EFUSE_TEST      0x0034
#define REG_EFUSE_CTRL      0x0030

#define PWC_EV12V           BIT(15)
#define FEN_ELDR            BIT(12)
#define LOADER_CLK_EN       BIT(5)
#define ANA8M               BIT(1)

#define HWSET_MAX_SIZE      128
#define EFUSE_MAX_SECTION   16
#define EFUSE_REAL_CONTENT_LEN 512
#define EFUSE_OOB_PROTECT_BYTES 15

#define REG_CAMCMD          0x0670
#define REG_CAMWRITE        0x0674
#define REG_CAMREAD         0x0678
#define REG_CAMDBG          0x067C
#define REG_SECCFG          0x0680

#define CAM_NONE            0x0
#define CAM_WEP40           0x01
#define CAM_TKIP            0x02
#define CAM_AES             0x04
#define CAM_WEP104          0x05

#define IMR_BCNDMAINT6      BIT(31)
#define IMR_BCNDMAINT5      BIT(30)
#define IMR_BCNDMAINT4      BIT(29)
#define IMR_BCNDMAINT3      BIT(28)
#define IMR_BCNDMAINT2      BIT(27)
#define IMR_BCNDMAINT1      BIT(26)
#define IMR_BCNDOK8         BIT(25)
#define IMR_BCNDOK7         BIT(24)
#define IMR_BCNDOK6         BIT(23)
#define IMR_BCNDOK5         BIT(22)
#define IMR_BCNDOK4         BIT(21)
#define IMR_BCNDOK3         BIT(20)
#define IMR_BCNDOK2         BIT(19)
#define IMR_BCNDOK1         BIT(18)
#define IMR_TIMEOUT2        BIT(17)
#define IMR_TIMEOUT1        BIT(16)

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t regs[0x1000];

    /* DMA Context */

    /* Operational status flags */
    /* State used to handle reset sequences */
    /* Power management state (D0-D3) */
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr + size <= sizeof(s->regs)) {
        memcpy(&val, s->regs + addr, size);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
            "%s: read out of bounds at addr 0x%" HWADDR_PRIx " size %u\n",
            __func__, addr, size);
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr + size <= sizeof(s->regs)) {
        memcpy(s->regs + addr, &val, size);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR,
            "%s: write out of bounds at addr 0x%" HWADDR_PRIx " size %u\n",
            __func__, addr, size);
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

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
    memset(s->regs, 0, sizeof(s->regs));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0280);
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
    s->bar_info[0].index = 2;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "rtl8723ae-bar2";
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "rtl8723ae_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_instance_init(Object *obj)
{
    PCIBaseState *s = PCIBASE_DEVICE(obj);
    memset(s->regs, 0, sizeof(s->regs));
}

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
        .instance_init = pcibase_instance_init,
        .class_init = pcibase_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);
