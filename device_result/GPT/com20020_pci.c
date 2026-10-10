/*
 * QEMU PCI device model for com20020-pci
 * Phase 2: Functional behavior implementation based on Linux driver.
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

#define TYPE_PCIBASE_DEVICE "com20020_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID 0x1571
#define PCIBASE_DEVICE_ID 0xa001
#define PCIBASE_CLASS_ID  PCI_CLASS_NETWORK_OTHER

#define COM20020_REG_R_STATUS      0
#define COM20020_REG_R_DIAGSTAT    1
#define COM20020_REG_W_INTMASK     0
#define COM20020_REG_W_COMMAND     1
#define COM20020_REG_W_ADDR_HI     2
#define COM20020_REG_W_ADDR_LO     3
#define COM20020_REG_RW_MEMDATA    4
#define COM20020_REG_W_SUBADR      5
#define COM20020_REG_W_CONFIG      6
#define COM20020_REG_W_XREG        7

#define ARC_CAN_10MBIT  2
#define ARC_IS_5MBIT    1
#define ARC_HAS_LED     4
#define ARC_HAS_ROTARY  8

#define PLX_PCI_MAX_CARDS 2

#define BUS_ALIGN 1

#define ARCNET_LED_NAME_SZ (IFNAMSIZ + 6)

#define NORXflag        0x80

#define XTOcfg(x)   ((x) << 3)

#define ARCNET_DEBUG_MAX (127)

#define D_NORMAL        1
#define D_EXTRA         2
#define D_INIT          4
#define D_PROTO         64
#define D_DURING        128
#define D_SKB           1024
#define D_SKB_SIZE      2048
#define D_INIT_REASONS  8
#define D_TIMING        4096
#define D_DEBUG         8192

#define MTU 253
#define XMTU 508

/* Bits and constants derived from new driver snippet */
#define RESETcfg        0x04  /* used with XTOcfg(3) | RESETcfg */
#define RESETtime       10    /* mdelay(RESETtime); approximate reset time */
#define P1MODE          0x01  /* lp->setup |= P1MODE; */
#define SUB_SETUP1      0x04  /* subaddress for setup1 register */
#define SUB_SETUP2      0x05  /* subaddress for setup2 register */
#define SUB_NODE        0x01  /* low bits ORed into lp->config */
#define STARTIOcmd      0x40  /* "restart operation" command bit */
#define CFLAGScmd       0x18  /* generic flag clear command bits */
#define RESETclear      0x02  /* clear RESET flag */
#define CONFIGclear     0x01  /* clear CONFIG flag */
#define RDDATAflag      0x10  /* read memory data flag for ADDR_HI */
#define AUTOINCflag     0x20  /* autoincrement flag for ADDR_HI */
#define TESTvalue       0xD1  /* expected signature byte in memory */
#define TXFREEflag      0x10  /* transmitter free flag in STATUS */
#define RESETflag       0x01  /* reset complete flag in STATUS */

/* Simple local representation of the com20020 chip internal registers.
 * Only fields actually touched by the provided driver snippet are modeled.
 */

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
    uint8_t chan_regs[8]; /* 8-byte per-channel I/O window */

    /* Very small on-card RAM for MEMDATA window access. Size is 256 bytes
     * as the Linux driver never specifies the range and only does byte
     * accesses through COM20020_REG_RW_MEMDATA and subaddressing.
     */
    uint8_t mem[256];
    uint8_t addr_hi;
    uint8_t addr_lo;
    uint8_t subadr;
    uint8_t config;

    /* Misc I/O region used for LEDs, rotary and backplane detection.
     * We provide a 16-byte I/O space starting at the same BAR as
     * the com20020 channel BAR (BAR 2, offset 0x10 in real hardware).
     * Internally we only emulate simple read/write latches.
     */
    uint8_t misc_regs[16];
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* The provided driver snippet only calls com20020_found(dev, IRQF_SHARED)
     * and never touches interrupt status/enable bits explicitly in this file.
     * Without the com20020 core driver we cannot model detailed interrupt
     * semantics, so we leave this as a no-op for now.
     */
    (void)s;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* The PCI front-end driver com20020-pci.c performs only programmed I/O
     * via inb/outb and does not set up any DMA descriptors or buffers.
     * Therefore we do not implement bus-master DMA here.
     */
    (void)s;
    (void)is_write;
}

