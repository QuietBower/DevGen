/*
 * QEMU PCI device model for k10temp-compatible PCI device
 * Generated for driver: drivers/hwmon/k10temp.c
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

/* Removed Linux driver-specific include that is not available in QEMU build environment */
/* #include "hwmon/k10temp.h" */

#define TYPE_PCIBASE_DEVICE "k10temp_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define K10TEMP_VENDOR_ID 0x1022
#define K10TEMP_DEVICE_ID 0x1203
#define K10TEMP_CLASS_ID  0xff

#define DDR3_MODE                          (1u << 8)
#define PCI_DEVICE_ID_AMD_15H_M70H_NB_F3   0x15b3
#define CPUID_PKGTYPE_MASK                 0xF0000000u
#define CPUID_PKGTYPE_F                    0x00000000u
#define CPUID_PKGTYPE_AM2R2_AM3            0x10000000u
#define REG_DCT0_CONFIG_HIGH               0x094
#define REG_HARDWARE_THERMAL_CONTROL       0x64
#define HTC_ENABLE                         (1u << 0)
#define REG_REPORTED_TEMPERATURE           0xa4
#define REG_NORTHBRIDGE_CAPABILITIES       0xe8
#define NB_CAP_HTC                         (1u << 10)
#define F15H_M60H_HARDWARE_TEMP_CTRL_OFFSET 0xd8200c64
#define F15H_M60H_REPORTED_TEMP_CTRL_OFFSET 0xd8200ca4
#define ZEN_REPORTED_TEMP_CTRL_BASE        0x00059800
#define ZEN_CCD_TEMP(offset, x)            (ZEN_REPORTED_TEMP_CTRL_BASE + (offset) + ((x) * 4))
#define ZEN_CCD_TEMP_VALID                 (1u << 11)
#define ZEN_CCD_TEMP_MASK                  0x7ffu
#define ZEN_CUR_TEMP_SHIFT                 21
#define ZEN_CUR_TEMP_RANGE_SEL_MASK        (1u << 19)
#define ZEN_CUR_TEMP_TJ_SEL_MASK           0x00030000u
#define AMD_I3255_STR                      "3255"
#define PCI_DEVICE_ID_AMD_17H_M90H_DF_F3   0x1663
#define PCI_DEVICE_ID_AMD_1AH_M50H_DF_F3   0x166d
#define PCI_DEVICE_ID_AMD_1AH_M90H_DF_F3   0x127b
#define TCTL_BIT                           0
#define TDIE_BIT                           1
#define TCCD_BIT(x)                        ((x) + 2)

#define HAVE_TEMP(d, channel)              ((d)->show_temp & (1u << (channel)))

typedef struct tctl_offset {
    uint8_t model;
    const char *id;
    int offset;
} tctl_offset;

/* Unused in QEMU model; kept for structural mirroring but not referenced to avoid warnings */
static const tctl_offset tctl_offset_table[] G_GNUC_UNUSED = {
    { 0x17, "AMD Ryzen 5 1600X", 20000 },
    { 0x17, "AMD Ryzen 7 1700X", 20000 },
    { 0x17, "AMD Ryzen 7 1800X", 20000 },
    { 0x17, "AMD Ryzen 7 2700X", 10000 },
    { 0x17, "AMD Ryzen Threadripper 19", 27000 },
    { 0x17, "AMD Ryzen Threadripper 29", 27000 },
};

/* Unused in QEMU model; kept for structural mirroring but not referenced to avoid warnings */
static const char *k10temp_temp_label[] G_GNUC_UNUSED = {
    "Tctl",
    "Tdie",
    "Tccd1",
    "Tccd2",
    "Tccd3",
    "Tccd4",
    "Tccd5",
    "Tccd6",
    "Tccd7",
    "Tccd8",
    "Tccd9",
    "Tccd10",
    "Tccd11",
    "Tccd12",
};

/* Linux hwmon structures are not used by QEMU device model at compile time; provide minimal stubs */
struct hwmon_channel_info { int dummy; };
struct hwmon_ops { int dummy; };
struct hwmon_chip_info {
    const struct hwmon_ops *ops;
    const struct hwmon_channel_info *const *info;
};

