/*
 * QEMU PCI device model for Solos PCI (functional model).
 * Auto-generated based on Linux driver drivers/atm/solos-pci.c
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
/* solos-attrlist.c was referenced but is not available in QEMU build context.
 * It is safe to drop this include for the QEMU device model. */

#define TYPE_PCIBASE_DEVICE "solos_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID 0x10ee
#define PCIBASE_DEVICE_ID 0x0300
#define PCIBASE_CLASS_ID  PCI_CLASS_OTHERS

#define IRQ_CLEAR      0x70
#define FPGA_VER       0x74
#define CONFIG_RAM_SIZE    128
#define FLAGS_ADDR     0x7C
#define IRQ_EN_ADDR    0x78
#define WRITE_FLASH    0x6C
#define PORTS          0x68
#define FLASH_BLOCK    0x64
#define FLASH_BUSY     0x60
#define FPGA_MODE      0x5C
#define FLASH_MODE     0x58
#define GPIO_STATUS    0x54
#define DRIVER_VER     0x50
#define TX_DMA_ADDR(port)   (0x40 + (4 * (port)))
#define RX_DMA_ADDR(port)   (0x30 + (4 * (port)))
#define DATA_RAM_SIZE  32768
#define RX_DMA_SIZE    2048
#define LEGACY_BUFFERS 2
#define DMA_SUPPORTED  4

#define PORTS_MAX      4


/* buffer layout helpers used in the driver (for MMIO path) */
#define BUF_SIZE       2048
#define OLD_BUF_SIZE   1024

#define FLASH_BUF      0x0000
#define RX_BUF(card, port) ((card)->buffers + ((port) * (card)->buffer_size))
#define TX_BUF(card, port) ((card)->buffers + ((port) * (card)->buffer_size))


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
    uint32_t config_regs_shadow[0x80 / 4];

    /* Simple model of data buffer RAM (BAR1 in real hw; here we emulate locally) */
    uint8_t data_ram[DATA_RAM_SIZE];

    /* IRQ related state */
    bool irq_enabled;
    bool irq_asserted;

    /* Flags register state (FLAGS_ADDR) */
    uint32_t flags_reg;

    /* GPIO status register shadow */
    uint32_t gpio_status_reg;

    /* Flash / FPGA related state */
    uint32_t fpga_ver_reg;    /* value returned at FPGA_VER offset */
    uint32_t ports_reg;       /* value returned at PORTS offset */
    uint32_t flash_busy_reg;  /* non-zero while flash op "busy" */
    uint32_t flash_mode_reg;
    uint32_t fpga_mode_reg;
    uint32_t write_flash_reg;
    uint32_t flash_block_reg;
    uint32_t driver_ver_reg;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* In the real hardware, interrupts are level- or edge-triggered by
     * internal events such as RX/TX completion or flash operations.
     * The provided driver, however, only clears IRQs by writing to
     * IRQ_CLEAR and enables them via IRQ_EN_ADDR. It never polls
     * explicit status bits from the device to decide whether to call
     * the ISR.
     *
     * To keep behaviour minimal and deterministic, we only gate the
     * PCI interrupt line on the irq_enabled flag. The actual assertion
     * and deassertion are performed by helpers below.
     */

    if (s->irq_enabled && s->irq_asserted) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

G_GNUC_UNUSED static void pcibase_raise_irq(PCIBaseState *s)
{
    s->irq_asserted = true;
    pcibase_update_irq(s);
}

static void pcibase_lower_irq(PCIBaseState *s)
{
    s->irq_asserted = false;
    pcibase_update_irq(s);
}

