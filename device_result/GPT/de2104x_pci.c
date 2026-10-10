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

#define TYPE_PCIBASE_DEVICE "de2104x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define __NETIF_MSG_BIT(bit)   ((u32)1 << (bit))
#define __USER_HZ  100
#define HZ __USER_HZ
#define CONFIG_DE2104X_DSL 0
#define DRV_NAME        "de2104x"
#define DE_RX_RING_SIZE     128
#define DE_TX_RING_SIZE     64
#define NEXT_RX(N)      (((N) + 1) & (DE_RX_RING_SIZE - 1))
#define NEXT_TX(N)      (((N) + 1) & (DE_TX_RING_SIZE - 1))
#define TX_TIMEOUT      (6*HZ)
#define PKT_BUF_SZ      1536
#define DRV_RELDATE     "Mar 17, 2004"
#define DSL             CONFIG_DE2104X_DSL
#define TX_BUFFS_AVAIL(CP)                    \
    (((CP)->tx_tail <= (CP)->tx_head) ?           \
      (CP)->tx_tail + (DE_TX_RING_SIZE - 1) - (CP)->tx_head : \
      (CP)->tx_tail - (CP)->tx_head - 1)
#define NETIF_MSG_DRV        __NETIF_MSG(DRV)
#define NETIF_MSG_PROBE      __NETIF_MSG(PROBE)
#define NETIF_MSG_LINK       __NETIF_MSG(LINK)
#define NETIF_MSG_IFDOWN     __NETIF_MSG(IFDOWN)
#define NETIF_MSG_IFUP       __NETIF_MSG(IFUP)
#define NETIF_MSG_RX_ERR     __NETIF_MSG(RX_ERR)
#define NETIF_MSG_TX_ERR     __NETIF_MSG(TX_ERR)
#define DE_DEF_MSG_ENABLE   (NETIF_MSG_DRV      | \
                 NETIF_MSG_PROBE   | \
                 NETIF_MSG_LINK    | \
                 NETIF_MSG_IFDOWN  | \
                 NETIF_MSG_IFUP    | \
                 NETIF_MSG_RX_ERR  | \
                 NETIF_MSG_TX_ERR)
#define DE_RING_BYTES       \
        ((sizeof(struct de_desc) * DE_RX_RING_SIZE) +   \
        (sizeof(struct de_desc) * DE_TX_RING_SIZE))
#define RX_OFFSET       2
#define DE_SETUP_SKB        ((struct sk_buff *) 1)
#define DE_DUMMY_SKB        ((struct sk_buff *) 2)
#define DE_SETUP_FRAME_WORDS    96
#define DE_EEPROM_WORDS     256
#define DE_EEPROM_SIZE      (DE_EEPROM_WORDS * sizeof(u16))
#define DE_MAX_MEDIA        5
#define DE_MEDIA_TP_AUTO    0
#define DE_MEDIA_BNC        1
#define DE_MEDIA_AUI        2
#define DE_MEDIA_TP     3
#define DE_MEDIA_TP_FD      4
#define DE_MEDIA_INVALID    DE_MAX_MEDIA
#define DE_MEDIA_FIRST      0
#define DE_MEDIA_LAST       (DE_MAX_MEDIA - 1)
#define __ETHTOOL_LINK_MODE_LEGACY_MASK(bit) (1U << (bit))
#define SUPPORTED_AUI            __ETHTOOL_LINK_MODE_LEGACY_MASK(AUI)
#define SUPPORTED_BNC            __ETHTOOL_LINK_MODE_LEGACY_MASK(BNC)
#define DE_AUI_BNC      (SUPPORTED_AUI | SUPPORTED_BNC)
#define DE_TIMER_LINK       (60 * HZ)
#define DE_TIMER_NO_LINK    (5 * HZ)
#define DE_NUM_REGS     16
#define DE_REGS_SIZE        (DE_NUM_REGS * sizeof(u32))
#define DE_REGS_VER     1
#define FULL_DUPLEX_MAGIC   0x6969
#define PCI_VENDOR_ID_DEC        0x1011
#define PCI_DEVICE_ID_DEC_TULIP     0x0002
#define PCI_DEVICE_ID_DEC_TULIP_PLUS 0x0014
#define PCI_ANY_ID (~0)
#define PCI_CLASS_NETWORK_ETHERNET  0x0200