static const struct hwmon_channel_info * const k10temp_info[] = {
    NULL
};

/* Linux pci_device_id and related macros/types are not required for QEMU model;
 * they are kept only to mirror driver source but must be stubbed to compile. */
struct pci_device_id {
    uint32_t dummy;
};

#define PCI_VDEVICE(vendor, device)  { 0 }

#define PCI_DEVICE_ID_AMD_10H_NB_MISC  0x1203
#define PCI_DEVICE_ID_AMD_11H_NB_MISC  0x1303
#define PCI_DEVICE_ID_AMD_CNB17H_F3    0x1703
#define PCI_DEVICE_ID_AMD_15H_NB_F3    0x1603
#define PCI_DEVICE_ID_AMD_15H_M10H_F3  0x1403
#define PCI_DEVICE_ID_AMD_15H_M30H_NB_F3 0x141d
#define PCI_DEVICE_ID_AMD_15H_M60H_NB_F3 0x1573
#define PCI_DEVICE_ID_AMD_15H_M70H_NB_F3 0x15b3
#define PCI_DEVICE_ID_AMD_16H_NB_F3    0x1533
#define PCI_DEVICE_ID_AMD_16H_M30H_NB_F3 0x1583
#define PCI_DEVICE_ID_AMD_17H_DF_F3    0x1463
#define PCI_DEVICE_ID_AMD_17H_M10H_DF_F3 0x15eb
#define PCI_DEVICE_ID_AMD_17H_M30H_DF_F3 0x1493
#define PCI_DEVICE_ID_AMD_17H_M40H_DF_F3 0x13f3
#define PCI_DEVICE_ID_AMD_17H_M60H_DF_F3 0x144b
#define PCI_DEVICE_ID_AMD_17H_M70H_DF_F3 0x1443
#define PCI_DEVICE_ID_AMD_17H_MA0H_DF_F3 0x1727
#define PCI_DEVICE_ID_AMD_19H_DF_F3    0x1653
#define PCI_DEVICE_ID_AMD_19H_M10H_DF_F3 0x14b0
#define PCI_DEVICE_ID_AMD_19H_M40H_DF_F3 0x167c
#define PCI_DEVICE_ID_AMD_19H_M50H_DF_F3 0x166d
#define PCI_DEVICE_ID_AMD_19H_M60H_DF_F3 0x14e3
#define PCI_DEVICE_ID_AMD_19H_M70H_DF_F3 0x14f3
#define PCI_DEVICE_ID_AMD_19H_M78H_DF_F3 0x12fb
#define PCI_DEVICE_ID_AMD_1AH_M00H_DF_F3 0x12c3
#define PCI_DEVICE_ID_AMD_1AH_M20H_DF_F3 0x16fb
#define PCI_DEVICE_ID_AMD_1AH_M60H_DF_F3 0x124b
#define PCI_DEVICE_ID_AMD_1AH_M70H_DF_F3 0x12bb

static const struct pci_device_id k10temp_id_table[] = {
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_10H_NB_MISC),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_11H_NB_MISC),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_CNB17H_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_15H_NB_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_15H_M10H_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_15H_M30H_NB_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_15H_M60H_NB_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_15H_M70H_NB_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_16H_NB_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_16H_M30H_NB_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_17H_DF_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_17H_M10H_DF_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_17H_M30H_DF_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_17H_M40H_DF_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_17H_M60H_DF_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_17H_M70H_DF_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_17H_M90H_DF_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_17H_MA0H_DF_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_19H_DF_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_19H_M10H_DF_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_19H_M40H_DF_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_19H_M50H_DF_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_19H_M60H_DF_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_19H_M70H_DF_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_19H_M78H_DF_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_1AH_M00H_DF_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_1AH_M20H_DF_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_1AH_M50H_DF_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_1AH_M60H_DF_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_1AH_M70H_DF_F3),
    PCI_VDEVICE(AMD, PCI_DEVICE_ID_AMD_1AH_M90H_DF_F3),
    { 0 },
};

struct pci_dev { int dummy; };
struct device { int dummy; };

enum hwmon_sensor_types { HWMON_SENSOR_DUMMY };
typedef unsigned int umode_t;

struct k10temp_data {
    uint32_t show_temp;
};

