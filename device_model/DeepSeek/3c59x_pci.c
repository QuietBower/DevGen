/*
 * 3c59x QEMU device model for 3Com 3c590 Ethernet adapter
 * QEMU 8.2.10 compatible
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
/* No additional includes needed */

#define TYPE_PCIBASE_DEVICE "3c59x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x10B7
#define DEVICE_ID 0x5900
#define CLASS_ID 0x0200

#define EL3_CMD 0x0E
#define EL3_STATUS 0x0E
#define Wn0EepromCmd 10
#define Wn0EepromData 12
#define Wn2_ResetOptions 12
#define Wn3_Config 0
#define Wn3_MaxPktSize 4
#define Wn3_MAC_Ctrl 6
#define Wn3_Options 8
#define Wn4_FIFODiag 4
#define Wn4_NetDiag 6
#define Wn4_PhysicalMgmt 8
#define Wn4_Media 10
#define Wn7_MasterAddr 0
#define Wn7_VlanEtherType 4
#define Wn7_MasterLen 6
#define Wn7_MasterStatus 12
#define TxFree 0x1C
#define TxStatus 0x1B
#define Timer 0x1A
#define RxStatus 0x18
#define RxErrors 0x14
#define TX_FIFO 0x10
#define RX_FIFO 0x10
#define IntrStatus 0x0E

/* Command values */
#define TotalReset (0<<11)
#define SelectWindow (1<<11)
#define StartCoax (2<<11)
#define RxDisable (3<<11)
#define RxEnable (4<<11)
#define RxReset (5<<11)
#define UpStall (6<<11)
#define UpUnstall ((6<<11)+1)
#define DownStall ((6<<11)+2)
#define DownUnstall ((6<<11)+3)
#define RxDiscard (8<<11)
#define TxEnable (9<<11)
#define TxDisable (10<<11)
#define TxReset (11<<11)
#define FakeIntr (12<<11)
#define AckIntr (13<<11)
#define SetIntrEnb (14<<11)
#define SetStatusEnb (15<<11)
#define SetRxFilter (16<<11)
#define SetRxThreshold (17<<11)
#define SetTxThreshold (18<<11)
#define SetTxStart (19<<11)
#define StartDMAUp (20<<11)
#define StartDMADown ((20<<11)+1)
#define StatsEnable (21<<11)
#define StatsDisable (22<<11)
#define StopCoax (23<<11)
#define SetFilterBit (25<<11)

/* Status bits */
#define IntLatch 0x0001
#define HostError 0x0002
#define TxComplete 0x0004
#define TxAvailable 0x0008
#define RxComplete 0x0010
#define RxEarly 0x0020
#define IntReq 0x0040
#define StatsFull 0x0080
#define DMADone (1<<8)
#define DownComplete (1<<9)
#define UpComplete (1<<10)
#define DMAInProgress (1<<11)
#define CmdInProgress (1<<12)

/* Descriptor sizes */
#define TX_RING_SIZE 16
#define RX_RING_SIZE 32

/* EEPROM offsets */
#define PhysAddr01 0
#define PhysAddr23 1
#define PhysAddr45 2
#define ModelID 3
#define EtherLink3ID 7
#define IFXcvrIO 8
#define IRQLine 9
#define NodeAddr01 10
#define NodeAddr23 11
#define NodeAddr45 12
#define DriverTune 13
#define Checksum 15

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

/* DMA descriptor structures from driver */
struct boom_rx_desc {
    uint32_t next;
    uint32_t status;
    uint32_t addr;
    uint32_t length;
};

struct boom_tx_desc {
    uint32_t next;
    uint32_t status;
    uint32_t addr;
    uint32_t length;
};

#define MAX_WINDOWS 8
#define MAX_WINDOW_REGS 16

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint16_t intr_status;
    uint16_t intr_enable;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint16_t window_regs[MAX_WINDOWS][MAX_WINDOW_REGS]; /* 8 windows, each with up to 16 16-bit registers */
    uint16_t current_window;
    uint16_t window_select;      /* index of selected register within window */
    uint16_t command;
    uint16_t status;

    /* DMA Context */
    dma_addr_t tx_ring_base;
    dma_addr_t rx_ring_base;
    uint16_t tx_ring_size;
    uint16_t rx_ring_size;
    uint32_t cur_tx;
    uint32_t cur_rx;
    uint32_t dirty_tx;
    bool dma_running;

    /* Operational status flags */
    bool up;
    bool full_duplex;
    uint8_t media_state;

    /* State used to handle reset sequences */
    bool reset_done;

    /* Power management state (D0-D3) */
    uint8_t pm_state;

    /* EEPROM simulation */
    uint16_t eeprom_contents[0x40];
    uint16_t eeprom_addr;
};

