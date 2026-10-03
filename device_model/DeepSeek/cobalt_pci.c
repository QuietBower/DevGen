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

#define TYPE_PCIBASE_DEVICE "cobalt_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */

#define PCI_VENDOR_ID_CISCO 0x1137
#define PCI_DEVICE_ID_COBALT 0x2732
#define PCI_CLASS_COBALT 0x0400

#define COBALT_SYSSTAT_VI0_INT2_MSK		BIT(6)
#define COBALT_SYSSTAT_AUD_IN_LOST_DATA_MSK	BIT(29)
#define COBALT_SYSSTAT_VOHSMA_INT1_MSK		BIT(24)
#define COBALT_SYSSTAT_VI2_INT2_MSK		BIT(14)
#define COBALT_SYSSTAT_VI3_LOST_DATA_MSK	BIT(19)
#define COBALT_SYS_STAT_EDGE			(COBALT_SYS_STAT_BASE + 0x0c)
#define COBALT_SYSSTAT_VI1_LOST_DATA_MSK	BIT(11)
#define COBALT_SYSSTAT_VI1_INT2_MSK		BIT(10)
#define COBALT_SYSSTAT_VI2_LOST_DATA_MSK	BIT(15)
#define COBALT_SYSSTAT_AUD_OUT_LOST_DATA_MSK	BIT(30)
#define COBALT_SYSSTAT_VI1_INT1_MSK		BIT(9)
#define COBALT_SYSSTAT_VOHSMA_LOST_DATA_MSK	BIT(26)
#define COBALT_SYSSTAT_VI3_INT1_MSK		BIT(17)
#define COBALT_SYSSTAT_VI0_INT1_MSK		BIT(5)
#define COBALT_SYSSTAT_VI0_LOST_DATA_MSK	BIT(7)
#define COBALT_SYSSTAT_VIHSMA_LOST_DATA_MSK	BIT(23)
#define COBALT_SYSSTAT_VI2_INT1_MSK		BIT(13)
#define COBALT_SYSSTAT_VIHSMA_INT1_MSK		BIT(21)
#define COBALT_SYSSTAT_VI3_INT2_MSK		BIT(18)
#define COBALT_SYS_STAT_MASK			(COBALT_SYS_STAT_BASE + 0x08)
#define COBALT_SYSSTAT_VIHSMA_INT2_MSK		BIT(22)
#define COBALT_NUM_NODES	6
#define COBALT_SYS_CTRL_HPD_TO_CONNECTOR_BIT(n)	(6 + 4 * (n))
#define COBALT_SYS_CTRL_BASE			0x400
#define COBALT_HDL_INFO_BASE			0x4800
#define COBALT_HDL_SEARCH_STR			"** HDL version info **"
#define COBALT_HDL_INFO_SIZE			0x200
#define COBALT_HSMA_OUT_NODE	5
#define COBALT_HSMA_IN_NODE	4
#define COBALT_SYS_CTRL_VIDEO_RX_RESETN_BIT(n)	(4 + 4 * (n))
#define COBALT_SYS_CTRL_NRESET_TO_HDMI_BIT(n)	(5 + 4 * (n))
#define COBALT_SYS_CTRL_PWRDN0_TO_HSMA_TX_BIT	24
#define COBALT_SYS_CTRL_VIDEO_TX_RESETN_BIT	25
#define COBALT_SYS_CTRL_HSMA_TX_ENABLE_BIT	1
#define COBALT_SYSSTAT_HSMA_PRSNTN_MSK		BIT(2)
#define COBALT_SYS_STAT_BASE			0x500
#define COBALT_I2C_3_BASE			0x180
#define COBALT_I2C_0_BASE			0x0
#define COBALT_I2C_HSMA_BASE			0x200
#define COBALT_I2C_2_BASE			0x100
#define COBALT_I2C_1_BASE			0x080
#define COBALT_BYTES_PER_PIXEL_RGB32		4
#define COBALT_BYTES_PER_PIXEL_YUYV		2
#define COBALT_BUS_FLASH_BASE			0x08000000
#define COBALT_BUS_BAR1_BASE			0x600
#define COBALT_CVI_CLK_LOSS(cobalt, c) \
	(cobalt->bar1 + COBALT_VID_BASE + (c) * COBALT_VID_SIZE + 0x400)
