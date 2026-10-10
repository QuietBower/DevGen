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
#define TX_FIFO_THRESH 256
#define NUM_TX_DESC 4
#define MAX_ETH_FRAME_SIZE 1792
#define TX_BUF_SIZE MAX_ETH_FRAME_SIZE
#define TX_BUF_TOT_LEN (TX_BUF_SIZE * NUM_TX_DESC)

#define HW_REVID(b30, b29, b28, b27, b26, b23, b22) \
	(b30<<30 | b29<<29 | b28<<28 | b27<<27 | b26<<26 | b23<<23 | b22<<22)

#define RX_BUF_IDX	2
#define RX_BUF_PAD	16
#define RX_BUF_WRAP_PAD 2048

#define TYPE_PCIBASE_DEVICE "8139too_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x10ec
#define DEVICE_ID 0x8139
#define CLASS_ID PCI_CLASS_NETWORK_ETHERNET

enum RTL8139_registers {
    MAC0        = 0,
    MAR0        = 8,
    TxStatus0   = 0x10,
    TxAddr0     = 0x20,
    RxBuf       = 0x30,
    ChipCmd     = 0x37,
    RxBufPtr    = 0x38,
    RxBufAddr   = 0x3A,
    IntrMask    = 0x3C,
    IntrStatus  = 0x3E,
    TxConfig    = 0x40,
    RxConfig    = 0x44,
    Timer       = 0x48,
    RxMissed    = 0x4C,
    Cfg9346     = 0x50,
    Config0     = 0x51,
    Config1     = 0x52,
    TimerInt    = 0x54,
    MediaStatus = 0x58,
    Config3     = 0x59,
    Config4     = 0x5A,
    HltClk      = 0x5B,
    MultiIntr   = 0x5C,
    TxSummary   = 0x60,
    BasicModeCtrl   = 0x62,
    BasicModeStatus = 0x64,
    NWayAdvert  = 0x66,
    NWayLPAR    = 0x68,
    NWayExpansion   = 0x6A,
    FIFOTMS     = 0x70,
    CSCR        = 0x74,
    PARA78      = 0x78,
    FlashReg    = 0xD4,
    PARA7c      = 0x7c,
    Config5     = 0xD8,
};

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
    uint16_t intr_status;
    uint16_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t mac[6];
    uint8_t mar[8];
    uint32_t tx_status[4];
    uint32_t tx_addr[4];
    uint32_t rx_buf;
    uint8_t chip_cmd;
    uint16_t rx_buf_ptr;
    uint32_t rx_buf_addr;
    uint32_t tx_config;
    uint32_t rx_config;
    uint32_t timer;
    uint32_t rx_missed;
    uint8_t cfg9346;
    uint8_t config0;
    uint8_t config1;
    uint8_t config3;
    uint8_t config4;
    uint8_t config5;
    uint32_t timer_int;
    uint8_t media_status;
    uint8_t hlt_clk;
    uint16_t multi_intr;
    uint32_t tx_summary;
    uint16_t basic_mode_ctrl;
    uint16_t basic_mode_status;
    uint16_t nway_advert;
    uint16_t nway_lpar;
    uint16_t nway_expansion;
    uint32_t fifotms;
    uint32_t cscr;
    uint32_t para78;
    uint32_t flash_reg;
    uint32_t para7c;

    /* DMA Context */
    uint32_t rx_ring_dma;
    uint32_t tx_bufs_dma[4];

};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->intr_status & s->intr_mask) {
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
            uint32_t status = s->tx_status[i];
            uint32_t addr = s->tx_addr[i];
            uint32_t len = status & 0x1FFF;
            if (len > 0 && len <= 1792) {
                uint8_t buf[1792];
                pci_dma_read(pdev, addr, buf, len);
                /* Note: TxStatOK and TxOK macros are missing, so we cannot fully complete the TX state machine here yet. */
            }
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
        case MAC0: return *(uint32_t*)&s->mac[0];
        case MAC0 + 4: return *(uint16_t*)&s->mac[4];
        case MAR0: return *(uint32_t*)&s->mar[0];
        case MAR0 + 4: return *(uint32_t*)&s->mar[4];
        case TxStatus0: return s->tx_status[0];
        case TxStatus0 + 4: return s->tx_status[1];
        case TxStatus0 + 8: return s->tx_status[2];
        case TxStatus0 + 12: return s->tx_status[3];
        case TxAddr0: return s->tx_addr[0];
        case TxAddr0 + 4: return s->tx_addr[1];
        case TxAddr0 + 8: return s->tx_addr[2];
        case TxAddr0 + 12: return s->tx_addr[3];
        case RxBuf: return s->rx_buf;
        case ChipCmd: return s->chip_cmd;
        case RxBufPtr: return s->rx_buf_ptr;
        case RxBufAddr: return s->rx_buf_addr;
        case IntrMask: return s->intr_mask;
        case IntrStatus: return s->intr_status;
        case TxConfig: return s->tx_config;
        case RxConfig: return s->rx_config;
        case Timer: return s->timer;
        case RxMissed: return s->rx_missed;
        case Cfg9346: return s->cfg9346;
        case Config0: return s->config0;
        case Config1: return s->config1;
        case Config3: return s->config3;
        case Config4: return s->config4;
        case Config5: return s->config5;
        case TimerInt: return s->timer_int;
        case MediaStatus: return s->media_status;
        case HltClk: return s->hlt_clk;
        case MultiIntr: return s->multi_intr;
        case TxSummary: return s->tx_summary;
        case BasicModeCtrl: return s->basic_mode_ctrl;
        case BasicModeStatus: return s->basic_mode_status;
        case NWayAdvert: return s->nway_advert;
        case NWayLPAR: return s->nway_lpar;
        case NWayExpansion: return s->nway_expansion;
        case FIFOTMS: return s->fifotms;
        case CSCR: return s->cscr;
        case PARA78: return s->para78;
        case FlashReg: return s->flash_reg;
        case PARA7c: return s->para7c;
    }
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
        case MAC0: *(uint32_t*)&s->mac[0] = val; break;
        case MAC0 + 4: *(uint16_t*)&s->mac[4] = val; break;
        case MAR0: *(uint32_t*)&s->mar[0] = val; break;
        case MAR0 + 4: *(uint32_t*)&s->mar[4] = val; break;
        case TxStatus0: s->tx_status[0] = val; pcibase_do_dma(s, true); break;
        case TxStatus0 + 4: s->tx_status[1] = val; pcibase_do_dma(s, true); break;
        case TxStatus0 + 8: s->tx_status[2] = val; pcibase_do_dma(s, true); break;
        case TxStatus0 + 12: s->tx_status[3] = val; pcibase_do_dma(s, true); break;
        case TxAddr0: s->tx_addr[0] = val; break;
        case TxAddr0 + 4: s->tx_addr[1] = val; break;
        case TxAddr0 + 8: s->tx_addr[2] = val; break;
        case TxAddr0 + 12: s->tx_addr[3] = val; break;
        case RxBuf: s->rx_buf = val; s->rx_ring_dma = val; break;
        case ChipCmd: s->chip_cmd = 0; break; /* Auto-clear reset to satisfy driver polling */
        case RxBufPtr: s->rx_buf_ptr = val; break;
        case RxBufAddr: s->rx_buf_addr = val; break;
        case IntrMask: s->intr_mask = val; pcibase_update_irq(s); break;
        case IntrStatus: s->intr_status &= ~val; pcibase_update_irq(s); break; /* W1C */
        case TxConfig: s->tx_config = val; break;
        case RxConfig: s->rx_config = val; break;
        case Timer: s->timer = val; break;
        case RxMissed: s->rx_missed = val; break;
        case Cfg9346: s->cfg9346 = val; break;
        case Config0: s->config0 = val; break;
        case Config1: s->config1 = val; break;
        case Config3: s->config3 = val; break;
        case Config4: s->config4 = val; break;
        case Config5: s->config5 = val; break;
        case TimerInt: s->timer_int = val; break;
        case MediaStatus: s->media_status = val; break;
        case HltClk: s->hlt_clk = val; break;
        case MultiIntr: s->multi_intr = val; break;
        case TxSummary: s->tx_summary = val; break;
        case BasicModeCtrl: s->basic_mode_ctrl = val; break;
        case BasicModeStatus: s->basic_mode_status = val; break;
        case NWayAdvert: s->nway_advert = val; break;
        case NWayLPAR: s->nway_lpar = val; break;
        case NWayExpansion: s->nway_expansion = val; break;
        case FIFOTMS: s->fifotms = val; break;
        case CSCR: s->cscr = val; break;
        case PARA78: s->para78 = val; break;
        case FlashReg: s->flash_reg = val; break;
        case PARA7c: s->para7c = val; break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return pcibase_mmio_read(opaque, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_mmio_write(opaque, addr, val, size);
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

    memset(s->mac, 0, sizeof(s->mac));
    memset(s->mar, 0, sizeof(s->mar));
    memset(s->tx_status, 0, sizeof(s->tx_status));
    memset(s->tx_addr, 0, sizeof(s->tx_addr));
    s->rx_buf = 0;
    s->chip_cmd = 0;
    s->rx_buf_ptr = 0;
    s->rx_buf_addr = 0;
    s->intr_mask = 0;
    s->intr_status = 0;
    s->tx_config = 0;
    s->rx_config = 0;
    s->timer = 0;
    s->rx_missed = 0;
    s->cfg9346 = 0;
    s->config0 = 0;
    s->config1 = 0;
    s->config3 = 0;
    s->config4 = 0;
    s->config5 = 0;
    s->timer_int = 0;
    s->media_status = 0;
    s->hlt_clk = 0;
    s->multi_intr = 0;
    s->tx_summary = 0;
    s->basic_mode_ctrl = 0;
    s->basic_mode_status = 0;
    s->nway_advert = 0;
    s->nway_lpar = 0;
    s->nway_expansion = 0;
    s->fifotms = 0;
    s->cscr = 0;
    s->para78 = 0;
    s->flash_reg = 0;
    s->para7c = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x10ec );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x8139 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
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
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = 256, .name = "rtl8139-pio" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 256, .name = "rtl8139-mmio" };  
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
    .name = "8139too_pci",
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
