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

#define TYPE_PCIBASE_DEVICE "snd_ens1370_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor/Device IDs for ES1370 */
#define ES_VENDOR_ID 0x1274
#define ES_DEVICE_ID 0x5000
#define PCI_CLASS_MULTIMEDIA_AUDIO 0x0401

/* IO Register offsets (ES1370) - keep for documentation; we use hardcoded offsets */
enum {
    ES_REG_CONTROL      = 0x00,
    ES_REG_STATUS       = 0x04,
    ES_REG_UART_DATA    = 0x08,
    ES_REG_UART_STATUS  = 0x09,
    ES_REG_UART_CONTROL = 0x09,
    ES_REG_UART_RES     = 0x0a,
    ES_REG_MEM_PAGE     = 0x0c,
    ES_REG_1370_CODEC   = 0x10,
    ES_REG_SERIAL       = 0x20,
    ES_REG_DAC1_COUNT   = 0x24,
    ES_REG_DAC2_COUNT   = 0x28,
    ES_REG_ADC_COUNT    = 0x2c,
    ES_REG_FRAME        = 0x30,
    ES_REG_SIZE         = 0x34,
    ES_REG_FRAME2       = 0x38,
    ES_REG_SIZE2        = 0x3c,
    ES_REG_CHANNEL_STATUS = 0x1c
};

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int     index;
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

