/*
 * QEMU SoC model for Softlogic SOLO6x10 PCI device
 * Generated from Linux driver analysis.
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
#include "qemu/bitops.h"

#define TYPE_PCIBASE_DEVICE "solo6x10_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_SOFTLOGIC 0x9413
#define PCI_DEVICE_ID_SOLO6010 0x6010
#define CLASS_ID PCI_CLASS_OTHERS

/* Missing identifiers inferred from usage */
#define SOLO_CHIP_ID_MASK   0x0f
#define BAR0_SIZE           0x2000
#define SDRAM_SIZE          (32 * 1024 * 1024)  /* 32 MB */

#define SOLO_SYS_CFG			0x0000
#define SOLO_DMA_CTRL			0x0004
#define SOLO_DMA_CTRL1			0x0008
#define SOLO_SYS_VCLK			0x000C
#define SOLO_IRQ_STAT			0x0010
#define SOLO_IRQ_MASK			0x0014
#define SOLO_CHIP_OPTION		0x001C
#define SOLO_PLL_CONFIG			0x0020
#define SOLO_PCI_ERR			0x0070
#define SOLO_EEPROM_CTRL		0x0060
#define SOLO_P2M_CONFIG(n)		(0x0080 + ((n)*0x20))
#define SOLO_P2M_DES_ADR(n)		(0x0084 + ((n)*0x20))
#define SOLO_P2M_DESC_ID(n)		(0x0088 + ((n)*0x20))
#define SOLO_P2M_CONTROL(n)		(0x0090 + ((n)*0x20))
#define SOLO_P2M_EXT_CFG(n)		(0x0094 + ((n)*0x20))
#define SOLO_P2M_TAR_ADR(n)		(0x0098 + ((n)*0x20))
#define SOLO_P2M_EXT_ADR(n)		(0x009C + ((n)*0x20))
#define SOLO_VI_CH_SWITCH_0		0x0100
#define SOLO_VI_CH_SWITCH_1		0x0104
#define SOLO_VI_CH_SWITCH_2		0x0108
#define SOLO_VI_CH_ENA			0x010C
#define SOLO_VI_CH_FORMAT		0x0110
#define SOLO_VI_FMT_CFG			0x0114
#define SOLO_VI_PAGE_SW			0x0118
#define SOLO_VI_ACT_I_P			0x011C
#define SOLO_VI_ACT_I_S			0x0120
#define SOLO_VI_ACT_P			0x0124
#define SOLO_VI_PB_CONFIG		0x0130
#define SOLO_VI_PB_RANGE_HV		0x0134
#define SOLO_VI_PB_ACT_H		0x0138
#define SOLO_VI_PB_ACT_V		0x013C
#define SOLO_VI_WIN_CTRL0(ch)		(0x0180 + ((ch)*4))
#define SOLO_VI_WIN_CTRL1(ch)		(0x01C0 + ((ch)*4))
#define SOLO_VI_WIN_ON(ch)		(0x0200 + ((ch)*4))
#define SOLO_VI_WIN_SW			0x0240
#define SOLO_VI_MOT_ADR			0x0260
#define SOLO_VI_MOT_CTRL		0x0264
#define SOLO_VI_MOTION_BORDER		0x0270
#define SOLO_VI_MOTION_BAR		0x0274
#define SOLO_VO_FMT_ENC			0x0300
#define SOLO_VO_ACT_H			0x0304
#define SOLO_VO_ACT_V			0x0308
#define SOLO_VO_RANGE_HV			0x030C
#define SOLO_VO_DISP_CTRL		0x0310
#define SOLO_VO_DISP_ERASE		0x0314
#define SOLO_VO_ZOOM_CTRL		0x0318
#define SOLO_VO_FREEZE_CTRL		0x031C
#define SOLO_VO_BKG_COLOR		0x0320
#define SOLO_VO_BORDER_LINE_COLOR	0x0330
#define SOLO_VO_BORDER_FILL_COLOR	0x0334
#define SOLO_VO_BORDER_LINE_MASK	0x0338
#define SOLO_VO_BORDER_FILL_MASK	0x033C
#define SOLO_VO_BORDER_X(n)		(0x0340+((n)*4))
#define SOLO_VO_BORDER_Y(n)		(0x0354+((n)*4))
#define SOLO_VO_RECTANGLE_CTRL(n)	(0x0368+((n)*12))
#define SOLO_VO_RECTANGLE_START(n)	(0x036c+((n)*12))
#define SOLO_VO_RECTANGLE_STOP(n)	(0x0370+((n)*12))
#define SOLO_VO_CURSOR_POS		0x0380
#define SOLO_VO_CURSOR_CLR		0x0384
#define SOLO_VO_CURSOR_CLR2		0x0388
#define SOLO_VO_CURSOR_MASK(id)		(0x0390+((id)*4))
#define SOLO_CAP_BASE			0x0400
#define SOLO_CAP_BTW			0x0404
#define SOLO_DIM_SCALE1			0x0408
#define SOLO_DIM_SCALE2			0x040C
#define SOLO_DIM_SCALE3			0x0410
#define SOLO_DIM_SCALE4			0x0414
#define SOLO_DIM_SCALE5			0x0418
#define SOLO_DIM_PROG			0x041C
#define SOLO_CAP_CH_SCALE(ch)		(0x0440+((ch)*4))
#define SOLO_CAP_CH_COMP_ENA_E(ch)	(0x0480+((ch)*4))
#define SOLO_CAP_CH_INTV(ch)		(0x04C0+((ch)*4))
#define SOLO_CAP_CH_INTV_E(ch)		(0x0500+((ch)*4))
#define SOLO_VE_CFG0			0x0610
#define SOLO_VE_CFG1			0x0614
#define SOLO_VE_WMRK_POLY		0x061C
#define SOLO_VE_VMRK_INIT_KEY		0x0620
#define SOLO_VE_WMRK_STRL		0x0624
#define SOLO_VE_ENCRYP_POLY		0x0628
#define SOLO_VE_ENCRYP_INIT		0x062C
#define SOLO_VE_ATTR			0x0630
#define SOLO_VE_COMPT_MOT		0x0634
#define SOLO_VE_JPEG_QP_TBL		0x0670
#define SOLO_VE_JPEG_QP_CH_L		0x0674
#define SOLO_VE_JPEG_QP_CH_H		0x0678
#define SOLO_VE_JPEG_CFG		0x067C
#define SOLO_VE_JPEG_CTRL		0x0680
#define SOLO_VE_JPEG_CFG1		0x0688
#define SOLO_VE_WMRK_ENABLE		0x068C
#define SOLO_VE_OSD_CH			0x0690
#define SOLO_VE_OSD_BASE			0x0694
#define SOLO_VE_OSD_CLR			0x0698
#define SOLO_VE_OSD_OPT			0x069C
#define SOLO_VE_CH_INTL(ch)		(0x0700+((ch)*4))
#define SOLO_VE_CH_MOT(ch)		(0x0740+((ch)*4))
#define SOLO_VE_CH_QP(ch)		(0x0780+((ch)*4))
#define SOLO_VE_CH_QP_E(ch)		(0x07C0+((ch)*4))
#define SOLO_VE_CH_GOP(ch)		(0x0800+((ch)*4))
#define SOLO_VE_CH_GOP_E(ch)		(0x0840+((ch)*4))
#define SOLO_VE_CH_REF_BASE(ch)		(0x0880+((ch)*4))
#define SOLO_VE_CH_REF_BASE_E(ch)	(0x08C0+((ch)*4))
#define SOLO_GPIO_CONFIG_0		0x0B00
#define SOLO_GPIO_CONFIG_1		0x0B04
#define SOLO_GPIO_DATA_OUT		0x0B08
#define SOLO_GPIO_DATA_IN		0x0B0C
#define SOLO_IIC_CFG			0x0B20
#define SOLO_IIC_CTRL			0x0B24
#define SOLO_IIC_TXD			0x0B28
#define SOLO_IIC_RXD			0x0B2C
#define SOLO_TIMER_CLOCK_NUM		0x0BE0
#define SOLO_WATCHDOG			0x0BE4
#define SOLO_TIMER_USEC			0x0BE8
#define SOLO_TIMER_SEC			0x0BEC
#define SOLO_AUDIO_CONTROL		0x0D00
#define SOLO_AUDIO_SAMPLE		0x0D04
#define SOLO_AUDIO_FDMA_INTR		0x0D08
#define SOLO_TIMER_USEC_LSB		0x0D20

