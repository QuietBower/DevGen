/*
 * QEMU device model for Digigram miXart sound card
 * Based on Linux driver sound/pci/mixart/mixart.c
 * Phase 4: Debug & Update - Corrected firmware state machine handling
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
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "snd_mixart_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID PCI_VENDOR_ID_MOTOROLA
#define DEVICE_ID 0x0003
#define CLASS_ID PCI_CLASS_MULTIMEDIA_AUDIO

/* BAR sizes */
#define MIXART_BA0_SIZE (16 * 1024 * 1024)
#define MIXART_BA1_SIZE (4 * 1024)

/* Known offsets in BAR0 (big-endian) */
#define MIXART_PSEUDOREG 0x2000
#define MIXART_PSEUDOREG_BOARDNUMBER       (MIXART_PSEUDOREG+0)
#define MIXART_PSEUDOREG_DBRD_PRESENCE     (MIXART_PSEUDOREG+0x990)
#define MIXART_PSEUDOREG_DBRD_TYPE         (MIXART_PSEUDOREG+0x994)
#define MIXART_PSEUDOREG_MXLX_BASE_ADDR    (MIXART_PSEUDOREG+0x9C)
#define MIXART_PSEUDOREG_MXLX_SIZE         (MIXART_PSEUDOREG+0xA0)
#define MIXART_PSEUDOREG_MXLX_STATUS       (MIXART_PSEUDOREG+0xA4)
#define MIXART_PSEUDOREG_DXLX_BASE_ADDR    (MIXART_PSEUDOREG+0x998)
#define MIXART_PSEUDOREG_DXLX_SIZE         (MIXART_PSEUDOREG+0x99C)
#define MIXART_PSEUDOREG_DXLX_STATUS       (MIXART_PSEUDOREG+0x9A0)
#define MIXART_PSEUDOREG_ELF_STATUS        (MIXART_PSEUDOREG+0xB0)
#define MIXART_PSEUDOREG_PERF_STREAM_LOAD  (MIXART_PSEUDOREG+0x70)
#define MIXART_PSEUDOREG_PERF_SYSTEM_LOAD  (MIXART_PSEUDOREG+0x78)
#define MIXART_PSEUDOREG_PERF_MAILBX_LOAD  (MIXART_PSEUDOREG+0x7C)
#define MIXART_PSEUDOREG_PERF_INTERR_LOAD  (MIXART_PSEUDOREG+0x74)

/* Messaging area offsets in BAR0 */
#define MSG_INBOUND_FREE_HEAD   0x010000
#define MSG_INBOUND_FREE_TAIL   0x010004
#define MSG_INBOUND_POST_HEAD   0x010008
#define MSG_OUTBOUND_FREE_HEAD  0x010010
#define MSG_INBOUND_FREE_STACK  0x100000
#define MSG_INBOUND_POST_STACK  0x104000
#define MSG_OUTBOUND_FREE_STACK 0x10C000

/* BAR1 offsets */
#define MIXART_PCI_OMIMR_OFFSET         0x34
#define MIXART_BA1_BRUTAL_RESET_OFFSET  0x68
#define MIXART_PCI_OMISR_OFFSET         0x30
#define MIXART_PCI_ODBR_OFFSET          0x60

/* Offsets updated from driver source */
#define MIXART_FLOWTABLE_PTR            0x3000
#define MIXART_MOTHERBOARD_XLX_BASE_ADDRESS 0x00600000

/* DSP firmware load states */
#define STATUS_IDLE  0
#define STATUS_COPY  1
#define STATUS_DONE  2
#define STATUS_READY 4

/* Daughterboard status states */
#define DXLX_IDLE        0
#define DXLX_WAIT_BASE   2
#define DXLX_INIT_DONE   3