/* Helpers implementing the com20020 register model visible via arcnet_inb/outb.
 * The channel I/O window is 8 bytes wide and mapped to BAR2 in this virtual
 * device (index 0 in bar_info is BAR2).
 */

static uint8_t pcibase_chan_read(PCIBaseState *s, uint8_t reg)
{
    uint8_t val = 0xff;

    switch (reg) {
    case COM20020_REG_R_STATUS:
        /* Status register: implement flags used by com20020_check().
         * Bits checked: NORXflag (0x80), TXFREEflag (0x10), RESETflag (0x01).
         */
        val = s->chan_regs[COM20020_REG_R_STATUS];
        /* Ensure reserved NORXflag is set (no RX in progress) */
        val |= NORXflag;
        /* Ensure transmitter free flag is set */
        val |= TXFREEflag;
        /* Ensure RESETflag reflects reset-complete state. The reset handler
         * initializes it; later CFLAGScmd | RESETclear | CONFIGclear will
         * clear it explicitly.
         */
        if (s->chan_regs[COM20020_REG_R_STATUS] & RESETflag) {
            val |= RESETflag;
        } else {
            val &= ~RESETflag;
        }
        s->chan_regs[COM20020_REG_R_STATUS] = val;
        break;
    case COM20020_REG_R_DIAGSTAT:
        val = s->chan_regs[COM20020_REG_R_DIAGSTAT];
        break;
    case COM20020_REG_RW_MEMDATA: {
        /* Read from internal memory using current address hi/lo. */
        uint16_t addr = ((uint16_t)s->addr_hi << 8) | s->addr_lo;
        val = s->mem[addr & (sizeof(s->mem) - 1)];
        /* If AUTOINCflag is set in addr_hi, increment address after read. */
        if (s->addr_hi & AUTOINCflag) {
            addr++;
            s->addr_hi = (addr >> 8) & 0xff;
            s->addr_lo = addr & 0xff;
            s->chan_regs[COM20020_REG_W_ADDR_HI] = s->addr_hi;
            s->chan_regs[COM20020_REG_W_ADDR_LO] = s->addr_lo;
        }
        break;
    }
    default:
        /* For registers we don't explicitly model, return stored value. */
        if (reg < sizeof(s->chan_regs)) {
            val = s->chan_regs[reg];
        } else {
            val = 0xff;
        }
        break;
    }

    return val;
}

