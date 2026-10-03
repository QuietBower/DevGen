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

#define TYPE_PCIBASE_DEVICE "tn40xx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_TEHUTI 0x1FC9
#define TN40_DEVICE_ID 0x4022
#define TN40_CLASS_ID PCI_CLASS_NETWORK_ETHERNET

#define TN40_REGS_SIZE 0x10000
#define REGS_ARRAY_SIZE (TN40_REGS_SIZE / sizeof(uint32_t))

/* Register offsets */
#define TN40_REG_CTRLST            0x6008
#define TN40_REG_MAC_LNK_STAT      0x0200
#define TN40_MAC_LINK_STAT         0x0004
#define TN40_IR_TMR0               0x00000040
#define TN40_IR_LNKCHG0            0x08000000
#define TN40_IR_LNKCHG1            0x10000000
#define TN40_IR_TX_FREE_0          0x00008000
#define TN40_REG_ISR_MSK0          0x5140
#define TN40_IR_EXTRA              (TN40_IR_RX_FREE_0 | TN40_IR_LNKCHG0 | TN40_IR_LNKCHG1 | TN40_IR_PSE | TN40_IR_TMR0 | TN40_IR_PCIE_LINK | TN40_IR_PCIE_TOUT)
#define TN40_IR_RX_DESC_0          0x00000800
#define TN40_IR_TMR1               0x00000080
#define TN40_REG_INIT_STATUS       0x5180
#define TN40_REG_VPC               0x2300
#define TN40_REG_INIT_SEMAPHORE    0x5170
#define TN40_REG_VIC               0x2320
#define TN40_REG_MAC_ADDR_0        0x600C
#define TN40_REG_MAC_ADDR_1        0x6010
#define TN40_REG_UNC_MAC0_A        0x1250
#define TN40_REG_UNC_MAC2_A        0x1270
#define TN40_REG_UNC_MAC1_A        0x1260
#define TN40_REG_RX_FIFO_SECTION   0x601C
#define TN40_MAX_FRAME_AB_VAL      0x3fff
#define TN40_REG_GMAC_RXF_A        0x1240
#define TN40_REG_RDINTCM2          0x5128
#define TN40_REG_TX_FULLNESS       0x6028
#define TN40_REG_PAUSE_QUANT       0x6054
#define TN40_GMAC_RX_FILTER_AB     0x0004
#define TN40_GMAC_RX_FILTER_AM     0x0008
#define TN40_REG_RDINTCM0          0x5120
#define TN40_REG_TDINTCM0          0x5130
#define TN40_REG_FRM_LENGTH        0x6014
#define TN40_REG_TX_FIFO_SECTION   0x6020
#define TN40_GMAC_RX_FILTER_TXFC   0x0400
#define TN40_GMAC_RX_FILTER_OSEN   0x1000
#define TN40_REG_VGLB              0x2340
#define TN40_REG_RX_FULLNESS       0x6024
#define TN40_REG_MAX_FRAME_A       0x12C0
#define TN40_REG_CLKPLL            0x5000
#define TN40_CLKPLL_SFTRST         0x0001
#define TN40_CLKPLL_LKD            (TN40_CLKPLL_PLLLKD | TN40_CLKPLL_RSTEND)
#define TN40_REG_TXF_RPTR_3        0x40FC
#define TN40_REG_DIS_PORT          0x7010
#define TN40_REG_RST_PORT          0x7000
#define TN40_REG_DIS_QU            0x7030
#define TN40_REG_RST_QU            0x7020
#define TN40_REG_VLAN_0            0x1800
#define TN40_GMAC_RX_FILTER_PRM    0x0001
#define TN40_REG_RX_MCST_HASH0     0x1A00
#define TN40_REG_RX_MAC_MCST0      0x1A80
#define TN40_MAC_MCST_NUM          15
#define TN40_MAC_MCST_HASH_NUM     8
#define TN40_REG_RX_MAC_MCST1      0x1A84
#define TN40_REG_MDIO_CMD_STAT     0x6030
#define TN40_MAX_MTU               BIT(14)
#define TN40_IR_RX_FREE_0          0x00080000
#define TN40_IR_PSE                0x00000400
#define TN40_FPGA_VER              0x5030
#define PCI_DEVICE_ID_TEHUTI_TN9510 0x4025
#define TN40_REG_IMR0              0x5110
#define TN40_IR_PCIE_TOUT          0x00000001
#define TN40_IR_PCIE_LINK          0x00000002
#define TN40_CLKPLL_PLLLKD         0x0200
#define TN40_CLKPLL_RSTEND         0x0100
#define TN40_REG_ISR0              0x5100
#define TN40_MDIO_SPEED_6MHZ       (6)
#define TN40_MDIO_SPEED_1MHZ       (1)
#define TN40_REG_MDIO_ADDR         0x603C
#define TN40_REG_MDIO_DATA         0x6038
#define TN40_REG_MDIO_CMD          0x6034

