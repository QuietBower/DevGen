/*
 * QEMU PCI device model for FarSync T2P card (farsync.c)
 * Phase 2: Functional implementation.
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

#define TYPE_PCIBASE_DEVICE "fst_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* ============================================================
 * Register Layout and Hardware Identifiers extracted from driver
 * ============================================================ */

/* PCI IDs from driver headers */
#define PCI_VENDOR_ID_FARSITE           0x1619
#define PCI_DEVICE_ID_FARSITE_T2P       0x0400
#define PCI_CLASS_ID                    0x020000  /* Network controller */

/* Memory layout */
#define FST_MEMSIZE 0x100000
#define SMC_BASE    0x2000
#define BFM_BASE    0x10000

/* Driver constants */
#define FST_MAX_PORTS           4
#define NUM_TX_BUFFER           2
#define NUM_RX_BUFFER           8
#define LEN_TX_BUFFER           8192
#define LEN_RX_BUFFER           8192
#define LEN_SMALL_TX_BUFFER     256
#define LEN_SMALL_RX_BUFFER     256
#define MAX_CIRBUFF             32
#define SMC_VERSION             24
#define END_SIG                 0x12345678

/* PLX 9052 register offsets (relative to PLX local config space) */
#define CNTRL_9052     0x50
#define INTCSR_9052    0x4c
#define DMAMODE0       0x80
#define DMAPADR0       0x84
#define DMALADR0       0x88
#define DMASIZ0        0x8c
#define DMADPR0        0x90
#define DMAMODE1       0x94
#define DMAPADR1       0x98
#define DMALADR1       0x9c
#define DMASIZ1        0xa0
#define DMADPR1        0xa4
#define DMACSR0        0xa8
#define DMACSR1        0xa9
#define DMAARB         0xac
#define DMATHR         0xb0
#define DMADAC0        0xb4
#define DMADAC1        0xb8

/* Hardware descriptor structures (from driver) */
struct txdesc {
    volatile uint16_t ladr;
    volatile uint8_t  hadr;
    volatile uint8_t  bits;
    volatile uint16_t bcnt;
    uint16_t unused;
};

struct rxdesc {
    volatile uint16_t ladr;
    volatile uint8_t  hadr;
    volatile uint8_t  bits;
    volatile uint16_t bcnt;
    volatile uint16_t mcnt;
};

struct cirbuff {
    uint8_t rdindex;
    uint8_t wrindex;
    uint8_t evntbuff[MAX_CIRBUFF];
};

struct port_cfg {
    uint16_t lineInterface;
    uint8_t  x25op;
    uint8_t  internalClock;
    uint8_t  transparentMode;
    uint8_t  invertClock;
    uint8_t  padBytes[6];
    uint32_t lineSpeed;
};

struct su_config {
    uint32_t dataRate;
    uint8_t  clocking;
    uint8_t  framing;
    uint8_t  structure;
    uint8_t  interface;
    uint8_t  coding;
    uint8_t  lineBuildOut;
    uint8_t  equalizer;
    uint8_t  transparentMode;
    uint8_t  loopMode;
    uint8_t  range;
    uint8_t  txBufferMode;
    uint8_t  rxBufferMode;
    uint8_t  startingSlot;
    uint8_t  losThreshold;
    uint8_t  enableIdleCode;
    uint8_t  idleCode;
    uint8_t  spare[44];
};

struct su_status {
    uint32_t receiveBufferDelay;
    uint32_t framingErrorCount;
    uint32_t codeViolationCount;
    uint32_t crcErrorCount;
    uint32_t lineAttenuation;
    uint8_t  portStarted;
    uint8_t  lossOfSignal;
    uint8_t  receiveRemoteAlarm;
    uint8_t  alarmIndicationSignal;
    uint8_t  spare[40];
};

struct fst_shared {
    struct rxdesc rxDescrRing[FST_MAX_PORTS][NUM_RX_BUFFER];
    struct txdesc txDescrRing[FST_MAX_PORTS][NUM_TX_BUFFER];

