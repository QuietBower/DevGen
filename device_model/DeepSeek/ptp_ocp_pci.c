/*
 * QEMU PCI device model for PTP OCP Timecard
 * Based on driver: /home/eely/linux-7.1/drivers/ptp/ptp_ocp.c
 * Phase 2: Full functional implementation.
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

#define TYPE_PCIBASE_DEVICE "ptp_ocp_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor and Device IDs extracted from driver's first pci_device_id entry */
#define VENDOR_ID 0x1d9b /* PCI_VENDOR_ID_META */
#define DEVICE_ID 0x0400 /* PCI_DEVICE_ID_META_TIMECARD */
#define CLASS_ID  0x0b4000 /* Precision Time */

/* Register offsets from ocp_fb_resource array */
#define IMAGE_OFFSET           0x00020000
#define PPS_SELECT_OFFSET      0x00130000
#define SMA_MAP1_OFFSET        0x00140000
#define SMA_MAP2_OFFSET        0x00220000
#define I2C_CTRL_OFFSET        0x00150000
#define PORT_GNSS_OFFSET       0x00161000
#define PORT_GNSS2_OFFSET      0x00171000
#define PORT_MAC_OFFSET        0x00181000
#define PORT_NMEA_OFFSET       0x00191000
#define SPI_FLASH_OFFSET       0x00310000
#define REG_OFFSET             0x01000000
#define TS0_OFFSET             0x01010000
#define TS1_OFFSET             0x01020000
#define PPS_TO_EXT_OFFSET      0x01030000
#define PPS_TO_CLK_OFFSET      0x01040000
#define TOD_OFFSET             0x01050000
#define TS2_OFFSET             0x01060000
#define IRIG_IN_OFFSET         0x01070000
#define IRIG_OUT_OFFSET        0x01080000
#define DCF_IN_OFFSET          0x01090000
#define DCF_OUT_OFFSET         0x010A0000
#define NMEA_OUT_OFFSET        0x010B0000
#define PPS_OFFSET             0x010C0000
#define SIGNAL0_OFFSET         0x010D0000
#define SIGNAL1_OFFSET         0x010E0000
#define SIGNAL2_OFFSET         0x010F0000
#define SIGNAL3_OFFSET         0x01100000
#define TS3_OFFSET             0x01110000
#define TS4_OFFSET             0x01120000
#define FREQ0_OFFSET           0x01200000
#define FREQ1_OFFSET           0x01210000
#define FREQ2_OFFSET           0x01220000
#define FREQ3_OFFSET           0x01230000
#define MSIX_TABLE_OFFSET      0x00002000

/* Bit definitions from driver */
#define OCP_CTRL_ENABLE           BIT(0)
#define OCP_CTRL_ADJUST_TIME      BIT(1)
#define OCP_CTRL_ADJUST_OFFSET    BIT(2)
#define OCP_CTRL_ADJUST_DRIFT     BIT(3)
#define OCP_CTRL_ADJUST_SERVO     BIT(8)
#define OCP_CTRL_READ_TIME_REQ    BIT(30)
#define OCP_CTRL_READ_TIME_DONE   BIT(31)
#define OCP_STATUS_IN_SYNC        BIT(0)
#define OCP_STATUS_IN_HOLDOVER    BIT(1)

/* Struct definitions mirroring hardware register layouts */
struct ocp_reg {
    uint32_t ctrl;
    uint32_t status;
    uint32_t select;
    uint32_t version;
    uint32_t time_ns;
    uint32_t time_sec;
    uint32_t __pad0[2];
    uint32_t adjust_ns;
    uint32_t adjust_sec;
    uint32_t __pad1[2];
    uint32_t offset_ns;
    uint32_t offset_window_ns;
    uint32_t __pad2[2];
    uint32_t drift_ns;
    uint32_t drift_window_ns;
    uint32_t __pad3[6];
    uint32_t servo_offset_p;
    uint32_t servo_offset_i;
    uint32_t servo_drift_p;
    uint32_t servo_drift_i;
    uint32_t status_offset;
    uint32_t status_drift;
};

struct img_reg {
    uint32_t version;
};

struct gpio_reg {
    uint32_t gpio1;
    uint32_t __pad0;
    uint32_t gpio2;
    uint32_t __pad1;
};

struct ts_reg {
    uint32_t enable;
    uint32_t intr_mask;
    uint32_t intr;
    uint32_t __pad;
    uint32_t time_sec;
    uint32_t time_ns;
};

struct signal_reg {
    uint32_t start_sec;
    uint32_t start_ns;
    uint32_t period_sec;
    uint32_t period_ns;
    uint32_t pulse_sec;
    uint32_t pulse_ns;
    uint32_t polarity;
    uint32_t repeat_count;
    uint32_t intr;
    uint32_t intr_mask;
    uint32_t enable;
    uint32_t status;
};

struct irig_in_reg {
    uint32_t ctrl;
    uint32_t status;
};

struct irig_out_reg {
    uint32_t ctrl;
    uint32_t status;
    uint32_t adj_sec;
};

