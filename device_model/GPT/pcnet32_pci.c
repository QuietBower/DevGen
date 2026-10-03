/*
 * QEMU PCI device model for AMD PCnet32 (minimal functional model
 * sufficient to let the Linux pcnet32 driver probe and bind).
 *
 * This model implements the PCnet32 I/O register interface used by
 * the driver: RAP/RDP/BDP/RESET in both word and dword modes.
 * It provides shadow CSR/BCR arrays and a tiny amount of behavior
 * that the driver explicitly depends on during probe and open:
 * - Access method/width detection
 * - Chip version reporting via CSR88/CSR89
 * - Station address CSR12-14 and PROM bytes at ioaddr+0..5
 * - CSR0 basic control bits (INIT/START/STOP/TXPOLL/INTEN/IDON)
 * - CSR3 interrupt mask and CSR0 interrupt status/acknowledge
 * - CSR4 and CSR15 simple storage
 * - Basic interrupt generation/clearing following driver's usage
 *
 * No actual DMA, rings, or packet processing is implemented; these
 * are not required for the driver to successfully bind and keep the
 * interface up from the kernel's perspective.
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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "pcnet32_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID   PCI_VENDOR_ID_AMD
#define PCIBASE_DEVICE_ID   0x2000
#define PCIBASE_CLASS_ID    PCI_CLASS_NETWORK_ETHERNET

/* PCnet32 register and constant definitions (from driver) */
#define CSR0                0
#define CSR3                3
#define CSR4                4
#define CSR5                5
#define CSR15               15

#define PCNET32_PORT_AUI        0x00
#define PCNET32_PORT_10BT       0x01
#define PCNET32_PORT_GPSI       0x02
#define PCNET32_PORT_MII        0x03
#define PCNET32_PORT_PORTSEL    0x03
#define PCNET32_PORT_ASEL       0x04
#define PCNET32_PORT_100        0x40
#define PCNET32_PORT_FD         0x80

#define PCNET32_DMA_MASK        0xffffffffU

#define PCNET32_WIO_RDP         0x10
#define PCNET32_WIO_RAP         0x12
#define PCNET32_WIO_RESET       0x14
#define PCNET32_WIO_BDP         0x16

#define PCNET32_DWIO_RDP        0x10
#define PCNET32_DWIO_RAP        0x14
#define PCNET32_DWIO_RESET      0x18
#define PCNET32_DWIO_BDP        0x1C

#define PCNET32_TOTAL_SIZE      0x20

#define CSR0_INIT           0x0001
#define CSR0_START          0x0002
#define CSR0_STOP           0x0004
#define CSR0_TXPOLL         0x0008
#define CSR0_INTEN          0x0040
#define CSR0_IDON           0x0100
#define CSR0_NORMAL         (CSR0_START | CSR0_INTEN)

#define PCNET32_INIT_LOW    1
#define PCNET32_INIT_HIGH   2

#define CSR5_SUSPEND        0x0001

#define PCNET32_MC_FILTER   8
#define PCNET32_79C970A     0x2621

#define PCNET32_REGS_PER_PHY    32
#define PCNET32_MAX_PHYS        32

#define PCNET32_NUM_REGS    136

#define PCNET32_LOG_TX_BUFFERS      4
#define PCNET32_LOG_RX_BUFFERS      5
#define PCNET32_LOG_MAX_TX_BUFFERS  9
#define PCNET32_LOG_MAX_RX_BUFFERS  9

#define TX_RING_SIZE        (1 << (PCNET32_LOG_TX_BUFFERS))
#define RX_RING_SIZE        (1 << (PCNET32_LOG_RX_BUFFERS))
#define TX_MAX_RING_SIZE    (1 << (PCNET32_LOG_MAX_TX_BUFFERS))
#define RX_MAX_RING_SIZE    (1 << (PCNET32_LOG_MAX_RX_BUFFERS))

#define PKT_BUF_SKB         1544

#define PCNET32_TOTAL_IO_SIZE  PCNET32_TOTAL_SIZE


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

    /* Hardware Register Shadows */

    /* CSR and BCR arrays as seen through RAP/RDP/BDP */
    uint16_t csr[256];
    uint16_t bcr[256];

    /* RAP index (selected CSR/BCR) */
    uint16_t rap;

    /* PROM / station address bytes (ioaddr + 0..15) */
    uint8_t prom[16];

    /* latched access width mode; the driver will first try WIO then DWIO
     * using the same base address. We support both unconditionally. */

    /* simple interrupt line state */
    bool irq_level;
};