/* Bit macros from driver */
#define SOLO_DMA_CTRL_SDRAM_SIZE(n)	((n)<<6)
#define SOLO_DMA_CTRL_REFRESH_CYCLE(n)	((n)<<8)
#define SOLO_DMA_CTRL_LATENCY(n)	((n)<<0)
#define SOLO_DMA_CTRL_READ_CLK_SELECT	BIT(2)
#define SOLO_DMA_CTRL_SDRAM_CLK_INVERT	BIT(5)
#define SOLO_SYS_CFG_OUTDIV(__n)	((__n) & 0x003) << 3)
#define SOLO_SYS_CFG_INPUTDIV(__n)	((__n) & 0x01f) << 14)
#define SOLO_SYS_CFG_FEEDBACKDIV(__n)	((__n) & 0x1ff) << 5)
#define SOLO_SYS_CFG_SDRAM64BIT		0x40000000
#define SOLO_SYS_CFG_RESET		0x80000000
#define SOLO_IRQ_G723			BIT(3)
#define SOLO_IRQ_P2M(n)			BIT((n) + 17)
#define SOLO_IRQ_PCI_ERR		BIT(10)
#define SOLO_IRQ_VIDEO_IN		BIT(14)
#define SOLO_IRQ_ENCODER		BIT(0)
#define SOLO_IRQ_IIC			BIT(6)

