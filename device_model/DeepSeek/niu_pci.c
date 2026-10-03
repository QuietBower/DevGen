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

#define TYPE_PCIBASE_DEVICE "niu_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID 0x108e
#define DEVICE_ID 0xabcd
#define CLASS_ID PCI_CLASS_NETWORK_ETHERNET

#define FZC_MAC 0x180000UL
#define DMC 0x600000UL
#define TXC 0x700000UL
#define ZCP 0x500000UL
#define FFLP 0x300000UL
#define FZC_FFLP 0x380000UL
#define FZC_DMC 0x680000UL
#define FZC_PIO 0x080000UL
#define FZC_TXC 0x780000UL
#define FZC_ZCP 0x580000UL
#define FZC_IPP 0x280000UL
#define FZC_PROM 0xc80000UL
#define PIO_LDSV 0x800000UL
#define PIO_IMASK1 0xb00000UL
#define PIO_IMASK0 0xa00000UL

#define XMAC_PORT0_OFF (FZC_MAC + 0x000000)
#define XMAC_PORT1_OFF (FZC_MAC + 0x006000)
#define BMAC_PORT2_OFF (FZC_MAC + 0x00c000)
#define BMAC_PORT3_OFF (FZC_MAC + 0x010000)

#define XTXMAC_SW_RST 0x00000UL
#define XRXMAC_SW_RST 0x00008UL
#define BTXMAC_SW_RST 0x00000UL
#define BRXMAC_SW_RST 0x00008UL

#define MIF_FRAME_OUTPUT (FZC_MAC + 0x16018UL)
#define MIF_CONFIG       (FZC_MAC + 0x16020UL)
#define MIF_STATUS       (FZC_MAC + 0x16040UL)

#define ESPC_NCR_BASE   (FZC_PROM + 0x40080UL)
#define ESPC_NCR(IDX)   (ESPC_NCR_BASE + (IDX)*0x8UL)

#define ESPC_VER_IMGSZ            ESPC_NCR(21)
#define ESPC_VER_IMGSZ_IMGSZ      0x00000000ffff0000ULL
#define ESPC_VER_IMGSZ_IMGSZ_SHIFT 16

#define ESPC_PHY_TYPE             ESPC_NCR(18)
#define ESPC_PHY_TYPE_PORT0       0x00000000ff000000ULL
#define ESPC_PHY_TYPE_PORT1       0x0000000000ff0000ULL
#define ESPC_PHY_TYPE_PORT2       0x000000000000ff00ULL
#define ESPC_PHY_TYPE_PORT3       0x00000000000000ffULL
#define ESPC_PHY_TYPE_PORT0_SHIFT 24
#define ESPC_PHY_TYPE_PORT1_SHIFT 16
#define ESPC_PHY_TYPE_PORT2_SHIFT 8
#define ESPC_PHY_TYPE_PORT3_SHIFT 0

#define ESPC_MAC_ADDR0            ESPC_NCR(0)
#define ESPC_MAC_ADDR1            ESPC_NCR(1)
#define ESPC_MOD_STR_LEN          ESPC_NCR(4)
#define ESPC_BD_MOD_STR_LEN       ESPC_NCR(13)
#define ESPC_NUM_PORTS_MACS       ESPC_NCR(2)
#define ESPC_NUM_PORTS_MACS_VAL   0x00000000000000ffULL

#define ESPC_PIO_EN     (FZC_PROM + 0x40000UL)
#define ESPC_PIO_STAT   (FZC_PROM + 0x40008UL)
#define ESPC_PIO_EN_ENABLE          0x0000000000000001ULL

#define ESR_INT_SIGNALS       (FZC_MAC + 0x14800UL)
#define ESR_INT_SRDY0_P0      0x0000000020000000ULL
#define ESR_INT_DET0_P0       0x0000000010000000ULL
#define ESR_INT_XSRDY_P0      0x0000000002000000ULL
#define ESR_INT_XDP_P0_CH0    0x0000000000200000ULL
#define ESR_INT_XDP_P0_CH1    0x0000000000400000ULL
#define ESR_INT_XDP_P0_CH2    0x0000000000800000ULL
#define ESR_INT_XDP_P0_CH3    0x0000000001000000ULL
#define ESR_INT_SRDY0_P1      0x0000000008000000ULL
#define ESR_INT_DET0_P1       0x0000000004000000ULL
#define ESR_INT_XSRDY_P1      0x0000000000100000ULL
#define ESR_INT_XDP_P1_CH0    0x0000000000010000ULL
#define ESR_INT_XDP_P1_CH1    0x0000000000020000ULL
#define ESR_INT_XDP_P1_CH2    0x0000000000040000ULL
#define ESR_INT_XDP_P1_CH3    0x0000000000080000ULL

#define BAR0_SIZE 0x1000000
#define BAR2_SIZE 0x1000

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
    uint64_t intr_status;
    uint64_t intr_mask;

    uint8_t mmio[BAR0_SIZE];

    uint64_t rx_desc_base;
    uint64_t tx_desc_base;

    uint32_t status_flags;
    bool in_reset;
    uint8_t power_state;
    uint8_t plat_type;

    uint64_t mif_response;
    bool mif_response_valid;

    struct {
        hwaddr offset;
        uint64_t mask;
    } auto_clear[16];
    int num_auto_clear;

    uint8_t vpd_data[256];
};