#define COBALT_CVI_VMR(cobalt, c) \
	(cobalt->bar1 + COBALT_VID_BASE + (c) * COBALT_VID_SIZE + 0x100)
#define COBALT_CVI_FREEWHEEL(cobalt, c) \
	(cobalt->bar1 + COBALT_VID_BASE + (c) * COBALT_VID_SIZE + 0x300)
#define COBALT_CLK		50000000
#define COBALT_CVI_EVCNT(cobalt, c) \
	(cobalt->bar1 + COBALT_VID_BASE + (c) * COBALT_VID_SIZE + 0x200)
#define COBALT_CVI(cobalt, c) \
	(cobalt->bar1 + COBALT_VID_BASE + (c) * COBALT_VID_SIZE)
#define COBALT_MAX_BPP				3
#define COBALT_MAX_WIDTH			1920
#define COBALT_MAX_HEIGHT			1200
#define COBALT_VID_SIZE				0x1000
#define COBALT_VID_BASE				0x10000
#define COBALT_SYS_CTRL_AUDIO_IPP_RESETN_BIT(n)	(7 + 4 * (n))
#define COBALT_SYS_CTRL_AUDIO_OPP_RESETN_BIT	27
#define COBALT_TX_BASE(cobalt) (cobalt->bar1 + COBALT_VID_BASE + 0x5000)
#define DMA_INTERRUPT_STATUS_REG		0x08

/* M00235_CONTROL_BITMAP macros */
#define M00235_CONTROL_BITMAP_ENABLE_MSK         (0x1 << M00235_CONTROL_BITMAP_ENABLE_OFST)
#define M00235_CONTROL_BITMAP_PACK_FORMAT_OFST   (1)
#define M00235_CONTROL_BITMAP_ENDIAN_FORMAT_MSK  (0x1 << M00235_CONTROL_BITMAP_ENDIAN_FORMAT_OFST)
#define M00235_CONTROL_BITMAP_ENABLE_OFST        (0)
#define M00235_CONTROL_BITMAP_ENDIAN_FORMAT_OFST (3)

/* M00233 macros */
#define M00233_CONTROL_BITMAP_ENABLE_MEASURE_MSK      (0x1 << M00233_CONTROL_BITMAP_ENABLE_MEASURE_OFST)
#define M00233_IRQ_TRIGGERS_BITMAP_HACTIVE_AREA_MSK   (0x1 << M00233_IRQ_TRIGGERS_BITMAP_HACTIVE_AREA_OFST)
#define M00233_IRQ_TRIGGERS_BITMAP_VACTIVE_AREA_MSK   (0x1 << M00233_IRQ_TRIGGERS_BITMAP_VACTIVE_AREA_OFST)
#define M00233_CONTROL_BITMAP_ENABLE_MEASURE_OFST     (2)
#define M00233_IRQ_TRIGGERS_BITMAP_HACTIVE_AREA_OFST  (6)
#define M00233_IRQ_TRIGGERS_BITMAP_VACTIVE_AREA_OFST  (2)

/* M00473 macros */
#define M00473_CTRL_BITMAP_ENABLE_MSK                (0x1 << M00473_CTRL_BITMAP_ENABLE_OFST)
#define M00473_CTRL_BITMAP_FORCE_FREEWHEEL_MODE_MSK  (0x1 << M00473_CTRL_BITMAP_FORCE_FREEWHEEL_MODE_OFST)
#define M00473_CTRL_BITMAP_ENABLE_OFST               (0)
#define M00473_CTRL_BITMAP_FORCE_FREEWHEEL_MODE_OFST (1)

/* M00479 macros */
#define M00479_CTRL_BITMAP_ENABLE_MSK           (0x1 << M00479_CTRL_BITMAP_ENABLE_OFST)
#define M00479_CTRL_BITMAP_ENABLE_OFST          (0)

/* M00460 macros */
#define M00460_CONTROL_BITMAP_CLEAR_MSK   (0x1 << M00460_CONTROL_BITMAP_CLEAR_OFST)
#define M00460_CONTROL_BITMAP_ENABLE_MSK  (0x1 << M00460_CONTROL_BITMAP_ENABLE_OFST)
#define M00460_CONTROL_BITMAP_CLEAR_OFST  (1)
#define M00460_CONTROL_BITMAP_ENABLE_OFST (0)

