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

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_TEHUTI     0x1FC9
#define TEHUTI_DEVICE_ID         0x3009
#define TEHUTI_PCI_CLASS         0x0200
#define BDX_REGS_SIZE            0x1000
#define FPGA_VER_OFFSET          0x5030
#define FW_VER_OFFSET            0x5010
#define SROM_VER_OFFSET          0x5020
#define FPGA_SEED_OFFSET         0x5040
#define MAC_LINK_STAT_OFFSET     0x4

/* Supplementary driver register offsets */
#define regRXF_CFG0_0            0x4050
#define regRXD_CFG0_0            0x4060
#define regTXD_CFG0_0            0x4040
#define regTXF_CFG0_0            0x4070
#define regRXF_RPTR_0            0x40D0
#define regRXD_WPTR_0            0x40A0
#define regTXF_RPTR_0            0x40F0
#define regTXD_WPTR_0            0x4080
#define regIMR0                  0x5110
#define regISR0                  0x5100

/* Newly provided driver register definitions */
#define regCLKPLL               0x5000
#define CLKPLL_SFTRST           0x0001
#define CLKPLL_PLLLKD           0x0200
#define CLKPLL_RSTEND           0x0100
#define CLKPLL_LKD             (CLKPLL_PLLLKD|CLKPLL_RSTEND)
#define regINIT_SEMAPHORE       0x5170
#define regINIT_STATUS          0x5180
#define regUNC_MAC0_A           0x1250
#define regUNC_MAC1_A           0x1260
#define regUNC_MAC2_A           0x1270

#define IR_RUN                   (IR_EXTRA | IR_RX_DESC_0 | IR_TX_FREE_0)
#define IR_EXTRA                 (IR_RX_FREE_0 | IR_LNKCHG0 | IR_PSE | IR_TMR0 | IR_PCIE_LINK | IR_PCIE_TOUT)
#define IR_RX_DESC_0             0x00000800
#define IR_TX_FREE_0             0x00008000
#define IR_RX_FREE_0             0x00080000
#define IR_LNKCHG0               0x08000000
#define IR_PSE                   0x00000400
#define IR_TMR0                  0x00000040
#define IR_PCIE_LINK             0x00000002
#define IR_PCIE_TOUT             0x00000001
#define TX_RX_CFG0_BASE          0xfffff000

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

    /* Hardware Register Shadows */
    uint32_t fpga_ver_val;
    uint32_t fw_ver_val;
    uint32_t srom_ver_val;
    uint32_t fpga_seed_val;
    uint32_t imr;
    uint32_t isr;

    /* DMA Context */
    dma_addr_t rx_dma_base;
    dma_addr_t tx_dma_base;

    /* Operational status flags */
    bool link_up;

    /* State used to handle reset sequences */
    bool reset_done;

    /* Power management state (D0-D3) */
    uint32_t pm_state;

    /* Additional registers for emulation */
    uint32_t clkpll;
    int pll_read_countdown;
    uint32_t init_semaphore;
    uint32_t init_status;
    uint32_t mac_link_stat;
    uint32_t mac_addr[3];
    uint32_t fifo_cfg0[4];  /* RXF, RXD, TXD, TXF */
    uint32_t fifo_cfg1[4];
    uint32_t fifo_rptr[4];
    uint32_t fifo_wptr[4];
};