static void pcibase_chan_write(PCIBaseState *s, uint8_t reg, uint8_t val)
{
    switch (reg) {
    case COM20020_REG_W_COMMAND:
        /* Handle specific commands used by com20020_check()/found(). */
        s->chan_regs[COM20020_REG_W_COMMAND] = val;
        /* CFLAGScmd | RESETclear | CONFIGclear clears RESET and CONFIG bits. */
        if ((val & (CFLAGScmd | RESETclear | CONFIGclear)) ==
            (CFLAGScmd | RESETclear | CONFIGclear)) {
            s->chan_regs[COM20020_REG_R_STATUS] &= ~RESETflag;
        }
        /* STARTIOcmd is used as a "restart operation" command; we do not
         * need additional behavior for the probe to succeed.
         */
        break;
    case COM20020_REG_W_INTMASK:
        s->chan_regs[COM20020_REG_W_INTMASK] = val;
        break;
    case COM20020_REG_W_ADDR_HI:
        s->addr_hi = val;
        s->chan_regs[COM20020_REG_W_ADDR_HI] = val;
        break;
    case COM20020_REG_W_ADDR_LO:
        s->addr_lo = val;
        s->chan_regs[COM20020_REG_W_ADDR_LO] = val;
        break;
    case COM20020_REG_RW_MEMDATA: {
        uint16_t addr = ((uint16_t)s->addr_hi << 8) | s->addr_lo;
        s->mem[addr & (sizeof(s->mem) - 1)] = val;
        s->chan_regs[COM20020_REG_RW_MEMDATA] = val;
        /* AUTOINCflag also applies to write paths. */
        if (s->addr_hi & AUTOINCflag) {
            addr++;
            s->addr_hi = (addr >> 8) & 0xff;
            s->addr_lo = addr & 0xff;
            s->chan_regs[COM20020_REG_W_ADDR_HI] = s->addr_hi;
            s->chan_regs[COM20020_REG_W_ADDR_LO] = s->addr_lo;
        }
        break;
    }
    case COM20020_REG_W_SUBADR:
        s->subadr = val;
        s->chan_regs[COM20020_REG_W_SUBADR] = val;
        break;
    case COM20020_REG_W_CONFIG:
        s->config = val;
        s->chan_regs[COM20020_REG_W_CONFIG] = val;
        /* Writing RESETcfg along with XTOcfg(x) triggers reset timing.
         * We do not implement delays but we can mark RESETflag set.
         */
        if (val & RESETcfg) {
            s->chan_regs[COM20020_REG_R_STATUS] |= RESETflag;
        }
        break;
    case COM20020_REG_W_XREG:
        /* XREG meaning depends on subaddress; we just latch the value. */
        s->chan_regs[COM20020_REG_W_XREG] = val;
        break;
    default:
        if (reg < sizeof(s->chan_regs)) {
            s->chan_regs[reg] = val;
        }
        break;
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* This device only exposes PIO BARs to the guest driver; no MMIO is
     * used in com20020-pci.c. We keep this region unused.
     */
    (void)opaque;
    (void)addr;
    (void)size;
    return 0xffffffffu;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Unused MMIO region. */
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0xff;

    /* Channel window is BAR2 offset range [0x0,0x8). The template registers
     * BAR2 as a separate I/O region that is exactly 8 bytes in size, so
     * within this MemoryRegion the guest-visible offset is 0..7 and maps
     * directly to COM20020_REG_* indexes.
     */

    if (addr < 0x08 && size == 1) {
        uint8_t reg = (uint8_t)addr;
        val = pcibase_chan_read(s, reg);
        return val;
    }

    /* We also provide a small misc I/O area represented by misc_regs, which
     * the real hardware maps starting at BAR2 offset 0x10. Because our
     * current template only exposes an 8-byte region, the operating system
     * will never reach here through this PIO handler. The code remains for
     * completeness in case the BAR layout is extended in future iterations.
     */
    if (addr >= 0x10 && addr < 0x10 + sizeof(s->misc_regs) && size == 1) {
        val = s->misc_regs[addr - 0x10];
        return val;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x08 && size == 1) {
        uint8_t reg = (uint8_t)addr;
        pcibase_chan_write(s, reg, (uint8_t)val);
        return;
    }

    if (addr >= 0x10 && addr < 0x10 + sizeof(s->misc_regs) && size == 1) {
        s->misc_regs[addr - 0x10] = (uint8_t)val;
        return;
    }
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

    /* Initialize channel registers and internal memory to known values.
     * The probe expects that reading COM20020_REG_R_STATUS does not
     * return 0xff. We set it to NORXflag | TXFREEflag | RESETflag so
     * that (status & 0x99) matches the expected value.
     */
    memset(s->chan_regs, 0, sizeof(s->chan_regs));
    memset(s->mem, 0, sizeof(s->mem));
    memset(s->misc_regs, 0, sizeof(s->misc_regs));

    s->addr_hi = 0;
    s->addr_lo = 0;
    s->subadr  = 0;
    s->config  = 0;

    s->chan_regs[COM20020_REG_R_STATUS] = NORXflag | TXFREEflag | RESETflag;
    s->chan_regs[COM20020_REG_R_DIAGSTAT] = 0x00;

    /* Program the first byte of internal memory with TESTvalue so that
     * com20020_check() finds the expected signature when reading
     * address 0 with RDDATAflag | AUTOINCflag.
     */
    s->mem[0] = TESTvalue;
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

    /* BAR Initialization
     * The driver uses BAR2 for the com20020 channel and, for some cards,
     * also BAR2 offset 0x10 for misc controls. Our template exposes a
     * single PIO BAR (index 2) of size 0x08 as the channel window.
     */
    s->num_bars = 1;
    s->bar_info[0].index = 2;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x08;
    s->bar_info[0].name = "com20020-chan";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize runtime state */
    memset(s->chan_regs, 0, sizeof(s->chan_regs));
    memset(s->mem, 0, sizeof(s->mem));
    memset(s->misc_regs, 0, sizeof(s->misc_regs));
    s->addr_hi = 0;
    s->addr_lo = 0;
    s->subadr  = 0;
    s->config  = 0;
    s->chan_regs[COM20020_REG_R_STATUS] = NORXflag | TXFREEflag | RESETflag;
    s->chan_regs[COM20020_REG_R_DIAGSTAT] = 0x00;
    s->mem[0] = TESTvalue;
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
    .name = "com20020_pci",
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
