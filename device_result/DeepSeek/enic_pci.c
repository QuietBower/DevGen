/*
 * QEMU 8.2.10 PCI device model for Cisco ENIC NIC (probe skeleton)
 *
 * This model emulates the control-plane subset needed by the Linux ENIC
 * driver to probe and bind. It does NOT attempt to emulate
 *
 * - packet movement through vNIC queues
 * - the DEVCMD2 DMA command queue
 * - interrupts/queues/completions
 * - actual DMA datapath
 *
 * The BAR0 resource directory and the legacy, MMIO-based DEVCMD1 interface
 * are implemented below because both are required during enic_probe().
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

#define TYPE_PCIBASE_DEVICE "enic_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs: use first entry in enic_id_table[] */
#define ENIC_PCI_VENDOR_ID_CISCO   0x1137
#define PCI_DEVICE_ID_CISCO_VIC_ENET         0x0043
#define PCI_DEVICE_ID_CISCO_VIC_ENET_DYN     0x0044
#define PCI_DEVICE_ID_CISCO_VIC_ENET_VF      0x0071
#define ENIC_PCI_DEVICE_ID_ENET    PCI_DEVICE_ID_CISCO_VIC_ENET

/* Class: Ethernet controller */
#define ENIC_PCI_CLASS_ID          PCI_CLASS_NETWORK_ETHERNET

#define ENIC_BARS_MAX              6

/* BAR0 starts with a firmware resource directory. */
#define VNIC_RES_MAGIC             0x766e6963U
#define VNIC_RES_VERSION           0U
#define VNIC_RES_STRIDE            128U

#define RES_TYPE_EOL               0U
#define RES_TYPE_WQ                1U
#define RES_TYPE_RQ                2U
#define RES_TYPE_CQ                3U
#define RES_TYPE_INTR_CTRL         10U
#define RES_TYPE_DEVCMD            16U

#define ENIC_WQ_OFFSET             0x1000U
#define ENIC_RQ_OFFSET             0x1080U
#define ENIC_CQ_OFFSET             0x1100U
#define ENIC_INTR_OFFSET           0x1200U
#define ENIC_DEVCMD_OFFSET         0x1300U
#define ENIC_DEVCMD_SIZE           128U
#define ENIC_BAR0_USED_SIZE        (ENIC_DEVCMD_OFFSET + ENIC_DEVCMD_SIZE)

/* struct vnic_devcmd offsets and command-number decoding. */
#define ENIC_DEVCMD_STATUS         0U
#define ENIC_DEVCMD_CMD            4U
#define ENIC_DEVCMD_ARGS           8U
#define ENIC_DEVCMD_NARGS          15U
#define ENIC_CMD_NMASK             ((1U << 14) - 1)

#define ENIC_CMD_DEV_SPEC_NR       2U
#define ENIC_CMD_GET_MAC_ADDR_NR   9U
#define ENIC_CMD_SOFT_RESET_ST_NR  20U
#define ENIC_CMD_OPEN_STATUS_NR    24U
#define ENIC_CMD_INIT_V1_NR        26U
#define ENIC_CMD_INIT_STATUS_NR    31U
#define ENIC_CMD_CAPABILITY_NR     36U

/* Offsets in struct vnic_enet_config returned by CMD_DEV_SPEC. */
#define ENIC_CFG_FLAGS             0U
#define ENIC_CFG_WQ_DESC_COUNT     4U
#define ENIC_CFG_RQ_DESC_COUNT     8U
#define ENIC_CFG_MTU               12U
#define ENIC_CFG_INTR_TIMER_TYPE   16U
#define ENIC_CFG_INTR_MODE         17U
#define ENIC_CFG_INTR_TIMER_USEC   36U
#define ENIC_CFG_LOOP_TAG          40U
#define ENIC_CFG_NUM_ARFS          44U
#define ENIC_CFG_MAX_RQ_RING       112U
#define ENIC_CFG_MAX_WQ_RING       116U
#define ENIC_CFG_MAX_CQ_RING       120U

#define ENIC_MIN_DESC_COUNT        64U
#define ENIC_DEFAULT_MTU           1500U
#define ENIC_INTR_MODE_MSI         1U
#define ENIC_MSIX_VECTORS          4U

/* Interrupt indices used by driver */
#define ENIC_LEGACY_ERR_INTR   1
#define ENIC_LEGACY_IO_INTR    0
#define ENIC_LEGACY_NOTIFY_INTR 2

