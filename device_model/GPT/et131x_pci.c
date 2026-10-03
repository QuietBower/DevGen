/*
 * QEMU PCI device model for Agere ET131x NIC (behavioral model)
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

/* Local minimal register layout mirroring driver-visible structs.
 * Only definitions required for compilation and field access are provided;
 * no behavioral meaning is added.
 */

struct global_regs {
    uint32_t pm_csr;
    uint32_t int_status;
    uint32_t int_mask;
};

struct mac_regs {
    uint32_t cfg1;
    uint32_t cfg2;
};

struct address_map {
    struct global_regs global;
    struct mac_regs mac;
};

/* Constants referenced from the driver model; values chosen to satisfy
 * compile-time usage only. */
#define ET_PMCSR_INIT      0x00000000u
#define INT_MASK_DISABLE   0x00000000u

#define TYPE_PCIBASE_DEVICE "et131x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define ET131X_PCI_DEVICE_ID_GIG  0xED00
#define ET131X_PCI_DEVICE_ID_FAST 0xED01

/* First entry in et131x_pci_table uses PCI_VDEVICE(ATT, ET131X_PCI_DEVICE_ID_GIG).
 * The Linux driver relies on the vendor macro ATT from pci_ids.h, which expands
 * to the PCI vendor ID for Agere/Attansic (0x11c1).
 */
#define ET131X_PCI_VENDOR_ID_ATT  0x11C1

/* The driver's address_map struct describes the entire MMIO space mapped by BAR0.
 * Its total size is 2 MiB (0x200000) as defined by the layout in et131x_regs.h.
 */
#define ET131X_REGS_LEN           (2 * MiB)

#define VENDOR_ID  ET131X_PCI_VENDOR_ID_ATT
#define DEVICE_ID  ET131X_PCI_DEVICE_ID_GIG
#define CLASS_ID   PCI_CLASS_NETWORK_ETHERNET

/* Simple interrupt bits modeled from driver usage */
/* These come from et131x_regs.h; we mirror their usage but do not redefine. */

/* We keep only minimal state to satisfy driver MMIO expectations. */

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
    struct address_map regs_shadow;

    /* Simple interrupt emulation state */
    uint32_t int_status;
    uint32_t int_mask;
};


static void et131x_update_irq(PCIBaseState *s)
{
    /* In real silicon, various blocks OR into global.int_status. We keep a
     * very simple model: if any unmasked bit is set, raise INTx.
     */
    uint32_t pending = s->int_status & ~s->int_mask;

    if (pending) {
        pci_set_irq(PCI_DEVICE(s), 1);
    } else {
        pci_set_irq(PCI_DEVICE(s), 0);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t *base = (uint8_t *)&s->regs_shadow;

    /* Global block is used heavily; special cases needed for a few regs */
    if (addr + size > sizeof(struct address_map)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "et131x: MMIO read out of range addr=0x%" HWADDR_PRIx " size=%u\n",
                      addr, size);
        return 0;
    }

    /* Treat register file as little-endian memory */
    uint64_t val = 0;

    switch (size) {
    case 1:
        val = *(uint8_t *)(base + addr);
        break;
    case 2:
        val = le16_to_cpu(*(uint16_t *)(base + addr));
        break;
    case 4:
        val = le32_to_cpu(*(uint32_t *)(base + addr));
        break;
    case 8:
        val = le64_to_cpu(*(uint64_t *)(base + addr));
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "et131x: bad MMIO read size %u at 0x%" HWADDR_PRIx "\n",
                      size, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t *base = (uint8_t *)&s->regs_shadow;

    if (addr + size > sizeof(struct address_map)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "et131x: MMIO write out of range addr=0x%" HWADDR_PRIx " size=%u val=0x%" PRIx64 "\n",
                      addr, size, val);
        return;
    }

    /* For most registers, just mirror into shadow. We special-case
     * a few global/interrupt related registers according to driver usage.
     */

    /* Compute pointer into struct */
    uint32_t *p32;

    /* global block offset inside address_map */
    const size_t off_global = offsetof(struct address_map, global);

    /* global.int_status offset */
    const size_t off_int_status = off_global + offsetof(struct global_regs, int_status);
    const size_t off_int_mask   = off_global + offsetof(struct global_regs, int_mask);

    if (size == 4 && addr == off_int_mask) {
        /* Driver writes mask with INT_MASK_ENABLE / _NO_FLOW and uses
         * readl(&global.int_status) & ~mask to get pending bits.
         */
        p32 = (uint32_t *)(base + addr);
        *p32 = cpu_to_le32((uint32_t)val);
        s->int_mask = (uint32_t)val;
        et131x_update_irq(s);
        return;
    }

    if (size == 4 && addr == off_int_status) {
        /* int_status is not explicitly W1C in driver, but ISR clears
         * sub-block COR status registers. For simplicity we just mirror.
         * Guest rarely writes here; still store and recompute IRQ.
         */
        p32 = (uint32_t *)(base + addr);
        *p32 = cpu_to_le32((uint32_t)val);
        s->int_status = (uint32_t)val;
        et131x_update_irq(s);
        return;
    }

    /* All other registers: generic little-endian store into shadow */
    switch (size) {
    case 1:
        *(uint8_t *)(base + addr) = (uint8_t)val;
        break;
    case 2:
        *(uint16_t *)(base + addr) = cpu_to_le16((uint16_t)val);
        break;
    case 4:
        *(uint32_t *)(base + addr) = cpu_to_le32((uint32_t)val);
        break;
    case 8:
        *(uint64_t *)(base + addr) = cpu_to_le64((uint64_t)val);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "et131x: bad MMIO write size %u at 0x%" HWADDR_PRIx " val=0x%" PRIx64 "\n",
                      size, addr, val);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Device does not use legacy PIO in the Linux driver. */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* Device does not use legacy PIO in the Linux driver. */
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
    uint8_t *base = (uint8_t *)&s->regs_shadow;

    pci_device_reset(PCI_DEVICE(dev));

    /* Clear shadow register file */
    memset(&s->regs_shadow, 0, sizeof(s->regs_shadow));

    /* Initialize key globals to power-on defaults used by driver. */
    {
        struct global_regs *g = &s->regs_shadow.global;
        g->pm_csr = cpu_to_le32(ET_PMCSR_INIT);
        g->int_mask = cpu_to_le32(INT_MASK_DISABLE);
        g->int_status = 0;
        s->int_mask = INT_MASK_DISABLE;
        s->int_status = 0;
    }

    /* MAC config defaults: soft reset asserted then deasserted as part of
     * driver et131x_soft_reset(); we just start deasserted.
     */
    {
        struct mac_regs *m = &s->regs_shadow.mac;
        m->cfg1 = 0;
        m->cfg2 = 0;
    }

    /* Zero rest of memory region backing registers */
    memset(base, 0, sizeof(struct address_map));

    et131x_update_irq(s);
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

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = ET131X_REGS_LEN;
    s->bar_info[0].name = "et131x-mmio";

    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* The Linux driver uses legacy INTx only; no MSI/MSI-X setup here. */
    s->has_msi = false;
    s->has_msix = false;

    /* Initialize register shadow and interrupt state */
    memset(&s->regs_shadow, 0, sizeof(s->regs_shadow));
    s->int_status = 0;
    s->int_mask = INT_MASK_DISABLE;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, 0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* No additional uninit actions required. */
    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "et131x_pci",
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