    uint8_t smallRxBuffer[FST_MAX_PORTS][NUM_RX_BUFFER][LEN_SMALL_RX_BUFFER];
    uint8_t smallTxBuffer[FST_MAX_PORTS][NUM_TX_BUFFER][LEN_SMALL_TX_BUFFER];

    uint8_t  taskStatus;
    uint8_t  interruptHandshake;
    uint16_t smcVersion;
    uint32_t smcFirmwareVersion;

    uint16_t txa_done;
    uint16_t rxa_done;
    uint16_t txb_done;
    uint16_t rxb_done;
    uint16_t txc_done;
    uint16_t rxc_done;
    uint16_t txd_done;
    uint16_t rxd_done;

    uint16_t mailbox[4];
    struct cirbuff interruptEvent;

    uint32_t v24IpSts[FST_MAX_PORTS];
    uint32_t v24OpSts[FST_MAX_PORTS];

    struct port_cfg portConfig[FST_MAX_PORTS];

    uint16_t clockStatus[FST_MAX_PORTS];
    uint16_t cableStatus;

    uint16_t txDescrIndex[FST_MAX_PORTS];
    uint16_t rxDescrIndex[FST_MAX_PORTS];

    uint16_t portMailbox[FST_MAX_PORTS][2];
    uint16_t cardMailbox[4];

    uint32_t interruptRetryCount;
    uint32_t portHandle[FST_MAX_PORTS];
    uint32_t transmitBufferUnderflow[FST_MAX_PORTS];

    uint32_t v24DebouncedSts[FST_MAX_PORTS];
    uint32_t ctsTimer[FST_MAX_PORTS];
    uint32_t ctsTimerRun[FST_MAX_PORTS];
    uint32_t dcdTimer[FST_MAX_PORTS];
    uint32_t dcdTimerRun[FST_MAX_PORTS];

    uint32_t numberOfPorts;

    uint16_t _reserved[64];
    uint16_t cardMode;
    uint16_t portScheduleOffset;

    struct su_config suConfig;
    struct su_status suStatus;

    uint32_t endOfSmcSignature;
};

struct buf_window {
    uint8_t txBuffer[FST_MAX_PORTS][NUM_TX_BUFFER][LEN_TX_BUFFER];
    uint8_t rxBuffer[FST_MAX_PORTS][NUM_RX_BUFFER][LEN_RX_BUFFER];
};

/* =============================
 * End of driver definitions
 * ============================= */

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

    MemoryRegion bar1_mr;
    MemoryRegion bar2_mr;
    MemoryRegion bar3_mr;
    BARInfo bar_info[3];
    int num_bars;

    /* PLX 9052 local configuration registers */
    uint16_t intcsr_9052;

    /* Hardware Register Shadows */
    struct fst_shared fst_shared_mem;
    struct buf_window buf_window_mem;
};

/* Internal helpers */
static void pcibase_bar2_auto_ack(PCIBaseState *s, hwaddr addr, unsigned size)
{
    /* If a command is written to a port mailbox, ack it by clearing */
    if (size == 2 && addr >= SMC_BASE && addr < SMC_BASE + sizeof(struct fst_shared)) {
        hwaddr offset = addr - SMC_BASE;
        for (int port = 0; port < FST_MAX_PORTS; port++) {
            hwaddr mailbox_offset = offsetof(struct fst_shared, portMailbox[port][0]);
            if (offset == mailbox_offset) {
                s->fst_shared_mem.portMailbox[port][0] = 0;
                return;
            }
        }
    }
}

/* MMIO handlers for BAR1 (PLX configuration space) */
static uint64_t bar1_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case INTCSR_9052:
        if (size == 2) {
            val = s->intcsr_9052;
        }
        break;
    default:
        break;
    }
    return val;
}

