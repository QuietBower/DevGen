#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define DRIVERS_VIDEO_FBDEV_ATY_RADEON_BASE_BUF_LEN 1024

extern uint64_t target_value[DRIVERS_VIDEO_FBDEV_ATY_RADEON_BASE_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[DRIVERS_VIDEO_FBDEV_ATY_RADEON_BASE_BUF_LEN];
    int is_probe_time;
} Drivers_video_fbdev_aty_radeon_baseState;

#define TYPE_PCI_DRIVERS_VIDEO_FBDEV_ATY_RADEON_BASE_DEVICE "drivers_video_fbdev_aty_radeon_base"
#define DRIVERS_VIDEO_FBDEV_ATY_RADEON_BASE(obj)        OBJECT_CHECK(Drivers_video_fbdev_aty_radeon_baseState, obj, TYPE_PCI_DRIVERS_VIDEO_FBDEV_ATY_RADEON_BASE_DEVICE)

static uint64_t drivers_video_fbdev_aty_radeon_base_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Drivers_video_fbdev_aty_radeon_baseState *drivers_video_fbdev_aty_radeon_base = opaque;
    int i;

    if (target_value_reset) {
        drivers_video_fbdev_aty_radeon_base->len = 0;
        target_value_reset = false;
    }

    if (!drivers_video_fbdev_aty_radeon_base->is_probe_time) {
        for (i = 0; i < DRIVERS_VIDEO_FBDEV_ATY_RADEON_BASE_BUF_LEN; ++i) {
            drivers_video_fbdev_aty_radeon_base->buf[i] = target_value[i];
        }
    } else {
        drivers_video_fbdev_aty_radeon_base->is_probe_time--;
    }

    return drivers_video_fbdev_aty_radeon_base->buf[(drivers_video_fbdev_aty_radeon_base->len++) % DRIVERS_VIDEO_FBDEV_ATY_RADEON_BASE_BUF_LEN];
}

static void drivers_video_fbdev_aty_radeon_base_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps drivers_video_fbdev_aty_radeon_base_mmio_ops = {
    .read = drivers_video_fbdev_aty_radeon_base_mmio_read,
    .write = drivers_video_fbdev_aty_radeon_base_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 8,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 8,
    },

};

