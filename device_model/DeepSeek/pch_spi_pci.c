/*
 * QEMU model for Intel PCH SPI controller (spi-topcliff-pch)
 * Based on Linux driver /home/eely/linux-7.1/drivers/spi/spi-topcliff-pch.c
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

#define TYPE_PCIBASE_DEVICE "pch_spi_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL        0x8086
#define PCI_DEVICE_ID_PCH_SPI      0x8816
#define PCI_CLASS_ID               0x00ff00

#define PCH_SPCR           0x00
#define PCH_SPBRR          0x04
#define PCH_SPSR           0x08
#define PCH_SPDWR          0x0C
#define PCH_SPDRR          0x10
#define PCH_SSNXCR         0x18
#define PCH_SRST           0x1C
#define PCH_ADDRESS_SIZE   0x20

#define PCH_SPSR_TFD       0x000007C0
#define PCH_SPSR_RFD       0x0000F800
#define PCH_READABLE(x)    (((x) & PCH_SPSR_RFD)>>11)
#define PCH_WRITABLE(x)    (((x) & PCH_SPSR_TFD)>>6)
#define PCH_RX_THOLD       7
#define PCH_RX_THOLD_MAX   15
#define PCH_TX_THOLD       2
#define PCH_MAX_BAUDRATE   5000000
#define PCH_MAX_FIFO_DEPTH 16
#define STATUS_RUNNING     1
#define STATUS_EXITING     2
#define PCH_SLEEP_TIME     10
#define SSN_LOW            0x02U
#define SSN_HIGH           0x03U
#define SSN_NO_CONTROL     0x00U
#define PCH_MAX_CS         0xFF
#define PCI_DEVICE_ID_GE_SPI   0x8816
#define SPCR_SPE_BIT        (1 << 0)
#define SPCR_MSTR_BIT       (1 << 1)
#define SPCR_LSBF_BIT       (1 << 4)
#define SPCR_CPHA_BIT       (1 << 5)
#define SPCR_CPOL_BIT       (1 << 6)
#define SPCR_TFIE_BIT       (1 << 8)
#define SPCR_RFIE_BIT       (1 << 9)
#define SPCR_FIE_BIT        (1 << 10)
#define SPCR_ORIE_BIT       (1 << 11)
#define SPCR_MDFIE_BIT      (1 << 12)
#define SPCR_FICLR_BIT      (1 << 24)
#define SPSR_TFI_BIT        (1 << 0)
#define SPSR_RFI_BIT        (1 << 1)
#define SPSR_FI_BIT         (1 << 2)
#define SPSR_ORF_BIT        (1 << 3)
#define SPBRR_SIZE_BIT      (1 << 10)
#define PCH_ALL             (SPCR_TFIE_BIT|SPCR_RFIE_BIT|SPCR_FIE_BIT|SPCR_ORIE_BIT|SPCR_MDFIE_BIT)
#define SPCR_RFIC_FIELD     20
#define SPCR_TFIC_FIELD     16
#define MASK_SPBRR_SPBR_BITS   ((1 << 10) - 1)
#define MASK_RFIC_SPCR_BITS    (0xf << SPCR_RFIC_FIELD)
#define MASK_TFIC_SPCR_BITS    (0xf << SPCR_TFIC_FIELD)
#define PCH_CLOCK_HZ         50000000
#define PCH_MAX_SPBR         1023

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
    uint32_t irq_reg_sts;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t spcr;
    uint32_t spbrr;
    uint32_t spsr;
    uint32_t spdwr;
    uint32_t spdrr;
    uint32_t ssnxcr;
    uint32_t srst;
    uint32_t addr_size;

    /* Power management state (D0-D3) */
    uint8_t power_state;
};

/* Internal helper for status-triggered signaling. Not used in this minimal model. */

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read size %u at 0x%"HWADDR_PRIx"\n",
                      __func__, size, addr);
        return 0xFFFFFFFF;
    }

    switch (addr & 0x1F) {
    case PCH_SPCR:
        val = s->spcr;
        break;
    case PCH_SPBRR:
        val = s->spbrr;
        break;
    case PCH_SPSR:
        val = s->spsr;
        break;
    case PCH_SPDWR:
        val = s->spdwr;
        break;
    case PCH_SPDRR:
        val = s->spdrr;
        break;
    case PCH_SSNXCR:
        val = s->ssnxcr;
        break;
    case PCH_SRST:
        val = s->srst;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read from unknown offset 0x%"HWADDR_PRIx"\n",
                      __func__, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write size %u at 0x%"HWADDR_PRIx"\n",
                      __func__, size, addr);
        return;
    }

    switch (addr & 0x1F) {
    case PCH_SPCR:
        s->spcr = val;
        break;
    case PCH_SPBRR:
        s->spbrr = val;
        break;
    case PCH_SPSR:
        /* W1C: write 1 to clear bits */
        s->spsr &= ~val;
        break;
    case PCH_SPDWR:
        s->spdwr = val;
        /* In a full emulation, this would push data into TX FIFO,
         * trigger DMA or loopback, and possibly update SPSR.
         * For minimal viable probe, we just store the value.
         */
        break;
    case PCH_SPDRR:
        s->spdrr = val;
        break;
    case PCH_SSNXCR:
        s->ssnxcr = val;
        break;
    case PCH_SRST:
        s->srst = val;
        if (val & 1) {
            /* Device reset: clear all registers */
            s->spcr = 0;
            s->spbrr = 0;
            s->spsr = 0;
            s->spdwr = 0;
            s->spdrr = 0;
            s->ssnxcr = 0;
            /* srst will be cleared by the next write of 0 */
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write to unknown offset 0x%"HWADDR_PRIx" value 0x%"PRIx64"\n",
                      __func__, addr, val);
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Revert registers to power-on defaults */
    s->spcr = 0;
    s->spbrr = 0;
    s->spsr = 0;
    s->spdwr = 0;
    s->spdrr = 0;
    s->ssnxcr = 0;
    s->srst = 0;
    s->addr_size = 0;
    s->irq_reg_sts = 0;
    s->intr_mask = 0;
    s->power_state = 0;
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
        /* PIO access not needed; removed to avoid undefined pcibase_pio_ops */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x8086);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x8816);
    pci_config_set_class(pdev, 0x00ff00);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization: BAR1 MMIO
     * Size: generous 4K to cover multiple channels (actual required size depends on
     * pch_spi_pcidev_id table, not yet available).
     */
    s->num_bars = 1;
    s->bar_info[0].index = 1;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "pch-spi-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Final state initialization before the device is 'live' */
    pcibase_reset(DEVICE(s));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* No extra resources to free */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "pch_spi_pci",
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