#define u8  unsigned char
#define u16 unsigned short
#define u32 unsigned int

/* Minimal definition to fix incomplete type error; table is unused in QEMU. */
struct pci_device_id {
    u32 vendor;
    u32 device;
    u32 subvendor;
    u32 subdevice;
    u32 class;
    u32 class_mask;
    unsigned long driver_data;
};

/* BAR index used by driver: pci_resource_start(pdev, 1) */

struct rb_node {
    unsigned long  __rb_parent_color;
    struct rb_node *rb_right;
    struct rb_node *rb_left;
};

struct list_head {
    struct list_head *next, *prev;
};

struct llist_node {
    struct llist_node *next;
};

struct hlist_node {
    struct hlist_node *next, **pprev;
};

#define NR_LOCKDEP_CACHING_CLASSES 2

struct lock_class_key;
struct lock_class;

struct lockdep_map {
    struct lock_class_key      *key;
    struct lock_class          *class_cache[NR_LOCKDEP_CACHING_CLASSES];
    const char                  *name;
    u8                          wait_type_outer;
    u8                          wait_type_inner;
    u8                          lock_type;
#ifdef CONFIG_LOCK_STAT
    int                         cpu;
    unsigned long               ip;
#endif
};

struct timer_list {
    struct hlist_node   entry;
    unsigned long       expires;
    void            (*function)(struct timer_list *);
    u32             flags;
#ifdef CONFIG_LOCKDEP
    struct lockdep_map  lockdep_map;
#endif
};

struct sk_buff {
    union {
        struct {
            struct sk_buff      *next;
            struct sk_buff      *prev;
            union {
                struct net_device   *dev;
                unsigned long       dev_scratch;
            };
        };
        struct rb_node      rbnode;
        struct list_head    list;
        struct llist_node   ll_node;
    };
};

struct ring_info {
    struct sk_buff      *skb;
    dma_addr_t      mapping;
};

struct de_srom_media_block {
    u8          opts;
    u16         csr13;
    u16         csr14;
    u16         csr15;
};

struct de_srom_info_leaf {
    u16         default_media;
    u8          n_blocks;
    u8          unused;
};

struct de_desc {
    uint32_t          opts1;
    uint32_t          opts2;
    uint32_t          addr1;
    uint32_t          addr2;
};

struct media_info {
    u16         type;
    u16         csr13;
    u16         csr14;
    u16         csr15;
};

struct de_private {
    unsigned        tx_head;
    unsigned        tx_tail;
    unsigned        rx_tail;
    void            *regs;
    void            *dev;
    void            *lock;
    struct de_desc      *rx_ring;
    struct de_desc      *tx_ring;
    struct ring_info    tx_skb[DE_TX_RING_SIZE];
    struct ring_info    rx_skb[DE_RX_RING_SIZE];
    unsigned        rx_buf_sz;
    dma_addr_t      ring_dma;
    u32         msg_enable;
    struct pci_dev      *pdev;
    u16         setup_frame[DE_SETUP_FRAME_WORDS];
    u32         media_type;
    u32         media_supported;
    u32         media_advertise;
    struct media_info   media[DE_MAX_MEDIA];
    struct timer_list   media_timer;
    u8          *ee_data;
    unsigned        board_idx;
    unsigned        de21040 : 1;
    unsigned        media_lock : 1;
};

/* Variables and tables below are declared to mirror the driver but are
 * deliberately unused in this QEMU model. Kept for completeness. */
static const u32 de_intr_mask =
    0; /* placeholder, exact bits unknown in this QEMU model */
static const u32 de_bus_mode = 0;

