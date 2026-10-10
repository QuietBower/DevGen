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
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"
#include <string.h>

/* Additional include files retrieved from driver context */

#define TYPE_PCIBASE_DEVICE "rtl8192ee_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_REALTEK         0x10EC
#define RTL8192EE_DEVICE_ID           0x818B
#define PCI_CLASS_NETWORK_OTHER       0x0280

/* BAR2 size placeholder; exact size is unknown from provided source */
#define RTL8192EE_BAR2_SIZE           0x1000  /* TODO: verify correct size */

/* Known register offsets from the driver */
#define REG_SYS_ISO_CTRL              0x0000
#define REG_SYS_FUNC_EN               0x0002
#define REG_SYS_CLKR                  0x0008
#define REG_HIMR                      0x0120
#define REG_HIMRE                     0x0128
#define REG_EFUSE_ACCESS              0x00CF
#define REG_EFUSE_TEST                0x0034
#define REG_EFUSE_CTRL                0x0030
#define REG_CAMCMD                    0x0670
#define REG_CAMWRITE                  0x0674
#define REG_CAMREAD                   0x0678
#define REG_CAMDBG                    0x067C
#define REG_SECCFG                    0x0680
#define REG_AFE_CTRL4                 0x0078
#define REG_SYS_SWR_CTRL1             0x0010
#define REG_SYS_SWR_CTRL2             0x0014
#define REG_AFE_CTRL2                 0x0028
#define REG_RXQ_TXBD_IDX              0x03B4
#define REG_RETRY_LIMIT               0x042A
#define REG_BKQ_TXBD_IDX              0x03AC
#define REG_VIQ_TXBD_IDX              0x03A4
#define REG_MGQ_TXBD_IDX              0x03B0
#define REG_BEQ_TXBD_IDX              0x03A8
#define REG_VOQ_TXBD_IDX              0x03A0
#define REG_HI0Q_TXBD_IDX             0x03B8

/* Bitfield defines */
#define AM                            BIT(2)
#define AB                            BIT(3)
#define ACRC32                        BIT(8)
#define ACF                           BIT(12)
#define AAP                           BIT(0)
#define PWC_EV12V                     BIT(15)
#define FEN_ELDR                      BIT(12)
#define LOADER_CLK_EN                 BIT(5)
#define ANA8M                         BIT(1)

/* Interrupt Mask Register bits */
#define IMR_BCNDMAINT6                BIT(31)
#define IMR_BCNDMAINT5                BIT(30)
#define IMR_BCNDMAINT4                BIT(29)
#define IMR_BCNDMAINT3                BIT(28)
#define IMR_BCNDMAINT2                BIT(27)
#define IMR_BCNDMAINT1                BIT(26)
#define IMR_BCNDOK7                   BIT(24)
#define IMR_BCNDOK6                   BIT(23)
#define IMR_BCNDOK5                   BIT(22)
#define IMR_BCNDOK4                   BIT(21)
#define IMR_BCNDOK3                   BIT(20)
#define IMR_BCNDOK2                   BIT(19)
#define IMR_BCNDOK1                   BIT(18)
#define IMR_TXFOVW                    BIT(15)
#define IMR_PSTIMEOUT                 BIT(14)
#define IMR_BCNDMAINT0                BIT(20)
#define IMR_RXFOVW                    BIT(12)
#define IMR_RDU                       BIT(11)
#define IMR_ATIMEND                   BIT(10)
#define IMR_BCNDOK0                   BIT(16)
#define IMR_MGNTDOK                   BIT(6)
#define IMR_TBDER                     BIT(5)
#define IMR_HIGHDOK                   BIT(8)
#define IMR_TBDOK                     BIT(7)
#define IMR_BKDOK                     BIT(4)
#define IMR_BEDOK                     BIT(3)
#define IMR_VIDOK                     BIT(2)
#define IMR_VODOK                     BIT(1)
#define IMR_ROK                       BIT(0)

/* EFUSE constants */
#define HWSET_MAX_SIZE                128
#define EFUSE_MAX_SECTION             16
#define EFUSE_REAL_CONTENT_LEN        512
#define EFUSE_OOB_PROTECT_BYTES       15

/* CAM constants */
#define CAM_NONE                      0x0
#define CAM_WEP40                     0x01
#define CAM_TKIP                      0x02
#define CAM_AES                       0x04
#define CAM_WEP104                    0x05

/*
 * Many register offset macros may still be missing from the provided source.
 * They will be added in later iterations if discovered.
 */

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
    uint32_t irq_mask[4];
    uint32_t sys_irq_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t *mmio_data;   /* pointer to allocated MMIO space */

    /* EFUSE emulation */
    uint8_t efuse_data[EFUSE_REAL_CONTENT_LEN];
    uint32_t efuse_addr;

    uint32_t status;      /* Generic status flags */

    /* State used to handle reset sequences */
    /* No specific reset logic extracted */

    /* Power management state (D0-D3) */
    bool power_on;

    /* Additional driver-specific fields */
    /* (none) */
};

/*
 * Hardware descriptor structures defined as in the driver, for later use.
 */