static void pci_drivers_video_fbdev_aty_radeon_base_realize(PCIDevice *pdev, Error **errp)
{
    Drivers_video_fbdev_aty_radeon_baseState *drivers_video_fbdev_aty_radeon_base = DRIVERS_VIDEO_FBDEV_ATY_RADEON_BASE(pdev);
    uint8_t *pci_conf = pdev->config;
	int pos;

    pci_config_set_interrupt_pin(pci_conf, 1);
	

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
	
	pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
	pcie_endpoint_cap_init(pdev, 0);

	pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
	pdev->exp.pm_cap = pos;
	pci_set_word(pdev->config + pos + PCI_PM_PMC, 0x3);

    drivers_video_fbdev_aty_radeon_base->len = 0;
    drivers_video_fbdev_aty_radeon_base->is_probe_time = 214;
	drivers_video_fbdev_aty_radeon_base->buf[0] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[1] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[2] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[3] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[4] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[5] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[6] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[7] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[8] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[9] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[10] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[11] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[12] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[13] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[14] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[15] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[16] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[17] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[18] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[19] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[20] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[21] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[22] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[23] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[24] = 0x0;
	drivers_video_fbdev_aty_radeon_base->buf[25] = 0x0;
	drivers_video_fbdev_aty_radeon_base->buf[26] = 0x0;
	drivers_video_fbdev_aty_radeon_base->buf[27] = 0x0;
	drivers_video_fbdev_aty_radeon_base->buf[28] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[29] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[30] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[31] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[32] = 0x0;
	drivers_video_fbdev_aty_radeon_base->buf[33] = 0x0;
	drivers_video_fbdev_aty_radeon_base->buf[34] = 0x0;
	drivers_video_fbdev_aty_radeon_base->buf[35] = 0x0;
	drivers_video_fbdev_aty_radeon_base->buf[36] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[37] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[38] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[39] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[40] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[41] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[42] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[43] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[44] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[45] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[46] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[47] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[48] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[49] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[50] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[51] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[52] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[53] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[54] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[55] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[56] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[57] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[58] = 0x0;
	drivers_video_fbdev_aty_radeon_base->buf[59] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[60] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[61] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[62] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[63] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[64] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[65] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[66] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[67] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[68] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[69] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[70] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[71] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[72] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[73] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[74] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[75] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[76] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[77] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[78] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[79] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[80] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[81] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[82] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[83] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[84] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[85] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[86] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[87] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[88] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[89] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[90] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[91] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[92] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[93] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[94] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[95] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[96] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[97] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[98] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[99] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[100] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[101] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[102] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[103] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[104] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[105] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[106] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[107] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[108] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[109] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[110] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[111] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[112] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[113] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[114] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[115] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[116] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[117] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[118] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[119] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[120] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[121] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[122] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[123] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[124] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[125] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[126] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[127] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[128] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[129] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[130] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[131] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[132] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[133] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[134] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[135] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[136] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[137] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[138] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[139] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[140] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[141] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[142] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[143] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[144] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[145] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[146] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[147] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[148] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[149] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[150] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[151] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[152] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[153] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[154] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[155] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[156] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[157] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[158] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[159] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[160] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[161] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[162] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[163] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[164] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[165] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[166] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[167] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[168] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[169] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[170] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[171] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[172] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[173] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[174] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[175] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[176] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[177] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[178] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[179] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[180] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[181] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[182] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[183] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[184] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[185] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[186] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[187] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[188] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[189] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[190] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[191] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[192] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[193] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[194] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[195] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[196] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[197] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[198] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[199] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[200] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[201] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[202] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[203] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[204] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[205] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[206] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[207] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[208] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[209] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[210] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[211] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[212] = 0xff;
	drivers_video_fbdev_aty_radeon_base->buf[213] = 0xff;;

    memory_region_init_io(&drivers_video_fbdev_aty_radeon_base->mmio[0], OBJECT(drivers_video_fbdev_aty_radeon_base), &drivers_video_fbdev_aty_radeon_base_mmio_ops, drivers_video_fbdev_aty_radeon_base,
                    "drivers_video_fbdev_aty_radeon_base-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_video_fbdev_aty_radeon_base->mmio[0]);
    memory_region_init_io(&drivers_video_fbdev_aty_radeon_base->mmio[1], OBJECT(drivers_video_fbdev_aty_radeon_base), &drivers_video_fbdev_aty_radeon_base_mmio_ops, drivers_video_fbdev_aty_radeon_base,
                    "drivers_video_fbdev_aty_radeon_base-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_video_fbdev_aty_radeon_base->mmio[1]);
    memory_region_init_io(&drivers_video_fbdev_aty_radeon_base->mmio[2], OBJECT(drivers_video_fbdev_aty_radeon_base), &drivers_video_fbdev_aty_radeon_base_mmio_ops, drivers_video_fbdev_aty_radeon_base,
                    "drivers_video_fbdev_aty_radeon_base-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_video_fbdev_aty_radeon_base->mmio[2]);
    memory_region_init_io(&drivers_video_fbdev_aty_radeon_base->mmio[3], OBJECT(drivers_video_fbdev_aty_radeon_base), &drivers_video_fbdev_aty_radeon_base_mmio_ops, drivers_video_fbdev_aty_radeon_base,
                    "drivers_video_fbdev_aty_radeon_base-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_video_fbdev_aty_radeon_base->mmio[3]);
    memory_region_init_io(&drivers_video_fbdev_aty_radeon_base->mmio[4], OBJECT(drivers_video_fbdev_aty_radeon_base), &drivers_video_fbdev_aty_radeon_base_mmio_ops, drivers_video_fbdev_aty_radeon_base,
                    "drivers_video_fbdev_aty_radeon_base-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_video_fbdev_aty_radeon_base->mmio[4]);
    memory_region_init_io(&drivers_video_fbdev_aty_radeon_base->mmio[5], OBJECT(drivers_video_fbdev_aty_radeon_base), &drivers_video_fbdev_aty_radeon_base_mmio_ops, drivers_video_fbdev_aty_radeon_base,
                    "drivers_video_fbdev_aty_radeon_base-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_video_fbdev_aty_radeon_base->mmio[5]);
    memory_region_init_io(&drivers_video_fbdev_aty_radeon_base->mmio[6], OBJECT(drivers_video_fbdev_aty_radeon_base), &drivers_video_fbdev_aty_radeon_base_mmio_ops, drivers_video_fbdev_aty_radeon_base,
                    "drivers_video_fbdev_aty_radeon_base-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_video_fbdev_aty_radeon_base->mmio[6]);
}

static void pci_drivers_video_fbdev_aty_radeon_base_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void drivers_video_fbdev_aty_radeon_base_instance_init(Object *obj)
{
	return;
}

static void drivers_video_fbdev_aty_radeon_base_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_drivers_video_fbdev_aty_radeon_base_realize;
    k->exit = pci_drivers_video_fbdev_aty_radeon_base_uninit;
    k->vendor_id = 0x1002;
    k->device_id = 0x5955;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_drivers_video_fbdev_aty_radeon_base_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo drivers_video_fbdev_aty_radeon_base_info = {
        .name          = TYPE_PCI_DRIVERS_VIDEO_FBDEV_ATY_RADEON_BASE_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Drivers_video_fbdev_aty_radeon_baseState),
        .instance_init = drivers_video_fbdev_aty_radeon_base_instance_init,
        .class_init    = drivers_video_fbdev_aty_radeon_base_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&drivers_video_fbdev_aty_radeon_base_info);
}
type_init(pci_drivers_video_fbdev_aty_radeon_base_register_types)