struct dcf_in_reg {
    uint32_t ctrl;
    uint32_t status;
};

struct dcf_out_reg {
    uint32_t ctrl;
    uint32_t status;
    uint32_t adj_sec;
};

struct nmea_out_reg {
    uint32_t ctrl;
    uint32_t uart_baud;
    uint32_t status;
};

struct frequency_reg {
    uint32_t ctrl;
    uint32_t status;
};

struct tod_reg {
    uint32_t ctrl;
    uint32_t version;
    uint32_t status;
    uint32_t adj_sec;
    uint32_t utc_status;
    uint32_t leap;
};

struct pps_to_ext_reg {
    uint32_t status;
};

struct pps_to_clk_reg {
    uint32_t status;
};

/* BAR type enum */
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

    /* Hardware Register Shadows */
    struct ocp_reg reg;
    struct img_reg image;
    struct gpio_reg sma_map1;
    struct gpio_reg sma_map2;
    struct gpio_reg pps_select;
    struct ts_reg ts0, ts1, ts2, ts3, ts4, pps;
    struct signal_reg signal[4];
    struct frequency_reg freq[4];
    struct irig_in_reg irig_in;
    struct irig_out_reg irig_out;
    struct dcf_in_reg dcf_in;
    struct dcf_out_reg dcf_out;
    struct nmea_out_reg nmea_out;
    struct tod_reg tod;
    struct pps_to_ext_reg pps_to_ext;
    struct pps_to_clk_reg pps_to_clk;

    /* Region descriptor table for MMIO dispatch */
    struct {
        hwaddr offset;
        uint32_t size;
        void *data;
    } regions[32];
    int num_regions;
};

/* Internal helper for region management */
static void pcibase_add_region(PCIBaseState *s, hwaddr offset, void *data, size_t size)
{
    int i = s->num_regions++;
    s->regions[i].offset = offset;
    s->regions[i].size = (uint32_t)size;
    s->regions[i].data = data;
}

/* MMIO Read Handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    for (int i = 0; i < s->num_regions; i++) {
        hwaddr off = addr - s->regions[i].offset;
        if (off < s->regions[i].size) {
            uint32_t *p = (uint32_t *)s->regions[i].data;
            if (size == 4 && (off & 3) == 0) {
                return p[off / 4];
            } else {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "Unaligned or wrong size MMIO read: addr=0x%" HWADDR_PRIx " size=%u\n",
                              addr, size);
                return 0;
            }
        }
    }
    qemu_log_mask(LOG_GUEST_ERROR, "MMIO read outside any region: 0x%" HWADDR_PRIx "\n", addr);
    return 0;
}

/* MMIO Write Handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    for (int i = 0; i < s->num_regions; i++) {
        hwaddr off = addr - s->regions[i].offset;
        if (off < s->regions[i].size) {
            uint32_t *p = (uint32_t *)s->regions[i].data;
            if (size == 4 && (off & 3) == 0) {
                uint32_t idx = off / 4;
                p[idx] = (uint32_t)val;
                /* side effects: ctrl register in main reg block */
                if (s->regions[i].data == &s->reg && off == offsetof(struct ocp_reg, ctrl)) {
                    if (val & OCP_CTRL_READ_TIME_REQ) {
                        s->reg.ctrl |= OCP_CTRL_READ_TIME_DONE;
                    }
                    if (val & OCP_CTRL_ADJUST_TIME) {
                        s->reg.time_ns = s->reg.adjust_ns;
                        s->reg.time_sec = s->reg.adjust_sec;
                    }
                }
                return;
            } else {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "Unaligned or wrong size MMIO write: addr=0x%" HWADDR_PRIx " size=%u val=0x%" PRIx64 "\n",
                              addr, size, val);
                return;
            }
        }
    }
    qemu_log_mask(LOG_GUEST_ERROR, "MMIO write outside any region: 0x%" HWADDR_PRIx "\n", addr);
}