/* M00514 macros */
#define M00514_CONTROL_BITMAP_EVCNT_CLEAR_MSK                (0x1 << M00514_CONTROL_BITMAP_EVCNT_CLEAR_OFST)
#define M00514_CONTROL_BITMAP_FLOW_CTRL_OUTPUT_ENABLE_MSK    (0x1 << M00514_CONTROL_BITMAP_FLOW_CTRL_OUTPUT_ENABLE_OFST)
#define M00514_CONTROL_BITMAP_FORMAT_16_BPP_MSK              (0x1 << M00514_CONTROL_BITMAP_FORMAT_16_BPP_OFST)
#define M00514_CONTROL_BITMAP_SYNC_GENERATOR_LOAD_PARAM_MSK  (0x1 << M00514_CONTROL_BITMAP_SYNC_GENERATOR_LOAD_PARAM_OFST)
#define M00514_CONTROL_BITMAP_SYNC_GENERATOR_ENABLE_MSK      (0x1 << M00514_CONTROL_BITMAP_SYNC_GENERATOR_ENABLE_OFST)
#define M00514_CONTROL_BITMAP_EVCNT_ENABLE_MSK               (0x1 << M00514_CONTROL_BITMAP_EVCNT_ENABLE_OFST)
#define M00514_CONTROL_BITMAP_EVCNT_CLEAR_OFST               (6)
#define M00514_CONTROL_BITMAP_FLOW_CTRL_OUTPUT_ENABLE_OFST   (2)
#define M00514_CONTROL_BITMAP_FORMAT_16_BPP_OFST             (7)
#define M00514_CONTROL_BITMAP_SYNC_GENERATOR_LOAD_PARAM_OFST (0)
#define M00514_CONTROL_BITMAP_SYNC_GENERATOR_ENABLE_OFST     (1)
#define M00514_CONTROL_BITMAP_EVCNT_ENABLE_OFST              (5)

/* New defines from supplementary driver source */
#define CAPABILITY_REGISTER	(BASE + 0x04)
#define CS_REG(c)		(BASE + 0x60 + ((c) * 0x40))
#define PCI_64BIT		(1 << 8)
#define DONE                    (1 << 4)
#define ABORT                   (1 << 2)
#define DMA_TYPE_FIFO		(1 << 18)

/* BASE is now defined as bar0 base address (from driver) */
#define BASE 0 /* Relative to BAR0, but in QEMU we use offset within BAR0 */

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
    uint32_t sys_ctrl;
    uint32_t sys_status;
    uint32_t sys_mask;
    uint32_t sys_edge;
    uint8_t hdl_info[COBALT_HDL_INFO_SIZE];
    uint32_t video_regs[COBALT_NUM_NODES][COBALT_VID_SIZE / 4];
    uint32_t tx_regs[COBALT_VID_SIZE / 4];

    /* DMA Context */
    uint32_t dma_intr_status; /* Placeholder; more from setup */

    /* Status_Stru: Operational status flags */
    uint32_t sys_stat;

    /* Probe_Reset_Stru: State used to handle reset sequences */
    uint32_t reset_state;

    /* New registers from omni_sg_dma_init context */
    uint32_t dma_adrs_reg;       /* ADRS_REG at 0x600 */
    uint32_t dma_lower_data;     /* LOWER_DATA at 0x604 */
    uint32_t dma_upper_data;     /* UPPER_DATA at 0x606 */
    /* CAPABILITY_REGISTER at BAR0 offset 0x04 */
    uint32_t capability_register;
    /* CS_REG(c) at BAR0 offset 0x60 + c*0x40 */
    uint32_t cs_reg[8];
};

