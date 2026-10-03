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

#define TYPE_PCIBASE_DEVICE "atp870u_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_ARTOP        0x1191
#define PCI_CLASS_STORAGE_SCSI     0x0100
#define ATP880_DEVID2 0x8081
#define ATP880_DEVID1 0x8080
#define ATP885_DEVID 0x808A
#define qcnt        32
#define ATP870U_MAX_SECTORS 128
#define ATP870U_SCATTER 128

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

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* simple register banks backing the 3 I/O spaces used by the driver */
    uint8_t base_regs[0x100];   /* baseport (config/EEPROM etc.), size unknown, pick 0x100 */
    uint8_t ch_io_regs[2][0x40]; /* per-channel IO port block, driver uses up to 0x1f/0x1c */
    uint8_t ch_pci_regs[2][0x20]; /* per-channel "pciport" block, driver uses up to 0x04 */

    /* IRQ line state */
    qemu_irq irq;
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

/* We encode BAR index into addr high bits when creating separate MemoryRegions
 * but in this template we have one ops per BAR, so we provide wrappers per BAR
 * via opaque if needed. For simplicity, we will assume pcibase_pio_ops is
 * installed separately per BAR and QEMU passes addr relative to that BAR.
 * We therefore need a way to know which BAR we're in; we can't from addr, so
 * we create small wrappers per BAR is not provided here. Instead, we will
 * treat:
 *   BAR0 uses pcibase_pio_ops and accesses base_regs
 *   BAR1/2 use pcibase_pio_ops but in realize we will set different opaque
 * objects; however template only passes 's'. To stay within template, we
 * approximate mapping:
 *   BAR0: baseport+addr
 *   BAR1: ioport[0]+addr
 *   BAR2: ioport[1]+addr
 *   BAR3: pciport[0]+addr
 *   BAR4: pciport[1]+addr
 * Since we can't know bar index, we instead allocate disjoint address
 * subranges by size in the same region is not supported. To remain compliant
 * with template, we will map simply based on the sizes we chose: 
 * BAR0 0x00-0x3f: base_regs
 * BAR1 0x40-0x7f: ch_io_regs[0]
 * BAR2 0x80-0xbf: ch_io_regs[1]
 * BAR3 0xc0-0xdf: ch_pci_regs[0]
 * BAR4 0xe0-0xff: ch_pci_regs[1]
 */