#define TN40_REG_RXF_RPTR_0        0x40D0
#define TN40_REG_RXD_WPTR_0        0x40A0
#define TN40_REG_RXD_RPTR_0        0x40E0
#define TN40_REG_RXF_CFG0_0        0x4050
#define TN40_REG_RXD_CFG0_0        0x4060
#define TN40_REG_RXF_CFG1_0        0x4010
#define TN40_REG_RXD_CFG1_0        0x4020
#define TN40_REG_RXF_WPTR_0        0x4090
#define TN40_REG_TXF_CFG1_0        0x4030
#define TN40_REG_TXD_CFG0_0        0x4040
#define TN40_REG_TXD_WPTR_0        0x4080
#define TN40_REG_TXF_CFG0_0        0x4070
#define TN40_REG_TXF_RPTR_0        0x40F0
#define TN40_REG_TXF_WPTR_0        0x40B0
#define TN40_REG_TXD_CFG1_0        0x4000
#define TN40_REG_TXD_RPTR_0        0x40C0


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
    uint32_t intr_status;       /* ISR value */
    uint32_t intr_mask;         /* IMR value */

    /* Hardware Register Shadows */
    uint32_t regs[REGS_ARRAY_SIZE];

    /* Timer for firmware loading simulation */
    QEMUTimer fw_timer;
};

/* Forward declaration */
static void pcibase_fw_timer(void *opaque);

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

}

/* MMIO Read Handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    unsigned idx = addr >> 2;

    if (idx >= REGS_ARRAY_SIZE) {
        return 0xFFFFFFFF;
    }
    if (size != 4) {
        return 0;
    }
    switch (addr) {
    case 0x5030: /* TN40_FPGA_VER */
        return 0x134; /* 308 */
    case 0x1014: /* ETHSD.INIT_STAT */
        if (s->regs[idx] == 0x43) {
            return 0x43 | (1 << 9);
        }
        return s->regs[idx];
    default:
        return s->regs[idx];
    }
}

/* MMIO Write Handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    unsigned idx = addr >> 2;

    if (idx >= REGS_ARRAY_SIZE) {
        return;
    }
    if (size != 4) {
        return;
    }
    switch (addr) {
    case 0x5000: /* TN40_REG_CLKPLL */
        s->regs[idx] = val;
        if ((val & TN40_CLKPLL_SFTRST) == 0) {
            s->regs[idx] |= TN40_CLKPLL_LKD;
        }
        break;
    case 0x4080: /* TN40_REG_TXD_WPTR_0 */
        s->regs[idx] = val;
        timer_del(&s->fw_timer);
        timer_mod(&s->fw_timer, qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) + 1000);
        break;
    case 0x1014: /* ETHSD.INIT_STAT */
        s->regs[idx] = val;
        break;
    default:
        s->regs[idx] = val;
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
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
    /* Set default MAC address (00:11:22:33:44:55) */
    s->regs[0x1250 >> 2] = 0x4455; /* MAC0_A */
    s->regs[0x1260 >> 2] = 0x2233; /* MAC1_A */
    s->regs[0x1270 >> 2] = 0x0011; /* MAC2_A */
    /* Allow firmware loading from the start */
    s->regs[0x5170 >> 2] = 1; /* INIT_SEMAPHORE */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_TEHUTI );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  TN40_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, TN40_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Enable MSI support */
    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        return;
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = TN40_REGS_SIZE,
        .name = "tn40xx-mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize firmware timer */
    timer_init_us(&s->fw_timer, QEMU_CLOCK_VIRTUAL, pcibase_fw_timer, s);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    timer_del(&s->fw_timer);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "tn40xx_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_fw_timer(void *opaque)
{
    /* Firmware loading simulation placeholder. 
     * This should be implemented based on the driver's firmware loading sequence.
     */
    PCIBaseState *s = opaque;
    /* For now, do nothing; real behavior must be added once driver src is provided. */
}

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