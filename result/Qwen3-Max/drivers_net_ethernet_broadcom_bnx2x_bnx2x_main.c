#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qemu/main-loop.h" /* iothread mutex */
#include "qemu/module.h"
#include "qapi/visitor.h"

#define DRIVERS_NET_ETHERNET_BROADCOM_BNX2X_BNX2X_MAIN_BUF_LEN 1024

extern uint64_t target_value[DRIVERS_NET_ETHERNET_BROADCOM_BNX2X_BNX2X_MAIN_BUF_LEN];
extern bool target_value_reset;

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio[7];

    uint32_t len;
    uint64_t buf[DRIVERS_NET_ETHERNET_BROADCOM_BNX2X_BNX2X_MAIN_BUF_LEN];
    int is_probe_time;
} Drivers_net_ethernet_broadcom_bnx2x_bnx2x_mainState;

#define TYPE_PCI_DRIVERS_NET_ETHERNET_BROADCOM_BNX2X_BNX2X_MAIN_DEVICE "drivers_net_ethernet_broadcom_bnx2x_bnx2x_main"
#define DRIVERS_NET_ETHERNET_BROADCOM_BNX2X_BNX2X_MAIN(obj)        OBJECT_CHECK(Drivers_net_ethernet_broadcom_bnx2x_bnx2x_mainState, obj, TYPE_PCI_DRIVERS_NET_ETHERNET_BROADCOM_BNX2X_BNX2X_MAIN_DEVICE)

static uint64_t drivers_net_ethernet_broadcom_bnx2x_bnx2x_main_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    Drivers_net_ethernet_broadcom_bnx2x_bnx2x_mainState *drivers_net_ethernet_broadcom_bnx2x_bnx2x_main = opaque;
    int i;

    if (target_value_reset) {
        drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->len = 0;
        target_value_reset = false;
    }

    if (!drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->is_probe_time) {
        for (i = 0; i < DRIVERS_NET_ETHERNET_BROADCOM_BNX2X_BNX2X_MAIN_BUF_LEN; ++i) {
            drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[i] = target_value[i];
        }
    } else {
        drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->is_probe_time--;
    }

    return drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[(drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->len++) % DRIVERS_NET_ETHERNET_BROADCOM_BNX2X_BNX2X_MAIN_BUF_LEN];
}

static void drivers_net_ethernet_broadcom_bnx2x_bnx2x_main_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                unsigned size)
{
	return;
}