static const struct pci_device_id de_pci_tbl[] = {
    { PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_DEC_TULIP,
      PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
    { PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_DEC_TULIP_PLUS,
      PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1 },
    { },
};

static const char * const media_name[DE_MAX_MEDIA] QEMU_USED = {
    "10baseT auto",
    "BNC",
    "AUI",
    "10baseT-HD",
    "10baseT-FD"
};

#define DE2104X_VENDOR_ID   PCI_VENDOR_ID_DEC
#define DE2104X_DEVICE_ID   PCI_DEVICE_ID_DEC_TULIP
#define DE2104X_CLASS_ID    PCI_CLASS_NETWORK_ETHERNET

/* Register offsets used via dr32/dw32 macros in driver */
#define CSR11   0x58
#define CSR13   0x68
#define CSR14   0x70
#define CSR15   0x78

/* The following register identifiers are accessed by dr32()/dw32() but
 * their numeric offsets are not provided in the snippet. Declare them
 * with concrete values matching the classic Tulip layout so that the
 * driver's macros can operate on them.
 */

enum {
    BusMode    = 0x00, /* CSR0 */
    TxPoll     = 0x08, /* CSR1 */
    RxRingAddr = 0x18, /* CSR3 */
    TxRingAddr = 0x20, /* CSR4 */
    MacMode    = 0x28, /* CSR6 */
    IntrMask   = 0x3C, /* CSR7 */
    MacStatus  = 0x38, /* CSR5 */
    RxMissed   = 0x40, /* CSR8 */
    SIAStatus  = CSR11,
};

/* Bit masks referenced in driver but not defined numerically here */
#define DescOwn        (1U << 31)
#define LastFrag       (1U << 30)
#define TxError        (1U << 29)
#define TxOWC          (1U << 28)
#define TxMaxCol       (1U << 27)
#define TxLinkFail     (1U << 26)
#define TxFIFOUnder    (1U << 25)
#define RingEnd        (1U << 15)
#define RxMissedOver   (1U << 16)
#define RxMissedMask   0xFFFFU
#define RxState        (1U << 0)
#define TxState        (1U << 1)
#define RxTx           (1U << 2)
#define FullDuplex     (1U << 3)
#define MacModeClear   0xFFFFFFFFU
#define LinkPass       (1U << 0)
#define LinkFail       (1U << 1)
#define NWayState      (1U << 0)
#define NWayRestart    (1U << 1)

#define RX_ERR          0x40


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

    /* Simple model state: 16 CSRs of 32 bits */
    uint32_t regs[DE_NUM_REGS];
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

static uint32_t pcibase_reg_index(hwaddr addr)
{
    return (uint32_t)(addr >> 2);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size > DE_REGS_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] mmio_read out of range addr=%" PRIx64 " size=%u\n",
                      TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
        return 0;
    }

    switch (size) {
    case 1: {
        unsigned idx = pcibase_reg_index(addr);
        unsigned shift = (addr & 3) * 8;
        val = (s->regs[idx] >> shift) & 0xff;
        break;
    }
    case 2: {
        unsigned idx = pcibase_reg_index(addr);
        unsigned shift = (addr & 3) * 8;
        val = (s->regs[idx] >> shift) & 0xffff;
        break;
    }
    case 4: {
        unsigned idx = pcibase_reg_index(addr);
        val = s->regs[idx];
        break;
    }
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] mmio_read invalid size=%u\n", TYPE_PCIBASE_DEVICE, size);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > DE_REGS_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] mmio_write out of range addr=%" PRIx64 " size=%u\n",
                      TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
        return;
    }

    switch (size) {
    case 1: {
        unsigned idx = pcibase_reg_index(addr);
        unsigned shift = (addr & 3) * 8;
        uint32_t mask = 0xffu << shift;
        uint32_t v = s->regs[idx];
        v = (v & ~mask) | ((uint32_t)(val & 0xffu) << shift);
        s->regs[idx] = v;
        break;
    }
    case 2: {
        unsigned idx = pcibase_reg_index(addr);
        unsigned shift = (addr & 3) * 8;
        uint32_t mask = 0xffffu << shift;
        uint32_t v = s->regs[idx];
        v = (v & ~mask) | ((uint32_t)(val & 0xffffu) << shift);
        s->regs[idx] = v;
        break;
    }
    case 4: {
        unsigned idx = pcibase_reg_index(addr);
        s->regs[idx] = (uint32_t)val;
        break;
    }
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] mmio_write invalid size=%u\n", TYPE_PCIBASE_DEVICE, size);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
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
    int i;

    pci_device_reset(pdev);

    for (i = 0; i < DE_NUM_REGS; i++) {
        s->regs[i] = 0;
    }

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
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
    case 1: val &= 0xFF; break;
    case 2: val &= 0xFFFF; break;
    case 4: break;
    default: break;
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
    int i;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  DE2104X_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DE2104X_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, DE2104X_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    s->num_bars = 1;
    s->bar_info[0].index = 1;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = DE_REGS_SIZE;
    s->bar_info[0].name = "de2104x-mmio";
    s->bar_info[0].sparse = false;

    for (i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }
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

