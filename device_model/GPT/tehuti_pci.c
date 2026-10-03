/*
 * Tehuti Networks PCI device emulation for QEMU 8.2.10
 * Functional model generated from linux/drivers/net/ethernet/tehuti/tehuti.c
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

#define BDX_REGS_SIZE   0x1000
#define BDX_NIC_NAME    "Tehuti 10 Giga TOE SmartNIC"
#define BDX_DRV_NAME    "tehuti"
#define BDX_DRV_VERSION "7.29.3"

#define BDX_DEVICE_ID_0 0x3009

#define IR_PCIE_TOUT 0x00000001
#define IR_PCIE_LINK 0x00000002
#define IR_TMR0      0x00000040
#define IR_PSE       0x00000400
#define IR_RX_DESC_0 0x00000800
#define IR_TX_FREE_0 0x00008000
#define IR_RX_FREE_0 0x00080000
#define IR_LNKCHG0   0x08000000

#define IR_EXTRA (IR_RX_FREE_0 | IR_LNKCHG0 | IR_PSE | \
    IR_TMR0 | IR_PCIE_LINK | IR_PCIE_TOUT)
#define IR_RUN (IR_EXTRA | IR_RX_DESC_0 | IR_TX_FREE_0)

#define FW_VER    0x5010
#define SROM_VER  0x5020
#define FPGA_VER  0x5030
#define FPGA_SEED 0x5040

#define PCI_DEV_CTRL_REG     0x88
#define PCI_LINK_STATUS_REG  0x92

#define MAC_LINK_STAT        0x4

#define GMAC_RX_FILTER_PRM   0x0001
#define GMAC_RX_FILTER_AM    0x0008
#define GMAC_RX_FILTER_AB    0x0004
#define GMAC_RX_FILTER_OSEN  0x1000

#define MAX_FRAME_AB_VAL     0x3fff

#define CLKPLL_SFTRST        0x0001
#define CLKPLL_RSTEND        0x0100
#define CLKPLL_PLLLKD        0x0200
#define CLKPLL_LKD           (CLKPLL_PLLLKD | CLKPLL_RSTEND)

#define TX_RX_CFG0_BASE      0xfffff000
#define FIFO_SIZE            4096
#define FIFO_EXTRA_SPACE     1024

#define TXF_WPTR_WR_PTR      0x7ff8
#define TXF_WPTR_MASK        0x7ff0
#define BDX_TXF_DESC_SZ      16

#define BDX_MAX_MTU          (16 * 1024)
#define BDX_NDEV_TXQ_LEN     3000

#define BDX_MAX_RX_DONE      150
#define BDX_MIN_TX_LEVEL     256
#define BDX_NO_UPD_PACKETS   40

#define MAC_MCST_NUM         15
#define MAC_MCST_HASH_NUM    8

#define BDX_COPYBREAK        257

#define LUXOR_MAX_PORT       2

#define TEHUTI_VENDOR_ID 0x1fc9
#define BDX_PCI_CLASS    0

#define BDX_PCI_VENDOR_ID  TEHUTI_VENDOR_ID
#define BDX_PCI_DEVICE_ID  BDX_DEVICE_ID_0
#define BDX_PCI_CLASS_ID   BDX_PCI_CLASS

/* New register and macro definitions from supplementary driver source */
#define regMAC_LNK_STAT   0x0200
#define regISR0           0x5100
#define regISR            regISR0
#define regTXF_WPTR_0     0x40B0
#define regRXD_WPTR_0     0x40A0
#define regINIT_SEMAPHORE 0x5170
#define regINIT_STATUS    0x5180
#define regVPC            0x2300
#define regVIC            0x2320
#define regUNC_MAC0_A     0x1250
#define regUNC_MAC1_A     0x1260
#define regUNC_MAC2_A     0x1270
#define regFRM_LENGTH     0x6014
#define regPAUSE_QUANT    0x6018
#define regRX_FIFO_SECTION 0x601C
#define regTX_FIFO_SECTION 0x6020
#define regRX_FULLNESS    0x6024
#define regTX_FULLNESS    0x6028
#define regCTRLST         0x6008
#define regCTRLST_RX_ENA  0x0002
#define regCTRLST_TX_ENA  0x0001
#define regVGLB           0x2340
#define regMAX_FRAME_A    0x12C0
#define regRDINTCM0       0x5120
#define regRDINTCM2       0x5128
#define regTDINTCM0       0x5130
#define regGMAC_RXF_A     0x1240
#define regCLKPLL         0x5000
#define regRXD_CFG0_0     0x4060
#define regDIS_PORT       0x7010
#define regDIS_QU         0x7030
#define regRST_PORT       0x7000
#define regTXD_WPTR_0     0x4080
#define regTXF_RPTR_3     0x40FC
#define regIMR0           0x5110
#define regIMR            regIMR0
#define regRST_QU         0x7020
#define regRXD_CFG1_0     0x4020
#define regRXD_RPTR_0     0x40E0
#define regRXF_CFG0_0     0x4050
#define regRXF_CFG1_0     0x4010
#define regRXF_RPTR_0     0x40D0
#define regRXF_WPTR_0     0x4090
#define regRX_MCST_HASH0  0x1A00
#define regRX_MAC_MCST0   0x1A80
#define regRX_MAC_MCST1   0x1A84
#define regVLAN_0         0x1800
#define regTXD_CFG0_0     0x4040
#define regTXD_CFG1_0     0x4000
#define regTXD_RPTR_0     0x40C0
#define regTXF_CFG0_0     0x4070
#define regTXF_CFG1_0     0x4030
#define regTXF_RPTR_0     0x40F0
#define regRXD_CFG0_0     0x4060

