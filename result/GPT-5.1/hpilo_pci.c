/*
 * QEMU PCI model for HP iLO (hpilo) adapter
 *
 * Corrected for QEMU 8.2.x, based solely on Linux driver drivers/misc/hpilo.c
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

#define TYPE_PCIBASE_DEVICE "hpilo_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Constants and macros imported from driver */
#define ENTRY_BITPOS_DESCRIPTOR  10
#define ENTRY_MASK_DESCRIPTOR \
    (((1 << ENTRY_BITS_DESCRIPTOR) - 1) << ENTRY_BITPOS_DESCRIPTOR)
#define ENTRY_MASK_QWORDS \
    (((1 << ENTRY_BITS_QWORDS) - 1) << ENTRY_BITPOS_QWORDS)
#define ENTRY_BITPOS_QWORDS      0
#define L2_QENTRY_SZ    12
#define FIFOBARTOHANDLE(_fifo) \
    ((struct fifo *)(((char *)(_fifo)) - FIFOHANDLESIZE))
#define ENTRY_MASK_NOSTATE (ENTRY_MASK >> (ENTRY_BITS_C + ENTRY_BITS_O))
#define ENTRY_MASK_O (((1 << ENTRY_BITS_O) - 1) << ENTRY_BITPOS_O)
#define ENTRY_MASK ((1 << ENTRY_BITS_TOTAL) - 1)
#define ENTRY_MASK_C (((1 << ENTRY_BITS_C) - 1) << ENTRY_BITPOS_C)
#define SENDQ        1
#define CTRL_BITPOS_FIFOINDEXMASK    4
#define CTRL_BITPOS_DESCLIMIT        18
#define CTRL_BITPOS_G                31
#define CTRL_BITPOS_A                30
#define CTRL_BITPOS_L2SZ             0
#define FIFOHANDLESIZE (sizeof(struct fifo))
#define WAIT_TIME    10
#define MAX_WAIT    (MAX_WAIT_TIME / WAIT_TIME)
#define ILO_CACHE_SZ      128
#define L2_DB_SIZE        14
#define NR_QENTRY        4
#define ILO_START_ALIGN    4096
#define ILOHW_CCB_SZ    128
#define RECVQ        2
#define DB_OUT        0xD4
#define DB_RESET    26
#define DB_IRQ        0xB2
#define ONE_DB_SIZE        (1 << L2_DB_SIZE)
#define PCI_REV_ID_NECHES    7
#define MAX_ILO_DEV    1
#define MIN_CCB        8
#define ILO_NAME "hpilo"
#define MAX_CCB           24
#define MAX_OPEN    (MAX_CCB * MAX_ILO_DEV)
#define ENTRY_BITS_DESCRIPTOR    12
#define ENTRY_BITS_QWORDS        10
#define ENTRY_BITS_C             1
#define ENTRY_BITS_O             1
#define ENTRY_BITPOS_O           23
#define ENTRY_BITS_TOTAL    \
    (ENTRY_BITS_C + ENTRY_BITS_O + \
     ENTRY_BITS_QWORDS + ENTRY_BITS_DESCRIPTOR)
#define ENTRY_BITPOS_C           22
#define MAX_WAIT_TIME    10000

/* Local definition for missing vendor macro to satisfy compiler. */
#ifndef PCI_VENDOR_ID_COMPAQ
#define PCI_VENDOR_ID_COMPAQ 0x0E11
#endif

#define HPILO_VENDOR_ID PCI_VENDOR_ID_COMPAQ
#define HPILO_DEVICE_ID 0xB204
#define HPILO_CLASS_ID  PCI_CLASS_OTHERS

struct fifo {
    uint64_t nrents;    /* user requested number of fifo entries */
    uint64_t imask;  /* mask to extract valid fifo index */
    uint64_t merge;    /*  O/C bits to merge in during enqueue operation */
    uint64_t reset;    /* set to non-zero when the target device resets */
    uint8_t  pad_0[ILO_CACHE_SZ - (sizeof(uint64_t) * 4)];

    uint64_t head;
    uint8_t  pad_1[ILO_CACHE_SZ - (sizeof(uint64_t))];

