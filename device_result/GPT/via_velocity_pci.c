/*
 * QEMU PCI device model for VIA Velocity (structural + minimal behavioral)
 * This model only implements enough register behavior and interrupt logic
 * for the Linux via-velocity driver to probe and bind.
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

#define TYPE_PCIBASE_DEVICE "via_velocity_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VELOCITY_VENDOR_ID   0x1106
#define VELOCITY_DEVICE_ID   0x3119
#define VELOCITY_CLASS_ID    0x0200

#define VELOCITY_IO_SIZE     256

/* Commonly used MAC register offsets from via-velocity */
#define MAC_REG_PAR          0x00
#define MAC_REG_CR0_SET      0x08
#define MAC_REG_CR0_CLR      0x0C
#define MAC_REG_MAR          0x10
#define MAC_REG_TDCSR_SET    0x30
#define MAC_REG_TDCSR_CLR    0x34
#define MAC_REG_RDBASE_LO    0x38
#define MAC_REG_RDCSR_SET    0x32
#define MAC_REG_IMR          0x28
#define MAC_REG_FIFO_TEST0   0x60

/* Selected interrupt/status related bits */
#define CR0_GINTMSK1         0x02000000UL
#define IMR_PPTIM            0x00000002UL
#define IMR_PPRXIM           0x00000001UL
#define IMR_PTXIM            0x00000008UL
#define IMR_PRXIM            0x00000004UL
#define IMR_PWEIM            0x00040000UL
#define IMR_TXWB0IM          0x00000100UL
#define IMR_TXWB1IM          0x00000200UL
#define IMR_FLONIM           0x00000800UL
#define IMR_OVFIM            0x00001000UL
#define IMR_LSTEIM           0x00002000UL
#define IMR_LSTPEIM          0x00004000UL
#define IMR_SRCIM            0x00008000UL
#define IMR_TMR0IM           0x00010000UL
#define IMR_TMR1IM           0x00020000UL
#define IMR_MIBFIM           0x00200000UL
#define IMR_SHDNIM           0x00100000UL
#define IMR_TXSTLM           0x02000000UL

#define INT_MASK_DEF        (IMR_PPTIM|IMR_PPRXIM|IMR_PTXIM|IMR_PRXIM|\
                             IMR_PWEIM|IMR_TXWB0IM|IMR_TXWB1IM|IMR_FLONIM|\
                             IMR_OVFIM|IMR_LSTEIM|IMR_LSTPEIM|IMR_SRCIM|IMR_MIBFIM|\
                             IMR_SHDNIM|IMR_TMR1IM|IMR_TMR0IM|IMR_TXSTLM)

#define ISR_PPRXI            0x00000001UL
#define ISR_PPTXI            0x00000002UL
#define ISR_PRXI             0x00000004UL
#define ISR_PTXI             0x00000008UL
#define ISR_PTX0I            0x00000010UL
#define ISR_PTX1I            0x00000020UL
#define ISR_PTX2I            0x00000040UL
#define ISR_PTX3I            0x00000080UL
#define ISR_LSTEI            0x00002000UL
#define ISR_SRCI             0x00008000UL
#define ISR_PWEI             0x00040000UL
#define ISR_MIBFI            0x00200000UL
#define ISR_TXSTLI           0x02000000UL

#define CR0_STRT             0x00000001UL
#define CR0_STOP             0x00000002UL
#define CR0_RXON             0x00000004UL
#define CR0_TXON             0x00000008UL

#define VELOCITY_FLAGS_OPENED      0x00010000UL
#define VELOCITY_FLAGS_WOL_ENABLED 0x00080000UL

#define TX_QUEUE_NO          4
#define MAX_HW_MIB_COUNTER   32

#define VCAM_SIZE            64
#define MCAM_SIZE            64

#define PKT_BUF_SZ           1540

#define VELOCITY_MIN_MTU     64
#define VELOCITY_MAX_MTU     9000

