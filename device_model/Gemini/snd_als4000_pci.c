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

#define TYPE_PCIBASE_DEVICE "snd_als4000_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define ALS4000_VENDOR_ID 0x4005
#define ALS4000_DEVICE_ID 0x4000
#define ALS4000_CLASS_ID  PCI_CLASS_MULTIMEDIA_AUDIO

#define SB_DSP4_IRQSTATUS	0x82
#define SB_IRQTYPE_8BIT		0x01
#define SB_IRQTYPE_16BIT	0x02
#define SB_IRQTYPE_MPUIN	0x04
#define ALS4K_IRQTYPE_CR1E_DMA	0x20

enum als4k_iobase_t {
    ALS4K_IOD_00_AC97_ACCESS = 0x00,
    ALS4K_IOW_04_AC97_READ = 0x04,
    ALS4K_IOB_06_AC97_STATUS = 0x06,
    ALS4K_IOB_07_IRQSTATUS = 0x07,
    ALS4K_IOD_08_GCR_DATA = 0x08,
    ALS4K_IOB_0C_GCR_INDEX = 0x0c,
    ALS4K_IOB_0E_IRQTYPE_SB_CR1E_MPU = 0x0e,
    ALS4K_IOB_10_ADLIB_ADDR0 = 0x10,
    ALS4K_IOB_11_ADLIB_ADDR1 = 0x11,
    ALS4K_IOB_12_ADLIB_ADDR2 = 0x12,
    ALS4K_IOB_13_ADLIB_ADDR3 = 0x13,
    ALS4K_IOB_14_MIXER_INDEX = 0x14,
    ALS4K_IOB_15_MIXER_DATA = 0x15,
    ALS4K_IOB_16_ESP_RESET = 0x16,
    ALS4K_IOB_18_OPL_ADDR0 = 0x18,
    ALS4K_IOB_19_OPL_ADDR1 = 0x19,
    ALS4K_IOB_1A_ESP_RD_DATA = 0x1a,
    ALS4K_IOB_1C_ESP_CMD_DATA = 0x1c,
    ALS4K_IOB_1E_ESP_RD_STATUS8 = 0x1e,
    ALS4K_IOB_1F_ESP_RD_STATUS16 = 0x1f,
    ALS4K_IOB_20_ESP_GAMEPORT_200 = 0x20,
    ALS4K_IOB_21_ESP_GAMEPORT_201 = 0x21,
    ALS4K_IOB_30_MIDI_DATA = 0x30,
    ALS4K_IOB_31_MIDI_STATUS = 0x31,
};

enum als4k_iobase_0e_t {
    ALS4K_IOB_0E_MPU_IRQ = 0x10,
    ALS4K_IOB_0E_CR1E_IRQ = 0x40,
    ALS4K_IOB_0E_SB_DMA_IRQ = 0x80,
};

enum als4k_gcr_t {
    ALS4K_GCR8C_MISC_CTRL = 0x8c,
    ALS4K_GCR90_TEST_MODE_REG = 0x90,
    ALS4K_GCR91_DMA0_ADDR = 0x91,
    ALS4K_GCR92_DMA0_MODE_COUNT = 0x92,
    ALS4K_GCR93_DMA1_ADDR = 0x93,
    ALS4K_GCR94_DMA1_MODE_COUNT = 0x94,
    ALS4K_GCR95_DMA3_ADDR = 0x95,
    ALS4K_GCR96_DMA3_MODE_COUNT = 0x96,
    ALS4K_GCR99_DMA_EMULATION_CTRL = 0x99,
    ALS4K_GCRA0_FIFO1_CURRENT_ADDR = 0xa0,
    ALS4K_GCRA1_FIFO1_STATUS_BYTECOUNT = 0xa1,
    ALS4K_GCRA2_FIFO2_PCIADDR = 0xa2,
    ALS4K_GCRA3_FIFO2_COUNT = 0xa3,
    ALS4K_GCRA4_FIFO2_CURRENT_ADDR = 0xa4,
    ALS4K_GCRA5_FIFO1_STATUS_BYTECOUNT = 0xa5,
    ALS4K_GCRA6_PM_CTRL = 0xa6,
    ALS4K_GCRA7_PCI_ACCESS_STORAGE = 0xa7,
    ALS4K_GCRA8_LEGACY_CFG1 = 0xa8,
    ALS4K_GCRA9_LEGACY_CFG2 = 0xa9,
    ALS4K_GCRFF_DUMMY_SCRATCH = 0xff,
};

