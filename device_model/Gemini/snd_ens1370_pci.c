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

#define TYPE_PCIBASE_DEVICE "snd_ens1370_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define ES_REG_CONTROL 0x00
#define ES_REG_STATUS 0x04
#define ES_REG_UART_DATA 0x08
#define ES_REG_UART_STATUS 0x09
#define ES_REG_UART_CONTROL 0x09
#define ES_REG_UART_RES 0x0a
#define ES_REG_MEM_PAGE 0x0c
#define ES_REG_1370_CODEC 0x10
#define ES_REG_1371_CODEC 0x14
#define ES_REG_1371_SMPRATE 0x10
#define ES_REG_1371_LEGACY 0x18
#define ES_REG_CHANNEL_STATUS 0x1c
#define ES_REG_SERIAL 0x20
#define ES_REG_DAC1_COUNT 0x24
#define ES_REG_DAC2_COUNT 0x28
#define ES_REG_ADC_COUNT 0x2c
#define ES_REG_DAC1_FRAME 0x30
#define ES_REG_DAC1_SIZE 0x34
#define ES_REG_DAC2_FRAME 0x38
#define ES_REG_DAC2_SIZE 0x3c
#define ES_REG_ADC_FRAME 0x30
#define ES_REG_ADC_SIZE 0x34
#define ES_REG_PHANTOM_FRAME 0x38
#define ES_REG_PHANTOM_COUNT 0x3c
#define ES_REG_UART_FIFO 0x30