#define VELOCITY_NAME        "via-velocity"


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
        uint8_t mac_par[6];
        uint32_t cr0;
        uint32_t isr;
        uint32_t imr;
        uint8_t regs_raw[VELOCITY_IO_SIZE];
    } regs;

    /* DMA Context */


    uint32_t status_flags;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Simple level-triggered interrupt: raise if any unmasked ISR bit set. */
    uint32_t pending = s->regs.isr & s->regs.imr;

    if (pending) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns
 * The driver uses bus mastering heavily, but we do not need to
 * implement real DMA transfers for probe/bind to succeed.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)pdev;
    (void)is_write;
}

/* Helpers to access shadow register space */
static uint8_t pcibase_reg_read8(PCIBaseState *s, hwaddr addr)
{
    if (addr < VELOCITY_IO_SIZE) {
        return s->regs.regs_raw[addr];
    }
    return 0xff;
}

static void pcibase_reg_write8(PCIBaseState *s, hwaddr addr, uint8_t val)
{
    if (addr < VELOCITY_IO_SIZE) {
        s->regs.regs_raw[addr] = val;
    }
}

static uint16_t pcibase_reg_read16(PCIBaseState *s, hwaddr addr)
{
    uint16_t v = 0xffff;
    if (addr + 1 < VELOCITY_IO_SIZE) {
        v = s->regs.regs_raw[addr];
        v |= ((uint16_t)s->regs.regs_raw[addr + 1]) << 8;
    }
    return v;
}

static void pcibase_reg_write16(PCIBaseState *s, hwaddr addr, uint16_t val)
{
    if (addr + 1 < VELOCITY_IO_SIZE) {
        s->regs.regs_raw[addr] = val & 0xff;
        s->regs.regs_raw[addr + 1] = (val >> 8) & 0xff;
    }
}

static uint32_t pcibase_reg_read32(PCIBaseState *s, hwaddr addr)
{
    uint32_t v = 0xffffffffU;
    if (addr + 3 < VELOCITY_IO_SIZE) {
        v  = (uint32_t)s->regs.regs_raw[addr];
        v |= (uint32_t)s->regs.regs_raw[addr + 1] << 8;
        v |= (uint32_t)s->regs.regs_raw[addr + 2] << 16;
        v |= (uint32_t)s->regs.regs_raw[addr + 3] << 24;
    }
    return v;
}

