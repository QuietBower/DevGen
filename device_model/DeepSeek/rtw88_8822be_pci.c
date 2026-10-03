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

#define TYPE_PCIBASE_DEVICE "rtw88_8822be_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_REALTEK         0x10EC
#define PCI_DEVICE_ID_RTW8822BE       0xB822
#define PCI_CLASS_ID                  PCI_CLASS_NETWORK_OTHER

#define RTK_PCI_HIMR0                  0x0B0
#define RTK_PCI_HIMR1                  0x0B8
#define RTK_PCI_HIMR3                  0x10B8
#define REG_MDIO_V1                    0x03F4
#define REG_PCIE_MIX_CFG               0x03F8
#define RTK_PCIE_CLKDLY_CTRL           0x0725
#define REG_DBI_WDATA_V1               0x03E8
#define REG_DBI_FLAG_V1                0x03F0
#define REG_DBI_RDATA_V1               0x03EC
#define RTK_PCI_CTRL                   0x300
#define RTK_PCI_TXBD_BCN_WORK          0x383
#define RTK_PCIE_LINK_CFG              0x0719
#define RTK_PCI_TXBD_H2CQ_CSR          0x1330
#define RTK_PCI_TXBD_IDX_H2CQ          0x132C
#define RTK_PCI_TXBD_IDX_BEQ           0x3A8
#define RTK_PCI_TXBD_IDX_BKQ           0x3AC
#define RTK_PCI_TXBD_IDX_VIQ           0x3A4
#define RTK_PCI_TXBD_IDX_HI0Q          0x3B8
#define RTK_PCI_TXBD_IDX_MGMTQ         0x3B0
#define RTK_PCI_TXBD_IDX_VOQ           0x3A0
#define RTK_PCI_TXBD_NUM_HI0Q          0x38C
#define RTK_PCI_TXBD_NUM_BKQ           0x38A
#define RTK_PCI_TXBD_NUM_BEQ           0x388
#define RTK_PCI_TXBD_NUM_H2CQ          0x1328
#define RTK_PCI_TXBD_NUM_VIQ           0x386
#define RTK_PCI_TXBD_NUM_MGMTQ         0x380
#define RTK_PCI_TXBD_NUM_VOQ           0x384
#define RTK_PCI_RXBD_NUM_MPDUQ         0x382
#define RTK_PCI_TXBD_DESA_VIQ          0x320
#define RTK_PCI_TXBD_DESA_MGMTQ        0x310
#define RTK_PCI_TXBD_DESA_VOQ          0x318
#define RTK_PCI_TXBD_DESA_BKQ          0x330
#define RTK_PCI_TXBD_DESA_BCNQ         0x308
#define RTK_PCI_TXBD_DESA_BEQ          0x328
#define RTK_PCI_TXBD_DESA_HI0Q         0x340
#define RTK_PCI_TXBD_DESA_H2CQ         0x1320
#define RTK_PCI_RXBD_DESA_MPDUQ        0x338
#define RTK_PCI_TXBD_RWPTR_CLR         0x39C

#define IMR_C2HCMD                     BIT(10)
#define IMR_BEDOK                      BIT(4)
#define IMR_MGNTDOK                    BIT(6)
#define IMR_BCNDMAINT_E               BIT(14)
#define IMR_VIDOK                      BIT(3)
#define IMR_H2CDOK                     BIT(16)
#define IMR_HIGHDOK                    BIT(7)
#define IMR_TXFOVW                     BIT(9)
#define IMR_BKDOK                      BIT(5)
#define IMR_ROK                        BIT(0)
#define IMR_VODOK                      BIT(2)
#define BIT_RST_TRXDMA_INTF            BIT(20)
#define BIT_CLKREQ_SW_EN               BIT(4)
#define BIT_L1_SW_EN                   BIT(3)
#define BIT_RX_TAG_EN                  BIT(15)

