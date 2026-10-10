/*
 * QEMU PCI device model for wanXL (behavioral implementation)
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
/* linux/pci_ids.h is not available in QEMU build environment; we provide
 * the specific IDs we need directly instead of including it. */

#define TYPE_PCIBASE_DEVICE "wanXL_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* PCI identifiers: use first entry of wanxl_pci_tbl */
/* Macros and register-related constants from wanxl.c */
#define PLX_CTL_RESET          0x40000000
#define MBX1_CMD_ABORTJ        0x85000000
#define MBX1_CMD_BSWAP         0x8C000001
#define MBX2_MEMSZ_MASK        0xFFFF0000

#define STATUS_CABLE_NONE      7
#define STATUS_CABLE_EIA530    5
#define STATUS_CABLE_DCE       0x8000
#define STATUS_CABLE_V35       2
#define STATUS_CABLE_X21       3
#define STATUS_CABLE_DCD       0x0008
#define STATUS_CABLE_DSR       0x0010
#define STATUS_CABLE_V24       4
#define STATUS_CABLE_PM_SHIFT  5

#define PACKET_FULL            0x10
#define PACKET_UNDERRUN        0x30
#define PACKET_EMPTY           0x00

#define TX_BUFFERS             10
#define RX_QUEUE_LENGTH        40

#define PACKET_PORT_MASK       0x03

#define DOORBELL_FROM_CARD_CABLE_0   5
#define DOORBELL_FROM_CARD_TX_0      0
#define DOORBELL_FROM_CARD_RX        4

#define DOORBELL_TO_CARD_TX_0        8
#define DOORBELL_TO_CARD_OPEN_0      0
#define DOORBELL_TO_CARD_CLOSE_0     4

#define ALIGN32(x) (((x) + 3) & 0xFFFFFFFC)

#define PLX_OFFSET             0
#define PLX_DOORBELL_FROM_CARD (PLX_OFFSET + 0x64)
#define PLX_DOORBELL_TO_CARD   (PLX_OFFSET + 0x60)
#define PLX_MAILBOX_0          (PLX_OFFSET + 0x40)
#define PLX_MAILBOX_1          (PLX_OFFSET + 0x44)
#define PLX_MAILBOX_2          (PLX_OFFSET + 0x48)
#define PLX_MAILBOX_5          (PLX_OFFSET + 0x54)
#define PLX_CONTROL            (PLX_OFFSET + 0x6C)

#define BUFFERS_ADDR           0x4000
#define PDM_OFFSET             0x1000

#define RX_BUFFERS             30

#define DETECT_RAM             0
#define RESET_WHILE_LOADING    0

/* BAR/PCI identifiers: vendor and device from first wanxl_pci_tbl entry */
/* The numeric values come from linux/pci_ids.h, but we define only the
 * specific IDs needed here to avoid including that header. */
#define WANXL_VENDOR_ID        0x1176
#define WANXL_DEVICE_ID        0x0301
#define WANXL_CLASS_ID         PCI_CLASS_NETWORK_OTHER


/* New structures from driver header for documentation; layout is driven by
 * the driver but we do not interpret individual fields in detail. */
#define HDLC_MAX_MRU 1600

typedef struct {
    volatile uint32_t stat;
    uint32_t address;   /* PCI address */
    volatile uint32_t length;
} desc_t;

typedef struct {
    /* Card to host */
    volatile uint32_t open;
    volatile uint32_t cable;
    volatile uint32_t rx_overruns;
    volatile uint32_t rx_frame_errors;

    /* Host to card */
    uint32_t parity;
    uint32_t encoding;
    uint32_t clocking;
    desc_t tx_descs[TX_BUFFERS];
} port_status_t;

