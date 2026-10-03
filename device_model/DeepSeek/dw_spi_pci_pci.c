/*
 * QEMU PCI device model for dw_spi_pci driver.
 * Generated from driver source and register definitions.
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

#define TYPE_PCIBASE_DEVICE "dw_spi_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID            0x8086
#define DEVICE_ID            0x0800
#define CLASS_ID             0x0c00   /* Serial bus controller, SPI subclass */

/* Hardware register offsets from the DW APB SSI controller */
#define DW_SPI_CTRLR0               0x00
#define DW_SPI_CTRLR1               0x04
#define DW_SPI_SSIENR               0x08
#define DW_SPI_SER                  0x10
#define DW_SPI_BAUDR                0x14
#define DW_SPI_TXFTLR               0x18
#define DW_SPI_RXFTLR               0x1c
#define DW_SPI_TXFLR                0x20
#define DW_SPI_RXFLR                0x24
#define DW_SPI_SR                   0x28
#define DW_SPI_IMR                  0x2c
#define DW_SPI_ISR                  0x30
#define DW_SPI_RISR                 0x34
#define DW_SPI_ICR                  0x48
#define DW_SPI_DMACR                0x4c
#define DW_SPI_DMATDLR              0x50
#define DW_SPI_DMARDLR              0x54
#define DW_SPI_VERSION              0x5c
#define DW_SPI_DR                   0x60
#define DW_SPI_RX_SAMPLE_DLY        0xf0
#define DW_SPI_CS_OVERRIDE          0xf4

/* Bit definitions */
#define DW_SPI_CTRLR0_TMOD_TR       0x0
#define DW_SPI_CTRLR0_TMOD_TO       0x1
#define DW_SPI_CTRLR0_TMOD_RO       0x2
#define DW_SPI_CTRLR0_TMOD_EPROMREAD 0x3
#define DW_SPI_CTRLR0_FRF_MOTO_SPI  0x0

#define DW_PSSI_CTRLR0_DFS_MASK     GENMASK(3, 0)
#define DW_PSSI_CTRLR0_DFS32_MASK   GENMASK(20, 16)
#define DW_PSSI_CTRLR0_FRF_MASK     GENMASK(5, 4)
#define DW_PSSI_CTRLR0_TMOD_MASK    GENMASK(9, 8)
#define DW_PSSI_CTRLR0_SCPOL        BIT(7)
#define DW_PSSI_CTRLR0_SCPHA        BIT(6)
#define DW_PSSI_CTRLR0_SRL          BIT(11)

#define DW_HSSI_CTRLR0_FRF_MASK     GENMASK(7, 6)
#define DW_HSSI_CTRLR0_TMOD_MASK    GENMASK(11, 10)
#define DW_HSSI_CTRLR0_SCPHA        BIT(8)
#define DW_HSSI_CTRLR0_SCPOL        BIT(9)
#define DW_HSSI_CTRLR0_SRL          BIT(13)
#define DW_HSSI_CTRLR0_MST          BIT(31)

#define DW_SPI_NDF_MASK             GENMASK(15, 0)

#define DW_SPI_INT_TXEI             BIT(0)
#define DW_SPI_INT_TXOI             BIT(1)
#define DW_SPI_INT_RXUI             BIT(2)
#define DW_SPI_INT_RXOI             BIT(3)
#define DW_SPI_INT_RXFI             BIT(4)

#define DW_SPI_DMACR_RDMAE          BIT(0)
#define DW_SPI_DMACR_TDMAE          BIT(1)

#define DW_SPI_SR_BUSY              BIT(0)
#define DW_SPI_SR_TF_EMPT           BIT(2)
#define DW_SPI_SR_RF_NOT_EMPT       BIT(3)

#define DW_SPI_CAP_CS_OVERRIDE      BIT(0)
#define DW_SPI_CAP_DFS32            BIT(1)

#define DW_SPI_GET_BYTE(_val, _idx) \
    ((_val) >> (BITS_PER_BYTE * (_idx)) & 0xff)

#define DW_SPI_BUF_SIZE \
    (sizeof_field(struct spi_mem_op, cmd.opcode) + \
     sizeof_field(struct spi_mem_op, addr.val) + 256)

#define DW_SPI_WAIT_RETRIES         5

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Shadow registers: 256 bytes of MMIO space, indexed by word offset */
    uint32_t regs[0x40];

    dma_addr_t dma_addr;
    bool dma_in_progress;

    bool enabled;
    bool busy;
    bool reset_active;
    uint8_t power_state;
};