static const MemoryRegionOps drivers_net_ethernet_broadcom_bnx2x_bnx2x_main_mmio_ops = {
    .read = drivers_net_ethernet_broadcom_bnx2x_bnx2x_main_mmio_read,
    .write = drivers_net_ethernet_broadcom_bnx2x_bnx2x_main_mmio_write,
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

static void pci_drivers_net_ethernet_broadcom_bnx2x_bnx2x_main_realize(PCIDevice *pdev, Error **errp)
{
    Drivers_net_ethernet_broadcom_bnx2x_bnx2x_mainState *drivers_net_ethernet_broadcom_bnx2x_bnx2x_main = DRIVERS_NET_ETHERNET_BROADCOM_BNX2X_BNX2X_MAIN(pdev);
    uint8_t *pci_conf = pdev->config;
	int pos;

    pci_config_set_interrupt_pin(pci_conf, 1);
	pci_set_long(&pci_conf[152], 4294967295);
	

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
	
	pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
	pcie_endpoint_cap_init(pdev, 0);

	pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
	pdev->exp.pm_cap = pos;
	pci_set_word(pdev->config + pos + PCI_PM_PMC, 0x3);

    drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->len = 0;
    drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->is_probe_time = 496;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[0] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[1] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[2] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[3] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[4] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[5] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[6] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[7] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[8] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[9] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[10] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[11] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[12] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[13] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[14] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[15] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[16] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[17] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[18] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[19] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[20] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[21] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[22] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[23] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[24] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[25] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[26] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[27] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[28] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[29] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[30] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[31] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[32] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[33] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[34] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[35] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[36] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[37] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[38] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[39] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[40] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[41] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[42] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[43] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[44] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[45] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[46] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[47] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[48] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[49] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[50] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[51] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[52] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[53] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[54] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[55] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[56] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[57] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[58] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[59] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[60] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[61] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[62] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[63] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[64] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[65] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[66] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[67] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[68] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[69] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[70] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[71] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[72] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[73] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[74] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[75] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[76] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[77] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[78] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[79] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[80] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[81] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[82] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[83] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[84] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[85] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[86] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[87] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[88] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[89] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[90] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[91] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[92] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[93] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[94] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[95] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[96] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[97] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[98] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[99] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[100] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[101] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[102] = 0x3;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[103] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[104] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[105] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[106] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[107] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[108] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[109] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[110] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[111] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[112] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[113] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[114] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[115] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[116] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[117] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[118] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[119] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[120] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[121] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[122] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[123] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[124] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[125] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[126] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[127] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[128] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[129] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[130] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[131] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[132] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[133] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[134] = 0x3;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[135] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[136] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[137] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[138] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[139] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[140] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[141] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[142] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[143] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[144] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[145] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[146] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[147] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[148] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[149] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[150] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[151] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[152] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[153] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[154] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[155] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[156] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[157] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[158] = 0x3;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[159] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[160] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[161] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[162] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[163] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[164] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[165] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[166] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[167] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[168] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[169] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[170] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[171] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[172] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[173] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[174] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[175] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[176] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[177] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[178] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[179] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[180] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[181] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[182] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[183] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[184] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[185] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[186] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[187] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[188] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[189] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[190] = 0x3;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[191] = 0x0;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[192] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[193] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[194] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[195] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[196] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[197] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[198] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[199] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[200] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[201] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[202] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[203] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[204] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[205] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[206] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[207] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[208] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[209] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[210] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[211] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[212] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[213] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[214] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[215] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[216] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[217] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[218] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[219] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[220] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[221] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[222] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[223] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[224] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[225] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[226] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[227] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[228] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[229] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[230] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[231] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[232] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[233] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[234] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[235] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[236] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[237] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[238] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[239] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[240] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[241] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[242] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[243] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[244] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[245] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[246] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[247] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[248] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[249] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[250] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[251] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[252] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[253] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[254] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[255] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[256] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[257] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[258] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[259] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[260] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[261] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[262] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[263] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[264] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[265] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[266] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[267] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[268] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[269] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[270] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[271] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[272] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[273] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[274] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[275] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[276] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[277] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[278] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[279] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[280] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[281] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[282] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[283] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[284] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[285] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[286] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[287] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[288] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[289] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[290] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[291] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[292] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[293] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[294] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[295] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[296] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[297] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[298] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[299] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[300] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[301] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[302] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[303] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[304] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[305] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[306] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[307] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[308] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[309] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[310] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[311] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[312] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[313] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[314] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[315] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[316] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[317] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[318] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[319] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[320] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[321] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[322] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[323] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[324] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[325] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[326] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[327] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[328] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[329] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[330] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[331] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[332] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[333] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[334] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[335] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[336] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[337] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[338] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[339] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[340] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[341] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[342] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[343] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[344] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[345] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[346] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[347] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[348] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[349] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[350] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[351] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[352] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[353] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[354] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[355] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[356] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[357] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[358] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[359] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[360] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[361] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[362] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[363] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[364] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[365] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[366] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[367] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[368] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[369] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[370] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[371] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[372] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[373] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[374] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[375] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[376] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[377] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[378] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[379] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[380] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[381] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[382] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[383] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[384] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[385] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[386] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[387] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[388] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[389] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[390] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[391] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[392] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[393] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[394] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[395] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[396] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[397] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[398] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[399] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[400] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[401] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[402] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[403] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[404] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[405] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[406] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[407] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[408] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[409] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[410] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[411] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[412] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[413] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[414] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[415] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[416] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[417] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[418] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[419] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[420] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[421] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[422] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[423] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[424] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[425] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[426] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[427] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[428] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[429] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[430] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[431] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[432] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[433] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[434] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[435] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[436] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[437] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[438] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[439] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[440] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[441] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[442] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[443] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[444] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[445] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[446] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[447] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[448] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[449] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[450] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[451] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[452] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[453] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[454] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[455] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[456] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[457] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[458] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[459] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[460] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[461] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[462] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[463] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[464] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[465] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[466] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[467] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[468] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[469] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[470] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[471] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[472] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[473] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[474] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[475] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[476] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[477] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[478] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[479] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[480] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[481] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[482] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[483] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[484] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[485] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[486] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[487] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[488] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[489] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[490] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[491] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[492] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[493] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[494] = 0xff;
	drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->buf[495] = 0xff;;

    memory_region_init_io(&drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->mmio[0], OBJECT(drivers_net_ethernet_broadcom_bnx2x_bnx2x_main), &drivers_net_ethernet_broadcom_bnx2x_bnx2x_main_mmio_ops, drivers_net_ethernet_broadcom_bnx2x_bnx2x_main,
                    "drivers_net_ethernet_broadcom_bnx2x_bnx2x_main-mmio0", 16 * MiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->mmio[0]);
    memory_region_init_io(&drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->mmio[1], OBJECT(drivers_net_ethernet_broadcom_bnx2x_bnx2x_main), &drivers_net_ethernet_broadcom_bnx2x_bnx2x_main_mmio_ops, drivers_net_ethernet_broadcom_bnx2x_bnx2x_main,
                    "drivers_net_ethernet_broadcom_bnx2x_bnx2x_main-mmio1", 16 * MiB);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->mmio[1]);
    memory_region_init_io(&drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->mmio[2], OBJECT(drivers_net_ethernet_broadcom_bnx2x_bnx2x_main), &drivers_net_ethernet_broadcom_bnx2x_bnx2x_main_mmio_ops, drivers_net_ethernet_broadcom_bnx2x_bnx2x_main,
                    "drivers_net_ethernet_broadcom_bnx2x_bnx2x_main-mmio2", 16 * MiB);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->mmio[2]);
    memory_region_init_io(&drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->mmio[3], OBJECT(drivers_net_ethernet_broadcom_bnx2x_bnx2x_main), &drivers_net_ethernet_broadcom_bnx2x_bnx2x_main_mmio_ops, drivers_net_ethernet_broadcom_bnx2x_bnx2x_main,
                    "drivers_net_ethernet_broadcom_bnx2x_bnx2x_main-mmio3", 16 * MiB);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->mmio[3]);
    memory_region_init_io(&drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->mmio[4], OBJECT(drivers_net_ethernet_broadcom_bnx2x_bnx2x_main), &drivers_net_ethernet_broadcom_bnx2x_bnx2x_main_mmio_ops, drivers_net_ethernet_broadcom_bnx2x_bnx2x_main,
                    "drivers_net_ethernet_broadcom_bnx2x_bnx2x_main-mmio4", 16 * MiB);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->mmio[4]);
    memory_region_init_io(&drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->mmio[5], OBJECT(drivers_net_ethernet_broadcom_bnx2x_bnx2x_main), &drivers_net_ethernet_broadcom_bnx2x_bnx2x_main_mmio_ops, drivers_net_ethernet_broadcom_bnx2x_bnx2x_main,
                    "drivers_net_ethernet_broadcom_bnx2x_bnx2x_main-mmio5", 16 * MiB);
    pci_register_bar(pdev, 5, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->mmio[5]);
    memory_region_init_io(&drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->mmio[6], OBJECT(drivers_net_ethernet_broadcom_bnx2x_bnx2x_main), &drivers_net_ethernet_broadcom_bnx2x_bnx2x_main_mmio_ops, drivers_net_ethernet_broadcom_bnx2x_bnx2x_main,
                    "drivers_net_ethernet_broadcom_bnx2x_bnx2x_main-mmio5", 16 * MiB);
    pci_register_bar(pdev, 6, PCI_BASE_ADDRESS_SPACE_MEMORY, &drivers_net_ethernet_broadcom_bnx2x_bnx2x_main->mmio[6]);
}

