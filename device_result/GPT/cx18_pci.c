/*
 * QEMU PCI device model for Conexant CX23418 (cx18)
 *
 * Implementation based strictly on Linux driver drivers/media/pci/cx18/cx18-driver.c
 * Target QEMU version: 8.2.10
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

#define TYPE_PCIBASE_DEVICE "cx18_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_CX      0x14f1
#define PCI_DEVICE_ID_CX23418 0x5b7a
#define CX18_MAX_STREAMS      7
#define CX18_MAX_MDL_ACKS     2
#define MAX_MB_ARGUMENTS      6
#define CX18_VBI_FRAMES       32
#define CX18_SLICED_MPEG_DATA_MAXSZ 1584
#define CX18_SLICED_MPEG_DATA_BUFSZ (CX18_SLICED_MPEG_DATA_MAXSZ + 8)
#define CX18_CARD_MAX_VIDEO_INPUTS 6
#define CX18_CARD_MAX_AUDIO_INPUTS 3
#define CX18_CARD_MAX_TUNERS      2
#define CX18_MEM_SIZE 0x04000000
#define CX18_REG_OFFSET 0x02000000
#define CX18_MEM_OFFSET 0x00000000
#define SCB_OFFSET  0xDC0000
#define SCB_RESERVED_SIZE 0x10000
#define SW1_INT_SET                     0xc73100
#define SW1_INT_STATUS                  0xc73104
#define SW1_INT_ENABLE_PCI              0xc7311c
#define SW2_INT_STATUS                  0xc73144
#define SW2_INT_ENABLE_CPU              0xc73158
#define SW2_INT_ENABLE_PCI              0xc7315c
#define HW2_INT_CLR_STATUS              0xc730c4
#define HW2_I2C1_INT                    (1 << 22)
#define HW2_I2C2_INT                    (1 << 23)
#define IRQ_APU_TO_EPU_ACK     0x00000080
#define IRQ_APU_TO_EPU         0x00020000
#define IRQ_CPU_TO_EPU         0x00010000
#define IRQ_CPU_TO_EPU_ACK     0x00000008
#define IRQ_CPU_TO_PPU_ACK     0x00000004
#define IRQ_EPU_TO_CPU         0x00000008
#define IRQ_HPU_TO_PPU_ACK     0x00000400
#define IRQ_APU_TO_CPU         0x00000001
#define IRQ_PPU_TO_EPU_ACK     0x00008000
#define IRQ_EPU_TO_APU_ACK     0x00020000
#define IRQ_CPU_TO_APU_ACK     0x00000001
#define IRQ_CPU_TO_APU         0x00000010
#define IRQ_APU_TO_PPU         0x00002000
#define IRQ_EPU_TO_PPU         0x00008000
#define IRQ_PPU_TO_HPU_ACK     0x00004000
#define IRQ_EPU_TO_CPU_ACK     0x00010000
#define IRQ_CPU_TO_HPU_ACK     0x00000002
#define IRQ_APU_TO_HPU_ACK     0x00000020
#define IRQ_HPU_TO_CPU         0x00000002
#define IRQ_PPU_TO_HPU         0x00000400
#define IRQ_PPU_TO_APU         0x00000040
#define IRQ_APU_TO_PPU_ACK     0x00000040
#define IRQ_PPU_TO_EPU         0x00080000
#define IRQ_EPU_TO_HPU         0x00000800
#define IRQ_CPU_TO_PPU         0x00001000
#define IRQ_EPU_TO_PPU_ACK     0x00080000
#define IRQ_PPU_TO_APU_ACK     0x00002000
#define IRQ_CPU_TO_HPU         0x00000100
#define IRQ_HPU_TO_EPU         0x00040000
#define IRQ_EPU_TO_APU         0x00000080
#define IRQ_PPU_TO_CPU_ACK     0x00001000
#define IRQ_HPU_TO_EPU_ACK     0x00000800
#define IRQ_HPU_TO_CPU_ACK     0x00000100
#define IRQ_PPU_TO_CPU         0x00000004
#define IRQ_APU_TO_HPU         0x00000200
#define IRQ_APU_TO_CPU_ACK     0x00000010
#define IRQ_HPU_TO_PPU         0x00004000
#define IRQ_EPU_TO_HPU_ACK     0x00040000
#define IRQ_HPU_TO_APU         0x00000020
#define IRQ_HPU_TO_APU_ACK     0x00000200

/* BAR metadata */
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

    /* Simple register storage for a few MMIO locations used in driver */
    uint32_t sw1_int_set;
    uint32_t sw1_int_status;
    uint32_t sw1_int_enable_pci;
    uint32_t sw2_int_status;
    uint32_t sw2_int_enable_cpu;
    uint32_t sw2_int_enable_pci;
    uint32_t hw2_int_clr_status;

    /* Interrupt line state */
    bool irq_level;
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
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_update_irq(PCIBaseState *s)
{
    bool level = false;

    /* The real device has complex interrupt routing; here we only
     * expose a single legacy INTx line based on SW1/SW2 status bits
     * that are enabled for PCI. This is enough for the driver, which
     * just requests the IRQ and expects it to exist. There is no
     * explicit check of interrupt status in the provided code.
     */

    if ((s->sw1_int_status & s->sw1_int_enable_pci) != 0) {
        level = true;
    }
    if ((s->sw2_int_status & s->sw2_int_enable_pci) != 0) {
        level = true;
    }

    if (level != s->irq_level) {
        s->irq_level = level;
        pci_set_irq(PCI_DEVICE(s), level ? 1 : 0);
    }
}

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

