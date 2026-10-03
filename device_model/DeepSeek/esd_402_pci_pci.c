/*
 * QEMU PCI device model for esd 402 PCIe CAN controller
 * Based on Linux driver esd_402_pci-core.c
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

/* Helper macro */
#define BIT(n) (1UL << (n))

/* Additional definitions from driver source */
#define CAN_MAX_DLEN 8

/* PCI identification */
#define PCI_VENDOR_ID_ESDGMBH        0x12fe
#define ESD_PCI_DEVICE_ID_PCIE402    0x0402
#define PCI402_CLASS_ID              0x0000   /* CAN class not set by driver, default 0x0000 */

/* BAR info */
#define PCI402_BAR                    0
#define PCI402_IO_OV_OFFS             0
#define PCI402_IO_PCIEP_OFFS          0x10000
#define PCI402_IO_LEN_TOTAL           0x20000
#define PCI402_IO_LEN_CORE            0x2000
#define PCI402_MAX_CORES              6

/* DMA parameters */
#define PCI402_DMA_MASK               ((1ULL << 32) - 1)  /* 32-bit */
#define PCI402_DMA_SIZE               0x10000

/* MSI capability offset in PCI config */
#define PCI402_PCICFG_MSICAP          0x50

/* Register offsets (Overview region, base 0x0000) */
#define ACC_OV_OF_VERSION             0x0004
#define ACC_OV_OF_INFO                0x0008
#define ACC_OV_OF_CANCORE_FREQ        0x000c
#define ACC_OV_OF_TS_FREQ_LO          0x0010
#define ACC_OV_OF_MODE                0x002c
#define ACC_OV_OF_BM_IRQ_MASK         0x0074
#define ACC_OV_OF_BM_IRQ_COUNTER      0x0070
#define ACC_OV_OF_MSI_DATA            0x0080
#define ACC_OV_OF_MSI_ADDRESSOFFSET   0x0084

/* Register offsets (PCIEP region, base 0x10000) */
#define PCI402_PCIEP_OF_INT_ENABLE    0x0050
#define PCI402_PCIEP_OF_BM_ADDR_LO     0x1000
#define PCI402_PCIEP_OF_BM_ADDR_HI     0x1004
#define PCI402_PCIEP_OF_MSI_ADDR_LO    0x1008
#define PCI402_PCIEP_OF_MSI_ADDR_HI    0x100c

/* Core register offsets (per core, base + 0x2000 * core) */
#define ACC_CORE_OF_CTRL              0x0000
#define ACC_CORE_OF_BTR               0x0010
#define ACC_CORE_OF_BRP               0x000c
#define ACC_CORE_OF_STATUS            0x0030
#define ACC_CORE_OF_TXFIFO_CONFIG     0x0048
#define ACC_CORE_OF_TXFIFO_STATUS     0x004c
#define ACC_CORE_OF_TX_ABORT_MASK     0x0054
#define ACC_CORE_OF_TXFIFO_DATA_0     0x00c8
#define ACC_CORE_OF_TXFIFO_DATA_1     0x00cc
#define ACC_CORE_OF_TXFIFO_ID         0x00c0
#define ACC_CORE_OF_TXFIFO_DLC        0x00c4

/* Bit definitions */
#define ACC_OV_REG_MODE_MASK_ENDIAN_LITTLE    BIT(0)
#define ACC_OV_REG_MODE_MASK_BM_ENABLE        BIT(1)
#define ACC_OV_REG_MODE_MASK_I2C_ENABLE       BIT(11)
#define ACC_OV_REG_MODE_MASK_MSI_ENABLE       BIT(14)
#define ACC_OV_REG_MODE_MASK_NEW_PSC_ENABLE   BIT(15)
#define ACC_OV_REG_MODE_MASK_FPGA_RESET       BIT(31)
#define ACC_OV_REG_FEAT_MASK_CANFD            BIT(11) /* 27-16, bit 11 */
#define ACC_OV_REG_FEAT_MASK_DAR              BIT(14) /* 30-16, bit 14 */
#define ACC_OV_REG_FEAT_MASK_NEW_PSC          BIT(12) /* 28-16, bit 12 */
#define ACC_REG_CTRL_MASK_RESETMODE           BIT(0)
#define ACC_REG_CTRL_MASK_LOM                 BIT(1)
#define ACC_REG_CTRL_MASK_IE_RXTX             BIT(8)
#define ACC_REG_CTRL_MASK_IE_TXERROR          BIT(9)
#define ACC_REG_CTRL_MASK_IE_ERRWARN          BIT(10)
#define ACC_REG_CTRL_MASK_IE_OVERRUN          BIT(11)
#define ACC_REG_CTRL_MASK_IE_ERRPASS          BIT(13)
#define ACC_REG_CTRL_MASK_IE_BUSERR           BIT(15)

