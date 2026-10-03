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

#define TYPE_PCIBASE_DEVICE "atomisp_isp2_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x8086
#define DEVICE_ID 0x1178
#define CLASS_ID 0x0480

/* Register offsets from driver source (atomisp_v4l2.c) */
#define MRFLD_PCI_CSI_CONTROL			0xe8
#define MRFLD_PCI_CSI_CONTROL_CSI_READY		BIT(25)
#define MRFLD_PCI_CSI_AFE_RCOMP_CONTROL	0xe0
#define PCI_MSI_DATA				0x98
#define MRFLD_PCI_CSI_AFE_HS_CONTROL		0xdc
#define MRFLD_PCI_CSI_HS_OVR_CLK_GATE_ON_UPDATE	0x800000
#define MRFLD_PCI_CSI_CONTROL_PARPATHEN		BIT(24)
#define PCI_INTERRUPT_CTRL			0x9C
#define MRFLD_PCI_I_CONTROL_ENABLE_WRITE_COMBINING	0x2
#define PCI_I_CONTROL				0xfc
#define MRFLD_PCI_PMCS				0x84
#define MRFLD_PCI_CSI_RCOMP_CONTROL		0xf4
#define PCI_MSI_CAPID				0x90
#define MRFLD_PCI_CSI_ACCESS_CTRL_VIOL		0xd4
#define MRFLD_PCI_CSI_DEADLINE_CONTROL		0xec
#define PCI_MSI_ADDR				0x94
#define MRFLD_PCI_CSI_AFE_TRIM_CONTROL		0xe4
#define MRFLD_CSI_RECEIVER_SELECTION_REG	0x8081c
#define MRFLD_INTR_ENABLE_REG			0x510
#define INTR_IER				24
#define MRFLD_INTR_STATUS_REG			0x508
#define MRFLD_INTR_CLEAR_REG			0x50c
#define INTR_IIR				16
#define MRFLD_ISPSSDVFS				0x13F
#define MRFLD_BIT1				0x0002
#define MRFLD_BIT0				0x0001
#define MRFLD_ISPSSPM0_IUNIT_POWER_OFF		0x3
#define MRFLD_ISPSSPM0_ISPSSS_OFFSET		24
#define MRFLD_ISPSSPM0				0x39
#define MRFLD_ISPSSPM0_ISPSSC_MASK		0x3
#define MRFLD_ISPSSPM0_IUNIT_POWER_ON		0
#define MRFLD_ALL_CSI_PORTS_OFF_MASK		0x7
#define MRFLD_PORT3_LANES_SHIFT			8
#define MRFLD_PORT1_LANES_SHIFT			3
#define CHV_PORT_CONFIG_MASK			0x1f07ff
#define MRFLD_PORT1_ENABLE_SHIFT		0
#define MRFLD_PORT_CONFIG_NUM			8
#define MRFLD_PORT2_LANES_SHIFT			7
#define MRFLD_PORT_CONFIG_MASK			0x000f03ff
#define MRFLD_PORT2_ENABLE_SHIFT		1
#define CHV_PORT3_LANES_SHIFT			9
#define MRFLD_PORT3_ENABLE_SHIFT		2
#define MRFLD_PORT_CONFIGCODE_SHIFT		16
#define ATOMISP_SUBDEV_PAD_SOURCE		1
#define SENSOR_ISP_PAD_SINK			0
#define CSI2_PAD_SOURCE				1
#define SENSOR_ISP_PAD_SOURCE			1
#define CSI2_PAD_SINK				0
#define ATOMISP_SUBDEV_PAD_SINK			0
#define MRFLD_PCI_CSI1_HSRXCLKTRIM_SHIFT	16
#define MRFLD_PCI_CSI3_HSRXCLKTRIM_SHIFT	28
#define MRFLD_PCI_CSI3_HSRXCLKTRIM		0x2
#define HPLL_FREQ_1600MHZ			0x640
#define MRFLD_PCI_CSI_HSRXCLKTRIM_MASK		0xf
#define MRFLD_PCI_CSI2_HSRXCLKTRIM_SHIFT	24
#define CCK_FUSE_HPLL_FREQ_MASK			0x03
#define ATOMISP_PCI_DEVICE_SOC_MRFLD_1179	0x1179
#define ATOMISP_PCI_DEVICE_SOC_MRFLD_117A	0x117a
#define ATOMISP_PCI_DEVICE_SOC_BYT		0x0f38
#define MRFLD_PCI_CSI2_HSRXCLKTRIM		0x3
#define ATOMISP_PCI_DEVICE_SOC_ANN		0x1478
#define HPLL_FREQ_2000MHZ			0x7D0
#define ATOMISP_PCI_DEVICE_SOC_MRFLD		0x1178
#define MRFLD_PCI_CSI1_HSRXCLKTRIM		0x2
#define HPLL_FREQ_800MHZ			0x320
#define CCK_FUSE_REG_0				0x08
#define ATOMISP_PCI_DEVICE_SOC_CHT		0x22b8
#define DMA_BURST_SIZE_REG			0xCD408
#define CSI2_PORT_A_BASE			0xC0000
#define CSI2_LANE_D2_BASE			0x430
#define CSI2_REG_RX_CSI_DLY_CNT_TERMEN		0
#define CSI2_LANE_D1_BASE			0x428
#define CSI2_REG_RX_CSI_DLY_CNT_SETTLE		0x4
#define CSI2_PORT_C_BASE			0xC4000
#define CSI2_LANE_CL_BASE			0x418
#define CSI2_LANE_D0_BASE			0x420
#define CSI2_LANE_D3_BASE			0x438
#define CSI2_PORT_B_BASE			0xC2000

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
    struct {
        uint16_t pcicmdsts;
        uint32_t ispmmadr;
        uint32_t msicap;
        uint32_t msi_addr;
        uint16_t msi_data;
        uint8_t intr;
        uint32_t interrupt_control;
        uint32_t pmcs;
        uint32_t cg_dis;
        uint32_t i_control;
        uint32_t csi_rcomp_config;
        uint32_t csi_afe_dly;
        uint32_t csi_control;
        uint32_t csi_afe_rcomp_config;
        uint32_t csi_afe_hs_control;
        uint32_t csi_deadline_control;
        uint32_t csi_access_viol;
        uint32_t csi_receiver_selection;
    } regs;

    /* DMA Context (not used in this implementation, keeping placeholder) */
    uint64_t dma_addr;
    uint32_t dma_count;
    uint32_t dma_status;
    uint32_t dma_burst_size;

    uint32_t status;
    /* Operational status flags */

    /* State used to handle reset sequences */
    /* Power management state (D0-D3) */
    uint32_t pm_state;
};

