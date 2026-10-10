/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
 * NOTE: Do not directly fill this file with the Linux kernel source code.
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

#define PCI_VENDOR_ID_SBE		0x1176
#define PCI_DEVICE_ID_SBE_WANXL100	0x0301

#define PLX_CTL_RESET   0x40000000
#define MBX1_CMD_ABORTJ 0x85000000
#define MBX1_CMD_BSWAP  0x8C000001
#define MBX2_MEMSZ_MASK 0xFFFF0000
#define PLX_OFFSET      0
#define PLX_MAILBOX_0   (PLX_OFFSET + 0x40)
#define PLX_MAILBOX_1   (PLX_OFFSET + 0x44)
#define PLX_MAILBOX_2   (PLX_OFFSET + 0x48)
#define PLX_MAILBOX_5   (PLX_OFFSET + 0x54)
#define PLX_DOORBELL_TO_CARD    (PLX_OFFSET + 0x60)
#define PLX_DOORBELL_FROM_CARD  (PLX_OFFSET + 0x64)
#define PLX_CONTROL     (PLX_OFFSET + 0x6C)

#define PDM_OFFSET 0x1000
#define BUFFERS_ADDR	0x4000
#define TX_BUFFERS 10
#define RX_BUFFERS 30
#define RX_QUEUE_LENGTH 40
#define DOORBELL_FROM_CARD_TX_0		0
#define DOORBELL_FROM_CARD_CABLE_0	5
#define DOORBELL_FROM_CARD_RX		4

#define DOORBELL_TO_CARD_OPEN_0		0
#define DOORBELL_TO_CARD_CLOSE_0	4
#define DOORBELL_TO_CARD_TX_0		8
#define PACKET_EMPTY		0x00
#define PACKET_FULL		0x10

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

    uint32_t plx_mailbox[6];
    uint32_t plx_doorbell_to_card;
    uint32_t plx_doorbell_from_card;
    uint32_t plx_control;

    dma_addr_t status_address;
};

typedef struct {
    volatile uint32_t stat;
    uint32_t address;
    volatile uint32_t length;
} desc_t;

typedef struct {
    volatile uint32_t open;
    volatile uint32_t cable;
    volatile uint32_t rx_overruns;
    volatile uint32_t rx_frame_errors;
    uint32_t parity;
    uint32_t encoding;
    uint32_t clocking;
    desc_t tx_descs[10];
} port_status_t;

typedef struct {
    desc_t rx_descs[40];
    port_status_t port_status[4];
} card_status;

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->plx_doorbell_from_card) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t *ram_ptr = memory_region_get_ram_ptr(&s->bar_regions[2]);
    uint32_t status_address = ldl_le_p(ram_ptr + PDM_OFFSET + 20);

    if (!status_address) return;

    uint32_t db = s->plx_doorbell_to_card;
    if (!db) return;

    for (int i = 0; i < 4; i++) {
        if (db & (1 << (DOORBELL_TO_CARD_OPEN_0 + i))) {
            uint32_t open_val = 1;
            hwaddr open_addr = status_address + offsetof(card_status, port_status[i].open);
            pci_dma_write(pdev, open_addr, &open_val, sizeof(open_val));
            s->plx_doorbell_to_card &= ~(1 << (DOORBELL_TO_CARD_OPEN_0 + i));
        }
        if (db & (1 << (DOORBELL_TO_CARD_CLOSE_0 + i))) {
            uint32_t open_val = 0;
            hwaddr open_addr = status_address + offsetof(card_status, port_status[i].open);
            pci_dma_write(pdev, open_addr, &open_val, sizeof(open_val));
            s->plx_doorbell_to_card &= ~(1 << (DOORBELL_TO_CARD_CLOSE_0 + i));
        }
        if (db & (1 << (DOORBELL_TO_CARD_TX_0 + i))) {
            for (int j = 0; j < 10; j++) {
                hwaddr stat_addr = status_address + offsetof(card_status, port_status[i].tx_descs[j].stat);
                uint32_t stat;
                pci_dma_read(pdev, stat_addr, &stat, sizeof(stat));
                if (stat == PACKET_FULL) {
                    stat = PACKET_EMPTY;
                    pci_dma_write(pdev, stat_addr, &stat, sizeof(stat));
                }
            }
            s->plx_doorbell_from_card |= (1 << (DOORBELL_FROM_CARD_TX_0 + i));
            pcibase_update_irq(s);
            s->plx_doorbell_to_card &= ~(1 << (DOORBELL_TO_CARD_TX_0 + i));
        }
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case PLX_MAILBOX_0:
        val = s->plx_mailbox[0];
        break;
    case PLX_MAILBOX_1:
        val = s->plx_mailbox[1];
        break;
    case PLX_MAILBOX_2:
        val = s->plx_mailbox[2];
        break;
    case PLX_MAILBOX_5:
        val = s->plx_mailbox[5];
        break;
    case PLX_DOORBELL_TO_CARD:
        val = s->plx_doorbell_to_card;
        break;
    case PLX_DOORBELL_FROM_CARD:
        val = s->plx_doorbell_from_card;
        break;
    case PLX_CONTROL:
        val = s->plx_control;
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case PLX_MAILBOX_0:
        s->plx_mailbox[0] = val;
        if (val == 0x80) {
            s->plx_mailbox[0] = 0;
        }
        break;
    case PLX_MAILBOX_1:
        s->plx_mailbox[1] = val;
        if (val == MBX1_CMD_BSWAP) {
            s->plx_mailbox[1] = 0;
        } else if (val == MBX1_CMD_ABORTJ) {
            s->plx_mailbox[1] = 0;
            s->plx_mailbox[5] = 0x00400000;
        }
        break;
    case PLX_MAILBOX_2:
        s->plx_mailbox[2] = val;
        break;
    case PLX_MAILBOX_5:
        s->plx_mailbox[5] = val;
        break;
    case PLX_DOORBELL_TO_CARD:
        s->plx_doorbell_to_card = val;
        pcibase_do_dma(s, true);
        break;
    case PLX_DOORBELL_FROM_CARD:
        s->plx_doorbell_from_card &= ~val;
        pcibase_update_irq(s);
        break;
    case PLX_CONTROL:
        s->plx_control = val;
        if (val & PLX_CTL_RESET) {
            s->plx_mailbox[0] = 0;
            s->plx_mailbox[1] = 0;
            s->plx_mailbox[5] = 0;
            s->plx_doorbell_to_card = 0;
            s->plx_doorbell_from_card = 0;
            pcibase_update_irq(s);
        }
        break;
    default:
        break;
    }
}

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

    memset(s->plx_mailbox, 0, sizeof(s->plx_mailbox));
    s->plx_mailbox[2] = 0x00400000;
    s->plx_doorbell_to_card = 0;
    s->plx_doorbell_from_card = 0;
    s->plx_control = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_SBE);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_SBE_WANXL100);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0280);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_MMIO, 0x80, "wanxl-plx"};
    s->bar_info[1] = (BARInfo){1, BAR_TYPE_NONE, 0, ""};
    s->bar_info[2] = (BARInfo){2, BAR_TYPE_RAM, 4 * 1024 * 1024, "wanxl-mem"};

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "wanXL_pci",
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
