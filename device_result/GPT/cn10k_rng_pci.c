/*
 * QEMU PCI device model for CN10K RNG, tailored for cn10k-rng.c driver
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

#define TYPE_PCIBASE_DEVICE "cn10k_rng_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define CN10K_RNM_CTL_STATUS         0x000
#define CN10K_RNM_ENTROPY_STATUS     0x008
#define CN10K_RNM_CONST              0x030
#define CN10K_RNM_EBG_ENT            0x048
#define CN10K_RNM_PF_EBG_HEALTH      0x050
#define CN10K_RNM_PF_RANDOM          0x400
#define CN10K_RNM_TRNG_RESULT        0x408
#define CN10K_RNM_PF_TRNG_DAT        0x1000
#define CN10K_RNM_PF_TRNG_RES        0x1008
#define CN10K_PLAT_OCTEONTX_RESET_RNG_EBG_HEALTH_STATE 0xc2000b0f
#define CN10K_PCI_SUBSYS_DEVID_CN10K_A_RNG   0xB900
#define CN10K_PCI_SUBSYS_DEVID_CNF10K_A_RNG  0xBA00
#define CN10K_PCI_SUBSYS_DEVID_CNF10K_B_RNG  0xBC00

#ifndef PCI_VENDOR_ID_CAVIUM
#define PCI_VENDOR_ID_CAVIUM 0x177d
#endif

#define CN10K_RNG_VENDOR_ID  PCI_VENDOR_ID_CAVIUM
#define CN10K_RNG_DEVICE_ID  0xA098
#define CN10K_RNG_CLASS_ID   PCI_CLASS_OTHERS

/* Bit 20 used by driver for health status */
#ifndef BIT_ULL
#define BIT_ULL(nr) (1ULL << (nr))
#endif

/* simple PRNG for deterministic but non-zero random values */
typedef struct {
    uint64_t state;
} SimplePRNG;

static uint64_t prng_next(SimplePRNG *p)
{
    /* xorshift64* */
    uint64_t x = p->state;
    if (x == 0) {
        x = 88172645463393265ULL;
    }
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    p->state = x;
    return x * 2685821657736338717ULL;
}

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
    uint64_t regs[0x1100 / 8 + 1];

    /* Simple internal PRNG state for RNG output */
    SimplePRNG prng;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* cn10k-rng driver does not use interrupts; keep empty. */
    (void)s;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* cn10k-rng driver does not use DMA; keep empty. */
    (void)s;
    (void)is_write;
}

/* Helpers to access shadow register array */
static inline uint64_t pcibase_reg_read64(PCIBaseState *s, hwaddr addr)
{
    unsigned index = addr / 8;
    if (index < sizeof(s->regs) / sizeof(s->regs[0])) {
        return s->regs[index];
    }
    return 0;
}

static inline void pcibase_reg_write64(PCIBaseState *s, hwaddr addr, uint64_t val)
{
    unsigned index = addr / 8;
    if (index < sizeof(s->regs) / sizeof(s->regs[0])) {
        s->regs[index] = val;
    }
}

/* Generate non-zero 64-bit random value, as expected by driver */
static uint64_t pcibase_rng_get_random(PCIBaseState *s)
{
    uint64_t v = prng_next(&s->prng);
    if (v == 0) {
        v = 0x1ULL; /* avoid zero, driver treats zero specially */
    }
    return v;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Driver uses 64-bit readq; but allow other sizes safely */

    switch (addr) {
    case CN10K_RNM_PF_EBG_HEALTH:
        /* Health status register; bit 20 indicates error. Keep it cleared. */
        val = pcibase_reg_read64(s, addr);
        break;
    case CN10K_RNM_PF_TRNG_DAT:
        /* TRNG data register: returns 64-bit random value, possibly zero. */
        val = pcibase_reg_read64(s, addr);
        break;
    case CN10K_RNM_PF_TRNG_RES:
        /* TRNG result/status register.
         * For simplicity, make it 1 whenever TRNG_DAT is non-zero so
         * cn10k_read_trng() does not spin forever.
         */
        val = pcibase_reg_read64(s, addr);
        break;
    case CN10K_RNM_PF_RANDOM:
        /* Legacy random register used also when extended regs supported. */
        val = pcibase_reg_read64(s, addr);
        break;
    default:
        /* Other registers: return shadowed content (default 0). */
        val = pcibase_reg_read64(s, addr);
        break;
    }

    /* Handle sub-64-bit accesses by masking */
    if (size < 8) {
        uint64_t mask = (size == 4) ? 0xffffffffULL :
                        (size == 2) ? 0xffffULL :
                        (size == 1) ? 0xffULL : ~0ULL;
        val &= mask;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* The cn10k-rng driver does not perform MMIO writes to RNG registers.
     * Implement generic shadow write for completeness.
     */

    uint64_t cur = pcibase_reg_read64(s, addr);
    uint64_t newv = cur;

    if (size >= 8) {
        newv = val;
    } else {
        uint64_t mask = (size == 4) ? 0xffffffffULL :
                        (size == 2) ? 0xffffULL :
                        (size == 1) ? 0xffULL : ~0ULL;
        /* Only affect low bits, keep upper bits intact */
        newv = (cur & ~mask) | (val & mask);
    }

    pcibase_reg_write64(s, addr, newv);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* cn10k-rng driver does not use PIO */
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* cn10k-rng driver does not use PIO */
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

    /* Initialize register shadows to sane defaults */
    memset(s->regs, 0, sizeof(s->regs));

    /* Keep EBG health status bit 20 cleared (no error) */
    pcibase_reg_write64(s, CN10K_RNM_PF_EBG_HEALTH,
                        pcibase_reg_read64(s, CN10K_RNM_PF_EBG_HEALTH) & ~BIT_ULL(20));

    /* Initialize RNG output registers with non-zero random values */
    uint64_t rnd = pcibase_rng_get_random(s);
    pcibase_reg_write64(s, CN10K_RNM_PF_RANDOM, rnd);
    pcibase_reg_write64(s, CN10K_RNM_PF_TRNG_DAT, rnd);
    /* Indicate valid TRNG result */
    pcibase_reg_write64(s, CN10K_RNM_PF_TRNG_RES, 1ULL);
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

    /* Static PCI configuration matching driver's first id_table entry */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  CN10K_RNG_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  CN10K_RNG_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CN10K_RNG_CLASS_ID );
    /* Choose a revision that will NOT be rejected by cn10k_is_extended_trng_regs_supported() */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x52); /* arbitrary non-A0/A1/A0/etc */
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Subsystem IDs: pick a value that is not in any of the reject lists
     * so cn10k_is_extended_trng_regs_supported() returns true.
     */
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, CN10K_RNG_VENDOR_ID);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x0000);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize PRNG seed */
    s->prng.state = 0x123456789abcdef0ULL ^ (uint64_t)qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

    /* BAR Initialization: single MMIO BAR 0 large enough for used registers */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    /* Need to cover at least up to 0x1008; choose 8 KiB */
    s->bar_info[0].size = 0x2000;
    s->bar_info[0].name = "cn10k-rng-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Ensure reset initializes registers */
    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "cn10k_rng_pci",
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
