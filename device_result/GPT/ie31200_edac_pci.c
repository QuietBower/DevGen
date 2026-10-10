/*
 * QEMU PCI device model for Intel IE31200 EDAC host bridge compatible
 * Target QEMU version: 8.2.x
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

#define TYPE_PCIBASE_DEVICE "ie31200_edac_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_DEVICE_ID_INTEL_IE31200_HB_1  0x0108
#define PCI_DEVICE_ID_INTEL_IE31200_HB_2  0x010c
#define PCI_DEVICE_ID_INTEL_IE31200_HB_3  0x0150
#define PCI_DEVICE_ID_INTEL_IE31200_HB_4  0x0158
#define PCI_DEVICE_ID_INTEL_IE31200_HB_5  0x015c
#define PCI_DEVICE_ID_INTEL_IE31200_HB_6  0x0c04
#define PCI_DEVICE_ID_INTEL_IE31200_HB_7  0x0c08
#define PCI_DEVICE_ID_INTEL_IE31200_HB_8  0x190F
#define PCI_DEVICE_ID_INTEL_IE31200_HB_9  0x1918
#define PCI_DEVICE_ID_INTEL_IE31200_HB_10 0x191F
#define PCI_DEVICE_ID_INTEL_IE31200_HB_11 0x590f
#define PCI_DEVICE_ID_INTEL_IE31200_HB_12 0x5918
#define PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_MASK 0x3e00
#define PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_1    0x3e0f
#define PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_2    0x3e18
#define PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_3    0x3e1f
#define PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_4    0x3e30
#define PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_5    0x3e31
#define PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_6    0x3e32
#define PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_7    0x3e33
#define PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_8    0x3ec2
#define PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_9    0x3ec6
#define PCI_DEVICE_ID_INTEL_IE31200_HB_CFL_10   0x3eca
#define PCI_DEVICE_ID_INTEL_IE31200_RPL_S_1  0xa703
#define PCI_DEVICE_ID_INTEL_IE31200_RPL_S_2  0x4640
#define PCI_DEVICE_ID_INTEL_IE31200_RPL_S_3  0x4630
#define PCI_DEVICE_ID_INTEL_IE31200_RPL_S_4  0xa700
#define PCI_DEVICE_ID_INTEL_IE31200_RPL_S_5  0xa740
#define PCI_DEVICE_ID_INTEL_IE31200_RPL_S_6  0xa704
#define PCI_DEVICE_ID_INTEL_IE31200_RPL_HX_1 0xa702
#define PCI_DEVICE_ID_INTEL_IE31200_ADL_S_1 0x4660
#define PCI_DEVICE_ID_INTEL_IE31200_ADL_S_2 0x4668
#define PCI_DEVICE_ID_INTEL_IE31200_ADL_S_3 0x4648
#define PCI_DEVICE_ID_INTEL_IE31200_BTL_S_1 0x4639
#define PCI_DEVICE_ID_INTEL_IE31200_BTL_S_2 0x463c
#define PCI_DEVICE_ID_INTEL_IE31200_BTL_S_3 0x4642
#define PCI_DEVICE_ID_INTEL_IE31200_BTL_S_4 0x4643
#define PCI_DEVICE_ID_INTEL_IE31200_BTL_S_5 0xa731
#define PCI_DEVICE_ID_INTEL_IE31200_BTL_S_6 0xa732
#define PCI_DEVICE_ID_INTEL_IE31200_BTL_S_7 0xa733
#define PCI_DEVICE_ID_INTEL_IE31200_BTL_S_8 0xa741
#define PCI_DEVICE_ID_INTEL_IE31200_BTL_S_9 0xa744
#define PCI_DEVICE_ID_INTEL_IE31200_BTL_S_10 0xa745

#define BIT(nr)            (1UL << (nr))
#define BIT_ULL(nr)        (1ULL << (nr))

#define IE31200_RANKS_PER_CHANNEL  8
#define IE31200_DIMMS_PER_CHANNEL  2
#define IE31200_CHANNELS           2
#define IE31200_IMC_NUM            2
#define IE31200_MCHBAR_LOW         0x48
#define IE31200_MCHBAR_HIGH        0x4c
#define IE31200_ERRSTS             0xc8
#define IE31200_ERRSTS_UE          BIT(1)
#define IE31200_ERRSTS_CE          BIT(0)
#define IE31200_ERRSTS_BITS        (IE31200_ERRSTS_UE | IE31200_ERRSTS_CE)
#define IE31200_CAPID0             0xe4
#define IE31200_CAPID0_PDCD        BIT(4)
#define IE31200_CAPID0_DDPCD       BIT(6)
#define IE31200_CAPID0_ECC         BIT(1)

/* New constant from driver for this iteration */
#define I3200_CAPID0               0xe0


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

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* simple representations of config registers used by the driver */
    uint16_t errsts_reg;
    uint8_t capid0_bytes[4];

    /* mirror of MCHBAR low/high dwords */
    uint32_t mchbar_low;
    uint32_t mchbar_high;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

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

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (s->mmio_backing && addr + size <= s->mmio_backing_size) {
        uint64_t val = 0;
        memcpy(&val, s->mmio_backing + addr, size);
        return val;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (s->mmio_backing && addr + size <= s->mmio_backing_size) {
        memcpy(s->mmio_backing + addr, &val, size);
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    (void)s;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n", TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
    (void)s;
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    s->errsts_reg = 0;
    s->capid0_bytes[0] = 0;
    s->capid0_bytes[1] = 0;
    s->capid0_bytes[2] = 0;
    s->capid0_bytes[3] = 0;

    s->mchbar_low = 0;
    s->mchbar_high = 0;

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }
}

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    (void)errp;
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val;

    /* ERRSTS is a 16-bit register at 0xc8 */
    if (addr == IE31200_ERRSTS && len == 2) {
        val = s->errsts_reg;
    /* Mirror CAPID0 32-bit dword when accessed at its base offset */
    } else if (addr == IE31200_CAPID0 && len == 4) {
        val = s->capid0_bytes[0] |
              (s->capid0_bytes[1] << 8) |
              (s->capid0_bytes[2] << 16) |
              (s->capid0_bytes[3] << 24);
    /* Also allow byte-wise reads within the CAPID0 dword */
    } else if (addr >= IE31200_CAPID0 && addr < IE31200_CAPID0 + 4 && len == 1) {
        uint32_t idx = addr - IE31200_CAPID0;
        val = s->capid0_bytes[idx];
    /* MCHBAR low/high dwords */
    } else if (addr == IE31200_MCHBAR_LOW && len == 4) {
        val = s->mchbar_low;
    } else if (addr == IE31200_MCHBAR_HIGH && len == 4) {
        val = s->mchbar_high;
    /* I3200_CAPID0 is another CAPID-like dword at 0xe0. There is no
     * separate mirror in the current device state, so just fall through
     * to default PCI config handling for it.
     */
    } else {
        val = pci_default_read_config(pdev, addr, len);
    }

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
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }

    if (addr == IE31200_ERRSTS && len == 2) {
        uint16_t wval = (uint16_t)val;
        /* Writing 1s clears the corresponding error bits */
        s->errsts_reg &= ~wval;
        return;
    }

    if (addr == IE31200_MCHBAR_LOW && len == 4) {
        s->mchbar_low = val;
        pci_default_write_config(pdev, addr, val, len);
        return;
    }

    if (addr == IE31200_MCHBAR_HIGH && len == 4) {
        s->mchbar_high = val;
        pci_default_write_config(pdev, addr, val, len);
        return;
    }

    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x8086);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_INTEL_IE31200_HB_1);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_HOST);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    s->num_bars = 0;

    s->mmio_backing_size = 0;
    s->mmio_backing = NULL;

    s->has_msi = false;
    s->has_msix = false;

    /* initialize CAPID0 bytes so that the driver sees ECC capable,
     * dual-channel enabled and 2 DIMMs per channel enabled
     *
     * The driver checks:
     *  - ecc_capable(): reads IE31200_CAPID0 + 3 (4th byte) and tests
     *    IE31200_CAPID0_ECC; ECC is supported if that bit is 0.
     *  - how_many_channels(): reads CAPID0 + 8 (8th byte) and tests
     *    bit 5 (0x20) for dual-channel disable and bit 4 (0x10) for
     *    "2 DIMMS per channel" disable.
     *
     * We program these bytes so that ecc_capable() returns true and
     * how_many_channels() reports dual-channel mode with 2 DIMMs per
     * channel enabled.
     */
    /* bytes 0-2: not used by the visible driver logic here */
    s->capid0_bytes[0] = 0x00;
    s->capid0_bytes[1] = 0x00;
    s->capid0_bytes[2] = 0x00;
    /* 4th byte (offset +3): make ECC capable => IE31200_CAPID0_ECC clear */
    s->capid0_bytes[3] = 0x00;

    /* Note: the driver reads CAPID0 + 8, which is beyond this 32-bit
     * CAPID0 dword. That access goes through normal PCI config space
     * and is not handled by capid0_bytes[] mirroring.
     */

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
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

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
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