typedef struct {
    desc_t rx_descs[RX_QUEUE_LENGTH];
    port_status_t port_status[4];
} card_status;

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

    /* Simple interrupt emulation state */
    uint32_t irq_status;
    uint32_t irq_mask;

    /* PLX register shadows */
    uint32_t plx_mailbox0;
    uint32_t plx_mailbox1;
    uint32_t plx_mailbox2;
    uint32_t plx_mailbox5;
    uint32_t plx_doorbell_from_card;
    uint32_t plx_doorbell_to_card;
    uint32_t plx_control;

    /* Basic card/firmware state approximated from driver expectations */
    bool firmware_running;
    uint32_t reported_ramsize;
    uint32_t status_dma_addr_low;
    uint32_t status_dma_addr_high;

    /* Card status DMA shadow: we keep one instance per function to
     * synthesize minimal behavior expected by the driver when it
     * inspects descriptor and port status fields. */
    card_status status_shadow;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Simple level-triggered interrupt: raise when any status bit set & unmasked */
    uint32_t pending = s->irq_status & s->irq_mask;
    if (pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* The real hardware uses PLX DMA; the driver never programs DMA engine
     * registers directly. It only passes host addresses to firmware via
     * on-board RAM and mailbox commands. That firmware behavior is not
     * modeled here, so this helper is unused. */
    (void)s;
    (void)is_write;
}

/* Helper to estimate and record the base address of the card_status
 * structure in guest memory. We only know the DMA address of the first
 * RX descriptor (rx_descs[0]) from driver programming. Given the
 * card_status layout from the driver, rx_descs is the first field, so
 * the base address of card_status equals the address of rx_descs[0]. */
static void pcibase_set_status_dma_addr(PCIBaseState *s, uint64_t dma_addr)
{
    s->status_dma_addr_low = (uint32_t)(dma_addr & 0xFFFFFFFFULL);
    s->status_dma_addr_high = (uint32_t)((dma_addr >> 32) & 0xFFFFFFFFULL);
}

/* Helper invoked when the driver programs RX descriptors in DMA memory.
 * We fetch one descriptor and cache its DMA address as the base of the
 * card_status structure; later we use the shadow only for reporting
 * open/cable state etc. */