/* Forward declaration for IRQ update */
static void pcibase_update_irq(PCIBaseState *s);

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* IRQ logic: raise INTx# if any enabled interrupt source is active */
    if (s->intr_status & s->intr_enable) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Not used by this device (PIO-only) */
    return ~0ULL;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Not used by this device (PIO-only) */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case EL3_STATUS:  /* 0x0E - Interrupt/Status register */
        val = s->intr_status;
        /* CmdInProgress should be low for driver to proceed. We keep it 0. */
        break;
    case 0x0C:       /* Window data register */
        {
            uint16_t window = s->current_window & 0x07;
            uint16_t reg = s->window_select;
            if (window == 0 && reg == Wn0EepromCmd) {
                /* Return EEPROM command status (always 0 = ready) */
                val = 0;
            } else if (window == 0 && reg == Wn0EepromData) {
                val = s->eeprom_contents[s->eeprom_addr & 0x3F];
            } else {
                if (window < MAX_WINDOWS && reg < MAX_WINDOW_REGS) {
                    val = s->window_regs[window][reg];
                }
            }
        }
        break;
    case RxStatus:    /* 0x18 */
        val = 0;  /* No received packets */
        break;
    case RxErrors:    /* 0x14 */
        val = 0;
        break;
    case Timer:       /* 0x1A */
        val = 0;
        break;
    case TxStatus:    /* 0x1B */
        val = 0;
        break;
    case TxFree:      /* 0x1C */
        val = 0xFFFF; /* Indicate large amount of free space */
        break;
    case TX_FIFO:     /* 0x10: read returns RX FIFO */
        val = 0;
        break;
    default:
        /* Other addresses: unknown, return 0 */
        break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case EL3_CMD:  /* 0x0E - Command register */
    {
        uint16_t cmd = (uint16_t)val;
        uint16_t arg = cmd & 0x07FF;   /* low 11 bits */
        uint16_t op = cmd & 0x7800;    /* bits 11-14 */

        if (op == SelectWindow) {
            s->current_window = arg & 0x07;
        } else if (op == AckIntr) {
            s->intr_status &= ~arg;
            pcibase_update_irq(s);
        } else if (op == SetIntrEnb) {
            s->intr_enable = arg;
            pcibase_update_irq(s);
        } else if (op == SetStatusEnb) {
            /* Status enable not emulated separately */
        } else if (op == TotalReset) {
            s->intr_status = 0;
            s->intr_enable = 0;
            s->current_window = 0;
            pcibase_update_irq(s);
        }
        /* Other commands handled here or ignored as needed */
        break;
    }
    case 0x0A:  /* Window register select */
        s->window_select = (uint16_t)val;
        break;
    case 0x0C:  /* Window register data */
        {
            uint16_t window = s->current_window & 0x07;
            uint16_t reg = s->window_select;
            if (window == 0 && reg == Wn0EepromCmd) {
                s->eeprom_addr = (uint16_t)val;
            } else if (window < MAX_WINDOWS && reg < MAX_WINDOW_REGS) {
                s->window_regs[window][reg] = (uint16_t)val;
            }
        }
        break;
    case TxStatus:    /* 0x1B: clear on write */
        /* Write to clear, can be ignored */
        break;
    case TxFree:      /* 0x1C: not writable */
        break;
    default:
        break;
    }
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
    /* Reset all state to power-on defaults */
    memset(s->window_regs, 0, sizeof(s->window_regs));
    s->current_window = 0;
    s->window_select = 0;
    s->intr_status = 0;
    s->intr_enable = 0;
    s->eeprom_addr = 0;
    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x40;   /* 64 bytes, sufficient for all registers */
    s->bar_info[0].name = "3c590-io";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize EEPROM contents */
    memset(s->eeprom_contents, 0, sizeof(s->eeprom_contents));
    /* Set MAC address: 02:00:00:00:00:01. EEPROM words are little-endian (low byte first). */
    s->eeprom_contents[PhysAddr01] = 0x0002;
    s->eeprom_contents[PhysAddr23] = 0x0000;
    s->eeprom_contents[PhysAddr45] = 0x0100;
    s->eeprom_contents[NodeAddr01] = 0x0002;
    s->eeprom_contents[NodeAddr23] = 0x0000;
    s->eeprom_contents[NodeAddr45] = 0x0100;
    /* Set EtherLink3ID signature to 0x6D50 */
    s->eeprom_contents[EtherLink3ID] = 0x6D50;
    /* Compute checksum: XOR of words 0-14 is 0x6D50, so set word 15 to 0x6D50 for zero XOR */
    s->eeprom_contents[Checksum] = 0x6D50;

    /* Set window 3 register 8 (Wn3_Options) to 0x40 so available_media is not zero */
    s->window_regs[3][8] = 0x40;

    /* No DMA, timers, or other init needed */
    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* No additional cleanup needed */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "3c59x_pci",
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