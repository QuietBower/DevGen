/*
 * QEMU PCI device model for AMD MP2 I2C (for i2c-amd-mp2-pci.c)
 *
 * Phase 4: minimal fix for BAR size power-of-two assertion.
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"

#define TYPE_PCIBASE_DEVICE "i2c_amd_mp2_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_DEVICE_ID_AMD_MP2 0x15E6

/* register offsets used by driver */
#define AMD_C2P_MSG0        0x10500
#define AMD_C2P_MSG1        0x10504
#define AMD_C2P_MSG2        0x10508
#define AMD_P2C_MSG1        0x10684
#define AMD_P2C_MSG2        0x10688
#define AMD_P2C_MSG_INTEN   0x10680

enum status_type {
    i2c_readcomplete_event = 0,
    i2c_readfail_event = 1,
    i2c_writecomplete_event = 2,
    i2c_writefail_event = 3,
    i2c_busenable_complete = 4,
    i2c_busenable_failed = 5,
    i2c_busdisable_complete = 6,
    i2c_busdisable_failed = 7,
    invalid_data_length = 8,
    invalid_slave_address = 9,
    invalid_i2cbus_id = 10,
    invalid_dram_addr = 11,
    invalid_command = 12,
    mp2_active = 13,
    numberof_sensors_discovered_resp = 14,
    i2c_bus_notinitialized
};

enum response_type {
    invalid_response = 0,
    command_success = 1,
    command_failed = 2,
};

enum speed_enum {
    speed100k = 0,
    speed400k = 1,
    speed1000k = 2,
    speed1400k = 3,
    speed3400k = 4
};

enum mem_type {
    use_dram = 0,
    use_c2pmsg = 1,
};

enum i2c_cmd {
    i2c_read = 0,
    i2c_write,
    i2c_enable,
    i2c_disable,
    number_of_sensor_discovered,
    is_mp2_active,
    invalid_cmd = 0xF,
};

union i2c_cmd_base {
    uint32_t ul;
    struct {
        enum i2c_cmd i2c_cmd : 4;
        uint8_t bus_id : 4;
        uint32_t slave_addr : 8;
        uint32_t length : 12;
        enum speed_enum i2c_speed : 3;
        enum mem_type mem_type : 1;
    } s;
};

union i2c_event {
    uint32_t ul;
    struct {
        enum response_type response : 2;
        enum status_type status : 5;
        enum mem_type mem_type : 1;
        uint8_t bus_id : 4;
        uint32_t length : 12;
        uint32_t slave_addr : 8;
    } r;
};

struct amd_i2c_common {
    union i2c_event eventval;
    void *mp2_dev;              /* struct amd_mp2_dev * in driver */
    void *msg;                  /* struct i2c_msg * in driver */
    void (*cmd_completion)(struct amd_i2c_common *i2c_common);
    enum i2c_cmd reqcmd;
    uint8_t cmd_success;
    uint8_t bus_id;
    enum speed_enum i2c_speed;
    uint8_t *dma_buf;
    uint64_t dma_addr;          /* dma_addr_t in driver */
};

struct amd_mp2_dev_shadow {
    void *pci_dev;              /* struct pci_dev * in driver */
    struct amd_i2c_common *busses[2];
    void *mmio;                 /* void __iomem * in driver */
    uint8_t c2p_lock_busid;
    unsigned int probed;
    int dev_irq;
};

/* BAR metadata */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

/* Internal minimal emulation layout (not exposed to driver):
 * We model a single MMIO BAR (BAR2) with space sufficient for message
 * registers used in the driver.
 */

/* We allocate 0x11000 bytes for BAR2 to cover MMIO offsets used */
#define AMD_MP2_MMIO_BAR_INDEX 2
#define AMD_MP2_MMIO_BAR_SIZE  0x11000

/* Internal aliases mapping the driver's MMIO offsets into our BAR.
 * The driver uses AMD_C2P_MSG0/1/2, AMD_P2C_MSG1 and AMD_P2C_MSG_INTEN,
 * and also accesses AMD_C2P_MSG2 as a data window for up to 32 bytes.
 * We back these directly at their specified offsets.
 */