/* PIO handlers (unused, device uses MMIO) */
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

    memset(&s->image, 0, sizeof(s->image));
    memset(&s->reg, 0, sizeof(s->reg));
    memset(&s->sma_map1, 0, sizeof(s->sma_map1));
    memset(&s->sma_map2, 0xff, sizeof(s->sma_map2)); /* fixed directions */
    memset(&s->pps_select, 0, sizeof(s->pps_select));
    memset(&s->ts0, 0, sizeof(s->ts0));
    memset(&s->ts1, 0, sizeof(s->ts1));
    memset(&s->ts2, 0, sizeof(s->ts2));
    memset(&s->ts3, 0, sizeof(s->ts3));
    memset(&s->ts4, 0, sizeof(s->ts4));
    memset(&s->pps, 0, sizeof(s->pps));
    memset(s->signal, 0, sizeof(s->signal));
    memset(s->freq, 0, sizeof(s->freq));
    memset(&s->irig_in, 0, sizeof(s->irig_in));
    memset(&s->irig_out, 0, sizeof(s->irig_out));
    memset(&s->dcf_in, 0, sizeof(s->dcf_in));
    memset(&s->dcf_out, 0, sizeof(s->dcf_out));
    memset(&s->nmea_out, 0, sizeof(s->nmea_out));
    memset(&s->tod, 0, sizeof(s->tod));
    memset(&s->pps_to_ext, 0, sizeof(s->pps_to_ext));
    memset(&s->pps_to_clk, 0, sizeof(s->pps_to_clk));

    /* Set initial values for probe success */
    s->image.version = 0x00130001; /* version 19.1 */
    s->reg.version = 0x00020000;
    s->reg.status = OCP_STATUS_IN_SYNC;
    s->sma_map2.gpio2 = 0xFFFFFFFF;
    s->reg.time_ns = 0;
    s->reg.time_sec = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    #define BAR0_SIZE (32 * 1024 * 1024) /* 32 MB, covers all resource offsets */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) { .index = 0, .type = BAR_TYPE_MMIO, .size = BAR0_SIZE, .name = "bar0" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Populate MMIO dispatch regions */
    s->num_regions = 0;
    pcibase_add_region(s, IMAGE_OFFSET, &s->image, sizeof(s->image));
    pcibase_add_region(s, PPS_SELECT_OFFSET, &s->pps_select, sizeof(s->pps_select));
    pcibase_add_region(s, SMA_MAP1_OFFSET, &s->sma_map1, sizeof(s->sma_map1));
    pcibase_add_region(s, SMA_MAP2_OFFSET, &s->sma_map2, sizeof(s->sma_map2));
    pcibase_add_region(s, REG_OFFSET, &s->reg, sizeof(s->reg));
    pcibase_add_region(s, TS0_OFFSET, &s->ts0, sizeof(s->ts0));
    pcibase_add_region(s, TS1_OFFSET, &s->ts1, sizeof(s->ts1));
    pcibase_add_region(s, PPS_TO_EXT_OFFSET, &s->pps_to_ext, sizeof(s->pps_to_ext));
    pcibase_add_region(s, PPS_TO_CLK_OFFSET, &s->pps_to_clk, sizeof(s->pps_to_clk));
    pcibase_add_region(s, TOD_OFFSET, &s->tod, sizeof(s->tod));
    pcibase_add_region(s, TS2_OFFSET, &s->ts2, sizeof(s->ts2));
    pcibase_add_region(s, IRIG_IN_OFFSET, &s->irig_in, sizeof(s->irig_in));
    pcibase_add_region(s, IRIG_OUT_OFFSET, &s->irig_out, sizeof(s->irig_out));
    pcibase_add_region(s, DCF_IN_OFFSET, &s->dcf_in, sizeof(s->dcf_in));
    pcibase_add_region(s, DCF_OUT_OFFSET, &s->dcf_out, sizeof(s->dcf_out));
    pcibase_add_region(s, NMEA_OUT_OFFSET, &s->nmea_out, sizeof(s->nmea_out));
    pcibase_add_region(s, PPS_OFFSET, &s->pps, sizeof(s->pps));
    pcibase_add_region(s, SIGNAL0_OFFSET, &s->signal[0], sizeof(struct signal_reg));
    pcibase_add_region(s, SIGNAL1_OFFSET, &s->signal[1], sizeof(struct signal_reg));
    pcibase_add_region(s, SIGNAL2_OFFSET, &s->signal[2], sizeof(struct signal_reg));
    pcibase_add_region(s, SIGNAL3_OFFSET, &s->signal[3], sizeof(struct signal_reg));
    pcibase_add_region(s, TS3_OFFSET, &s->ts3, sizeof(s->ts3));
    pcibase_add_region(s, TS4_OFFSET, &s->ts4, sizeof(s->ts4));
    pcibase_add_region(s, FREQ0_OFFSET, &s->freq[0], sizeof(struct frequency_reg));
    pcibase_add_region(s, FREQ1_OFFSET, &s->freq[1], sizeof(struct frequency_reg));
    pcibase_add_region(s, FREQ2_OFFSET, &s->freq[2], sizeof(struct frequency_reg));
    pcibase_add_region(s, FREQ3_OFFSET, &s->freq[3], sizeof(struct frequency_reg));

    /* Initialize all registers to reset state */
    pcibase_reset(DEVICE(s));

    /* Interrupt configuration: support both MSI and MSI-X */
    s->has_msi = true;
    s->has_msix = true;
    if (msix_init(pdev, 17,
                  &s->bar_regions[0], 0, MSIX_TABLE_OFFSET,
                  &s->bar_regions[0], 0, MSIX_TABLE_OFFSET + 0x1000,
                  0, errp)) {
        error_setg(errp, "Failed to initialize MSI-X");
        return;
    }

    /* No DMA or timers needed */
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
    .name = "ptp_ocp_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(reg.ctrl, PCIBaseState),
        VMSTATE_UINT32(reg.status, PCIBaseState),
        VMSTATE_UINT32(image.version, PCIBaseState),
        VMSTATE_UINT32(intr_status, PCIBaseState),
        VMSTATE_UINT32(intr_mask, PCIBaseState),
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
