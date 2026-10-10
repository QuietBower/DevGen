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

/* Additional include files retrieved from driver context */
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "qlcnic_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#ifndef PCI_VENDOR_ID_QLOGIC
#define PCI_VENDOR_ID_QLOGIC 0x1077
#endif
#define PCI_DEVICE_ID_QLOGIC_QLE824X 0x8020
#define QLCNIC_82XX_BAR0_LENGTH 0x00200000UL
#define QLCNIC_MSIX_BASE 0x132110
#define QLCNIC_MSIX_TABLE_OFFSET 0x44

/* Newly discovered absolute register offsets */
#define PCIX_INT_VECTOR         0x10100
#define PCIX_OCM_WINDOW         0x10800
#define PCIE_MISCCFG_RC         0x1206c

/* Device States */
#define QLCNIC_DEV_COLD			0x1
#define QLCNIC_DEV_INITIALIZING		0x2
#define QLCNIC_DEV_READY		0x3
#define QLCNIC_DEV_NEED_RESET		0x4
#define QLCNIC_DEV_NEED_QUISCENT	0x5
#define QLCNIC_DEV_FAILED		0x6
#define QLCNIC_DEV_QUISCENT		0x7

#define QLCNIC_DRV_IDC_VER  0x01

struct cmd_desc_type0 {
    uint8_t tcp_hdr_offset;
    uint8_t ip_hdr_offset;
    uint16_t flags_opcode;
    uint32_t nfrags__length;
    uint64_t addr_buffer2;
    uint16_t encap_descr;
    uint16_t mss;
    uint8_t port_ctxid;
    uint8_t hdr_length;
    uint8_t outer_hdr_length;
    uint8_t rsvd1;
    uint64_t addr_buffer3;
    uint64_t addr_buffer1;
    uint16_t buffer_length[4];
    uint64_t addr_buffer4;
    uint8_t eth_addr[6];
    uint16_t vlan_TCI;
};

struct rcv_desc {
    uint16_t reference_handle;
    uint16_t reserved;
    uint32_t buffer_length;
    uint64_t addr_buffer;
};

struct status_desc {
    uint64_t status_desc_data[2];
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
    uint32_t isr_int_state;
    uint32_t isr_int_vector;
    uint32_t crb_drv_version;
    uint32_t fw_capabilities;
    uint32_t pcie_misccfg_rc;

    /* DMA Context */
    dma_addr_t tx_ring_phys_addr;
    dma_addr_t rx_ring_phys_addr;
    dma_addr_t sts_ring_phys_addr;

    uint32_t dev_state;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    
    switch (addr) {
        case PCIX_INT_VECTOR:
            val = s->isr_int_vector;
            break;
        case PCIE_MISCCFG_RC:
            val = s->pcie_misccfg_rc;
            break;
        default:
            /* 
             * Concrete MMIO read logic is pending resolution of base offset macros 
             * (e.g., QLCNIC_CRB_PCIX_MD, QLCNIC_REG, QLCNIC_CRB_NIU).
             */
            break;
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    switch (addr) {
        case PCIX_INT_VECTOR:
            s->isr_int_vector = val;
            break;
        case PCIE_MISCCFG_RC:
            s->pcie_misccfg_rc = val;
            break;
        default:
            /* 
             * Concrete MMIO write logic is pending resolution of base offset macros 
             * (e.g., QLCNIC_CRB_PCIX_MD, QLCNIC_REG, QLCNIC_CRB_NIU).
             */
            break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    return val;
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
    
    /* Initialize device state to COLD as expected by driver probe */
    s->dev_state = QLCNIC_DEV_COLD;
    s->isr_int_vector = 0;
    s->pcie_misccfg_rc = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_QLOGIC );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_QLOGIC_QLE824X );
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
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = QLCNIC_82XX_BAR0_LENGTH;
    s->bar_info[0].name = "qlcnic-bar0";  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = true;
    msi_init(pdev, 0, 1, true, false, errp);  
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "qlcnic_pci",
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
