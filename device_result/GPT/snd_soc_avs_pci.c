/*
 * QEMU PCI device model for Intel AVS (behavioral phase).
 * Generated based on Linux driver sound/soc/intel/avs/core.c
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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "snd_soc_avs_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define AVS_ADSP_GEN_BASE               0x0
#define SKL_ADSP_IPC_BASE               0x40
#define CNL_ADSP_IPC_BASE               0xC0
#define MTL_DWICTL_BASE                 0x1800
#define MTL_REG_HfIPC_BASE              0x0  /* base not explicitly defined in core.c */

#define AZX_PCIREG_PGCTL                0x44
#define AZX_PCIREG_CGCTL                0x48

#define SKL_ADSP_SRAM_BASE_OFFSET       0x8000
#define SKL_ADSP_SRAM_WINDOW_SIZE       0x2000
#define APL_ADSP_SRAM_BASE_OFFSET       0x80000
#define APL_ADSP_SRAM_WINDOW_SIZE       0x20000
#define MTL_ADSP_SRAM_BASE_OFFSET       0x180000
#define MTL_ADSP_SRAM_WINDOW_SIZE       0x8000

#define SKL_ADSP_REG_HIPCT              (SKL_ADSP_IPC_BASE + 0x00)
#define SKL_ADSP_REG_HIPCTE             (SKL_ADSP_IPC_BASE + 0x04)
#define SKL_ADSP_REG_HIPCI              (SKL_ADSP_IPC_BASE + 0x08)
#define SKL_ADSP_REG_HIPCIE             (SKL_ADSP_IPC_BASE + 0x0C)
#define SKL_ADSP_REG_HIPCCTL            (SKL_ADSP_IPC_BASE + 0x10)

#define CNL_ADSP_REG_HIPCTDR            (CNL_ADSP_IPC_BASE + 0x00)
#define CNL_ADSP_REG_HIPCTDA            (CNL_ADSP_IPC_BASE + 0x04)
#define CNL_ADSP_REG_HIPCTDD            (CNL_ADSP_IPC_BASE + 0x08)
#define CNL_ADSP_REG_HIPCIDR            (CNL_ADSP_IPC_BASE + 0x10)
#define CNL_ADSP_REG_HIPCIDD            (CNL_ADSP_IPC_BASE + 0x18)
#define CNL_ADSP_REG_HIPCIDA            (CNL_ADSP_IPC_BASE + 0x14)
#define CNL_ADSP_REG_HIPCCTL            (CNL_ADSP_IPC_BASE + 0x28)

#define MTL_REG_HfIPCxTDR               (MTL_REG_HfIPC_BASE + 0x200)
#define MTL_REG_HfIPCxIDR               (MTL_REG_HfIPC_BASE + 0x210)
#define MTL_REG_HfIPCxIDA               (MTL_REG_HfIPC_BASE + 0x214)
#define MTL_REG_HfIPCxCTL               (MTL_REG_HfIPC_BASE + 0x228)
#define MTL_REG_HfIPCxIDD               (MTL_REG_HfIPC_BASE + 0x380)

#define AVS_ADSP_REG_ADSPCS             (AVS_ADSP_GEN_BASE + 0x04)
#define AVS_ADSP_REG_ADSPIC             (AVS_ADSP_GEN_BASE + 0x08)
#define AVS_ADSP_REG_ADSPIS             (AVS_ADSP_GEN_BASE + 0x0C)

#define AVS_ADSP_ADSPIC_IPC             BIT(0)
#define AVS_ADSP_ADSPIC_CLDMA           BIT(1)
#define AVS_ADSP_ADSPIS_IPC             BIT(0)
#define AVS_ADSP_ADSPIS_CLDMA           BIT(1)

#define AVS_ADSPCS_INTERVAL_US          500
#define AVS_ADSPCS_TIMEOUT_US           10000
#define AVS_ADSPCS_CRST_MASK(cm)        (cm)
#define AVS_ADSPCS_CSTALL_MASK(cm)      ((cm) << 8)
#define AVS_ADSPCS_SPA_MASK(cm)         ((cm) << 16)
#define AVS_ADSPCS_CPA_MASK(cm)         ((cm) << 24)

#define AVS_MAILBOX_SIZE                SZ_4K
#define AVS_WINDOW_CHUNK_SIZE           SZ_4K

