/*
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
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

#define TYPE_PCIBASE_DEVICE "solos_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_SOLOS     0x10ee
#define PCI_DEVICE_ID_SOLOS     0x0300
#define PCI_CLASS_SOLOS         0x028000  /* Network controller: Other */

/* Register offsets from config_regs base */
#define DRIVER_VER    0x50
#define GPIO_STATUS   0x54
#define FLASH_MODE    0x58
#define FPGA_MODE     0x5C
#define FLASH_BUSY    0x60
#define FLASH_BLOCK   0x64
#define PORTS         0x68
#define WRITE_FLASH   0x6C
#define IRQ_CLEAR     0x70
#define FPGA_VER      0x74
#define IRQ_EN_ADDR   0x78
#define FLAGS_ADDR    0x7C

#define TX_DMA_ADDR(port)  (0x40 + 4 * (port))
#define RX_DMA_ADDR(port)  (0x30 + 4 * (port))

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

typedef struct SolosConfigRegs {
    uint32_t driver_ver;
    uint32_t gpio_status;
    uint32_t flash_mode;
    uint32_t fpga_mode;
    uint32_t flash_busy;
    uint32_t flash_block;
    uint32_t ports;
    uint32_t write_flash;
    uint32_t irq_clear;
    uint32_t fpga_ver;
    uint32_t irq_en;
    uint32_t flags;
    uint32_t tx_dma_addr[4]; /* Offsets 0x40, 0x44, 0x48, 0x4C */
    uint32_t rx_dma_addr[4]; /* Offsets 0x30, 0x34, 0x38, 0x3C */
} SolosConfigRegs;

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
    bool irq_raised;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    SolosConfigRegs config_regs;

    /* DMA Context */
    uint32_t dma_status;
    dma_addr_t dma_addr;
    hwaddr dma_bounce_paddr;

    int fpga_version;
    int buffer_size;
    int nr_ports;
    int using_dma;

    bool reset_active;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t flags = s->config_regs.flags;
    bool level = (s->config_regs.irq_en != 0) && (flags & 0x000000F0);

    if (level) {
        if (!s->irq_raised) {
            pci_set_irq(pdev, 1);
            s->irq_raised = true;
        }
    } else {
        if (s->irq_raised) {
            pci_set_irq(pdev, 0);
            s->irq_raised = false;
        }
    }
}

