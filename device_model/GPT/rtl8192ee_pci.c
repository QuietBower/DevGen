/*
 * QEMU 8.2.10 PCI device model skeleton for rtl8192ee_pci
 * Phase 2: Behavioral implementation based on rtl8192ee driver.
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

#define TYPE_PCIBASE_DEVICE "rtl8192ee_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID   PCI_VENDOR_ID_REALTEK
#define PCIBASE_DEVICE_ID   0x818B
#define PCIBASE_CLASS_ID    PCI_CLASS_NETWORK_OTHER

/* MMIO register offsets used in rtl8192ee driver */
#define REG_SYS_ISO_CTRL            0x0000
#define REG_SYS_FUNC_EN             0x0002
#define REG_SYS_CLKR                0x0008
#define REG_EFUSE_CTRL              0x0030
#define REG_EFUSE_TEST              0x0034
#define REG_SYS_SWR_CTRL1           0x0010
#define REG_SYS_SWR_CTRL2           0x0014
#define REG_AFE_CTRL2               0x0028
#define REG_AFE_CTRL4               0x0078
#define REG_HIMR                    0x00B0
#define REG_HIMRE                   0x00B8
#define REG_EFUSE_ACCESS            0x00CF
#define REG_RETRY_LIMIT             0x042A
#define REG_NAV_UPPER               0x0652
#define REG_CAMCMD                  0x0670
#define REG_CAMWRITE                0x0674
#define REG_CAMREAD                 0x0678
#define REG_CAMDBG                  0x067C
#define REG_SECCFG                  0x0680
#define REG_RXQ_TXBD_IDX            0x03B4
#define REG_BKQ_TXBD_IDX            0x03AC
#define REG_BEQ_TXBD_IDX            0x03A8
#define REG_VIQ_TXBD_IDX            0x03A4
#define REG_MGQ_TXBD_IDX            0x03B0
#define REG_VOQ_TXBD_IDX            0x03A0
#define REG_HI0Q_TXBD_IDX           0x03B8
#define REG_SYS_CFG1                0x00F0
#define REG_HI4Q_TXBD_NUM           0x0394
#define REG_VOQ_TXBD_NUM            0x0384
#define REG_HI2Q_TXBD_NUM           0x0390
#define REG_VIQ_TXBD_NUM            0x0386
#define REG_MGQ_TXBD_NUM            0x0380
#define REG_HI3Q_TXBD_NUM           0x0392
#define REG_HI0Q_TXBD_NUM           0x038C
#define REG_RX_RXBD_NUM             0x0382
#define REG_HI1Q_TXBD_NUM           0x038E
#define REG_TSFTIMER_HCI            0x039C
#define REG_HQ0_DESA                0x0340
#define REG_BKQ_TXBD_NUM            0x038A
#define REG_HI6Q_TXBD_NUM           0x0398
#define REG_BEQ_TXBD_NUM            0x0388
#define REG_HI7Q_TXBD_NUM           0x039A
#define REG_HI5Q_TXBD_NUM           0x0396

/* Bit definitions from driver */
#define AM                          (1 << 2)
#define AB                          (1 << 3)
#define ACRC32                      (1 << 8)
#define ACF                         (1 << 12)
#define AAP                         (1 << 0)

#define PWC_EV12V                   (1 << 15)
#define FEN_ELDR                    (1 << 12)
#define LOADER_CLK_EN               (1 << 5)
#define ANA8M                       (1 << 1)

#define CAM_NONE                    0x0
#define CAM_WEP40                   0x01
#define CAM_TKIP                    0x02
#define CAM_AES                     0x04
#define CAM_WEP104                  0x05

#define IMR_BCNDMAINT6              (1 << 26)
#define IMR_BCNDMAINT5              (1 << 25)
#define IMR_BCNDMAINT4              (1 << 24)
#define IMR_BCNDMAINT3              (1 << 23)
#define IMR_BCNDMAINT2              (1 << 22)
#define IMR_BCNDMAINT1              (1 << 21)
#define IMR_BCNDOK7                 (1 << 20)
#define IMR_BCNDOK6                 (1 << 19)
#define IMR_BCNDOK5                 (1 << 18)
#define IMR_BCNDOK4                 (1 << 17)
#define IMR_BCNDOK3                 (1 << 16)
#define IMR_BCNDOK2                 (1 << 15)
#define IMR_BCNDOK1                 (1 << 14)
#define IMR_TXFOVW                  (1 << 9)
#define IMR_PSTIMEOUT               (1 << 29)
#define IMR_BCNDMAINT0              (1 << 20)
#define IMR_RXFOVW                  (1 << 8)
#define IMR_RDU                     (1 << 1)
#define IMR_ATIMEND                 (1 << 12)
#define IMR_BCNDOK0                 (1 << 16)
#define IMR_MGNTDOK                 (1 << 6)
#define IMR_TBDER                   (1 << 26)
#define IMR_HIGHDOK                 (1 << 7)
#define IMR_TBDOK                   (1 << 25)
#define IMR_BKDOK                   (1 << 5)
#define IMR_BEDOK                   (1 << 4)
#define IMR_VIDOK                   (1 << 3)
#define IMR_VODOK                   (1 << 2)
#define IMR_ROK                     (1 << 0)

