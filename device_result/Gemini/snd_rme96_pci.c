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

/* Additional include files retrieved from driver context */


#define TYPE_PCIBASE_DEVICE "snd_rme96_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_XILINX		0x10ee
#define PCI_DEVICE_ID_RME_DIGI96	0x3fc0

#define RME96_SPDIF_NCHANNELS 2
#define RME96_BUFFER_SIZE 0x10000
#define RME96_IO_SIZE 0x60000
#define RME96_IO_PLAY_BUFFER      0x0
#define RME96_IO_REC_BUFFER       0x10000
#define RME96_IO_CONTROL_REGISTER 0x20000
#define RME96_IO_ADDITIONAL_REG   0x20004
#define RME96_IO_CONFIRM_PLAY_IRQ 0x20008
#define RME96_IO_CONFIRM_REC_IRQ  0x2000C
#define RME96_IO_SET_PLAY_POS     0x40000
#define RME96_IO_RESET_PLAY_POS   0x4FFFC
#define RME96_IO_SET_REC_POS      0x50000
#define RME96_IO_RESET_REC_POS    0x5FFFC
#define RME96_IO_GET_PLAY_POS     0x20000
#define RME96_IO_GET_REC_POS      0x30000
#define RME96_WCR_START     (1 << 0)
#define RME96_WCR_START_2   (1 << 1)
#define RME96_WCR_GAIN_0    (1 << 2)
#define RME96_WCR_GAIN_1    (1 << 3)
#define RME96_WCR_MODE24    (1 << 4)
#define RME96_WCR_MODE24_2  (1 << 5)
#define RME96_WCR_BM        (1 << 6)
#define RME96_WCR_BM_2      (1 << 7)
#define RME96_WCR_ADAT      (1 << 8)
#define RME96_WCR_FREQ_0    (1 << 9)
#define RME96_WCR_FREQ_1    (1 << 10)
#define RME96_WCR_DS        (1 << 11)
#define RME96_WCR_PRO       (1 << 12)
#define RME96_WCR_EMP       (1 << 13)
#define RME96_WCR_SEL       (1 << 14)
#define RME96_WCR_MASTER    (1 << 15)
#define RME96_WCR_PD        (1 << 16)
#define RME96_WCR_INP_0     (1 << 17)
#define RME96_WCR_INP_1     (1 << 18)
#define RME96_WCR_THRU_0    (1 << 19)
#define RME96_WCR_THRU_1    (1 << 20)
#define RME96_WCR_THRU_2    (1 << 21)
#define RME96_WCR_THRU_3    (1 << 22)
#define RME96_WCR_THRU_4    (1 << 23)
#define RME96_WCR_THRU_5    (1 << 24)
#define RME96_WCR_THRU_6    (1 << 25)
#define RME96_WCR_THRU_7    (1 << 26)
#define RME96_WCR_DOLBY     (1 << 27)
#define RME96_WCR_MONITOR_0 (1 << 28)
#define RME96_WCR_MONITOR_1 (1 << 29)
#define RME96_WCR_ISEL      (1 << 30)
#define RME96_WCR_IDIS      (1 << 31)
#define RME96_RCR_AUDIO_ADDR_MASK 0xFFFF
#define RME96_RCR_IRQ_2     (1 << 16)
#define RME96_RCR_T_OUT     (1 << 17)
#define RME96_RCR_DEV_ID_0  (1 << 21)
#define RME96_RCR_DEV_ID_1  (1 << 22)
#define RME96_RCR_LOCK      (1 << 23)
#define RME96_RCR_VERF      (1 << 26)
#define RME96_RCR_F0        (1 << 27)
#define RME96_RCR_F1        (1 << 28)
#define RME96_RCR_F2        (1 << 29)
#define RME96_RCR_AUTOSYNC  (1 << 30)
#define RME96_RCR_IRQ       (1 << 31)
#define RME96_AR_WSEL       (1 << 0)
#define RME96_AR_ANALOG     (1 << 1)
#define RME96_AR_FREQPAD_0  (1 << 2)
#define RME96_AR_FREQPAD_1  (1 << 3)
#define RME96_AR_FREQPAD_2  (1 << 4)
#define RME96_AR_PD2        (1 << 5)
#define RME96_AR_DAC_EN     (1 << 6)
#define RME96_AR_CLATCH     (1 << 7)
#define RME96_AR_CCLK       (1 << 8)
#define RME96_AR_CDATA      (1 << 9)
#define RME96_TB_START_PLAYBACK 1
#define RME96_TB_START_CAPTURE 2
#define RME96_TB_STOP_PLAYBACK 4
#define RME96_TB_STOP_CAPTURE 8
#define RME96_TB_RESET_PLAYPOS 16
#define RME96_TB_RESET_CAPTUREPOS 32
#define RME96_TB_CLEAR_PLAYBACK_IRQ 64
#define RME96_TB_CLEAR_CAPTURE_IRQ 128

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
    uint32_t wcreg;
    uint32_t wcreg_spdif;
    uint32_t wcreg_spdif_stream;
    uint32_t rcreg;
    uint32_t areg;
    uint16_t vol[2];
    uint8_t rev;
    uint32_t playback_pointer;
    uint32_t capture_pointer;

    uint8_t play_buffer[RME96_BUFFER_SIZE];
    uint8_t rec_buffer[RME96_BUFFER_SIZE];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->rcreg & RME96_RCR_IRQ) || (s->rcreg & RME96_RCR_IRQ_2);
    pci_set_irq(pdev, level ? 1 : 0);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < RME96_IO_REC_BUFFER) {
        if (addr + size <= RME96_BUFFER_SIZE) {
            memcpy(&val, &s->play_buffer[addr], size);
        }
        return val;
    } else if (addr < RME96_IO_CONTROL_REGISTER) {
        hwaddr offset = addr - RME96_IO_REC_BUFFER;
        if (offset + size <= RME96_BUFFER_SIZE) {
            memcpy(&val, &s->rec_buffer[offset], size);
        }
        return val;
    }

    switch (addr) {
    case RME96_IO_GET_PLAY_POS:
        val = (s->rcreg & ~RME96_RCR_AUDIO_ADDR_MASK) | (s->playback_pointer & RME96_RCR_AUDIO_ADDR_MASK);
        break;
    case RME96_IO_GET_REC_POS:
        val = (s->rcreg & ~RME96_RCR_AUDIO_ADDR_MASK) | (s->capture_pointer & RME96_RCR_AUDIO_ADDR_MASK);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < RME96_IO_REC_BUFFER) {
        if (addr + size <= RME96_BUFFER_SIZE) {
            memcpy(&s->play_buffer[addr], &val, size);
        }
        return;
    } else if (addr < RME96_IO_CONTROL_REGISTER) {
        hwaddr offset = addr - RME96_IO_REC_BUFFER;
        if (offset + size <= RME96_BUFFER_SIZE) {
            memcpy(&s->rec_buffer[offset], &val, size);
        }
        return;
    }

    if (addr >= RME96_IO_SET_PLAY_POS && addr < RME96_IO_SET_REC_POS) {
        if (addr == RME96_IO_RESET_PLAY_POS) {
            s->playback_pointer = 0;
        } else {
            s->playback_pointer = (addr - RME96_IO_SET_PLAY_POS) & RME96_RCR_AUDIO_ADDR_MASK;
        }
        return;
    }

    if (addr >= RME96_IO_SET_REC_POS && addr < RME96_IO_SIZE) {
        if (addr == RME96_IO_RESET_REC_POS) {
            s->capture_pointer = 0;
        } else {
            s->capture_pointer = (addr - RME96_IO_SET_REC_POS) & RME96_RCR_AUDIO_ADDR_MASK;
        }
        return;
    }

    switch (addr) {
    case RME96_IO_CONTROL_REGISTER:
        s->wcreg = val;
        break;
    case RME96_IO_ADDITIONAL_REG:
        s->areg = val;
        break;
    case RME96_IO_CONFIRM_PLAY_IRQ:
        s->rcreg &= ~RME96_RCR_IRQ;
        pcibase_update_irq(s);
        break;
    case RME96_IO_CONFIRM_REC_IRQ:
        s->rcreg &= ~RME96_RCR_IRQ_2;
        pcibase_update_irq(s);
        break;
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

    s->wcreg = 0;
    s->areg = 0;
    s->rcreg = 0;
    s->playback_pointer = 0;
    s->capture_pointer = 0;
    memset(s->play_buffer, 0, sizeof(s->play_buffer));
    memset(s->rec_buffer, 0, sizeof(s->rec_buffer));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_XILINX );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_RME_DIGI96 );
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
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = RME96_IO_SIZE;
    s->bar_info[0].name = "rme96-mmio";  
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
    .name = "snd_rme96_pci",
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