#define AVS_FW_REGS_WINDOW              0
#define AVS_FW_REGS_SIZE                AVS_WINDOW_CHUNK_SIZE
#define AVS_DEBUG_WINDOW                2
#define AVS_UPLINK_WINDOW               AVS_FW_REGS_WINDOW
#define AVS_DOWNLINK_WINDOW             1

#define AVS_ADSP_HIPCCTL_BUSY           BIT(0)
#define AVS_ADSP_HIPCCTL_DONE           BIT(1)

#define SKL_ADSP_HIPCI_BUSY             BIT(31)
#define SKL_ADSP_HIPCT_BUSY             BIT(31)
#define SKL_ADSP_HIPCIE_DONE            BIT(30)

#define CNL_ADSP_HIPCIDR_BUSY           BIT(31)
#define CNL_ADSP_HIPCTDR_BUSY           BIT(31)
#define CNL_ADSP_HIPCTDA_DONE           BIT(31)
#define CNL_ADSP_HIPCIDA_DONE           BIT(31)

#define MTL_HfIPCxIDR_BUSY              BIT(31)
#define MTL_HfIPCxTDR_BUSY              BIT(31)
#define MTL_HfIPCxIDA_DONE              BIT(31)

#define MTL_DWICTL_REG_INTENL           (MTL_DWICTL_BASE + 0x0)
#define MTL_DWICTL_REG_FINALSTATUSL     (MTL_DWICTL_BASE + 0x30)

#define LNL_REG_HfDFR(x)                (0x160200 + (x) * 0x8)

#define AVS_PLATATTR_CLDMA              BIT_ULL(0)
#define AVS_PLATATTR_IMR                BIT_ULL(1)
#define AVS_PLATATTR_ACE                BIT_ULL(2)
#define AVS_PLATATTR_ALTHDA             BIT_ULL(3)

#define AVS_MAIN_CORE_MASK              BIT(0)

#define AVS_CHANNELS_MAX                16
#define AVS_COEFF_CHANNELS_MAX          8

#define AVS_BASEFW_MOD_ID               0
#define AVS_BASEFW_INST_ID              0
#define AVS_PROBE_INST_ID               0

#define AVS_ADSP_ADSPIC                 AVS_ADSP_REG_ADSPIC
#define AVS_ADSP_ADSPIS                 AVS_ADSP_REG_ADSPIS

#define AVS_FW_REG_BASE(adev)           ((adev)->spec->hipc->sts_offset)
#define AVS_FW_REG_STATUS(adev)         (AVS_FW_REG_BASE(adev) + 0x0)
#define AVS_FW_REG_ERROR(adev)          (AVS_FW_REG_BASE(adev) + 0x4)

/* PCI IDs: first entry of avs_ids[] uses INTEL, HDA_SKL_LP */
#define AVS_PCI_VENDOR_ID               PCI_VENDOR_ID_INTEL
#define AVS_PCI_DEVICE_ID               0x9d70
#define AVS_PCI_CLASS_ID                PCI_CLASS_MULTIMEDIA_AUDIO


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
    /* Minimal state to satisfy accesses seen in core.c and HDA helpers */

    /* PCI config shadow for PGCTL and CGCTL referenced via pci_read/write_config */
    uint32_t pgctl;
    uint32_t cgctl;

    /* Simple interrupt related shadow: INTCTL and INTSTS */
    uint32_t intctl;
    uint32_t intsts;

    /* RIRB status register shadow used with RIRBSTS */
    uint8_t rirbsts;

    /* ADSP generic control/status/irq registers */
    uint32_t adspcs;
    uint32_t adspic;
    uint32_t adspis;

    /* IPC-related registers (we only implement a very small subset) */
    uint32_t hipci;
    uint32_t hipcie;
    uint32_t hipct;
    uint32_t hipcte;
    uint32_t hipcctl;

    /* A tiny SRAM/IPC status area so that AVS_FW_REG_STATUS/ERROR can be read */
    uint8_t sram[SKL_ADSP_SRAM_BASE_OFFSET + 0x10];
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Very simple model: if any interrupt enable bit is set in INTCTL, and
     * INTSTS is non-zero, assert the legacy INTx line.
     *
     * core.c specifically checks AZX_INT_GLOBAL_EN in INTSTS/INTCTL via
     * snd_hdac_chip_updatel(bus, INTCTL, AZX_INT_GLOBAL_EN, ...).
     * We don't model individual stream interrupts, only the fact that
     * an interrupt is pending or not.
     */
    if (s->intsts && (s->intctl & 0x1)) { /* treat bit0 as AZX_INT_GLOBAL_EN */
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 1);
        } else {
            msi_notify(pdev, 0);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}


