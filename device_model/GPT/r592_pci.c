/*
 * QEMU PCI device model for Ricoh R5C592 (r592) memstick host controller
 * Phase 2: Functional behavior implementation based on Linux driver r592.c
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
/* #include "hw/memstick/memstick.h" */

#define TYPE_PCIBASE_DEVICE "r592_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Ricoh R5C592 specific register offsets and bit definitions */
#define R592_TPC_EXEC                  0x00
#define R592_STATUS                    0x10
#define R592_IO                        0x18
#define R592_POWER                     0x20
#define R592_IO_MODE                   0x24
#define R592_REG_MSC                   0x28
#define R592_FIFO_DMA                  0x2C
#define R592_FIFO_PIO                  0x30
#define R592_FIFO_DMA_SETTINGS         0x34

#define R592_TPC_EXEC_TPC_SHIFT        28
#define R592_TPC_EXEC_LEN_SHIFT        16
#define R592_TPC_EXEC_BIG_FIFO         (1U << 26)

#define R592_STATUS_SEND_ERR           (1U << 24)
#define R592_STATUS_RECV_ERR           (1U << 25)
#define R592_STATUS_P_CMDNACK          (1U << 16)
#define R592_STATUS_P_INTERR           (1U << 18)
#define R592_STATUS_P_CED              (1U << 19)
#define R592_STATUS_P_BREQ             (1U << 17)
#define R592_STATUS_RDY                (1U << 28)
#define R592_STATUS_CED                (1U << 29)

#define R592_POWER_0                   (1U << 0)
#define R592_POWER_1                   (1U << 1)
#define R592_POWER_20                  (1U << 5)

#define R592_IO_RESET                  (1U << 31)
#define R592_IO_SERIAL1                (1U << 20)
#define R592_IO_SERIAL2                (1U << 30)
#define R592_IO_DIRECTION              (1U << 24)

#define R592_IO_MODE_SERIAL            1U
#define R592_IO_MODE_PARALLEL          3U

#define R592_REG_MSC_PRSNT             (1U << 1)
#define R592_REG_MSC_IRQ_INSERT        (1U << 8)
#define R592_REG_MSC_IRQ_REMOVE        (1U << 9)
#define R592_REG_MSC_LED               (1U << 15)
#define R592_REG_MSC_FIFO_DMA_DONE     (1U << 11)
#define R592_REG_MSC_FIFO_DMA_ERR      (1U << 14)

#define R592_REG_MSC_FIFO_EMPTY        (1U << 10)

#define R592_FIFO_DMA_SETTINGS_EN      (1U << 0)
#define R592_FIFO_DMA_SETTINGS_DIR     (1U << 1)
#define R592_FIFO_DMA_SETTINGS_CAP     (1U << 24)

#define R592_LFIFO_SIZE                512

#define IRQ_ALL_ACK_MASK               0x00007F00U
#define IRQ_ALL_EN_MASK                (IRQ_ALL_ACK_MASK << 16)
#define DMA_IRQ_ACK_MASK               (R592_REG_MSC_FIFO_DMA_DONE | R592_REG_MSC_FIFO_DMA_ERR)
#define DMA_IRQ_EN_MASK                (DMA_IRQ_ACK_MASK << 16)

/*
 * PCI identification
 * From driver's pci_device_id table: { PCI_VDEVICE(RICOH, 0x0592), }
 * RICOH vendor ID is 0x1180 (standard PCI ID).
 */