/* Forward declaration */
static void pcibase_update_irq(PCIBaseState *s);

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint16_t csr0 = s->csr[CSR0];
    uint16_t csr3 = s->csr[CSR3];

    /* The driver treats CSR0 bits 0x8f00 as interrupt summary sources and
     * enables/disables them via CSR3 mask bits 0x5f00. We do not model
     * individual sources, but we assert the IRQ line whenever any of
     * those masked bits are set in CSR0 and INTEN is enabled. */

    bool active = false;

    if (csr0 & CSR0_INTEN) {
        uint16_t src = csr0 & 0x8f00;
        uint16_t mask = csr3 & 0x5f00;
        if (src & mask) {
            active = true;
        }
    }

    if (active && !s->irq_level) {
        s->irq_level = true;
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else if (!active && s->irq_level) {
        s->irq_level = false;
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns
 *
 * The provided driver code never programs CSR1/CSR2 with an init block
 * DMA address in a way that we must emulate actual DMA transactions;
 * it only checks CSR0 IDON and uses its own DMA descriptors. So we do
 * not implement any pci_dma_read/write here.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)s;
    (void)pdev;
    (void)is_write;
}

/* Helper: reset internal CSR/BCR state to power-on defaults */
static void pcibase_soft_reset(PCIBaseState *s)
{
    int i;

    /* Clear all CSR/BCR */
    for (i = 0; i < 256; i++) {
        s->csr[i] = 0;
        s->bcr[i] = 0;
    }

    s->rap = 0;

    /* CSR0: STOP bit set after reset (bit 2) */
    s->csr[CSR0] = CSR0_STOP;

    /* CSR3: interrupt mask initially zero (driver will program it) */
    s->csr[CSR3] = 0;

    /* CSR4, CSR15 default to zero; driver will write specific values */
    s->csr[CSR4] = 0;
    s->csr[CSR5] = 0;
    s->csr[CSR15] = 0;

    /* Chip version: driver expects read_csr(88) | (read_csr(89) << 16) to
     * yield a value whose low 12 bits equal 0x003 (for PCnet). It then
     * shifts >> 12 to get chip_version and compares against constants
     * like 0x2621 (PCNET32_79C970A). We can encode 0x2621 here by
     * splitting into CSR88/89 as in the driver's reconstruction:
     *   chip_raw = (csr[89] << 16) | csr[88]
     *   if ((chip_raw & 0xfff) != 0x003) fail
     *   chip_version = (chip_raw >> 12) & 0xffff
     * We need chip_version == 0x2621 => chip_raw = (0x2621 << 12) | 0x003.
     */
    {
        uint32_t chip_raw = (PCNET32_79C970A << 12) | 0x0003;
        s->csr[88] = chip_raw & 0xffff;
        s->csr[89] = (chip_raw >> 16) & 0xffff;
    }

    /* PROM / station address: default fixed address 52:54:00:12:34:56.
     * The driver reads CSR12-14 to get MAC, then reads PROM bytes at
     * ioaddr+0..5 and may override the MAC if CSR address is invalid.
     * We keep both CSR and PROM consistent. */
    s->prom[0] = 0x52;
    s->prom[1] = 0x54;
    s->prom[2] = 0x00;
    s->prom[3] = 0x12;
    s->prom[4] = 0x34;
    s->prom[5] = 0x56;
    s->prom[6] = 0x00;
    s->prom[7] = 0x00;
    s->prom[8] = 0x00;
    s->prom[9] = 0x00;
    s->prom[10] = 0x00;
    s->prom[11] = 0x00;
    s->prom[12] = 0x00;
    s->prom[13] = 0x00;
    s->prom[14] = 0x00;
    s->prom[15] = 0x00;

    /* Program CSR12-14 with MAC address as the hardware would. Each CSR
     * holds two bytes little-endian. */
    s->csr[12] = (uint16_t)s->prom[0] | ((uint16_t)s->prom[1] << 8);
    s->csr[13] = (uint16_t)s->prom[2] | ((uint16_t)s->prom[3] << 8);
    s->csr[14] = (uint16_t)s->prom[4] | ((uint16_t)s->prom[5] << 8);

    s->irq_level = false;
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* No MMIO BARs are defined for this device; all accesses should be PIO.
     * Return 0 for any accidental MMIO access. */
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
}

/* Helper functions for word/dword access semantics */
static uint16_t pcibase_read_csr(PCIBaseState *s)
{
    uint16_t index = s->rap;
    if (index == CSR0) {
        /* CSR0 reflects current status/command; nothing special here. */
    }
    return s->csr[index];
}

static void pcibase_write_csr(PCIBaseState *s, uint16_t val)
{
    uint16_t index = s->rap;

    switch (index) {
    case CSR0: {
        uint16_t old = s->csr[CSR0];
        uint16_t newv = old;

        /* Bits 0..3 are command bits INIT/START/STOP/TXPOLL as written by driver. */
        if (val & CSR0_INIT) {
            /* INIT command: set IDON and clear STOP. */
            newv |= CSR0_IDON;
            newv &= ~CSR0_STOP;
        }
        if (val & CSR0_START) {
            newv |= CSR0_START;
            newv &= ~CSR0_STOP;
        }
        if (val & CSR0_STOP) {
            newv |= CSR0_STOP;
            newv &= ~CSR0_START;
        }

        /* INTEN directly from written value. */
        if (val & CSR0_INTEN) {
            newv |= CSR0_INTEN;
        } else {
            newv &= ~CSR0_INTEN;
        }

        /* IDON: the driver never explicitly clears it, and comments mention
         * clearing may trigger hardware bugs; we leave it sticky once set. */
        if (old & CSR0_IDON) {
            newv |= CSR0_IDON;
        }

        s->csr[CSR0] = newv;
        break;
    }
    case CSR3:
        /* CSR3 is interrupt mask; driver manipulates lower byte separately. */
        s->csr[CSR3] = val;
        break;
    case CSR4:
    case CSR5:
    case CSR15:
        s->csr[index] = val;
        break;
    default:
        /* Generic CSR storage for indices the driver reads/writes. */
        s->csr[index] = val;
        break;
    }

    pcibase_update_irq(s);
}