/* Device-initiated DMA logic based on driver access patterns
 *
 * The provided core.c file does not expose any device-initiated DMA register
 * programming for AVS itself (all DMA is host-driven via standard HDA flows
 * implemented in generic HDA code). Therefore we keep this unimplemented.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)pdev;
    (void)is_write;
}

/* Helper to read 32-bit little-endian from our SRAM area safely */
static uint32_t pcibase_read_sram32(PCIBaseState *s, hwaddr offset)
{
    if (offset + 4 > sizeof(s->sram)) {
        return 0;
    }
    return le32_to_cpu(*(uint32_t *)(s->sram + offset));
}

static void pcibase_write_sram32(PCIBaseState *s, hwaddr offset, uint32_t val)
{
    if (offset + 4 > sizeof(s->sram)) {
        return;
    }
    *(uint32_t *)(s->sram + offset) = cpu_to_le32(val);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Only little-endian accesses are expected. Allow 1/2/4/8-byte loads but
     * we only implement 32-bit semantics for the few registers we care about.
     */

    /* INTSTS and INTCTL are in the standard HDA register space. The exact
     * offsets are defined in generic HDA headers, which are not part of
     * core.c. Here we just implement a very small subset at heuristic
     * offsets 0x20/0x24 if accessed.
     */
    switch (addr) {
    case AVS_ADSP_REG_ADSPCS:
        val = s->adspcs;
        break;
    case AVS_ADSP_REG_ADSPIC:
        val = s->adspic;
        break;
    case AVS_ADSP_REG_ADSPIS:
        val = s->adspis;
        break;
    case SKL_ADSP_REG_HIPCI:
        val = s->hipci;
        break;
    case SKL_ADSP_REG_HIPCIE:
        val = s->hipcie;
        break;
    case SKL_ADSP_REG_HIPCT:
        val = s->hipct;
        break;
    case SKL_ADSP_REG_HIPCTE:
        val = s->hipcte;
        break;
    case SKL_ADSP_REG_HIPCCTL:
        val = s->hipcctl;
        break;
    default:
        /* SRAM/firmware status window used via AVS_FW_REG_STATUS/ERROR.
         * These macros compute addresses relative to sts_offset in avs_hipc_spec.
         * For the skl_hipc_spec, sts_offset == SKL_ADSP_SRAM_BASE_OFFSET.
         */
        if (addr >= SKL_ADSP_SRAM_BASE_OFFSET &&
            addr < SKL_ADSP_SRAM_BASE_OFFSET + sizeof(s->sram)) {
            hwaddr off = addr - SKL_ADSP_SRAM_BASE_OFFSET;
            if (size == 4) {
                val = pcibase_read_sram32(s, off);
            } else if (size == 1) {
                if (off < sizeof(s->sram)) {
                    val = s->sram[off];
                }
            } else if (size == 2) {
                if (off + 2 <= sizeof(s->sram)) {
                    val = le16_to_cpu(*(uint16_t *)(s->sram + off));
                }
            } else if (size == 8) {
                if (off + 8 <= sizeof(s->sram)) {
                    val = le64_to_cpu(*(uint64_t *)(s->sram + off));
                }
            }
            break;
        }
        /* For all other addresses, simply return 0. This is sufficient for
         * probe to continue as long as we do not trigger unexpected faults.
         */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v32 = (uint32_t)val;

    switch (addr) {
    case AVS_ADSP_REG_ADSPCS:
        /* Core.c only uses helper macros with timeouts around ADSPCS.
         * We just store the value and allow any bits.
         */
        s->adspcs = v32;
        break;
    case AVS_ADSP_REG_ADSPIC:
        /* Interrupt mask: just store. */
        s->adspic = v32;
        break;
    case AVS_ADSP_REG_ADSPIS:
        /* Interrupt status: core.c uses write-1-to-clear semantics for
         * other status-like registers; model the same here.
         */
        s->adspis &= ~v32;
        break;
    case SKL_ADSP_REG_HIPCCTL:
        s->hipcctl = v32;
        break;
    case SKL_ADSP_REG_HIPCI:
        /* Host-to-DSP IPC request. Mark busy bit and reflect data in HIPCT. */
        s->hipci = v32 | SKL_ADSP_HIPCI_BUSY;
        s->hipct = v32; /* mirror for simplicity */
        s->hipct |= SKL_ADSP_HIPCT_BUSY;
        /* Signal IPC interrupt towards host if enabled in ADSPIC. */
        if (s->adspic & AVS_ADSP_ADSPIC_IPC) {
            s->adspis |= AVS_ADSP_ADSPIS_IPC;
        }
        pcibase_update_irq(s);
        break;
    case SKL_ADSP_REG_HIPCT:
        /* Host may clear busy bit by writing; treat as direct store. */
        s->hipct = v32;
        break;
    case SKL_ADSP_REG_HIPCTE:
        s->hipcte = v32;
        break;
    case SKL_ADSP_REG_HIPCIE:
        /* Host writes to clear DONE or acknowledge; treat as W1C. */
        s->hipcie &= ~v32;
        break;
    default:
        if (addr >= SKL_ADSP_SRAM_BASE_OFFSET &&
            addr < SKL_ADSP_SRAM_BASE_OFFSET + sizeof(s->sram)) {
            hwaddr off = addr - SKL_ADSP_SRAM_BASE_OFFSET;
            if (size == 4) {
                pcibase_write_sram32(s, off, v32);
            } else if (size == 1) {
                if (off < sizeof(s->sram)) {
                    s->sram[off] = (uint8_t)val;
                }
            } else if (size == 2) {
                if (off + 2 <= sizeof(s->sram)) {
                    *(uint16_t *)(s->sram + off) = cpu_to_le16((uint16_t)val);
                }
            } else if (size == 8) {
                if (off + 8 <= sizeof(s->sram)) {
                    *(uint64_t *)(s->sram + off) = cpu_to_le64((uint64_t)val);
                }
            }
            break;
        }
        /* Other addresses are ignored. */
        break;
    }

    /* Some writes may affect IRQ state (e.g. clearing adspis). */
    pcibase_update_irq(s);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
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

    /* Reset shadow registers to a benign state expected by the driver. */
    s->pgctl = 0;
    s->cgctl = 0;
    s->intctl = 0;
    s->intsts = 0;
    s->rirbsts = 0;
    s->adspcs = 0;
    s->adspic = 0;
    s->adspis = 0;
    s->hipci = 0;
    s->hipcie = 0;
    s->hipct = 0;
    s->hipcte = 0;
    s->hipcctl = 0;
    memset(s->sram, 0, sizeof(s->sram));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  AVS_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  AVS_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, AVS_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: AVS core.c does not specify BAR sizes; use a single MMIO BAR with 64KB. */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x10000; /* conservative generic size; not driver-defined */
    s->bar_info[0].name = "avs-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No explicit MSI/MSI-X usage in avs core.c itself; leave disabled. */
    s->has_msi = false;
    s->has_msix = false;

    /* Initialize register shadows */
    pcibase_reset(DEVICE(pdev));
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_soc_avs_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(pgctl, PCIBaseState),
        VMSTATE_UINT32(cgctl, PCIBaseState),
        VMSTATE_UINT32(intctl, PCIBaseState),
        VMSTATE_UINT32(intsts, PCIBaseState),
        VMSTATE_UINT8(rirbsts, PCIBaseState),
        VMSTATE_UINT32(adspcs, PCIBaseState),
        VMSTATE_UINT32(adspic, PCIBaseState),
        VMSTATE_UINT32(adspis, PCIBaseState),
        VMSTATE_UINT32(hipci, PCIBaseState),
        VMSTATE_UINT32(hipcie, PCIBaseState),
        VMSTATE_UINT32(hipct, PCIBaseState),
        VMSTATE_UINT32(hipcte, PCIBaseState),
        VMSTATE_UINT32(hipcctl, PCIBaseState),
        VMSTATE_UINT8_ARRAY(sram, PCIBaseState, sizeof(((PCIBaseState *)0)->sram)),
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
