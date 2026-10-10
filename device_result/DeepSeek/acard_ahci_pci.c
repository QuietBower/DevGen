/*
 * QEMU ACard AHCI PCI device model
 * Generated for Linux driver acard-ahci.c
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
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "acard_ahci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_ARTOP      0x1191
#define PCI_DEVICE_ID_ATP8620    0x000d
#define PCI_CLASS_STORAGE_AHCI   0x010601

/* AHCI Global Register Offsets */
#define HOST_CAP                 0x00
#define HOST_CTL                 0x04
#define HOST_IRQ_STAT            0x08
#define HOST_PORTS_IMPL          0x0C
#define HOST_VERSION             0x10
#define HOST_CCC_CTL             0x14
#define HOST_CCC_PORTS           0x18
#define HOST_EM_LOC              0x1C
#define HOST_EM_CTL              0x20
#define HOST_CAP2                0x24

/* AHCI Port Register Offsets */
#define PORT_BASE                0x100
#define PORT_STRIDE              0x80
#define PORT_LST_ADDR            0x00
#define PORT_FIS_ADDR            0x08
#define PORT_IRQ_STAT            0x10
#define PORT_IRQ_MASK            0x14
#define PORT_CMD                 0x18
#define PORT_TFDATA              0x20
#define PORT_SIG                 0x24
#define PORT_SSTS                0x28
#define PORT_SERR                0x2C
#define PORT_CI                  0x38

/* AHCI constants inferred from driver usage */
#define AHCI_MAX_PORTS           1       /* minimal; driver reads port map */
#define ACARD_AHCI_RX_FIS_SZ     128

/* Hardware descriptor structures */
struct acard_sg {
    __le32 addr;
    __le32 addr_hi;
    __le32 reserved;
    __le32 size;   /* bit 31 (EOT) max==0x10000 (64k) */
};

struct ahci_cmd_hdr {
    __le32 opts;
    __le32 status;
    __le32 tbl_addr;
    __le32 tbl_addr_hi;
    __le32 reserved[4];
};

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
        uint32_t cap;
        uint32_t ghc;
        uint32_t is;
        uint32_t pi;
        uint32_t vs;
        uint32_t ccc_ctl;
        uint32_t ccc_ports;
        uint32_t em_loc;
        uint32_t em_ctl;
        uint32_t cap2;
        struct {
            uint32_t lst_addr;
            uint32_t lst_addr_hi;
            uint32_t fis_addr;
            uint32_t fis_addr_hi;
            uint32_t is;
            uint32_t ie;
            uint32_t cmd;
            uint32_t tfd;
            uint32_t sig;
            uint32_t ssts;
            uint32_t serr;
            uint32_t ci;
        } port[AHCI_MAX_PORTS];
    } regs;

    /* DMA Context */
    struct {
        dma_addr_t cmd_list;
        dma_addr_t fis_base;
        dma_addr_t cmd_tbl;
        bool busy;
    } dma;

    /* Operational status flags */
    struct {
        bool running;
    } status;

    /* State used to handle reset sequences */
    struct {
        bool resetting;
    } probe_reset;

    /* Power management state (D0-D3) */
    struct {
        uint8_t state;
    } pm;

    /* No other additions */
};

