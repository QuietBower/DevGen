/*
 * QEMU model for Wil6210 wireless device (Phase 4 - Debug & Update)
 * Fix: Ensure BAR0 size is 4 MB (within driver expected range 2-4 MB)
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

#define TYPE_PCIBASE_DEVICE "wil6210_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor, Device, Class ID */
#define VENDOR_ID 0x1ae9
#define DEVICE_ID 0x0310
#define CLASS_ID  0x0280

/* Hardware register offsets and bit masks (from driver) */
#define RGF_USER_JTAG_DEV_ID	(0x880b34)
#define RGF_USER_REVISION_ID_MASK	(3)
#define RGF_USER_REVISION_ID		(0x88afe4)
#define RGF_USER_OTP_HW_RD_MACHINE_1	(0x880ce0)
#define RGF_DMA_MISC_CTL				(0x881d6c)
#define RGF_USER_USAGE_8		(0x880020)
#define RGF_DMA_EP_TX_ICR		(0x881bb4)
#define RGF_INT_GEN_RX_ICR		(0x8bc0f4)
#define RGF_DMA_EP_MISC_ICR		(0x881bec)
#define RGF_DMA_EP_RX_ICR		(0x881bd0)
#define RGF_INT_GEN_TX_ICR		(0x8bc110)
#define RGF_USER_USAGE_2		(0x880008)
#define RGF_USER_USAGE_1		(0x880004)
#define RGF_USER_BL			(0x880A3C)
#define RGF_USER_XPM_IFC_RD_TIME4	(0x880cf8)
#define RGF_USER_CLKS_CTL_SW_RST_VEC_2	(0x880b0c)
#define RGF_USER_CLKS_CTL_EXT_SW_RST_VEC_0	(0x880c18)
#define RGF_USER_XPM_IFC_RD_TIME5	(0x880cfc)
#define RGF_USER_CLKS_CTL_0		(0x880abc)
#define RGF_USER_CLKS_CTL_SW_RST_VEC_1	(0x880b08)
#define RGF_USER_XPM_IFC_RD_TIME9	(0x880d0c)
#define RGF_USER_CLKS_CTL_SW_RST_MASK_0	(0x880b14)
#define RGF_USER_CLKS_CTL_SW_RST_VEC_3	(0x880b10)
#define RGF_DMA_OFUL_NID_0		(0x881cd4)
#define RGF_CAF_PLL_LOCK_STATUS		(0x88afec)
#define RGF_USER_XPM_IFC_RD_TIME10	(0x880d10)
#define RGF_CAF_OSC_CONTROL		(0x88afa4)
#define RGF_USER_CLKS_CTL_EXT_SW_RST_VEC_1	(0x880c2c)
#define RGF_USER_SPARROW_M_4			(0x880c50)
#define RGF_USER_XPM_IFC_RD_TIME7	(0x880d04)
#define RGF_USER_CLKS_CTL_SW_RST_VEC_0	(0x880b04)
#define RGF_USER_XPM_IFC_RD_TIME3	(0x880cf4)
#define RGF_USER_XPM_IFC_RD_TIME8	(0x880d08)
#define RGF_USER_XPM_RD_DOUT_SAMPLE_TIME (0x880d64)
#define RGF_USER_XPM_IFC_RD_TIME1	(0x880cec)
#define RGF_USER_XPM_IFC_RD_TIME2	(0x880cf0)
#define RGF_USER_USAGE_6		(0x880018)
#define RGF_USER_XPM_IFC_RD_TIME6	(0x880d00)
#define RGF_HP_CTRL			(0x88265c)
#define RGF_USER_USER_CPU_0		(0x8801e0)
#define RGF_USER_USER_CPU_0_TALYN_MB	(0x8c0138)
#define RGF_USER_MAC_CPU_0		(0x8801fc)
#define RGF_USER_MAC_CPU_0_TALYN_MB	(0x8c0154)
#define RGF_OTP_OEM_MAC			(0x8a0334)
#define RGF_OTP_MAC			(0x8a0620)
#define RGF_OTP_MAC_TALYN_MB		(0x8a0304)
#define RGF_PAL_UNIT_ICR		(0x88266c)
#define RGF_USER_FW_CALIB_RESULT	(0x880a90)
#define RGF_CAF_ICR			(0x88946c)
#define RGF_INT_CTRL_INT_GEN_CFG_0	(0x8bc000)
#define RGF_INT_GEN_IDLE_TIME_LIMIT	(0x8bc134)
#define RGF_INT_COUNT_ON_SPECIAL_EVT	(0x8b62d8)
#define RGF_INT_GEN_TIME_UNIT_LIMIT	(0x8bc0c8)
#define RGF_INT_CTRL_INT_GEN_CFG_1	(0x8bc004)
#define RGF_DMA_ITR_TX_IDL_CNT_TRSH			(0x881d60)
#define RGF_DMA_ITR_TX_CNT_TRSH			(0x881d34)
#define RGF_DMA_ITR_TX_CNT_CTL			(0x881d3c)
#define RGF_DMA_ITR_TX_IDL_CNT_CTL			(0x881d68)
#define RGF_DMA_ITR_RX_CNT_TRSH			(0x881d44)
#define RGF_DMA_ITR_RX_IDL_CNT_CTL			(0x881d5c)
#define RGF_DMA_ITR_RX_CNT_CTL			(0x881d4c)
#define RGF_DMA_ITR_RX_IDL_CNT_TRSH			(0x881d54)
#define RGF_DMA_PSEUDO_CAUSE_MASK_SW	(0x881c6c)
#define RGF_OTP_QC_SECURED		(0x8a0038)
#define RGF_USER_CPU_PC			(0x8801e8)
#define RGF_USER_USER_ICR		(0x880b4c)
#define RGF_MBOX   RGF_USER_USER_SCRATCH_PAD
#define RGF_USER_USER_SCRATCH_PAD	(0x8802bc)