#define ENIC_MSIX_ERR_INTR     0
#define ENIC_MSIX_NOTIFY_INTR  1
#define ENIC_MSIX_RESERVED_INTR 2
#define ENIC_MSIX_IO_INTR_BASE ENIC_MSIX_RESERVED_INTR

/* Forward declarations matching driver context (opaque to QEMU) */
struct vnic_enet_config;
struct vnic_dev_bar;


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

    /* BAR0 resource directory, queue controls, and DEVCMD1 registers. */
    uint8_t mmio[ENIC_BAR0_USED_SIZE];

    uint32_t dummy_reg0;
    uint32_t dummy_reg1;
};

/* Internal helper for status-triggered signaling. Currently unused because
 * the driver never touches device-specific interrupt status registers in
 * the provided code; it relies entirely on MSI/MSI-X/legacy wiring and
 * vnic_* helpers talking to hardware via BAR resources.
 */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)pdev;
}

/* Packet-ring DMA would be implemented here. It is deliberately not needed
 * for PCI probe: dma_set_mask_and_coherent() negotiates an address width and
 * dma_alloc_coherent() allocates rings, but no packet DMA is required before
 * register_netdev() completes.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)pdev;
    (void)is_write;
}

static void pcibase_add_resource(PCIBaseState *s, unsigned int index,
                                 uint8_t type, uint32_t offset,
                                 uint32_t count)
{
    uint8_t *r = &s->mmio[8 + index * 12];

    r[0] = type;
    r[1] = 0; /* BAR0 */
    stl_le_p(r + 4, offset);
    stl_le_p(r + 8, count);
}

static void pcibase_devcmd_set_arg(PCIBaseState *s, unsigned int index,
                                    uint64_t value)
{
    g_assert(index < ENIC_DEVCMD_NARGS);
    stq_le_p(&s->mmio[ENIC_DEVCMD_OFFSET + ENIC_DEVCMD_ARGS + index * 8],
             value);
}

static uint64_t pcibase_devcmd_get_arg(PCIBaseState *s, unsigned int index)
{
    g_assert(index < ENIC_DEVCMD_NARGS);
    return ldq_le_p(&s->mmio[ENIC_DEVCMD_OFFSET + ENIC_DEVCMD_ARGS +
                             index * 8]);
}

static uint64_t pcibase_config_value(uint32_t offset)
{
    switch (offset) {
    case ENIC_CFG_FLAGS:
        return 0; /* no datapath offloads are advertised */
    case ENIC_CFG_WQ_DESC_COUNT:
    case ENIC_CFG_RQ_DESC_COUNT:
    case ENIC_CFG_MAX_RQ_RING:
    case ENIC_CFG_MAX_WQ_RING:
    case ENIC_CFG_MAX_CQ_RING:
        return ENIC_MIN_DESC_COUNT;
    case ENIC_CFG_MTU:
        return ENIC_DEFAULT_MTU;
    case ENIC_CFG_INTR_TIMER_TYPE:
    case ENIC_CFG_INTR_TIMER_USEC:
    case ENIC_CFG_LOOP_TAG:
    case ENIC_CFG_NUM_ARFS:
        return 0;
    case ENIC_CFG_INTR_MODE:
        return ENIC_INTR_MODE_MSI;
    default:
        return 0;
    }
}

/* Complete the small, synchronous subset of DEVCMD1 used by probe. */
static void pcibase_handle_devcmd(PCIBaseState *s)
{
    uint8_t *devcmd = &s->mmio[ENIC_DEVCMD_OFFSET];
    uint32_t cmd = ldl_le_p(devcmd + ENIC_DEVCMD_CMD);
    uint32_t nr = cmd & ENIC_CMD_NMASK;
    uint32_t offset;

    /* The emulated commands below all complete immediately and succeed. */
    stl_le_p(devcmd + ENIC_DEVCMD_STATUS, 0);

    switch (nr) {
    case ENIC_CMD_CAPABILITY_NR:
        /* Report optional commands as unsupported. The driver has compatible
         * fallbacks for INIT, interrupt timer conversion and extended CQs.
         */
        pcibase_devcmd_set_arg(s, 0, 1);
        pcibase_devcmd_set_arg(s, 1, 0);
        break;
    case ENIC_CMD_DEV_SPEC_NR:
        offset = (uint32_t)pcibase_devcmd_get_arg(s, 0);
        pcibase_devcmd_set_arg(s, 0, pcibase_config_value(offset));
        break;
    case ENIC_CMD_GET_MAC_ADDR_NR:
        /* Bytes as observed by the guest: 02:00:00:00:00:01. */
        pcibase_devcmd_set_arg(s, 0, UINT64_C(0x0000010000000002));
        break;
    case ENIC_CMD_OPEN_STATUS_NR:
    case ENIC_CMD_SOFT_RESET_ST_NR:
    case ENIC_CMD_INIT_STATUS_NR:
    case ENIC_CMD_INIT_V1_NR:
        pcibase_devcmd_set_arg(s, 0, 0); /* operation is complete */
        break;
    default:
        /* Configuration/no-wait commands need no result beyond success. */
        break;
    }
}

