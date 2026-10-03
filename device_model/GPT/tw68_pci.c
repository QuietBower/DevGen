/*
 * QEMU PCI device model for Techwell TW68 video capture (behavioral stub)
 * Generated for driver linux-6.18/drivers/media/pci/tw68/tw68-core.c
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

#define TYPE_PCIBASE_DEVICE "tw68_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define TW68_HUE              0x254
#define TW68_IAGC             0x284
#define TW68_MISSCNT          0x298
#define TW68_CKILL            0x2A8
#define TW68_ACNTL            0x218
#define TW68_MISC2            0x2BC
#define TW68_OPFORM           0x20C
#define TW68_DMAC             0x000
#define TW68_SDT              0x270
#define TW68_COMB             0x2AC
#define TW68_GPOE             0x028
#define TW68_CLMD             0x2CC
#define TW68_VSCALE_LO        0x234
#define TW68_BRIGHT           0x240
#define TW68_CLMPL            0x290
#define TW68_GPIOC            0x024
#define TW68_LOOP             0x2B8
#define TW68_SDTR             0x274
#define TW68_CORING           0x260
#define TW68_VSHARP           0x25C
#define TW68_GPDATA           0x100
#define TW68_HDELAY_LO        0x228
#define TW68_SYNCT            0x294
#define TW68_SAT_U            0x24C
#define TW68_INFORM           0x208
#define TW68_HSCALE_LO        0x23C
#define TW68_HACTIVE_LO       0x22C
#define TW68_INTMASK          0x020
#define TW68_TESTREG          0x02C
#define TW68_SCALE_HI         0x238
#define TW68_MVSN             0x2C0
#define TW68_SHARP2           0x258
#define TW68_AGCGAIN          0x288
#define TW68_CNTRL2           0x268
#define TW68_CROP_HI          0x21C
#define TW68_CLMPG            0x280
#define TW68_CLCNTL1          0x2D4
#define TW68_HSYNC            0x210
#define TW68_CONTRAST         0x244
#define TW68_VACTIVE_LO       0x224
#define TW68_SHARPNESS        0x248
#define TW68_PCLAMP           0x29C
#define TW68_VCNTL1           0x2A0
#define TW68_MISC1            0x2B4
#define TW68_PEAKWT           0x28C
#define TW68_SAT_V            0x250
#define TW68_CNTRL1           0x230
#define TW68_VCNTL2           0x2A4
#define TW68_INTSTAT          0x01C
#define TW68_VBIC             0x010
#define TW68_CAP_CTL          0x040
#define TW68_IDCNTL           0x2D0
#define TW68_VDELAY_LO        0x220
#define TW68_LDLY             0x2B0
#define TW68_DMAP_SA          0x004
#define TW68_STATUS1          0x204

#define TW68_FIFO_EN          (1 << 1)
#define TW68_DMAP_EN          (1 << 0)
#define TW68_FFERR            (1 << 15)
#define TW68_DMAPERR          (1 << 5)
#define TW68_DMAPI            (1 << 1)
#define TW68_PABORT           (1 << 6)
#define TW68_VLOCK            (1 << 19)
#define TW68_FDMIS            (1 << 4)
#define TW68_FFOF             (1 << 3)
#define TW68_HLOCK            (1 << 22)

#define RISC_INT_BIT          0x08000000
#define RISC_JUMP             0xB0000000
#define RISC_INLINE           0xA0000000
#define RISC_SYNCO            0xC0000000
#define RISC_LINESTART        0x90000000
#define RISC_SYNCE            0xD0000000

#define ColorFormatRGB16      0x20
#define ColorFormatRGB24      0x10
#define ColorFormatRGB15      0x30
#define ColorFormatYUY2       0x40
#define ColorFormatRGB32      0x00

#define VideoFormatPALBDGHI   1
#define VideoFormatSECAM      2
#define VideoFormatNTSC       0
#define VideoFormatPAL60      6
#define VideoFormatPALNC      5
#define VideoFormatPALM       4

#define TW68_MAXBOARDS        16
#define UNSET                 (-1U)

#define TW68_VID_INTS   (TW68_FFERR | TW68_PABORT | TW68_DMAPERR | \
                         TW68_FFOF   | TW68_DMAPI)
#define TW68_VID_INTSX  (TW68_FDMIS | TW68_HLOCK | TW68_VLOCK)

#define TW68_INPUT_MAX        4

#define VideoClass            PCI_CLASS_MULTIMEDIA_VIDEO

/* Standards macros from driver are not needed in device model */

#define VENDOR_ID   0x1797
#define DEVICE_ID   0x6800
#define CLASS_ID    PCI_CLASS_MULTIMEDIA_VIDEO


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

    /* Interrupt related registers */
    uint32_t intmask;
    uint32_t intstat;

    /* Basic control/status shadow registers up to 0x400 */
    uint8_t regs[0x400];

    /* DMA state (RISC engine) */
    uint32_t dmac;
    uint32_t dmap_sa;

    QEMUTimer dma_timer;
};


