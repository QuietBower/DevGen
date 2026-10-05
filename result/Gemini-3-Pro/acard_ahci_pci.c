/*
 * QEMU ACard AHCI PCI device model
 *
 * Based on Linux driver: drivers/ata/acard-ahci.c
 * Target QEMU: 8.2.10
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"

#define TYPE_PCIBASE_DEVICE "acard_ahci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Driver Constants */
#define VENDOR_ID      0x1191 /* ARTOP */
#define DEVICE_ID      0x000d
#define CLASS_ID       PCI_CLASS_STORAGE_SATA

#define AHCI_PCI_BAR        5
#define AHCI_MEM_SIZE       0x2000 /* 8KB, power of 2 */

/* AHCI Register Offsets */
#define AHCI_CAP            0x00
#define AHCI_GHC            0x04
#define AHCI_IS             0x08
#define AHCI_PI             0x0C
#define AHCI_VS             0x10

/* AHCI Constants */
#define AHCI_GHC_HR         (1 << 0)
#define AHCI_GHC_IE         (1 << 1)
#define AHCI_GHC_AE         (1U << 31)

#define HOST_CAP_64         (1U << 31)
#define HOST_CAP_NCQ        (1 << 30)
#define HOST_CAP_SSS        (1 << 27)
#define HOST_CAP_AHCI       (1 << 18)

#define AHCI_PORT_OFFSET    0x100
#define AHCI_PORT_STRIDE    0x80

#define PORT_CLB            0x00
#define PORT_CLBU           0x04
#define PORT_FB             0x08
#define PORT_FBU            0x0C
#define PORT_IS             0x10
#define PORT_IE             0x14
#define PORT_CMD            0x18
#define PORT_TFD            0x20
#define PORT_SIG            0x24
#define PORT_SSTS           0x28
#define PORT_SCTL           0x2C
#define PORT_SERR           0x30
#define PORT_SACT           0x34
#define PORT_CI             0x38
#define PORT_SNTF           0x3C
#define PORT_FBS            0x40

#define PORT_CMD_ST         (1 << 0)
#define PORT_CMD_FRE        (1 << 4)
#define PORT_CMD_FR         (1 << 14)
#define PORT_CMD_CR         (1 << 15)

#define PORT_IRQ_DHR        (1 << 0)
#define PORT_IRQ_PSS        (1 << 1)
#define PORT_IRQ_SDB        (1 << 2)

#define AHCI_CMD_SLOT_SZ    32
#define AHCI_CMD_TBL_HDR_SZ 0x80

/* BAR metadata definition */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

/* Port State */
typedef struct AHCIPort {
    uint32_t clb;
    uint32_t clbu;
    uint32_t fb;
    uint32_t fbu;
    uint32_t is;
    uint32_t ie;
    uint32_t cmd;
    uint32_t tfd;
    uint32_t sig;
    uint32_t ssts;
    uint32_t sctl;
    uint32_t serr;
    uint32_t sact;
    uint32_t ci;
    uint32_t sntf;
    uint32_t fbs;
} AHCIPort;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* AHCI Registers */
    uint32_t cap;
    uint32_t ghc;
    uint32_t is;
    uint32_t pi;
    uint32_t vs;

    AHCIPort ports[1]; /* Implementing 1 port for simplicity */
};

/* Driver specific structures */
struct acard_sg {
    uint32_t addr;
    uint32_t addr_hi;
    uint32_t reserved;
    uint32_t size;   /* bit 31 (EOT) max==0x10000 (64k) */
};

struct ahci_cmd_hdr {
    uint32_t opts;
    uint32_t status;
    uint32_t tbl_addr;
    uint32_t tbl_addr_hi;
    uint32_t reserved[4];
};

