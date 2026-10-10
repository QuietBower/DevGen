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

#define TYPE_PCIBASE_DEVICE "rt61pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define RT61PCI_VENDOR_ID 0x1814
#define RT61PCI_DEVICE_ID 0x0301
#define RT61PCI_CLASS_ID  PCI_CLASS_NETWORK_OTHER

#define PHY_CSR3 0x308c
#define PHY_CSR4 0x3090
#define H2M_MAILBOX_CSR 0x2100
#define HOST_CMD_CSR 0x0008
#define E2PROM_CSR 0x3470
#define MAC_CSR13 0x3034
#define MCU_LED 0x50
#define MCU_LED_STRENGTH 0x52
#define MAC_CSR14 0x3038
#define SEC_CSR3 0x30ac
#define SEC_CSR4 0x30b0
#define SEC_CSR2 0x30a8
#define TXRX_CSR0 0x3040
#define TXRX_CSR9 0x3064
#define MAC_CSR2 0x3008
#define MAC_CSR4 0x3010
#define MAC_CSR9 0x3024
#define TXRX_CSR5 0x3054
#define TXRX_CSR4 0x3050
#define MAC_CSR8 0x3020
#define PHY_CSR0 0x3080
#define MAC_CSR11 0x302c
#define IO_CNTL_CSR 0x3498
#define PCI_USEC_CSR 0x001c
#define SOFT_RESET_CSR 0x0010
#define STA_CSR0 0x30c0
#define STA_CSR1 0x30c4
#define TX_CNTL_CSR 0x3430
#define M2H_CMD_DONE_CSR 0x2104
#define MAC_CSR1 0x3004
#define MAC_CSR0 0x3000
#define MCU_CNTL_CSR 0x000c
#define AC0_BASE_CSR 0x3400
#define RX_RING_CSR 0x3454
#define TX_RING_CSR1 0x341c
#define LOAD_TX_RING_CSR 0x3434
#define AC2_BASE_CSR 0x3408
#define AC3_BASE_CSR 0x340c
#define TX_DMA_DST_CSR 0x342c
#define RX_CNTL_CSR 0x3458
#define RX_BASE_CSR 0x3450
#define AC1_BASE_CSR 0x3404
#define TX_RING_CSR0 0x3418
#define SEC_CSR5 0x30b4
#define TXRX_CSR2 0x3048
#define TXRX_CSR8 0x3060
#define STA_CSR2 0x30c8
#define HW_BEACON_BASE2 0x2e00
#define SEC_CSR0 0x30a0
#define MAC_CSR6 0x3018
#define MAC_CSR10 0x3028
#define PCI_CFG_CSR 0x3460
#define PHY_CSR6 0x3098
#define TXRX_CSR15 0x307c
#define HW_BEACON_BASE0 0x2c00
#define HW_BEACON_BASE3 0x2f00
#define TXRX_CSR7 0x305c
#define TXRX_CSR1 0x3044
#define HW_BEACON_BASE1 0x2d00
#define TXRX_CSR3 0x304c
#define PHY_CSR1 0x3084
#define SEC_CSR1 0x30a4
#define PHY_CSR7 0x309c
#define TEST_MODE_CSR 0x3484
#define PHY_CSR5 0x3094
#define MCU_INT_MASK_CSR 0x0018
#define INT_SOURCE_CSR 0x3468
#define MCU_INT_SOURCE_CSR 0x0014
#define INT_MASK_CSR 0x346c
#define MAC_CSR12 0x3030
#define TXRX_CSR10 0x3068
#define STA_CSR4 0x30d0
#define EEPROM_FREQ 0x002f
#define EEPROM_LED 0x0030
#define EEPROM_NIC 0x0011
#define EEPROM_ANTENNA 0x0010
#define EEPROM_BASE 0x0000
#define EEPROM_MAC_ADDR_0 0x0002
#define EEPROM_TXPOWER_A_START 0x0031
#define EEPROM_TXPOWER_G_START 0x0023
#define AC_TXOP_CSR0 0x3474
#define CWMAX_CSR 0x3428
#define CWMIN_CSR 0x3424
#define AIFSN_CSR 0x3420
#define TXRX_CSR12 0x3070
#define TXRX_CSR13 0x3074
#define RF_BASE 0x0004
#define CSR_REG_BASE 0x3000
#define BBP_BASE 0x0000

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
    uint32_t int_mask_csr;
    uint32_t int_source_csr;
    uint32_t mcu_int_mask_csr;
    uint32_t mcu_int_source_csr;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t mac_csr0;
    uint32_t pci_cfg_csr;
    uint32_t mcu_cntl_csr;
    uint32_t host_cmd_csr;
    uint32_t soft_reset_csr;
    uint32_t io_cntl_csr;
    uint32_t e2prom_csr;

    /* DMA Context */
    uint32_t rx_base_csr;
    uint32_t rx_ring_csr;
    uint32_t rx_cntl_csr;
    uint32_t ac0_base_csr;
    uint32_t ac1_base_csr;
    uint32_t ac2_base_csr;
    uint32_t ac3_base_csr;
    uint32_t tx_ring_csr0;
    uint32_t tx_ring_csr1;
    uint32_t tx_dma_dst_csr;
    uint32_t load_tx_ring_csr;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool raise = false;

    if (s->int_source_csr & s->int_mask_csr) {
        raise = true;
    }
    if (s->mcu_int_source_csr & s->mcu_int_mask_csr) {
        raise = true;
    }

    pci_set_irq(pdev, raise ? 1 : 0);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case MAC_CSR0:
        val = s->mac_csr0 ? s->mac_csr0 : 0x00010000;
        break;
    case MCU_CNTL_CSR:
        val = s->mcu_cntl_csr | 0xffffffff;
        break;
    case E2PROM_CSR:
        val = s->e2prom_csr | 0xffffffff;
        break;
    case MAC_CSR12:
        val = 0xffffffff;
        break;
    case PHY_CSR3:
    case PHY_CSR4:
        val = 0;
        break;
    case H2M_MAILBOX_CSR:
        val = 0;
        break;
    case STA_CSR4:
        val = 0;
        break;
    case INT_SOURCE_CSR:
        val = s->int_source_csr;
        break;
    case MCU_INT_SOURCE_CSR:
        val = s->mcu_int_source_csr;
        break;
    case INT_MASK_CSR:
        val = s->int_mask_csr;
        break;
    case MCU_INT_MASK_CSR:
        val = s->mcu_int_mask_csr;
        break;
    case RX_BASE_CSR:
        val = s->rx_base_csr;
        break;
    case RX_RING_CSR:
        val = s->rx_ring_csr;
        break;
    case RX_CNTL_CSR:
        val = s->rx_cntl_csr;
        break;
    case AC0_BASE_CSR:
        val = s->ac0_base_csr;
        break;
    case AC1_BASE_CSR:
        val = s->ac1_base_csr;
        break;
    case AC2_BASE_CSR:
        val = s->ac2_base_csr;
        break;
    case AC3_BASE_CSR:
        val = s->ac3_base_csr;
        break;
    case TX_RING_CSR0:
        val = s->tx_ring_csr0;
        break;
    case TX_RING_CSR1:
        val = s->tx_ring_csr1;
        break;
    case TX_DMA_DST_CSR:
        val = s->tx_dma_dst_csr;
        break;
    case LOAD_TX_RING_CSR:
        val = s->load_tx_ring_csr;
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case MAC_CSR0:
        s->mac_csr0 = val;
        break;
    case MCU_CNTL_CSR:
        s->mcu_cntl_csr = val;
        break;
    case E2PROM_CSR:
        s->e2prom_csr = val;
        break;
    case INT_SOURCE_CSR:
        s->int_source_csr &= ~val;
        pcibase_update_irq(s);
        break;
    case MCU_INT_SOURCE_CSR:
        s->mcu_int_source_csr &= ~val;
        pcibase_update_irq(s);
        break;
    case INT_MASK_CSR:
        s->int_mask_csr = val;
        pcibase_update_irq(s);
        break;
    case MCU_INT_MASK_CSR:
        s->mcu_int_mask_csr = val;
        pcibase_update_irq(s);
        break;
    case RX_BASE_CSR:
        s->rx_base_csr = val;
        break;
    case RX_RING_CSR:
        s->rx_ring_csr = val;
        break;
    case RX_CNTL_CSR:
        s->rx_cntl_csr = val;
        break;
    case AC0_BASE_CSR:
        s->ac0_base_csr = val;
        break;
    case AC1_BASE_CSR:
        s->ac1_base_csr = val;
        break;
    case AC2_BASE_CSR:
        s->ac2_base_csr = val;
        break;
    case AC3_BASE_CSR:
        s->ac3_base_csr = val;
        break;
    case TX_RING_CSR0:
        s->tx_ring_csr0 = val;
        break;
    case TX_RING_CSR1:
        s->tx_ring_csr1 = val;
        break;
    case TX_DMA_DST_CSR:
        s->tx_dma_dst_csr = val;
        break;
    case LOAD_TX_RING_CSR:
        s->load_tx_ring_csr = val;
        break;
    case HOST_CMD_CSR:
        s->host_cmd_csr = val;
        break;
    case SOFT_RESET_CSR:
        s->soft_reset_csr = val;
        break;
    case IO_CNTL_CSR:
        s->io_cntl_csr = val;
        break;
    case PCI_CFG_CSR:
        s->pci_cfg_csr = val;
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

    s->mac_csr0 = 0;
    s->pci_cfg_csr = 0;
    s->mcu_cntl_csr = 0;
    s->host_cmd_csr = 0;
    s->soft_reset_csr = 0;
    s->io_cntl_csr = 0;
    s->e2prom_csr = 0;
    s->int_mask_csr = 0;
    s->int_source_csr = 0;
    s->mcu_int_mask_csr = 0;
    s->mcu_int_source_csr = 0;
    s->rx_base_csr = 0;
    s->rx_ring_csr = 0;
    s->rx_cntl_csr = 0;
    s->ac0_base_csr = 0;
    s->ac1_base_csr = 0;
    s->ac2_base_csr = 0;
    s->ac3_base_csr = 0;
    s->tx_ring_csr0 = 0;
    s->tx_ring_csr1 = 0;
    s->tx_dma_dst_csr = 0;
    s->load_tx_ring_csr = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1814 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0301 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_OTHER );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x4000;
    s->bar_info[0].name = "rt61pci-mmio";

    /* BAR Initialization */
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
    .name = "rt61pci_pci",
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