static bool pcibase_msi_enabled(PCIBaseState *s)
{
    return msi_enabled(PCI_DEVICE(s));
}

/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->intstat & s->intmask) {
        if (pcibase_msi_enabled(s)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!pcibase_msi_enabled(s)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns.
 * The real card executes a RISC program starting at TW68_DMAP_SA and
 * generates DMAPI at the end. The driver does not inspect frame
 * contents for probe, so we only need to model completion + interrupt.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)pdev;
    (void)is_write;
    /* For now, we do not emulate actual data transfers.
     * Completion is driven by a timer callback below.
     */
}

static void pcibase_dma_complete(void *opaque)
{
    PCIBaseState *s = opaque;

    /* Set DMAPI bit in INTSTAT and signal interrupt if enabled */
    s->intstat |= TW68_DMAPI;
    /* Mirror into register shadow */
    *(uint32_t *)(s->regs + TW68_INTSTAT) = cpu_to_le32(s->intstat);
    pcibase_update_irq(s);
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= sizeof(s->regs)) {
        return 0;
    }

    /* Handle 32-bit special registers with little-endian semantics */
    switch (addr) {
    case TW68_INTMASK:
        if (size == 4) {
            return s->intmask;
        }
        break;
    case TW68_INTSTAT:
        if (size == 4) {
            return s->intstat;
        }
        break;
    case TW68_DMAC:
        if (size == 4) {
            return s->dmac;
        }
        break;
    case TW68_DMAP_SA:
        if (size == 4) {
            return s->dmap_sa;
        }
        break;
    default:
        break;
    }

    uint64_t val = 0;
    switch (size) {
    case 1:
        val = s->regs[addr];
        break;
    case 2:
        val = le16_to_cpu(*(uint16_t *)(s->regs + addr));
        break;
    case 4:
        val = le32_to_cpu(*(uint32_t *)(s->regs + addr));
        break;
    case 8:
        val = le64_to_cpu(*(uint64_t *)(s->regs + addr));
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= sizeof(s->regs)) {
        return;
    }

    /* Handle 32-bit registers used by the driver */
    if (size == 4) {
        switch (addr) {
        case TW68_INTMASK:
            s->intmask = (uint32_t)val;
            *(uint32_t *)(s->regs + TW68_INTMASK) = cpu_to_le32(s->intmask);
            pcibase_update_irq(s);
            return;
        case TW68_INTSTAT:
            /* Write-1-to-clear semantics */
            s->intstat &= ~((uint32_t)val);
            *(uint32_t *)(s->regs + TW68_INTSTAT) = cpu_to_le32(s->intstat);
            pcibase_update_irq(s);
            return;
        case TW68_DMAC:
            s->dmac = (uint32_t)val;
            *(uint32_t *)(s->regs + TW68_DMAC) = cpu_to_le32(s->dmac);
            /* Start or stop DMA based on DMAP_EN */
            if (s->dmac & TW68_DMAP_EN) {
                /* Schedule completion timer to emulate one frame */
                timer_mod(&s->dma_timer,
                          qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 30);
            } else {
                timer_del(&s->dma_timer);
            }
            return;
        case TW68_DMAP_SA:
            s->dmap_sa = (uint32_t)val;
            *(uint32_t *)(s->regs + TW68_DMAP_SA) = cpu_to_le32(s->dmap_sa);
            return;
        default:
            break;
        }
    }

    /* Generic shadowed register write */
    switch (size) {
    case 1:
        s->regs[addr] = (uint8_t)val;
        break;
    case 2:
        *(uint16_t *)(s->regs + addr) = cpu_to_le16((uint16_t)val);
        break;
    case 4:
        *(uint32_t *)(s->regs + addr) = cpu_to_le32((uint32_t)val);
        break;
    case 8:
        *(uint64_t *)(s->regs + addr) = cpu_to_le64((uint64_t)val);
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    /* The driver does not use PIO; leave unimplemented. */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
    /* The driver does not use PIO; leave unimplemented. */
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
    int i;

    pci_device_reset(PCI_DEVICE(dev));

    /* Clear shadow registers */
    memset(s->regs, 0, sizeof(s->regs));

    /* Power-on defaults derived from tw68_hw_init1() */
    s->intmask = 0;
    s->intstat = 0;
    s->dmac = 0x2000; /* driver writes this during init; safe default */
    s->dmap_sa = 0;

    *(uint32_t *)(s->regs + TW68_INTMASK) = cpu_to_le32(s->intmask);
    *(uint32_t *)(s->regs + TW68_INTSTAT) = cpu_to_le32(s->intstat);
    *(uint32_t *)(s->regs + TW68_DMAC) = cpu_to_le32(s->dmac);
    *(uint32_t *)(s->regs + TW68_DMAP_SA) = cpu_to_le32(s->dmap_sa);

    /* Initialize control bytes to the same values the driver writes in hw_init1
     * This makes subsequent driver writes idempotent but is not mandatory
     * for probe success; provided here to resemble hardware state.
     */
    s->regs[TW68_ACNTL]   = 0x42;
    s->regs[TW68_INFORM]  = 0x40;
    s->regs[TW68_OPFORM]  = 0x04;
    s->regs[TW68_HSYNC]   = 0x00;
    s->regs[TW68_CROP_HI] = 0x02;
    s->regs[TW68_VDELAY_LO] = 0x12;
    s->regs[TW68_VACTIVE_LO] = 0xf0;
    s->regs[TW68_HDELAY_LO] = 0x0f;
    s->regs[TW68_HACTIVE_LO] = 0xd0;
    s->regs[TW68_CNTRL1]  = 0xcd;
    s->regs[TW68_VSCALE_LO] = 0x00;
    s->regs[TW68_SCALE_HI] = 0x11;
    s->regs[TW68_HSCALE_LO] = 0x00;
    s->regs[TW68_BRIGHT] = 0x00;
    s->regs[TW68_CONTRAST] = 0x5c;
    s->regs[TW68_SHARPNESS] = 0x51;
    s->regs[TW68_SAT_U] = 0x80;
    s->regs[TW68_SAT_V] = 0x80;
    s->regs[TW68_HUE] = 0x00;
    s->regs[TW68_SHARP2] = 0x53;
    s->regs[TW68_VSHARP] = 0x80;
    s->regs[TW68_CORING] = 0x44;
    s->regs[TW68_CNTRL2] = 0x00;
    s->regs[TW68_SDT] = 0x07;
    s->regs[TW68_SDTR] = 0x7f;
    s->regs[TW68_CLMPG] = 0x50;
    s->regs[TW68_IAGC] = 0x22;
    s->regs[TW68_AGCGAIN] = 0xf0;
    s->regs[TW68_PEAKWT] = 0xd8;
    s->regs[TW68_CLMPL] = 0x3c;
    s->regs[TW68_SYNCT] = 0x30;
    s->regs[TW68_MISSCNT] = 0x44;
    s->regs[TW68_PCLAMP] = 0x28;
    s->regs[TW68_VCNTL1] = 0x04;
    s->regs[TW68_VCNTL2] = 0x00;
    s->regs[TW68_CKILL] = 0x68;
    s->regs[TW68_COMB] = 0x44;
    s->regs[TW68_LDLY] = 0x30;
    s->regs[TW68_MISC1] = 0x14;
    s->regs[TW68_LOOP] = 0xa5;
    s->regs[TW68_MISC2] = 0xe0;
    s->regs[TW68_MVSN] = 0x00;
    s->regs[TW68_CLMD] = 0x05;
    s->regs[TW68_IDCNTL] = 0x00;
    s->regs[TW68_CLCNTL1] = 0x00;

    /* VBIC, CAP_CTL, TESTREG, GPIOC, GPOE, GPDATA */
    *(uint32_t *)(s->regs + TW68_VBIC) = cpu_to_le32(0x03);
    *(uint32_t *)(s->regs + TW68_CAP_CTL) = cpu_to_le32(0x03);
    *(uint32_t *)(s->regs + TW68_TESTREG) = cpu_to_le32(0x00);
    *(uint32_t *)(s->regs + TW68_GPIOC) = cpu_to_le32(0x00);
    *(uint32_t *)(s->regs + TW68_GPOE) = cpu_to_le32(0x0f);
    *(uint32_t *)(s->regs + TW68_GPDATA) = cpu_to_le32(0x00);

    /* Status1/MVSN used for tw68_enum_input, return no errors by default */
    s->regs[TW68_STATUS1] = 0x00;
    s->regs[TW68_MVSN] = 0x00;

    /* Clear any pending timers */
    timer_del(&s->dma_timer);

    /* Ensure config space interrupt pin is set */
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t *pci_conf = pdev->config;
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Clear BAR regions content - not strictly needed */
    for (i = 0; i < 6; ++i) {
        /* nothing */
    }
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    /* PCI requires BAR sizes to be a power of 2. */
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

    (void)s;

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

    /* BAR Initialization - driver only uses BAR0 MMIO */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000; /* minimal page-sized BAR */
    s->bar_info[0].name = "tw68-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize MSI capability (optional, driver works with INTx) */
    s->has_msi = (msi_init(pdev, 0, 1, true, false, errp) == 0);
    s->has_msix = false;

    /* Initialize DMA completion timer */
    timer_init_ms(&s->dma_timer, QEMU_CLOCK_VIRTUAL, pcibase_dma_complete, s);

    /* Initialize register state as at reset */
    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    timer_del(&s->dma_timer);

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
    .name = "tw68_pci",
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

