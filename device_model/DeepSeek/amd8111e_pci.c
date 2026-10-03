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

#define TYPE_PCIBASE_DEVICE "amd8111e_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs: first entry from pci_device_id table */
#define VENDOR_ID 0x1022  /* PCI_VENDOR_ID_AMD */
#define DEVICE_ID 0x7462  /* PCI_DEVICE_ID_AMD8111E_7462 */
#define CLASS_ID  0x0200  /* PCI_CLASS_NETWORK_ETHERNET */

/* Register Offsets (from driver #defines) */
#define CHIPID              0x04
#define MIB_DATA            0x10
#define MIB_ADDR            0x14
#define STAT0               0x30
#define INT0                0x38
#define INTEN0              0x40
#define CMD0                0x48
#define CMD2                0x50
#define CMD3                0x54
#define CMD7                0x64
#define CTRL1               0x6C
#define CTRL2               0x70
#define XMT_RING_LIMIT      0x7C
#define AUTOPOLL0           0x88
#define DLY_INT_A           0xA8
#define DLY_INT_B           0xAC
#define FLOW_CONTROL        0xC8
#define PHY_ACCESS          0xD0
#define STVAL               0xD8
#define XMT_RING_BASE_ADDR0 0x100
#define XMT_RING_BASE_ADDR1 0x108
#define XMT_RING_BASE_ADDR2 0x110
#define XMT_RING_BASE_ADDR3 0x118
#define RCV_RING_BASE_ADDR0 0x120
#define XMT_RING_LEN0       0x140
#define XMT_RING_LEN1       0x144
#define XMT_RING_LEN2       0x148
#define XMT_RING_LEN3       0x14C
#define RCV_RING_LEN0       0x150
#define PADR                0x160
#define LADRF               0x168
#define SRAM_SIZE           0x178
#define IFS1                0x18C
#define IPG                 0x18E

/* DMA descriptor definitions (from driver structs) */
struct amd8111e_rx_dr {
    uint32_t reserved;
    uint16_t msg_count;   /* Received message len */
    uint16_t tag_ctrl_info;
    uint16_t buff_count;  /* Len of the buffer pointed by descriptor. */
    uint16_t rx_flags;
    uint32_t buff_phy_addr;
};

struct amd8111e_tx_dr {
    uint16_t buff_count; /* Size of the buffer pointed by this descriptor */
    uint16_t tx_flags;
    uint16_t tag_ctrl_info;
    uint16_t tag_ctrl_cmd;
    uint32_t buff_phy_addr;
    uint32_t reserved;
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

/* Placeholder for missing PHY/MIB command bits - will be resolved via needed_sources */
#define PHY_CMD_ACTIVE_BIT  31
#define PHY_CMD_ACTIVE      (1U << PHY_CMD_ACTIVE_BIT)
#define PHY_RD_ERR_BIT      30
#define PHY_RD_ERR          (1U << PHY_RD_ERR_BIT)

#define MIB_CMD_ACTIVE_BIT  15
#define MIB_CMD_ACTIVE      (1U << MIB_CMD_ACTIVE_BIT)

/* New driver constants from this iteration */
#define INTR            0x0f
#define SPEED_MASK      0x145
#define PHY_SPEED_10    0x2
#define PHY_SPEED_100   0x3
#define VSIZE           16
#define SOFT_TIMER_FREQ 0xBEBC
#define DEFAULT_IPG     0x60
#define IFS1_DELTA      36
#define TT_MASK         0x000c
#define RESET_RX_FLAGS  0x0000

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
    uint8_t regs[0x200];  /* MMIO register file, covers offsets up to 0x1FF */

    /* DMA Context */
    dma_addr_t tx_ring_base;
    dma_addr_t rx_ring_base;
    uint32_t tx_ring_len;
    uint32_t rx_ring_len;

    uint32_t status;
    bool reset_active;
    uint8_t power_state;

    /* PHY emulation: 32 registers per PHY address (0..31) */
    uint16_t phy_regs[32][32];
    uint32_t mib_data;
};

/* Forward declarations */
static void amd8111e_update_irq(PCIBaseState *s);
static void amd8111e_handle_phy_access(PCIBaseState *s, uint32_t val);
static void amd8111e_handle_mib_addr(PCIBaseState *s, uint32_t val);