/* Bit macros for hardware registers */
#define BIT_DMA_EP_TX_ICR_TX_DONE	BIT(0)
#define BIT_DMA_EP_RX_ICR_RX_HTRSH	BIT(1)
#define BIT_DMA_EP_RX_ICR_RX_DONE	BIT(0)
#define BIT_DMA_OFUL_NID_0_RX_EXT_A3_SRC	BIT(2)
#define BIT_DMA_OFUL_NID_0_RX_EXT_TR_EN		BIT(0)
#define BIT_CAF_OSC_DIG_XTAL_STABLE	BIT(0)
#define BIT_CAF_OSC_XTAL_EN		BIT(0)
#define BIT_HPAL_PERST_FROM_PAD	BIT(6)
#define BIT_CAR_PERST_RST	BIT(7)
#define BIT_USER_CLKS_CAR_AHB_SW_SEL	BIT(1)
#define BIT_USER_CLKS_RST_PWGD	BIT(11)
#define BIT_SPARROW_M_4_SEL_SLEEP_OR_REF	BIT(2)
#define BIT_USER_USER_CPU_MAN_RST	BIT(1)
#define BIT_USER_MAC_CPU_MAN_RST	BIT(1)
#define BIT_USER_OOB_R2_MODE		BIT(30)
#define BIT_USER_OOB_MODE		BIT(31)
#define BIT_DMA_ITR_RX_IDL_CNT_CTL_CLR			BIT(3)
#define BIT_DMA_ITR_RX_CNT_CTL_EXT_TIC_SEL	BIT(1)
#define BIT_DMA_ITR_RX_IDL_CNT_CTL_EN			BIT(0)
#define BIT_DMA_ITR_RX_CNT_CTL_EN		BIT(0)
#define BIT_DMA_ITR_RX_IDL_CNT_CTL_EXT_TIC_SEL		BIT(1)
#define BIT_DMA_ITR_TX_CNT_CTL_EN		BIT(0)
#define BIT_DMA_ITR_TX_IDL_CNT_CTL_EN			BIT(0)
#define BIT_DMA_ITR_TX_CNT_CTL_EXT_TIC_SEL	BIT(1)
#define BIT_DMA_ITR_TX_CNT_CTL_CLR		BIT(3)
#define BIT_DMA_ITR_TX_IDL_CNT_CTL_EXT_TIC_SEL		BIT(1)
#define BIT_DMA_ITR_TX_IDL_CNT_CTL_CLR			BIT(3)
#define BIT_DMA_ITR_RX_CNT_CTL_CLR		BIT(3)
#define BIT_DMA_PSEUDO_CAUSE_RX		BIT(0)
#define BIT_DMA_PSEUDO_CAUSE_MISC	BIT(2)
#define BIT_DMA_PSEUDO_CAUSE_TX		BIT(1)
#define BIT_DMA_EP_MISC_ICR_HALP	BIT(27)
#define BIT_DMA_EP_MISC_ICR_FW_INT(n)	BIT(28+n)
#define BIT_USER_PREVENT_DEEP_SLEEP	BIT(0)
#define BIT_NO_FLASH_INDICATION			BIT(8)
#define BIT_OFUL34_RDY_VALID_BUG_FIX_EN			BIT(7)
#define BIT_USER_SUPPORT_T_POWER_ON_0	BIT(1)
#define BIT_USER_EXT_CLK		BIT(2)
#define BIT_BOOT_FROM_ROM		BIT(31)
#define BIT_OTP_SIGNATURE_ERR_TALYN_MB		BIT(0)
#define BIT_OTP_HW_SECTION_DONE_TALYN_MB	BIT(2)