/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->isr & s->imr & IR_RUN) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case SROM_VER_OFFSET:
        val = s->srom_ver_val;
        break;
    case FPGA_VER_OFFSET:
        val = s->fpga_ver_val;
        break;
    case FW_VER_OFFSET:
        val = s->fw_ver_val;
        break;
    case FPGA_SEED_OFFSET:
        val = s->fpga_seed_val;
        break;
    case MAC_LINK_STAT_OFFSET:
        val = s->mac_link_stat;
        break;
    case regISR0:
        val = s->isr;
        s->isr = 0; /* read clears */
        break;
    case regIMR0:
        val = s->imr;
        break;
    /* FIFO configuration registers */
    case regRXF_CFG0_0:
        val = s->fifo_cfg0[0];
        break;
    case regRXD_CFG0_0:
        val = s->fifo_cfg0[1];
        break;
    case regTXD_CFG0_0:
        val = s->fifo_cfg0[2];
        break;
    case regTXF_CFG0_0:
        val = s->fifo_cfg0[3];
        break;
    /* FIFO pointer registers */
    case regRXF_RPTR_0:
        val = s->fifo_rptr[0];
        break;
    case regRXD_WPTR_0:
        val = s->fifo_wptr[1];
        break;
    case regTXF_RPTR_0:
        val = s->fifo_rptr[3];
        break;
    case regTXD_WPTR_0:
        val = s->fifo_wptr[2];
        break;
    /* MAC registers */
    case regUNC_MAC0_A:
        val = s->mac_addr[0];
        break;
    case regUNC_MAC1_A:
        val = s->mac_addr[1];
        break;
    case regUNC_MAC2_A:
        val = s->mac_addr[2];
        break;
    /* PLL */
    case regCLKPLL:
        if (s->pll_read_countdown > 0) {
            s->pll_read_countdown--;
            if (s->pll_read_countdown == 0) {
                s->clkpll |= CLKPLL_LKD;
            }
        }
        val = s->clkpll;
        break;
    /* Firmware init */
    case regINIT_SEMAPHORE:
        val = s->init_semaphore;
        break;
    case regINIT_STATUS:
        val = s->init_status;
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
    case FPGA_VER_OFFSET:
        s->fpga_ver_val = val;
        break;
    case FW_VER_OFFSET:
        s->fw_ver_val = val;
        break;
    case SROM_VER_OFFSET:
        s->srom_ver_val = val;
        break;
    case FPGA_SEED_OFFSET:
        s->fpga_seed_val = val;
        break;
    case MAC_LINK_STAT_OFFSET:
        s->mac_link_stat = val;
        break;
    case regISR0:
        s->isr = val; /* may be write-to-clear? */
        break;
    case regIMR0:
        s->imr = val;
        break;
    /* FIFO configuration registers */
    case regRXF_CFG0_0:
        s->fifo_cfg0[0] = val;
        break;
    case regRXD_CFG0_0:
        s->fifo_cfg0[1] = val;
        break;
    case regTXD_CFG0_0:
        s->fifo_cfg0[2] = val;
        break;
    case regTXF_CFG0_0:
        s->fifo_cfg0[3] = val;
        break;
    /* FIFO pointer registers */
    case regRXF_RPTR_0:
        s->fifo_rptr[0] = val;
        break;
    case regRXD_WPTR_0:
        s->fifo_wptr[1] = val;
        break;
    case regTXF_RPTR_0:
        s->fifo_rptr[3] = val;
        break;
    case regTXD_WPTR_0:
        s->fifo_wptr[2] = val;
        break;
    /* MAC registers */
    case regUNC_MAC0_A:
        s->mac_addr[0] = val;
        break;
    case regUNC_MAC1_A:
        s->mac_addr[1] = val;
        break;
    case regUNC_MAC2_A:
        s->mac_addr[2] = val;
        break;
    /* PLL */
    case regCLKPLL:
        if (val & CLKPLL_SFTRST) {
            s->pll_read_countdown = 0;
            s->clkpll = val | 0x8; /* simulate +0x8? */
        } else {
            if (s->clkpll & CLKPLL_SFTRST) {
                /* reset cleared, start countdown to lock */
                s->pll_read_countdown = 5;
            }
            s->clkpll = val & ~CLKPLL_SFTRST;
        }
        break;
    /* Firmware init */
    case regINIT_SEMAPHORE:
        s->init_semaphore = val;
        break;
    case regINIT_STATUS:
        s->init_status = val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    /* PIO read logic not used */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    /* PIO write logic not used */
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

    s->fpga_ver_val = 0x0100017A; /* version >= 378 for MSI */
    s->fw_ver_val = 0;
    s->srom_ver_val = 0;
    s->fpga_seed_val = 0;
    s->imr = 0;
    s->isr = 0;
    s->link_up = false;
    s->reset_done = true;
    s->pm_state = 0;
    s->clkpll = 0;
    s->pll_read_countdown = 0;
    s->init_semaphore = 0;
    s->init_status = 1; /* firmware already loaded */
    s->mac_link_stat = 0;
    memset(s->mac_addr, 0, sizeof(s->mac_addr));
    memset(s->fifo_cfg0, 0, sizeof(s->fifo_cfg0));
    memset(s->fifo_cfg1, 0, sizeof(s->fifo_cfg1));
    memset(s->fifo_rptr, 0, sizeof(s->fifo_rptr));
    memset(s->fifo_wptr, 0, sizeof(s->fifo_wptr));
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_TEHUTI);
    pci_set_word(pci_conf + PCI_DEVICE_ID, TEHUTI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, TEHUTI_PCI_CLASS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization: size sufficient for all registers (0x10000) */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x10000,
        .name = "tehuti-mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI initialization */
    msi_init(pdev, 0, 1, true, false, errp);

    s->fpga_ver_val = 0x0100017A;
    s->fw_ver_val = 0;
    s->srom_ver_val = 0;
    s->fpga_seed_val = 0;
    s->imr = 0;
    s->isr = 0;
    s->link_up = false;
    s->pm_state = 0;
    s->clkpll = 0;
    s->pll_read_countdown = 0;
    s->init_semaphore = 0;
    s->init_status = 1;
    s->mac_link_stat = 0;
    memset(s->mac_addr, 0, sizeof(s->mac_addr));
    memset(s->fifo_cfg0, 0, sizeof(s->fifo_cfg0));
    memset(s->fifo_cfg1, 0, sizeof(s->fifo_cfg1));
    memset(s->fifo_rptr, 0, sizeof(s->fifo_rptr));
    memset(s->fifo_wptr, 0, sizeof(s->fifo_wptr));
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
