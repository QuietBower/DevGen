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

#define TYPE_PCIBASE_DEVICE "dw_mmc_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define BIT(x) (1U << (x))
#define SYNOPSYS_DW_MCI_VENDOR_ID 0x700
#define SYNOPSYS_DW_MCI_DEVICE_ID 0x1107
#define PCI_CLASS_DEVICE 0x0805
#define BAR2_SIZE 0x1000

/* Register offsets */
#define SDMMC_CTRL           0x000
#define SDMMC_CMD            0x02c
#define SDMMC_STATUS         0x048
#define DATA_OFFSET          0x100
#define DATA_240A_OFFSET     0x200

/* Register bit definitions */
#define SDMMC_INT_CMD_DONE               BIT(2)
#define SDMMC_CTRL_INT_ENABLE            BIT(4)
#define SDMMC_CTRL_ALL_RESET_FLAGS       (SDMMC_CTRL_RESET | SDMMC_CTRL_FIFO_RESET | SDMMC_CTRL_DMA_RESET)
#define SDMMC_GET_HDATA_WIDTH(x)         (((x)>>7) & 0x7)
#define SDMMC_INT_TXDR                   BIT(4)
#define SDMMC_INT_RXDR                   BIT(5)
#define SDMMC_GET_VERID(x)               ((x) & 0xFFFF)
#define SDMMC_INT_DATA_OVER              BIT(3)
#define SDMMC_CTRL_DMA_RESET             BIT(2)
#define SDMMC_CTRL_RESET                 BIT(0)
#define SDMMC_CTRL_FIFO_RESET            BIT(1)
#define SDMMC_INT_CD                     BIT(0)
#define SDMMC_GET_ADDR_CONFIG(x)         (((x)>>27) & 0x1)
#define SDMMC_GET_TRANS_MODE(x)          (((x)>>16) & 0x3)
#define DMA_INTERFACE_IDMA               (0x0)
#define DMA_INTERFACE_GDMA               (0x2)
#define DMA_INTERFACE_DWDMA              (0x1)
#define SDMMC_INT_RCRC                   BIT(6)
#define SDMMC_INT_RTO                    BIT(8)
#define SDMMC_INT_HLE                    BIT(12)
#define SDMMC_INT_RESP_ERR              BIT(1)
#define SDMMC_INT_SBE                    BIT(13)
#define SDMMC_INT_EBE                    BIT(15)
#define SDMMC_INT_HTO                    BIT(10)
#define SDMMC_INT_DRTO                   BIT(9)
#define SDMMC_INT_DCRC                   BIT(7)
#define SDMMC_STATUS_BUSY                BIT(9)
#define SDMMC_UHS_18V                    BIT(0)
#define SDMMC_CTYPE_4BIT                 BIT(0)
#define SDMMC_CTYPE_8BIT                 BIT(16)
#define SDMMC_CTYPE_1BIT                 0
#define SDMMC_RST_HWACTIVE               0x1
#define SDMMC_CTRL_USE_IDMAC             BIT(25)
#define SDMMC_IDMAC_FB                   BIT(1)
#define SDMMC_IDMAC_ENABLE               BIT(7)
#define SDMMC_IDMAC_SWRESET              BIT(0)
#define SDMMC_IDMAC_INT_RI               BIT(1)
#define SDMMC_IDMAC_INT_TI               BIT(0)
#define SDMMC_IDMAC_INT_NI               BIT(8)
#define SDMMC_CMD_INIT                   BIT(15)
#define SDMMC_CMD_UPD_CLK                BIT(21)
#define SDMMC_CLKEN_LOW_PWR              BIT(16)
#define SDMMC_CMD_PRV_DAT_WAIT           BIT(13)
#define SDMMC_CMD_VOLT_SWITCH            BIT(28)
#define SDMMC_CLKEN_ENABLE               BIT(0)
#define SDMMC_IDMAC_INT_AI               BIT(9)
#define SDMMC_IDMAC_INT_FBE              BIT(2)
#define SDMMC_IDMAC_INT_DU               BIT(4)
#define SDMMC_IDMAC_INT_CES              BIT(5)
#define SDMMC_CMD_RESP_CRC               BIT(8)
#define SDMMC_CMD_DAT_WR                 BIT(10)
#define SDMMC_CMD_USE_HOLD_REG           BIT(29)
#define SDMMC_CMD_RESP_LONG              BIT(7)
#define SDMMC_CMD_STOP                   BIT(14)
#define SDMMC_CMD_RESP_EXP               BIT(6)
#define SDMMC_CMD_DAT_EXP                BIT(9)
#define SDMMC_CTRL_DMA_ENABLE            BIT(5)
#define SDMMC_CMD_START                  BIT(31)
#define SDMMC_CARD_RD_THR_EN             BIT(0)
#define SDMMC_CARD_WR_THR_EN             BIT(2)
#define DW_MMC_240A                      0x240a
#define DW_MMC_280A                      0x280a

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

    /* Hardware Register Shadows */
    uint32_t regs[128];

    /* DMA Context */
    uint32_t idmac_enable;
    uint32_t idmac_int_status;
    dma_addr_t idmac_desc_base;

    uint32_t status;
    bool in_reset;
    uint8_t power_state;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status & s->intr_mask) {
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

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint32_t regidx = addr >> 2;

    if (addr < 0x200) {
        if (addr == 0x100) {
            /* FIFO read: return 0 */
            val = 0;
        } else if (regidx < 128) {
            val = s->regs[regidx];
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t regidx = addr >> 2;
    uint32_t v32 = (uint32_t)val;

    switch (addr) {
    case 0x000: /* SDMMC_CTRL */
        /* Handle reset bits: clear them immediately */
        s->regs[0] = v32;
        if (v32 & SDMMC_CTRL_RESET) {
            s->regs[0] &= ~SDMMC_CTRL_RESET;
            s->idmac_enable = 0;
        }
        if (v32 & SDMMC_CTRL_FIFO_RESET) {
            s->regs[0] &= ~SDMMC_CTRL_FIFO_RESET;
        }
        if (v32 & SDMMC_CTRL_DMA_RESET) {
            s->regs[0] &= ~SDMMC_CTRL_DMA_RESET;
        }
        break;
    case 0x044: /* SDMMC_INTMASK */
        s->intr_mask = v32;
        pcibase_update_irq(s);
        break;
    case 0x048: /* SDMMC_RINTSTS */
        /* Write 1 to clear */
        s->intr_status &= ~v32;
        pcibase_update_irq(s);
        break;
    case 0x050: /* IDMAC_DBADR low */
        s->idmac_desc_base = (s->idmac_desc_base & ~0xFFFFFFFFULL) | v32;
        s->regs[0x050>>2] = v32;
        break;
    case 0x054: /* IDMAC_DBADR high */
        s->idmac_desc_base = (s->idmac_desc_base & 0xFFFFFFFFULL) | ((uint64_t)v32 << 32);
        s->regs[0x054>>2] = v32;
        break;
    case 0x058: /* IDMAC_IDSTS */
        /* Write 1 to clear */
        s->idmac_int_status &= ~v32;
        s->regs[0x058>>2] = s->idmac_int_status;
        break;
    case 0x05C: /* IDMAC_IDINTEN */
        s->regs[0x05C>>2] = v32;
        break;
    case 0x060: /* IDMAC_BMOD */
        s->regs[0x060>>2] = v32;
        if (v32 & SDMMC_IDMAC_SWRESET) {
            s->regs[0x060>>2] &= ~SDMMC_IDMAC_SWRESET;
            s->idmac_enable = 0;
        }
        if (v32 & SDMMC_IDMAC_ENABLE) {
            s->idmac_enable = 1;
        } else {
            s->idmac_enable = 0;
        }
        break;
    case 0x100: /* FIFO write */
        break;
    default:
        if (addr < 0x200 && regidx < 128) {
            s->regs[regidx] = v32;
        }
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->idmac_enable = 0;
    s->idmac_int_status = 0;
    s->idmac_desc_base = 0;
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
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, SYNOPSYS_DW_MCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, SYNOPSYS_DW_MCI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_DEVICE);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    msi_init(pdev, 0, 1, true, false, errp);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 2;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR2_SIZE;
    s->bar_info[0].name = "dw_mci_mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize hardware registers */
    s->regs[0x06C>>2] = 0x2002; /* VERID */
    s->regs[0x040>>2] = 0x40000000 | (DMA_INTERFACE_IDMA << 16) | (0x7 << 7); /* HCON */
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

static const VMStateDescription vmstate_pcibase = {
    .name = "dw_mmc_pci_pci",
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