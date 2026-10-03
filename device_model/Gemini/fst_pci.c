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


#define TYPE_PCIBASE_DEVICE "fst_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_FARSITE           0x1619
#define PCI_DEVICE_ID_FARSITE_T2P       0x0400

#define FST_MEMSIZE 0x100000
#define SMC_BASE 0x00002000L
#define BFM_BASE 0x00010000L

#define CNTRL_9052      0x50
#define CNTRL_9054      0x6c
#define INTCSR_9052     0x4c
#define INTCSR_9054     0x68

#define DMAMODE0        0x80
#define DMAPADR0        0x84
#define DMALADR0        0x88
#define DMASIZ0         0x8c
#define DMADPR0         0x90
#define DMAMODE1        0x94
#define DMAPADR1        0x98
#define DMALADR1        0x9c
#define DMASIZ1         0xa0
#define DMADPR1         0xa4
#define DMACSR0         0xa8
#define DMACSR1         0xa9
#define DMAARB          0xac
#define DMATHR          0xb0
#define DMADAC0         0xb4
#define DMADAC1         0xb8
#define DMAMARBR        0xac

#define FST_FAMILY_TXU  1
#define TE1_ALMA        0x30
#define CTLA_CHG        0x18
#define INIT_CPLT       0x20
#define MAX_CIRBUFF     32
#define FST_MAX_PORTS   4

#define NUM_TX_BUFFER 2
#define LEN_TX_BUFFER 8192
#define NUM_RX_BUFFER 8
#define LEN_RX_BUFFER 8192

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
    uint32_t intcsr_9052;
    uint32_t intcsr_9054;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t cntrl_9052;
    uint32_t cntrl_9054;

    /* DMA Context */
    uint32_t dmamode0;
    uint32_t dmapadr0;
    uint32_t dmaladr0;
    uint32_t dmasiz0;
    uint32_t dmadpr0;
    uint32_t dmamode1;
    uint32_t dmapadr1;
    uint32_t dmaladr1;
    uint32_t dmasiz1;
    uint32_t dmadpr1;
    uint8_t  dmacsr0;
    uint8_t  dmacsr1;
    uint32_t dmaarb;
    uint32_t dmathr;
    uint32_t dmadac0;
    uint32_t dmadac1;

    uint8_t *shared_mem;
};



/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    bool irq_status = false;
    if (s->intcsr_9054 & 0x00600000) {
        irq_status = true;
    }
    pci_set_irq(pdev, irq_status ? 1 : 0);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (!is_write) {
        /* Rx DMA: Card to Host */
        if (s->shared_mem && s->dmaladr0 + s->dmasiz0 <= FST_MEMSIZE) {
            pci_dma_write(pdev, s->dmapadr0, s->shared_mem + s->dmaladr0, s->dmasiz0);
        }
        s->intcsr_9054 |= 0x00200000;
    } else {
        /* Tx DMA: Host to Card */
        if (s->shared_mem && s->dmaladr1 + s->dmasiz1 <= FST_MEMSIZE) {
            pci_dma_read(pdev, s->dmapadr1, s->shared_mem + s->dmaladr1, s->dmasiz1);
        }
        s->intcsr_9054 |= 0x00400000;
    }
    pcibase_update_irq(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < FST_MEMSIZE && s->shared_mem) {
        memcpy(&val, s->shared_mem + addr, size);
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < FST_MEMSIZE && s->shared_mem) {
        memcpy(s->shared_mem + addr, &val, size);
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
        case CNTRL_9052: val = s->cntrl_9052; break;
        case CNTRL_9054: val = s->cntrl_9054; break;
        case INTCSR_9052: val = s->intcsr_9052; break;
        case INTCSR_9054: val = s->intcsr_9054; break;
        case DMAMODE0: val = s->dmamode0; break;
        case DMAPADR0: val = s->dmapadr0; break;
        case DMALADR0: val = s->dmaladr0; break;
        case DMASIZ0: val = s->dmasiz0; break;
        case DMADPR0: val = s->dmadpr0; break;
        case DMAMODE1: val = s->dmamode1; break;
        case DMAPADR1: val = s->dmapadr1; break;
        case DMALADR1: val = s->dmaladr1; break;
        case DMASIZ1: val = s->dmasiz1; break;
        case DMADPR1: val = s->dmadpr1; break;
        case DMACSR0: val = s->dmacsr0; break;
        case DMACSR1: val = s->dmacsr1; break;
        case DMAARB: val = s->dmaarb; break;
        case DMATHR: val = s->dmathr; break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    switch (addr) {
        case CNTRL_9052: s->cntrl_9052 = val; break;
        case CNTRL_9054: s->cntrl_9054 = val; break;
        case INTCSR_9052: s->intcsr_9052 = val; break;
        case INTCSR_9054: s->intcsr_9054 = val; break;
        case DMAMODE0: s->dmamode0 = val; break;
        case DMAPADR0: s->dmapadr0 = val; break;
        case DMALADR0: s->dmaladr0 = val; break;
        case DMASIZ0: s->dmasiz0 = val; break;
        case DMADPR0: s->dmadpr0 = val; break;
        case DMAMODE1: s->dmamode1 = val; break;
        case DMAPADR1: s->dmapadr1 = val; break;
        case DMALADR1: s->dmaladr1 = val; break;
        case DMASIZ1: s->dmasiz1 = val; break;
        case DMADPR1: s->dmadpr1 = val; break;
        case DMACSR0: 
            s->dmacsr0 = val; 
            if (val & 0x03) { pcibase_do_dma(s, false); }
            if (val & 0x08) { s->intcsr_9054 &= ~0x00200000; pcibase_update_irq(s); }
            break;
        case DMACSR1: 
            s->dmacsr1 = val; 
            if (val & 0x03) { pcibase_do_dma(s, true); }
            if (val & 0x08) { s->intcsr_9054 &= ~0x00400000; pcibase_update_irq(s); }
            break;
        case DMAARB: s->dmaarb = val; break;
        case DMATHR: s->dmathr = val; break;
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

    s->cntrl_9052 = 0;
    s->cntrl_9054 = 0;
    s->intcsr_9052 = 0;
    s->intcsr_9054 = 0;
    s->dmamode0 = 0;
    s->dmapadr0 = 0;
    s->dmaladr0 = 0;
    s->dmasiz0 = 0;
    s->dmadpr0 = 0;
    s->dmamode1 = 0;
    s->dmapadr1 = 0;
    s->dmaladr1 = 0;
    s->dmasiz1 = 0;
    s->dmadpr1 = 0;
    s->dmacsr0 = 0;
    s->dmacsr1 = 0;
    s->dmaarb = 0;
    s->dmathr = 0;
    if (s->shared_mem) {
        memset(s->shared_mem, 0, FST_MEMSIZE);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_FARSITE );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_FARSITE_T2P );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_OTHER );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 2, .type = BAR_TYPE_MMIO, .size = FST_MEMSIZE, .name = "phys_mem" };
    s->bar_info[1] = (BARInfo){ .index = 3, .type = BAR_TYPE_MMIO, .size = 0x10, .name = "phys_ctlmem" };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->shared_mem = g_malloc0(FST_MEMSIZE);
    s->bar_info[s->num_bars] = (BARInfo){ .index = 1, .type = BAR_TYPE_PIO, .size = 0x100, .name = "pci_conf" };
    pcibase_register_bar(pdev, s, &s->bar_info[s->num_bars], errp);
    s->num_bars++;
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

    if (s->shared_mem) {
        g_free(s->shared_mem);
        s->shared_mem = NULL;
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "fst_pci",
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
