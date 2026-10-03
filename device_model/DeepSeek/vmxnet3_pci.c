/*
 * QEMU vmxnet3 device model based on Linux driver vmxnet3_drv.c
 * Implements probe behavior: registers VRRS, UVRS, DCR, MAC, interrupt config,
 * command handling for GET_CONF_INTR, GET_MAX_QUEUES_CONF, GET_LINK, GET_DCR0_REG.
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

/* PCI IDs */
#define VENDOR_ID 0x15AD
#define DEVICE_ID 0x07B0
#define CLASS_ID PCI_CLASS_NETWORK_ETHERNET

/* Register offsets for BAR0 (doorbells & interrupts) */
#define VMXNET3_REG_IMR       0x0000  /* Interrupt Mask Register, stride 8 */
#define VMXNET3_REG_TXPROD    0x0600  /* Tx Producer Index, stride 8 */
#define VMXNET3_REG_RXPROD    0x0800  /* Rx Producer Index (ring0), stride 8 */
#define VMXNET3_REG_RXPROD2   0x0a00  /* Rx Producer Index (ring1), stride 8 */
/* Large BAR offsets (not used without CAP_LARGE_BAR) */
#define VMXNET3_REG_LB_TXPROD  0x2200
#define VMXNET3_REG_LB_RXPROD  0x2400
#define VMXNET3_REG_LB_RXPROD2 0x2600
#define VMXNET3_REG_ALIGN      8

/* Register offsets for BAR1 (control registers) */
#define VMXNET3_REG_VRRS  0x0000  /* Version Register (RW) */
#define VMXNET3_REG_UVRS  0x0004  /* Upt Version Register (RW) */
#define VMXNET3_REG_DSAL  0x0008  /* Driver Shared Area Low */
#define VMXNET3_REG_DSAH  0x000c  /* Driver Shared Area High */
#define VMXNET3_REG_CMD   0x0010  /* Command / Status */
#define VMXNET3_REG_MACL  0x0014  /* MAC Address Low */
#define VMXNET3_REG_MACH  0x0018  /* MAC Address High */
#define VMXNET3_REG_ICR   0x001c  /* Interrupt Cause Register */
#define VMXNET3_REG_ECR   0x0020  /* Event Cause Register */
#define VMXNET3_REG_DCR   0x0024  /* Device Capabilities Register */
#define VMXNET3_REG_PTCR  0x0028  /* Pass-Through Capabilities */

/* BAR sizes */
#define VMXNET3_BAR0_SIZE 0x1000
#define VMXNET3_BAR1_SIZE 0x1000

/* Version constants */
#define VMXNET3_REV_1 0
#define VMXNET3_REV_2 1
#define VMXNET3_REV_3 2
#define VMXNET3_REV_4 3
#define VMXNET3_REV_5 4
#define VMXNET3_REV_6 5
#define VMXNET3_REV_7 6
#define VMXNET3_REV_8 7
#define VMXNET3_REV_9 8

/* Capability bits */
#define VMXNET3_CAP_LARGE_BAR   16
#define VMXNET3_CAP_OOORX_COMP  17
#define VMXNET3_DCR_ERROR       31

/* Commands (written to VMXNET3_REG_CMD) */
#define VMXNET3_CMD_GET_DCR0_REG        1
#define VMXNET3_CMD_GET_MAX_QUEUES_CONF 2
#define VMXNET3_CMD_GET_CONF_INTR       3
#define VMXNET3_CMD_GET_LINK            4
#define VMXNET3_CMD_ACTIVATE_DEV        5
#define VMXNET3_CMD_QUIESCE_DEV         6
#define VMXNET3_CMD_RESET_DEV           7
#define VMXNET3_CMD_UPDATE_RX_MODE      8
#define VMXNET3_CMD_UPDATE_MAC_FILTERS  9
#define VMXNET3_CMD_UPDATE_VLAN_FILTERS 10
#define VMXNET3_CMD_UPDATE_PMCFG        11
#define VMXNET3_CMD_GET_TXDATA_DESC_SIZE 12
#define VMXNET3_CMD_GET_TSRING_DESC_SIZE 13
#define VMXNET3_CMD_SET_RING_BUFFER_SIZE 14
#define VMXNET3_CMD_GET_COALESCE        15
#define VMXNET3_CMD_SET_COALESCE        16
#define VMXNET3_CMD_GET_RSS_FIELDS      17
#define VMXNET3_CMD_SET_RSS_FIELDS      18
#define VMXNET3_CMD_GET_DISABLED_OFFLOADS 19

/* Interrupt types (returned by GET_CONF_INTR) */
#define VMXNET3_IT_INTX  0
#define VMXNET3_IT_MSI   1
#define VMXNET3_IT_MSIX  2

