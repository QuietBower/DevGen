/* This template provides a robust skeleton for hardware emulation.
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

#define TYPE_PCIBASE_DEVICE "kyrofb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_ST 0x104a
#define PCI_DEVICE_ID_STG4000 0x0010
#define PCI_CLASS_ID PCI_CLASS_DISPLAY_VGA

/* Vendor-specific PCI configuration register offset for Core PLL Control */
#define CorePllControl 0x40

/* Register offsets */
#define REG_THREAD0ENABLE       0x0000
#define REG_THREAD1ENABLE       0x0004
#define REG_THREAD0RECOVER      0x0008
#define REG_THREAD1RECOVER      0x000C
#define REG_THREAD0STEP         0x0010
#define REG_THREAD1STEP         0x0014
#define REG_VIDEOINSTATUS       0x0018
#define REG_CORE2INSIGNSTART    0x001C
#define REG_CORE1RESETVECTOR    0x0020
#define REG_CORE1ROMOFFSET      0x0024
#define REG_CORE1ARBITERPRIORITY 0x0028
#define REG_VIDEOINCONTROL      0x002C
#define REG_VIDEOINREG0CTRLA    0x0030
#define REG_VIDEOINREG0CTRLB    0x0034
#define REG_VIDEOINREG1CTRLA    0x0038
#define REG_VIDEOINREG1CTRLB    0x003C
#define REG_THREAD0KICKER       0x0040
#define REG_CORE2INPUTSIGN      0x0044
#define REG_THREAD0PROGCTR      0x0048
#define REG_THREAD1PROGCTR      0x004C
#define REG_THREAD1KICKER       0x0050
#define REG_GPREGISTER1         0x0054
#define REG_GPREGISTER2         0x0058
#define REG_GPREGISTER3         0x005C
#define REG_GPREGISTER4         0x0060
#define REG_SERIALINTA          0x0064
#define REG_SOFTRESET           0x0080
#define REG_SERIALINTB          0x0084
#define REG_ROMELQV             0x011C
#define REG_WLWH                0x0120
#define REG_ROMELWL             0x0124
#define REG_INTSTATUS           0x012C
#define REG_INTMASK             0x0130
#define REG_INTCLEAR            0x0134
#define REG_ROMGPIOA            0x0150
#define REG_ROMGPIOB            0x0154
#define REG_ROMGPIOC            0x0158
#define REG_ROMGPIOD            0x015C
#define REG_AGPINTID            0x0168
#define REG_AGPINTCLASSCODE     0x016C
#define REG_AGPINTBIST          0x0170
#define REG_AGPINTSSID          0x0174
#define REG_AGPINTPMCSR         0x0178
#define REG_VGAFRAMEBUFBASE     0x017C
#define REG_VGANOTIFY           0x0180
#define REG_DACPLLMODE          0x0184
#define REG_CORE1VIDEOCLOCKDIV  0x0188
#define REG_AGPINTSTAT          0x018C
#define REG_TACTRLSTREAMBASE    0x0800
#define REG_TAOBJDATABASE       0x0804
#define REG_TAPTRDATABASE       0x0808
#define REG_TAREGIONDATABASE    0x080C
#define REG_TATAILPTRBASE       0x0810
#define REG_TAPTRREGIONSIZE     0x0814
#define REG_TACONFIGURATION     0x0818
#define REG_TAOBJDATASTARTADDR  0x081C
#define REG_TAOBJDATAENDADDR    0x0820
#define REG_TAXSCREENCLIP       0x0824
#define REG_TAYSCREENCLIP       0x0828
#define REG_TARHWCLAMP          0x082C
#define REG_TARHWCOMPARE        0x0830
#define REG_TASTART             0x0834
#define REG_TAOBJRESTART        0x0838
#define REG_TAPTRRESTART        0x083C
#define REG_TASTATUS1           0x0840
#define REG_TASTATUS2           0x0844
#define REG_TAINTSTATUS         0x0848
#define REG_TAINTMASK           0x084C
#define REG_TEXTUREADDRTHRESH   0x0BFC
#define REG_CORE1TRANSLATION    0x0C00
#define REG_TEXTUREADDREMAP     0x0C04
#define REG_RENDEROUTAGPREMAP   0x0C08
#define REG_3DREGIONREADTRANS   0x0C0C
#define REG_3DPTRREADTRANS      0x0C10
#define REG_3DPARAMREADTRANS    0x0C14
#define REG_3DREGIONREADTHRESH  0x0C18
#define REG_3DPTRREADTHRESH     0x0C1C
#define REG_3DPARAMREADTHRESH   0x0C20
#define REG_3DREGIONREADAGPREMAP 0x0C24
#define REG_3DPTRREADAGPREMAP   0x0C28
#define REG_3DPARAMREADAGPREMAP 0x0C2C
#define REG_ZBUFFERAGPREMAP     0x0C30
#define REG_TAINDEXAGPREMAP     0x0C34
#define REG_TAVERTEXAGPREMAP    0x0C38
#define REG_TAUVADDRTRANS       0x0C3C
#define REG_TATAILPTRCACHETRANS 0x0C40
#define REG_TAPARAMWRITETRANS   0x0C44
#define REG_TAPTRWRITETRANS     0x0C48
#define REG_TAPARAMWRITETHRESH  0x0C4C
#define REG_TAPTRWRITETHRESH    0x0C50
#define REG_TATAILPTRCACHEAGPRE 0x0C54
#define REG_TAPARAMWRITEAGPRE   0x0C58
#define REG_TAPTRWRITEAGPRE     0x0C5C
#define REG_SDRAMARBITERCONF    0x0C60
#define REG_SDRAMCONF0          0x0C64
#define REG_SDRAMCONF1          0x0C68
#define REG_SDRAMCONF2          0x0C6C
#define REG_SDRAMREFRESH        0x0C70
#define REG_SDRAMPOWERSTAT      0x0C74
#define REG_RAMBISTDATA         0x0C80
#define REG_RAMBISTCTRL         0x0C84
#define REG_FIFOBISTKEY         0x0C88
#define REG_RAMBISTRESULT       0x0C8C
#define REG_FIFOBISTRESULT      0x0C90
#define REG_SDRAMADDRSIGN       0x0CD4
#define REG_SDRAMDATASIGN       0x0CD8
#define REG_SDRAMSIGNCONF       0x0CDC
#define REG_ISPSIGNATURE        0x0CE4
#define REG_DACPRIMADDRESS      0x1400
#define REG_DACPRIMSIZE         0x1404
#define REG_DACCURSORADDR       0x1408
#define REG_DACCURSORCTRL       0x140C
#define REG_DACOVERLAYADDR      0x1410
#define REG_DACOVERLAYUADDR     0x1414
#define REG_DACOVERLAYVADDR     0x1418
#define REG_DACOVERLAYSIZE      0x141C
#define REG_DACOVERLAYVTDEC     0x1420
#define REG_DACVERTICALSCAL     0x1448
#define REG_DACPIXELFORMAT      0x144C
#define REG_DACHORIZONTALSCAL   0x1450
#define REG_DACVIDWINSTART      0x1454
#define REG_DACVIDWINEND        0x1458
#define REG_DACBLENDCTRL        0x145C
#define REG_DACHORTIM1          0x1460
#define REG_DACHORTIM2          0x1464
#define REG_DACHORTIM3          0x1468
#define REG_DACVERTIM1          0x146C
#define REG_DACVERTIM2          0x1470
#define REG_DACVERTIM3          0x1474
#define REG_DACBORDERCOLOR      0x1478
#define REG_DACSYNCCTRL         0x147C
#define REG_DACSTREAMCTRL       0x1480
#define REG_DACLUTADDRESS       0x1484
#define REG_DACLUTDATA          0x1488
#define REG_DACBURSTCTRL        0x148C
#define REG_DACCRCTRIGGER       0x1490
#define REG_DACCRCDONE          0x1494
#define REG_DACCRCRESULT1       0x1498
#define REG_DACCRCRESULT2       0x149C
#define REG_DACLINECOUNT        0x14A0
#define REG_DIGVIDPORTCTRL      0x1700
#define REG_DIGVIDPORTSTAT      0x1704
#define REG_SIZE                0x3004  /* covers up to 0x3003 */

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
    uint32_t regs[REG_SIZE / 4];

    /* Operational status flags – placeholder for future use */
};

