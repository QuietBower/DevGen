#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"
#include "qemu/bswap.h"

#define TYPE_PCIBASE_DEVICE "uli526x_pci"

typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define ULI526X_VENDOR_ID 0x10B9
#define ULI5261_DEVICE_ID 0x5261
#define ULI5263_DEVICE_ID 0x5263
#define ULI526X_CLASS_ID  PCI_CLASS_NETWORK_ETHERNET

#define RX_DESC_CNT     0x30
#define TX_DESC_CNT     0x20
#define PCI_ULI5261_ID  0x526110B9
#define PCI_ULI5263_ID  0x526310B9
#define ULI526X_IO_SIZE 0x100
#define TX_FREE_DESC_CNT (TX_DESC_CNT - 2)
#define TX_WAKE_DESC_CNT (TX_DESC_CNT - 3)
#define DESC_ALL_CNT    (TX_DESC_CNT + RX_DESC_CNT)
#define TX_BUF_ALLOC    0x600
#define RX_ALLOC_SIZE   0x620
#define ULI526X_RESET    1
#define CR0_DEFAULT     0
#define CR6_DEFAULT     0x22200000
#define CR7_DEFAULT     0x180c1
#define CR15_DEFAULT    0x06
#define TDES0_ERR_MASK  0x4302
#define MAX_PACKET_SIZE 1514
#define ULI5261_MAX_MULTICAST 14
#define RX_COPY_SIZE    100
#define MAX_CHECK_PACKET 0x8000
#define ULI526X_10MHF      0
#define ULI526X_100MHF     1
#define ULI526X_10MFD      4
#define ULI526X_100MFD     5
#define ULI526X_AUTO       8
#define ULI526X_TXTH_72 0x400000
#define ULI526X_TXTH_96 0x404000
#define ULI526X_TXTH_128    0x0000
#define ULI526X_TXTH_256    0x4000
#define ULI526X_TXTH_512    0x8000
#define ULI526X_TXTH_1K 0xC000
#define CR9_SROM_READ   0x4800
#define CR9_SRCS        0x1
#define CR9_SRCLK       0x2
#define CR9_CRDOUT      0x8
#define SROM_DATA_0     0x0
#define SROM_DATA_1     0x4
#define PHY_DATA_1      0x20000
#define PHY_DATA_0      0x00000
#define MDCLKH          0x10000
#define PHY_POWER_DOWN  0x800
#define SROM_V41_CODE   0x14
#define FLT_SHIFT 0

/* minimal helper macros to model driver I/O helpers */
#define uw32(_reg, _val) pcibase_reg_write(s, (_reg), (_val))
#define ur32(_reg)       pcibase_reg_read(s, (_reg))

enum uli526x_offsets {
    DCR0 = 0x00, DCR1 = 0x08, DCR2 = 0x10, DCR3 = 0x18, DCR4 = 0x20,
    DCR5 = 0x28, DCR6 = 0x30, DCR7 = 0x38, DCR8 = 0x40, DCR9 = 0x48,
    DCR10 = 0x50, DCR11 = 0x58, DCR12 = 0x60, DCR13 = 0x68, DCR14 = 0x70,
    DCR15 = 0x78
};

enum uli526x_CR6_bits {
    CR6_RXSC = 0x2, CR6_PBF = 0x8, CR6_PM = 0x40, CR6_PAM = 0x80,
    CR6_FDM = 0x200, CR6_TXSC = 0x2000, CR6_STI = 0x100000,
    CR6_SFT = 0x200000, CR6_RXA = 0x40000000, CR6_NO_PURGE = 0x20000000
};

typedef uint32_t aceaddr;

struct tx_desc {
    aceaddr addr;
    uint32_t flagsize;
    uint32_t vlanres;
};

struct rx_desc {
    aceaddr addr;

    uint16_t size;
    uint16_t idx;

    uint16_t flags;
    uint16_t type;

    uint16_t tcp_udp_csum;
    uint16_t ip_csum;

    uint16_t vlan;
    uint16_t err_flags;

    uint32_t reserved;
    uint32_t opague;
};

struct uli_phy_ops_qemu {
    void (*write)(void *, uint8_t, uint8_t, uint16_t);
    uint16_t (*read)(void *, uint8_t, uint8_t);
};

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    dma_addr_t buf_pool_dma_ptr;
    dma_addr_t buf_pool_dma_start;
    dma_addr_t desc_pool_dma_ptr;
    dma_addr_t first_tx_desc_dma;
    dma_addr_t first_rx_desc_dma;

    uint8_t *buf_pool_ptr;
    uint8_t *buf_pool_start;
    uint8_t *desc_pool_ptr;
    struct tx_desc *first_tx_desc;
    struct tx_desc *tx_insert_ptr;
    struct tx_desc *tx_remove_ptr;
    struct rx_desc *first_rx_desc;
    struct rx_desc *rx_insert_ptr;
    struct rx_desc *rx_ready_ptr;

    uint32_t cr0_data;
    uint32_t cr5_data;
    uint32_t cr6_data;
    uint32_t cr7_data;
    uint32_t cr15_data;

    uint16_t dbug_cnt;
    uint16_t NIC_capability;
    uint16_t PHY_reg4;

    uint8_t media_mode;
    uint8_t op_mode;
    uint8_t phy_addr;
    uint8_t link_failed;
    uint8_t wait_reset;

    unsigned long tx_packet_cnt;
    unsigned long rx_avail_cnt;
    unsigned long interval_rx_cnt;

    unsigned long tx_fifo_underrun;
    unsigned long tx_loss_carrier;
    unsigned long tx_no_carrier;
    unsigned long tx_late_collision;
    unsigned long tx_excessive_collision;
    unsigned long tx_jabber_timeout;
    unsigned long reset_count;
    unsigned long reset_cr8;
    unsigned long reset_fatal;
    unsigned long reset_TXtimeout;

    uint8_t srom[128];
    uint8_t init;

    struct uli_phy_ops_qemu phy;

    uint32_t regs[16];

    QEMUTimer timer;
    uint64_t jiffies;
    uint64_t last_tx_start;
};