#define SYS_CLK                     0x03

#define HWSET_MAX_SIZE              512
#define EFUSE_MAX_SECTION           64
#define EFUSE_REAL_CONTENT_LEN      256
#define EFUSE_OOB_PROTECT_BYTES     18

#define EFUSE_TEST                  REG_EFUSE_TEST
#define EFUSE_CTRL                  REG_EFUSE_CTRL
#define RWCAM                       REG_CAMCMD
#define WCAMI                       REG_CAMWRITE

/* BAR configuration based on rtl92ee_hal_cfg.bar_id = 2 */
#define PCIBASE_MMIO_BAR_INDEX 2


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
    uint8_t regs[0x1000];
    uint32_t himr;
    uint32_t himre;
    uint32_t intr_status; /* pending interrupts (ROK/etc.) */
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->intr_status & s->himr;

    if (pending) {
        if (msi_enabled(pdev)) {
            /* single-vector MSI */
            msi_notify(pdev, 0);
        } else if (!msix_enabled(pdev)) {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev) && !msix_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic placeholder: not used explicitly in provided source */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* The provided sw.c does not show any direct MMIO-exposed DMA engine
     * control registers or ring base address registers being accessed.
     * The detailed DMA descriptor operations are done in host memory by
     * the driver, but no explicit device bus-master start/stop registers
     * are visible here. Therefore, we do not emulate an internal DMA
     * engine at this stage.
     */
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= sizeof(s->regs)) {
        return 0;
    }

    switch (size) {
    case 1:
        val = s->regs[addr];
        break;
    case 2:
        if (addr + 1 < sizeof(s->regs)) {
            val = s->regs[addr] | ((uint16_t)s->regs[addr + 1] << 8);
        }
        break;
    case 4:
        if (addr + 3 < sizeof(s->regs)) {
            val = s->regs[addr] |
                  ((uint32_t)s->regs[addr + 1] << 8) |
                  ((uint32_t)s->regs[addr + 2] << 16) |
                  ((uint32_t)s->regs[addr + 3] << 24);
        }
        break;
    case 8:
        /* device does not use 8-byte registers in provided driver; return 0 */
        val = 0;
        break;
    default:
        val = 0;
        break;
    }

    /* Provide direct shadows for known registers that driver may inspect */
    if (size == 4) {
        switch (addr) {
        case REG_HIMR:
            val = s->himr;
            break;
        case REG_HIMRE:
            val = s->himre;
            break;
        default:
            break;
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= sizeof(s->regs)) {
        return;
    }

    /* Generic shadow register writes */
    switch (size) {
    case 1:
        s->regs[addr] = (uint8_t)val;
        break;
    case 2:
        if (addr + 1 < sizeof(s->regs)) {
            s->regs[addr] = (uint8_t)(val & 0xff);
            s->regs[addr + 1] = (uint8_t)((val >> 8) & 0xff);
        }
        break;
    case 4:
        if (addr + 3 < sizeof(s->regs)) {
            s->regs[addr] = (uint8_t)(val & 0xff);
            s->regs[addr + 1] = (uint8_t)((val >> 8) & 0xff);
            s->regs[addr + 2] = (uint8_t)((val >> 16) & 0xff);
            s->regs[addr + 3] = (uint8_t)((val >> 24) & 0xff);
        }
        break;
    case 8:
        /* not used */
        break;
    default:
        break;
    }

    /* Special handling for key control/interrupt registers */
    if (size == 4) {
        switch (addr) {
        case REG_HIMR:
            /* Interrupt mask register: RW */
            s->himr = (uint32_t)val;
            pcibase_update_irq(s);
            break;
        case REG_HIMRE:
            /* Extended interrupt mask register: RW */
            s->himre = (uint32_t)val;
            pcibase_update_irq(s);
            break;
        default:
            break;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    /* Logic for Port I/O (Legacy support). The rtl8192ee PCI driver
     * uses MMIO via pci_iomap on bar_id=2; no PIO accesses are present
     * in the provided source, so we simply return 0.
     */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;
    /* Logic for Port I/O (Legacy support). Not used by this driver. */
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

    /* Revert registers to power-on defaults */
    memset(s->regs, 0, sizeof(s->regs));
    s->himr = 0;
    s->himre = 0;
    s->intr_status = 0;

    /* Minimal defaults for some registers that the driver may depend on */
    /* SYS_CLKR: ensure clocks appear enabled */
    s->regs[REG_SYS_CLKR] = SYS_CLK;
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

    /* BAR Initialization */
    s->num_bars = 1;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }
    s->bar_info[0].index = PCIBASE_MMIO_BAR_INDEX;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000; /* minimal page-sized MMIO window, actual use defined later */
    s->bar_info[0].name = "rtl8192ee-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize internal state */
    memset(s->regs, 0, sizeof(s->regs));
    s->himr = 0;
    s->himre = 0;
    s->intr_status = 0;

    /* Basic clock/power defaults similar to reset */
    s->regs[REG_SYS_CLKR] = SYS_CLK;
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
    /* Free buffers, stop timers, etc. (none allocated in this model) */
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