typedef struct { uint32_t dword[16]; } rtl_tx_desc;
typedef struct { uint32_t dword[8];  } rtl_rx_desc;
typedef struct { uint32_t dword[4];  } rtl_rx_buffer_desc;
typedef struct { uint32_t dword[8];  } rtl_tx_buffer_desc;  /* BUFDESC_SEG_NUM=1 => size 8*4=32 bytes */

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending_irqs = 0;

    /* Combine interrupt status bits that are unmasked.
     * In a real device, the interrupt status register would be read separately.
     * For minimal emulation, we ignore actual pending status since the driver
     * sets up interrupts later and does not expect them during probe.
     */
    /* Example: pending_irqs = s->status & s->irq_mask[0] */

    if (pending_irqs) {
        if (s->has_msi && msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!s->has_msi && !msix_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Default: read from shadow memory */
    if (addr + size > RTL8192EE_BAR2_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds read offset 0x%" HWADDR_PRIx " size %u\n",
                      __func__, addr, size);
        return ~0ULL;
    }
    memcpy(&val, s->mmio_data + addr, size);

    /* Special registers modeling */
    switch (addr) {
    case REG_EFUSE_TEST:
        if (size == 4) {
            /* Read from efuse data at currently set address */
            uint32_t offset = s->efuse_addr;
            if (offset < EFUSE_REAL_CONTENT_LEN) {
                val = 0;
                memcpy(&val, s->efuse_data + offset, MIN(size, EFUSE_REAL_CONTENT_LEN - offset));
            }
        }
        break;
    case REG_EFUSE_ACCESS:
        /* Similar to EFUSE_TEST, but used as a data port? */
        if (size == 4) {
            uint32_t offset = s->efuse_addr;
            if (offset < EFUSE_REAL_CONTENT_LEN) {
                val = 0;
                memcpy(&val, s->efuse_data + offset, MIN(size, EFUSE_REAL_CONTENT_LEN - offset));
            }
        }
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Default: write to shadow memory */
    if (addr + size > RTL8192EE_BAR2_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds write offset 0x%" HWADDR_PRIx " size %u\n",
                      __func__, addr, size);
        return;
    }
    memcpy(s->mmio_data + addr, &val, size);

    /* Handle side effects for specific registers */
    switch (addr) {
    case REG_HIMR:
        if (size == 4) {
            s->irq_mask[0] = val;
            pcibase_update_irq(s);
        }
        break;
    case REG_HIMRE:
        if (size == 4) {
            s->irq_mask[1] = val;
            pcibase_update_irq(s);
        }
        break;
    case REG_EFUSE_CTRL:
        if (size == 4) {
            s->efuse_addr = val;
        }
        break;
    case REG_EFUSE_TEST:
        if (size == 4) {
            /* Write data into efuse at current address */
            uint32_t offset = s->efuse_addr;
            if (offset < EFUSE_REAL_CONTENT_LEN) {
                memcpy(s->efuse_data + offset, &val, MIN(size, EFUSE_REAL_CONTENT_LEN - offset));
            }
        }
        break;
    case REG_EFUSE_ACCESS:
        if (size == 4) {
            uint32_t offset = s->efuse_addr;
            if (offset < EFUSE_REAL_CONTENT_LEN) {
                memcpy(s->efuse_data + offset, &val, MIN(size, EFUSE_REAL_CONTENT_LEN - offset));
            }
        }
        break;
    default:
        break;
    }
}

/* PIO handlers are not used by this driver, so they are omitted. */

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset MMIO registers to zero */
    if (s->mmio_data) {
        memset(s->mmio_data, 0, RTL8192EE_BAR2_SIZE);
    }
    memset(s->efuse_data, 0, EFUSE_REAL_CONTENT_LEN);
    /* Initialize efuse with a minimal valid header to avoid driver checksum failures.
     * The exact format is unknown; this sets a common pattern seen in some Realtek chips.
     * First 16 bytes: 0x01, 0x00, ..., attempt to mimic a valid efuse map header.
     */
    s->efuse_data[0] = 0x01;
    s->efuse_data[1] = 0x00;
    /* ... rest are zeros; adjust if needed based on driver feedback */
    s->efuse_addr = 0;
    s->status = 0;
    s->power_on = true;
    memset(s->irq_mask, 0, sizeof(s->irq_mask));
    s->sys_irq_mask = 0;
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
        /* memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size); */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_REALTEK );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  RTL8192EE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_OTHER );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) {
        .index = 2,
        .type = BAR_TYPE_MMIO,
        .size = RTL8192EE_BAR2_SIZE,
        .name = "rtl8192ee-bar2"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI initialization (driver requests MSI) */
    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        error_setg(errp, "Failed to initialize MSI");
        return;
    }
    s->has_msi = true;

    /* Allocate MMIO register space */
    s->mmio_data = g_malloc0(RTL8192EE_BAR2_SIZE);
    if (!s->mmio_data) {
        error_setg(errp, "Failed to allocate MMIO memory");
        return;
    }

    /* DMA configuration is not set (no pci_set_dma* APIs allowed) */
    /* Timer configuration: none */
    /* Initial field state */
    s->power_on = true;
    memset(s->irq_mask, 0, sizeof(s->irq_mask));
    s->sys_irq_mask = 0;
    s->status = 0;
    s->efuse_addr = 0;
    memcpy(s->efuse_data, ((uint8_t[]){0x01, 0x00}), 2);  /* minimal init */
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

    g_free(s->mmio_data);
    /* additional cleanup: none */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "rtl8192ee_pci",
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
