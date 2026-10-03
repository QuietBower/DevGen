/* Complete QEMU 8.2.10 PCI device model for rtw8821ce. Fix: BAR2 changed from I/O to memory-mapped 64KB to resolve pci_iomap failure during probe. */
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

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID    0x10ec
#define DEVICE_ID    0xb821
#define CLASS_ID     0x0280

/* Generic bit manipulation */
#define BIT(x) (1U << (x))

/* Register offsets and masks from rtw8821ce driver */
#define RTK_PCI_HIMR0           0x0B0
#define RTK_PCI_HIMR1           0x0B8
#define RTK_PCI_HIMR3           0x10B8
#define REG_MDIO_V1             0x03F4
#define REG_PCIE_MIX_CFG        0x03F8
#define RTK_PCIE_CLKDLY_CTRL    0x0725
#define REG_DBI_WDATA_V1        0x03E8
#define REG_DBI_FLAG_V1         0x03F0
#define REG_DBI_RDATA_V1        0x03EC
#define RTK_PCI_TXBD_BCN_WORK   0x383
#define RTK_PCIE_LINK_CFG       0x0719
#define RTK_PCI_CTRL            0x300
#define RTK_PCI_TXBD_IDX_H2CQ   0x132C
#define RTK_PCI_TXBD_IDX_BEQ    0x3A8
#define RTK_PCI_TXBD_IDX_BKQ    0x3AC
#define RTK_PCI_TXBD_IDX_VIQ    0x3A4
#define RTK_PCI_TXBD_IDX_HI0Q   0x3B8
#define RTK_PCI_TXBD_IDX_MGMTQ  0x3B0
#define RTK_PCI_TXBD_IDX_VOQ    0x3A0
#define RTK_PCI_TXBD_NUM_HI0Q   0x38C
#define RTK_PCI_TXBD_NUM_BKQ    0x38A
#define RTK_PCI_RXBD_NUM_MPDUQ  0x382
#define RTK_PCI_TXBD_H2CQ_CSR   0x1330
#define RTK_PCI_TXBD_DESA_VIQ   0x320
#define RTK_PCI_TXBD_NUM_BEQ    0x388
#define RTK_PCI_TXBD_DESA_MGMTQ 0x310
#define RTK_PCI_TXBD_NUM_H2CQ   0x1328
#define RTK_PCI_TXBD_DESA_VOQ   0x318
#define RTK_PCI_TXBD_NUM_VIQ    0x386
#define RTK_PCI_TXBD_NUM_MGMTQ  0x380
#define RTK_PCI_TXBD_NUM_VOQ    0x384
#define RTK_PCI_TXBD_DESA_BKQ   0x330
#define RTK_PCI_TXBD_DESA_BCNQ  0x308
#define RTK_PCI_TXBD_DESA_BEQ   0x328
#define RTK_PCI_TXBD_RWPTR_CLR  0x39C
#define RTK_PCI_TXBD_DESA_HI0Q  0x340
#define RTK_PCI_TXBD_DESA_H2CQ  0x1320
#define RTK_PCI_RXBD_DESA_MPDUQ 0x338
#define RTK_MAX_RX_DESC_NUM     512
#define RTK_PCI_TXBD_OWN_OFFSET 15
#define BIT_MDIO_WFLAG_V1       BIT(5)
#define BIT_DBI_WFLAG           BIT(16)
#define BIT_DBI_RFLAG           BIT(17)
#define BITS_DBI_ADDR_MASK      0x3FC
#define BITS_DBI_WREN           0xF000
#define BITS_MDIO_ADDR_MASK     0x1F
#define BIT_PCI_BCNQ_FLAG       BIT(4)
#define BIT_RST_TRXDMA_INTF     BIT(20)
#define BIT_RX_TAG_EN           BIT(15)
#define BIT_L1_SW_EN            BIT(3)
#define BIT_CLKREQ_SW_EN        BIT(4)
#define BIT_CLR_H2CQ_HOST_IDX   BIT(16)
#define BIT_CLR_H2CQ_HW_IDX     BIT(8)
#define TX_PAGE_SIZE_SHIFT      7
#define TX_PAGE_SIZE            (1 << TX_PAGE_SIZE_SHIFT)
#define RTK_BEQ_TX_DESC_NUM     256
#define RTK_DEFAULT_TX_DESC_NUM 128
#define RTK_PCI_RX_BUF_SIZE     (11454 + 24)
#define RTW_PCI_MDIO_PG_OFFS_G1 0
#define RTW_PCI_MDIO_PG_OFFS_G2 2
#define RTW_PCI_MDIO_PG_SZ      BIT(5)
#define RTW_PCI_WR_RETRY_CNT    20