static const struct hwmon_ops k10temp_hwmon_ops;
static const struct hwmon_chip_info k10temp_chip_info;
struct pci_driver {
    const char *name;
    const struct pci_device_id *id_table;
    int (*probe)(struct pci_dev *pdev, const struct pci_device_id *id);
};
static struct pci_driver k10temp_driver;

static void read_htcreg_pci(struct pci_dev *pdev, uint32_t *regval) G_GNUC_UNUSED; 
static void read_htcreg_pci(struct pci_dev *pdev, uint32_t *regval) { (void)pdev; (void)regval; }
static void read_tempreg_pci(struct pci_dev *pdev, uint32_t *regval) G_GNUC_UNUSED; 
static void read_tempreg_pci(struct pci_dev *pdev, uint32_t *regval) { (void)pdev; (void)regval; }
static void amd_nb_index_read(struct pci_dev *pdev, unsigned int devfn,
                              unsigned int base, int offset, uint32_t *val) G_GNUC_UNUSED;
static void amd_nb_index_read(struct pci_dev *pdev, unsigned int devfn,
                              unsigned int base, int offset, uint32_t *val)
{
    (void)pdev; (void)devfn; (void)base; (void)offset; (void)val;
}
static void read_htcreg_nb_f15(struct pci_dev *pdev, uint32_t *regval) G_GNUC_UNUSED; 
static void read_htcreg_nb_f15(struct pci_dev *pdev, uint32_t *regval) { (void)pdev; (void)regval; }
static void read_tempreg_nb_f15(struct pci_dev *pdev, uint32_t *regval) G_GNUC_UNUSED; 
static void read_tempreg_nb_f15(struct pci_dev *pdev, uint32_t *regval) { (void)pdev; (void)regval; }
static uint16_t amd_pci_dev_to_node_id(struct pci_dev *pdev) G_GNUC_UNUSED; 
static uint16_t amd_pci_dev_to_node_id(struct pci_dev *pdev) { (void)pdev; return 0; }
static void read_tempreg_nb_zen(struct pci_dev *pdev, uint32_t *regval) G_GNUC_UNUSED; 
static void read_tempreg_nb_zen(struct pci_dev *pdev, uint32_t *regval) { (void)pdev; (void)regval; }
static int read_ccd_temp_reg(struct k10temp_data *data, int ccd, uint32_t *regval) G_GNUC_UNUSED;
static int read_ccd_temp_reg(struct k10temp_data *data, int ccd, uint32_t *regval)
{
    (void)data; (void)ccd; (void)regval; return 0;
}
static long get_raw_temp(struct k10temp_data *data) G_GNUC_UNUSED; 
static long get_raw_temp(struct k10temp_data *data) { (void)data; return 0; }
static int k10temp_read_labels(struct device *dev,
                               enum hwmon_sensor_types type,
                               uint32_t attr, int channel, const char **str) G_GNUC_UNUSED;
static int k10temp_read_labels(struct device *dev,
                               enum hwmon_sensor_types type,
                               uint32_t attr, int channel, const char **str)
{
    (void)dev; (void)type; (void)attr; (void)channel; (void)str; return 0;
}
static int k10temp_read(struct device *dev, enum hwmon_sensor_types type,
                        uint32_t attr, int channel, long *val) G_GNUC_UNUSED;
static int k10temp_read(struct device *dev, enum hwmon_sensor_types type,
                        uint32_t attr, int channel, long *val)
{
    (void)dev; (void)type; (void)attr; (void)channel; (void)val; return 0;
}
static umode_t k10temp_is_visible(const void *drvdata,
                                  enum hwmon_sensor_types type,
                                  uint32_t attr, int channel) G_GNUC_UNUSED;
static umode_t k10temp_is_visible(const void *drvdata,
                                  enum hwmon_sensor_types type,
                                  uint32_t attr, int channel)
{
    (void)drvdata; (void)type; (void)attr; (void)channel; return 0;
}
static bool has_erratum_319(struct pci_dev *pdev) G_GNUC_UNUSED; 
static bool has_erratum_319(struct pci_dev *pdev) { (void)pdev; return false; }
static void k10temp_get_ccd_support(struct k10temp_data *data, int limit) G_GNUC_UNUSED;
static void k10temp_get_ccd_support(struct k10temp_data *data, int limit)
{
    (void)data; (void)limit;
}
static int k10temp_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    (void)pdev; (void)id; return 0;
}

