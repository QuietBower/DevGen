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


#define TYPE_PCIBASE_DEVICE "atl2_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define REG_PM_CTRLSTAT         0x44
#define REG_PCIE_CAP_LIST       0x58
#define REG_VPD_CAP             0x6C
#define REG_VPD_DATA            0x70
#define REG_SPI_FLASH_CTRL      0x200
#define REG_SPI_ADDR            0x204
#define REG_SPI_DATA            0x208
#define REG_SPI_FLASH_CONFIG    0x20C
#define REG_SPI_FLASH_OP_PROGRAM 0x210
#define REG_SPI_FLASH_OP_SC_ERASE 0x211
#define REG_SPI_FLASH_OP_CHIP_ERASE 0x212
#define REG_SPI_FLASH_OP_RDID   0x213
#define REG_SPI_FLASH_OP_WREN   0x214
#define REG_SPI_FLASH_OP_RDSR   0x215
#define REG_SPI_FLASH_OP_WRSR   0x216
#define REG_SPI_FLASH_OP_READ   0x217
#define REG_TWSI_CTRL           0x218
#define REG_PCIE_DEV_MISC_CTRL  0x21C
#define REG_PCIE_PHYMISC        0x1000
#define REG_PCIE_DLL_TX_CTRL1   0x1104
#define REG_LTSSM_TEST_MODE     0x12FC
#define REG_MASTER_CTRL         0x1400
#define REG_MANUAL_TIMER_INIT   0x1404
#define REG_IRQ_MODU_TIMER_INIT 0x1408
#define REG_PHY_ENABLE          0x140C
#define REG_CMBDISDMA_TIMER     0x140E
#define REG_IDLE_STATUS         0x1410
#define REG_MDIO_CTRL           0x1414
#define REG_SERDES_LOCK         0x1424
#define REG_MAC_CTRL            0x1480
#define REG_MAC_IPG_IFG         0x1484
#define REG_MAC_STA_ADDR        0x1488
#define REG_RX_HASH_TABLE       0x1490
#define REG_MAC_HALF_DUPLX_CTRL 0x1498
#define REG_MTU                 0x149C
#define REG_WOL_CTRL            0x14A0
#define REG_SRAM_TXRAM_END      0x1500
#define REG_DESC_BASE_ADDR_HI   0x1540
#define REG_TXD_BASE_ADDR_LO    0x1544
#define REG_TXD_MEM_SIZE        0x1548
#define REG_TXS_BASE_ADDR_LO    0x154C
#define REG_TXS_MEM_SIZE        0x1550
#define REG_RXD_BASE_ADDR_LO    0x1554
#define REG_RXD_BUF_NUM         0x1558
#define REG_DMAR                0x1580
#define REG_TX_CUT_THRESH       0x1590
#define REG_DMAW                0x15A0
#define REG_PAUSE_ON_TH         0x15A8
#define REG_PAUSE_OFF_TH        0x15AA
#define REG_MB_TXD_WR_IDX       0x15f0
#define REG_MB_RXD_RD_IDX       0x15F4
#define REG_ISR                 0x1600
#define REG_IMR                 0x1604
#define REG_STS_RXD_OV          0x1704
#define REG_STS_RXS_OV          0x1708

#define MASTER_CTRL_SOFT_RST		0x1
#define ISR_PHY		0x800
#define ISR_DIS_INT			0x80000000
#define ISR_PHY_LINKDOWN		0x10000000
#define ISR_DMAR_TO_RST	0x200
#define ISR_DMAW_TO_RST	0x400
#define ISR_MANUAL	2

#define MDIO_START			0x800000
#define MDIO_BUSY			0x8000000
#define SPI_FLASH_CTRL_START		0x800
#define VPD_CAP_VPD_FLAG		0x80000000
#define ISR_TXF_UR	8
#define ISR_TXS_OV	0x10
#define ISR_HOST_TXD_UR	0x80
#define ISR_TS_UPDATE	0x10000
#define ISR_TX_EARLY	0x40000
#define ISR_RXF_OV	4
#define ISR_RXS_OV	0x20
#define ISR_HOST_RXD_OV	0x100
#define ISR_RS_UPDATE	0x20000

#define ISR_TX_EVENT (ISR_TXF_UR | ISR_TXS_OV | ISR_HOST_TXD_UR |\
	ISR_TS_UPDATE | ISR_TX_EARLY)
#define ISR_RX_EVENT (ISR_RXF_OV | ISR_RXS_OV | ISR_HOST_RXD_OV |\
	 ISR_RS_UPDATE)

#define IMR_NORMAL_MASK		(\
	ISR_MANUAL		|\
	ISR_DMAR_TO_RST		|\
	ISR_DMAW_TO_RST		|\
	ISR_PHY			|\
	ISR_PHY_LINKDOWN	|\
	ISR_TS_UPDATE		|\
	ISR_RS_UPDATE)

