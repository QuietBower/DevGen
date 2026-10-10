/*
 * QEMU PCI device model for RME9652-compatible audio card
 * Generated for Linux driver sound/pci/rme9652/rme9652.c
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

#include <stdint.h>

#define TYPE_PCIBASE_DEVICE "snd_rme9652_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define RME9652_NCHANNELS       26
#define RME9636_NCHANNELS       18
#define RME9652_SYNC_FROM_SPDIF 0
#define RME9652_SYNC_FROM_ADAT1 1
#define RME9652_SYNC_FROM_ADAT2 2
#define RME9652_SYNC_FROM_ADAT3 3
#define RME9652_SPDIFIN_OPTICAL 0
#define RME9652_SPDIFIN_COAXIAL 1
#define RME9652_SPDIFIN_INTERN  2
#define RME9652_IRQ           (1u<<0)
#define RME9652_lock_2       (1u<<1)
#define RME9652_lock_1       (1u<<2)
#define RME9652_lock_0       (1u<<3)
#define RME9652_fs48         (1u<<4)
#define RME9652_wsel_rd      (1u<<5)
#define RME9652_sync_2       (1u<<16)
#define RME9652_sync_1       (1u<<17)
#define RME9652_sync_0       (1u<<18)
#define RME9652_DS_rd        (1u<<19)
#define RME9652_tc_busy      (1u<<20)
#define RME9652_tc_out       (1u<<21)
#define RME9652_F_0          (1u<<22)
#define RME9652_F_1          (1u<<23)
#define RME9652_F_2          (1u<<24)
#define RME9652_ERF          (1u<<25)
#define RME9652_buffer_id    (1u<<26)
#define RME9652_tc_valid     (1u<<27)
#define RME9652_SPDIF_READ   (1u<<28)
#define RME9652_sync         (RME9652_sync_0|RME9652_sync_1|RME9652_sync_2)
#define RME9652_lock         (RME9652_lock_0|RME9652_lock_1|RME9652_lock_2)
#define RME9652_F            (RME9652_F_0|RME9652_F_1|RME9652_F_2)
#define RME9652_buf_pos      0x000FFC0u
#define RME9652_REV15_buf_pos(x) ((((x)&0xE0000000u)>>26)|((x)&RME9652_buf_pos))
#define RME9652_IO_EXTENT     1024
#define RME9652_init_buffer       0
#define RME9652_play_buffer       32
#define RME9652_rec_buffer        36
#define RME9652_control_register  64
#define RME9652_irq_clear         96
#define RME9652_time_code         100
#define RME9652_thru_base         128
#define RME9652_status_register    0
#define RME9652_start_bit       (1u<<0)
#define RME9652_Master          (1u<<4)
#define RME9652_IE              (1u<<5)
#define RME9652_freq            (1u<<6)
#define RME9652_freq1           (1u<<7)
#define RME9652_DS                 (1u<<8)
#define RME9652_PRO             (1u<<9)
#define RME9652_EMP             (1u<<10)
#define RME9652_Dolby           (1u<<11)
#define RME9652_opt_out           (1u<<12)
#define RME9652_wsel           (1u<<13)
#define RME9652_inp_0           (1u<<14)
#define RME9652_inp_1           (1u<<15)
#define RME9652_SyncPref_ADAT2    (1u<<16)
#define RME9652_SyncPref_ADAT3    (1u<<17)
#define RME9652_SPDIF_RESET        (1u<<18)
#define RME9652_SPDIF_SELECT       (1u<<19)
#define RME9652_SPDIF_CLOCK        (1u<<20)
#define RME9652_SPDIF_WRITE        (1u<<21)
#define RME9652_ADAT1_INTERNAL     (1u<<22)
#define RME9652_latency            0x0eu
#define RME9652_inp                (RME9652_inp_0|RME9652_inp_1)

#define RME9652_SyncPref_Mask      (RME9652_SyncPref_ADAT2|RME9652_SyncPref_ADAT3)
#define RME9652_SyncPref_ADAT1     0
#define RME9652_SyncPref_SPDIF     (RME9652_SyncPref_ADAT2|RME9652_SyncPref_ADAT3)

#define rme9652_encode_latency(x)  (((x)&0x7)<<1)
#define rme9652_encode_spdif_in(x) (((x)&0x3)<<14)
#define rme9652_decode_spdif_in(x) (((x)>>14)&0x3)
#define rme9652_running_double_speed(s) ((s)->control_register & RME9652_DS)
#define rme9652_decode_spdif_rate(x) ((x)>>22)

#define PCI_CLASS_MULTIMEDIA_AUDIO 0x0401

#define PCIBASE_VENDOR_ID 0x10ee
#define PCIBASE_DEVICE_ID 0x3fc4
#define PCIBASE_CLASS_ID  PCI_CLASS_MULTIMEDIA_AUDIO

#define RME9652_BAR_INDEX 0
#define RME9652_BAR_SIZE  RME9652_IO_EXTENT


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

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    uint32_t control_reg_shadow;
    uint32_t status_reg_shadow;

    uint32_t rec_buffer_addr;
    uint32_t play_buffer_addr;

    /* model internal: cached logical control state for convenience */
    uint32_t logical_control;
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->status_reg_shadow & RME9652_IRQ) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        return 0xffffffffu;
    }

    switch (addr) {
    case RME9652_status_register:
        /* status register: expose current status bits (including IRQ bit) */
        val = s->status_reg_shadow;
        break;
    case RME9652_control_register:
        /* control register reflects last written control value */
        val = s->control_reg_shadow;
        break;
    case RME9652_play_buffer:
        val = s->play_buffer_addr;
        break;
    case RME9652_rec_buffer:
        val = s->rec_buffer_addr;
        break;
    case RME9652_irq_clear:
        /* read of irq_clear has no side effects in driver */
        val = 0;
        break;
    case RME9652_time_code:
        /* driver may poll time code; we just return 0 for now */
        val = 0;
        break;
    default:
        /* thru bits or undefined offsets: return 0 */
        if (addr >= RME9652_thru_base &&
            addr < RME9652_thru_base + RME9652_NCHANNELS * 4) {
            /* thru channels - not explicitly accessed in provided code */
            val = 0;
        } else {
            val = 0;
        }
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case RME9652_status_register:
        /* driver does not appear to write status register directly */
        break;
    case RME9652_control_register: {
        /* cache raw control register value exactly as written */
        uint32_t v = (uint32_t)val;
        s->control_reg_shadow = v;

        /* keep a copy of logical control bits (mirrors driver control_register) */
        s->logical_control = v;
        break;
    }
    case RME9652_play_buffer:
        /* physical DMA address for playback buffer */
        s->play_buffer_addr = (uint32_t)val;
        break;
    case RME9652_rec_buffer:
        /* physical DMA address for capture buffer */
        s->rec_buffer_addr = (uint32_t)val;
        break;
    case RME9652_irq_clear:
        /* writing here clears IRQ bit as indicated by driver */
        s->status_reg_shadow &= ~RME9652_IRQ;
        pcibase_update_irq(s);
        break;
    default:
        if (addr >= RME9652_thru_base &&
            addr < RME9652_thru_base + RME9652_NCHANNELS * 4) {
            /* channel pass-thru bits: driver uses s->thru_bits; we only
             * need to accept writes so that reads of the driver cache make sense.
             * Hardware register layout is not explicitly defined in provided code,
             * so we ignore actual content here per no-hallucination rule.
             */
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
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

    s->control_reg_shadow = 0;
    s->status_reg_shadow = 0;
    s->rec_buffer_addr = 0;
    s->play_buffer_addr = 0;
    s->intr_status = 0;
    s->intr_mask = 0;
    s->logical_control = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x03);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    s->bar_info[0].index = RME9652_BAR_INDEX;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = RME9652_BAR_SIZE;
    s->bar_info[0].name = "rme9652-io";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    s->intr_status = 0;
    s->intr_mask = 0;
    s->control_reg_shadow = 0;
    s->status_reg_shadow = 0;
    s->rec_buffer_addr = 0;
    s->play_buffer_addr = 0;
    s->logical_control = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "snd_rme9652_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(control_reg_shadow, PCIBaseState),
        VMSTATE_UINT32(status_reg_shadow, PCIBaseState),
        VMSTATE_UINT32(rec_buffer_addr, PCIBaseState),
        VMSTATE_UINT32(play_buffer_addr, PCIBaseState),
        VMSTATE_UINT32(logical_control, PCIBaseState),
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
