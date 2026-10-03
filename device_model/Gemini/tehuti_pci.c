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

#define TYPE_PCIBASE_DEVICE "tehuti_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#ifndef PCI_VENDOR_ID_TEHUTI
#define PCI_VENDOR_ID_TEHUTI 0x1fc9
#endif

#define BDX_DEVICE_ID 0x3009
#define BDX_REGS_SIZE 0x8000

#define SROM_VER       0x5020
#define FPGA_VER       0x5030
#define FW_VER         0x5010
#define FPGA_SEED      0x5040
#define MAC_LINK_STAT  0x4
#define PCI_LINK_STATUS_REG 0x92
#define PCI_DEV_CTRL_REG 0x88
#define TX_RX_CFG0_BASE 0xfffff000
#define TXF_WPTR_WR_PTR 0x7ff8

#define IR_PCIE_TOUT 0x00000001
#define IR_PCIE_LINK 0x00000002
#define IR_TMR0      0x00000040
#define IR_PSE       0x00000400
#define IR_RX_DESC_0 0x00000800
#define IR_TX_FREE_0 0x00008000
#define IR_RX_FREE_0 0x00080000
#define IR_LNKCHG0   0x08000000
#define IR_EXTRA (IR_RX_FREE_0 | IR_LNKCHG0 | IR_PSE | IR_TMR0 | IR_PCIE_LINK | IR_PCIE_TOUT)
#define IR_RUN (IR_EXTRA | IR_RX_DESC_0 | IR_TX_FREE_0)

#define regIMR0          0x5110
#define regIMR regIMR0

#define regMAC_LNK_STAT  0x0200
#define regTXF_WPTR_0   0x40B0
#define regRXD_WPTR_0   0x40A0
#define regINIT_SEMAPHORE 0x5170
#define regINIT_STATUS    0x5180
#define regVPC                  0x2300
#define regVIC                  0x2320
#define regUNC_MAC0_A   0x1250
#define regUNC_MAC1_A   0x1260
#define regUNC_MAC2_A   0x1270
#define regFRM_LENGTH      0x6014
#define regPAUSE_QUANT     0x6018
#define regRX_FIFO_SECTION 0x601C
#define regTX_FIFO_SECTION 0x6020
#define regRX_FULLNESS     0x6024
#define regTX_FULLNESS     0x6028
#define regCTRLST          0x6008
#define regVGLB                 0x2340
#define regMAX_FRAME_A  0x12C0
#define regRDINTCM0      0x5120
#define regRDINTCM2      0x5128
#define regTDINTCM0      0x5130
#define regGMAC_RXF_A   0x1240
#define regCLKPLL               0x5000
#define regRXD_CFG0_0   0x4060
#define regRXD_CFG1_0   0x4020
#define regRXD_RPTR_0   0x40E0
#define regRXF_CFG0_0   0x4050
#define regRXF_CFG1_0   0x4010
#define regRXF_RPTR_0   0x40D0
#define regRXF_WPTR_0   0x4090
#define regTXD_CFG0_0   0x4040
#define regTXD_CFG1_0   0x4000
#define regTXD_RPTR_0   0x40C0
#define regTXD_WPTR_0   0x4080
#define regTXF_CFG0_0   0x4070
#define regTXF_CFG1_0   0x4030
#define regTXF_RPTR_0   0x40F0
#define regTXF_WPTR_0   0x40B0
#define regTXF_RPTR_3   0x40FC
#define regDIS_PORT        0x7010
#define regDIS_QU          0x7030
#define regRST_PORT        0x7000
#define regRST_QU          0x7020
#define regVLAN_0       0x1800
#define regRX_MCST_HASH0   0x1A00
#define regRX_MAC_MCST0    0x1A80
#define regRX_MAC_MCST1    0x1A84
#define CLKPLL_SFTRST          0x0001

#define regCTRLST_RX_ENA   0x0002
#define regCTRLST_TX_ENA   0x0001
#define MAX_FRAME_AB_VAL       0x3fff

#define GMAC_RX_FILTER_OSEN  0x1000
#define GMAC_RX_FILTER_AM    0x0008
#define GMAC_RX_FILTER_AB    0x0004
#define GMAC_RX_FILTER_PRM   0x0001

#define TXF_WPTR_MASK 0x7ff0
#define BDX_TXF_DESC_SZ    16

#define regISR0          0x5100
#define CLKPLL_PLLLKD          0x0200
#define CLKPLL_RSTEND          0x0100
#define regCTRLST_PAD_ENA  0x0020
#define regCTRLST_PRM_ENA  0x0010

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

    uint32_t regs[BDX_REGS_SIZE / 4];

    uint32_t rxd_fifo0_base;
    uint32_t rxf_fifo0_base;
    uint32_t txd_fifo0_base;
    uint32_t txf_fifo0_base;

    uint32_t mac_link_stat;
};

struct rxf_desc {
    uint32_t info;
    uint32_t va_lo;
    uint32_t va_hi;
    uint32_t pa_lo;
    uint32_t pa_hi;
    uint32_t len;
};

struct rxd_desc {
    uint32_t rxd_val1;
    uint16_t len;
    uint16_t rxd_vlan;
    uint32_t va_lo;
    uint32_t va_hi;
};

struct pbl {
    uint32_t pa_lo;
    uint32_t pa_hi;
    uint32_t len;
};

struct txd_desc {
    uint32_t txd_val1;
    uint16_t mss;
    uint16_t length;
    uint32_t va_lo;
    uint32_t va_hi;
    struct pbl pbl[0];
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->intr_status & s->intr_mask) != 0;
    
    if (s->has_msi && msi_enabled(pdev)) {
        if (level) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, level);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < BDX_REGS_SIZE) {
        val = s->regs[addr / 4];
    }
    
    if (addr == regISR0) {
        val = s->intr_status;
    } else if (addr == regCLKPLL) {
        val |= CLKPLL_PLLLKD | CLKPLL_RSTEND;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < BDX_REGS_SIZE) {
        s->regs[addr / 4] = val;
        if (addr == regIMR0) {
            s->intr_mask = val;
            pcibase_update_irq(s);
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

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[SROM_VER / 4] = 0x1234;
    s->regs[FPGA_VER / 4] = 378;
    s->regs[FW_VER / 4] = 0x1000;
    s->regs[FPGA_SEED / 4] = 0x1;
    s->intr_status = 0;
    s->intr_mask = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_TEHUTI );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x3009 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
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
    s->bar_info[0].size = BDX_REGS_SIZE;
    s->bar_info[0].name = "tehuti-mmio";  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = true;
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        s->has_msi = false;
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
    .name = "tehuti_pci",
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
