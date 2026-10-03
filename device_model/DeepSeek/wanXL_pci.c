/*
 * QEMU PCI device model for wanXL serial card
 * Based on driver wanxl.c
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

#define TYPE_PCIBASE_DEVICE "wanXL_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs (extracted from driver) */
#define PCI_VENDOR_ID_SBE           0x1176
#define PCI_DEVICE_ID_SBE_WANXL100  0x0301
#define PCI_CLASS_ID                0x0280

/* PLX register offsets (PLX_OFFSET = 0) */
#define PLX_OFFSET              0
#define PLX_CONTROL             (PLX_OFFSET + 0x6C)
#define PLX_MAILBOX_0           (PLX_OFFSET + 0x40)
#define PLX_MAILBOX_1           (PLX_OFFSET + 0x44)
#define PLX_MAILBOX_2           (PLX_OFFSET + 0x48)
#define PLX_MAILBOX_5           (PLX_OFFSET + 0x54)
#define PLX_DOORBELL_TO_CARD    (PLX_OFFSET + 0x60)
#define PLX_DOORBELL_FROM_CARD  (PLX_OFFSET + 0x64)
#define BUFFERS_ADDR            0x4000
#define PDM_OFFSET              0x1000

/* Driver constants */
#define TX_BUFFERS              10
#define RX_QUEUE_LENGTH         40
#define RX_BUFFERS              30
#define MAX_PORTS               4

/* Mailbox command constants (from supplementary source) */
#define MBX2_MEMSZ_MASK         0xFFFF0000
#define MBX1_CMD_BSWAP          0x8C000001
#define MBX1_CMD_ABORTJ         0x85000000

/* HDLC maximum frame size, determines buffer length */
#define HDLC_MAX_MRU  1600
#define BUFFER_LENGTH HDLC_MAX_MRU

/* Clock selector */
#define CLOCK_EXT               1

/* Hardware descriptor structures */
typedef struct {
    uint32_t stat;
    uint32_t address;
    uint32_t length;
} desc_t;

typedef struct {
    uint32_t open;
    uint32_t cable;
    uint32_t rx_overruns;
    uint32_t rx_frame_errors;
    uint32_t parity;
    uint32_t encoding;
    uint32_t clocking;
    desc_t tx_descs[TX_BUFFERS];
} port_status_t;

/* Card status structure (DMA'd to host memory) */
typedef struct {
    desc_t rx_descs[RX_QUEUE_LENGTH];
    port_status_t port_status[4];   /* MAX_PORTS */
} card_status_t;

/* Define a per-port state container (minimal for now) */
typedef struct PortState {
    /* placeholder for per-port hardware state */
} PortState;

/* BAR descriptor */
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

/* Main device state */
struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    uint32_t plx_regs[0x80 / 4]; /* BAR0 PLX registers, 32-bit aligned */
    QEMUTimer *fw_cmd_timer;     /* timer to process firmware commands */
    uint32_t fw_cmd;             /* command written to MAILBOX_1 */
    uint8_t *ram_ptr;            /* pointer to BAR2 RAM for direct access */
    dma_addr_t card_status_addr; /* DMA address of card_status */
    uint32_t ramsize;            /* reported on-board RAM size */
    uint32_t intr_status;
    uint32_t intr_mask;

    PortState ports[MAX_PORTS];
    int n_ports;
};

/* Forward declarations */
static void pcibase_update_irq(PCIBaseState *s);

/* Timer callback: process pending firmware mailbox command */
static void pcibase_fw_cmd_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;

    switch (s->fw_cmd) {
    case MBX1_CMD_BSWAP:
        /* Set byte swap mode, command acknowledged */
        s->plx_regs[PLX_MAILBOX_1 / 4] = 0;
        break;
    case MBX1_CMD_ABORTJ:
        /* Firmware initializes, clear command, then set MAILBOX_5 */
        s->plx_regs[PLX_MAILBOX_1 / 4] = 0;
        /* Read card_status DMA address from on-board RAM at PDM_OFFSET+20 */
        if (s->ram_ptr) {
            s->card_status_addr = ldl_le_p(s->ram_ptr + PDM_OFFSET + 20);
        }
        /* Report RAM size (non-zero) to indicate success */
        s->plx_regs[PLX_MAILBOX_5 / 4] = s->ramsize;
        break;
    default:
        break;
    }
    s->fw_cmd = 0;
}

/* Update IRQ line based on doorbell from card register */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->plx_regs[PLX_DOORBELL_FROM_CARD / 4] != 0) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO read handler for BAR0 (PLX registers) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4 || addr >= 0x80) {
        return ~0ULL;
    }

    unsigned index = addr >> 2;
    val = s->plx_regs[index];

    return val;
}

/* MMIO write handler for BAR0 (PLX registers) */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4 || addr >= 0x80) {
        return;
    }

    unsigned index = addr >> 2;
    uint32_t old = s->plx_regs[index];
    s->plx_regs[index] = val;

    /* Side effects for specific registers */
    switch (addr) {
    case PLX_MAILBOX_1:
        /* Firmware command: start processing after a small delay */
        s->fw_cmd = val;
        timer_mod(s->fw_cmd_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 10);
        break;
    case PLX_DOORBELL_FROM_CARD:
        /* Host clears doorbell bits by writing the same value */
        s->plx_regs[index] = old & ~val;
        pcibase_update_irq(s);
        break;
    case PLX_DOORBELL_TO_CARD:
        /* Doorbell to card: not processed for probe, would trigger DMA updates */
        break;
    default:
        break;
    }

    /* Additional side effects can be added here if needed */
}

/* PIO read handler (not used) */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

/* PIO write handler (not used) */
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
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

    memset(s->plx_regs, 0, sizeof(s->plx_regs));
    s->plx_regs[PLX_MAILBOX_2 / 4] = s->ramsize & MBX2_MEMSZ_MASK;
    s->fw_cmd = 0;
    timer_del(s->fw_cmd_timer);
    s->card_status_addr = 0;
    pcibase_update_irq(s);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_SBE);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_SBE_WANXL100);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Set up BAR configuration: BAR0 for PLX registers, BAR2 for on-board RAM */
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x80, .name = "plx-mmio" };
    s->bar_info[1] = (BARInfo){ .index = 2, .type = BAR_TYPE_RAM, .size = 0x400000, .name = "ram" };
    s->num_bars = 2;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize state */
    s->ramsize = 0x400000; /* 4 MB */
    s->fw_cmd_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, pcibase_fw_cmd_timer_cb, s);
    s->fw_cmd = 0;
    s->card_status_addr = 0;
    memset(s->plx_regs, 0, sizeof(s->plx_regs));
    s->plx_regs[PLX_MAILBOX_2 / 4] = s->ramsize & MBX2_MEMSZ_MASK;
    /* Get pointer to BAR2 RAM for direct firmware access */
    s->ram_ptr = memory_region_get_ram_ptr(&s->bar_regions[2]);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    timer_del(s->fw_cmd_timer);
    timer_free(s->fw_cmd_timer);
    s->ram_ptr = NULL;
}

static const VMStateDescription vmstate_pcibase = {
    .name = "wanXL_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32_ARRAY(plx_regs, PCIBaseState, 0x80 / 4),
        VMSTATE_UINT32(fw_cmd, PCIBaseState),
        VMSTATE_UINT64(card_status_addr, PCIBaseState),
        VMSTATE_UINT32(ramsize, PCIBaseState),
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
