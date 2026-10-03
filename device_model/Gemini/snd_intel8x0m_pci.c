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


#define TYPE_PCIBASE_DEVICE "snd_intel8x0m_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_INTEL_82801AA_6 0x2416

#define ICH_MAX_FRAGS		32

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
    uint32_t int_sta_reg;
    uint32_t int_sta_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t glob_cnt;
    uint32_t glob_sta;
    uint32_t acc_sema;

    /* DMA Context */
    struct {
        uint32_t bdbar;
        uint8_t civ;
        uint8_t lvi;
        uint8_t sr;
        uint16_t picb;
        uint8_t piv;
        uint8_t cr;
    } ichd[2];
};

#define ICH_REG_GLOB_CNT 0x3c
#define ICH_TRIE 0x00000040
#define ICH_SRIE 0x00000020
#define ICH_PRIE 0x00000010
#define ICH_ACLINK 0x00000008
#define ICH_AC97WARM 0x00000004
#define ICH_AC97COLD 0x00000002
#define ICH_GIE 0x00000001

#define ICH_REG_GLOB_STA 0x40
#define ICH_TRI 0x20000000
#define ICH_TCR 0x10000000
#define ICH_BCS 0x08000000
#define ICH_SPINT 0x04000000
#define ICH_P2INT 0x02000000
#define ICH_M2INT 0x01000000
#define ICH_SAMPLE_CAP 0x00c00000
#define ICH_MULTICHAN_CAP 0x00300000
#define ICH_MD3 0x00020000
#define ICH_AD3 0x00010000
#define ICH_RCS 0x00008000
#define ICH_BIT3 0x00004000
#define ICH_BIT2 0x00002000
#define ICH_BIT1 0x00001000
#define ICH_SRI 0x00000800
#define ICH_PRI 0x00000400
#define ICH_SCR 0x00000200
#define ICH_PCR 0x00000100
#define ICH_MCINT 0x00000080
#define ICH_POINT 0x00000040
#define ICH_PIINT 0x00000020
#define ICH_NVSPINT 0x00000010
#define ICH_MOINT 0x00000004
#define ICH_MIINT 0x00000002
#define ICH_GSCI 0x00000001

#define ICH_REG_ACC_SEMA 0x44
#define ICH_CAS 0x01

#define ICH_REG_LVI_MASK 0x1f
#define ICH_FIFOE 0x10
#define ICH_BCIS 0x08
#define ICH_LVBCI 0x04
#define ICH_CELV 0x02
#define ICH_DCH 0x01