static void pcibase_reg_write32(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    if (addr + 3 < VELOCITY_IO_SIZE) {
        s->regs.regs_raw[addr] = val & 0xff;
        s->regs.regs_raw[addr + 1] = (val >> 8) & 0xff;
        s->regs.regs_raw[addr + 2] = (val >> 16) & 0xff;
        s->regs.regs_raw[addr + 3] = (val >> 24) & 0xff;
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (size) {
    case 1:
        val = pcibase_reg_read8(s, addr);
        break;
    case 2:
        val = pcibase_reg_read16(s, addr);
        break;
    case 4:
        /* Special cases for documented dword registers */
        if (addr == MAC_REG_CR0_SET || addr == MAC_REG_CR0_CLR) {
            /* CR0 set/clear registers are write-only in HW; driver polls
             * CR0 via readl(&regs->CR0Set) using DWORD_REG_BITS_IS_ON.
             * We mirror CR0 into regs.cr0 and return that value here.
             */
            val = s->regs.cr0;
        } else if (addr == MAC_REG_IMR) {
            val = s->regs.imr;
        } else if (addr == MAC_REG_MAR || addr == MAC_REG_PAR) {
            /* Fall back to raw space; PAR[0..5] are accessed byte-wise too. */
            val = pcibase_reg_read32(s, addr);
        } else {
            val = pcibase_reg_read32(s, addr);
        }
        break;
    default:
        /* Unsupported size, return all-ones like typical hardware */
        val = ~0ULL;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (size) {
    case 1:
        pcibase_reg_write8(s, addr, (uint8_t)val);
        break;
    case 2:
        pcibase_reg_write16(s, addr, (uint16_t)val);
        break;
    case 4:
        /* Handle control and interrupt registers with semantics visible
         * in the driver. Everything else is stored in the raw array.
         */
        if (addr == MAC_REG_CR0_SET) {
            /* Write-1-to-set bits in CR0 */
            uint32_t w = (uint32_t)val;
            s->regs.cr0 |= w;
            pcibase_reg_write32(s, MAC_REG_CR0_SET, s->regs.cr0);
        } else if (addr == MAC_REG_CR0_CLR) {
            /* Write-1-to-clear bits in CR0 */
            uint32_t w = (uint32_t)val;
            s->regs.cr0 &= ~w;
            pcibase_reg_write32(s, MAC_REG_CR0_CLR, s->regs.cr0);
        } else if (addr == MAC_REG_IMR) {
            /* Interrupt mask register is read/write */
            s->regs.imr = (uint32_t)val;
            pcibase_reg_write32(s, MAC_REG_IMR, s->regs.imr);
            s->intr_mask = s->regs.imr;
            pcibase_update_irq(s);
        } else if (addr == MAC_REG_PAR) {
            pcibase_reg_write32(s, addr, (uint32_t)val);
        } else if (addr >= MAC_REG_PAR && addr < MAC_REG_PAR + 6) {
            /* Individual MAC bytes written via writeb */
            pcibase_reg_write8(s, addr, (uint8_t)val);
        } else if (addr == MAC_REG_TDCSR_SET || addr == MAC_REG_TDCSR_CLR ||
                   addr == MAC_REG_RDBASE_LO || addr == MAC_REG_RDCSR_SET) {
            /* The driver writes these registers during ring setup, but
             * does not read them back in any critical way for probe.
             * Mirror them in raw space only.
             */
            pcibase_reg_write32(s, addr, (uint32_t)val);
        } else {
            pcibase_reg_write32(s, addr, (uint32_t)val);
        }
        break;
    default:
        break;
    }

    /* Any write which might affect interrupts should update IRQ line. */
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

    memset(s->regs.regs_raw, 0, sizeof(s->regs.regs_raw));
    memset(s->regs.mac_par, 0, sizeof(s->regs.mac_par));

    s->regs.cr0 = 0;
    s->regs.isr = 0;
    s->regs.imr = INT_MASK_DEF;
    s->intr_status = 0;
    s->intr_mask = INT_MASK_DEF;
    s->status_flags = 0;

    /* Make IMR shadow visible in raw space at MAC_REG_IMR */
    pcibase_reg_write32(s, MAC_REG_IMR, s->regs.imr);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VELOCITY_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  VELOCITY_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, VELOCITY_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization
     * Driver expectation:
     *  - BAR0 : IORESOURCE_IO
     *  - BAR1 : memory resource used with ioremap()
     * For simplicity we expose the emulated register space as MMIO in BAR1,
     * and we still define BAR0 as I/O but leave it unused.
     */
    s->num_bars = 2;

    /* BAR0: I/O space, minimal size */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "via-velocity-io";

    /* BAR1: MMIO register block used by driver via ioremap() */
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = VELOCITY_IO_SIZE;
    s->bar_info[1].name = "via-velocity-mmio";

    for (int i = 2; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    (void)errp;

    memset(s->regs.regs_raw, 0, sizeof(s->regs.regs_raw));
    memset(s->regs.mac_par, 0, sizeof(s->regs.mac_par));

    s->regs.cr0 = 0;
    s->regs.isr = 0;
    s->regs.imr = INT_MASK_DEF;
    s->intr_status = 0;
    s->intr_mask = INT_MASK_DEF;
    s->status_flags = 0;

    /* Make IMR visible */
    pcibase_reg_write32(s, MAC_REG_IMR, s->regs.imr);
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
    .name = "via_velocity_pci",
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