enum als4k_gcr8c_t {
    ALS4K_GCR8C_IRQ_MASK_CTRL_ENABLE = 0x8000,
    ALS4K_GCR8C_CHIP_REV_MASK = 0xf0000
};

enum als4k_cr_t {
    ALS4K_CR0_SB_CONFIG = 0x00,
    ALS4K_CR2_MISC_CONTROL = 0x02,
    ALS4K_CR3_CONFIGURATION = 0x03,
    ALS4K_CR17_FIFO_STATUS = 0x17,
    ALS4K_CR18_ESP_MAJOR_VERSION = 0x18,
    ALS4K_CR19_ESP_MINOR_VERSION = 0x19,
    ALS4K_CR1A_MPU401_UART_MODE_CONTROL = 0x1a,
    ALS4K_CR1C_FIFO2_BLOCK_LENGTH_LO = 0x1c,
    ALS4K_CR1D_FIFO2_BLOCK_LENGTH_HI = 0x1d,
    ALS4K_CR1E_FIFO2_CONTROL = 0x1e,
    ALS4K_CR3A_MISC_CONTROL = 0x3a,
    ALS4K_CR3B_CRC32_BYTE0 = 0x3b,
    ALS4K_CR3C_CRC32_BYTE1 = 0x3c,
    ALS4K_CR3D_CRC32_BYTE2 = 0x3d,
    ALS4K_CR3E_CRC32_BYTE3 = 0x3e,
};

