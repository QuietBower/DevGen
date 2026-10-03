/*
 * QEMU model for Brooktree Bt878 audio PCI device (snd_bt87x)
 * Generated from driver sound/pci/bt87x.c for QEMU 8.2.10
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

#define TYPE_PCIBASE_DEVICE "snd_bt87x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register offsets derived from driver source */
#define PCI_VENDOR_ID_BROOKTREE 0x109e
#define PCI_DEVICE_ID_BROOKTREE_878 0x0878

#define REG_INT_STAT        0x100
#define REG_INT_MASK        0x104
#define REG_GPIO_DMA_CTL    0x10c
#define REG_PACKET_LEN      0x110
#define REG_RISC_STRT_ADD   0x114
#define REG_RISC_COUNT      0x120

/* Interrupt bits */
#define INT_OFLOW   (1 <<  3)
#define INT_RISCI   (1 << 11)
#define INT_FBUS    (1 << 12)
#define INT_FTRGT   (1 << 13)
#define INT_FDSR    (1 << 14)
#define INT_PPERR   (1 << 15)
#define INT_RIPERR  (1 << 16)
#define INT_PABORT  (1 << 17)
#define INT_OCERR   (1 << 18)
#define INT_SCERR   (1 << 19)
#define INT_RISC_EN (1 << 27)
#define INT_RISCS_SHIFT      28

/* GPIO/DMA control bits */
#define CTL_FIFO_ENABLE     (1 <<  0)
#define CTL_RISC_ENABLE     (1 <<  1)
#define CTL_PKTP_4          (0 <<  2)
#define CTL_PKTP_8          (1 <<  2)
#define CTL_PKTP_16         (2 <<  2)
#define CTL_ACAP_EN         (1 <<  4)
#define CTL_DA_APP          (1 <<  5)
#define CTL_DA_IOM_AFE      (0 <<  6)
#define CTL_DA_IOM_DA       (1 <<  6)
#define CTL_DA_SDR_SHIFT    8
#define CTL_DA_SDR_MASK     (0xf<< 8)
#define CTL_DA_LMT          (1 << 12)
#define CTL_DA_ES2          (1 << 13)
#define CTL_DA_SBR          (1 << 14)
#define CTL_DA_DPM          (1 << 15)
#define CTL_DA_LRD_SHIFT    16
#define CTL_DA_MLB          (1 << 21)
#define CTL_DA_LRI          (1 << 22)
#define CTL_DA_SCE          (1 << 23)
#define CTL_A_SEL_STV       (0 << 24)
#define CTL_A_SEL_SFM       (1 << 24)
#define CTL_A_SEL_SML       (2 << 24)
#define CTL_A_SEL_SMXC      (3 << 24)
#define CTL_A_SEL_SHIFT     24
#define CTL_A_SEL_MASK      (3 << 24)
#define CTL_A_PWRDN         (1 << 26)
#define CTL_A_G2X           (1 << 27)
#define CTL_A_GAIN_SHIFT    28
#define CTL_A_GAIN_MASK     (0xf<<28)

#define BAR0_SIZE 0x1000

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Memory regions */
    MemoryRegion mmio;

    /* Shadow registers */
    uint32_t int_stat;
    uint32_t int_mask;
    uint32_t gpio_dma_ctl;
    uint32_t packet_len;
    uint32_t risc_start_addr;
    uint32_t risc_count;

    /* DMA emulation */
    QEMUTimer *dma_timer;
    bool dma_active;
    uint32_t current_block;
    uint32_t lines;
    uint32_t line_bytes;
};

static void bt87x_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool pending = s->int_stat & s->int_mask;
    pci_set_irq(pdev, pending ? 1 : 0);
}

static void bt87x_dma_timer(void *opaque)
{
    PCIBaseState *s = opaque;

    if (!s->dma_active) {
        return;
    }

    /* Set RISC interrupt with current block status */
    s->int_stat |= INT_RISCI | (s->current_block << INT_RISCS_SHIFT);
    bt87x_update_irq(s);

    /* Advance to next block */
    s->current_block = (s->current_block + 1) % s->lines;

    /* Re-arm timer for next period (1 ms simulated) */
    timer_mod(s->dma_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
}

static uint64_t bt87x_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "bt87x: bad read size %u\n", size);
        return 0;
    }

    switch (addr) {
    case REG_INT_STAT:
        val = s->int_stat;
        break;
    case REG_INT_MASK:
        val = s->int_mask;
        break;
    case REG_GPIO_DMA_CTL:
        val = s->gpio_dma_ctl;
        break;
    case REG_PACKET_LEN:
        val = s->packet_len;
        break;
    case REG_RISC_STRT_ADD:
        val = s->risc_start_addr;
        break;
    case REG_RISC_COUNT:
        val = 0; /* unused by driver */
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "bt87x: unknown read addr 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
    return val;
}