/* Enums from driver */
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
    uint32_t irq_status;
    uint32_t irq_mask;

    /* Shadow registers: addressable space, covers all defined offsets */
    uint32_t regs[(BAR0_SIZE) / 4];
    uint8_t sdram[SDRAM_SIZE];
};

/* Forward declaration of pcibase_reset to avoid implicit declaration error */
static void pcibase_reset(DeviceState *dev);
static void pcibase_soft_reset(PCIBaseState *s, uint32_t sys_cfg_val);

/* Helper: update IRQ line based on status and mask */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->irq_status & s->irq_mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read out of bounds at 0x%"HWADDR_PRIx"\n", __func__, addr);
        return ~0ULL;
    }

    /* Most registers are simple 32-bit reads from shadow array */
    val = s->regs[addr / 4];

    /* Special handling for some registers if needed (e.g., read-clear) */
    switch (addr) {
    case SOLO_IRQ_STAT:
        /* IRQ_STAT is read-only? driver reads it. We return current status */
        break;
    case SOLO_IRQ_MASK:
        break;
    case SOLO_CHIP_OPTION:
        /* Read chip ID; lower bits are chip ID */
        break;
    default:
        break;
    }

    return val;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t idx = addr / 4;

    if (addr >= BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write out of bounds at 0x%"HWADDR_PRIx"\n", __func__, addr);
        return;
    }

    switch (addr) {
    case SOLO_IRQ_STAT:
        /* W1C: clear only bits that are set in val */
        s->irq_status &= ~((uint32_t)val);
        s->regs[idx] = s->irq_status;
        pcibase_update_irq(s);
        break;

    case SOLO_IRQ_MASK:
        s->irq_mask = (uint32_t)val;
        s->regs[idx] = s->irq_mask;
        pcibase_update_irq(s);
        break;

    case SOLO_SYS_CFG:
        /* Writing SOLO_SYS_CFG_RESET triggers a chip soft reset */
        if (val & SOLO_SYS_CFG_RESET) {
            pcibase_soft_reset(s, (uint32_t)val);
        } else {
            s->regs[idx] = (uint32_t)val;
        }
        break;

    default:
        /* Check for P2M_CONTROL registers */
        if (addr >= SOLO_P2M_CONTROL(0) &&
            (addr - SOLO_P2M_CONTROL(0)) % 0x20 == 0) {
            int n = (addr - SOLO_P2M_CONTROL(0)) / 0x20;
            if (val & 1) {  /* start bit */
                PCIDevice *pdev = PCI_DEVICE(s);
                uint32_t config  = s->regs[SOLO_P2M_CONFIG(n) / 4];
                uint32_t tar_adr = s->regs[SOLO_P2M_TAR_ADR(n) / 4];
                uint32_t ext_adr = s->regs[SOLO_P2M_EXT_ADR(n) / 4];
                uint32_t p2m_size = config; /* assume config holds size */
                bool write_to_sdram = (val & 2) ? true : false; /* direction bit */

                if (ext_adr + p2m_size > SDRAM_SIZE) {
                    qemu_log_mask(LOG_GUEST_ERROR,
                                 "%s: SDRAM overflow: ext_adr 0x%x, size %d\n",
                                 __func__, ext_adr, p2m_size);
                    break;
                }

                if (write_to_sdram) {
                    pci_dma_read(pdev, tar_adr, s->sdram + ext_adr, p2m_size);
                } else {
                    pci_dma_write(pdev, tar_adr, s->sdram + ext_adr, p2m_size);
                }

                /* Clear start bit to indicate completion */
                s->regs[idx] &= ~1u;

                /* Signal completion interrupt */
                s->irq_status |= SOLO_IRQ_P2M(n);
                s->regs[SOLO_IRQ_STAT / 4] = s->irq_status;
                pcibase_update_irq(s);
            } else {
                s->regs[idx] = (uint32_t)val;
            }
        } else {
            /* For all other registers, just store the value */
            s->regs[idx] = (uint32_t)val;
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

/* Not used by driver, kept as stub */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* Soft reset: triggered by writing SOLO_SYS_CFG with RESET bit.
   Preserves irq_mask and chip_option; clears other regs, does NOT clear SDRAM. */
static void pcibase_soft_reset(PCIBaseState *s, uint32_t sys_cfg_val)
{
    uint32_t saved_irq_mask = s->irq_mask;
    uint32_t saved_chip_option = s->regs[SOLO_CHIP_OPTION / 4];

    /* Clear all registers */
    memset(s->regs, 0, sizeof(s->regs));

    /* Restore critical settings */
    s->regs[SOLO_CHIP_OPTION / 4] = saved_chip_option;
    s->irq_mask = saved_irq_mask;
    s->regs[SOLO_IRQ_MASK / 4] = saved_irq_mask;

    /* Clear irq_status */
    s->irq_status = 0;
    s->regs[SOLO_IRQ_STAT / 4] = 0;

    /* Set SYS_CFG register value without RESET bit, as written by driver */
    s->regs[SOLO_SYS_CFG / 4] = sys_cfg_val & ~SOLO_SYS_CFG_RESET;

    /* Update IRQ line (should be low since irq_status is 0) */
    pcibase_update_irq(s);

    /* Do NOT clear s->sdram */
}

/* Full reset: power-on or PCI reset */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Initialize shadow registers with sensible defaults */
    memset(s->regs, 0, sizeof(s->regs));
    memset(s->sdram, 0, SDRAM_SIZE);

    /* Set chip ID: simulate a 16-channel chip (chip_id 7) */
    s->regs[SOLO_CHIP_OPTION / 4] = 0x07; /* low bits are chip ID */

    /* Interrupts off */
    s->irq_status = 0;
    s->irq_mask = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_SOFTLOGIC);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_SOLO6010);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
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
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "bar0";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X not used by driver */
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
    .name = "solo6x10_pci",
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