G_GNUC_UNUSED static void pcibase_learn_status_location(PCIBaseState *s, uint64_t desc_dma_addr)
{
    card_status cs;
    uint64_t base_addr;

    /* desc_t is the first field of rx_descs[0], which is the first
     * field of card_status, so the beginning of card_status is at
     * desc_dma_addr - offsetof(card_status, rx_descs[0]). Given the
     * provided layout, offsetof(card_status, rx_descs) is 0. So the
     * base address equals desc_dma_addr. */
    base_addr = desc_dma_addr;

    /* Read the whole card_status structure from guest RAM once so that
     * our shadow matches what the driver has initialized. */
    pci_dma_read(PCI_DEVICE(s), base_addr, &cs, sizeof(cs));
    s->status_shadow = cs;
    pcibase_set_status_dma_addr(s, base_addr);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    /* All accesses in the driver use readl/writel (32-bit). */
    if (size != 4) {
        return 0xFFFFFFFFU;
    }

    switch (addr) {
    case PLX_MAILBOX_0:
        /*
         * Driver uses PLX_MAILBOX_0 in probe():
         *  - waits until it becomes 0 after firmware PUTS phase.
         *  - thus start as non-zero in reset, then 0 after firmware_running.
         */
        val = s->plx_mailbox0;
        break;
    case PLX_MAILBOX_1:
        /* Command mailbox used by wanxl_puts_command(). Driver writes cmd
         * and then polls until readback becomes 0. */
        val = s->plx_mailbox1;
        break;
    case PLX_MAILBOX_2:
        /* Contains memory size in upper bits masked by MBX2_MEMSZ_MASK. */
        val = s->plx_mailbox2;
        break;
    case PLX_MAILBOX_5:
        /* Used as status/nonzero value after MBX1_CMD_ABORTJ to indicate
         * firmware initialization success. Probe waits until nonzero. */
        val = s->plx_mailbox5;
        break;
    case PLX_DOORBELL_FROM_CARD:
        /* Status/interrupt doorbell from card. ISR loops while nonzero and
         * then writes back same value to clear. */
        val = s->plx_doorbell_from_card;
        break;
    case PLX_DOORBELL_TO_CARD:
        /* Host-to-card doorbell; driver never reads it, but return shadow. */
        val = s->plx_doorbell_to_card;
        break;
    case PLX_CONTROL:
        val = s->plx_control;
        break;
    default:
        /* Unknown/unused registers - return 0. */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case PLX_MAILBOX_0:
        /* Driver writes 0x80 before reset, then never again. Keep shadow. */
        s->plx_mailbox0 = (uint32_t)val;
        break;

    case PLX_MAILBOX_1:
        /* Commands issued via wanxl_puts_command(): MBX1_CMD_BSWAP,
         * MBX1_CMD_ABORTJ, etc.
         * After write, driver expects subsequent readl() to eventually
         * return 0 to indicate completion. We emulate immediate completion
         * and update related state for known commands. */
        s->plx_mailbox1 = (uint32_t)val;
        if (val == MBX1_CMD_BSWAP) {
            /* Byte-swap mode command: acknowledge immediately. */
            s->plx_mailbox1 = 0;
        } else if (val == MBX1_CMD_ABORTJ) {
            /* Abort and Jump: start firmware and set mailbox5 nonzero. */
            s->firmware_running = true;
            if (s->plx_mailbox5 == 0) {
                s->plx_mailbox5 = 1;
            }
            s->plx_mailbox1 = 0;
        } else {
            /* For unknown commands just complete immediately. */
            s->plx_mailbox1 = 0;
        }
        break;

    case PLX_MAILBOX_2:
        /* Driver only reads masked by MBX2_MEMSZ_MASK. Allow writes to
         * override reported RAM size for flexibility. */
        s->plx_mailbox2 = (uint32_t)val;
        s->reported_ramsize = s->plx_mailbox2 & MBX2_MEMSZ_MASK;
        break;

    case PLX_MAILBOX_5:
        /* Probe writes 0 then waits for nonzero after MBX1_CMD_ABORTJ. */
        s->plx_mailbox5 = (uint32_t)val;
        break;

    case PLX_DOORBELL_FROM_CARD:
        /* ISR: stat = readl(...FROM_CARD); writel(stat,...FROM_CARD);
         * That is write-1-to-clear behavior. Host only ever writes the
         * value it just read, so we clear corresponding bits. */
        s->plx_doorbell_from_card &= ~((uint32_t)val);
        /* Clear interrupt status bits modelled here. */
        s->irq_status &= ~((uint32_t)val);
        pcibase_update_irq(s);
        break;

    case PLX_DOORBELL_TO_CARD:
        /* Host-to-card doorbell. Several uses:
         *  - TX:  writel(1 << (DOORBELL_TO_CARD_TX_0 + port->node), ...)
         *  - OPEN: writel(1 << (DOORBELL_TO_CARD_OPEN_0 + port->node), ...)
         *  - CLOSE: writel(1 << (DOORBELL_TO_CARD_CLOSE_0 + port->node), ...)
         * We cannot model full firmware, but to let the driver progress we
         * synthesize card responses directly in host-visible status area:
         *  - OPEN: emulate immediate success by logically setting
         *    status_shadow.port_status[0].open.
         *  - TX: mark tx_descs[0] as complete and raise doorbells.
         */
        s->plx_doorbell_to_card = (uint32_t)val;

        if (val & (((uint32_t)1) << DOORBELL_TO_CARD_TX_0)) {
            /* TX request for port 0: emulate immediate TX complete
             * by marking the first TX descriptor as PACKET_EMPTY
             * (no error) and raising FROM_CARD TX and RX bits so
             * that the ISR runs. We only touch the shadow; the
             * driver owns real DMA memory. */
            s->status_shadow.port_status[0].tx_descs[0].stat = PACKET_EMPTY;

            s->plx_doorbell_from_card |= ((uint32_t)1 << DOORBELL_FROM_CARD_TX_0);
            s->plx_doorbell_from_card |= ((uint32_t)1 << DOORBELL_FROM_CARD_RX);
            s->irq_status |= ((uint32_t)1 << DOORBELL_FROM_CARD_TX_0);
            s->irq_status |= ((uint32_t)1 << DOORBELL_FROM_CARD_RX);
            pcibase_update_irq(s);
        }
        if (val & (((uint32_t)1) << DOORBELL_TO_CARD_OPEN_0)) {
            /* Port open request for port 0: mark immediately open in
             * our shadow so that when the driver examines card_status
             * it sees the port as open. */
            s->status_shadow.port_status[0].open = 1;
        }
        if (val & (((uint32_t)1) << DOORBELL_TO_CARD_CLOSE_0)) {
            /* Port close: clear open flag in shadow. */
            s->status_shadow.port_status[0].open = 0;
        }
        break;

    case PLX_CONTROL:
        /* wanxl_reset() sequences PLX_CTL_RESET bit, while preserving
         * other bits. We maintain shadow and emulate reset side effects
         * in a minimal way. */
        s->plx_control = (uint32_t)val;
        if (s->plx_control & PLX_CTL_RESET) {
            /* During reset assert we could clear firmware_running, etc. */
            s->firmware_running = false;
        } else {
            /* After deassert, hardware comes out of reset. We do not need
             * further changes for driver probe. */
        }
        break;

    default:
        /* Unknown/unused */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    /* Port I/O is not used by the provided driver; return all-ones to
     * make it obvious if something unexpected happens. */
    return 0xFFFFFFFFU;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
    /* Port I/O write behavior not used in provided snippet. */
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

    /* Initialize PLX registers to values consistent with expectations
     * in wanxl_pci_init_one() and wanxl_reset(). */
    s->irq_status = 0;
    s->irq_mask = 0xFFFFFFFFU; /* All doorbell bits unmasked. */

    /* Mailbox 0: nonzero at power-on to make probe wait until firmware
     * completes initial PUTS, then becomes 0. We simply start at 0 to
     * avoid long delays. */
    s->plx_mailbox0 = 0;

    /* Mailbox 1: command mailbox, idle at 0. */
    s->plx_mailbox1 = 0;

    /* Mailbox 2: reported RAM size. The driver masks with MBX2_MEMSZ_MASK
     * and expects at least BUFFERS_ADDR + (TX_BUFFERS + RX_BUFFERS)
     * * BUFFER_LENGTH * ports. BUFFER_LENGTH is not available here; use
     * a generous default (e.g. 4 MB) and rely on the mask. */
    s->reported_ramsize = 4 * 1024 * 1024;
    s->plx_mailbox2 = s->reported_ramsize & MBX2_MEMSZ_MASK;

    /* Mailbox 5: firmware status. 0 until MBX1_CMD_ABORTJ processed. */
    s->plx_mailbox5 = 0;

    s->plx_doorbell_from_card = 0;
    s->plx_doorbell_to_card = 0;

    /* Control register: clear reset, otherwise 0. */
    s->plx_control = 0;

    s->firmware_running = false;
    s->status_dma_addr_low = 0;
    s->status_dma_addr_high = 0;

    /* Clear shadowed card status. */
    memset(&s->status_shadow, 0, sizeof(s->status_shadow));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  WANXL_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  WANXL_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, WANXL_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    /* The driver maps BAR0 as PLX (size 0x70) and BAR2 as on-board RAM.
     * Only PLX BAR0 is relevant for probe and basic operation here. */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x1000; /* covers PLX registers (>= 0x6C) */
    s->bar_info[0].name  = "wanxl-bar0";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X are not referenced in the provided driver snippet. */
    s->has_msi = false;
    s->has_msix = false;

    /* Initialize device-specific state as in reset. */
    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* No dynamically allocated resources to free in this model. */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "wanXL_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(irq_status, PCIBaseState),
        VMSTATE_UINT32(irq_mask, PCIBaseState),
        VMSTATE_UINT32(plx_mailbox0, PCIBaseState),
        VMSTATE_UINT32(plx_mailbox1, PCIBaseState),
        VMSTATE_UINT32(plx_mailbox2, PCIBaseState),
        VMSTATE_UINT32(plx_mailbox5, PCIBaseState),
        VMSTATE_UINT32(plx_doorbell_from_card, PCIBaseState),
        VMSTATE_UINT32(plx_doorbell_to_card, PCIBaseState),
        VMSTATE_UINT32(plx_control, PCIBaseState),
        VMSTATE_BOOL(firmware_running, PCIBaseState),
        VMSTATE_UINT32(reported_ramsize, PCIBaseState),
        VMSTATE_UINT32(status_dma_addr_low, PCIBaseState),
        VMSTATE_UINT32(status_dma_addr_high, PCIBaseState),
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