/* IRQ update logic: raise/lower IRQ based on masked status */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t masked = s->regs[DW_SPI_ISR / 4] & s->regs[DW_SPI_IMR / 4];
    bool level = (masked != 0);

    if (msi_enabled(pdev)) {
        if (level) {
            msi_notify(pdev, 0);
        }
        /* MSI is edge-triggered: no need to deassert */
    } else {
        pci_set_irq(pdev, level ? 1 : 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0xffffffff;

    if (addr >= 0x100 || size != 4) {
        return 0xffffffff;
    }

    switch (addr) {
    case DW_SPI_CTRLR0:
    case DW_SPI_CTRLR1:
    case DW_SPI_SER:
    case DW_SPI_BAUDR:
    case DW_SPI_TXFTLR:
    case DW_SPI_RXFTLR:
    case DW_SPI_IMR:
    case DW_SPI_RISR:
    case DW_SPI_DMACR:
    case DW_SPI_DMATDLR:
    case DW_SPI_DMARDLR:
    case DW_SPI_RX_SAMPLE_DLY:
    case DW_SPI_CS_OVERRIDE:
        val = s->regs[addr / 4];
        break;

    case DW_SPI_SSIENR:
        val = s->regs[addr / 4] ? 1 : 0; /* only bit 0 matters */
        break;

    case DW_SPI_SR:
        /* Compute status on the fly based on enabled state */
        val = 0;
        if (s->regs[DW_SPI_SSIENR / 4] & 1) {
            val |= DW_SPI_SR_TF_EMPT; /* always empty when idle */
            if (!(s->regs[DW_SPI_SR / 4] & DW_SPI_SR_BUSY)) {
                /* not busy */
            }
        }
        break;

    case DW_SPI_TXFLR:
        /* Transmit FIFO level: always empty */
        val = 0;
        break;

    case DW_SPI_RXFLR:
        /* Receive FIFO level: always empty */
        val = 0;
        break;

    case DW_SPI_ISR:
        val = s->regs[addr / 4];
        break;

    case DW_SPI_ICR:
        /* Reading ICR clears all interrupt status (as used in driver) */
        s->regs[DW_SPI_ISR / 4] = 0;
        val = 0;
        pcibase_update_irq(s);
        break;

    case DW_SPI_DR:
        /* Data register: return 0, no FIFO emulation */
        val = 0;
        break;

    case DW_SPI_VERSION:
        val = 0x3436312a; /* version 4.01a */
        break;

    default:
        /* Unimplemented or reserved: return 0 */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t old_val, new_mask, idx;

    if (addr >= 0x100 || size != 4) {
        return;
    }

    idx = addr / 4;
    old_val = s->regs[idx];

    switch (addr) {
    case DW_SPI_CTRLR0:
    case DW_SPI_CTRLR1:
    case DW_SPI_SER:
    case DW_SPI_BAUDR:
    case DW_SPI_TXFTLR:
    case DW_SPI_RXFTLR:
    case DW_SPI_DMATDLR:
    case DW_SPI_DMARDLR:
    case DW_SPI_RX_SAMPLE_DLY:
    case DW_SPI_CS_OVERRIDE:
        s->regs[idx] = val;
        break;

    case DW_SPI_SSIENR:
        /* Only bit 0 is writable per driver */
        s->regs[idx] = val & 1;
        break;

    case DW_SPI_IMR:
        s->regs[idx] = val;
        pcibase_update_irq(s);
        break;

    case DW_SPI_ISR:
        /* Write-1-to-clear bits */
        s->regs[idx] &= ~val;
        pcibase_update_irq(s);
        break;

    case DW_SPI_ICR:
        /* Writing to ICR clears corresponding ISR bits (if any set) */
        s->regs[DW_SPI_ISR / 4] &= ~val;
        pcibase_update_irq(s);
        break;

    case DW_SPI_DMACR:
        s->regs[idx] = val;
        break;

    case DW_SPI_DR:
        /* Data register write: could trigger FIFO operations, but no emulation now */
        s->regs[idx] = val;
        break;

    case DW_SPI_SR:
        /* SR is generally read-only; write can clear some status bits? Ignore for now */
        break;

    default:
        /* Silently ignore writes to unimplemented registers */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    /* No PIO interface */
    return 0xffffffff;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    /* No PIO interface */
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

    /* Set power-on defaults for all registers */
    s->regs[DW_SPI_CTRLR0 / 4] = 0x00000000;
    s->regs[DW_SPI_CTRLR1 / 4] = 0x00000000;
    s->regs[DW_SPI_SSIENR / 4] = 0x00000000;
    s->regs[DW_SPI_SER / 4] = 0x00000000;
    s->regs[DW_SPI_BAUDR / 4] = 0x00000000;
    s->regs[DW_SPI_TXFTLR / 4] = 0x00000000;
    s->regs[DW_SPI_RXFTLR / 4] = 0x00000000;
    /* TXFLR and RXFLR are read-only, not stored */
    s->regs[DW_SPI_SR / 4] = 0x00000000;
    s->regs[DW_SPI_IMR / 4] = 0x00000000;
    s->regs[DW_SPI_ISR / 4] = 0x00000000;
    s->regs[DW_SPI_RISR / 4] = 0x00000000;
    s->regs[DW_SPI_ICR / 4] = 0x00000000;
    s->regs[DW_SPI_DMACR / 4] = 0x00000000;
    s->regs[DW_SPI_DMATDLR / 4] = 0x00000000;
    s->regs[DW_SPI_DMARDLR / 4] = 0x00000000;
    s->regs[DW_SPI_RX_SAMPLE_DLY / 4] = 0x00000000;
    s->regs[DW_SPI_CS_OVERRIDE / 4] = 0x00000000; /* no capabilities */
    /* Version is read-only; DR not cleared */

    /* Update IRQ state */
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Enable MSI: driver uses pci_alloc_irq_vectors with all types */
    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        /* MSI might not be available, but device can still work with legacy */
        error_report("Failed to initialize MSI, device will use legacy INTx");
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x100,  /* 256 bytes */
        .name = "dw_spi_mmio"
    };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize device state */
    pcibase_reset(DEVICE(s));
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
    .name = "dw_spi_pci_pci",
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
