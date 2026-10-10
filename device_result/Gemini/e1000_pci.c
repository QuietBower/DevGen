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

#define TYPE_PCIBASE_DEVICE "e1000_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define E1000_CTRL     0x00000
#define E1000_STATUS   0x00008
#define E1000_RCTL     0x00100
#define E1000_TCTL     0x00400
#define E1000_IMS      0x000D0
#define E1000_ICR      0x000C0
#define E1000_ICS      0x000C8
#define E1000_RDBAL    0x02800
#define E1000_RDBAH    0x02804
#define E1000_RDLEN    0x02808
#define E1000_TDBAL    0x03800
#define E1000_TDBAH    0x03804
#define E1000_TDLEN    0x03808
#define E1000_TDT 0x03818
#define E1000_82542_TDT 0x00438
#define E1000_TDH 0x03810
#define E1000_82542_TDH 0x00430
#define E1000_RDT 0x02818
#define E1000_82542_RDT 0x00128
#define E1000_82542_RDH 0x00120
#define E1000_RDH 0x02810

#define E1000_IMC      0x000D8
#define E1000_PBA      0x01000
#define E1000_WUC      0x05800
#define E1000_VET      0x00038
#define E1000_ICR_RXSEQ         0x00000008
#define E1000_ICR_LSC           0x00000004
#define E1000_STATUS_FUNC_1     0x00000004
#define E1000_STATUS_LU         0x00000002
#define E1000_TXD_STAT_DD       0x00000001
#define E1000_RXD_STAT_DD       0x01

#define E1000_CTRL_RST      0x04000000
#define E1000_STATUS_FD         0x00000001
#define E1000_STATUS_SPEED_1000 0x00000080
#define E1000_ICS_LSC       E1000_ICR_LSC

#define E1000_DEV_ID_82545EM_COPPER      0x100F
#define E1000_DEV_ID_82546GB_PCIE        0x108A
#define E1000_DEV_ID_82546EB_FIBER       0x1012
#define E1000_DEV_ID_82546GB_FIBER       0x107A
#define E1000_DEV_ID_82546GB_QUAD_COPPER_KSP3 0x10B5

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
    uint32_t mac_reg[0x10000 / 4];

    /* DMA Context */
    dma_addr_t rx_ring_base;
    dma_addr_t tx_ring_base;
    uint32_t rx_ring_size;
    uint32_t tx_ring_size;
};

struct e1000_tx_desc {
    uint64_t buffer_addr;
    union {
        uint32_t data;
        struct {
            uint16_t length;
            uint8_t cso;
            uint8_t cmd;
        } flags;
    } lower;
    union {
        uint32_t data;
        struct {
            uint8_t status;
            uint8_t css;
            uint16_t special;
        } fields;
    } upper;
};