/* Daughter board types */
#define DAUGHTER_TYPE_MASK 0x0F
#define DAUGHTER_TYPE_NONE 0x00
#define DAUGHTER_TYPE_AES  0x0E

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[2];
    uint8_t *bar0_mem;           /* backing store for BAR0 (16 MB) */
    uint32_t bar1_regs[1024];    /* backing store for BAR1 (4 KB, 32-bit aligned) */

    /* Interrupt registers shadow */
    uint32_t omimr;              /* BAR1 offset 0x34 */

    /* Board identification */
    uint32_t board_number;
    uint32_t dbrd_presence;
    uint32_t dbrd_type;

    /* Firmware state machines */
    uint32_t mxlx_status;
    uint32_t elf_status;
    uint32_t dxlx_status;

    /* Timers for firmware state transitions */
    QEMUTimer *mxlx_timer;       /* 2 -> 4 transition */
    QEMUTimer *elf_timer;        /* 2 -> 4 transition */
    QEMUTimer *dxlx_timer;       /* 1 -> 2 transition */
    QEMUTimer *dxlx_complete_timer; /* 4 -> 3 transition */
};

/* Forward declaration */
static void pcibase_reset(DeviceState *dev);

/* Read from big-endian BAR0 memory */
static uint64_t bar0_read_be(uint8_t *buf, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    for (unsigned i = 0; i < size; i++) {
        val = (val << 8) | buf[addr + i];
    }
    return val;
}

/* Write to big-endian BAR0 memory */
static void bar0_write_be(uint8_t *buf, hwaddr addr, uint64_t val, unsigned size)
{
    for (int i = size - 1; i >= 0; i--) {
        buf[addr + i] = val & 0xff;
        val >>= 8;
    }
}

/* MMIO read handler for BAR0 */
static uint64_t pcibase_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= MIXART_BA0_SIZE) {
        return ~0ULL;
    }

    /* Special handling for pseudo-registers */
    if (addr == MIXART_PSEUDOREG_BOARDNUMBER) {
        val = s->board_number;
    } else if (addr == MIXART_PSEUDOREG_DBRD_PRESENCE) {
        val = s->dbrd_presence;
    } else if (addr == MIXART_PSEUDOREG_DBRD_TYPE) {
        val = s->dbrd_type;
    } else if (addr == MIXART_PSEUDOREG_ELF_STATUS) {
        val = s->elf_status;
    } else if (addr == MIXART_PSEUDOREG_MXLX_STATUS) {
        val = s->mxlx_status;
    } else if (addr == MIXART_PSEUDOREG_DXLX_STATUS) {
        val = s->dxlx_status;
    } else if (addr >= MIXART_PSEUDOREG_PERF_STREAM_LOAD &&
               addr <= MIXART_PSEUDOREG_PERF_INTERR_LOAD) {
        val = 0;
    } else {
        /* Default to backing store */
        val = bar0_read_be(s->bar0_mem, addr, size);
    }

    return val;
}

static void mxlx_timer_cb(void *opaque);
static void elf_timer_cb(void *opaque);
static void dxlx_timer_cb(void *opaque);
static void dxlx_complete_timer_cb(void *opaque);

/* MMIO write handler for BAR0 */
static void pcibase_bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= MIXART_BA0_SIZE) {
        return;
    }

    /* Handle firmware status register writes */
    if (addr == MIXART_PSEUDOREG_MXLX_STATUS) {
        s->mxlx_status = val;
        if (val == 2) {
            timer_mod(s->mxlx_timer,
                      qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 500);
        }
        return;
    }
    if (addr == MIXART_PSEUDOREG_ELF_STATUS) {
        s->elf_status = val;
        if (val == 2) {
            timer_mod(s->elf_timer,
                      qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 300);
        }
        return;
    }
    if (addr == MIXART_PSEUDOREG_DXLX_STATUS) {
        s->dxlx_status = val;
        if (val == 1) {
            timer_mod(s->dxlx_timer,
                      qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 10);
        } else if (val == 4) {
            timer_mod(s->dxlx_complete_timer,
                      qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);
        }
        return;
    }
    if (addr == MIXART_PSEUDOREG_BOARDNUMBER) {
        s->board_number = val;
        return;
    }

    /* Write to backing store for other addresses */
    bar0_write_be(s->bar0_mem, addr, val, size);
}

static void mxlx_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    s->mxlx_status = STATUS_READY;
}

static void elf_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    s->elf_status = STATUS_READY;
    s->dbrd_presence = 1;
    s->dbrd_type = DAUGHTER_TYPE_NONE;
}