/* Device-initiated DMA logic based on driver access patterns
 *
 * The driver writes a host physical address into RX_DMA_ADDR(port) or
 * TX_DMA_ADDR(port) and then expects the FPGA to DMA data between the
 * card's local buffers and that host address. The exact buffer
 * contents and protocol are implemented on the FPGA; the Linux driver
 * only sees skb queues.
 *
 * QEMU does not need to emulate the full data path for driver probe
 * and binding to succeed. The driver does not verify payload contents
 * during probe; DMA is used later for data transfer. To stay within
 * the "no invented hardware" constraint, we only model the existence
 * of DMA registers and do not perform actual pci_dma_read/write
 * transactions.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)pdev;
    (void)is_write;
    /* No functional DMA behaviour required for successful probe. */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Registers are 32-bit wide; handle 4-byte accesses. */
    if (size != 4) {
        /* The driver only uses ioread32/iowrite32, so other sizes
         * should not occur. Return zero if they do. */
        return 0;
    }

    switch (addr) {
    case FPGA_VER:
        /* The driver reads FPGA_VER once at probe to parse
         * major/minor and svn version. Any value is acceptable as
         * long as the arithmetic is consistent. We choose a value
         * which passes the flash-upgrade checks:
         *   fpga_ver >= 37 and fpga_ver >= 39.
         *
         * Layout in driver:
         *   fpga_ver = (data32 & 0x0000FFFF);
         *   major_ver = ((data32 & 0xFF000000) >> 24);
         *   minor_ver = ((data32 & 0x00FF0000) >> 16);
         */
        val = s->fpga_ver_reg;
        break;

    case PORTS:
        /* Driver expects number of ports in low byte and uses it as
         * nr_ports, with a maximum of 4 implied by PORTS_MAX. */
        val = s->ports_reg;
        break;

    case GPIO_STATUS:
        val = s->gpio_status_reg;
        break;

    case DRIVER_VER:
        val = s->driver_ver_reg;
        break;

    case FLASH_BUSY:
        /* Driver waits for this to become zero (wait_event with
         * !ioread32(FLASH_BUSY)). Provide simple non-busy behaviour:
         * we clear busy on every read. */
        val = s->flash_busy_reg;
        s->flash_busy_reg = 0;
        break;

    case FLASH_MODE:
        val = s->flash_mode_reg;
        break;

    case FPGA_MODE:
        val = s->fpga_mode_reg;
        break;

    case WRITE_FLASH:
        val = s->write_flash_reg;
        break;

    case FLASH_BLOCK:
        val = s->flash_block_reg;
        break;

    case IRQ_EN_ADDR:
        val = s->irq_enabled ? 1 : 0;
        break;

    case IRQ_CLEAR:
        /* Reads are not used by driver; return 0. */
        val = 0;
        break;

    case FLAGS_ADDR:
        /* Holds various TX/RX flags. During probe, the driver only
         * uses FLAGS_ADDR in non-DMA case to set RX empty bits and in
         * fpga_tx()/solos_bh() runtime. For probe success, a fixed
         * value of zero is acceptable. */
        val = s->flags_reg;
        break;

    default:
        /* DMA address registers for RX/TX: RX_DMA_ADDR(port) and
         * TX_DMA_ADDR(port). The driver does not read them; only
         * writes. Return zero for completeness. */
        if (addr >= RX_DMA_ADDR(0) && addr < RX_DMA_ADDR(PORTS_MAX)) {
            val = 0;
        } else if (addr >= TX_DMA_ADDR(0) && addr < TX_DMA_ADDR(PORTS_MAX)) {
            val = 0;
        } else {
            /* For any other offset within our BAR, fall back to
             * generic shadow array (for potential future uses). */
            if (addr < sizeof(s->config_regs_shadow)) {
                unsigned index = addr >> 2;
                val = s->config_regs_shadow[index];
            } else {
                val = 0;
            }
        }
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        /* Driver only uses 32-bit MMIO */
        return;
    }

    switch (addr) {
    case GPIO_STATUS:
        /* GPIO register is read-modify-write in driver (geos_gpio_*).
         * Maintain a shadow of its value. */
        s->gpio_status_reg = (uint32_t)val;
        break;

    case DRIVER_VER:
        /* flash_upgrade() writes DRIVER_VERSION before permitting
         * flash upgrades. Store the value. */
        s->driver_ver_reg = (uint32_t)val;
        break;

    case FPGA_MODE:
        /* Driver toggles FPGA_MODE around reset and flash upgrade. */
        s->fpga_mode_reg = (uint32_t)val;
        break;

    case FLASH_MODE:
        s->flash_mode_reg = (uint32_t)val;
        break;

    case WRITE_FLASH:
        /* A value of 1 triggers flash operations and sets FLASH_BUSY
         * until done. We simply record the write and set busy, which
         * will be cleared on next FLASH_BUSY read. */
        s->write_flash_reg = (uint32_t)val;
        if (val) {
            s->flash_busy_reg = 1;
        }
        break;

    case FLASH_BLOCK:
        s->flash_block_reg = (uint32_t)val;
        break;

    case IRQ_EN_ADDR:
        /* Any nonzero value enables interrupts. */
        s->irq_enabled = (val != 0);
        pcibase_update_irq(s);
        break;

    case IRQ_CLEAR:
        /* Writing to IRQ_CLEAR clears the interrupt. */
        pcibase_lower_irq(s);
        break;

    case FLAGS_ADDR:
        /* Driver writes various bits here:
         *  - in non-DMA RX path, it sets RX empty flags for all ports
         *  - in fpga_tx(), it writes tx_started bits.
         * For probe success, we just record the new value. */
        s->flags_reg = (uint32_t)val;
        break;

    default:
        /* DMA address registers and generic shadow */
        if (addr >= RX_DMA_ADDR(0) && addr < RX_DMA_ADDR(PORTS_MAX)) {
            /* RX_DMA_ADDR(port) : driver writes a host DMA address
             * which we do not interpret further in this minimal
             * model. */
            /* no-op except for optional logging */
        } else if (addr >= TX_DMA_ADDR(0) && addr < TX_DMA_ADDR(PORTS_MAX)) {
            /* TX_DMA_ADDR(port) : same as RX_DMA_ADDR */
        } else {
            if (addr < sizeof(s->config_regs_shadow)) {
                unsigned index = addr >> 2;
                s->config_regs_shadow[index] = (uint32_t)val;
            }
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The driver does not use I/O-port based access at all. */
    (void)s;
    (void)addr;
    (void)size;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    /* The driver does not use I/O-port based access at all. */
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

    /* Reset register shadows to power-on defaults */
    memset(s->config_regs_shadow, 0, sizeof(s->config_regs_shadow));

    s->irq_enabled = false;
    s->irq_asserted = false;
    s->flags_reg = 0;
    s->gpio_status_reg = 0;
    s->flash_busy_reg = 0;
    s->flash_mode_reg = 0;
    s->fpga_mode_reg = 0;
    s->write_flash_reg = 0;
    s->flash_block_reg = 0;
    s->driver_ver_reg = 0;

    /* Provide stable, probe-friendly values for immutable registers */
    /* Example FPGA version: major 0x00, minor 0x04, svn 39 */
    s->fpga_ver_reg = (0x00u << 24) | (0x04u << 16) | 39u;

    /* Four ports available (nr_ports = 4) in low byte */
    s->ports_reg = 4u;

    /* Clear data RAM */
    memset(s->data_ram, 0, sizeof(s->data_ram));

    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization
     *
     * The driver maps two BARs:
     *   BAR0: config_regs (CONFIG_RAM_SIZE bytes)
     *   BAR1: buffers (DATA_RAM_SIZE bytes)
     *
     * Our Phase-1 skeleton only created a single MMIO bar. Here we
     * augment that to more closely match the usage while preserving
     * the existing BAR0 setup. BAR1 is modelled as RAM-backed MMIO so
     * that memcpy_toio/memcpy_fromio from the driver have a target.
     */

    s->num_bars = 2;

    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x1000; /* large enough to cover all cfg regs */
    s->bar_info[0].name  = "solos-mmio";

    s->bar_info[1].index = 1;
    s->bar_info[1].type  = BAR_TYPE_RAM;
    s->bar_info[1].size  = DATA_RAM_SIZE;
    s->bar_info[1].name  = "solos-data";

    for (int i = 2; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X are not explicitly used by the driver; leave disabled */

    /* Initialize register shadow state and power-on defaults */
    memset(s->config_regs_shadow, 0, sizeof(s->config_regs_shadow));

    s->irq_enabled = false;
    s->irq_asserted = false;
    s->flags_reg = 0;
    s->gpio_status_reg = 0;
    s->flash_busy_reg = 0;
    s->flash_mode_reg = 0;
    s->fpga_mode_reg = 0;
    s->write_flash_reg = 0;
    s->flash_block_reg = 0;
    s->driver_ver_reg = 0;

    /* Same values as in reset() */
    s->fpga_ver_reg = (0x00u << 24) | (0x04u << 16) | 39u;
    s->ports_reg = 4u;

    memset(s->data_ram, 0, sizeof(s->data_ram));

    pcibase_update_irq(s);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, 0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* No additional resources to free */
    (void)s;
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