/* Internal helper for status-triggered signaling. */
static void cobalt_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->sys_status & s->sys_mask) {
        msi_notify(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* No device-initiated DMA visible in driver */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */

/* BAR0: Contains CAPABILITY_REGISTER and CS_REG registers */
static uint64_t pcibase_bar0_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = (PCIBaseState *)opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0x04 ... 0x07: /* CAPABILITY_REGISTER */
        val = s->capability_register;
        break;
    case 0x60 ... 0x23F: /* CS_REG area, up to channel 7 offset 0x60+7*0x40+3 = 0x223 */
        if (addr >= 0x60 && addr <= (0x60 + 7 * 0x40 + 3)) {
            int channel = (addr - 0x60) / 0x40;
            hwaddr reg_offset = (addr - 0x60) % 0x40;
            if (reg_offset < 4 && channel < 8) {
                val = s->cs_reg[channel];
            } else {
                qemu_log_mask(LOG_UNIMP, "cobalt: unimplemented BAR0 CS_REG sub-offset 0x%"HWADDR_PRIx"\n", addr);
            }
        } else {
            qemu_log_mask(LOG_UNIMP, "cobalt: unimplemented BAR0 read at 0x%"HWADDR_PRIx"\n", addr);
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "cobalt: unimplemented BAR0 read at 0x%"HWADDR_PRIx"\n", addr);
        break;
    }
    return val;
}

static void pcibase_bar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = (PCIBaseState *)opaque;

    switch (addr) {
    case 0x04 ... 0x07:
        s->capability_register = val;
        break;
    case 0x60 ... 0x23F:
        if (addr >= 0x60 && addr <= (0x60 + 7 * 0x40 + 3)) {
            int channel = (addr - 0x60) / 0x40;
            hwaddr reg_offset = (addr - 0x60) % 0x40;
            if (reg_offset < 4 && channel < 8) {
                s->cs_reg[channel] = val;
            } else {
                qemu_log_mask(LOG_UNIMP, "cobalt: unimplemented BAR0 CS_REG sub-offset 0x%"HWADDR_PRIx"\n", addr);
            }
        } else {
            qemu_log_mask(LOG_UNIMP, "cobalt: unimplemented BAR0 write at 0x%"HWADDR_PRIx"\n", addr);
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "cobalt: unimplemented BAR0 write at 0x%"HWADDR_PRIx"\n", addr);
        break;
    }
}

static const MemoryRegionOps pcibase_bar0_ops = {
    .read = pcibase_bar0_read,
    .write = pcibase_bar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* BAR1: Contains video/capture registers, DMA-related registers, etc. */
static uint64_t pcibase_bar1_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = (PCIBaseState *)opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0x400 ... 0x403:
        val = s->sys_ctrl;
        break;
    case 0x500 ... 0x503:
        val = s->sys_status;
        break;
    case 0x508 ... 0x50b:
        val = s->sys_mask;
        break;
    case 0x50c ... 0x50f:
        val = 0; /* Edge register write-only */
        break;
    case 0x4800 ... 0x49ff:
        val = s->hdl_info[addr - 0x4800];
        break;
    /* New DMA-related registers */
    case 0x600 ... 0x603:  /* ADRS_REG */
        val = s->dma_adrs_reg;
        break;
    case 0x604 ... 0x605:  /* LOWER_DATA (2 bytes? driver uses +4, so maybe 32-bit at 0x604) */
        if (size == 4) {
            val = s->dma_lower_data;
        } else {
            qemu_log_mask(LOG_UNIMP, "cobalt: Unaligned read at LOWER_DATA");
        }
        break;
    case 0x606 ... 0x607:  /* UPPER_DATA (2 bytes? driver uses +6, so maybe 16-bit at 0x606) */
        if (size == 2 || size == 4) {
            val = s->dma_upper_data;
        } else {
            qemu_log_mask(LOG_UNIMP, "cobalt: Unaligned read at UPPER_DATA");
        }
        break;
    default:
        if (addr >= 0x10000 && addr < 0x16000) {
            hwaddr offset = addr - 0x10000;
            if (offset < 0x5000) {
                int channel = offset / 0x1000;
                hwaddr reg = (offset % 0x1000) / 4;
                val = s->video_regs[channel][reg];
            } else if (offset >= 0x5000 && offset < 0x6000) {
                hwaddr reg = (offset - 0x5000) / 4;
                val = s->tx_regs[reg];
            }
        } else {
            qemu_log_mask(LOG_UNIMP, "cobalt: unimplemented BAR1 read at 0x%"HWADDR_PRIx"\n", addr);
        }
        break;
    }

    return val;
}