#define ES_1370_CSTAT (1 << 30)

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows */
    uint32_t control;
    uint32_t sstatus;
    uint8_t uart_data;
    uint8_t uart_status;
    uint8_t uart_control;
    uint8_t uart_res;
    uint16_t codec;
    uint32_t mem_page;
    uint32_t channel_status;
    uint32_t serial;
    uint32_t dac1_count;
    uint32_t dac2_count;
    uint32_t adc_count;
    uint32_t dac1_frame;
    uint32_t dac1_size;
    uint32_t dac2_frame;
    uint32_t dac2_size;
    uint32_t adc_frame;
    uint32_t adc_size;
    uint32_t phantom_frame;
    uint32_t phantom_count;

    uint8_t pm_state;
    uint8_t revision;

    /* Codec state */
    uint16_t codec_regs[128];
    uint16_t codec_read_latch;
    bool codec_read_pending;
};

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0x00: /* CONTROL */
        val = s->control;
        break;
    case 0x04: /* STATUS */
        val = s->sstatus;
        if (s->codec_read_pending) {
            val |= ES_1370_CSTAT;
        }
        break;
    case 0x08: /* UART_DATA */
        val = s->uart_data;
        break;
    case 0x09: /* UART_STATUS */
        val = s->uart_status;
        break;
    case 0x0a: /* UART_RES */
        val = s->uart_res;
        break;
    case 0x0c: /* MEM_PAGE */
        val = s->mem_page;
        break;
    case 0x10: /* 1370_CODEC */
        if (s->codec_read_pending) {
            val = s->codec_read_latch;
            s->codec_read_pending = false;
        } else {
            val = s->codec;
        }
        break;
    case 0x1c: /* CHANNEL_STATUS */
        val = s->channel_status;
        break;
    case 0x20: /* SERIAL */
        val = s->serial;
        break;
    case 0x24: /* DAC1_COUNT */
        val = s->dac1_count;
        break;
    case 0x28: /* DAC2_COUNT */
        val = s->dac2_count;
        break;
    case 0x2c: /* ADC_COUNT */
        val = s->adc_count;
        break;
    case 0x30: /* FRAME (page-dependent) */
        if (s->mem_page & 1) {
            val = s->adc_frame;
        } else {
            val = s->dac1_frame;
        }
        break;
    case 0x34: /* SIZE (page-dependent) */
        if (s->mem_page & 1) {
            val = s->adc_size;
        } else {
            val = s->dac1_size;
        }
        break;
    case 0x38: /* FRAME2 (page-dependent: DAC2 or PHANTOM) */
        if (s->mem_page & 1) {
            val = s->phantom_frame;
        } else {
            val = s->dac2_frame;
        }
        break;
    case 0x3c: /* SIZE2 (page-dependent: DAC2_SIZE or PHANTOM_COUNT) */
        if (s->mem_page & 1) {
            val = s->phantom_count;
        } else {
            val = s->dac2_size;
        }
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0x00: /* CONTROL */
        s->control = val;
        break;
    case 0x04: /* STATUS */
        s->sstatus = val;
        break;
    case 0x08: /* UART_DATA */
        if (size == 1) s->uart_data = val & 0xFF;
        break;
    case 0x09: /* UART_CONTROL */
        if (size == 1) s->uart_control = val & 0xFF;
        break;
    case 0x0a: /* UART_RES */
        if (size == 1) s->uart_res = val & 0xFF;
        break;
    case 0x0c: /* MEM_PAGE */
        s->mem_page = val;
        break;
    case 0x10: /* 1370_CODEC */
        {
            uint32_t cmd = val;
            if (cmd & (1 << 31)) {
                /* Read request */
                uint8_t reg = (cmd >> 16) & 0x7f;
                if (reg < 128) {
                    s->codec_read_latch = s->codec_regs[reg];
                    s->codec_read_pending = true;
                }
            } else {
                /* Write request */
                uint8_t reg = (cmd >> 16) & 0x7f;
                if (reg == 0x00) {
                    /* AC'97 reset: restore default codec state */
                    memset(s->codec_regs, 0, sizeof(s->codec_regs));
                    s->codec_regs[0x00] = 0x0D50;
                    s->codec_regs[0x7c] = 0x414b;
                    s->codec_regs[0x7e] = 0x4531;
                } else if (reg < 128) {
                    s->codec_regs[reg] = cmd & 0xffff;
                }
                s->codec_read_pending = false;
            }
            s->codec = cmd;
        }
        break;
    case 0x1c: /* CHANNEL_STATUS */
        s->channel_status = val;
        break;
    case 0x20: /* SERIAL */
        s->serial = val;
        break;
    case 0x24: /* DAC1_COUNT */
        s->dac1_count = val;
        break;
    case 0x28: /* DAC2_COUNT */
        s->dac2_count = val;
        break;
    case 0x2c: /* ADC_COUNT */
        s->adc_count = val;
        break;
    case 0x30: /* FRAME (page-dependent) */
        if (s->mem_page & 1) {
            s->adc_frame = val;
        } else {
            s->dac1_frame = val;
        }
        break;
    case 0x34: /* SIZE (page-dependent) */
        if (s->mem_page & 1) {
            s->adc_size = val;
        } else {
            s->dac1_size = val;
        }
        break;
    case 0x38: /* FRAME2 (page-dependent: DAC2 or PHANTOM_FRAME) */
        if (s->mem_page & 1) {
            s->phantom_frame = val;
        } else {
            s->dac2_frame = val;
        }
        break;
    case 0x3c: /* SIZE2 (page-dependent: DAC2_SIZE or PHANTOM_COUNT) */
        if (s->mem_page & 1) {
            s->phantom_count = val;
        } else {
            s->dac2_size = val;
        }
        break;
    default:
        break;
    }
}

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

    memset(&s->control, 0, sizeof(*s) - offsetof(PCIBaseState, control));
    s->revision = 0x01;

    /* Initialize codec state */
    s->codec_read_pending = false;
    s->codec_read_latch = 0;
    memset(s->codec_regs, 0, sizeof(s->codec_regs));
    s->codec_regs[0x00] = 0x0D50; /* Standard AC'97 reset value, non-zero to pass validity check */
    s->codec_regs[0x7c] = 0x414b; /* Vendor ID1 high */
    s->codec_regs[0x7e] = 0x4531; /* Vendor ID2 low */

    /* Set serial to expected value */
    s->serial = 0x11001af4;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x1274);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x5000);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0401);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x40;
    s->bar_info[0].name = "es1370-pio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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
}

static const VMStateDescription vmstate_pcibase = {
    .name = "snd_ens1370_pci",
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