#define TYPE_PCIBASE_DEVICE "esd_402_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Updated from driver source: FPGA version minimum and TS frequency */
#define PCI402_FPGA_VER_MIN             0x003d
#define ACC_TS_FREQ_80MHZ               80000000

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

/* DMA message structures from driver */
struct acc_bmmsg_rxtxdone {
    uint8_t msg_id;
    uint8_t txfifo_level;
    uint8_t reserved1[2];
    uint8_t txtsfifo_level;
    uint8_t reserved2[3];
    uint32_t id;
    struct {
        uint8_t len;
        uint8_t txdfifo_idx;
        uint8_t zeroes8;
        uint8_t reserved;
    } acc_dlc;
    uint8_t data[CAN_MAX_DLEN];
    uint64_t ts;
} QEMU_PACKED;

struct acc_bmmsg_txabort {
    uint8_t msg_id;
    uint8_t txfifo_level;
    uint16_t abort_mask;
    uint8_t txtsfifo_level;
    uint8_t reserved2[1];
    uint16_t abort_mask_txts;
    uint64_t ts;
    uint32_t reserved3[4];
} QEMU_PACKED;

struct acc_bmmsg_overrun {
    uint8_t msg_id;
    uint8_t txfifo_level;
    uint8_t lost_cnt;
    uint8_t reserved1;
    uint8_t txtsfifo_level;
    uint8_t reserved2[3];
    uint64_t ts;
    uint32_t reserved3[4];
} QEMU_PACKED;

struct acc_bmmsg_buserr {
    uint8_t msg_id;
    uint8_t txfifo_level;
    uint8_t ecc;
    uint8_t reserved1;
    uint8_t txtsfifo_level;
    uint8_t reserved2[3];
    uint64_t ts;
    uint32_t reg_status;
    uint32_t reg_btr;
    uint32_t reserved3[2];
} QEMU_PACKED;

struct acc_bmmsg_errstatechange {
    uint8_t msg_id;
    uint8_t txfifo_level;
    uint8_t reserved1[2];
    uint8_t txtsfifo_level;
    uint8_t reserved2[3];
    uint64_t ts;
    uint32_t reg_status;
    uint32_t reserved3[3];
} QEMU_PACKED;

struct acc_bmmsg_timeslice {
    uint8_t msg_id;
    uint8_t txfifo_level;
    uint8_t reserved1[2];
    uint8_t txtsfifo_level;
    uint8_t reserved2[3];
    uint64_t ts;
    uint32_t reserved3[4];
} QEMU_PACKED;

struct acc_bmmsg_hwtimer {
    uint8_t msg_id;
    uint8_t reserved1[3];
    uint32_t reserved2[1];
    uint64_t timer;
    uint32_t reserved3[4];
} QEMU_PACKED;

union acc_bmmsg {
    uint8_t msg_id;
    struct acc_bmmsg_rxtxdone rxtxdone;
    struct acc_bmmsg_txabort txabort;
    struct acc_bmmsg_overrun overrun;
    struct acc_bmmsg_buserr buserr;
    struct acc_bmmsg_errstatechange errstatechange;
    struct acc_bmmsg_timeslice timeslice;
    struct acc_bmmsg_hwtimer hwtimer;
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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows */
    uint8_t regs[PCI402_IO_LEN_TOTAL]; /* full MMIO register file */

    /* DMA Context */
    dma_addr_t dma_addr;
    uint32_t dma_size;
    uint32_t dma_irq_cnt; /* simulated BM IRQ counter */

    uint32_t status;      /* Operational status flags */
    bool fpga_reset;      /* FPGA reset state */
};

/* IRQ update logic: not implemented as no interrupts are generated during probe */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* Not used in probe, placeholder for future interrupt handling */
}