static void pcibase_bar1_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = (PCIBaseState *)opaque;

    switch (addr) {
    case 0x400 ... 0x403:
        s->sys_ctrl = val;
        break;
    case 0x508 ... 0x50b:
        s->sys_mask = val;
        cobalt_update_irq(s);
        break;
    case 0x50c ... 0x50f:
        s->sys_status &= ~val;
        cobalt_update_irq(s);
        break;
    /* New DMA-related registers */
    case 0x600 ... 0x603:  /* ADRS_REG */
        s->dma_adrs_reg = val;
        break;
    case 0x604 ... 0x605:  /* LOWER_DATA (driver may use iowrite32 at +4, so 32-bit) */
        if (size == 4) {
            s->dma_lower_data = val;
        } else {
            qemu_log_mask(LOG_UNIMP, "cobalt: Unaligned write to LOWER_DATA");
        }
        break;
    case 0x606 ... 0x607:  /* UPPER_DATA (driver may use iowrite32 at +6, might be misaligned) */
        if (size == 2 || size == 4) {
            s->dma_upper_data = val;
        } else {
            qemu_log_mask(LOG_UNIMP, "cobalt: Unaligned write to UPPER_DATA");
        }
        break;
    default:
        if (addr >= 0x10000 && addr < 0x16000) {
            hwaddr offset = addr - 0x10000;
            if (offset < 0x5000) {
                int channel = offset / 0x1000;
                hwaddr reg = (offset % 0x1000) / 4;
                s->video_regs[channel][reg] = val;
            } else if (offset >= 0x5000 && offset < 0x6000) {
                hwaddr reg = (offset - 0x5000) / 4;
                s->tx_regs[reg] = val;
            }
        } else if (addr >= 0x4800 && addr < 0x4a00) {
            /* HDL info is read-only */
        } else if (addr >= 0x500 && addr < 0x50c) {
            /* Status register is read-only */
        } else {
            qemu_log_mask(LOG_UNIMP, "cobalt: unimplemented BAR1 write at 0x%"HWADDR_PRIx"\n", addr);
        }
        break;
    }
}

static const MemoryRegionOps pcibase_bar1_ops = {
    .read = pcibase_bar1_read,
    .write = pcibase_bar1_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset_Func */
    s->sys_ctrl = 0;
    s->sys_status = 0;
    s->sys_mask = 0;
    s->sys_edge = 0;
    memset(s->hdl_info, 0, sizeof(s->hdl_info));
    memcpy(s->hdl_info, COBALT_HDL_SEARCH_STR "\n", strlen(COBALT_HDL_SEARCH_STR) + 1);
    memset(s->video_regs, 0, sizeof(s->video_regs));
    memset(s->tx_regs, 0, sizeof(s->tx_regs));
    s->dma_intr_status = 0;
    s->sys_stat = 0;
    s->reset_state = 0;
    s->dma_adrs_reg = 0;
    s->dma_lower_data = 0;
    s->dma_upper_data = 0;
    s->capability_register = 0;
    memset(s->cs_reg, 0, sizeof(s->cs_reg));
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x1137);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x2732);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0400);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    /* Override PCIe link capabilities and status to indicate a x4 link width and active link */
    pci_set_word(pci_conf + 0x80 + 0x0C, 0x0042); /* Link Capability: Max Link Width x4, Max Link Speed 2.5GT/s */
    pci_set_word(pci_conf + 0x80 + 0x12, 0x0441); /* Link Status: DLLLA, Negotiated Width x4, Speed 2.5GT/s */

    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization */
    s->num_bars = 2;

    /* BAR 0 */
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_bar0_ops, s, "cobalt-bar0", 0x1000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[0]);

    /* BAR 1 */
    memory_region_init_io(&s->bar_regions[1], OBJECT(s), &pcibase_bar1_ops, s, "cobalt-bar1", 0x20000);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[1]);

    /* MSI_OR_MSIX_INIT: msi_init */
    msi_init(pdev, 0, 1, true, false, errp);

    /* DMA_Config_Real: TODO: DMA configuration */
    /* Timer_Config_Real */
    /* Field_Init_Real */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* Uninit_Func: Free buffers, stop timers, etc. */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "cobalt_pci",
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