static void bt87x_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "bt87x: bad write size %u\n", size);
        return;
    }

    switch (addr) {
    case REG_INT_STAT:
        /* W1C */
        s->int_stat &= ~(val & 0xFFFFFFFF);
        bt87x_update_irq(s);
        break;
    case REG_INT_MASK:
        s->int_mask = val & 0xFFFFFFFF;
        bt87x_update_irq(s);
        break;
    case REG_GPIO_DMA_CTL:
        s->gpio_dma_ctl = val & 0xFFFFFFFF;
        /* Check DMA enable bits */
        {
            bool now_active = (val & (CTL_FIFO_ENABLE | CTL_RISC_ENABLE | CTL_ACAP_EN)) ==
                              (CTL_FIFO_ENABLE | CTL_RISC_ENABLE | CTL_ACAP_EN);
            if (!s->dma_active && now_active) {
                s->dma_active = true;
                s->current_block = 0;
                timer_mod(s->dma_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
            } else if (s->dma_active && !now_active) {
                s->dma_active = false;
                timer_del(s->dma_timer);
            }
        }
        break;
    case REG_PACKET_LEN:
        s->packet_len = val;
        s->line_bytes = val & 0xFFFF;
        s->lines = (val >> 16) & 0xFFFF;
        break;
    case REG_RISC_STRT_ADD:
        s->risc_start_addr = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "bt87x: unknown write addr 0x%" HWADDR_PRIx "\n", addr);
        break;
    }
}

static const MemoryRegionOps bt87x_mmio_ops = {
    .read = bt87x_mmio_read,
    .write = bt87x_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void bt87x_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->int_stat = 0;
    s->int_mask = 0;
    s->gpio_dma_ctl = 0;
    s->packet_len = 0;
    s->risc_start_addr = 0;
    s->risc_count = 0;
    s->dma_active = false;
    s->current_block = 0;
    s->lines = 0;
    s->line_bytes = 0;
    timer_del(s->dma_timer);
}

static void bt87x_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,   PCI_VENDOR_ID_BROOKTREE);
    pci_set_word(pci_conf + PCI_DEVICE_ID,   PCI_DEVICE_ID_BROOKTREE_878);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_AUDIO);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_set_byte(pci_conf + PCI_INTERRUPT_PIN, 1);

    /* PCIe capability */
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Initialize DMA timer */
    s->dma_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, bt87x_dma_timer, s);

    /* BAR 0: MMIO */
    memory_region_init_io(&s->mmio, OBJECT(s), &bt87x_mmio_ops, s, "bt87x-mmio", BAR0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
}

static void bt87x_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    timer_del(s->dma_timer);
    timer_free(s->dma_timer);
    /* No MSI/MSI-X used */
}

static const VMStateDescription vmstate_bt87x = {
    .name = TYPE_PCIBASE_DEVICE,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(int_stat, PCIBaseState),
        VMSTATE_UINT32(int_mask, PCIBaseState),
        VMSTATE_UINT32(gpio_dma_ctl, PCIBaseState),
        VMSTATE_UINT32(packet_len, PCIBaseState),
        VMSTATE_UINT32(risc_start_addr, PCIBaseState),
        VMSTATE_UINT32(risc_count, PCIBaseState),
        VMSTATE_BOOL(dma_active, PCIBaseState),
        VMSTATE_UINT32(current_block, PCIBaseState),
        VMSTATE_UINT32(lines, PCIBaseState),
        VMSTATE_UINT32(line_bytes, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void bt87x_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = bt87x_realize;
    k->exit    = bt87x_uninit;
    dc->reset  = bt87x_reset;
    dc->vmsd   = &vmstate_bt87x;
    dc->categories[0] = DEVICE_CATEGORY_MISC;
}

static void bt87x_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo bt87x_info = {
        .name          = TYPE_PCIBASE_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseState),
        .class_init    = bt87x_class_init,
        .interfaces    = interfaces,
    };
    type_register_static(&bt87x_info);
}
type_init(bt87x_register_types);