/* Internal helper for status-triggered signaling. */
static void amd8111e_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t int_en = ldl_le_p(s->regs + INTEN0);
    uint32_t int_sts = ldl_le_p(s->regs + INT0);
    uint32_t cmd0 = ldl_le_p(s->regs + CMD0);

    /* Global interrupt enable: assume bit 1 of CMD0 (to be updated when definitions are available) */
    bool global_en = (cmd0 & (1U << 1)) != 0;

    /* Compute the logical OR of all enabled interrupt status bits */
    uint32_t enabled_sts = int_sts & int_en;
    bool any_enabled = (enabled_sts != 0) && global_en;

    if (any_enabled) {
        /* Raise IRQ if not already */
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read out of bounds: 0x%" HWADDR_PRIx "\n", __func__, addr);
        return 0;
    }

    switch (addr) {
    case CHIPID:
        val = 0x20000000;  /* chip version 2 */
        break;
    case MIB_DATA:
        val = s->mib_data;
        break;
    case MIB_ADDR:
        /* Return the MIB_ADDR status: we clear the active bit after command */
        val = ldl_le_p(s->regs + MIB_ADDR);
        break;
    case STAT0:
        val = ldl_le_p(s->regs + STAT0);
        break;
    case INT0:
        /* Read interrupt status: compute INTR summary bit */
        {
            uint32_t raw = ldl_le_p(s->regs + INT0);
            uint32_t int_en = ldl_le_p(s->regs + INTEN0);
            uint32_t enabled = raw & int_en;
            if (enabled) {
                raw |= (1U << 31); /* Assume INTR is bit 31 (to be confirmed) */
            } else {
                raw &= ~(1U << 31);
            }
            val = raw;
        }
        break;
    case INTEN0:
        val = ldl_le_p(s->regs + INTEN0);
        break;
    case CMD0:
    case CMD2:
    case CMD3:
    case CMD7:
        val = ldl_le_p(s->regs + addr);
        break;
    case PHY_ACCESS:
        val = ldl_le_p(s->regs + PHY_ACCESS);
        break;
    case PADR:
    case PADR+1:
    case PADR+2:
    case PADR+3:
    case PADR+4:
    case PADR+5:
        /* MAC address bytes */
        val = s->regs[addr];
        break;
    default:
        /* Generic read from shadow */
        switch (size) {
        case 1:
            val = s->regs[addr];
            break;
        case 2:
            if (addr % 2 == 0) {
                val = lduw_le_p(s->regs + addr);
            } else {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned 16-bit read at 0x%" HWADDR_PRIx "\n", __func__, addr);
                val = 0;
            }
            break;
        case 4:
            if (addr % 4 == 0) {
                val = ldl_le_p(s->regs + addr);
            } else {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned 32-bit read at 0x%" HWADDR_PRIx "\n", __func__, addr);
                val = 0;
            }
            break;
        case 8:
            if (addr % 8 == 0) {
                val = ldq_le_p(s->regs + addr);
            } else {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned 64-bit read at 0x%" HWADDR_PRIx "\n", __func__, addr);
                val = 0;
            }
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid read size %d at 0x%" HWADDR_PRIx "\n", __func__, size, addr);
            val = 0;
            break;
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write out of bounds: 0x%" HWADDR_PRIx "\n", __func__, addr);
        return;
    }

    switch (addr) {
    case CHIPID:
        /* Read-only, ignore writes */
        break;
    case MIB_DATA:
        /* Read-only */
        break;
    case MIB_ADDR:
        amd8111e_handle_mib_addr(s, val);
        break;
    case STAT0:
        /* Read-only; ignore */
        break;
    case INT0:
        /* Write-1-to-clear: clear bits that are set in 'val', except INTR which is read-only */
        {
            uint32_t current = ldl_le_p(s->regs + INT0);
            /* Assume INTR is bit 31, and others are writable. Mask out read-only bits. */
            uint32_t writable_clear = val & ~(1U << 31);
            current &= ~writable_clear;
            stl_le_p(s->regs + INT0, current);
        }
        amd8111e_update_irq(s);
        break;
    case INTEN0:
        stl_le_p(s->regs + INTEN0, val);
        amd8111e_update_irq(s);
        break;
    case CMD0:
        stl_le_p(s->regs + CMD0, val);
        amd8111e_update_irq(s);
        break;
    case CMD2:
    case CMD3:
    case CMD7:
        stl_le_p(s->regs + addr, val);
        break;
    case PHY_ACCESS:
        amd8111e_handle_phy_access(s, val);
        break;
    case PADR:
    case PADR+1:
    case PADR+2:
    case PADR+3:
    case PADR+4:
    case PADR+5:
        /* MAC address bytes */
        s->regs[addr] = (uint8_t)val;
        break;
    default:
        /* Generic write to shadow */
        switch (size) {
        case 1:
            s->regs[addr] = (uint8_t)val;
            break;
        case 2:
            if (addr % 2 == 0) {
                stw_le_p(s->regs + addr, (uint16_t)val);
            } else {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned 16-bit write at 0x%" HWADDR_PRIx "\n", __func__, addr);
            }
            break;
        case 4:
            if (addr % 4 == 0) {
                stl_le_p(s->regs + addr, (uint32_t)val);
            } else {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned 32-bit write at 0x%" HWADDR_PRIx "\n", __func__, addr);
            }
            break;
        case 8:
            if (addr % 8 == 0) {
                stq_le_p(s->regs + addr, val);
            } else {
                qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned 64-bit write at 0x%" HWADDR_PRIx "\n", __func__, addr);
            }
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid write size %d at 0x%" HWADDR_PRIx "\n", __func__, size, addr);
            break;
        }
        break;
    }
}

