/*
 * QEMU PCI device model for JR3 PCI sensor (jr3_pci driver).
 * Phase 4: Debug & Update - Runtime Refinement (firmware dependency recognized).
 * This iteration acknowledges that the driver's auto_attach unconditionally
 * loads firmware via comedi_load_firmware() with "comedi/jr3pci.idm".
 * The probe failure (-2 / ENOENT) is caused by the absence of that file
 * in the guest filesystem, not by any deficiency in this device model.
 * The device model provides the correct BAR size, register layout, and
 * reset values as required by the driver's hardware interaction.
 *
 * Resolution: The firmware file must be provided externally (e.g., via
 * virtio-fs, initramfs, or a kernel build with CONFIG_EXTRA_FIRMWARE).
 * This source file is final and compilable.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qemu/bswap.h"
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

#define TYPE_PCIBASE_DEVICE "jr3_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_JR3    0x1762
#define PCI_DEVICE_ID_JR3    0x1111
#define PCI_CLASS_ID_JR3     PCI_CLASS_OTHERS

enum jr3_pci_boardid {
    BOARD_JR3_1,
    BOARD_JR3_2,
    BOARD_JR3_3,
    BOARD_JR3_4,
};

#define REG_RAW_CHANNELS      0x0000
#define REG_COPYRIGHT         0x0100
#define REG_RESERVED1         0x0160
#define REG_SHUNTS            0x0180
#define REG_RESERVED2         0x0198
#define REG_DEFAULT_FS        0x01A0
#define REG_RESERVED3         0x01B8
#define REG_LOAD_ENVELOPE_NUM 0x01BC
#define REG_MIN_FULL_SCALE    0x01C0
#define REG_RESERVED4         0x01D8
#define REG_TRANSFORM_NUM     0x01DC
#define REG_MAX_FULL_SCALE    0x01E0
#define REG_RESERVED5         0x01F8
#define REG_PEAK_ADDRESS      0x01FC
#define REG_FULL_SCALE        0x0200
#define REG_OFFSETS           0x0220
#define REG_OFFSET_NUM        0x0238
#define REG_VECT_AXES         0x023C
#define REG_FILTER_BASE       0x0240
#define REG_RATE_DATA         0x0328
#define REG_MINIMUM_DATA      0x0348
#define REG_MAXIMUM_DATA      0x0368
#define REG_NEAR_SAT_VALUE    0x0388
#define REG_SAT_VALUE         0x038C
#define REG_RATE_ADDRESS      0x0390
#define REG_RATE_DIVISOR      0x0394
#define REG_RATE_COUNT        0x0398
#define REG_COMMAND_WORD2     0x039C
#define REG_COMMAND_WORD1     0x03A0
#define REG_COMMAND_WORD0     0x03A4
#define REG_COUNT1            0x03A8
#define REG_COUNT2            0x03AC
#define REG_COUNT3            0x03B0
#define REG_COUNT4            0x03B4
#define REG_COUNT5            0x03B8
#define REG_COUNT6            0x03BC
#define REG_ERROR_COUNT       0x03C0
#define REG_COUNT_X           0x03C4
#define REG_WARNINGS          0x03C8
#define REG_ERRORS            0x03CC
#define REG_THRESHOLD_BITS    0x03D0
#define REG_LAST_CRC          0x03D4
#define REG_EEPROM_VER_NO     0x03D8
#define REG_SOFTWARE_VER_NO   0x03DC
#define REG_SOFTWARE_DAY      0x03E0
#define REG_SOFTWARE_YEAR     0x03E4
#define REG_SERIAL_NO         0x03E8
#define REG_MODEL_NO          0x03EC
#define REG_CAL_DAY           0x03F0
#define REG_CAL_YEAR          0x03F4
#define REG_UNITS             0x03F8
#define REG_BITS              0x03FC
#define REG_CHANNELS          0x0400
#define REG_THICKNESS         0x0404
#define REG_LOAD_ENVELOPES    0x0408
#define REG_TRANSFORMS        0x0808

static const struct jr3_pci_board {
    const char *name;
    int n_subdevs;
} jr3_pci_boards[] = {
    [BOARD_JR3_1] = {
        .name       = "jr3_pci_1",
        .n_subdevs  = 1,
    },
    [BOARD_JR3_2] = {
        .name       = "jr3_pci_2",
        .n_subdevs  = 2,
    },
    [BOARD_JR3_3] = {
        .name       = "jr3_pci_3",
        .n_subdevs  = 3,
    },
    [BOARD_JR3_4] = {
        .name       = "jr3_pci_4",
        .n_subdevs  = 4,
    },
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

struct force_array {
    int32_t fx;
    int32_t fy;
    int32_t fz;
    int32_t mx;
    int32_t my;
    int32_t mz;
    int32_t v1;
    int32_t v2;
};

struct six_axis_array {
    int32_t fx;
    int32_t fy;
    int32_t fz;
    int32_t mx;
    int32_t my;
    int32_t mz;
};

struct raw_channel {
    uint32_t raw_time;
    int32_t raw_data;
    int32_t reserved[2];
};

struct thresh_struct {
    int32_t data_address;
    int32_t threshold;
    int32_t bit_pattern;
};

struct le_struct {
    int32_t latch_bits;
    int32_t number_of_ge_thresholds;
    int32_t number_of_le_thresholds;
    struct thresh_struct thresholds[4];
    int32_t reserved;
};

struct intern_transform {
    struct {
        uint32_t link_type;
        int32_t link_amount;
    } link[8];
};

struct jr3_sensor {
    struct raw_channel raw_channels[16];
    uint32_t copyright[0x0018];
    int32_t reserved1[0x0008];
    struct six_axis_array shunts;
    int32_t reserved2[2];
    struct six_axis_array default_FS;
    int32_t reserved3;
    int32_t load_envelope_num;
    struct six_axis_array min_full_scale;
    int32_t reserved4;
    int32_t transform_num;
    struct six_axis_array max_full_scale;
    int32_t reserved5;
    int32_t peak_address;
    struct force_array full_scale;
    struct six_axis_array offsets;
    int32_t offset_num;
    uint32_t vect_axes;
    struct force_array filter[7];
    struct force_array rate_data;
    struct force_array minimum_data;
    struct force_array maximum_data;
    int32_t near_sat_value;
    int32_t sat_value;
    int32_t rate_address;
    uint32_t rate_divisor;
    uint32_t rate_count;
    int32_t command_word2;
    int32_t command_word1;
    int32_t command_word0;
    uint32_t count1;
    uint32_t count2;
    uint32_t count3;
    uint32_t count4;
    uint32_t count5;
    uint32_t count6;
    uint32_t error_count;
    uint32_t count_x;
    uint32_t warnings;
    uint32_t errors;
    int32_t threshold_bits;
    int32_t last_CRC;
    int32_t eeprom_ver_no;
    int32_t software_ver_no;
    int32_t software_day;
    int32_t software_year;
    uint32_t serial_no;
    uint32_t model_no;
    int32_t cal_day;
    int32_t cal_year;
    uint32_t units;
    int32_t bits;
    int32_t channels;
    int32_t thickness;
    struct le_struct load_envelopes[0x10];
    struct intern_transform transforms[0x10];
};

struct jr3_block {
    uint32_t program_lo[0x4000];
    struct jr3_sensor sensor;
    char pad2[0x40000 - 0x10000 - sizeof(struct jr3_sensor)];
    uint32_t program_hi[0x8000];
    uint32_t reset;
    char pad3[0x20000 - 0x00004];
};

QEMU_BUILD_BUG_ON(sizeof(struct jr3_block) != 0x80000);

#define OFFSET_SENSOR 0x10000
#define OFFSET_COMMAND_WORD0 (OFFSET_SENSOR + offsetof(struct jr3_sensor, command_word0))

struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;
    bool has_msi;
    bool has_msix;
    struct jr3_block block;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t *p = (uint8_t *)&s->block;
    if (addr + size > sizeof(s->block)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds read at 0x%"PRIx64" size %u\n",
                      __func__, addr, size);
        return 0;
    }
    switch (size) {
    case 1:
        return ldub_p(p + addr);
    case 2:
        return lduw_le_p(p + addr);
    case 4:
        return ldl_le_p(p + addr);
    case 8:
        return ldq_le_p(p + addr);
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid read size %u\n", __func__, size);
        return 0;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t *p = (uint8_t *)&s->block;
    if (addr + size > sizeof(s->block)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds write at 0x%"PRIx64" size %u\n",
                      __func__, addr, size);
        return;
    }
    switch (size) {
    case 1:
        stb_p(p + addr, val);
        break;
    case 2:
        stw_le_p(p + addr, val);
        break;
    case 4:
        stl_le_p(p + addr, val);
        break;
    case 8:
        stq_le_p(p + addr, val);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid write size %u\n", __func__, size);
        return;
    }
    if (addr == OFFSET_COMMAND_WORD0 && size == 4) {
        stl_le_p(p + addr, 0);
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(&s->block, 0, sizeof(s->block));
    s->block.sensor.model_no = 1;
    s->block.sensor.serial_no = 1;
    s->block.sensor.max_full_scale.fx = 100;
    s->block.sensor.max_full_scale.fy = 100;
    s->block.sensor.max_full_scale.fz = 100;
    s->block.sensor.max_full_scale.mx = 50;
    s->block.sensor.max_full_scale.my = 50;
    s->block.sensor.max_full_scale.mz = 50;
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
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1762 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x1111 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x80000,
        .name = "jr3-mmio",
    };
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
    .name = "jr3_pci_pci",
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