#define OFFS_C2P_MSG0      AMD_C2P_MSG0
#define OFFS_C2P_MSG1      AMD_C2P_MSG1
#define OFFS_C2P_MSG2      AMD_C2P_MSG2
#define OFFS_P2C_MSG1      AMD_P2C_MSG1
#define OFFS_P2C_MSG2      AMD_P2C_MSG2
#define OFFS_P2C_MSG_INTEN AMD_P2C_MSG_INTEN

#define OFFS_C2P_DATA      AMD_C2P_MSG2
#define C2P_DATA_SIZE      32

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* simple register backing for message and inten */
    uint32_t c2p_msg[10];
    uint32_t p2c_msg[3]; /* index 0 unused, 1 and 2 correspond to P2C_MSG1/2 */
    uint32_t p2c_msg_inten;

    uint8_t c2p_data[C2P_DATA_SIZE];
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

/* MMIO handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    uint32_t val = 0;

    switch (addr) {
    case OFFS_C2P_MSG0:
        val = s->c2p_msg[0];
        break;
    case OFFS_C2P_MSG1:
        val = s->c2p_msg[1];
        break;
    case OFFS_C2P_MSG2:
        val = s->c2p_msg[2];
        break;
    case OFFS_P2C_MSG1:
        val = s->p2c_msg[1];
        break;
    case OFFS_P2C_MSG2:
        val = s->p2c_msg[2];
        break;
    case OFFS_P2C_MSG_INTEN:
        val = s->p2c_msg_inten;
        break;
    default:
        /* C2P data window read for up to 32 bytes */
        if (addr >= OFFS_C2P_DATA && addr < OFFS_C2P_DATA + C2P_DATA_SIZE) {
            unsigned offset = addr - OFFS_C2P_DATA;
            if (offset + size <= C2P_DATA_SIZE) {
                memcpy(&val, s->c2p_data + offset, size);
            }
        } else {
            qemu_log_mask(LOG_UNIMP, "[%s] mmio_read addr=%" PRIx64 " size=%u\n",
                          TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
        }
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case OFFS_C2P_MSG0:
        s->c2p_msg[0] = (uint32_t)val;
        break;
    case OFFS_C2P_MSG1:
        s->c2p_msg[1] = (uint32_t)val;
        break;
    case OFFS_C2P_MSG2:
        s->c2p_msg[2] = (uint32_t)val;
        break;
    case OFFS_P2C_MSG1:
        s->p2c_msg[1] = (uint32_t)val;
        break;
    case OFFS_P2C_MSG2:
        s->p2c_msg[2] = (uint32_t)val;
        break;
    case OFFS_P2C_MSG_INTEN:
        s->p2c_msg_inten = (uint32_t)val;
        break;
    default:
        /* C2P data window write for up to 32 bytes (memcpy_toio usage) */
        if (addr >= OFFS_C2P_DATA && addr < OFFS_C2P_DATA + C2P_DATA_SIZE) {
            unsigned offset = addr - OFFS_C2P_DATA;
            if (offset + size <= C2P_DATA_SIZE) {
                memcpy(s->c2p_data + offset, &val, size);
            }
        } else {
            qemu_log_mask(LOG_UNIMP, "[%s] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n",
                          TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_read addr=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, (uint64_t)val, size);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    memset(s->c2p_msg, 0, sizeof(s->c2p_msg));
    memset(s->p2c_msg, 0, sizeof(s->p2c_msg));
    s->p2c_msg_inten = 0;
    memset(s->c2p_data, 0, sizeof(s->c2p_data));

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }
}

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    (void)errp;
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
    case 1:
        val &= 0xFF;
        break;
    case 2:
        val &= 0xFFFF;
        break;
    case 4:
    default:
        break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x1022);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_AMD_MP2);
    /* Use generic serial bus class as PCI_CLASS_SERIAL_BUS_I2C is not available */
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SERIAL_SMBUS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Configure BAR2 as MMIO per driver pcim_iomap_regions(pci_dev, 1 << 2, ...) */
    s->num_bars = 1;
    s->bar_info[0].index = AMD_MP2_MMIO_BAR_INDEX;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    /* Ensure BAR size is a power of two to satisfy pci_register_bar() */
    s->bar_info[0].size = pow2ceil(AMD_MP2_MMIO_BAR_SIZE);
    s->bar_info[0].name = "amd_mp2-mmio";
    s->bar_info[0].sparse = false;

    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device realized\n", TYPE_PCIBASE_DEVICE);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit = pcibase_uninit;
    dc->reset = pcibase_reset;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pcibase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
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
