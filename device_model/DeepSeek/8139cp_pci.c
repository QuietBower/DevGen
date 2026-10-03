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

#define TYPE_PCIBASE_DEVICE "8139cp_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identification (first entry in pci_device_id) */
#define PCI_DEVICE_ID_REALTEK_8139   0x8139

/* Driver defined constants (from cp_private/config) */
#define CP_RX_RING_SIZE     64
#define CP_TX_RING_SIZE     64
#define CP_REGS_SIZE        (0xff + 1)   /* 256 bytes MMIO region */
#define CP_STATS_SIZE       64

/* Supplementary constants */
#define MAC0                0
#define CP_INTERNAL_PHY     32
#define TX_DMA_BURST        6

/* Interrupt status bits (from cp_private/8139cp.h) */
enum {
    RxDone    = 0x0001,
    RxHeader  = 0x0002,
    RxFull    = 0x0004,
    RxOverflow = 0x0008,
    RxError   = 0x0010,
    TxDone    = 0x0020,
    TxEmpty   = 0x0080,
    TxUnderrun = 0x0100,
    CntFull   = 0x0200,
    RxEarlyWarn = 0x0400,
    RxStarted = 0x0800,
    PhyEvent175 = 0x8000,
    PCIBusErr175 = 0x1000,
    PCIBusErr170 = 0x7000,
    IntrSummary = 0x010000,
    RxIdle    = 0x20000,
    TxIdle    = 0x40000,
};

/* Register offsets - obtained from driver header */
#define REG_COMMAND     0x2c
#define REG_CMD         0x0032
#define REG_IMR         0x1604
#define REG_ISR         0xa4
#define REG_TCR         0x0604
#define REG_RCR         0x0608
#define REG_9346CR      0x000A
#define REG_CONFIG1     0x8
#define REG_CONFIG2     0xc
#define REG_CONFIG3     0x78
#define REG_CONFIG4     0x7D
#define REG_CONFIG5     0x7C
#define REG_TEST        0x14
#define REG_INTR_STATUS 0x000C
/* REG_INTR_MASK depends on missing REG_INTR_PRIORITY and SP_INTC_REG_SIZE */
// #define REG_INTR_MASK (REG_INTR_PRIORITY + SP_INTC_REG_SIZE)

/* Descriptor structures used by the hardware/DMA (from driver) */
struct cp_desc {
    __le32 opts1;
    __le32 opts2;
    __le64 addr;
};

struct cp_dma_stats {
    __le64 tx_ok;
    __le64 rx_ok;
    __le64 tx_err;
    __le32 rx_err;
    __le16 rx_fifo;
    __le16 frame_align;
    __le32 tx_ok_1col;
    __le32 tx_ok_mcol;
    __le64 rx_ok_phys;
    __le64 rx_ok_bcast;
    __le32 rx_ok_mcast;
    __le16 tx_abort;
    __le16 tx_underrun;
};

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t regs[CP_REGS_SIZE];

    /* DMA Context (driver uses 32/64-bit DMA) */
    dma_addr_t rx_ring_dma;
    dma_addr_t tx_ring_dma;
    uint32_t cmd;

    /* EEPROM bit-bang state */
    uint8_t eeprom_state;
    uint8_t eeprom_bit_i;
    uint16_t eeprom_cmd_addr;
    uint16_t eeprom_data;
    int8_t eeprom_out_bit_i;
    uint8_t eeprom_data_out;

    /* Power management state (D0-D3) */
    bool d3_state;

    /* EEPROM and MAC storage */
    uint16_t eeprom[256];
    uint8_t mac_addr[6];
};