static bool pcibase_route_pio(PCIBaseState *s, hwaddr addr,
                              uint8_t **array, size_t *len,
                              int *which, hwaddr *off)
{
    if (addr < 0x40) {
        *array = s->base_regs;
        *len = sizeof(s->base_regs);
        *which = 0;
        *off = addr;
        return true;
    } else if (addr < 0x80) {
        *array = s->ch_io_regs[0];
        *len = sizeof(s->ch_io_regs[0]);
        *which = 10;
        *off = addr - 0x40;
        return true;
    } else if (addr < 0xc0) {
        *array = s->ch_io_regs[1];
        *len = sizeof(s->ch_io_regs[0]);
        *which = 11;
        *off = addr - 0x80;
        return true;
    } else if (addr < 0xe0) {
        *array = s->ch_pci_regs[0];
        *len = sizeof(s->ch_pci_regs[0]);
        *which = 20;
        *off = addr - 0xc0;
        return true;
    } else if (addr < 0x100) {
        *array = s->ch_pci_regs[1];
        *len = sizeof(s->ch_pci_regs[0]);
        *which = 21;
        *off = addr - 0xe0;
        return true;
    }
    return false;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (s->mmio_backing && addr + size <= s->mmio_backing_size) {
        memcpy(&val, s->mmio_backing + addr, size);
        return val;
    }

    qemu_log_mask(LOG_UNIMP, "[atp870u_pci] mmio_read addr=%" PRIx64 " size=%u\n",
                  (uint64_t)addr, size);
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (s->mmio_backing && addr + size <= s->mmio_backing_size) {
        memcpy(s->mmio_backing + addr, &val, size);
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[atp870u_pci] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n",
                  (uint64_t)addr, (uint64_t)val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t *array = NULL;
    size_t len = 0;
    int which = -1;
    hwaddr off = 0;
    uint32_t v = 0;

    if (!pcibase_route_pio(s, addr, &array, &len, &which, &off)) {
        qemu_log_mask(LOG_UNIMP, "[atp870u_pci] pio_read out of range addr=%" PRIx64 " size=%u\n",
                      (uint64_t)addr, size);
        return 0xff;
    }

    if (off + size > len) {
        qemu_log_mask(LOG_UNIMP, "[atp870u_pci] pio_read overflow addr=%" PRIx64 " size=%u\n",
                      (uint64_t)addr, size);
        return 0xff;
    }

    /* Implement minimal behavior required by atp_is() polling */
    if (which == 10 || which == 11) {
        int ch = (which == 10) ? 0 : 1;
        hwaddr reg = off;
        /* atp_is() waits for interrupt flag in 0x1f */
        if (reg == 0x1f && size == 1) {
            /* ensure bit7 set so loops that poll (reg & 0x80) terminate */
            uint8_t val8 = s->ch_io_regs[ch][0x1f];
            val8 |= 0x80;
            s->ch_io_regs[ch][0x1f] = val8;
        }
        /* status register 0x17 is tested against many phase codes; for
         * discovery path we return 0x8e (RESELECT) by default so that
         * the driver's condition checks can pass and loops terminate.
         */
        if (reg == 0x17 && size == 1) {
            uint8_t val8 = s->ch_io_regs[ch][0x17];
            if (val8 == 0x00) {
                val8 = 0x8e;
                s->ch_io_regs[ch][0x17] = val8;
            }
        }
        /* data-in register 0x19 is read when (0x1f & 0x01) != 0; expose
         * a simple fixed pattern so mbuf[] gets deterministic but
         * non-random values. Here we just reuse whatever is stored.
         */
    }

    memcpy(&v, array + off, size);
    return v;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t *array = NULL;
    size_t len = 0;
    int which = -1;
    hwaddr off = 0;

    if (!pcibase_route_pio(s, addr, &array, &len, &which, &off)) {
        qemu_log_mask(LOG_UNIMP, "[atp870u_pci] pio_write out of range addr=%" PRIx64 " val=%" PRIx64 " size=%u\n",
                      (uint64_t)addr, (uint64_t)val, size);
        return;
    }

    if (off + size > len) {
        qemu_log_mask(LOG_UNIMP, "[atp870u_pci] pio_write overflow addr=%" PRIx64 " val=%" PRIx64 " size=%u\n",
                      (uint64_t)addr, (uint64_t)val, size);
        return;
    }

    memcpy(array + off, &val, size);

    /* Minimal interrupt cause simulation: channel IO reg 0x1f bit 7 indicates interrupt pending. */
    if (which == 10 || which == 11) {
        int ch = (which == 10) ? 0 : 1;
        hwaddr reg = off;

        /* atp_is() writes many command sequences to registers 0x03-0x08,
         * 0x10, 0x11, 0x12, 0x13, 0x14 and then kicks the engine via 0x18.
         * The driver then polls 0x1f bit7 and inspects 0x17. To allow this
         * to complete, when 0x18 is written we immediately assert the
         * interrupt bit and set 0x17 to a completion phase that the driver
         * accepts in its condition checks (0x11 or 0x8e or 0x16 depending on
         * context). For the inquiry and negotiation paths it is enough to
         * return 0x8e so that the loops terminate and fall through.
         */
        if (reg == 0x18 && size == 1) {
            uint8_t cmd = (uint8_t)val;
            (void)cmd; /* driver distinguishes by surrounding setup, we do not */

            /* set interrupt pending bit */
            s->ch_io_regs[ch][0x1f] |= 0x80;
            /* choose 0x8e as generic "good" status so both
             *  (== 0x11 || == 0x8e) checks and loops expecting 0x8e
             *  make progress.
             */
            s->ch_io_regs[ch][0x17] = 0x8e;

            /* also raise PCI INTx line */
            if (!s->irq_level) {
                s->irq_level = true;
                qemu_set_irq(s->irq, 1);
            }
        }
    }

    /* clear interrupt when driver writes to PCI DMA control (channel pci reg 0) with 0x00 */
    if (which == 20 || which == 21) {
        int ch = (which == 20) ? 0 : 1;
        (void)ch;
        if (off == 0x00 && (uint8_t)val == 0x00) {
            s->ch_io_regs[0][0x1f] &= ~0x80;
            s->ch_io_regs[1][0x1f] &= ~0x80;
            if (s->irq_level) {
                s->irq_level = false;
                qemu_set_irq(s->irq, 0);
            }
        }
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    memset(s->base_regs, 0, sizeof(s->base_regs));
    memset(s->ch_io_regs, 0, sizeof(s->ch_io_regs));
    memset(s->ch_pci_regs, 0, sizeof(s->ch_pci_regs));
    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }

    s->irq_level = false;
    if (s->irq) {
        qemu_set_irq(s->irq, 0);
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
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ARTOP);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ATP885_DEVID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_SCSI);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* configure BAR layout: one large IO window subdivided logically into
     * baseport, two channel io blocks, and two PCI DMA blocks, total 0x100.
     */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "atp870u-io";
    s->bar_info[0].sparse = false;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    /* no MSI/MSI-X in driver */

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    /* connect INTx */
    s->irq = pci_allocate_irq(pdev);
    s->irq_level = false;

    qemu_log_mask(LOG_UNIMP, "[atp870u_pci] device realized\n");
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

    qemu_log_mask(LOG_UNIMP, "[atp870u_pci] device uninit\n");
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