static uint16_t pcibase_read_bcr(PCIBaseState *s)
{
    uint16_t index = s->rap;
    return s->bcr[index];
}

static void pcibase_write_bcr(PCIBaseState *s, uint16_t val)
{
    uint16_t index = s->rap;

    /* The driver manipulates many BCRs but only interprets a few (2, 4, 9, 18,
     * 20, 25, 26, 27, 32, 33, 49). For our purposes, simple storage is enough. */
    s->bcr[index] = val;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0xffff;

    /* The driver uses 16-bit and 32-bit in/out ops on the same base.
     * We treat size to emulate word vs dword register variants. */

    if (addr < 0x10) {
        /* PROM / station address area: inw/inb(ioaddr + i) in get_regs and
         * probe functions. Reads below 16 bytes return PROM data. */
        if (size == 1) {
            if (addr < sizeof(s->prom)) {
                val = s->prom[addr];
            } else {
                val = 0xff;
            }
        } else if (size == 2) {
            uint16_t off = addr & ~1;
            uint16_t lo = 0xff, hi = 0xff;
            if (off < sizeof(s->prom)) {
                lo = s->prom[off];
                if (off + 1 < sizeof(s->prom)) {
                    hi = s->prom[off + 1];
                }
            }
            val = lo | (hi << 8);
        } else {
            /* 32-bit read from PROM not used; return all-ones. */
            val = 0xffffffffU;
        }
        return val;
    }

    /* NOTE: 16-bit and 32-bit accessors share the same offsets for RDP/RESET.
     * To avoid duplicate case labels on the same numeric value, dispatch on
     * access size inside a single case for each overlapping offset. */
    switch (addr) {
    case PCNET32_WIO_RAP:
        /* word-width RAP */
        if (size == 2) {
            val = s->rap;
        } else if (size == 4) {
            /* DWIO RAP uses the same numeric offset as WIO_RAP in this model. */
            val = s->rap;
        }
        break;
    case PCNET32_WIO_RDP:
        if (size == 2) {
            val = pcibase_read_csr(s);
        } else if (size == 4) {
            /* DWIO RDP shares offset with WIO_RDP, return same CSR. */
            val = pcibase_read_csr(s);
        }
        break;
    case PCNET32_WIO_BDP:
        if (size == 2) {
            val = pcibase_read_bcr(s);
        } else if (size == 4) {
            /* DWIO BDP shares offset with WIO_BDP. */
            val = pcibase_read_bcr(s);
        }
        break;
    case PCNET32_WIO_RESET:
        /* RESET is also reachable via dword access at same offset. */
        if (size == 2 || size == 4) {
            pcibase_soft_reset(s);
            val = 0;
        }
        break;

    default:
        /* Any other PIO reads within BAR range return 0xffff/0xffffffff. */
        if (size == 1) {
            val = 0xff;
        } else if (size == 2) {
            val = 0xffff;
        } else {
            val = 0xffffffffU;
        }
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x10) {
        /* Driver never writes to PROM region, ignore. */
        return;
    }

    /* As in pcibase_pio_read, 16-bit and 32-bit accessors share offsets.
     * We branch on access size within a single case per offset. */
    switch (addr) {
    case PCNET32_WIO_RAP:
        if (size == 2 || size == 4) {
            s->rap = (uint16_t)(val & 0xffff);
        }
        break;
    case PCNET32_WIO_RDP:
        if (size == 2 || size == 4) {
            pcibase_write_csr(s, (uint16_t)(val & 0xffff));
        }
        break;
    case PCNET32_WIO_BDP:
        if (size == 2 || size == 4) {
            pcibase_write_bcr(s, (uint16_t)(val & 0xffff));
        }
        break;
    case PCNET32_WIO_RESET:
        /* Write to RESET not used by driver; ignore for both sizes. */
        break;

    default:
        /* Ignore writes to other offsets within BAR. */
        break;
    }
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
    pcibase_soft_reset(s);
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

    /* BAR Initialization: one I/O BAR as used by the driver */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_PIO;
    s->bar_info[0].size  = PCNET32_TOTAL_IO_SIZE;
    s->bar_info[0].name  = "pcnet32-io";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X are not explicitly used by the driver */
    s->has_msi = false;
    s->has_msix = false;

    /* Initialize internal state, CSRs and PROM. */
    pcibase_soft_reset(s);
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "pcnet32_pci",
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