/* MMIO handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= REG_SIZE) {
        return val;
    }

    /* Return a fixed non-zero signature for chip identification */
    if (addr == REG_ISPSIGNATURE && size == 4) {
        return 0x04000000; /* arbitrary valid-looking signature */
    }

    switch (size) {
    case 1:
        val = *(uint8_t *)((uint8_t *)s->regs + addr);
        break;
    case 2:
        val = *(uint16_t *)((uint8_t *)s->regs + addr);
        break;
    case 4:
        val = *(uint32_t *)((uint8_t *)s->regs + addr);
        break;
    case 8:
        val = *(uint64_t *)((uint8_t *)s->regs + addr);
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= REG_SIZE) {
        return;
    }

    switch (size) {
    case 1:
        *(uint8_t *)((uint8_t *)s->regs + addr) = (uint8_t)val;
        break;
    case 2:
        *(uint16_t *)((uint8_t *)s->regs + addr) = (uint16_t)val;
        break;
    case 4:
        *(uint32_t *)((uint8_t *)s->regs + addr) = (uint32_t)val;
        break;
    case 8:
        *(uint64_t *)((uint8_t *)s->regs + addr) = val;
        break;
    default:
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
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* Custom PCI config write to handle CorePllControl vendor-specific register */
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr,
                                 uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    /* Allow writes to the CorePllControl register to succeed
     * (required by SetCoreClockPLL initialization sequence).
     */
    if (addr == CorePllControl && len == 2) {
        /* Silently accept the write; the actual PLL frequency is irrelevant in QEMU. */
        return;
    }
    /* For all other config space registers, use the default handler. */
    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ST );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_STG4000 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, 0x104a);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x0010);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Override config write to handle vendor-specific registers */
    pdev->config_write = pcibase_config_write;

    /* BAR configuration: BAR0 framebuffer RAM, BAR1 MMIO registers */
    s->num_bars = 2;

    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_RAM;
    s->bar_info[0].size = 8 * MiB;
    s->bar_info[0].name = "kyrofb-framebuffer";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x4000;  /* covers up to 0x3004, power of 2 */
    s->bar_info[1].name = "kyrofb-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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
    .name = "kyrofb_pci",
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