#define INT_REG_VAL(coal, coal_rc, rxf_th, pck_th) \
    ((coal) | ((coal_rc) << 15) | ((rxf_th) << 16) | ((pck_th) << 20))

#define INT_COAL_MULT 2
#define PCK_TH_MULT   128

#define BDX_TXF_DESC_SZ_DRV 16

#define BDX_DELAY_WPTR

/* Access helpers the driver uses as WRITE_REG/READ_REG macros */
#define WRITE_REG_PRIV(priv, reg, val) \
    pcibase_reg_writel((PCIBaseState *)((priv)->nic ? (priv)->nic->regs : NULL), (reg), (val))
#define READ_REG_PRIV(priv, reg) \
    pcibase_reg_readl((PCIBaseState *)((priv)->nic ? (priv)->nic->regs : NULL), (reg))


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

    struct {
        uint32_t fw_version;
        uint32_t srom_version;
        uint32_t fpga_version;
        uint32_t fpga_seed;
    } id_regs;

    MemoryRegion *mmio_region;

    uint32_t regs[BDX_REGS_SIZE / 4];

    uint16_t mac_words[3];
    bool link_up;
};

static inline uint32_t pcibase_reg_readl(PCIBaseState *s, hwaddr addr)
{
    if (!s) {
        return 0;
    }
    if (addr >= BDX_REGS_SIZE) {
        return 0;
    }
    return s->regs[addr >> 2];
}

static inline void pcibase_reg_writel(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    if (!s) {
        return;
    }
    if (addr >= BDX_REGS_SIZE) {
        return;
    }
    s->regs[addr >> 2] = val;
}

static bool pcibase_msi_enabled(PCIBaseState *s)
{
    return msi_enabled(PCI_DEVICE(s));
}

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    uint32_t pending = s->intr_status & s->intr_mask & IR_RUN;

    if (pending) {
        if (pcibase_msi_enabled(s)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!pcibase_msi_enabled(s)) {
            pci_set_irq(pdev, 0);
        }
    }
}

