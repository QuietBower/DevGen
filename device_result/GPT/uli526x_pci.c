/*
 * QEMU PCI ULi 526x Ethernet (minimal functional model for uli526x.c)
 * Target QEMU: 8.2.10
 */

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

#define TYPE_PCIBASE_DEVICE "uli526x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define RX_DESC_CNT     0x30
#define TX_DESC_CNT     0x20
#define MAX_PACKET_SIZE 1514
#define TX_FREE_DESC_CNT (TX_DESC_CNT - 2)
#define TX_WAKE_DESC_CNT (TX_DESC_CNT - 3)
#define DESC_ALL_CNT    (TX_DESC_CNT + RX_DESC_CNT)
#define TX_BUF_ALLOC    0x600
#define RX_ALLOC_SIZE   0x620
#define CR0_DEFAULT     0
#define CR6_DEFAULT     0x22200000
#define CR7_DEFAULT     0x180c1
#define CR15_DEFAULT    0x06
#define TDES0_ERR_MASK  0x4302
#define RX_COPY_SIZE    100
#define MAX_CHECK_PACKET 0x8000
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
#define PCI_ULI5261_ID  0x526110B9
#define PCI_ULI5263_ID  0x526310B9
#define ULI526X_IO_SIZE 0x100
#define ULI526X_RESET    1
#define ULI5261_MAX_MULTICAST 14
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
#define FLT_SHIFT 0

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

/* Removed unused static int mode = 8; to avoid compiler warnings treated as errors */

struct tx_desc {
    uint32_t tdes0, tdes1, tdes2, tdes3;
    char *tx_buf_ptr;
    struct tx_desc *next_tx_desc;
};

struct rx_desc {
    uint32_t rdes0, rdes1, rdes2, rdes3;
    void *rx_skb_ptr;
    struct rx_desc *next_rx_desc;
};

struct uli526x_board_info {
    struct uli_phy_ops_inner {
        void (*write)(struct uli526x_board_info *, uint8_t, uint8_t, uint16_t);
        uint16_t (*read)(struct uli526x_board_info *, uint8_t, uint8_t);
    } phy;
    void *next_dev;
    void *pdev;
    unsigned long lock;

    void *ioaddr;
    uint32_t cr0_data;
    uint32_t cr5_data;
    uint32_t cr6_data;
    uint32_t cr7_data;
    uint32_t cr15_data;

    unsigned long buf_pool_dma_ptr;
    unsigned long buf_pool_dma_start;
    unsigned long desc_pool_dma_ptr;
    unsigned long first_tx_desc_dma;
    unsigned long first_rx_desc_dma;

    unsigned char *buf_pool_ptr;
    unsigned char *buf_pool_start;
    unsigned char *desc_pool_ptr;
    struct tx_desc *first_tx_desc;
    struct tx_desc *tx_insert_ptr;
    struct tx_desc *tx_remove_ptr;
    struct rx_desc *first_rx_desc;
    struct rx_desc *rx_insert_ptr;
    struct rx_desc *rx_ready_ptr;
    unsigned long tx_packet_cnt;
    unsigned long rx_avail_cnt;
    unsigned long interval_rx_cnt;

    uint16_t dbug_cnt;
    uint16_t NIC_capability;
    uint16_t PHY_reg4;

    uint8_t media_mode;
    uint8_t op_mode;
    uint8_t phy_addr;
    uint8_t link_failed;
    uint8_t wait_reset;

    unsigned long timer;

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

    unsigned char srom[128];
    uint8_t init;
};

struct uli_phy_ops {
    void (*write)(struct uli526x_board_info *, uint8_t, uint8_t, uint16_t);
    uint16_t (*read)(struct uli526x_board_info *, uint8_t, uint8_t);
};

/* Removed unused static uint32_t cr6set; to avoid compiler warnings treated as errors */
/* Removed unused static int uli526x_debug; to avoid compiler warnings treated as errors */
/* Removed unused static uint32_t uli526x_cr6_user_set; to avoid compiler warnings treated as errors */

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
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

    /* simple register shadows */
    uint32_t regs[16]; /* DCR0..DCR15, 8-byte spaced but we model as 32-bit */

    /* interrupt line state */
    bool irq_level;
};

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
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static uint32_t uli_reg_index(hwaddr addr)
{
    switch (addr) {
    case DCR0:  return 0;
    case DCR1:  return 1;
    case DCR2:  return 2;
    case DCR3:  return 3;
    case DCR4:  return 4;
    case DCR5:  return 5;
    case DCR6:  return 6;
    case DCR7:  return 7;
    case DCR8:  return 8;
    case DCR9:  return 9;
    case DCR10: return 10;
    case DCR11: return 11;
    case DCR12: return 12;
    case DCR13: return 13;
    case DCR14: return 14;
    case DCR15: return 15;
    default:    return 0xFFFFFFFFU;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t idx;

    if (size != 4) {
        return 0;
    }

    idx = uli_reg_index(addr);
    if (idx != 0xFFFFFFFFU) {
        return s->regs[idx];
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read unknown addr=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t idx;

    if (size != 4) {
        return;
    }

    idx = uli_reg_index(addr);
    if (idx == 0xFFFFFFFFU) {
        qemu_log_mask(LOG_UNIMP, "[%s] mmio_write unknown addr=%" PRIx64 " val=%" PRIx64 " size=%u\n",
                      TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
        return;
    }

    switch (addr) {
    case DCR0:
        s->regs[idx] = (uint32_t)val;
        if (val & ULI526X_RESET) {
            /* emulate reset by clearing other regs */
            for (int i = 1; i < 16; i++) {
                s->regs[i] = 0;
            }
        }
        break;
    case DCR5:
        /* write-to-clear interrupt status; keep only bits not written as 1 */
        s->regs[idx] &= ~((uint32_t)val);
        /* if no more interrupt bits, drop INTx */
        if (!(s->regs[idx] & CR7_DEFAULT) && s->irq_level) {
            pci_set_irq(PCI_DEVICE(s), 0);
            s->irq_level = false;
        }
        break;
    case DCR7:
        s->regs[idx] = (uint32_t)val;
        break;
    case DCR1:
        s->regs[idx] = (uint32_t)val;
        break;
    case DCR3:
    case DCR4:
    case DCR6:
    case DCR8:
    case DCR9:
    case DCR10:
    case DCR11:
    case DCR12:
    case DCR13:
    case DCR14:
    case DCR15:
    case DCR2:
    default:
        s->regs[idx] = (uint32_t)val;
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* This device uses only BAR0 in I/O space in the driver, but all access
     * is through pci_iomap() which may map I/O or MMIO. We model registers
     * via MMIO BAR, keep PIO BAR as log-only. */
    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[0] = CR0_DEFAULT;
    s->regs[6] = CR6_DEFAULT;
    s->regs[7] = CR7_DEFAULT;
    s->regs[15] = CR15_DEFAULT;

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }

    if (s->irq_level) {
        pci_set_irq(pdev, 0);
        s->irq_level = false;
    }
}

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    (void)errp;
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
    case 1:
        val &= 0xFF;
        break;
    case 2:
        val &= 0xFFFF;
        break;
    case 4:
    default:
        break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }
    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x10B9);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x5261);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR0: IO region of size 0x100 as required by driver */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_PIO;
    s->bar_info[0].size  = ULI526X_IO_SIZE;
    s->bar_info[0].name  = "uli526x-io";
    s->bar_info[0].sparse = false;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    s->irq_level = false;

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
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

type_init(pcibase_register_types);

