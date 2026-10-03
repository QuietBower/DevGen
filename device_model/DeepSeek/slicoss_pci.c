/* QEMU 8.2.10 device model for Alacritech SLICOSS, based on provided driver snippets */
#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "qemu/module.h"
#include "hw/pci/pci_ids.h"

#define TYPE_PCI_SLICOSS_DEVICE "slicoss_pci"
typedef struct SlicossState SlicossState;
DECLARE_INSTANCE_CHECKER(SlicossState, SLICOSS, TYPE_PCI_SLICOSS_DEVICE)

/* Driver register defines */
#define SLIC_ICR_INT_MASK  2
#define SLIC_ICR_INT_OFF   0
#define SLIC_ICR_INT_ON    1
#define SLIC_UPR_CONFIG    1
#define SLIC_UPR_LSTAT     0
#define SLIC_ISR_UPC       0x10000000
#define SLIC_ISR_UPCERR    0x01000000
#define SLIC_ISR_UPCBSY    0x00020000
#define SLIC_ISR_UPCERR_MASK (SLIC_ISR_UPCERR | SLIC_ISR_UPCBSY)

/* EEPROM definitions */
#define SLIC_EEPROM_MAGIC 0xa5a5
#define SLIC_EEPROM_SIZE  128
typedef enum { SLIC_MODEL_MOJAVE = 0, SLIC_MODEL_OASIS = 1 } SlicossModel;

/* EEPROM structures, translated to QEMU-compatible types */
#pragma pack(push, 1)
struct slic_mojave_eeprom {
    uint16_t id;
    uint16_t eeprom_code_size;
    uint16_t flash_size;
    uint16_t eeprom_size;
    uint16_t vendor_id;
    uint16_t dev_id;
    uint8_t  rev_id;
    uint8_t  class_code[3];
    uint8_t  irqpin_dbg;
    uint8_t  irqpin;
    uint8_t  min_grant;
    uint8_t  max_lat;
    uint16_t pci_stat;
    uint16_t sub_vendor_id;
    uint16_t sub_id;
    uint16_t dev_id_dbg;
    uint16_t ramrom;
    uint16_t dram_size2pci;
    uint16_t rom_size2pci;
    uint8_t  pad[2];
    uint8_t  freetime;
    uint8_t  ifctrl;
    uint16_t dram_size;
    uint8_t  mac[6];
    uint8_t  mac2[6];
    uint8_t  pad2[6];
    uint16_t dev_id2;
    uint8_t  irqpin2;
    uint8_t  class_code2[3];
    uint16_t cfg_byte6;
    uint16_t pme_cap;
    uint16_t nwclk_ctrl;
    uint8_t  fru_format;
    uint8_t  fru_assembly[6];
    uint8_t  fru_rev[2];
    uint8_t  fru_serial[14];
    uint8_t  fru_pad[3];
    uint8_t  oem_fru[28];
    uint8_t  pad3[4];
};

struct slic_oasis_eeprom {
    uint16_t id;
    uint16_t eeprom_code_size;
    uint16_t spidev0_cfg;
    uint16_t spidev1_cfg;
    uint16_t vendor_id;
    uint16_t dev_id;
    uint8_t  rev_id;
    uint8_t  class_code0[3];
    uint8_t  irqpin1;
    uint8_t  class_code1[3];
    uint8_t  irqpin2;
    uint8_t  irqpin0;
    uint8_t  min_grant;
    uint8_t  max_lat;
    uint16_t sub_vendor_id;
    uint16_t sub_id;
    uint16_t flash_size;
    uint16_t dram_size2pci;
    uint16_t rom_size2pci;
    uint16_t dev_id1;
    uint16_t dev_id2;
    uint16_t dev_stat_cfg;
    uint16_t pme_cap;
    uint8_t  msi_cap;
    uint8_t  clock_div;
    uint16_t pci_stat_lo;
    uint16_t pci_stat_hi;
    uint16_t dram_cfg_lo;
    uint16_t dram_cfg_hi;
    uint16_t dram_size;
    uint16_t gpio_tbi_ctrl;
    uint16_t eeprom_size;
    uint8_t  mac[6];
    uint8_t  mac2[6];
    uint8_t  fru_format;
    uint8_t  fru_assembly[6];
    uint8_t  fru_rev[2];
    uint8_t  fru_serial[14];
    uint8_t  fru_pad[3];
    uint8_t  oem_fru[28];
    uint8_t  pad[4];
};
#pragma pack(pop)

struct SlicossState {
    PCIDevice pdev;
    MemoryRegion mmio;
    uint8_t eeprom[SLIC_EEPROM_SIZE];
    SlicossModel model;   /* Mojave or Oasis */
    /* Placeholder for further state as needed by driver */
};

static uint64_t slicoss_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Placeholder: to be filled when driver MMIO access patterns are known */
    return 0xFFFFFFFF;
}

static void slicoss_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Placeholder */
}

static const MemoryRegionOps slicoss_mmio_ops = {
    .read = slicoss_mmio_read,
    .write = slicoss_mmio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void slicoss_realize(PCIDevice *pdev, Error **errp)
{
    SlicossState *s = SLICOSS(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_config_set_interrupt_pin(pci_conf, 1);

    memory_region_init_io(&s->mmio, OBJECT(s), &slicoss_mmio_ops, s,
                          "slicoss-mmio", 4 * KiB);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
}

static void slicoss_exit(PCIDevice *pdev)
{
    /* Placeholder for cleanup if needed */
}

static void slicoss_instance_init(Object *obj)
{
    SlicossState *s = SLICOSS(obj);
    /* Initialize EEPROM with defaults; to be filled from driver usage */
    memset(s->eeprom, 0, sizeof(s->eeprom));
    s->model = SLIC_MODEL_MOJAVE;  /* Default, may be overridden */
}

static void slicoss_class_init(ObjectClass *class, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(class);

    k->realize = slicoss_realize;
    k->exit = slicoss_exit;
    /* Placeholder PCI IDs: need actual Vendor/Device from driver pci_device_id table */
    k->vendor_id = 0x1af4;   /* PCI_VENDOR_ID_QEMU as placeholder */
    k->device_id = 0x0001;   /* Placeholder */
    k->revision = 0x00;
    k->class_id = PCI_CLASS_NETWORK_ETHERNET;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static void pci_slicoss_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };
    static const TypeInfo slicoss_info = {
        .name          = TYPE_PCI_SLICOSS_DEVICE,
        .parent        = TYPE_PCI_DEVICE,
        .instance_size = sizeof(SlicossState),
        .instance_init = slicoss_instance_init,
        .class_init    = slicoss_class_init,
        .interfaces = interfaces,
    };
    type_register_static(&slicoss_info);
}
type_init(pci_slicoss_register_types)
