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


#define TYPE_PCIBASE_DEVICE "snd_sof_amd_acp70_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_AMD 0x1022
#define ACP_PCI_DEV_ID 0x15E2
#define PCI_CLASS_MULTIMEDIA_AUDIO 0x040100

#define BAR0 0
#define BAR0_SIZE (ACP70_REG_END - ACP70_REG_START)

#define ACP70_REG_START		0x1240000
#define ACP70_REG_END		0x125C000
#define ACP6X_EXTERNAL_INTR_CNTL	0x1A04
#define ACP6X_EXT_INTR_STAT		0x1A0C
#define ACP6X_DSP_SW_INTR_BASE		0x1808
#define ACP6X_AXI2DAGB_SEM_0		0x1874
#define ACP6X_DSP_FUSION_RUNSTALL	0x0644
#define ACP6X_SW0_I2S_ERROR_REASON	0x18B4
#define ACP6X_ERROR_STATUS		0x1A4C
#define ACP6X_EXTERNAL_INTR_ENB		0x1A00
#define ACP6X_SDW_MAX_MANAGER_COUNT	2
#define ACP6X_SRAM_PTE_OFFSET		0x03800000
#define ACP6X_PGFSM_BASE			0x1024
#define ACP6X_EXT_INTR_STAT1		0x1A10

#define ACP70_FUTURE_REG_ACLK_0		0x1854
#define FLAG_AMD_SOF			BIT(1)
#define FLAG_AMD_SOF_ONLY_DMIC		BIT(2)
#define SDW_ACPI_ADDR_ACP63		5
#define SOF_DBG_DSPLESS_MODE		BIT(15)
#define SND_SOF_SUSPEND_DELAY_MS	2000
#define FLAG_AMD_LEGACY_ONLY_DMIC	BIT(4)
#define FLAG_AMD_LEGACY			BIT(3)
#define SND_SOF_BARS			8
#define SOF_MAX_DSP_NUM_CORES		8

#define ACP70_EXTERNAL_INTR_CNTL		ACP6X_EXTERNAL_INTR_CNTL
#define SDW_ACPI_ADDR_ACP70			SDW_ACPI_ADDR_ACP63
#define ACP70_EXT_INTR_STAT			ACP6X_EXT_INTR_STAT
#define ACP70_DSP_SW_INTR_BASE		ACP6X_DSP_SW_INTR_BASE
#define ACP70_AXI2DAGB_SEM_0			ACP6X_AXI2DAGB_SEM_0
#define ACP70_DSP_FUSION_RUNSTALL		ACP6X_DSP_FUSION_RUNSTALL
#define ACP7X_SW0_I2S_ERROR_REASON		ACP6X_SW0_I2S_ERROR_REASON
#define ACP70_ERROR_STATUS			ACP6X_ERROR_STATUS
#define ACP70_EXTERNAL_INTR_ENB			ACP6X_EXTERNAL_INTR_ENB
#define ACP70_SDW_MAX_MANAGER_COUNT		ACP6X_SDW_MAX_MANAGER_COUNT
#define ACP70_SRAM_PTE_OFFSET			ACP6X_SRAM_PTE_OFFSET
#define ACP70_PGFSM_BASE				ACP6X_PGFSM_BASE
#define ACP70_EXT_INTR_STAT1			ACP6X_EXT_INTR_STAT1

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

/*
 * Minimal shadow structure for ACP70 MMIO registers used by the driver.
 * Not all registers are included; only those referenced in the driver source.
 */
typedef struct {
    uint32_t ext_intr_enb;
    uint32_t ext_intr_cntl;
    uint32_t ext_intr_stat;
    uint32_t ext_intr_stat1;
    uint32_t error_status;
    uint32_t sw0_i2s_error_reason;
    uint32_t fusion_dsp;
    uint32_t axi2dagb_sem_0;
    uint32_t future_reg_aclk_0;
    uint32_t dsp_sw_intr_base;
    uint32_t pgfsm_base;
} ACP70MMIORegs;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t irq_status;
    uint32_t irq_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    ACP70MMIORegs regs;

    /* DMA Context (not used by this driver) */
    /* Status flags */
    bool dsp_running;

    /* State used to handle reset sequences */
    bool in_reset;

    /* Power management state (not needed) */

    /* Additional info (not needed) */
};



