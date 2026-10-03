/*
 * QEMU PCI device model for AMD ACP70 SOF audio controller
 * Auto-generated based on Linux driver sound/soc/sof/amd/pci-acp70.c
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
#include "hw/hw.h"

#define TYPE_PCIBASE_DEVICE "snd_sof_amd_acp70_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID 0x1022
#define PCIBASE_DEVICE_ID 0x15E2
#define PCIBASE_CLASS_ID  0x0403

#define ACP70_FUTURE_REG_ACLK_0      0x1854
#define ACP70_REG_START              0x1240000
#define ACP70_REG_END                0x125C000
#define ACP71_PCI_ID                 0x71
#define ACP70_PCI_ID                 0x70
#define ACP72_PCI_ID                 0x72
#define ACP70_EXTERNAL_INTR_ENB      0x1A00
#define ACP70_SDW_MAX_MANAGER_COUNT  2
#define ACP70_EXT_INTR_STAT          0x1A0C
#define SDW_ACPI_ADDR_ACP70          5
#define ACP70_DSP_SW_INTR_BASE       0x1808
#define ACP70_DSP_FUSION_RUNSTALL    0x0644
#define ACP70_PGFSM_BASE             0x1024
#define ACP70_ERROR_STATUS           0x1A4C
#define ACP70_EXTERNAL_INTR_CNTL     0x1A04
#define ACP70_AXI2DAGB_SEM_0         0x1874
#define ACP7X_SW0_I2S_ERROR_REASON   0x18B4
#define ACP70_SRAM_PTE_OFFSET        0x03800000
#define ACP70_EXT_INTR_STAT1         0x1A10

#define PCIBASE_BAR_INDEX_REGS 0
#define PCIBASE_BAR_REGS_SIZE  (ACP70_REG_END - ACP70_REG_START)

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
    

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t ext_intr_enb;
    uint32_t ext_intr_cntl;
    uint32_t ext_intr_stat;
    uint32_t ext_intr_stat1;
    uint32_t acp_error_status;
    uint32_t dsp_sw_intr;
    uint32_t sw0_i2s_error_reason;
    uint32_t hw_semaphore;
    uint32_t fusion_runstall;
    uint32_t probe_reg;

    /* Simple IRQ state */
    uint32_t irq_status; /* mirror of ext_intr_stat/ext_intr_stat1 bits that are asserted */
    
    /* DMA Context */
    /* The provided driver source does not show any explicit DMA descriptor
     * or buffer programming against MMIO, so no device-initiated DMA logic
     * is implemented here.
     */

    /* Operational status flags */
    bool powered_on;
    /* State used to handle reset sequences */
    bool in_reset;
    /* Power management state (D0-D3) */
    uint8_t pm_state;
};

 

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Very simple interrupt model: raise INTx if any status bit is set
     * and corresponding enable is non-zero. The actual ACP interrupt
     * wiring is not visible in the provided driver fragment, but the
     * driver clearly uses ACP70_EXTERNAL_INTR_ENB/STAT/STAT1, so this
     * minimal model lets the guest see an interrupt when status bits
     * are non-zero and enables are set.
     */
    bool pending = false;

    if (s->ext_intr_enb && (s->ext_intr_stat || s->ext_intr_stat1)) {
        pending = true;
    }

    if (pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* No explicit MMIO-based DMA programming is visible in the provided
     * pci-acp70.c snippet; all higher-level audio/IPC code is in other
     * source files. Therefore, we do not implement any bus mastering DMA
     * here. This stub is intentionally left empty.
     */
    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* All register offsets from the driver are byte offsets within a
     * larger ACP register space. BAR0 is modeled starting at offset 0,
     * so we use the offsets directly.
     */
    switch (addr) {
    case ACP70_EXTERNAL_INTR_ENB:
        val = s->ext_intr_enb;
        break;
    case ACP70_EXTERNAL_INTR_CNTL:
        val = s->ext_intr_cntl;
        break;
    case ACP70_EXT_INTR_STAT:
        val = s->ext_intr_stat;
        break;
    case ACP70_EXT_INTR_STAT1:
        val = s->ext_intr_stat1;
        break;
    case ACP70_ERROR_STATUS:
        val = s->acp_error_status;
        break;
    case ACP70_DSP_SW_INTR_BASE:
        val = s->dsp_sw_intr;
        break;
    case ACP7X_SW0_I2S_ERROR_REASON:
        val = s->sw0_i2s_error_reason;
        break;
    case ACP70_AXI2DAGB_SEM_0:
        val = s->hw_semaphore;
        break;
    case ACP70_DSP_FUSION_RUNSTALL:
        val = s->fusion_runstall;
        break;
    case ACP70_FUTURE_REG_ACLK_0:
        /* The driver uses this as a probe register; reading back the
         * value that was written during probe is sufficient.
         */
        val = s->probe_reg;
        break;
    default:
        /* Unimplemented registers return 0. */
        val = 0;
        break;
    }

    /* Truncate/extend to the requested size; registers are naturally
     * 32-bit, so smaller sizes return the low bits.
     */
    if (size == 1) {
        val &= 0xff;
    } else if (size == 2) {
        val &= 0xffff;
    } else if (size == 4) {
        val &= 0xffffffffu;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, uint64_t addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t wval;

    /* Normalize value to 32 bits according to access size */
    if (size == 1) {
        wval = (uint8_t)val;
    } else if (size == 2) {
        wval = (uint16_t)val;
    } else {
        wval = (uint32_t)val;
    }

    switch (addr) {
    case ACP70_EXTERNAL_INTR_ENB:
        s->ext_intr_enb = wval;
        break;
    case ACP70_EXTERNAL_INTR_CNTL:
        s->ext_intr_cntl = wval;
        break;
    case ACP70_EXT_INTR_STAT:
        /* Status registers are typically write-1-to-clear. The provided
         * driver fragment does not show the exact clear behavior, but
         * using W1C is a safe minimal model.
         */
        s->ext_intr_stat &= ~wval;
        break;
    case ACP70_EXT_INTR_STAT1:
        s->ext_intr_stat1 &= ~wval;
        break;
    case ACP70_ERROR_STATUS:
        /* Error status is also commonly W1C; model it as such. */
        s->acp_error_status &= ~wval;
        break;
    case ACP70_DSP_SW_INTR_BASE:
        /* DSP software interrupt register; writing a non-zero value can
         * be interpreted as issuing an interrupt towards the DSP. For
         * host side we merely latch the value and optionally set a
         * status bit to let the driver observe completion.
         */
        s->dsp_sw_intr = wval;
        /* As a minimal behavior, set a bit in EXT_INTR_STAT to indicate
         * that an interrupt condition has occurred.
         */
        if (wval) {
            s->ext_intr_stat |= 0x1;
        }
        break;
    case ACP7X_SW0_I2S_ERROR_REASON:
        s->sw0_i2s_error_reason = wval;
        break;
    case ACP70_AXI2DAGB_SEM_0:
        /* Simple semaphore model: write takes ownership (set to 1),
         * writing 0 releases it.
         */
        s->hw_semaphore = wval;
        break;
    case ACP70_DSP_FUSION_RUNSTALL:
        /* RUNSTALL bit controls DSP run/stop. We just store the value. */
        s->fusion_runstall = wval;
        break;
    case ACP70_FUTURE_REG_ACLK_0:
        /* Probe register, just latch value. */
        s->probe_reg = wval;
        break;
    default:
        /* Ignore writes to unknown offsets. */
        break;
    }

    pcibase_update_irq(s);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Logic for Port I/O (Legacy support) - not used by the provided driver. */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* Logic for Port I/O (Legacy support) - not used by the provided driver. */
    (void)s;
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
    pci_device_reset(PCI_DEVICE(dev));

    /* Revert registers to power-on defaults */
    s->ext_intr_enb = 0;
    s->ext_intr_cntl = 0;
    s->ext_intr_stat = 0;
    s->ext_intr_stat1 = 0;
    s->acp_error_status = 0;
    s->dsp_sw_intr = 0;
    s->sw0_i2s_error_reason = 0;
    s->hw_semaphore = 0;
    s->fusion_runstall = 0;
    s->probe_reg = 0;
    s->irq_status = 0;
    s->powered_on = true;
    s->in_reset = false;
    s->pm_state = 0; /* D0 */

    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
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
    s->bar_info[0].index = PCIBASE_BAR_INDEX_REGS;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = PCIBASE_BAR_REGS_SIZE;
    s->bar_info[0].name  = "acp70-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize internal register state */
    s->ext_intr_enb = 0;
    s->ext_intr_cntl = 0;
    s->ext_intr_stat = 0;
    s->ext_intr_stat1 = 0;
    s->acp_error_status = 0;
    s->dsp_sw_intr = 0;
    s->sw0_i2s_error_reason = 0;
    s->hw_semaphore = 0;
    s->fusion_runstall = 0;
    s->probe_reg = 0;
    s->irq_status = 0;
    s->powered_on = true;
    s->in_reset = false;
    s->pm_state = 0; /* D0 */

      /* msi_init or msix_init calls */
      /* The driver snippet does not explicitly reference MSI/MSI-X,
       * so we leave them disabled for now.
       */
       /* Set DMA masks or ring buffer limits */
      /* Initialize internal hardware timers if used */
      /* Final state initialization before the device is 'live' */
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

    /* Free buffers, stop timers, etc. */
    (void)s;
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