/* Number of interrupt vectors we can mask in IMR */
#define VMXNET3_IMR_MAX  4

#define TYPE_PCIBASE_DEVICE "vmxnet3_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

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

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Register shadows */
    uint32_t vrrs;          /* Version register, read-only mask */
    uint32_t uvrs;          /* UPT version register, read-only mask */
    uint32_t dcr;           /* Device capabilities */
    uint32_t ptcr;          /* Pass-through capabilities */
    uint32_t cmd;           /* Command written */
    uint32_t cmd_result;    /* Result to return on CMD read */
    uint32_t mac_low;       /* MAC address low 32 bits */
    uint32_t mac_high;      /* MAC address high 16 bits */
    uint32_t dsal, dsah;    /* Driver shared address */
    uint32_t icr;           /* Interrupt Cause Register */
    uint32_t ecr;           /* Event Cause Register */
    uint32_t imr[VMXNET3_IMR_MAX];  /* Interrupt mask per vector, stride 8 */
    /* Doorbell registers (producer indices) */
    uint32_t tx_prod[2];    /* Up to 2 TX queues, stride 8 */
    uint32_t rx_prod[2];    /* Up to 2 RX queues ring0 */
    uint32_t rx_prod2[2];   /* Up to 2 RX queues ring1 */
    /* Current queue counts (derived from GET_MAX_QUEUES_CONF) */
    uint8_t num_tx_queues;
    uint8_t num_rx_queues;
};

/* Interrupt management */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int i;
    bool raise = false;

    for (i = 0; i < VMXNET3_IMR_MAX; i++) {
        if (s->imr[i] == 0 && (s->icr & (1 << i))) {
            raise = true;
            break;
        }
    }
    if (raise) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* BAR0 MMIO handlers (doorbells and IMR) */
static uint64_t bar0_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= VMXNET3_REG_IMR && addr < VMXNET3_REG_IMR + VMXNET3_IMR_MAX * 8) {
        int idx = (addr - VMXNET3_REG_IMR) / 8;
        if (idx < VMXNET3_IMR_MAX) {
            val = s->imr[idx];
        }
    } else if (addr >= VMXNET3_REG_TXPROD && addr < VMXNET3_REG_TXPROD + 2 * 8) {
        int idx = (addr - VMXNET3_REG_TXPROD) / 8;
        if (idx < 2) {
            val = s->tx_prod[idx];
        }
    } else if (addr >= VMXNET3_REG_RXPROD && addr < VMXNET3_REG_RXPROD + 2 * 8) {
        int idx = (addr - VMXNET3_REG_RXPROD) / 8;
        if (idx < 2) {
            val = s->rx_prod[idx];
        }
    } else if (addr >= VMXNET3_REG_RXPROD2 && addr < VMXNET3_REG_RXPROD2 + 2 * 8) {
        int idx = (addr - VMXNET3_REG_RXPROD2) / 8;
        if (idx < 2) {
            val = s->rx_prod2[idx];
        }
    }
    return val;
}

static void bar0_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= VMXNET3_REG_IMR && addr < VMXNET3_REG_IMR + VMXNET3_IMR_MAX * 8) {
        int idx = (addr - VMXNET3_REG_IMR) / 8;
        if (idx < VMXNET3_IMR_MAX) {
            s->imr[idx] = (uint32_t)val;
            pcibase_update_irq(s);
        }
    } else if (addr >= VMXNET3_REG_TXPROD && addr < VMXNET3_REG_TXPROD + 2 * 8) {
        int idx = (addr - VMXNET3_REG_TXPROD) / 8;
        if (idx < 2) {
            s->tx_prod[idx] = (uint32_t)val;
        }
    } else if (addr >= VMXNET3_REG_RXPROD && addr < VMXNET3_REG_RXPROD + 2 * 8) {
        int idx = (addr - VMXNET3_REG_RXPROD) / 8;
        if (idx < 2) {
            s->rx_prod[idx] = (uint32_t)val;
        }
    } else if (addr >= VMXNET3_REG_RXPROD2 && addr < VMXNET3_REG_RXPROD2 + 2 * 8) {
        int idx = (addr - VMXNET3_REG_RXPROD2) / 8;
        if (idx < 2) {
            s->rx_prod2[idx] = (uint32_t)val;
        }
    }
}

