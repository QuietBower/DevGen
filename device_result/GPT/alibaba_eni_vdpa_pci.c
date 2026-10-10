/*
 * QEMU PCI device model for Alibaba ENI vDPA (skeleton for Linux eni_vdpa driver)
 *
 * This model only implements the minimal PCI/virtio-legacy surface that the
 * eni_vdpa Linux driver directly touches in its own source file. All actual
 * virtio-legacy register semantics are delegated to missing helper logic
 * (vp_legacy_* family) that must be provided separately on the QEMU side if
 * full functionality is desired.
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

#define TYPE_PCIBASE_DEVICE "alibaba_eni_vdpa_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define ENI_MSIX_NAME_SIZE 256

/* ---------------- Virtio legacy layout bits used by the driver ------------- */

/* These offsets and constants mirror what eni_vdpa.c uses via
 * VIRTIO_PCI_* macros. Here we only include the ones directly accessed
 * by eni_vdpa.c.
 */

/* Queue notify register (16-bit), eni_vdpa writes qid here. */
#define VIRTIO_PCI_QUEUE_NOTIFY 0x10

/* Configuration area offset calculation helper placeholder. The driver uses
 * VIRTIO_PCI_CONFIG_OFF(vectors) which adds a vector-dependent offset.
 * We implement a minimal linear mapping: base + vectors * 4.
 */
#define VIRTIO_PCI_CONFIG_OFF(vectors) (0x20 + (vectors) * 4)

/* We also need some notion of device config space size used by driver. */
struct virtio_net_config {
    uint8_t mac[6];
    uint16_t status;
    uint16_t max_virtqueue_pairs;
    uint16_t mtu;
    uint32_t speed;
    uint8_t duplex;
    uint8_t rss_max_key_size;
    uint16_t rss_max_indirection_table_length;
    uint32_t supported_hash_types;
};

#define ENI_VIRTIO_CONFIG_SIZE (sizeof(struct virtio_net_config))

/* ---------------- BAR metadata definition ---------------------------------- */
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

/* ---------------- Device State --------------------------------------------- */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions */
    MemoryRegion bar_regions[6];

    /* optional linear backing for MMIO BAR0 */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt state feature flags */
    bool has_msi;
    bool has_msix;

    /* MSI-X: number of vectors requested by driver:
     * eni_vdpa_request_irq() uses queues + 1, up to the value we expose.
     */
    uint16_t msix_vectors;

    /* simple emulated register state */
    uint16_t last_queue_notify; /* most recently written qid */

    /* configuration space backing for virtio_net_config */
    uint8_t config_space[sizeof(struct virtio_net_config)];
};

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* ------------------------------------------------------------------ */
/* MemoryRegionOps                                                      */
/* ------------------------------------------------------------------ */
static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* Helper: register a BAR (MMIO or PIO)                                 */
/* ------------------------------------------------------------------ */
static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers                                                 */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* The driver accesses:
     *   - config space via eni_vdpa_get_config()/set_config(), which
     *     computes ioaddr = ldev->ioaddr + VIRTIO_PCI_CONFIG_OFF(vectors)
     *     and then ioread8(ioaddr + i).
     *   - it also uses vp_legacy_* helpers which internally will access
     *     virtio PCI common/ISR/etc. Those helpers are not emulated here.
     *
     * We therefore implement:
     *   - a simple config region at VIRTIO_PCI_CONFIG_OFF(msix_vectors)
     *     backed by s->config_space[]
     *   - the rest of the MMIO space returns 0.
     */

    /* MMIO BAR0 is assumed; addr is relative to BAR0 base. */

    uint64_t val = 0;

    /* Simple config window */
    hwaddr cfg_base = VIRTIO_PCI_CONFIG_OFF(s->msix_vectors);
    hwaddr cfg_end  = cfg_base + sizeof(s->config_space);

    if (addr >= cfg_base && addr < cfg_end) {
        hwaddr off = addr - cfg_base;
        unsigned i;

        if (off + size > sizeof(s->config_space)) {
            if (sizeof(s->config_space) > off) {
                size = sizeof(s->config_space) - off;
            } else {
                size = 0;
            }
        }

        for (i = 0; i < size; i++) {
            ((uint8_t *)&val)[i] = s->config_space[off + i];
        }
        return val;
    }

    /* Everything else: 0 */
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* The driver performs:
     *   - eni_vdpa_kick_vq(): iowrite16(qid, vring[qid].notify)
     *     where notify = ldev->ioaddr + VIRTIO_PCI_QUEUE_NOTIFY.
     *
     *   - eni_vdpa_set_config(): iowrite8() into config region based on
     *     VIRTIO_PCI_CONFIG_OFF(vectors) + offset.
     */

    /* Handle queue notify writes (16-bit) */
    if (addr == VIRTIO_PCI_QUEUE_NOTIFY && size == 2) {
        s->last_queue_notify = (uint16_t)(val & 0xFFFF);
        return;
    }

    /* Handle config writes */
    hwaddr cfg_base = VIRTIO_PCI_CONFIG_OFF(s->msix_vectors);
    hwaddr cfg_end  = cfg_base + sizeof(s->config_space);

    if (addr >= cfg_base && addr < cfg_end) {
        hwaddr off = addr - cfg_base;
        unsigned i;

        if (off + size > sizeof(s->config_space)) {
            if (sizeof(s->config_space) > off) {
                size = sizeof(s->config_space) - off;
            } else {
                size = 0;
            }
        }

        for (i = 0; i < size; i++) {
            s->config_space[off + i] = ((uint8_t *)&val)[i];
        }
        return;
    }

    /* All other writes ignored */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* eni_vdpa.c does not use any PIO ports directly. */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* eni_vdpa.c does not use any PIO ports directly. */
}