/* MMIO Read Handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported access size %u at 0x%"HWADDR_PRIx"\n",
                      __func__, size, addr);
        return ~0ULL;
    }

    switch (addr) {
    case RX_DMA_ADDR(0):
        val = s->config_regs.rx_dma_addr[0];
        break;
    case RX_DMA_ADDR(1):
        val = s->config_regs.rx_dma_addr[1];
        break;
    case RX_DMA_ADDR(2):
        val = s->config_regs.rx_dma_addr[2];
        break;
    case RX_DMA_ADDR(3):
        val = s->config_regs.rx_dma_addr[3];
        break;
    case TX_DMA_ADDR(0):
        val = s->config_regs.tx_dma_addr[0];
        break;
    case TX_DMA_ADDR(1):
        val = s->config_regs.tx_dma_addr[1];
        break;
    case TX_DMA_ADDR(2):
        val = s->config_regs.tx_dma_addr[2];
        break;
    case TX_DMA_ADDR(3):
        val = s->config_regs.tx_dma_addr[3];
        break;
    case DRIVER_VER:
        val = s->config_regs.driver_ver;
        break;
    case GPIO_STATUS:
        val = s->config_regs.gpio_status;
        break;
    case FLASH_MODE:
        val = s->config_regs.flash_mode;
        break;
    case FPGA_MODE:
        val = s->config_regs.fpga_mode;
        break;
    case FLASH_BUSY:
        val = s->config_regs.flash_busy;
        break;
    case FLASH_BLOCK:
        val = s->config_regs.flash_block;
        break;
    case PORTS:
        val = s->config_regs.ports;
        break;
    case WRITE_FLASH:
        val = s->config_regs.write_flash;
        break;
    case IRQ_CLEAR:
        val = s->config_regs.irq_clear;
        break;
    case FPGA_VER:
        val = s->config_regs.fpga_ver;
        break;
    case IRQ_EN_ADDR:
        val = s->config_regs.irq_en;
        break;
    case FLAGS_ADDR:
        val = s->config_regs.flags;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        val = 0xFFFFFFFF;
        break;
    }

    return val;
}

/* MMIO Write Handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported access size %u at 0x%"HWADDR_PRIx"\n",
                      __func__, size, addr);
        return;
    }

    switch (addr) {
    case RX_DMA_ADDR(0):
        s->config_regs.rx_dma_addr[0] = val;
        break;
    case RX_DMA_ADDR(1):
        s->config_regs.rx_dma_addr[1] = val;
        break;
    case RX_DMA_ADDR(2):
        s->config_regs.rx_dma_addr[2] = val;
        break;
    case RX_DMA_ADDR(3):
        s->config_regs.rx_dma_addr[3] = val;
        break;
    case TX_DMA_ADDR(0):
        s->config_regs.tx_dma_addr[0] = val;
        break;
    case TX_DMA_ADDR(1):
        s->config_regs.tx_dma_addr[1] = val;
        break;
    case TX_DMA_ADDR(2):
        s->config_regs.tx_dma_addr[2] = val;
        break;
    case TX_DMA_ADDR(3):
        s->config_regs.tx_dma_addr[3] = val;
        break;
    case DRIVER_VER:
        s->config_regs.driver_ver = val;
        break;
    case GPIO_STATUS:
        s->config_regs.gpio_status = val;
        break;
    case FLASH_MODE:
        s->config_regs.flash_mode = val;
        break;
    case FPGA_MODE:
        s->config_regs.fpga_mode = val;
        break;
    case FLASH_BUSY:
        s->config_regs.flash_busy = val;
        break;
    case FLASH_BLOCK:
        s->config_regs.flash_block = val;
        break;
    case PORTS:
        s->config_regs.ports = val;
        break;
    case WRITE_FLASH:
        s->config_regs.write_flash = val;
        break;
    case IRQ_CLEAR:
        /* Write to IRQ_CLEAR acknowledges interrupt and deasserts */
        s->config_regs.irq_clear = val;
        pci_set_irq(PCI_DEVICE(s), 0);
        s->irq_raised = false;
        /* Re-evaluate after ack to be safe */
        pcibase_update_irq(s);
        break;
    case FPGA_VER:
        s->config_regs.fpga_ver = val;
        break;
    case IRQ_EN_ADDR:
        s->config_regs.irq_en = val;
        pcibase_update_irq(s);
        break;
    case FLAGS_ADDR:
        /* Flags register: bits 4-7 are clear-on-write (RX done) */
        /* For simplicity, we just store, but we emulate W1C for bits 4-7. */
        /* Store the written value, but clear bits 4-7 that were set? */
        /* Actually in driver: it writes rx_done bits to clear them. */
        /* So we clear the bits that are written with 1 in the mask 0xF0. */
        s->config_regs.flags = (s->config_regs.flags & ~(val & 0xF0)) | (val & ~0xF0);
        pcibase_update_irq(s);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        break;
    }
}

/* PIO handlers (unused) */
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
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
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

    /* Reset registers to power-on defaults */
    memset(&s->config_regs, 0, sizeof(s->config_regs));
    s->config_regs.driver_ver = 0x01;
    s->config_regs.fpga_ver   = 0x0104;   /* Version 0.01 svn-260 */
    s->config_regs.ports      = 0x00000004; /* Four ports */
    s->config_regs.flash_busy = 0;
    s->config_regs.flags      = 0x00000000; /* Reset state */
    s->irq_raised = false;
    pci_set_irq(PCI_DEVICE(s), 0);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_SOLOS );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_SOLOS );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SOLOS );
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
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 128, .name = "solos-config" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_RAM, .size = 32768, .name = "solos-buffers" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X initialization needed; driver uses legacy IRQ */

    /* DMA Config: not used, device reports version below DMA_SUPPORTED */

    /* Timer Config: not used */

    /* Final state initialization */
    s->fpga_version = 0x0104;
    s->buffer_size = 2048;
    s->nr_ports = 4;
    s->using_dma = 0;
    s->has_msi = false;
    s->has_msix = false;
    memset(&s->config_regs, 0, sizeof(s->config_regs));
    /* Set sensible defaults for some registers */
    s->config_regs.driver_ver = 0x01;  /* DRIVER_VERSION */
    s->config_regs.fpga_ver = 0x0104;   /* Example FPGA version */
    s->config_regs.flash_busy = 0;      /* Not busy */
    s->config_regs.ports = 0x00000004;  /* Four ports (lower byte) */
    s->config_regs.flags = 0x00000000;  /* Reset state */
    s->irq_raised = false;
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