struct e1000_rx_desc {
    uint64_t buffer_addr;
    uint16_t length;
    uint16_t csum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->mac_reg[E1000_ICR / 4] & s->intr_mask) != 0;
    pci_set_irq(pdev, level);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, hwaddr addr)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (addr == E1000_TDT || addr == E1000_82542_TDT) {
        uint32_t tdh_reg = (addr == E1000_TDT) ? (E1000_TDH / 4) : (E1000_82542_TDH / 4);
        uint32_t tdt_reg = addr / 4;
        uint32_t tdh = s->mac_reg[tdh_reg];
        uint32_t tdt = s->mac_reg[tdt_reg];
        uint64_t tdba = ((uint64_t)s->mac_reg[E1000_TDBAH / 4] << 32) | s->mac_reg[E1000_TDBAL / 4];
        uint32_t tdlen = s->mac_reg[E1000_TDLEN / 4];
        uint32_t desc_count = tdlen / sizeof(struct e1000_tx_desc);

        if (desc_count == 0) return;

        while (tdh != tdt) {
            struct e1000_tx_desc desc;
            pci_dma_read(pdev, tdba + tdh * sizeof(desc), &desc, sizeof(desc));
            
            desc.upper.fields.status |= E1000_TXD_STAT_DD;
            
            pci_dma_write(pdev, tdba + tdh * sizeof(desc), &desc, sizeof(desc));

            tdh = (tdh + 1) % desc_count;
        }
        s->mac_reg[tdh_reg] = tdh;
    } else if (addr == E1000_RDT || addr == E1000_82542_RDT) {
        uint32_t rdh_reg = (addr == E1000_RDT) ? (E1000_RDH / 4) : (E1000_82542_RDH / 4);
        uint32_t rdt_reg = addr / 4;
        uint32_t rdh = s->mac_reg[rdh_reg];
        uint32_t rdt = s->mac_reg[rdt_reg];
        uint64_t rdba = ((uint64_t)s->mac_reg[E1000_RDBAH / 4] << 32) | s->mac_reg[E1000_RDBAL / 4];
        uint32_t rdlen = s->mac_reg[E1000_RDLEN / 4];
        uint32_t desc_count = rdlen / sizeof(struct e1000_rx_desc);

        if (desc_count > 0) {
            while (rdh != rdt) {
                struct e1000_rx_desc desc;
                pci_dma_read(pdev, rdba + rdh * sizeof(desc), &desc, sizeof(desc));
                
                desc.status |= E1000_RXD_STAT_DD;
                
                pci_dma_write(pdev, rdba + rdh * sizeof(desc), &desc, sizeof(desc));

                rdh = (rdh + 1) % desc_count;
            }
        }
        s->mac_reg[rdh_reg] = rdt;
    }
}

static void pcibase_reset(DeviceState *dev);

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t index = addr / 4;
    uint64_t val = 0;

    if (index < ARRAY_SIZE(s->mac_reg)) {
        if (addr == E1000_ICR) {
            val = s->mac_reg[index];
            s->mac_reg[index] = 0; /* Clear on read */
            pcibase_update_irq(s);
            return val;
        }
        if (addr == E1000_STATUS) {
            /* Return Link Up status */
            return s->mac_reg[index] | E1000_STATUS_LU | E1000_STATUS_FD | E1000_STATUS_SPEED_1000;
        }
        val = s->mac_reg[index];
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t index = addr / 4;

    if (index < ARRAY_SIZE(s->mac_reg)) {
        if (addr == E1000_CTRL) {
            if (val & E1000_CTRL_RST) {
                pcibase_reset(DEVICE(s));
            }
            s->mac_reg[index] = val & ~E1000_CTRL_RST;
        } else if (addr == E1000_IMS) {
            s->intr_mask |= val;
            pcibase_update_irq(s);
        } else if (addr == E1000_IMC) {
            s->intr_mask &= ~val;
            pcibase_update_irq(s);
        } else if (addr == E1000_ICS) {
            s->mac_reg[E1000_ICR / 4] |= val;
            pcibase_update_irq(s);
        } else if (addr == E1000_TDT || addr == E1000_82542_TDT) {
            s->mac_reg[index] = val;
            pcibase_do_dma(s, addr);
        } else if (addr == E1000_RDT || addr == E1000_82542_RDT) {
            s->mac_reg[index] = val;
            pcibase_do_dma(s, addr);
        } else {
            s->mac_reg[index] = val;
        }
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
    memset(s->mac_reg, 0, sizeof(s->mac_reg));
    s->intr_mask = 0;
    s->intr_status = 0;
    
    /* Initialize STATUS register with Link Up */
    s->mac_reg[E1000_STATUS / 4] |= E1000_STATUS_LU | E1000_STATUS_FD | E1000_STATUS_SPEED_1000;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x100F );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;  
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_MMIO, 0x20000, "e1000-mmio"};
    s->bar_info[1] = (BARInfo){1, BAR_TYPE_PIO, 0x40, "e1000-pio"};
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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
    .name = "e1000_pci",
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