/* Interrupt mask bits */
#define IMR_C2HCMD              BIT(10)
#define IMR_BEDOK               BIT(4)
#define IMR_MGNTDOK             BIT(6)
#define IMR_BCNDMAINT_E         BIT(14)
#define IMR_VIDOK               BIT(3)
#define IMR_H2CDOK              BIT(16)
#define IMR_HIGHDOK             BIT(7)
#define IMR_TXFOVW              BIT(9)
#define IMR_BKDOK               BIT(5)
#define IMR_ROK                 BIT(0)
#define IMR_VODOK               BIT(2)

/* BAR sizes: BAR0 = 64KB (0x10000), BAR2 = 64KB (0x10000) */
#define BAR0_SIZE               0x10000
#define BAR2_SIZE               0x10000
#define RTK_MAX_TX_QUEUE_NUM    8
#define RTK_MAX_RX_QUEUE_NUM    8

#define TYPE_PCIBASE_DEVICE "rtw88_8821ce_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

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
    uint32_t irq_mask[4];
    bool irq_enabled;
    uint32_t intr_status;

    struct dma_ring {
        dma_addr_t dma;
        uint32_t len;
        uint32_t wp;
        uint32_t rp;
    } tx_rings[RTK_MAX_TX_QUEUE_NUM], rx_rings[RTK_MAX_RX_QUEUE_NUM];

    bool running;
    uint32_t link_ctrl;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    /* Interrupt signaling placeholder. */
}

static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* No DMA logic available from driver source */
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case RTK_PCI_HIMR0:
        val = s->irq_mask[0];
        break;
    case RTK_PCI_HIMR1:
        val = s->irq_mask[1];
        break;
    case RTK_PCI_HIMR3:
        val = s->irq_mask[3];
        break;
    default:
        /* Unknown register; return 0 as safe default */
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case RTK_PCI_HIMR0:
        s->irq_mask[0] = (uint32_t)val;
        s->irq_enabled = (s->irq_mask[0] || s->irq_mask[1] || s->irq_mask[3]);
        break;
    case RTK_PCI_HIMR1:
        s->irq_mask[1] = (uint32_t)val;
        s->irq_enabled = (s->irq_mask[0] || s->irq_mask[1] || s->irq_mask[3]);
        break;
    case RTK_PCI_HIMR3:
        s->irq_mask[3] = (uint32_t)val;
        s->irq_enabled = (s->irq_mask[0] || s->irq_mask[1] || s->irq_mask[3]);
        break;
    default:
        /* Ignore writes to unknown registers */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    /* No PIO access patterns in driver */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    /* No PIO access patterns in driver */
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    /* Reset interrupt mask registers and IRQ enable flag */
    memset(s->irq_mask, 0, sizeof(s->irq_mask));
    s->irq_enabled = false;
    s->intr_status = 0;
    /* No other reset values known from driver source */
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x10ec);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0xb821);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0280);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Subsystem IDs (typical Realtek) */
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, 0x10ec);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0xb821);

    /* PCIe capabilities */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* MSI capability */
    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        error_append_hint(errp, "MSI initialization failed\n");
        return;
    }
    s->has_msi = true;

    /* BAR0: 32-bit memory, 64KB, to provide sufficient space and avoid 64-bit quirks */
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_mmio_ops, s, "rtw88-mmio", BAR0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[0]);

    /* BAR2: 32-bit memory, 64KB, replaces I/O BAR to match driver expectation for pci_iomap */
    memory_region_init_io(&s->bar_regions[2], OBJECT(s), &pcibase_mmio_ops, s, "rtw88-bar2", BAR2_SIZE);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[2]);
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
    .name = "rtw88_8821ce_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32_ARRAY(irq_mask, PCIBaseState, 4),
        VMSTATE_BOOL(irq_enabled, PCIBaseState),
        VMSTATE_UINT32(intr_status, PCIBaseState),
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