static void amd8111e_handle_phy_access(PCIBaseState *s, uint32_t val)
{
    /* Extract fields: bits [20:16] register, bits [25:21] PHY address, command in upper bits. */
    uint8_t dev_addr = (val >> 21) & 0x1f;
    uint8_t reg_addr = (val >> 16) & 0x1f;
    uint16_t data = val & 0xffff;

    /* Check if it's a read or write command. Assume PHY_RD_CMD is (1 << 26) and PHY_WR_CMD is (1 << 27) (to be confirmed) */
    const uint32_t PHY_RD_CMD_BIT = 26;
    const uint32_t PHY_WR_CMD_BIT = 27;
    bool is_read = (val & (1U << PHY_RD_CMD_BIT)) != 0;
    bool is_write = (val & (1U << PHY_WR_CMD_BIT)) != 0;

    if (is_read) {
        /* Read from PHY register */
        if (dev_addr < 32 && reg_addr < 32) {
            uint16_t phy_data = s->phy_regs[dev_addr][reg_addr];
            /* Set response: clear active and error bits, insert data in low 16 bits */
            uint32_t resp = val & ~(PHY_CMD_ACTIVE | PHY_RD_ERR | 0xFFFF);
            resp |= phy_data;
            stl_le_p(s->regs + PHY_ACCESS, resp);
        } else {
            /* Invalid address: set error and clear active */
            uint32_t resp = val | PHY_RD_ERR;
            resp &= ~PHY_CMD_ACTIVE;
            stl_le_p(s->regs + PHY_ACCESS, resp);
        }
    } else if (is_write) {
        /* Write to PHY register */
        if (dev_addr < 32 && reg_addr < 32) {
            s->phy_regs[dev_addr][reg_addr] = data;
            /* Write command: clear active bit (no error) */
            uint32_t resp = val & ~PHY_CMD_ACTIVE;
            stl_le_p(s->regs + PHY_ACCESS, resp);
        } else {
            uint32_t resp = val | PHY_RD_ERR;
            resp &= ~PHY_CMD_ACTIVE;
            stl_le_p(s->regs + PHY_ACCESS, resp);
        }
    } else {
        /* Not a recognized command; just store the value */
        stl_le_p(s->regs + PHY_ACCESS, val);
    }
}

static void amd8111e_handle_mib_addr(PCIBaseState *s, uint32_t val)
{
    /* The driver writes a 16-bit MIB command. Assume MIB_RD_CMD is a specific bit. */
    /* We'll just immediately complete the command: clear active bit and set MIB_DATA to 0. */
    s->mib_data = 0;
    /* Clear the active bit in MIB_ADDR and store the command without active bit */
    uint32_t cmd = val & ~MIB_CMD_ACTIVE;
    stl_le_p(s->regs + MIB_ADDR, cmd);
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->regs, 0, sizeof(s->regs));

    /* Set initial values for specific registers */
    /* CHIPID: version 2 */
    stl_le_p(s->regs + CHIPID, 0x20000000);

    /* PADR: default MAC address 00:11:22:33:44:55 */
    s->regs[PADR + 0] = 0x00;
    s->regs[PADR + 1] = 0x11;
    s->regs[PADR + 2] = 0x22;
    s->regs[PADR + 3] = 0x33;
    s->regs[PADR + 4] = 0x44;
    s->regs[PADR + 5] = 0x55;

    /* SRAM_SIZE: set default value as driver would */
    stl_le_p(s->regs + SRAM_SIZE, 0x80010);

    /* Set IPG to DEFAULT_IPG per driver constant */
    stw_le_p(s->regs + IPG, DEFAULT_IPG);

    /* Initialize PHY registers: we'll set a valid PHY ID at address 1 */
    memset(s->phy_regs, 0, sizeof(s->phy_regs));
    /* PHY ID: 0x00406300 (Broadcom-like) */
    s->phy_regs[1][2] = 0x0040;  /* MII_PHYSID1 */
    s->phy_regs[1][3] = 0x6300;  /* MII_PHYSID2 */
    /* Also set some basic status/control registers if needed */
    s->phy_regs[1][0] = 0x1000;  /* MII_BMCR: auto-negotiation enable */
    s->phy_regs[1][1] = 0x782D;  /* MII_BMSR: link up, etc. */
    s->phy_regs[1][4] = 0x01E1;  /* MII_ADVERTISE: advertise 10/100 half/full */

    /* Clear MIB data */
    s->mib_data = 0;
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
        /* PIO not used by this device; placeholder removed */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    /* BAR0: MMIO region for control/status registers, size 512 bytes (power of 2) */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x200;
    s->bar_info[0].name = "amd8111e-mmio";
    s->num_bars = 1;
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Ensure interrupt is initially low */
    pci_set_irq(pdev, 0);
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
    /* No additional cleanup needed */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "amd8111e_pci",
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