#define Other_Addition_Info_Defin

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

static inline uint32_t pcibase_reg_read(PCIBaseState *s, uint32_t off)
{
    unsigned idx = off >> 3; /* registers at 0,8,10,... */
    if (idx < 16) {
        return s->regs[idx];
    }
    return 0;
}

static inline void pcibase_reg_write(PCIBaseState *s, uint32_t off, uint32_t val)
{
    unsigned idx = off >> 3;
    if (idx < 16) {
        s->regs[idx] = val;
    }
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    MemoryRegion *mr;

    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    }

    (void)errp; /* currently unused but kept for API compatibility */
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)size;

    /* Map MMIO access to the same registers as PIO (DCRx) */
    if (addr <= DCR15) {
        uint32_t off = addr & ~0x7u;
        uint32_t val = pcibase_reg_read(s, off);
        return val;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=%"PRIx64" size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)size;

    if (addr <= DCR15) {
        uint32_t off = addr & ~0x7u;
        pcibase_reg_write(s, off, (uint32_t)val);
        /* Track some control registers for software-visible state */
        switch (off) {
        case DCR0:
            s->cr0_data = (uint32_t)val;
            break;
        case DCR5:
            s->cr5_data = (uint32_t)val;
            break;
        case DCR6:
            s->cr6_data = (uint32_t)val;
            break;
        case DCR7:
            s->cr7_data = (uint32_t)val;
            break;
        case DCR15:
            s->cr15_data = (uint32_t)val;
            break;
        default:
            break;
        }
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=%"PRIx64" val=%"PRIx64" size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)size;

    if (addr <= DCR15) {
        uint32_t off = addr & ~0x7u;
        uint32_t val = pcibase_reg_read(s, off);
        return val;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%"PRIx64" size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)size;

    if (addr <= DCR15) {
        uint32_t off = addr & ~0x7u;
        pcibase_reg_write(s, off, (uint32_t)val);
        switch (off) {
        case DCR0:
            /* MAC reset and CR0 programming as seen in driver */
            s->cr0_data = (uint32_t)val;
            break;
        case DCR5:
            s->cr5_data = (uint32_t)val;
            break;
        case DCR6:
            /* update_cr6() writes CR6 and then delays; we only store value */
            s->cr6_data = (uint32_t)val;
            break;
        case DCR7:
            /* Interrupt mask register as used by driver */
            s->cr7_data = (uint32_t)val;
            break;
        case DCR15:
            /* Tx jabber and Rx watchdog timer as per driver */
            s->cr15_data = (uint32_t)val;
            break;
        case DCR8:
            /* DCR8 is used by timer as an error indicator; no extra behavior */
            break;
        case DCR9:
            /* SROM clocking is implemented in driver via successive writes */
            break;
        default:
            break;
        }
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%"PRIx64" val=%"PRIx64" size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);
    msi_reset(pdev);
    msix_reset(pdev);

    s->cr0_data = CR0_DEFAULT;
    s->cr5_data = 0;
    s->cr6_data = CR6_DEFAULT;
    s->cr7_data = CR7_DEFAULT;
    s->cr15_data = CR15_DEFAULT;
    s->dbug_cnt = 0;
    s->NIC_capability = 0;
    s->PHY_reg4 = 0;
    s->media_mode = 0;
    s->op_mode = 0;
    s->phy_addr = 0;
    s->link_failed = 1;
    s->wait_reset = 0;
    s->tx_packet_cnt = 0;
    s->rx_avail_cnt = 0;
    s->interval_rx_cnt = 0;
    s->tx_fifo_underrun = 0;
    s->tx_loss_carrier = 0;
    s->tx_no_carrier = 0;
    s->tx_late_collision = 0;
    s->tx_excessive_collision = 0;
    s->tx_jabber_timeout = 0;
    s->reset_count = 0;
    s->reset_cr8 = 0;
    s->reset_fatal = 0;
    s->reset_TXtimeout = 0;
    memset(s->srom, 0, sizeof(s->srom));
    s->init = 0;
    s->jiffies = 0;
    s->last_tx_start = 0;

    memset(s->regs, 0, sizeof(s->regs));

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }
}

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    (void)pdev;
    (void)errp;
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = ULI526X_IO_SIZE;
    s->bar_info[0].name = "uli526x-io";
    s->bar_info[0].sparse = false;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    pcibase_dma_device_realize(pdev, errp);

    pci_conf[PCI_INTERRUPT_PIN] = 1;

    s->has_msi = false;
    s->has_msix = false;

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
    }
    if (s->has_msi) {
        msi_uninit(pdev);
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

    k->vendor_id = ULI526X_VENDOR_ID;
    k->device_id = ULI5261_DEVICE_ID;
    k->revision  = 0x00;
    k->class_id  = ULI526X_CLASS_ID;

    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static void pcibase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
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

static void pcibase_register_types_wrapper(void)
{
    pcibase_register_types();
}

type_init(pcibase_register_types_wrapper);