/* Forward declaration for reset function used in mmio write */
static void pcibase_reset(DeviceState *dev);

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint16_t mask = s->intr_mask;
    uint16_t status = s->intr_status & mask;
    if (status) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= CP_REGS_SIZE) {
        return val;
    }

    switch (addr) {
    /* MAC address (MAC0) 0x00-0x05 */
    case 0x00 ... 0x05:
        if (size == 1 || size == 2 || size == 4) {
            val = ldn_le_p(s->regs + addr, size);
        }
        break;
    /* RxConfig 0x44 (32-bit) */
    case 0x44 ... 0x47:
        val = ldn_le_p(s->regs + addr, size);
        break;
    /* TxConfig 0x40 (32-bit) */
    case 0x40 ... 0x43:
        val = ldn_le_p(s->regs + addr, size);
        break;
    /* Cmd 0x37 (8-bit) */
    case 0x37:
        val = s->regs[addr];
        break;
    /* IntrStatus 0x3E-0x3F (16-bit) */
    case 0x3E ... 0x3F:
        if (addr == 0x3E && size >= 2) {
            val = (s->intr_status & 0xffff);
        } else if (addr == 0x3F && size == 1) {
            val = (s->intr_status >> 8) & 0xff;
        }
        break;
    /* IntrMask 0x3C-0x3D (16-bit) */
    case 0x3C ... 0x3D:
        if (addr == 0x3C && size >= 2) {
            val = s->intr_mask;
        } else if (addr == 0x3D && size == 1) {
            val = (s->intr_mask >> 8) & 0xff;
        }
        break;
    /* CpCmd 0xE0-0xE1 (16-bit) */
    case 0xE0 ... 0xE1:
        val = ldn_le_p(s->regs + addr, size);
        break;
    /* TxPoll 0x38 (8-bit) */
    case 0x38:
        val = s->regs[addr];
        break;
    /* Cfg9346 0x50 (8-bit) with EEPROM data bit */
    case 0x50:
        val = s->regs[addr];
        if (s->eeprom_state == 2) {  /* Sending data, override EE_DATA_READ */
            val = (val & ~0x01) | (s->eeprom_data_out ? 0x01 : 0x00);
        }
        break;
    /* Config1 0x52 (8-bit) */
    case 0x52:
        val = s->regs[addr];
        break;
    /* Config3 0x59 (8-bit) */
    case 0x59:
        val = s->regs[addr];
        break;
    /* Config5 0x5C (8-bit) */
    case 0x5C:
        val = s->regs[addr];
        break;
    /* RxRingAddr 0xE4-0xE7 (low), 0xE8-0xEB (high) */
    case 0xE4 ... 0xEB:
        val = ldn_le_p(s->regs + addr, size);
        break;
    /* TxRingAddr 0xEC-0xEF (low), 0xF0-0xF3 (high) */
    case 0xEC ... 0xF3:
        val = ldn_le_p(s->regs + addr, size);
        break;
    /* HiTxRingAddr 0xF4-0xF7 */
    case 0xF4 ... 0xF7:
        val = ldn_le_p(s->regs + addr, size);
        break;
    /* StatsAddr 0xF8-0xFB (32-bit) */
    case 0xF8 ... 0xFB:
        val = ldn_le_p(s->regs + addr, size);
        break;
    /* TxThresh 0x36 */
    case 0x36:
        val = s->regs[addr];
        break;
    /* MultiIntr 0x5A-0x5B (16-bit) */
    case 0x5A ... 0x5B:
        val = ldn_le_p(s->regs + addr, size);
        break;
    default:
        /* Return current shadow value for any unhandled offset */
        if (addr < CP_REGS_SIZE) {
            val = ldn_le_p(s->regs + addr, size);
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= CP_REGS_SIZE) {
        return;
    }

    switch (addr) {
    /* MAC address writes (0x00-0x05) */
    case 0x00 ... 0x05:
        stn_le_p(s->regs + addr, size, val);
        break;
    /* RxConfig 0x44 (32-bit) */
    case 0x44 ... 0x47:
        stn_le_p(s->regs + addr, size, val);
        break;
    /* TxConfig 0x40 (32-bit) */
    case 0x40 ... 0x43:
        stn_le_p(s->regs + addr, size, val);
        break;
    /* Cmd 0x37 (8-bit) */
    case 0x37:
        if (val & 0x10) { /* CmdReset = 0x10 */
            /* Perform reset */
            pcibase_reset(DEVICE(s));
            /* The reset bit is cleared immediately */
            s->regs[addr] = val & ~0x10;
        } else {
            s->regs[addr] = val;
        }
        break;
    /* IntrStatus 0x3E-0x3F (16-bit) - Write-1-to-clear */
    case 0x3E ... 0x3F:
        if (addr == 0x3E && size >= 2) {
            /* Clear bits that are written as 1 */
            uint16_t wval = val & 0xffff;
            s->intr_status &= ~wval;
            pcibase_update_irq(s);
        } else if (addr == 0x3E && size == 1) {
            uint16_t wval = (val & 0xff) | ((s->intr_status & 0xff00) & ~(val & 0xff));
            s->intr_status &= ~wval;
            pcibase_update_irq(s);
        } else if (addr == 0x3F && size == 1) {
            uint16_t wval = ((val & 0xff) << 8);
            s->intr_status &= ~wval;
            pcibase_update_irq(s);
        }
        break;
    /* IntrMask 0x3C-0x3D (16-bit) */
    case 0x3C ... 0x3D:
        if (addr == 0x3C && size >= 2) {
            s->intr_mask = val & 0xffff;
        } else if (addr == 0x3C && size == 1) {
            s->intr_mask = (s->intr_mask & 0xff00) | (val & 0xff);
        } else if (addr == 0x3D && size == 1) {
            s->intr_mask = (s->intr_mask & 0x00ff) | ((val & 0xff) << 8);
        }
        pcibase_update_irq(s);
        break;
    /* CpCmd 0xE0-0xE1 (16-bit) */
    case 0xE0 ... 0xE1:
        stn_le_p(s->regs + addr, size, val);
        break;
    /* TxPoll 0x38 (8-bit) */
    case 0x38:
        s->regs[addr] = val;
        break;
    /* Cfg9346 0x50 (8-bit) EEPROM bit-bang */
    case 0x50:
        {
            uint8_t prev = s->regs[0x50];
            s->regs[addr] = val;
            /* Detect CS rising edge with EE_ENB active */
            if ((prev & 0x08) == 0 && (val & 0x08) && (val & 0x80)) {
                s->eeprom_state = 1; /* Receiving command/address */
                s->eeprom_bit_i = 0;
                s->eeprom_cmd_addr = 0;
                s->eeprom_data_out = 0;
            }
            /* Detect clock rising edge */
            if ((prev & 0x04) == 0 && (val & 0x04) && (val & 0x08) && s->eeprom_state != 0) {
                if (s->eeprom_state == 1) { /* Receiving bits */
                    uint8_t bit = (val & 0x02) ? 1 : 0;
                    s->eeprom_cmd_addr = (s->eeprom_cmd_addr << 1) | bit;
                    s->eeprom_bit_i++;
                    if (s->eeprom_bit_i == 9) {
                        uint8_t opcode = (s->eeprom_cmd_addr >> 6) & 0x3;
                        uint8_t eeprom_addr = s->eeprom_cmd_addr & 0x3F;
                        if (opcode == 2) { /* READ command */
                            s->eeprom_state = 2; /* Sending data */
                            s->eeprom_data = s->eeprom[eeprom_addr];
                            s->eeprom_out_bit_i = 15;
                            s->eeprom_data_out = (s->eeprom_data >> 15) & 1;
                        } else {
                            s->eeprom_state = 0; /* Unsupported, abort */
                        }
                    }
                } else if (s->eeprom_state == 2) { /* Sending data */
                    s->eeprom_out_bit_i--;
                    if (s->eeprom_out_bit_i >= 0) {
                        s->eeprom_data_out = (s->eeprom_data >> s->eeprom_out_bit_i) & 1;
                    } else {
                        s->eeprom_state = 0;
                    }
                }
            }
            /* CS falling edge resets transaction */
            if ((prev & 0x08) && (val & 0x08) == 0) {
                s->eeprom_state = 0;
            }
        }
        break;
    /* Config1 0x52 (8-bit) */
    case 0x52:
        s->regs[addr] = val;
        break;
    /* Config3 0x59 (8-bit) */
    case 0x59:
        s->regs[addr] = val;
        break;
    /* Config5 0x5C (8-bit) */
    case 0x5C:
        s->regs[addr] = val;
        break;
    /* RxRingAddr 0xE4-0xEB */
    case 0xE4 ... 0xEB:
        stn_le_p(s->regs + addr, size, val);
        break;
    /* TxRingAddr 0xEC-0xF3 */
    case 0xEC ... 0xF3:
        stn_le_p(s->regs + addr, size, val);
        break;
    /* HiTxRingAddr 0xF4-0xF7 */
    case 0xF4 ... 0xF7:
        stn_le_p(s->regs + addr, size, val);
        break;
    /* StatsAddr 0xF8-0xFB */
    case 0xF8 ... 0xFB:
        stn_le_p(s->regs + addr, size, val);
        break;
    /* TxThresh 0x36 */
    case 0x36:
        s->regs[addr] = val;
        break;
    /* MultiIntr 0x5A-0x5B */
    case 0x5A ... 0x5B:
        stn_le_p(s->regs + addr, size, val);
        break;
    default:
        if (addr < CP_REGS_SIZE) {
            stn_le_p(s->regs + addr, size, val);
        }
        break;
    }
}

/* PIO handlers – not used by this driver, but kept for completeness */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

    /* Reset register shadows to known default values */
    memset(s->regs, 0, CP_REGS_SIZE);
    s->intr_status = 0;
    s->intr_mask = 0;
    s->eeprom_state = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_REALTEK);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_REALTEK_8139);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x20); /* Must be >= 0x20 for 8139C+ */
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization – only BAR1 (MMIO) is used by the driver */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 1,
        .type = BAR_TYPE_MMIO,
        .size = CP_REGS_SIZE,
        .name = "8139cp-mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize EEPROM contents with valid MAC and ID */
    s->eeprom[0] = 0x8129;  /* ID to indicate 8139C+ with 8-bit address */
    s->eeprom[7] = 0x1100;  /* MAC bytes: 00:11 */
    s->eeprom[8] = 0x3322;  /* MAC bytes: 22:33 */
    s->eeprom[9] = 0x5544;  /* MAC bytes: 44:55 */
    /* Other EEPROM words are 0 */

    s->eeprom_state = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No MSI/MSI-X cleanup needed */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "8139cp_pci",
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