static void pcibase_reset(DeviceState *dev);

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = ~0ULL;

    if (size != 4) {
        return ~0ULL;
    }

    if (addr < 0x100) {
        switch (addr) {
        case HOST_CAP:          val = s->regs.cap; break;
        case HOST_CTL:          val = s->regs.ghc; break;
        case HOST_IRQ_STAT:     val = s->intr_status; break;
        case HOST_PORTS_IMPL:   val = s->regs.pi; break;
        case HOST_VERSION:      val = s->regs.vs; break;
        case HOST_CCC_CTL:      val = s->regs.ccc_ctl; break;
        case HOST_CCC_PORTS:    val = s->regs.ccc_ports; break;
        case HOST_EM_LOC:       val = s->regs.em_loc; break;
        case HOST_EM_CTL:       val = s->regs.em_ctl; break;
        case HOST_CAP2:         val = s->regs.cap2; break;
        default:                val = 0; break;
        }
        return val;
    }

    /* Port registers */
    if (addr >= PORT_BASE && addr < PORT_BASE + AHCI_MAX_PORTS * PORT_STRIDE) {
        int port = (addr - PORT_BASE) / PORT_STRIDE;
        int port_offset = (addr - PORT_BASE) % PORT_STRIDE;
        if (port >= AHCI_MAX_PORTS) return 0;
        switch (port_offset) {
        case PORT_LST_ADDR:     val = s->regs.port[port].lst_addr; break;
        case PORT_LST_ADDR+4:   val = s->regs.port[port].lst_addr_hi; break;
        case PORT_FIS_ADDR:     val = s->regs.port[port].fis_addr; break;
        case PORT_FIS_ADDR+4:   val = s->regs.port[port].fis_addr_hi; break;
        case PORT_IRQ_STAT:     val = s->regs.port[port].is; break;
        case PORT_IRQ_MASK:     val = s->regs.port[port].ie; break;
        case PORT_CMD:          val = s->regs.port[port].cmd; break;
        case PORT_TFDATA:       val = s->regs.port[port].tfd; break;
        case PORT_SIG:          val = s->regs.port[port].sig; break;
        case PORT_SSTS:         val = s->regs.port[port].ssts; break;
        case PORT_SERR:         val = s->regs.port[port].serr; break;
        case PORT_CI:           val = 0; break;
        default:                val = 0; break;
        }
        return val;
    }

    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (size != 4) return;

    if (addr < 0x100) {
        switch (addr) {
        case HOST_CAP: /* read-only */ break;
        case HOST_CTL:
            {
                s->regs.ghc = val & 0x80000002; /* AE (bit31), IE (bit1), HR (bit0) */
                /* HRESET: if bit0 set */
                if (val & 1) {
                    pcibase_reset(DEVICE(s));
                    s->regs.ghc = 0; /* HRESET clears itself */
                }
            }
            break;
        case HOST_IRQ_STAT: /* read-only for this device */ break;
        case HOST_PORTS_IMPL: /* read-only */ break;
        case HOST_VERSION: /* read-only */ break;
        case HOST_CCC_CTL:   s->regs.ccc_ctl = val; break;
        case HOST_CCC_PORTS: s->regs.ccc_ports = val; break;
        case HOST_EM_LOC:    s->regs.em_loc = val; break;
        case HOST_EM_CTL:    s->regs.em_ctl = val; break;
        case HOST_CAP2: /* read-only */ break;
        default: break;
        }
        return;
    }

    /* Port registers */
    if (addr >= PORT_BASE && addr < PORT_BASE + AHCI_MAX_PORTS * PORT_STRIDE) {
        int port = (addr - PORT_BASE) / PORT_STRIDE;
        int port_offset = (addr - PORT_BASE) % PORT_STRIDE;
        if (port >= AHCI_MAX_PORTS) return;
        switch (port_offset) {
        case PORT_LST_ADDR:     s->regs.port[port].lst_addr = val; break;
        case PORT_LST_ADDR+4:   s->regs.port[port].lst_addr_hi = val; break;
        case PORT_FIS_ADDR:     s->regs.port[port].fis_addr = val; break;
        case PORT_FIS_ADDR+4:   s->regs.port[port].fis_addr_hi = val; break;
        case PORT_IRQ_STAT:     s->regs.port[port].is &= ~val; break; /* W1C */
        case PORT_IRQ_MASK:     s->regs.port[port].ie = val; break;
        case PORT_CMD:
            {
                uint32_t old = s->regs.port[port].cmd;
                uint32_t mask = 0x4000001F; /* common writable bits */
                s->regs.port[port].cmd = (old & ~mask) | (val & mask);
                /* ST and FRE handling */
                if ((val & 1) && !(old & 1)) { /* ST 0->1 */
                    if ((s->regs.ghc & 0x80000000) && (s->regs.port[port].cmd & 0x10)) {
                        s->regs.port[port].cmd |= 0x8000; /* CR bit 15 */
                    }
                }
                if (!(val & 1) && (old & 1)) { /* ST 1->0 */
                    s->regs.port[port].cmd &= ~0x8000; /* clear CR */
                }
            }
            break;
        case PORT_TFDATA: /* read-only, ignore writes */ break;
        case PORT_SIG:    /* read-only */ break;
        case PORT_SSTS:   /* read-only */ break;
        case PORT_SERR:   s->regs.port[port].serr &= ~val; break; /* W1C */
        case PORT_CI:     /* write: we just ignore, read returns 0 */ break;
        default: break;
        }
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

    /* Reset registers to power-on defaults */
    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.cap   = 0x4731f000;   /* AHCI capabilities, NP=0 => 1 port */
    s->regs.ghc   = 0;            /* AE not enabled, HRESET not active */
    s->regs.vs    = 0x00010200;   /* version */
    s->regs.pi    = 0x1;          /* first port implemented */
    s->regs.cap2  = 0x0;
    s->status.running = false;
    s->intr_status = 0;
    s->intr_mask = 0;

    /* Port defaults */
    s->regs.port[0].tfd = 0x0000007F;  /* task file status */
    s->regs.port[0].sig = 0xFFFFFFFF; /* no device signature */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ARTOP);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_ATP8620);
    /* Set class code: mass storage, SATA, AHCI */
    pci_set_byte(pci_conf + 0x09, 0x01);
    pci_set_byte(pci_conf + 0x0a, 0x06);
    pci_set_byte(pci_conf + 0x0b, 0x01);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* MSI initialization: driver calls pci_enable_msi() */
    s->has_msi = true;
    msi_init(pdev, 0, 1, true, false, errp);

    /* BAR Initialization */
    s->num_bars = 0;
    s->bar_info[0].index = 5;        /* AHCI_PCI_BAR == 5 */
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x400;    /* 1KB minimum for AHCI registers */
    s->bar_info[0].name  = "ahci-bar";
    s->num_bars = 1;
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
    .name = "acard_ahci_pci",
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