static void pcibase_clear_irq_bits(PCIBaseState *s, uint32_t bits)
{
    s->intr_status &= ~bits;
    pcibase_update_irq(s);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    if (size != 4) {
        return 0;
    }

    switch (addr) {
    case SROM_VER:
        val = s->id_regs.srom_version;
        break;
    case FPGA_VER:
        val = s->id_regs.fpga_version;
        break;
    case FPGA_SEED:
        val = s->id_regs.fpga_seed;
        break;
    case FW_VER:
        val = s->id_regs.fw_version;
        break;
    case regMAC_LNK_STAT:
    case MAC_LINK_STAT:
        /* Reflect link state as seen by the driver via regMAC_LNK_STAT */
        val = s->link_up ? 1 : 0;
        break;
    case regUNC_MAC0_A:
        val = s->mac_words[2];
        break;
    case regUNC_MAC1_A:
        val = s->mac_words[1];
        break;
    case regUNC_MAC2_A:
        val = s->mac_words[0];
        break;
    case regRDINTCM0:
        /* Return current cached RX interrupt coalescing configuration */
        val = pcibase_reg_readl(s, addr);
        break;
    case regTDINTCM0:
        /* Return current cached TX interrupt coalescing configuration */
        val = pcibase_reg_readl(s, addr);
        break;
    case regINIT_SEMAPHORE:
        /* Firmware loader checks this; default to master enabled */
        val = pcibase_reg_readl(s, addr);
        break;
    case regINIT_STATUS:
        /* Firmware loader polls this; default to 1 (firmware ready) */
        val = pcibase_reg_readl(s, addr);
        if (val == 0) {
            val = 1;
        }
        break;
    default:
        val = pcibase_reg_readl(s, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v = (uint32_t)val;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case regIMR:
        /* Interrupt mask register */
        s->intr_mask = v;
        pcibase_update_irq(s);
        pcibase_reg_writel(s, addr, v);
        break;
    case regISR:
        /* W1C interrupt status */
        pcibase_clear_irq_bits(s, v);
        pcibase_reg_writel(s, addr, s->intr_status);
        break;
    case regMAC_LNK_STAT:
    case MAC_LINK_STAT:
        /* Allow driver to toggle link state if it writes here */
        s->link_up = !!(v & 0x1);
        pcibase_reg_writel(s, addr, v);
        break;
    case regUNC_MAC0_A:
        s->mac_words[2] = (uint16_t)(v & 0xFFFF);
        pcibase_reg_writel(s, addr, v);
        break;
    case regUNC_MAC1_A:
        s->mac_words[1] = (uint16_t)(v & 0xFFFF);
        pcibase_reg_writel(s, addr, v);
        break;
    case regUNC_MAC2_A:
        s->mac_words[0] = (uint16_t)(v & 0xFFFF);
        pcibase_reg_writel(s, addr, v);
        break;
    case regRDINTCM0:
        /* Cache RX interrupt coalescing configuration written by driver */
        pcibase_reg_writel(s, addr, v);
        break;
    case regTDINTCM0:
        /* Cache TX interrupt coalescing configuration written by driver */
        pcibase_reg_writel(s, addr, v);
        break;
    case regINIT_SEMAPHORE:
        /* Firmware loader writes 1 on completion; just store value */
        pcibase_reg_writel(s, addr, v);
        break;
    case regINIT_STATUS:
        /* Driver never writes in provided snippet, but accept and store */
        pcibase_reg_writel(s, addr, v);
        break;
    case regFRM_LENGTH:
    case regPAUSE_QUANT:
    case regRX_FIFO_SECTION:
    case regTX_FIFO_SECTION:
    case regRX_FULLNESS:
    case regTX_FULLNESS:
    case regCTRLST:
    case regVGLB:
    case regMAX_FRAME_A:
    case regRDINTCM2:
    case regGMAC_RXF_A:
    case regRXD_CFG0_0:
    case regRXD_CFG1_0:
    case regRXD_RPTR_0:
    case regRXD_WPTR_0:
    case regRXF_CFG0_0:
    case regRXF_CFG1_0:
    case regRXF_RPTR_0:
    case regRXF_WPTR_0:
    case regTXD_CFG0_0:
    case regTXD_CFG1_0:
    case regTXD_RPTR_0:
    case regTXD_WPTR_0:
    case regTXF_CFG0_0:
    case regTXF_CFG1_0:
    case regTXF_RPTR_0:
        /* Configuration registers touched during init/start paths */
        pcibase_reg_writel(s, addr, v);
        break;
    default:
        pcibase_reg_writel(s, addr, v);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
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
    PCIDevice *pdev = PCI_DEVICE(dev);
    uint8_t *pci_conf = pdev->config;

    pci_device_reset(pdev);

    memset(s->regs, 0, sizeof(s->regs));

    s->id_regs.fw_version = 0x00000001;
    s->id_regs.srom_version = 0x00000001;
    s->id_regs.fpga_version = 0x0000017A;
    s->id_regs.fpga_seed = 0x00000001;

    s->intr_status = 0;
    s->intr_mask = IR_RUN;

    s->link_up = true;

    /* Initialize MAC registers with a deterministic address */
    s->mac_words[0] = 0x1234;
    s->mac_words[1] = 0x5678;
    s->mac_words[2] = 0x9abc;

    /* Program some defaults into internal register array that driver may read */
    pcibase_reg_writel(s, regMAC_LNK_STAT, s->link_up ? 1 : 0);
    pcibase_reg_writel(s, regCLKPLL, CLKPLL_LKD);

    /* Initialize firmware related registers for bdx_fw_load() behaviour */
    pcibase_reg_writel(s, regINIT_SEMAPHORE, 1);
    pcibase_reg_writel(s, regINIT_STATUS, 1);

    pci_set_word(pci_conf + PCI_DEV_CTRL_REG, 0);
    pci_set_word(pci_conf + PCI_LINK_STATUS_REG, 0x0010);

    pcibase_update_irq(s);
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
        s->mmio_region = mr;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  BDX_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  BDX_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, BDX_PCI_CLASS_ID );
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
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = BDX_REGS_SIZE;
    s->bar_info[0].name  = "tehuti-mmio";
    for (int i = 1; i < 6; ++i) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = "";
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = true;
    s->has_msix = false;
    if (s->has_msi) {
        if (msi_init(pdev, 0, 1, true, false, errp)) {
        }
    }

    memset(s->regs, 0, sizeof(s->regs));
    s->id_regs.fw_version = 0x00000001;
    s->id_regs.srom_version = 0x00000001;
    s->id_regs.fpga_version = 0x0000017A;
    s->id_regs.fpga_seed = 0x00000001;
    s->intr_status = 0;
    s->intr_mask = IR_RUN;
    s->link_up = true;

    s->mac_words[0] = 0x1234;
    s->mac_words[1] = 0x5678;
    s->mac_words[2] = 0x9abc;

    pcibase_reg_writel(s, regMAC_LNK_STAT, s->link_up ? 1 : 0);
    pcibase_reg_writel(s, regCLKPLL, CLKPLL_LKD);

    pcibase_reg_writel(s, regINIT_SEMAPHORE, 1);
    pcibase_reg_writel(s, regINIT_STATUS, 1);
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

    (void)s;
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