static void pcibase_init_bar0(PCIBaseState *s)
{
    memset(s->mmio, 0, sizeof(s->mmio));

    stl_le_p(&s->mmio[0], VNIC_RES_MAGIC);
    stl_le_p(&s->mmio[4], VNIC_RES_VERSION);

    pcibase_add_resource(s, 0, RES_TYPE_WQ, ENIC_WQ_OFFSET, 1);
    pcibase_add_resource(s, 1, RES_TYPE_RQ, ENIC_RQ_OFFSET, 1);
    pcibase_add_resource(s, 2, RES_TYPE_CQ, ENIC_CQ_OFFSET, 2);
    pcibase_add_resource(s, 3, RES_TYPE_INTR_CTRL, ENIC_INTR_OFFSET, 1);
    pcibase_add_resource(s, 4, RES_TYPE_DEVCMD, ENIC_DEVCMD_OFFSET,
                         ENIC_DEVCMD_SIZE);
    pcibase_add_resource(s, 5, RES_TYPE_EOL, 0, 0);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= sizeof(s->mmio) || size > sizeof(s->mmio) - addr) {
        return 0;
    }

    return ldn_le_p(&s->mmio[addr], size);
}

static void pcibase_mmio_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= sizeof(s->mmio) || size > sizeof(s->mmio) - addr) {
        return;
    }

    /* The resource directory is firmware-owned and read-only. */
    if (addr < ENIC_WQ_OFFSET) {
        return;
    }

    stn_le_p(&s->mmio[addr], size, val);

    if (addr == ENIC_DEVCMD_OFFSET + ENIC_DEVCMD_CMD && size == 4) {
        pcibase_handle_devcmd(s);
    }
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

static void pcibase_pio_write(void *opaque, hwaddr addr,
                              uint64_t val, unsigned size)
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
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);

    pci_device_reset(PCI_DEVICE(dev));

    pcibase_init_bar0(s);
    s->dummy_reg0 = 0;
    s->dummy_reg1 = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s,
                                 BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    /* PCI requires BAR sizes to be a power of two. Use pow2ceil() to avoid
     * QEMU assertions when the size is not already aligned.
     */
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s,
                              bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY,
                         mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s,
                              bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY,
                         mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration matching the ENIC device identity */
    pci_set_word(pci_conf + PCI_VENDOR_ID, ENIC_PCI_VENDOR_ID_CISCO);
    pci_set_word(pci_conf + PCI_DEVICE_ID, ENIC_PCI_DEVICE_ID_ENET);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, ENIC_PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Expose PCIe capabilities as the real hardware is PCIe. */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* Power management capability; minimal stub. */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0,
                                    PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR0 contains the firmware resource directory and vNIC controls. */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 1 * MiB;   /* generic MMIO aperture */
    s->bar_info[0].name  = "enic-mmio";

    for (int i = 1; i < ENIC_BARS_MAX; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* The helper arguments are (number of vectors, BAR number). The old
     * call used (1, 0), unintentionally installing the MSI-X table in BAR0
     * and replacing/conflicting with the ENIC MMIO aperture.
     */
    s->has_msix = false;
    if (msix_init_exclusive_bar(pdev, ENIC_MSIX_VECTORS, 1, errp)) {
        return;
    }
    s->has_msix = true;

    s->has_msi = false;
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
    s->has_msi = true;

    pcibase_init_bar0(s);
    s->dummy_reg0 = 0;
    s->dummy_reg1 = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit_exclusive_bar(pdev);
    }
    if (s->has_msi) {
        msi_uninit(pdev);
    }

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "enic_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_BUFFER(mmio, PCIBaseState),
        VMSTATE_UINT32(dummy_reg0, PCIBaseState),
        VMSTATE_UINT32(dummy_reg1, PCIBaseState),
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

    /* The real device is a network controller, but we keep the MISC
     * category here since the model does not implement a full network
     * datapath.
     */
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
        .name          = TYPE_PCIBASE_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseState),
        .class_init    = pcibase_class_init,
        .interfaces    = interfaces,
    };

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types)