    uint64_t tail;
    uint8_t  pad_2[ILO_CACHE_SZ - (sizeof(uint64_t))];

    uint64_t fifobar[];
};

struct ilo_hwinfo {
    char *mmio_vaddr;
    char *db_vaddr;
    char *ram_vaddr;
    void *ccb_alloc[MAX_CCB];
    void *ilo_dev;
    unsigned long open_lock;
    unsigned long alloc_lock;
    unsigned long fifo_lock;
    uint8_t cdev_pad[1];
};

struct ccb {
    union {
        char *send_fifobar;
        uint64_t send_fifobar_pa;
    } ccb_u1;
    union {
        char *send_desc;
        uint64_t send_desc_pa;
    } ccb_u2;
    uint64_t send_ctrl;

    union {
        char *recv_fifobar;
        uint64_t recv_fifobar_pa;
    } ccb_u3;
    union {
        char *recv_desc;
        uint64_t recv_desc_pa;
    } ccb_u4;
    uint64_t recv_ctrl;

    union {
        char *db_base;
        uint64_t padding5;
    } ccb_u5;

    uint64_t channel;
};

struct ccb_data {
    struct ccb  driver_ccb;
    struct ccb  ilo_ccb;
    struct ccb *mapped_ccb;
    void       *dma_va;
    uint64_t    dma_pa;
    size_t      dma_size;
    struct ilo_hwinfo *ilo_hw;
    uint8_t     ccb_waitq_pad[1];
    int         ccb_cnt;
    int         ccb_excl;
};

/* BAR metadata */
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

    /* simple model state */
    uint8_t pci_rev_id;
    int current_irq_level;
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

/* MMIO behavior derived from driver: DB_OUT, DB_IRQ live in mmio_vaddr */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (s->mmio_backing && addr + size <= s->mmio_backing_size) {
        uint64_t val = 0;
        memcpy(&val, s->mmio_backing + addr, size);
        return val;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (s->mmio_backing && addr + size <= s->mmio_backing_size) {
        uint64_t tmp = val;
        memcpy(s->mmio_backing + addr, &tmp, size);

        /* model interrupt enable/disable on DB_IRQ bit0 */
        if (addr == DB_IRQ && size >= 1) {
            if (s->mmio_backing[DB_IRQ] & 0x1) {
                /* enable interrupts */
            } else {
                /* disable interrupts */
            }
        }
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }

    s->current_irq_level = 0;
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
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* basic PCI IDs */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  HPILO_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  HPILO_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, HPILO_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    s->pci_rev_id = 0x01;

    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BARs from driver usage: see hpilo.c */

    s->num_bars = 3;

    /* BAR1: mmio_vaddr (use minimal power-of-two size >= offsets) */
    s->bar_info[0].index = 1;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x1000; /* 4KB, power of two */
    s->bar_info[0].name  = "hpilo-mmio";
    s->bar_info[0].sparse = false;

    /* BAR2: RAM for CCBs; size must be power of two, keep >= MAX_CCB * ILOHW_CCB_SZ (3072) */
    s->bar_info[1].index = 2;
    s->bar_info[1].type  = BAR_TYPE_RAM;
    s->bar_info[1].size  = 0x1000; /* 4KB, minimal power-of-two >= 3072 */
    s->bar_info[1].name  = "hpilo-ram";
    s->bar_info[1].sparse = false;

    /* BAR3: DB region; (MAX_CCB * ONE_DB_SIZE) is already power of two (24 * 16384 = 393216 = 0x60000) */
    s->bar_info[2].index = 3;
    s->bar_info[2].type  = BAR_TYPE_MMIO;
    s->bar_info[2].size  = 0x10000;
    s->bar_info[2].name  = "hpilo-db";
    s->bar_info[2].sparse = false;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    /* Allocate a simple backing store for BAR1 mmio region for DB_OUT/DB_IRQ */
    s->mmio_backing_size = s->bar_info[0].size;
    s->mmio_backing = g_malloc0(s->mmio_backing_size);

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    s->has_msi = false;
    s->has_msix = false;
    s->current_irq_level = 0;

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

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
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