#define CMD_IO_SPACE		0x0001
#define CMD_MEMORY_SPACE		0x0002
#define CMD_BUS_MASTER		0x0004
#define PCI_REG_COMMAND		0x04

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
    uint32_t isr;
    uint32_t imr;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t regs[0x2000 / 4];

    /* DMA Context */
    uint32_t desc_base_addr_hi;
    uint32_t txd_base_addr_lo;
    uint32_t txs_base_addr_lo;
    uint32_t rxd_base_addr_lo;
    uint32_t txd_mem_size;
    uint32_t txs_mem_size;
    uint32_t rxd_buf_num;

    uint8_t mac_addr[6];
};

struct rx_pkt_status {
    unsigned pkt_size:11;
    unsigned:5;
    unsigned ok:1;
    unsigned bcast:1;
    unsigned mcast:1;
    unsigned pause:1;
    unsigned ctrl:1;
    unsigned crc:1;
    unsigned code:1;
    unsigned runt:1;
    unsigned frag:1;
    unsigned trunc:1;
    unsigned align:1;
    unsigned vlan:1;
    unsigned:3;
    unsigned update:1;
    unsigned short vtag;
    unsigned:16;
};

struct rx_desc {
    struct rx_pkt_status status;
    unsigned char packet[1536-sizeof(struct rx_pkt_status)];
};

struct tx_pkt_status {
    unsigned pkt_size:11;
    unsigned:5;
    unsigned ok:1;
    unsigned bcast:1;
    unsigned mcast:1;
    unsigned pause:1;
    unsigned ctrl:1;
    unsigned defer:1;
    unsigned exc_defer:1;
    unsigned single_col:1;
    unsigned multi_col:1;
    unsigned late_col:1;
    unsigned abort_col:1;
    unsigned underrun:1;
    unsigned:3;
    unsigned update:1;
};

struct tx_pkt_header {
    unsigned pkt_size:11;
    unsigned:4;
    unsigned ins_vlan:1;
    unsigned short vlan;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool raise = (s->regs[REG_ISR / 4] & s->regs[REG_IMR / 4]) != 0;
    
    if (raise) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint64_t txd_base = ((uint64_t)s->regs[REG_DESC_BASE_ADDR_HI / 4] << 32) | s->regs[REG_TXD_BASE_ADDR_LO / 4];
    
    if (txd_base != 0) {
        struct tx_pkt_header txph;
        pci_dma_read(pdev, txd_base, &txph, sizeof(txph));
        s->regs[REG_ISR / 4] |= ISR_TS_UPDATE; /* Dummy TX event bit */
        pcibase_update_irq(s);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t index = addr / 4;
    uint32_t val = 0;

    if (index < (0x2000 / 4)) {
        val = s->regs[index];
    }

    if (size == 4) {
        return val;
    } else if (size == 2) {
        return (addr % 4 == 0) ? (val & 0xFFFF) : (val >> 16);
    } else if (size == 1) {
        uint32_t shift = (addr % 4) * 8;
        return (val >> shift) & 0xFF;
    }

    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t index = addr / 4;
    uint32_t new_val;

    if (index >= (0x2000 / 4)) {
        return;
    }

    new_val = s->regs[index];

    if (size == 4) {
        new_val = val;
    } else if (size == 2) {
        if (addr % 4 == 0) {
            new_val = (new_val & 0xFFFF0000) | (val & 0xFFFF);
        } else {
            new_val = (new_val & 0x0000FFFF) | ((val & 0xFFFF) << 16);
        }
    } else if (size == 1) {
        uint32_t shift = (addr % 4) * 8;
        new_val = (new_val & ~(0xFF << shift)) | ((val & 0xFF) << shift);
    }

    switch (addr & ~3) {
        case REG_ISR:
            s->regs[index] &= ~new_val;
            pcibase_update_irq(s);
            break;
        case REG_IMR:
            s->regs[index] = new_val;
            pcibase_update_irq(s);
            break;
        case REG_MASTER_CTRL:
            s->regs[index] = new_val;
            if (new_val & MASTER_CTRL_SOFT_RST) {
                s->regs[REG_IDLE_STATUS / 4] = 0;
            }
            break;
        case REG_MDIO_CTRL:
            s->regs[index] = new_val & ~MDIO_START & ~MDIO_BUSY;
            break;
        case REG_SPI_FLASH_CTRL:
            s->regs[index] = new_val & ~SPI_FLASH_CTRL_START;
            break;
        case REG_VPD_CAP:
            s->regs[index] = new_val | VPD_CAP_VPD_FLAG;
            break;
        case REG_MB_TXD_WR_IDX:
            s->regs[index] = new_val;
            pcibase_do_dma(s, false);
            break;
        default:
            s->regs[index] = new_val;
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

    memset(s->regs, 0, sizeof(s->regs));
    
    s->regs[REG_MAC_STA_ADDR / 4] = 0x56341200;
    s->regs[(REG_MAC_STA_ADDR + 4) / 4] = 0x00009A78;
    s->regs[REG_PCIE_CAP_LIST / 4] = 0;
    s->regs[REG_IDLE_STATUS / 4] = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1969 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x2048 );
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
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x2000,
        .name = "atl2-mmio"
    };
      
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
    .name = "atl2_pci",
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