/* DMA transfer logic: not implemented, device bus mastering is simulated */
static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* Driver only sets up DMA buffers, no explicit DMA transfers triggered */
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Only 32-bit aligned accesses are expected by the driver */
    if (size != 4 || addr & 3) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid read: size=%u addr=0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        return 0xFFFFFFFFU;
    }

    if (addr < 0x10000) {
        /* Overview region */
        switch (addr) {
        case ACC_OV_OF_VERSION:
            val = 0x1234;
            break;
        case ACC_OV_OF_INFO:
            val = 0x00000001; /* active_cores=1, features=0 */
            break;
        case ACC_OV_OF_CANCORE_FREQ:
            val = 80000000;
            break;
        case ACC_OV_OF_TS_FREQ_LO:
            val = ACC_TS_FREQ_80MHZ;
            break;
        default:
            val = ldl_be_p(s->regs + addr);
            break;
        }
    } else if (addr < 0x12000) {
        /* PCIEP region */
        if (addr >= 0x10000 && addr <= 0x11FFF) {
            val = ldl_be_p(s->regs + addr);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: read out of PCIEP range addr=0x%" HWADDR_PRIx "\n",
                          __func__, addr);
            val = 0xFFFFFFFFU;
        }
    } else if (addr < 0x20000) {
        /* Core regions */
        if ((addr - 0x2000) % 0x2000 < 0x2000) {
            val = ldl_be_p(s->regs + addr);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid core address addr=0x%" HWADDR_PRIx "\n",
                          __func__, addr);
            val = 0xFFFFFFFFU;
        }
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: access out of bounds addr=0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        val = 0xFFFFFFFFU;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4 || addr & 3) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid write: size=%u addr=0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        return;
    }

    if (addr < 0x10000) {
        /* Overview region */
        switch (addr) {
        case ACC_OV_OF_MODE:
            if (val & ACC_OV_REG_MODE_MASK_FPGA_RESET) {
                s->fpga_reset = true;
                memset(s->regs, 0, sizeof(s->regs));
                /* Re-initialize essential reset values */
                stl_be_p(&s->regs[ACC_OV_OF_VERSION], 0x1234);
                stl_be_p(&s->regs[ACC_OV_OF_INFO], 0x00000001);
                stl_be_p(&s->regs[ACC_OV_OF_CANCORE_FREQ], 80000000);
                stl_be_p(&s->regs[ACC_OV_OF_TS_FREQ_LO], ACC_TS_FREQ_80MHZ);
                for (int i = 0; i < PCI402_MAX_CORES; i++) {
                    hwaddr core_base = 0x2000 + i * 0x2000;
                    stl_be_p(&s->regs[core_base + ACC_CORE_OF_TXFIFO_CONFIG], 0x10000000);
                }
                s->fpga_reset = false;
            }
            stl_be_p(s->regs + addr, val);
            break;
        default:
            stl_be_p(s->regs + addr, val);
            break;
        }
    } else if (addr < 0x12000) {
        /* PCIEP region */
        stl_be_p(s->regs + addr, val);
    } else if (addr < 0x20000) {
        /* Core regions */
        stl_be_p(s->regs + addr, val);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write out of bounds addr=0x%" HWADDR_PRIx "\n",
                      __func__, addr);
    }
}

/* PIO handlers: not used, minimal stub */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    qemu_log_mask(LOG_UNIMP, "%s: PIO read not implemented\n", __func__);
    return 0xFFFFFFFFU;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    qemu_log_mask(LOG_UNIMP, "%s: PIO write not implemented\n", __func__);
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
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

    /* Initialize register defaults to power-on state */
    memset(s->regs, 0, sizeof(s->regs));
    stl_be_p(&s->regs[ACC_OV_OF_VERSION], 0x1234);
    stl_be_p(&s->regs[ACC_OV_OF_INFO], 0x00000001);
    stl_be_p(&s->regs[ACC_OV_OF_CANCORE_FREQ], 80000000);
    stl_be_p(&s->regs[ACC_OV_OF_TS_FREQ_LO], ACC_TS_FREQ_80MHZ);

    /* Default TXFIFO_CONFIG for each core: size=16 (0x10) */
    for (int i = 0; i < PCI402_MAX_CORES; i++) {
        hwaddr core_base = 0x2000 + i * 0x2000;
        stl_be_p(&s->regs[core_base + ACC_CORE_OF_TXFIFO_CONFIG], 0x10000000);
    }

    s->fpga_reset = false;
    s->intr_status = 0;
    s->intr_mask = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ESDGMBH );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ESD_PCI_DEVICE_ID_PCIE402 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI402_CLASS_ID );
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
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = PCI402_IO_LEN_TOTAL, .name = "bar0" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI initialization: 64-bit, 1 vector */
    if (msi_init(pdev, PCI402_PCICFG_MSICAP, 1, true, false, errp) != 0) {
        return;
    }

    /* DMA configuration */
    /* No special init needed; driver will set up DMA addresses via BAR writes */

    /* Final state initialization */
    /* Initial reset state already set in pcibase_reset, called after realize */
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

    /* No additional cleanup required */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "esd_402_pci_pci",
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