/* Newly provided defines from driver source */
#define JTAG_DEV_ID_SPARROW	(0x2632072f)
#define REVISION_ID_SPARROW_D0	(0x3)
#define WIL6210_MIN_MEM_SIZE (2 * 1024 * 1024UL)
#define WIL6210_MAX_MEM_SIZE (4 * 1024 * 1024UL)

/*
 * WIL6210_FW_HOST_OFF is the base offset of the firmware registers in the
 * BAR0 aperture. The driver subtracts this value from firmware register
 * addresses to obtain the BAR offset (see HOSTADDR).
 */
#define WIL6210_FW_HOST_OFF      (0x880000UL)
#define HOSTADDR(reg) ((reg) - WIL6210_FW_HOST_OFF)

/*
 * BAR0 size must be within the driver's expected range:
 * WIL6210_MIN_MEM_SIZE (2 MB) to WIL6210_MAX_MEM_SIZE (4 MB).
 * Set to 4 MB to match the maximum allowed and ensure all registers fit.
 */
#define BAR0_SIZE (4 * 1024 * 1024UL)

/* State structure */
struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    int num_bars;

    bool has_msi;
    bool has_msix;  /* kept for structural compatibility but unused */

    /* Shadow register array for BAR0 MMIO space */
    uint32_t *regs;
};

/* Forward declarations */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* PIO is not used by the driver; delete PIO handlers and ops */

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Clear all shadow registers */
    memset(s->regs, 0, BAR0_SIZE);

    /* Set initial values for identification registers using correct BAR offset */
    uint32_t idx_jtag = HOSTADDR(RGF_USER_JTAG_DEV_ID) / 4;
    uint32_t idx_rev = HOSTADDR(RGF_USER_REVISION_ID) / 4;
    s->regs[idx_jtag] = JTAG_DEV_ID_SPARROW;
    s->regs[idx_rev] = REVISION_ID_SPARROW_D0;

    /* Other registers remain zero; they will be written by driver. */
}

/* MMIO read handler: return value from shadow register array. */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t reg_index = addr / 4;
    uint64_t val = 0;

    /* Bounds check */
    if (addr >= BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read out of bounds: addr=0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return 0;
    }

    val = s->regs[reg_index];

    return val;
}

/* MMIO write handler: store to shadow register array. */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t reg_index = addr / 4;

    /* Bounds check */
    if (addr >= BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write out of bounds: addr=0x%" HWADDR_PRIx
                      " val=0x%" PRIx64 "\n", __func__, addr, val);
        return;
    }

    /* Simple store. Write-1-to-clear logic for interrupt registers can be
       added when required. */
    s->regs[reg_index] = (uint32_t)val;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Allocate shadow register array */
    s->regs = g_new0(uint32_t, BAR0_SIZE / 4);

    /* BAR 0: MMIO */
    s->num_bars = 1;
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_mmio_ops, s,
                          "wil6210-mmio", BAR0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[0]);

    /* Initialize MSI (up to 4 vectors, must be power of two for QEMU) */
    if (msi_init(pdev, 0, 4, false, false, errp)) {
        g_free(s->regs);
        return;
    }
    s->has_msi = true;
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
    g_free(s->regs);
}

/* Minimal VMState */
static const VMStateDescription vmstate_pcibase = {
    .name = "wil6210_pci",
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

static const TypeInfo pcibase_info = {
    .name = TYPE_PCIBASE_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIBaseState),
    .class_init = pcibase_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pcibase_register_types(void)
{
    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types)