#define R592_PCI_VENDOR_ID             0x1180
#define R592_PCI_DEVICE_ID             0x0592
#define R592_PCI_CLASS_ID              0xFFFF

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
    uint32_t intr_status; /* pending IRQ bits (lower 16 of REG_MSC) */
    uint32_t intr_mask;   /* enable mask (upper 16 of REG_MSC) */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct {
        uint32_t tpc_exec;
        uint32_t status;
        uint32_t io;
        uint32_t power;
        uint32_t io_mode;
        uint32_t reg_msc;
        uint32_t fifo_dma;
        uint32_t fifo_pio;
        uint32_t fifo_dma_settings;
    } regs;

    /* DMA Context */
    struct {
        bool enabled;
        bool is_write;   /* true: host->device (WRITE), false: device->host */
        int error;
        dma_addr_t addr;
        uint32_t len;
    } dma;

    /* Operational status flags */
    bool parallel_mode;

    /* State used to handle reset sequences */
    bool reset_in_progress;

    /* Power management state (D0-D3) */
    uint8_t pm_state;

    /* Other additional info */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* In hardware, REG_MSC contains status (low 16 bits) and enables (high 16). */
    s->intr_status = s->regs.reg_msc & 0xFFFF;
    s->intr_mask   = (s->regs.reg_msc >> 16) & 0xFFFF;

    if (s->intr_status & s->intr_mask) {
        /* Level-triggered interrupt */
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (!s->dma.enabled || s->dma.len == 0) {
        return;
    }

    /* The real hardware uses FIFO of 512 bytes; driver enforces that. */
    uint8_t buf[R592_LFIFO_SIZE];
    uint32_t len = s->dma.len;
    if (len > R592_LFIFO_SIZE) {
        len = R592_LFIFO_SIZE;
    }

    if (is_write) {
        /* Host -> device: read from guest memory, discard (we don't emulate media). */
        pci_dma_read(pdev, s->dma.addr, buf, len);
    } else {
        /* Device -> host: write zero pattern. */
        memset(buf, 0, len);
        pci_dma_write(pdev, s->dma.addr, buf, len);
    }

    /* Mark DMA done, clear enable bit */
    s->dma.enabled = false;
    s->regs.fifo_dma_settings &= ~R592_FIFO_DMA_SETTINGS_EN;

    /* Raise DMA done interrupt (or error if s->dma.error set). */
    if (s->dma.error) {
        s->regs.reg_msc |= R592_REG_MSC_FIFO_DMA_ERR;
    } else {
        s->regs.reg_msc |= R592_REG_MSC_FIFO_DMA_DONE;
    }

    pcibase_update_irq(s);
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    /* Device is little-endian; driver uses readl/__/raw_be for FIFO only. */
    switch (addr) {
    case R592_TPC_EXEC:
        val = s->regs.tpc_exec;
        break;
    case R592_STATUS:
        /* Report ready & CED to satisfy r592_wait_status() and execute_tpc() */
        val = s->regs.status;
        break;
    case R592_IO:
        val = s->regs.io;
        break;
    case R592_POWER:
        val = s->regs.power;
        break;
    case R592_IO_MODE:
        val = s->regs.io_mode;
        break;
    case R592_REG_MSC:
        val = s->regs.reg_msc;
        break;
    case R592_FIFO_DMA:
        val = s->regs.fifo_dma;
        break;
    case R592_FIFO_PIO:
        /* PIO FIFO: driver uses __raw_readl in big endian; we just return shadow. */
        val = s->regs.fifo_pio;
        break;
    case R592_FIFO_DMA_SETTINGS:
        val = s->regs.fifo_dma_settings;
        break;
    default:
        /* Unknown/unused offsets read as zero */
        val = 0;
        break;
    }

    /* Honor access size: truncate for 1/2-byte reads */
    if (size == 1) {
        val &= 0xFF;
    } else if (size == 2) {
        val &= 0xFFFF;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v32 = (uint32_t)val;

    /* Expand sub-32-bit writes into full register writes (driver only uses 32-bit). */

    switch (addr) {
    case R592_TPC_EXEC:
        s->regs.tpc_exec = v32;
        /* When driver triggers TPC, it later waits on STATUS_RDY[/CED].
         * We immediately mark transaction as complete and set RDY/CED bits
         * if needed so r592_wait_status() succeeds.
         */
        s->regs.status |= R592_STATUS_RDY | R592_STATUS_CED;
        break;

    case R592_STATUS:
        /* Driver never writes STATUS directly; ignore to keep it simple. */
        s->regs.status = v32;
        break;

    case R592_IO:
        /* Handle reset bit and direction bits as simple latches */
        s->regs.io = v32;
        if (v32 & R592_IO_RESET) {
            /* Simulate a reset by clearing RDY/CED and FIFO/STATUS errors */
            s->regs.status &= ~(R592_STATUS_SEND_ERR | R592_STATUS_RECV_ERR |
                                R592_STATUS_RDY | R592_STATUS_CED |
                                R592_STATUS_P_CMDNACK | R592_STATUS_P_INTERR |
                                R592_STATUS_P_CED | R592_STATUS_P_BREQ);
            s->regs.reg_msc |= R592_REG_MSC_FIFO_EMPTY;
        }
        break;

    case R592_POWER:
        s->regs.power = v32;
        break;

    case R592_IO_MODE:
        s->regs.io_mode = v32;
        /* Track parallel_mode flag to match driver expectations */
        if (v32 == R592_IO_MODE_PARALLEL) {
            s->parallel_mode = true;
        } else {
            s->parallel_mode = false;
        }
        break;

    case R592_REG_MSC: {
        /* Full 32-bit register: low 16 bits status, high 16 bits enable */
        uint16_t old_status = s->regs.reg_msc & 0xFFFF;
        uint16_t old_enable = (s->regs.reg_msc >> 16) & 0xFFFF;
        uint16_t new_status = v32 & 0xFFFF;
        uint16_t new_enable = (v32 >> 16) & 0xFFFF;

        /* Driver acks interrupts by reg &= ~irq_status: write-1-to-clear
         * semantics implemented by r592_irq(). When it writes the full
         * register, we simply store the value.
         */
        s->regs.reg_msc = (new_enable << 16) | new_status;

        /* Maintain FIFO_EMPTY as set when DMA not in progress */
        if (!s->dma.enabled) {
            s->regs.reg_msc |= R592_REG_MSC_FIFO_EMPTY;
        }

        /* Keep track of masks and status for IRQ logic */
        s->intr_status = s->regs.reg_msc & 0xFFFF;
        s->intr_mask   = (s->regs.reg_msc >> 16) & 0xFFFF;

        (void)old_status;
        (void)old_enable;
        pcibase_update_irq(s);
        break;
    }

    case R592_FIFO_DMA:
        s->regs.fifo_dma = v32;
        s->dma.addr = v32;
        break;

    case R592_FIFO_PIO:
        /* PIO data is written/read in big-endian raw ops in the driver.
         * We treat FIFO as a simple latch; software manages the spill buffer.
         */
        s->regs.fifo_pio = v32;
        break;

    case R592_FIFO_DMA_SETTINGS: {
        /* Enable/dir bits plus CAP bit */
        uint32_t old = s->regs.fifo_dma_settings;
        s->regs.fifo_dma_settings = v32;

        /* Ensure CAP bit is set once probed by driver to mark dma_capable. */
        if (!(s->regs.fifo_dma_settings & R592_FIFO_DMA_SETTINGS_CAP)) {
            s->regs.fifo_dma_settings |= R592_FIFO_DMA_SETTINGS_CAP;
        }

        /* Update DMA context */
        s->dma.enabled = (s->regs.fifo_dma_settings & R592_FIFO_DMA_SETTINGS_EN) != 0;
        s->dma.is_write = !(s->regs.fifo_dma_settings & R592_FIFO_DMA_SETTINGS_DIR);

        if (s->dma.enabled && !(old & R592_FIFO_DMA_SETTINGS_EN)) {
            /* Starting new DMA; use fixed length 512 as enforced by driver. */
            s->dma.len = R592_LFIFO_SIZE;
            s->dma.error = 0;
            pcibase_do_dma(s, s->dma.is_write);
        }
        break;
    }

    default:
        /* Ignore writes to unknown offsets */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)size;
    return 0;
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

    s->regs.tpc_exec = 0;
    s->regs.status = 0;
    s->regs.io = 0;
    s->regs.power = 0;
    s->regs.io_mode = 0;
    /* Card present, FIFO empty by default, no IRQs enabled */
    s->regs.reg_msc = R592_REG_MSC_PRSNT | R592_REG_MSC_FIFO_EMPTY;
    s->regs.fifo_dma = 0;
    s->regs.fifo_pio = 0;
    s->regs.fifo_dma_settings = R592_FIFO_DMA_SETTINGS_CAP;

    s->intr_status = s->regs.reg_msc & 0xFFFF;
    s->intr_mask = (s->regs.reg_msc >> 16) & 0xFFFF;

    s->dma.enabled = false;
    s->dma.is_write = false;
    s->dma.error = 0;
    s->dma.addr = 0;
    s->dma.len = 0;

    s->parallel_mode = false;
    s->reset_in_progress = false;
    s->pm_state = 0;

    pcibase_update_irq(s);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  R592_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  R592_PCI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, R592_PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Single MMIO BAR used by driver */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000; /* 4 KiB */
    s->bar_info[0].name = "r592-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    /* Initialize state similar to reset */
    s->regs.tpc_exec = 0;
    s->regs.status = 0;
    s->regs.io = 0;
    s->regs.power = 0;
    s->regs.io_mode = 0;
    s->regs.reg_msc = R592_REG_MSC_PRSNT | R592_REG_MSC_FIFO_EMPTY;
    s->regs.fifo_dma = 0;
    s->regs.fifo_pio = 0;
    s->regs.fifo_dma_settings = R592_FIFO_DMA_SETTINGS_CAP;

    s->intr_status = s->regs.reg_msc & 0xFFFF;
    s->intr_mask   = (s->regs.reg_msc >> 16) & 0xFFFF;

    s->dma.enabled = false;
    s->dma.is_write = false;
    s->dma.error = 0;
    s->dma.addr = 0;
    s->dma.len = 0;

    s->parallel_mode = false;
    s->reset_in_progress = false;
    s->pm_state = 0;

    pcibase_update_irq(s);
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

    (void)s;
}

static const VMStateDescription vmstate_pcibase = {
    .name = "r592_pci",
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