static uint32_t pcibase_mmio_readl_reg(PCIBaseState *s, hwaddr addr)
{
    switch (addr) {
    case SW1_INT_SET:
        return s->sw1_int_set;
    case SW1_INT_STATUS:
        return s->sw1_int_status;
    case SW1_INT_ENABLE_PCI:
        return s->sw1_int_enable_pci;
    case SW2_INT_STATUS:
        return s->sw2_int_status;
    case SW2_INT_ENABLE_CPU:
        return s->sw2_int_enable_cpu;
    case SW2_INT_ENABLE_PCI:
        return s->sw2_int_enable_pci;
    case HW2_INT_CLR_STATUS:
        return s->hw2_int_clr_status;
    default:
        /* For all other addresses, we simply return 0 as the driver
         * code provided does not depend on specific values except for
         * cx18_read_reg(cx, 0xC72028) which is used as a revision ID.
         * Provide a constant non-0xffffffff pattern.
         */
        if (addr == 0xC72028) {
            return 0xff000000; /* match case 'A' in driver */
        }
        return 0;
    }
}

static void pcibase_mmio_writel_reg(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    switch (addr) {
    case SW1_INT_SET:
        /* Writing here sets bits in status */
        s->sw1_int_set |= val;
        s->sw1_int_status |= val;
        break;
    case SW1_INT_STATUS:
        /* Assume write-1-to-clear */
        s->sw1_int_status &= ~val;
        break;
    case SW1_INT_ENABLE_PCI:
        s->sw1_int_enable_pci = val;
        break;
    case SW2_INT_STATUS:
        /* write-1-to-clear */
        s->sw2_int_status &= ~val;
        break;
    case SW2_INT_ENABLE_CPU:
        s->sw2_int_enable_cpu = val;
        break;
    case SW2_INT_ENABLE_PCI:
        s->sw2_int_enable_pci = val;
        break;
    case HW2_INT_CLR_STATUS:
        /* Writing here clears hardware interrupt status; just store */
        s->hw2_int_clr_status = val;
        break;
    default:
        /* Unimplemented register: ignore */
        break;
    }

    pcibase_update_irq(s);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 4) {
        return pcibase_mmio_readl_reg(s, addr);
    } else if (size == 2 || size == 1) {
        uint32_t val = pcibase_mmio_readl_reg(s, addr & ~3u);
        unsigned shift = (addr & 3u) * 8;
        if (size == 2) {
            return (val >> shift) & 0xffffu;
        } else {
            return (val >> shift) & 0xffu;
        }
    }

    qemu_log_mask(LOG_GUEST_ERROR, "[cx18_pci] invalid mmio_read addr=%" PRIx64 " size=%u\n", (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 4) {
        pcibase_mmio_writel_reg(s, addr, (uint32_t)val);
        return;
    } else if (size == 2 || size == 1) {
        uint32_t old = pcibase_mmio_readl_reg(s, addr & ~3u);
        unsigned shift = (addr & 3u) * 8;
        uint32_t mask = (size == 2) ? 0xffffu : 0xffu;
        uint32_t newv = (old & ~(mask << shift)) | (((uint32_t)val & mask) << shift);
        pcibase_mmio_writel_reg(s, addr & ~3u, newv);
        return;
    }

    qemu_log_mask(LOG_GUEST_ERROR, "[cx18_pci] invalid mmio_write addr=%" PRIx64 " size=%u\n", (uint64_t)addr, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* The Linux driver does not use PIO for this device in the provided
     * code, so always return 0.
     */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO usage visible in driver; ignore */
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    s->sw1_int_set = 0;
    s->sw1_int_status = 0;
    s->sw1_int_enable_pci = 0;
    s->sw2_int_status = 0;
    s->sw2_int_enable_cpu = 0;
    s->sw2_int_enable_pci = 0;
    s->hw2_int_clr_status = 0;
    s->irq_level = false;
}

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    (void)errp;
    /* The provided driver code uses standard Linux DMA APIs, but the
     * detailed DMA descriptor formats and operations are not present in
     * this snippet, so no device-side DMA behaviour is implemented.
     */
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
    case 1:
        val &= 0xffu;
        break;
    case 2:
        val &= 0xffffu;
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

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_CX);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_CX23418);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_VIDEO);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR layout: driver uses pci_resource_start(pci_dev, 0) and expects
     * a 64MB MMIO space for encoder memory + registers.
     */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = CX18_MEM_SIZE;
    s->bar_info[0].name = "cx18-mmio";
    s->bar_info[0].sparse = false;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    /* Legacy INTx only. The driver uses request_irq with the PCI
     * irq line, and does not reference MSI/MSI-X explicitly in the
     * provided code.
     */
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
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
    k->realize = pcibase_realize;
    k->exit = pcibase_uninit;
    dc->reset = pcibase_reset;

    /* Category macro removed to fix compile error; this does not
     * affect runtime behaviour of the device model itself.
     */
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