static const struct hwmon_ops k10temp_hwmon_ops = {
};

static const struct hwmon_chip_info k10temp_chip_info G_GNUC_UNUSED = {
    .ops = &k10temp_hwmon_ops,
    .info = k10temp_info,
};

static struct pci_driver k10temp_driver G_GNUC_UNUSED = {
    .name = "k10temp",
    .id_table = k10temp_id_table,
    .probe = k10temp_probe,
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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t cfg_hardware_thermal_control;     /* REG_HARDWARE_THERMAL_CONTROL (0x64) */
    uint32_t cfg_reported_temperature;         /* REG_REPORTED_TEMPERATURE (0xa4) */
    uint32_t cfg_northbridge_capabilities;     /* REG_NORTHBRIDGE_CAPABILITIES (0xe8) */

    /* Indexed NB registers used via amd_nb_index_read base 0xb8 */
    uint32_t nb_index;                         /* base 0xb8 */
    uint32_t nb_data;                          /* base 0xbc */

    /* DDR3 config register used by has_erratum_319 */
    uint32_t dram_cfg_high;                    /* REG_DCT0_CONFIG_HIGH (0x94) on devfn(slot,2) */

    /* Simple SMN temperature register backing for Zen paths */
    uint32_t smn_reported_temp;                /* ZEN_REPORTED_TEMP_CTRL_BASE */

    /* CCD temperature registers (12 CCDs max in driver) */
    uint32_t ccd_temp[12];
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)pdev;
    /* k10temp driver does not use interrupts; leave unimplemented */
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)pdev;
    (void)s;
    (void)is_write;
    /* k10temp driver does not use DMA; no implementation */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* k10temp.c does not access BAR MMIO/PIO; only PCI config space and SMN */
    (void)s;
    (void)addr;
    (void)size;

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
    /* No BAR MMIO writes used by driver */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;

    return val;
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

    /* Initialize PCI config shadow registers to sane defaults used by k10temp */
    s->cfg_hardware_thermal_control = HTC_ENABLE | (70u << 16); /* HTC enabled, crit temp ~70C */
    /* Zen-style temperature encoding: value in bits [31:21] multiplied by 125 in millidegrees */
    s->cfg_reported_temperature = ((uint32_t)(300000 / 125) << ZEN_CUR_TEMP_SHIFT);
    s->cfg_northbridge_capabilities = NB_CAP_HTC; /* HTC capability present */

    s->nb_index = 0;
    s->nb_data = 0;
    s->dram_cfg_high = DDR3_MODE; /* Pretend DDR3 present to avoid erratum 319 path */

    s->smn_reported_temp = s->cfg_reported_temperature;

    for (int i = 0; i < 12; i++) {
        /* Valid CCD temps, about 55C: ((55000+49000)/125) & mask */
        uint32_t t = ((55000 + 49000) / 125) & ZEN_CCD_TEMP_MASK;
        s->ccd_temp[i] = ZEN_CCD_TEMP_VALID | t;
    }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  K10TEMP_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  K10TEMP_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, K10TEMP_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize internal register shadows before use */
    s->cfg_hardware_thermal_control = HTC_ENABLE | (70u << 16);
    s->cfg_reported_temperature = ((uint32_t)(300000 / 125) << ZEN_CUR_TEMP_SHIFT);
    s->cfg_northbridge_capabilities = NB_CAP_HTC;
    s->nb_index = 0;
    s->nb_data = 0;
    s->dram_cfg_high = DDR3_MODE;
    s->smn_reported_temp = s->cfg_reported_temperature;
    for (int i = 0; i < 12; i++) {
        uint32_t t = ((55000 + 49000) / 125) & ZEN_CCD_TEMP_MASK;
        s->ccd_temp[i] = ZEN_CCD_TEMP_VALID | t;
    }

    /* BAR Initialization: driver uses only PCI config, so we expose no BARs */
    s->num_bars = 0;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, 0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "k10temp_pci",
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