/* Forward declarations */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static void pcibase_reset(DeviceState *dev);

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE)
        return;

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void ahci_check_irq(PCIBaseState *s)
{
    int i;
    uint32_t is_total = 0;

    for (i = 0; i < 1; i++) {
        if (s->ports[i].is & s->ports[i].ie) {
            s->is |= (1 << i);
            is_total |= (1 << i);
        }
    }

    if (s->ghc & AHCI_GHC_IE && is_total) {
        if (s->has_msi) {
            msi_notify(&s->parent_obj, 0);
        } else {
            pci_set_irq(&s->parent_obj, 1);
        }
    } else {
        if (!s->has_msi) {
            pci_set_irq(&s->parent_obj, 0);
        }
    }
}

static void ahci_process_command(PCIBaseState *s, int port, int slot)
{
    AHCIPort *p = &s->ports[port];
    dma_addr_t clb = ((uint64_t)p->clbu << 32) | p->clb;
    dma_addr_t cmd_hdr_addr = clb + (slot * AHCI_CMD_SLOT_SZ);
    struct ahci_cmd_hdr hdr;
    struct acard_sg sg;
    dma_addr_t tbl_addr;
    dma_addr_t sg_addr;
    int i;
    AddressSpace *as = pci_get_address_space(&s->parent_obj);

    /* Read Command Header */
    if (dma_memory_read(as, cmd_hdr_addr, &hdr, sizeof(hdr), MEMTXATTRS_UNSPECIFIED)) {
        qemu_log_mask(LOG_GUEST_ERROR, "acard_ahci: failed to read cmd header\n");
        return;
    }

    tbl_addr = ((uint64_t)le32_to_cpu(hdr.tbl_addr_hi) << 32) | le32_to_cpu(hdr.tbl_addr);
    
    /* Skip Command Table Header (0x80 bytes) to get to SG list */
    sg_addr = tbl_addr + AHCI_CMD_TBL_HDR_SZ;

    /* Process SG list using ACard format */
    for (i = 0; i < 168; i++) { /* Max SG entries usually limited, loop safe limit */
        if (dma_memory_read(as, sg_addr, &sg, sizeof(sg), MEMTXATTRS_UNSPECIFIED)) {
            break;
        }

        uint32_t size_flags = le32_to_cpu(sg.size);
        uint32_t len = size_flags & 0x7FFFFFFF;
        bool eot = (size_flags & (1U << 31)) != 0;
        dma_addr_t buf_addr = ((uint64_t)le32_to_cpu(sg.addr_hi) << 32) | le32_to_cpu(sg.addr);

        /* 
         * In a real device, we would read/write data here.
         * For this model, we just validate the descriptor access.
         */
        (void)buf_addr;
        (void)len;

        if (eot) {
            break;
        }
        sg_addr += sizeof(sg);
    }

    /* Complete command */
    p->ci &= ~(1 << slot);
    p->sact &= ~(1 << slot);
    p->is |= PORT_IRQ_DHR | PORT_IRQ_PSS;
    
    ahci_check_irq(s);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    if (addr < AHCI_PORT_OFFSET) {
        switch (addr) {
        case AHCI_CAP: val = s->cap; break;
        case AHCI_GHC: val = s->ghc; break;
        case AHCI_IS:  val = s->is; break;
        case AHCI_PI:  val = s->pi; break;
        case AHCI_VS:  val = s->vs; break;
        default: break;
        }
    } else {
        int port = (addr - AHCI_PORT_OFFSET) / AHCI_PORT_STRIDE;
        int off = (addr - AHCI_PORT_OFFSET) % AHCI_PORT_STRIDE;
        
        if (port >= 1) return 0;

        switch (off) {
        case PORT_CLB:  val = s->ports[port].clb; break;
        case PORT_CLBU: val = s->ports[port].clbu; break;
        case PORT_FB:   val = s->ports[port].fb; break;
        case PORT_FBU:  val = s->ports[port].fbu; break;
        case PORT_IS:   val = s->ports[port].is; break;
        case PORT_IE:   val = s->ports[port].ie; break;
        case PORT_CMD:  val = s->ports[port].cmd; break;
        case PORT_TFD:  val = s->ports[port].tfd; break;
        case PORT_SIG:  val = s->ports[port].sig; break;
        case PORT_SSTS: val = s->ports[port].ssts; break;
        case PORT_SCTL: val = s->ports[port].sctl; break;
        case PORT_SERR: val = s->ports[port].serr; break;
        case PORT_SACT: val = s->ports[port].sact; break;
        case PORT_CI:   val = s->ports[port].ci; break;
        case PORT_SNTF: val = s->ports[port].sntf; break;
        case PORT_FBS:  val = s->ports[port].fbs; break;
        default: break;
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < AHCI_PORT_OFFSET) {
        switch (addr) {
        case AHCI_GHC:
            if (val & AHCI_GHC_HR) {
                pcibase_reset(DEVICE(s));
                return;
            }
            s->ghc = val;
            ahci_check_irq(s);
            break;
        case AHCI_IS:
            s->is &= ~val;
            ahci_check_irq(s);
            break;
        default: break;
        }
    } else {
        int port = (addr - AHCI_PORT_OFFSET) / AHCI_PORT_STRIDE;
        int off = (addr - AHCI_PORT_OFFSET) % AHCI_PORT_STRIDE;

        if (port >= 1) return;

        switch (off) {
        case PORT_CLB:  s->ports[port].clb = val; break;
        case PORT_CLBU: s->ports[port].clbu = val; break;
        case PORT_FB:   s->ports[port].fb = val; break;
        case PORT_FBU:  s->ports[port].fbu = val; break;
        case PORT_IS:   
            s->ports[port].is &= ~val;
            ahci_check_irq(s);
            break;
        case PORT_IE:   
            s->ports[port].ie = val;
            ahci_check_irq(s);
            break;
        case PORT_CMD:  
            s->ports[port].cmd = val;
            /* Simulate engine start/stop immediately */
            if (val & PORT_CMD_ST) s->ports[port].cmd |= PORT_CMD_CR;
            else s->ports[port].cmd &= ~PORT_CMD_CR;
            
            if (val & PORT_CMD_FRE) s->ports[port].cmd |= PORT_CMD_FR;
            else s->ports[port].cmd &= ~PORT_CMD_FR;
            break;
        case PORT_SCTL: s->ports[port].sctl = val; break;
        case PORT_SERR: s->ports[port].serr &= ~val; break;
        case PORT_SACT: s->ports[port].sact = val; break;
        case PORT_CI:
            s->ports[port].ci |= val;
            /* Trigger command processing for set bits */
            for (int i = 0; i < 32; i++) {
                if ((val & (1 << i)) && (s->ports[port].cmd & PORT_CMD_ST)) {
                    ahci_process_command(s, port, i);
                }
            }
            break;
        default: break;
        }
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    
    /* 
     * Removed HOST_CAP_64 and HOST_CAP_NCQ to match driver expectations.
     * The driver explicitly disables NCQ and forces 32-bit DMA.
     * Reporting 64-bit capability can confuse the driver/kernel logic.
     */
    s->cap = HOST_CAP_SSS | HOST_CAP_AHCI;
    s->ghc = 0;
    s->is = 0;
    s->pi = 1; /* Port 0 implemented */
    s->vs = 0x00010100; /* AHCI 1.1 */

    memset(s->ports, 0, sizeof(s->ports));
    /* Default TFD: Status=0x50 (Ready) */
    s->ports[0].tfd = 0x50; 
    s->ports[0].sig = 0x00000101; /* SATA signature */
    s->ports[0].ssts = 0x123; /* DET=3, SPD=2 */
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        s->has_msi = true;
    }

    s->num_bars = 1;
    s->bar_info[0].index = AHCI_PCI_BAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = AHCI_MEM_SIZE;
    s->bar_info[0].name = "acard-ahci-mmio";

    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
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