/* Hardware Bitmasks and Macros */
#define ES_1370_SRCLOCK	   1411200
#define ES_1371_CODEC_PIRD	   (1<<23)
#define ES_DAC1_EN		(1<<6)
#define ES_DAC2_EN		(1<<5)
#define ES_ADC_EN		(1<<4)
#define ES_UART_EN		(1<<3)
#define ES_JYSTK_EN		(1<<2)
#define ES_1370_PCLKDIVO(o)	(((o)&0x1fff)<<16)
#define ES_1370_CDC_EN	(1<<1)
#define ES_1370_SERR_DISABLE	(1<<0)
#define ES_1371_SYNC_RES	(1<<14)
#define ES_1373_BYPASS_P1	(1<<31)
#define ES_1373_SPDIF_THRU	(1<<26)
#define ES_1371_GPIO_OUT(o)	(((o)&0x0f)<<16)
#define ES_INTR               (1<<31)
#define ES_DAC1		(1<<2)
#define ES_DAC2		(1<<1)
#define ES_ADC		(1<<0)
#define ES_UART		(1<<3)
#define ES_1370_CSTAT		(1<<10)
#define ES_RXRDY		(1<<0)
#define ES_TXRDY		(1<<1)
#define ES_RXINTEN		(1<<7)
#define ES_TXINTENM		(0x03<<5)
#define ES_MEM_PAGEO(o)	(((o)&0x0f)<<0)
#define ES_PAGE_DAC	0x0c
#define ES_PAGE_ADC	0x0d
#define ES_1371_CODEC_WIP	   (1<<30)
#define ES_1371_CODEC_RDY	   (1<<31)
#define ES_1371_SRC_RAM_BUSY     (1<<23)
#define ES_P1_INT_EN		(1<<8)
#define ES_P2_INT_EN		(1<<9)
#define ES_R1_INT_EN		(1<<10)
#define ES_REG_FCURR_COUNTI(i) (((i)>>14)&0x3fffc)
#define ES_1370_CODEC_WRITE(a,d) ((((a)&0xff)<<8)|(((d)&0xff)<<0))
#define ES_1371_CODEC_WRITE(a,d) ((((a)&0x7f)<<16)|(((d)&0xffff)<<0))
#define ES_1371_CODEC_READS(a)   ((((a)&0x7f)<<16)|ES_1371_CODEC_PIRD)
#define ES_1371_CODEC_READ(i)    (((i)>>0)&0xffff)
#define ES_REG(ensoniq, x) ((ensoniq)->port + ES_REG_##x)
#define ES_1371_SRC_RAM_ADDRO(o) (((o)&0x7f)<<25)
#define ES_1371_SRC_RAM_DATAO(o) (((o)&0xffff)<<0)
#define ES_1371_SRC_RAM_WE	   (1<<24)
#define ES_1371_SRC_DISABLE      (1<<22)
#define ES_1371_DIS_P1	   (1<<21)
#define ES_1371_DIS_P2	   (1<<20)
#define ES_1371_DIS_R1	   (1<<19)
#define ES_P1_PAUSE		(1<<11)
#define ES_P2_PAUSE		(1<<12)
#define ES_P1_LOOP_SEL	(1<<13)
#define ES_P1_SCT_RLD		(1<<7)
#define ES_P1_MODEM		(0x03<<0)
#define ES_P1_MODEO(o)	(((o)&0x03)<<0)
#define ES_P2_LOOP_SEL	(1<<14)
#define ES_P2_DAC_SEN		(1<<6)
#define ES_P2_END_INCM		(0x07<<19)
#define ES_P2_ST_INCM		(0x07<<16)
#define ES_P2_MODEM		(0x03<<2)
#define ES_P2_END_INCO(o)	(((o)&0x07)<<19)
#define ES_P2_ST_INCO(o)	(((o)&0x07)<<16)
#define ES_R1_LOOP_SEL	(1<<15)
#define ES_R1_MODEM		(0x03<<4)
#define ES_R1_MODEO(o)	(((o)&0x03)<<4)
#define ES_1370_WTSRSELM	(0x03<<12)
#define ES_1370_WTSRSEL(o)	(((o)&0x03)<<12)
#define ES_1370_PCLKDIVM	((0x1fff)<<16)
#define ES_1370_SRTODIV(x) (ES_1370_SRCLOCK/(x)-2)
#define ES_MODE_PLAY1	0x0001
#define ES_MODE_PLAY2	0x0002
#define ES_MODE_CAPTURE	0x0004
#define ES_MODE_INPUT	0x0002
#define ES_MODE_OUTPUT	0x0001
#define ES_TXINTENO(o)	(((o)&0x03)<<5)
#define ES_TXINTENI(i)	(((i)>>5)&0x03)
#define ES_CNTRL(o)		(((o)&0x03)<<0)
#define ES_1371_JOY_ASELM	(0x03<<24)
#define ES_1371_JOY_ASEL(o)	(((o)&0x03)<<24)
#define ES_1371_JOY_ASELI(i)  (((i)>>24)&0x03)
#define ES_1370_XCTL1 	(1<<30)
#define ES_1370_XCTL0		(1<<8)
#define ES_1371_ST_AC97_RST	(1<<29)
#define ES_SMPREG_DAC1		0x70
#define ES_SMPREG_DAC2		0x74
#define ES_SMPREG_ADC		0x78
#define ES_SMPREG_TRUNC_N	0x00
#define ES_SMPREG_INT_REGS	0x01
#define ES_SMPREG_VFREQ_FRAC	0x03
#define ES_SMPREG_VOL_ADC	0x6c
#define ES_SMPREG_VOL_DAC1	0x7c
#define ES_SMPREG_VOL_DAC2	0x7e
#define EV_1938_CODEC_MAGIC	   (1<<26)
#define ES_1373_SPDIF_EN	(1<<18)
#define ES_1373_REAR_BIT27	(1<<27)
#define ES_1373_REAR_BIT26	(1<<26)
#define ES_1373_REAR_BIT24	(1<<24)

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t es_reg_control;
    uint32_t es_reg_status;
    uint32_t es_reg_uart_data;
    uint32_t es_reg_uart_status;
    uint32_t es_reg_uart_control;
    uint32_t es_reg_uart_res;
    uint32_t es_reg_mem_page;
    uint32_t es_reg_1370_codec;
    uint32_t es_reg_1371_codec;
    uint32_t es_reg_1371_smprate;
    uint32_t es_reg_1371_legacy;
    uint32_t es_reg_channel_status;
    uint32_t es_reg_serial;
    uint32_t es_reg_dac1_count;
    uint32_t es_reg_dac2_count;
    uint32_t es_reg_adc_count;
    uint32_t es_reg_dac1_frame;
    uint32_t es_reg_dac1_size;
    uint32_t es_reg_dac2_frame;
    uint32_t es_reg_dac2_size;
    uint32_t es_reg_adc_frame;
    uint32_t es_reg_adc_size;
    uint32_t es_reg_phantom_frame;
    uint32_t es_reg_phantom_count;
    uint32_t es_reg_uart_fifo;

    /* DMA Context */
    uint32_t p1_dma_size;
    uint32_t p2_dma_size;
    uint32_t c_dma_size;

    uint32_t mode;
    uint32_t uartm;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->es_reg_status) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return pcibase_pio_read(opaque, addr, size);
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_pio_write(opaque, addr, val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case ES_REG_CONTROL:
        val = s->es_reg_control;
        break;
    case ES_REG_STATUS:
        val = s->es_reg_status;
        break;
    case ES_REG_UART_DATA:
        val = s->es_reg_uart_data;
        break;
    case ES_REG_UART_STATUS:
        val = s->es_reg_uart_status;
        break;
    case ES_REG_MEM_PAGE:
        val = s->es_reg_mem_page;
        break;
    case ES_REG_1370_CODEC:
        val = s->es_reg_1370_codec | s->es_reg_1371_smprate;
        break;
    case ES_REG_1371_CODEC:
        val = s->es_reg_1371_codec;
        break;
    case ES_REG_1371_LEGACY:
        val = s->es_reg_1371_legacy;
        break;
    case ES_REG_CHANNEL_STATUS:
        val = s->es_reg_channel_status;
        break;
    case ES_REG_SERIAL:
        val = s->es_reg_serial;
        break;
    case ES_REG_DAC1_COUNT:
        val = s->es_reg_dac1_count;
        break;
    case ES_REG_DAC2_COUNT:
        val = s->es_reg_dac2_count;
        break;
    case ES_REG_ADC_COUNT:
        val = s->es_reg_adc_count;
        break;
    case ES_REG_DAC1_FRAME: /* 0x30 */
        val = s->es_reg_dac1_frame;
        break;
    case ES_REG_DAC1_SIZE: /* 0x34 */
        val = s->es_reg_dac1_size;
        break;
    case ES_REG_DAC2_FRAME: /* 0x38 */
        val = s->es_reg_dac2_frame;
        break;
    case ES_REG_DAC2_SIZE: /* 0x3c */
        val = s->es_reg_dac2_size;
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
    case ES_REG_CONTROL:
        s->es_reg_control = val;
        break;
    case ES_REG_STATUS:
        s->es_reg_status = val;
        break;
    case ES_REG_UART_DATA:
        s->es_reg_uart_data = val;
        break;
    case ES_REG_UART_CONTROL:
        s->es_reg_uart_control = val;
        break;
    case ES_REG_UART_RES:
        s->es_reg_uart_res = val;
        break;
    case ES_REG_MEM_PAGE:
        s->es_reg_mem_page = val;
        break;
    case ES_REG_1370_CODEC:
        s->es_reg_1370_codec = val;
        s->es_reg_1371_smprate = val;
        break;
    case ES_REG_1371_CODEC:
        s->es_reg_1371_codec = val;
        break;
    case ES_REG_1371_LEGACY:
        s->es_reg_1371_legacy = val;
        break;
    case ES_REG_CHANNEL_STATUS:
        s->es_reg_channel_status = val;
        break;
    case ES_REG_SERIAL:
        s->es_reg_serial = val;
        break;
    case ES_REG_DAC1_COUNT:
        s->es_reg_dac1_count = val;
        break;
    case ES_REG_DAC2_COUNT:
        s->es_reg_dac2_count = val;
        break;
    case ES_REG_ADC_COUNT:
        s->es_reg_adc_count = val;
        break;
    case ES_REG_DAC1_FRAME: /* 0x30 */
        s->es_reg_dac1_frame = val;
        s->es_reg_adc_frame = val;
        break;
    case ES_REG_DAC1_SIZE: /* 0x34 */
        s->es_reg_dac1_size = val;
        s->es_reg_adc_size = val;
        break;
    case ES_REG_DAC2_FRAME: /* 0x38 */
        s->es_reg_dac2_frame = val;
        s->es_reg_phantom_frame = val;
        break;
    case ES_REG_DAC2_SIZE: /* 0x3c */
        s->es_reg_dac2_size = val;
        s->es_reg_phantom_count = val;
        break;
    default:
        break;
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

    s->es_reg_control = 0;
    s->es_reg_status = 0;
    s->es_reg_uart_data = 0;
    s->es_reg_uart_status = 0;
    s->es_reg_uart_control = 0;
    s->es_reg_uart_res = 0;
    s->es_reg_mem_page = 0;
    s->es_reg_1370_codec = 0;
    s->es_reg_1371_codec = 0;
    s->es_reg_1371_smprate = 0;
    s->es_reg_1371_legacy = 0;
    s->es_reg_channel_status = 0;
    s->es_reg_serial = 0;
    s->es_reg_dac1_count = 0;
    s->es_reg_dac2_count = 0;
    s->es_reg_adc_count = 0;
    s->es_reg_dac1_frame = 0;
    s->es_reg_dac1_size = 0;
    s->es_reg_dac2_frame = 0;
    s->es_reg_dac2_size = 0;
    s->es_reg_adc_frame = 0;
    s->es_reg_adc_size = 0;
    s->es_reg_phantom_frame = 0;
    s->es_reg_phantom_count = 0;
    s->es_reg_uart_fifo = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ENSONIQ );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x5000 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_AUDIO );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

}

/* Minimal VMState to satisfy QEMU migration subsystems */
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