/* Device-initiated DMA logic based on driver access patterns (not used) */

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* No defined interrupt logic in provided driver sources */
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0x1A00: /* ACP70_EXTERNAL_INTR_ENB */
        val = s->regs.ext_intr_enb;
        break;
    case 0x1A04: /* ACP70_EXTERNAL_INTR_CNTL */
        val = s->regs.ext_intr_cntl;
        break;
    case 0x1A0C: /* ACP70_EXT_INTR_STAT */
        val = s->regs.ext_intr_stat;
        break;
    case 0x1A10: /* ACP70_EXT_INTR_STAT1 */
        val = s->regs.ext_intr_stat1;
        break;
    case 0x1A4C: /* ACP70_ERROR_STATUS */
        val = s->regs.error_status;
        break;
    case 0x18B4: /* ACP7X_SW0_I2S_ERROR_REASON */
        val = s->regs.sw0_i2s_error_reason;
        break;
    case 0x0644: /* ACP70_DSP_FUSION_RUNSTALL */
        val = s->regs.fusion_dsp;
        break;
    case 0x1874: /* ACP70_AXI2DAGB_SEM_0 */
        val = s->regs.axi2dagb_sem_0;
        break;
    case 0x1854: /* ACP70_FUTURE_REG_ACLK_0 */
        val = s->regs.future_reg_aclk_0;
        break;
    case 0x1808: /* ACP70_DSP_SW_INTR_BASE */
        val = s->regs.dsp_sw_intr_base;
        break;
    case 0x1024: /* ACP70_PGFSM_BASE */
        val = s->regs.pgfsm_base;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read from unknown addr 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0x1A00: /* ACP70_EXTERNAL_INTR_ENB */
        s->regs.ext_intr_enb = val;
        break;
    case 0x1A04: /* ACP70_EXTERNAL_INTR_CNTL */
        s->regs.ext_intr_cntl = val;
        break;
    case 0x1A0C: /* ACP70_EXT_INTR_STAT */
        s->regs.ext_intr_stat = val;
        break;
    case 0x1A10: /* ACP70_EXT_INTR_STAT1 */
        s->regs.ext_intr_stat1 = val;
        break;
    case 0x1A4C: /* ACP70_ERROR_STATUS */
        s->regs.error_status = val;
        break;
    case 0x18B4: /* ACP7X_SW0_I2S_ERROR_REASON */
        s->regs.sw0_i2s_error_reason = val;
        break;
    case 0x0644: /* ACP70_DSP_FUSION_RUNSTALL */
        s->regs.fusion_dsp = val;
        break;
    case 0x1874: /* ACP70_AXI2DAGB_SEM_0 */
        s->regs.axi2dagb_sem_0 = val;
        break;
    case 0x1854: /* ACP70_FUTURE_REG_ACLK_0 */
        s->regs.future_reg_aclk_0 = val;
        break;
    case 0x1808: /* ACP70_DSP_SW_INTR_BASE */
        s->regs.dsp_sw_intr_base = val;
        break;
    case 0x1024: /* ACP70_PGFSM_BASE */
        s->regs.pgfsm_base = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write to unknown addr 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* PIO read logic (not used) */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* PIO write logic (not used) */
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

    memset(&s->regs, 0, sizeof(s->regs));
    /* Set probe flag to indicate SOF support */
    s->regs.future_reg_aclk_0 = FLAG_AMD_SOF;
    s->dsp_running = false;
    s->in_reset = false;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1022 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x15E2 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x040100 );
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
    s->bar_info[0] = (BARInfo) {
        .index = BAR0,
        .type = BAR_TYPE_MMIO,
        .size = BAR0_SIZE,
        .name = "acp70-mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Enable MSI interrupts */
    s->has_msi = true;
    s->has_msix = false;
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }

    /* Initialize register defaults (reset state) */
    memset(&s->regs, 0, sizeof(s->regs));
    s->dsp_running = false;
    s->in_reset = false;
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

    /* No additional uninitialization needed */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_sof_amd_acp70_pci",
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