static inline uint64_t mmio_get64(PCIBaseState *s, hwaddr addr)
{
    return ldq_le_p(&s->mmio[addr]);
}
static inline void mmio_set64(PCIBaseState *s, hwaddr addr, uint64_t val)
{
    stq_le_p(&s->mmio[addr], val);
}

static void add_auto_clear(PCIBaseState *s, hwaddr offset, uint64_t mask)
{
    if (s->num_auto_clear < 16) {
        s->auto_clear[s->num_auto_clear].offset = offset;
        s->auto_clear[s->num_auto_clear].mask = mask;
        s->num_auto_clear++;
    }
}

static uint64_t handle_auto_clear(PCIBaseState *s, hwaddr addr, uint64_t current_val)
{
    for (int i = 0; i < s->num_auto_clear; i++) {
        if (s->auto_clear[i].offset == addr) {
            current_val &= ~s->auto_clear[i].mask;
            s->auto_clear[i].mask = 0;
            return current_val;
        }
    }
    return current_val;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 8) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned read size=%d addr=0x%"HWADDR_PRIx"\n",
                      __func__, size, addr);
        return ~0ULL;
    }

    if (addr == MIF_FRAME_OUTPUT) {
        if (s->mif_response_valid) {
            val = s->mif_response;
            s->mif_response_valid = false;
        } else {
            val = 0;
        }
        return val;
    }

    if (addr == MIF_STATUS) {
        return s->mif_response_valid ? 1 : 0;
    }

    val = mmio_get64(s, addr);
    return handle_auto_clear(s, addr, val);
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 8) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned write size=%d addr=0x%"HWADDR_PRIx"\n",
                      __func__, size, addr);
        return;
    }

    if (addr == MIF_FRAME_OUTPUT) {
        s->mif_response = (1ULL << 16) | 0xffff;
        s->mif_response_valid = true;
        return;
    }

    if (addr == XMAC_PORT0_OFF + XTXMAC_SW_RST && (val & 0x3)) {
        add_auto_clear(s, addr, 0x3);
    }
    if (addr == XMAC_PORT0_OFF + XRXMAC_SW_RST && (val & 0x3)) {
        add_auto_clear(s, addr, 0x3);
    }
    if (addr == BMAC_PORT2_OFF + BTXMAC_SW_RST && (val & 0x1)) {
        add_auto_clear(s, addr, 0x1);
    }
    if (addr == BMAC_PORT2_OFF + BRXMAC_SW_RST && (val & 0x1)) {
        add_auto_clear(s, addr, 0x1);
    }

    mmio_set64(s, addr, val);
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 8, .max_access_size = 8 },
    .impl  = { .min_access_size = 8, .max_access_size = 8 },
};

static ssize_t pcibase_vpd_read(PCIDevice *pdev, off_t offset, uint8_t *buf, size_t len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (offset >= sizeof(s->vpd_data)) {
        return 0;
    }
    if (offset + len > sizeof(s->vpd_data)) {
        len = sizeof(s->vpd_data) - offset;
    }
    memcpy(buf, s->vpd_data + offset, len);
    return len;
}

static ssize_t pcibase_vpd_write(PCIDevice *pdev, off_t offset, const uint8_t *buf, size_t len)
{
    return 0;
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->mmio, 0, BAR0_SIZE);
    s->mif_response_valid = false;
    s->num_auto_clear = 0;

    mmio_set64(s, ESR_INT_SIGNALS,
               ESR_INT_SRDY0_P0 | ESR_INT_DET0_P0 | ESR_INT_XSRDY_P0 |
               ESR_INT_XDP_P0_CH0 | ESR_INT_XDP_P0_CH1 | ESR_INT_XDP_P0_CH2 | ESR_INT_XDP_P0_CH3 |
               ESR_INT_SRDY0_P1 | ESR_INT_DET0_P1 | ESR_INT_XSRDY_P1 |
               ESR_INT_XDP_P1_CH0 | ESR_INT_XDP_P1_CH1 | ESR_INT_XDP_P1_CH2 | ESR_INT_XDP_P1_CH3);

    mmio_set64(s, MIF_CONFIG, 0);

    /* populate VPD data with SPROM image and correct checksum */
    uint64_t sprom[32] = {0};
    sprom[0] = 0x0000000033221100ULL;
    sprom[1] = 0x0000000000005544ULL;
    sprom[2] = 0x0000000000000002ULL;
    sprom[4] = 0x000000000000000cULL;
    sprom[5] = 0x0000000053554e57ULL;
    sprom[6] = 0x000000002c435033ULL;
    sprom[7] = 0x0000000032363000ULL;
    sprom[8] = 0x00000000000000c5ULL;
    sprom[13] = 0x0000000000000000ULL;
    sprom[18] = 0x0000000001000000ULL;
    sprom[21] = 0x0000000001000000ULL;

    memcpy(s->vpd_data, sprom, sizeof(sprom));

    uint8_t sum = 0;
    for (int i = 0; i < 255; i++) {
        sum += s->vpd_data[i];
    }
    s->vpd_data[255] = (uint8_t)((0xab - sum) & 0xff);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) return;

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* VPD capability for SPROM image */
    pci_vpd_register(pdev, s->vpd_data, sizeof(s->vpd_data), 0);

    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "niu-mmio";

    s->bar_info[1].index = 2;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = BAR2_SIZE;
    s->bar_info[1].name = "niu-bar2";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    msix_init_exclusive_bar(pdev, 64, 4, errp);
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

static const VMStateDescription vmstate_pcibase = {
    .name = "niu_pci",
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

type_init(pcibase_register_types)