static void pci_drivers_net_ethernet_broadcom_bnx2x_bnx2x_main_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static void drivers_net_ethernet_broadcom_bnx2x_bnx2x_main_instance_init(Object *obj)
{
	return;
}

static void drivers_net_ethernet_broadcom_bnx2x_bnx2x_main_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = pci_drivers_net_ethernet_broadcom_bnx2x_bnx2x_main_realize;
    k->exit = pci_drivers_net_ethernet_broadcom_bnx2x_bnx2x_main_uninit;
    k->vendor_id = 0x14e4;
    k->device_id = 0x164e;
    k->revision = 0;
    k->subsystem_vendor_id = 0xffffffff;
    k->subsystem_id = 0xffffffff;
    k->class_id = 16777215;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pci_drivers_net_ethernet_broadcom_bnx2x_bnx2x_main_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo drivers_net_ethernet_broadcom_bnx2x_bnx2x_main_info = {
        .name          = TYPE_PCI_DRIVERS_NET_ETHERNET_BROADCOM_BNX2X_BNX2X_MAIN_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(Drivers_net_ethernet_broadcom_bnx2x_bnx2x_mainState),
        .instance_init = drivers_net_ethernet_broadcom_bnx2x_bnx2x_main_instance_init,
        .class_init    = drivers_net_ethernet_broadcom_bnx2x_bnx2x_main_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&drivers_net_ethernet_broadcom_bnx2x_bnx2x_main_info);
}
type_init(pci_drivers_net_ethernet_broadcom_bnx2x_bnx2x_main_register_types)
