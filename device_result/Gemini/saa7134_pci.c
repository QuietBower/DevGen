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

#define TYPE_PCIBASE_DEVICE "saa7134_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_DEVICE_ID_PHILIPS_SAA7133 0x7133
#define PCI_DEVICE_ID_PHILIPS_SAA7135 0x7135
#define PCI_DEVICE_ID_PHILIPS_SAA7134 0x7134

#define SAA7134_GPIO_GPRESCAN                   0x80
#define SAA7134_GPIO_GPSTATUS0                  0x1B4
#define SAA7134_GPIO_GPMODE3                    0x1B3
#define SAA7134_VIDEO_PORT_CTRL6                0x196
#define SAA7134_REGION_ENABLE                   0x004
#define SAA7134_IRQ1                            (0x2c4 >> 2)
#define SAA7134_MAIN_CTRL                       (0x2a8 >> 2)
#define SAA7134_IRQ2                            (0x2c8 >> 2)
#define SAA7134_IRQ_REPORT                      (0x2cc >> 2)
#define SAA7134_IRQ_STATUS                      (0x2d0 >> 2)
#define SAA7134_SOURCE_TIMING2                  0x001
#define SAA7134_FIFO_SIZE                       (0x2a0 >> 2)
#define SAA7134_SPECIAL_MODE                    0x1d0
#define SAA7134_THRESHOULD                      (0x2a4 >> 2)
#define SAA7134_STATUS_VIDEO1                   0x11e
#define SAA7134_SYNC_CTRL                       0x108
#define SAA7134_STATUS_VIDEO2                   0x11f
#define SAA7134_ANALOG_IO_SELECT                0x16e
#define SAA7134_GPIO_GPMODE1                    0x1B1
#define SAA7134_GPIO_GPSTATUS3                  0x1B7
#define SAA7134_GPIO_GPSTATUS2                  0x1B6
#define SAA7134_PRODUCTION_TEST_MODE            0x1d1
#define SAA7134_GPIO_GPSTATUS1                  0x1B5
#define SAA7134_VIDEO_PORT_CTRL1                0x191
#define SAA7134_VIDEO_PORT_CTRL0                0x190
#define SAA7134_VIDEO_PORT_CTRL5                0x195
#define SAA7134_VIDEO_PORT_CTRL7                0x197
#define SAA7134_VIDEO_PORT_CTRL8                0x198
#define SAA7134_VIDEO_PORT_CTRL3                0x193
#define SAA7134_VIDEO_PORT_CTRL4                0x194
#define SAA7134_VIDEO_PORT_CTRL2                0x192
#define SAA7134_I2S_OUTPUT_LEVEL                0x16a
#define SAA7134_I2S_OUTPUT_SELECT               0x169
#define SAA7134_I2S_OUTPUT_FORMAT               0x168
#define SAA7134_I2S_AUDIO_OUTPUT                0x1c0
#define SAA7133_I2S_AUDIO_CONTROL               0x591
#define SAA7134_AUDIO_CLOCK2                    0x172
#define SAA7134_AUDIO_PLL_CTRL                  0x173
#define SAA7134_NICAM_ERROR_LOW                 0x148
#define SAA7134_AUDIO_CLOCK1                    0x171
#define SAA7134_NICAM_ERROR_HIGH                0x149
#define SAA7134_AUDIO_CLOCK0                    0x170
#define SAA7134_CHANNEL2_LEVEL                  0x164
#define SAA7134_CHANNEL1_LEVEL                  0x163
#define SAA7134_NICAM_LEVEL_ADJUST              0x166
#define SAA7134_TS_SERIAL1                      0x1a3
#define SAA7134_TS_PARALLEL_SERIAL              0x1a1
#define SAA7134_TS_DMA0                         0x1a4
#define SAA7134_TS_PARALLEL                     0x1a0
#define SAA7134_TS_DMA2                         0x1a6
#define SAA7134_TS_DMA1                         0x1a5
#define SAA7135_DSP_RWSTATE                     0x580
#define SAA7134_RS_BA2(n)                       ((0x204 >> 2) + 4*n)
#define SAA7134_RS_BA1(n)                       ((0x200 >> 2) + 4*n)
#define SAA7134_AUDIO_CLOCKS_PER_FIELD0         0x174
#define SAA7133_ANALOG_IO_SELECT                (0x594 >> 2)
#define SAA7135_DSP_RWCLEAR                     0x586
#define SAA7134_TS_SERIAL0                      0x1a2
#define SAA7134_RS_PITCH(n)                     ((0x208 >> 2) + 4*n)
#define SAA7134_RS_CONTROL(n)                   ((0x20c >> 2) + 4*n)
#define SAA7134_I2C_ATTR_STATUS                 0x180
#define SAA7134_I2C_DATA                        0x181