static const MemoryRegionOps bar0_mmio_ops = {
    .read = bar0_mmio_read,
    .write = bar0_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* BAR1 MMIO handlers (control registers) */
static uint64_t bar1_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case VMXNET3_REG_VRRS:
        val = s->vrrs;
        break;
    case VMXNET3_REG_UVRS:
        val = s->uvrs;
        break;
    case VMXNET3_REG_DSAL:
        val = s->dsal;
        break;
    case VMXNET3_REG_DSAH:
        val = s->dsah;
        break;
    case VMXNET3_REG_CMD:
        val = s->cmd_result;
        break;
    case VMXNET3_REG_MACL:
        val = s->mac_low;
        break;
    case VMXNET3_REG_MACH:
        val = s->mac_high;
        break;
    case VMXNET3_REG_ICR:
        val = s->icr;
        break;
    case VMXNET3_REG_ECR:
        val = 0;
        break;
    case VMXNET3_REG_DCR:
        val = s->dcr;
        break;
    case VMXNET3_REG_PTCR:
        val = s->ptcr;
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
    case VMXNET3_REG_VRRS:
        /* Read-only, ignore writes */
        break;
    case VMXNET3_REG_UVRS:
        /* Read-only, ignore writes */
        break;
    case VMXNET3_REG_DSAL:
        s->dsal = (uint32_t)val;
        break;
    case VMXNET3_REG_DSAH:
        s->dsah = (uint32_t)val;
        break;
    case VMXNET3_REG_CMD:
        s->cmd = (uint32_t)val;
        switch (s->cmd) {
        case VMXNET3_CMD_GET_DCR0_REG:
            s->cmd_result = s->dcr;
            break;
        case VMXNET3_CMD_GET_MAX_QUEUES_CONF:
            s->cmd_result = 0x0101;
            break;
        case VMXNET3_CMD_GET_CONF_INTR:
            s->cmd_result = 0;
            break;
        case VMXNET3_CMD_GET_LINK:
            s->cmd_result = (1 << 16) | 1;
            break;
        case VMXNET3_CMD_ACTIVATE_DEV:
            s->cmd_result = 0;
            break;
        case VMXNET3_CMD_QUIESCE_DEV:
            s->cmd_result = 0;
            break;
        case VMXNET3_CMD_RESET_DEV:
            s->cmd_result = 0;
            break;
        default:
            s->cmd_result = 0;
            break;
        }
        break;
    case VMXNET3_REG_MACL:
        s->mac_low = (uint32_t)val;
        break;
    case VMXNET3_REG_MACH:
        s->mac_high = (uint32_t)val;
        break;
    case VMXNET3_REG_ICR:
        s->icr &= ~((uint32_t)val);
        pcibase_update_irq(s);
        break;
    case VMXNET3_REG_ECR:
        s->ecr &= ~((uint32_t)val);
        break;
    case VMXNET3_REG_DCR:
        s->dcr = (uint32_t)val;
        break;
    case VMXNET3_REG_PTCR:
        /* Read-only, ignore writes */
        break;
    default:
        break;
    }
}

static const MemoryRegionOps bar1_mmio_ops = {
    .read = bar1_mmio_read,
    .write = bar1_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    int i;

    pci_device_reset(PCI_DEVICE(dev));

    /* Set default register values after reset */
    s->vrrs = (1 << (VMXNET3_REV_9 + 1)) - 1;   /* support all revisions 1-9 */
    s->uvrs = 1;                                 /* UPT version 1 */
    s->dcr = 0;
    s->ptcr = 0;
    s->cmd = 0;
    s->cmd_result = 0;
    /* MAC address: 52:54:00:12:34:56 */
    s->mac_low  = 0x12005452;
    s->mac_high = 0x5634;
    s->dsal = s->dsah = 0;
    s->icr = 0;
    s->ecr = 0;
    for (i = 0; i < VMXNET3_IMR_MAX; i++) {
        s->imr[i] = 0;
    }
    for (i = 0; i < 2; i++) {
        s->tx_prod[i] = 0;
        s->rx_prod[i] = 0;
        s->rx_prod2[i] = 0;
    }
    s->num_tx_queues = 1;
    s->num_rx_queues = 1;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        if (bi->index == 0) {
            memory_region_init_io(mr, OBJECT(s), &bar0_mmio_ops, s, bi->name, aligned_size);
        } else {
            memory_region_init_io(mr, OBJECT(s), &bar1_mmio_ops, s, bi->name, aligned_size);
        }
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), NULL, s, bi->name, aligned_size);
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

    s->num_bars = 2;

    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = VMXNET3_BAR0_SIZE;
    s->bar_info[0].name = "vmxnet3-bar0";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = VMXNET3_BAR1_SIZE;
    s->bar_info[1].name = "vmxnet3-bar1";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    pcibase_reset(DEVICE(s));
}

static void pcibase_uninit(PCIDevice *pdev)
{
}

static const VMStateDescription vmstate_pcibase = {
    .name = "vmxnet3_pci",
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
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
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
