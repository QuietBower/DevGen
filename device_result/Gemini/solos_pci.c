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


#define TYPE_PCIBASE_DEVICE "solos_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define SOLOS_VENDOR_ID 0x10ee
#define SOLOS_DEVICE_ID 0x0300

#define IRQ_CLEAR	0x70
#define FLAGS_ADDR	0x7C
#define IRQ_EN_ADDR	0x78
#define WRITE_FLASH	0x6C
#define FLASH_BLOCK	0x64
#define PORTS		0x68
#define FLASH_BUSY	0x60
#define FPGA_MODE	0x5C
#define FLASH_MODE	0x58
#define GPIO_STATUS	0x54
#define DRIVER_VER	0x50
#define TX_DMA_ADDR(port)	(0x40 + (4 * (port)))
#define RX_DMA_ADDR(port)	(0x30 + (4 * (port)))

/* Newly added from supplementary source */
#define CONFIG_RAM_SIZE	128
#define DATA_RAM_SIZE	32768
#define FPGA_VER	0x74
#define FPGA_VERSION(a,b) (((a) << 8) + (b))
#define LEGACY_BUFFERS	2
#define BUF_SIZE	2048
#define OLD_BUF_SIZE	4096
#define DMA_SUPPORTED	4

#define DRIVER_VERSION 0x01
#define RX_DMA_SIZE	2048
#define PKT_STATUS	5
#define PKT_COMMAND	1
#define PKT_DATA	0
#define PKT_POPEN	3
#define PKT_PCLOSE	4

#define ATMEL_FPGA_PAGE	528
#define ATMEL_SOLOS_PAGE	512

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
    uint32_t irq_en;
    uint32_t irq_clear;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t flags;
    uint32_t ports;
    uint32_t flash_busy;
    uint32_t flash_block;
    uint32_t fpga_mode;
    uint32_t flash_mode;
    uint32_t gpio_status;
    uint32_t driver_ver;
    uint32_t fpga_ver;

    /* DMA Context */
    uint32_t tx_dma_addr[4];
    uint32_t rx_dma_addr[4];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->irq_en && (s->flags & 0xF0)) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (is_write) {
        for (int i = 0; i < 4; i++) {
            if ((s->flags & (1 << i)) && s->tx_dma_addr[i]) {
                uint8_t dummy_buf[RX_DMA_SIZE];
                pci_dma_read(pdev, s->tx_dma_addr[i], dummy_buf, sizeof(dummy_buf));
            }
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case FPGA_VER:
        val = 0x01000027; /* Major 1, Minor 0, Ver 39 */
        break;
    case PORTS:
        val = 4;
        break;
    case IRQ_EN_ADDR:
        val = s->irq_en;
        break;
    case IRQ_CLEAR:
        val = s->irq_clear;
        break;
    case FLAGS_ADDR:
        val = s->flags;
        break;
    case FPGA_MODE:
        val = s->fpga_mode;
        break;
    case FLASH_MODE:
        val = s->flash_mode;
        break;
    case FLASH_BUSY:
        val = s->flash_busy;
        break;
    case FLASH_BLOCK:
        val = s->flash_block;
        break;
    case GPIO_STATUS:
        val = s->gpio_status;
        break;
    case DRIVER_VER:
        val = s->driver_ver;
        break;
    default:
        if (addr >= 0x30 && addr <= 0x3C) {
            int port = (addr - 0x30) / 4;
            val = s->rx_dma_addr[port];
        } else if (addr >= 0x40 && addr <= 0x4C) {
            int port = (addr - 0x40) / 4;
            val = s->tx_dma_addr[port];
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case IRQ_EN_ADDR:
        s->irq_en = val;
        pcibase_update_irq(s);
        break;
    case IRQ_CLEAR:
        s->irq_clear = val;
        if (val == 0) {
            pci_set_irq(PCI_DEVICE(s), 0);
        }
        break;
    case FLAGS_ADDR:
        s->flags = val;
        if (val & 0x0F) {
            pcibase_do_dma(s, true);
            s->flags &= ~0x0F;
            pcibase_update_irq(s);
        }
        break;
    case FPGA_MODE:
        s->fpga_mode = val;
        break;
    case FLASH_MODE:
        s->flash_mode = val;
        break;
    case WRITE_FLASH:
        s->flash_busy = 0;
        break;
    case FLASH_BLOCK:
        s->flash_block = val;
        break;
    case GPIO_STATUS:
        s->gpio_status = val;
        break;
    case DRIVER_VER:
        s->driver_ver = val;
        break;
    default:
        if (addr >= 0x30 && addr <= 0x3C) {
            int port = (addr - 0x30) / 4;
            s->rx_dma_addr[port] = val;
        } else if (addr >= 0x40 && addr <= 0x4C) {
            int port = (addr - 0x40) / 4;
            s->tx_dma_addr[port] = val;
        }
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

    s->irq_en = 0;
    s->irq_clear = 0;
    s->flags = 0;
    s->ports = 4;
    s->flash_busy = 0;
    s->flash_block = 0;
    s->fpga_mode = 0;
    s->flash_mode = 0;
    s->gpio_status = 0;
    s->driver_ver = 0;
    s->fpga_ver = 0x01000027;

    for (int i = 0; i < 4; i++) {
        s->tx_dma_addr[i] = 0;
        s->rx_dma_addr[i] = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x10ee );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0300 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_OTHER );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = CONFIG_RAM_SIZE, .name = "config_regs" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_RAM, .size = DATA_RAM_SIZE, .name = "buffers" };
      
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "solos_pci",
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