#define SAA7134_GPIO_GPMODE0                    0x1B0
#define TS_PACKET_SIZE 188
#define VP_T_CODE_P_INVERTED		0x01
#define VP_CLK_CTRL2_DELAYED		0x04
#define VP_CLK_CTRL1_INVERTED		0x02
#define VP_VS_TYPE_MASK			0x07
#define VP_VS_TYPE_OFF			0x00

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
    uint8_t mmio_regs[0x1000];

    /* DMA Context */
    dma_addr_t dma_base;
    uint32_t dma_size;

    uint32_t status;
    uint32_t reset_state;
};

enum saa7134_input_types {
    SAA7134_NO_INPUT = 0,
    SAA7134_INPUT_MUTE,
    SAA7134_INPUT_RADIO,
    SAA7134_INPUT_TV,
    SAA7134_INPUT_TV_MONO,
    SAA7134_INPUT_COMPOSITE,
    SAA7134_INPUT_COMPOSITE0,
    SAA7134_INPUT_COMPOSITE1,
    SAA7134_INPUT_COMPOSITE2,
    SAA7134_INPUT_COMPOSITE3,
    SAA7134_INPUT_COMPOSITE4,
    SAA7134_INPUT_SVIDEO,
    SAA7134_INPUT_SVIDEO0,
    SAA7134_INPUT_SVIDEO1,
    SAA7134_INPUT_COMPOSITE_OVER_SVIDEO,
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t report = ldl_le_p(&s->mmio_regs[0x2cc]);
    
    if (report) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    
    if (addr < sizeof(s->mmio_regs)) {
        switch (size) {
            case 1: val = s->mmio_regs[addr]; break;
            case 2: val = lduw_le_p(&s->mmio_regs[addr]); break;
            case 4: val = ldl_le_p(&s->mmio_regs[addr]); break;
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    if (addr < sizeof(s->mmio_regs)) {
        /* SAA7134_IRQ_REPORT (0x2cc) is Write-1-to-Clear */
        if (addr == 0x2cc && size == 4) {
            uint32_t current = ldl_le_p(&s->mmio_regs[addr]);
            current &= ~val;
            stl_le_p(&s->mmio_regs[addr], current);
            pcibase_update_irq(s);
            return;
        }
        
        switch (size) {
            case 1: s->mmio_regs[addr] = val; break;
            case 2: stw_le_p(&s->mmio_regs[addr], val); break;
            case 4: stl_le_p(&s->mmio_regs[addr], val); break;
        }
        
        /* Update IRQ if interrupt enable registers are modified */
        if (addr == 0x2c4 || addr == 0x2c8) {
            pcibase_update_irq(s);
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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
    
    memset(s->mmio_regs, 0, sizeof(s->mmio_regs));
    
    /* Default values from driver hw_enable1 */
    stl_le_p(&s->mmio_regs[0x2a0], 0x08070503); /* SAA7134_FIFO_SIZE */
    stl_le_p(&s->mmio_regs[0x2a4], 0x02020202); /* SAA7134_THRESHOULD */
    s->mmio_regs[0x1d0] = 0x01;                 /* SAA7134_SPECIAL_MODE */
    s->mmio_regs[0x001] = 0x20;                 /* SAA7134_SOURCE_TIMING2 */
    
    /* Default values for video status to simulate valid signal */
    s->mmio_regs[0x11e] = 0x02; /* SAA7134_STATUS_VIDEO1: locked, PAL */
    s->mmio_regs[0x11f] = 0x01; /* SAA7134_STATUS_VIDEO2: sync yes, interlaced */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1131 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_PHILIPS_SAA7134 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0400 );
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
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "saa7134-mmio";

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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "saa7134_pci",
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