/* ---- Forward declarations ---- */
static void pcibase_update_irq(PCIBaseState *s);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);

/* ---- MMIO Handlers ---- */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case MRFLD_INTR_STATUS_REG:
        val = s->intr_status;
        break;
    case MRFLD_INTR_ENABLE_REG:
        val = s->intr_mask;
        break;
    case MRFLD_CSI_RECEIVER_SELECTION_REG:
        val = s->regs.csi_receiver_selection;
        break;
    case DMA_BURST_SIZE_REG:
        val = s->dma_burst_size;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "pcibase: unknown MMIO read at 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case MRFLD_INTR_CLEAR_REG:
        s->intr_status &= ~val;
        pcibase_update_irq(s);
        break;
    case MRFLD_INTR_ENABLE_REG:
        s->intr_mask = val;
        pcibase_update_irq(s);
        break;
    case MRFLD_CSI_RECEIVER_SELECTION_REG:
        s->regs.csi_receiver_selection = val;
        break;
    case DMA_BURST_SIZE_REG:
        s->dma_burst_size = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "pcibase: unknown MMIO write at 0x%" HWADDR_PRIx " = 0x%" PRIx64 "\n", addr, val);
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

/* Dummy PIO ops to satisfy template references (no PIO used) */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/* ---- PCI Config Space Handlers ---- */

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (addr) {
    case 0x04: /* PCI_COMMAND */
        val = s->regs.pcicmdsts;
        break;
    case 0x84: /* MRFLD_PCI_PMCS */
        val = s->regs.pmcs;
        break;
    case 0x3C: /* PCI_INTERRUPT_LINE */
        val = s->regs.intr;
        break;
    case 0x9C: /* PCI_INTERRUPT_CTRL */
        val = s->regs.interrupt_control;
        break;
    case 0xFC: /* PCI_I_CONTROL */
        val = s->regs.i_control;
        break;
    case 0xd4: /* MRFLD_PCI_CSI_ACCESS_CTRL_VIOL */
        val = s->regs.csi_access_viol;
        break;
    case 0xf4: /* MRFLD_PCI_CSI_RCOMP_CONTROL */
        val = s->regs.csi_rcomp_config;
        break;
    case 0xe4: /* MRFLD_PCI_CSI_AFE_TRIM_CONTROL */
        val = s->regs.csi_afe_dly;
        break;
    case 0xe8: /* MRFLD_PCI_CSI_CONTROL */
        val = s->regs.csi_control;
        break;
    case 0xe0: /* MRFLD_PCI_CSI_AFE_RCOMP_CONTROL */
        val = s->regs.csi_afe_rcomp_config;
        break;
    case 0xdc: /* MRFLD_PCI_CSI_AFE_HS_CONTROL */
        val = s->regs.csi_afe_hs_control;
        break;
    case 0xec: /* MRFLD_PCI_CSI_DEADLINE_CONTROL */
        val = s->regs.csi_deadline_control;
        break;
    default:
        break; /* keep default value */
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    pci_default_write_config(pdev, addr, val, len);

    switch (addr) {
    case 0x04:
        s->regs.pcicmdsts = val & 0xFFFF;
        break;
    case 0x84:
        s->regs.pmcs = val;
        break;
    case 0x3C:
        s->regs.intr = val & 0xFF;
        break;
    case 0x9C:
        s->regs.interrupt_control = val;
        /* hardware may have W1C behaviour for INTR_IIR; implement if needed */
        break;
    case 0xFC:
        s->regs.i_control = val;
        break;
    case 0xd4:
        s->regs.csi_access_viol = val;
        break;
    case 0xf4:
        s->regs.csi_rcomp_config = val;
        break;
    case 0xe4:
        s->regs.csi_afe_dly = val;
        break;
    case 0xe8:
        s->regs.csi_control = val;
        break;
    case 0xe0:
        s->regs.csi_afe_rcomp_config = val;
        break;
    case 0xdc:
        s->regs.csi_afe_hs_control = val;
        break;
    case 0xec:
        s->regs.csi_deadline_control = val;
        break;
    default:
        break; /* handled by pci_default_write_config */
    }
}