static void bar1_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case INTCSR_9052:
        if (size == 2) {
            s->intcsr_9052 = (uint16_t)val;
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps bar1_ops = {
    .read = bar1_mmio_read,
    .write = bar1_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* MMIO handlers for BAR2 (shared memory and buffer windows) */
static uint64_t bar2_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    void *ptr;
    uint32_t offset;

    if (addr >= SMC_BASE && addr < SMC_BASE + sizeof(struct fst_shared)) {
        ptr = &s->fst_shared_mem;
        offset = addr - SMC_BASE;
    } else if (addr >= BFM_BASE && addr < BFM_BASE + sizeof(struct buf_window)) {
        ptr = &s->buf_window_mem;
        offset = addr - BFM_BASE;
    } else {
        return 0;
    }

    switch (size) {
    case 1:
        val = *(uint8_t *)(ptr + offset);
        break;
    case 2:
        val = *(uint16_t *)(ptr + offset);
        break;
    case 4:
        val = *(uint32_t *)(ptr + offset);
        break;
    default:
        break;
    }
    return val;
}

static void bar2_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    void *ptr;
    uint32_t offset;

    if (addr >= SMC_BASE && addr < SMC_BASE + sizeof(struct fst_shared)) {
        ptr = &s->fst_shared_mem;
        offset = addr - SMC_BASE;
    } else if (addr >= BFM_BASE && addr < BFM_BASE + sizeof(struct buf_window)) {
        ptr = &s->buf_window_mem;
        offset = addr - BFM_BASE;
    } else {
        return;
    }

    switch (size) {
    case 1:
        *(uint8_t *)(ptr + offset) = (uint8_t)val;
        break;
    case 2:
        *(uint16_t *)(ptr + offset) = (uint16_t)val;
        break;
    case 4:
        *(uint32_t *)(ptr + offset) = (uint32_t)val;
        break;
    default:
        break;
    }

    /* Auto-ack command mailboxes */
    pcibase_bar2_auto_ack(s, addr, size);
}

static const MemoryRegionOps bar2_ops = {
    .read = bar2_mmio_read,
    .write = bar2_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* MMIO handlers for BAR3 (control memory region) */
static uint64_t bar3_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Driver uses readb to force posted writes; nothing to do */
    return 0;
}

static void bar3_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Ignored */
}

static const MemoryRegionOps bar3_ops = {
    .read = bar3_mmio_read,
    .write = bar3_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Initialize shared memory to indicate firmware is already running */
    memset(&s->fst_shared_mem, 0, sizeof(s->fst_shared_mem));
    s->fst_shared_mem.taskStatus = 0x01;          /* Running */
    s->fst_shared_mem.smcVersion = SMC_VERSION;
    s->fst_shared_mem.endOfSmcSignature = END_SIG;
    s->fst_shared_mem.numberOfPorts = 2;          /* T2P has 2 ports */
    /* PLX registers */
    s->intcsr_9052 = 0x0543;                      /* Interrupt enabled, cleared */
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);

    switch (bi->index) {
    case 1:
        memory_region_init_io(&s->bar1_mr, OBJECT(s), &bar1_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar1_mr);
        break;
    case 2:
        memory_region_init_io(&s->bar2_mr, OBJECT(s), &bar2_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar2_mr);
        break;
    case 3:
        memory_region_init_io(&s->bar3_mr, OBJECT(s), &bar3_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar3_mr);
        break;
    default:
        break;
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_FARSITE);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_FARSITE_T2P);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Regular PCI device, not PCIe */
    pdev->cap_present &= ~QEMU_PCI_CAP_EXPRESS;

    /* BAR Initialization: three BARs used by driver */
    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){
        .index = 1,
        .type = BAR_TYPE_MMIO,
        .size = 0x100,
        .name = "fst-pci-conf"
    };
    s->bar_info[1] = (BARInfo){
        .index = 2,
        .type = BAR_TYPE_MMIO,
        .size = FST_MEMSIZE,
        .name = "fst-mem"
    };
    s->bar_info[2] = (BARInfo){
        .index = 3,
        .type = BAR_TYPE_MMIO,
        .size = 0x10,
        .name = "fst-ctl"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X */

    /* Initialize device state */
    pcibase_reset(DEVICE(pdev));
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "fst_pci",
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