/* ------------------------------------------------------------------ */
/* Reset                                                              */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    s->last_queue_notify = 0;
    memset(s->config_space, 0, sizeof(s->config_space));

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }
}

/* ------------------------------------------------------------------ */
/* DMA initialize (called from realize)                                */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
    case 1:
        val &= 0xFF;
        break;
    case 2:
        val &= 0xFFFF;
        break;
    case 4:
    default:
        break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }

    pci_default_write_config(pdev, addr, val, len);
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                              */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* The eni_vdpa driver uses vp_legacy_probe() which expects a legacy
     * virtio PCI device. We choose standard virtio-net IDs here so that
     * vp_legacy_probe() can bind. These constants are taken from the
     * virtio specification.
     */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1af4);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x1000); /* virtio-net legacy id */
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x00);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Set up BAR0 as MMIO of 4 KiB which is enough for basic legacy virtio */
    s->num_bars = 1;
    s->bar_info[0].index  = 0;
    s->bar_info[0].type   = BAR_TYPE_MMIO;
    s->bar_info[0].size   = 0x1000;
    s->bar_info[0].name   = "eni-vdpa-bar0";
    s->bar_info[0].sparse = false;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    /* Initialize backing buffer for MMIO if desired. Not strictly needed
     * for the minimal behaviour we implement, so we keep it NULL.
     */
    s->mmio_backing = NULL;
    s->mmio_backing_size = 0;

    /* Initialize MSI-X since the driver uses pci_alloc_irq_vectors(PCI_IRQ_MSIX)
     * and then pci_irq_vector() per queue + config. We expose a small number
     * of vectors; the exact number of queues is determined in the driver by
     * reading features, so here we just provide a generous upper bound.
     */
    s->msix_vectors = 8; /* arbitrary small number >= potential queues+1 */

    /* QEMU 8.2.10 msix_init signature (see hw/pci/msix.h):
     * int msix_init(PCIDevice *dev, unsigned short nentries,
     *               MemoryRegion *table_bar, unsigned table_bar_nr,
     *               uint8_t table_bar_idx, uint64_t table_offset,
     *               MemoryRegion *pba_bar, unsigned pba_bar_nr,
     *               uint8_t pba_bar_idx, uint64_t pba_offset);
     *
     * We pass NULL/0 for all BAR-related arguments to keep the original
     * behavior while satisfying the current API.
     */
    if (msix_init(pdev, s->msix_vectors,
                  NULL, 0, (uint8_t)0, 0,
                  NULL, 0, (uint8_t)0, 0) == 0) {
        s->has_msix = true;
    }

    /* DMA init (no-op for now) */
    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    memset(s->config_space, 0, sizeof(s->config_space));
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        /* QEMU 8.2.10 msix_uninit signature (see hw/pci/msix.h):
         * void msix_uninit(PCIDevice *dev, MemoryRegion *table_bar,
         *                  MemoryRegion *pba_bar);
         *
         * We pass NULL for both MemoryRegion pointers to match the
         * original intent while conforming to the API.
         */
        msix_uninit(pdev, NULL, NULL);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                      */
/* ------------------------------------------------------------------ */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static void pcibase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };

    static const TypeInfo pcibase_info = {
        .name          = TYPE_PCIBASE_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseState),
        .class_init    = pcibase_class_init,
        .interfaces    = interfaces,
    };

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);