/* ---- Interrupt Handling ---- */

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->intr_status & s->intr_mask;

    if (pending && msi_enabled(pdev)) {
        msi_notify(pdev, 0);
    } else if (pending) {
        pci_set_irq(pdev, 1);
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* ---- Reset Handler ---- */

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->intr_status = 0;
    s->intr_mask = 0;
    s->regs.csi_receiver_selection = 0;
    s->dma_burst_size = 0;
    s->pm_state = 0;
    /* Other fields persist across reset? We'll keep regs as is but zeroing is safe. */
    memset(&s->regs, 0, sizeof(s->regs));
    /* Restore some defaults if needed later */
}

/* ---- BAR Registration ---- */

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

/* ---- Realize ---- */

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration (vendor, device, class, revision are set in class_init) */
    pci_config_set_interrupt_pin(pci_conf, 1);
    /* Explicitly set revision ID to pass driver's revision check (>4) */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x06);

    /* Indicate PCIe, but do not add a PCIe capability structure to avoid conflicts */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;

    /* Add PM capability at fixed offset 0x80, which matches MRFLD_PCI_PMCS (0x84) */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0x80, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* MSI initialization at offset 0x90, as required by the driver */
    if (msi_init(pdev, 0x90, 1, true, true, errp) < 0) {
        return;
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 4 * MiB;  /* Placeholder size, actual size unknown */
    s->bar_info[0].name = "bar0-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Final state initialization */
    memset(&s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->dma_burst_size = 0;
    s->pm_state = 0;
}

/* ---- Uninit ---- */

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* Free buffers, stop timers, etc. placeholder */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "atomisp_isp2_pci",
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
    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);

    /* Set static PCI identifiers */
    k->vendor_id = VENDOR_ID;
    k->device_id = DEVICE_ID;
    k->class_id = CLASS_ID;
    k->revision = 0x06; /* Must be > ATOMISP_PCI_REV_BYT_A0_MAX (4) */
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