static void dxlx_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    s->dxlx_status = DXLX_WAIT_BASE;
    /* Provide a valid base address for daughterboard firmware */
    bar0_write_be(s->bar0_mem, MIXART_PSEUDOREG_DXLX_BASE_ADDR, 0x200000, 4);
}

static void dxlx_complete_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    s->dxlx_status = DXLX_INIT_DONE;
}

/* MMIO read handler for BAR1 */
static uint64_t pcibase_bar1_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= MIXART_BA1_SIZE || (addr & 3) != 0) {
        return ~0ULL;
    }

    uint32_t offset = addr >> 2;
    if (offset < ARRAY_SIZE(s->bar1_regs)) {
        val = s->bar1_regs[offset];
    }
    return val;
}

/* MMIO write handler for BAR1 */
static void pcibase_bar1_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= MIXART_BA1_SIZE || (addr & 3) != 0) {
        return;
    }

    uint32_t offset = addr >> 2;
    if (offset < ARRAY_SIZE(s->bar1_regs)) {
        s->bar1_regs[offset] = val;
    }

    /* Handle known registers */
    if (addr == MIXART_PCI_OMIMR_OFFSET) {
        s->omimr = val;
    } else if (addr == MIXART_BA1_BRUTAL_RESET_OFFSET) {
        if (val) {
            pcibase_reset(DEVICE(s));
        }
    }
}

static const MemoryRegionOps pcibase_bar0_ops = {
    .read = pcibase_bar0_read,
    .write = pcibase_bar0_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_bar1_ops = {
    .read = pcibase_bar1_read,
    .write = pcibase_bar1_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Clear BAR0 memory */
    if (s->bar0_mem) {
        memset(s->bar0_mem, 0, MIXART_BA0_SIZE);
    }
    /* Clear BAR1 registers */
    memset(s->bar1_regs, 0, sizeof(s->bar1_regs));
    s->omimr = 0;

    /* Initialize firmware state to IDLE (firmware not loaded) */
    s->mxlx_status = STATUS_IDLE;
    s->elf_status = STATUS_IDLE;
    s->dxlx_status = DXLX_IDLE;

    /* Set default board identification */
    s->board_number = 0;
    s->dbrd_presence = 0;
    s->dbrd_type = DAUGHTER_TYPE_NONE;

    /* Cancel any pending firmware timers */
    timer_del(s->mxlx_timer);
    timer_del(s->elf_timer);
    timer_del(s->dxlx_timer);
    timer_del(s->dxlx_complete_timer);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Allocate backing store for BAR0 */
    s->bar0_mem = g_malloc0(MIXART_BA0_SIZE);

    /* Initialize BAR1 registers to zero */
    memset(s->bar1_regs, 0, sizeof(s->bar1_regs));

    /* Register BAR0 */
    MemoryRegion *bar0_mr = &s->bar_regions[0];
    memory_region_init_io(bar0_mr, OBJECT(s), &pcibase_bar0_ops, s,
                         "mixart-bar0", MIXART_BA0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, bar0_mr);

    /* Register BAR1 */
    MemoryRegion *bar1_mr = &s->bar_regions[1];
    memory_region_init_io(bar1_mr, OBJECT(s), &pcibase_bar1_ops, s,
                         "mixart-bar1", MIXART_BA1_SIZE);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, bar1_mr);

    /* Set default board values (initial state) */
    s->board_number = 0;
    s->dbrd_presence = 0;
    s->dbrd_type = DAUGHTER_TYPE_NONE;
    s->mxlx_status = STATUS_IDLE;
    s->elf_status = STATUS_IDLE;
    s->dxlx_status = DXLX_IDLE;

    /* Create timers for firmware state transitions */
    s->mxlx_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, mxlx_timer_cb, s);
    s->elf_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, elf_timer_cb, s);
    s->dxlx_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, dxlx_timer_cb, s);
    s->dxlx_complete_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, dxlx_complete_timer_cb, s);
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
    timer_free(s->mxlx_timer);
    timer_free(s->elf_timer);
    timer_free(s->dxlx_timer);
    timer_free(s->dxlx_complete_timer);
    g_free(s->bar0_mem);
    s->bar0_mem = NULL;
}

static const VMStateDescription vmstate_pcibase = {
    .name = "snd_mixart_pci",
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