#define ICH_REG_PIV_MASK 0x1f
#define ICH_IOCE 0x10
#define ICH_FEIE 0x08
#define ICH_LVBIE 0x04
#define ICH_RESETREGS 0x02
#define ICH_STARTBM 0x01

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->glob_sta & (ICH_MIINT | ICH_MOINT)) != 0;
    pci_set_irq(pdev, level);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool raise = false;
    
    for (int ch = 0; ch < 2; ch++) {
        if (!(s->ichd[ch].cr & ICH_STARTBM)) {
            continue;
        }
        
        while (s->ichd[ch].civ != s->ichd[ch].lvi) {
            uint32_t desc_addr = s->ichd[ch].bdbar + s->ichd[ch].civ * 8;
            uint32_t buf_addr = 0, buf_ctrl = 0;
            
            pci_dma_read(pdev, desc_addr, &buf_addr, 4);
            pci_dma_read(pdev, desc_addr + 4, &buf_ctrl, 4);
            
            buf_ctrl = le32_to_cpu(buf_ctrl);
            
            s->ichd[ch].civ = (s->ichd[ch].civ + 1) & ICH_REG_LVI_MASK;
            
            if (buf_ctrl & 0x80000000) { /* IOC bit */
                s->ichd[ch].sr |= ICH_BCIS;
                if (s->ichd[ch].cr & ICH_IOCE) {
                    s->glob_sta |= (ch == 0) ? ICH_MIINT : ICH_MOINT;
                    raise = true;
                }
            }
        }
    }
    
    if (raise) {
        pcibase_update_irq(s);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    
    switch (addr) {
        case ICH_REG_GLOB_CNT:
            val = s->glob_cnt;
            break;
        case ICH_REG_GLOB_STA:
            val = s->glob_sta;
            break;
        case ICH_REG_ACC_SEMA:
            val = s->acc_sema;
            break;
        default:
            if (addr >= 0x00 && addr < 0x20) {
                int ch = (addr >= 0x10) ? 1 : 0;
                hwaddr offset = addr & 0x0F;
                switch (offset) {
                    case 0x00: val = s->ichd[ch].bdbar; break;
                    case 0x04: val = s->ichd[ch].civ; break;
                    case 0x05: val = s->ichd[ch].lvi; break;
                    case 0x06: val = s->ichd[ch].sr; break;
                    case 0x08: val = s->ichd[ch].picb; break;
                    case 0x0A: val = s->ichd[ch].piv; break;
                    case 0x0B: val = s->ichd[ch].cr; break;
                }
            }
            break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    switch (addr) {
        case ICH_REG_GLOB_CNT:
            s->glob_cnt = val & ~(ICH_AC97COLD | ICH_AC97WARM);
            break;
        case ICH_REG_GLOB_STA:
            s->glob_sta &= ~(val & (ICH_RCS | ICH_MIINT | ICH_MOINT));
            /* Re-evaluate DMA in case new descriptors were added before ACK */
            pcibase_do_dma(s, true);
            pcibase_update_irq(s);
            break;
        case ICH_REG_ACC_SEMA:
            s->acc_sema = val;
            break;
        default:
            if (addr >= 0x00 && addr < 0x20) {
                int ch = (addr >= 0x10) ? 1 : 0;
                hwaddr offset = addr & 0x0F;
                switch (offset) {
                    case 0x00:
                        s->ichd[ch].bdbar = val;
                        break;
                    case 0x05:
                        s->ichd[ch].lvi = val & ICH_REG_LVI_MASK;
                        break;
                    case 0x06:
                        s->ichd[ch].sr &= ~(val & (ICH_FIFOE | ICH_BCIS | ICH_LVBCI));
                        break;
                    case 0x0B:
                        s->ichd[ch].cr = val;
                        if (val & ICH_RESETREGS) {
                            s->ichd[ch].civ = 0;
                            s->ichd[ch].lvi = 0;
                            s->ichd[ch].picb = 0;
                            s->ichd[ch].sr = ICH_DCH;
                            s->ichd[ch].cr = 0;
                        } else if (val & ICH_STARTBM) {
                            s->ichd[ch].sr &= ~ICH_DCH;
                            pcibase_do_dma(s, true);
                        } else {
                            s->ichd[ch].sr |= ICH_DCH;
                        }
                        break;
                }
            }
            break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* AC97 Codec Registers. Return valid values to satisfy snd_ac97_mixer probe. */
    switch (addr & 0x7f) {
        case 0x00: return 0x0001; /* AC97_RESET */
        case 0x26: return 0x000f; /* AC97_POWERDOWN */
        case 0x28: return 0x0201; /* AC97_EXTENDED_ID */
        case 0x3c: return 0x0001; /* AC97_EXTENDED_MODEM_ID */
        case 0x7c: return 0x8086; /* AC97_VENDOR_ID1 */
        case 0x7e: return 0x1040; /* AC97_VENDOR_ID2 */
        default: return 0;
    }
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* AC97 Codec Writes. Ignored in this model. */
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
    
    s->glob_cnt = 0;
    s->glob_sta = ICH_PCR | ICH_SCR | ICH_TCR;
    s->acc_sema = 0;
    
    for (int i = 0; i < 2; i++) {
        s->ichd[i].bdbar = 0;
        s->ichd[i].civ = 0;
        s->ichd[i].lvi = 0;
        s->ichd[i].sr = ICH_DCH;
        s->ichd[i].picb = 0;
        s->ichd[i].piv = 0;
        s->ichd[i].cr = 0;
    }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_INTEL_82801AA_6 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0401 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_intel8x0m_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_instance_init(Object *obj)
{
    PCIBaseState *s = PCIBASE_DEVICE(obj);
    
    /* 
     * Driver maps BAR 0/2 for AC97 and BAR 1/3 for BM.
     * By providing BAR 0 as PIO and BAR 1 as MMIO, we cleanly separate
     * the two address spaces into pcibase_pio_ops and pcibase_mmio_ops.
     */
    s->num_bars = 2;
    
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 256;
    s->bar_info[0].name = "ac97-pio";
    
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 256;
    s->bar_info[1].name = "bm-mmio";
}

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
        .instance_init = pcibase_instance_init,
        .class_init = pcibase_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);