#define BAR0_SIZE                      0x10000
#define BAR2_SIZE                      0x2000

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
    uint32_t intr_mask[4];

    /* Hardware register shadows */
    uint32_t reg_0x0300;
    uint32_t reg_0x00B0;
    uint32_t reg_0x00B8;
    uint32_t reg_0x10B8;
    uint32_t reg_0x03F4;
    uint32_t reg_0x03F8;
    uint32_t reg_0x0725;
    uint32_t reg_0x03E8;
    uint32_t reg_0x03F0;
    uint32_t reg_0x03EC;
    uint32_t reg_0x0383;
    uint32_t reg_0x0719;
    /* Additional register storage will be added as needed */

    /* DMA context */
    dma_addr_t dma_src;
    dma_addr_t dma_dst;
    dma_addr_t dma_cnt;
    dma_addr_t dma_cmd;

    /* Operational status flags */
    bool irq_enabled;
    bool running;

    /* Reset control */
    bool reset_active;

    /* Power management state */
    uint8_t power_state;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case RTK_PCI_HIMR0:
        val = s->reg_0x00B0;
        break;
    case RTK_PCI_HIMR1:
        val = s->reg_0x00B8;
        break;
    case RTK_PCI_HIMR3:
        val = s->reg_0x10B8;
        break;
    case REG_MDIO_V1:
        val = s->reg_0x03F4;
        break;
    case REG_PCIE_MIX_CFG:
        val = s->reg_0x03F8;
        break;
    case RTK_PCIE_CLKDLY_CTRL:
        val = s->reg_0x0725;
        break;
    case REG_DBI_WDATA_V1:
        val = s->reg_0x03E8;
        break;
    case REG_DBI_FLAG_V1:
        val = s->reg_0x03F0;
        break;
    case REG_DBI_RDATA_V1:
        val = s->reg_0x03EC;
        break;
    case RTK_PCI_CTRL:
        val = s->reg_0x0300;
        break;
    case RTK_PCI_TXBD_BCN_WORK:
        val = s->reg_0x0383;
        break;
    case RTK_PCIE_LINK_CFG:
        val = s->reg_0x0719;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: MMIO read from unknown addr 0x%" HWADDR_PRIx "\n", __func__, addr);
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case RTK_PCI_HIMR0:
        s->reg_0x00B0 = (uint32_t)val;
        break;
    case RTK_PCI_HIMR1:
        s->reg_0x00B8 = (uint32_t)val;
        break;
    case RTK_PCI_HIMR3:
        s->reg_0x10B8 = (uint32_t)val;
        break;
    case REG_MDIO_V1:
        s->reg_0x03F4 = (uint32_t)val;
        break;
    case REG_PCIE_MIX_CFG:
        s->reg_0x03F8 = (uint32_t)val;
        break;
    case RTK_PCIE_CLKDLY_CTRL:
        s->reg_0x0725 = (uint32_t)val;
        break;
    case REG_DBI_WDATA_V1:
        s->reg_0x03E8 = (uint32_t)val;
        break;
    case REG_DBI_FLAG_V1:
        s->reg_0x03F0 = (uint32_t)val;
        break;
    case REG_DBI_RDATA_V1:
        s->reg_0x03EC = (uint32_t)val;
        break;
    case RTK_PCI_CTRL:
        s->reg_0x0300 = (uint32_t)val;
        break;
    case RTK_PCI_TXBD_BCN_WORK:
        s->reg_0x0383 = (uint32_t)val;
        break;
    case RTK_PCIE_LINK_CFG:
        s->reg_0x0719 = (uint32_t)val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: MMIO write to unknown addr 0x%" HWADDR_PRIx " with value 0x%" PRIx64 "\n", __func__, addr, val);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO registers used */
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

    /* Reset registers to power-on defaults */
    s->reg_0x0300 = 0;
    s->reg_0x00B0 = 0;
    s->reg_0x00B8 = 0;
    s->reg_0x10B8 = 0;
    s->reg_0x03F4 = 0;
    s->reg_0x03F8 = 0;
    s->reg_0x0725 = 0;
    s->reg_0x03E8 = 0;
    s->reg_0x03F0 = 0;
    s->reg_0x03EC = 0;
    s->reg_0x0383 = 0;
    s->reg_0x0719 = 0;
    s->irq_enabled = false;
    s->running = false;
    s->reset_active = true;
    s->power_state = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_REALTEK);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_RTW8822BE);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization: Only BAR0 (MMIO) and BAR2 (DMA descriptor MMIO) */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "rtw8822be-mmio";

    s->bar_info[1].index = 2;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = BAR2_SIZE;
    s->bar_info[1].name = "rtw8822be-dma";

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
    .name = "rtw88_8822be_pci",
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