enum als4k_cr0_t {
    ALS4K_CR0_DMA_CONTIN_MODE_CTRL = 0x02,
    ALS4K_CR0_DMA_90H_MODE_CTRL = 0x04,
    ALS4K_CR0_MX80_81_REG_WRITE_ENABLE = 0x80,
};

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
    uint32_t irq_status;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t gcr_index;
    uint32_t gcr_regs[256];
    uint8_t mixer_index;
    uint8_t cr_regs[256];

    /* DMA Context */
    uint32_t dma0_addr;
    uint32_t dma0_mode_count;
    uint32_t dma1_addr;
    uint32_t dma1_mode_count;
    uint32_t dma3_addr;
    uint32_t dma3_mode_count;

    /* ESP (SB DSP) State */
    uint8_t esp_resp_buf[16];
    uint8_t esp_resp_len;
    uint8_t esp_resp_idx;
    uint8_t esp_rd_status8;

    /* MPU401 State */
    uint8_t mpu_data;
    uint8_t mpu_status;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    pci_set_irq(pdev, s->irq_status != 0);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
        case ALS4K_IOB_0C_GCR_INDEX:
            val = s->gcr_index;
            break;
        case ALS4K_IOD_08_GCR_DATA:
            val = s->gcr_regs[s->gcr_index];
            break;
        case ALS4K_IOB_0E_IRQTYPE_SB_CR1E_MPU:
            val = s->irq_status;
            break;
        case ALS4K_IOB_14_MIXER_INDEX:
            val = s->mixer_index;
            break;
        case ALS4K_IOB_15_MIXER_DATA:
            val = s->cr_regs[s->mixer_index];
            break;
        case ALS4K_IOB_16_ESP_RESET:
            s->irq_status &= ~ALS4K_IOB_0E_CR1E_IRQ;
            pcibase_update_irq(s);
            break;
        case ALS4K_IOB_1A_ESP_RD_DATA:
            if (s->esp_resp_idx < s->esp_resp_len) {
                val = s->esp_resp_buf[s->esp_resp_idx++];
                if (s->esp_resp_idx >= s->esp_resp_len) {
                    s->esp_rd_status8 &= ~0x80;
                }
            } else {
                val = 0xff;
            }
            break;
        case ALS4K_IOB_1C_ESP_CMD_DATA:
            val = 0x00; /* Bit 7 = 0 means ready for command */
            break;
        case ALS4K_IOB_1E_ESP_RD_STATUS8:
            val = s->esp_rd_status8;
            s->irq_status &= ~ALS4K_IOB_0E_SB_DMA_IRQ;
            pcibase_update_irq(s);
            break;
        case ALS4K_IOB_1F_ESP_RD_STATUS16:
            s->irq_status &= ~ALS4K_IOB_0E_SB_DMA_IRQ;
            pcibase_update_irq(s);
            break;
        case ALS4K_IOB_30_MIDI_DATA:
            val = s->mpu_data;
            s->mpu_status |= 0x80; /* RX empty */
            break;
        case ALS4K_IOB_31_MIDI_STATUS:
            val = s->mpu_status;
            break;
        default:
            val = 0;
            break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    switch (addr) {
        case ALS4K_IOB_0C_GCR_INDEX:
            s->gcr_index = val & 0xff;
            break;
        case ALS4K_IOD_08_GCR_DATA:
            s->gcr_regs[s->gcr_index] = val;
            /* Mirror programmed DMA addresses to current address registers for pointer callbacks */
            if (s->gcr_index == ALS4K_GCR91_DMA0_ADDR) {
                s->gcr_regs[ALS4K_GCRA0_FIFO1_CURRENT_ADDR] = val;
            } else if (s->gcr_index == ALS4K_GCRA2_FIFO2_PCIADDR) {
                s->gcr_regs[ALS4K_GCRA4_FIFO2_CURRENT_ADDR] = val;
            }
            break;
        case ALS4K_IOB_0E_IRQTYPE_SB_CR1E_MPU:
            s->irq_status &= ~val; /* W1C */
            pcibase_update_irq(s);
            break;
        case ALS4K_IOB_14_MIXER_INDEX:
            s->mixer_index = val & 0xff;
            break;
        case ALS4K_IOB_15_MIXER_DATA:
            s->cr_regs[s->mixer_index] = val & 0xff;
            break;
        case ALS4K_IOB_16_ESP_RESET:
            if (val == 1) {
                s->esp_resp_buf[0] = 0xaa;
                s->esp_resp_len = 1;
                s->esp_resp_idx = 0;
                s->esp_rd_status8 |= 0x80;
            }
            break;
        case ALS4K_IOB_1C_ESP_CMD_DATA:
            if (val == 0xe1) {
                s->esp_resp_buf[0] = 0x04;
                s->esp_resp_buf[1] = 0x10;
                s->esp_resp_len = 2;
                s->esp_resp_idx = 0;
                s->esp_rd_status8 |= 0x80;
            }
            break;
        case ALS4K_IOB_31_MIDI_STATUS:
            if (val == 0xff || val == 0x3f) {
                s->mpu_data = 0xfe;
                s->mpu_status &= ~0x80; /* RX not empty */
            }
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

    s->gcr_index = 0;
    memset(s->gcr_regs, 0, sizeof(s->gcr_regs));
    s->mixer_index = 0;
    memset(s->cr_regs, 0, sizeof(s->cr_regs));
    s->irq_status = 0;

    s->esp_resp_len = 0;
    s->esp_resp_idx = 0;
    s->esp_rd_status8 = 0;
    s->mpu_data = 0;
    s->mpu_status = 0x80;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  ALS4000_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ALS4000_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, ALS4000_CLASS_ID );
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
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "als4000-pio";

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
    .name = "snd_als4000_pci",
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
