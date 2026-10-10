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


#define TYPE_PCIBASE_DEVICE "3c59x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x10B7
#define DEVICE_ID 0x5900
#define CLASS_ID  PCI_CLASS_NETWORK_ETHERNET

#define EL3_CMD 0x0e
#define EL3_STATUS 0x0e
#define VORTEX_TOTAL_SIZE 0x20
#define BOOMERANG_TOTAL_SIZE 0x40

#define RX_RING_SIZE	32
#define TX_RING_SIZE	16
#define PKT_BUF_SZ		1536
#define LAST_FRAG 	0x80000000

enum vortex_cmd {
    TotalReset = 0<<11, SelectWindow = 1<<11, StartCoax = 2<<11,
    RxDisable = 3<<11, RxEnable = 4<<11, RxReset = 5<<11,
    UpStall = 6<<11, UpUnstall = (6<<11)+1,
    DownStall = (6<<11)+2, DownUnstall = (6<<11)+3,
    RxDiscard = 8<<11, TxEnable = 9<<11, TxDisable = 10<<11, TxReset = 11<<11,
    FakeIntr = 12<<11, AckIntr = 13<<11, SetIntrEnb = 14<<11,
    SetStatusEnb = 15<<11, SetRxFilter = 16<<11, SetRxThreshold = 17<<11,
    SetTxThreshold = 18<<11, SetTxStart = 19<<11,
    StartDMAUp = 20<<11, StartDMADown = (20<<11)+1, StatsEnable = 21<<11,
    StatsDisable = 22<<11, StopCoax = 23<<11, SetFilterBit = 25<<11
};

enum vortex_status {
    IntLatch = 0x0001, HostError = 0x0002, TxComplete = 0x0004,
    TxAvailable = 0x0008, RxComplete = 0x0010, RxEarly = 0x0020,
    IntReq = 0x0040, StatsFull = 0x0080,
    DMADone = 1<<8, DownComplete = 1<<9, UpComplete = 1<<10,
    DMAInProgress = 1<<11,
    CmdInProgress = 1<<12
};

enum Window1 {
    TX_FIFO = 0x10,  RX_FIFO = 0x10,  RxErrors = 0x14,
    RxStatus = 0x18,  Timer=0x1A, TxStatus = 0x1B,
    TxFree = 0x1C
};

enum Window0 {
    Wn0EepromCmd = 10,
    Wn0EepromData = 12,
    IntrStatus=0x0E
};

enum Window2 { Wn2_ResetOptions=12 };
enum Window3 { Wn3_Config=0, Wn3_MaxPktSize=4, Wn3_MAC_Ctrl=6, Wn3_Options=8 };
enum Window4 { Wn4_FIFODiag = 4, Wn4_NetDiag = 6, Wn4_PhysicalMgmt=8, Wn4_Media = 10 };
enum Window7 { Wn7_MasterAddr = 0, Wn7_VlanEtherType=4, Wn7_MasterLen = 6, Wn7_MasterStatus = 12 };

enum MasterCtrl {
    PktStatus = 0x20, DownListPtr = 0x24, FragAddr = 0x28, FragLen = 0x2c,
    TxFreeThreshold = 0x2f, UpPktStatus = 0x30, UpListPtr = 0x38
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
    uint16_t intr_enable;
    uint16_t status_enable;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint16_t window;
    uint16_t el3_status;
    uint16_t el3_cmd;

    /* DMA Context */
    uint32_t rx_ring_dma;
    uint32_t tx_ring_dma;
    uint32_t up_list_ptr;
    uint32_t down_list_ptr;

    uint16_t eeprom_cmd;
    uint16_t eeprom_data;
    uint32_t wn3_config;
    uint16_t wn3_max_pkt_size;
    uint16_t wn3_mac_ctrl;
    uint16_t wn3_options;
    uint16_t wn4_fifo_diag;
    uint16_t wn4_net_diag;
    uint16_t wn4_phy_mgmt;
    uint16_t wn4_media;
    uint32_t wn7_master_addr;
    uint16_t wn7_vlan_ether_type;
    uint16_t wn7_master_len;
    uint16_t wn7_master_status;
    uint16_t wn2_reset_options;
    uint8_t mac[6];
    uint16_t tx_status;
    uint16_t tx_free;
    uint16_t rx_status;
    uint32_t pkt_status;
    uint8_t rx_bad_ssd;
    uint8_t up_stats;
};

struct boom_rx_desc {
    uint32_t next;
    uint32_t status;
    uint32_t addr;
    uint32_t length;
};

struct boom_tx_desc {
    uint32_t next;
    uint32_t status;
    uint32_t addr;
    uint32_t length;
};

static void pcibase_reset(DeviceState *dev);

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->el3_status & s->intr_enable) != 0;
    if (level) {
        s->el3_status |= IntLatch;
    } else {
        s->el3_status &= ~IntLatch;
    }
    pci_set_irq(pdev, level);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (is_write) {
        if (s->down_list_ptr != 0) {
            struct boom_tx_desc desc;
            pci_dma_read(pdev, s->down_list_ptr, &desc, sizeof(desc));
            s->down_list_ptr = le32_to_cpu(desc.next);
            s->el3_status |= DownComplete;
            pcibase_update_irq(s);
        }
    } else {
        if (s->up_list_ptr != 0) {
            struct boom_rx_desc desc;
            pci_dma_read(pdev, s->up_list_ptr, &desc, sizeof(desc));
            desc.status = cpu_to_le32(0x8000 | 64); /* RxDComplete | dummy length */
            pci_dma_write(pdev, s->up_list_ptr, &desc, sizeof(desc));
            s->up_list_ptr = le32_to_cpu(desc.next);
            s->el3_status |= UpComplete;
            pcibase_update_irq(s);
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr <= 0x0D) {
        switch (s->window) {
            case 0:
                if (addr == 10) val = s->eeprom_cmd;
                else if (addr == 12) val = s->eeprom_data;
                break;
            case 2:
                if (addr < 6) {
                    for (int i = 0; i < size && (addr + i) < 6; i++) {
                        val |= ((uint64_t)s->mac[addr + i]) << (i * 8);
                    }
                }
                else if (addr == 12) val = s->wn2_reset_options;
                break;
            case 3:
                if (addr == 0) val = s->wn3_config;
                else if (addr == 4) val = s->wn3_max_pkt_size;
                else if (addr == 6) val = s->wn3_mac_ctrl;
                else if (addr == 8) val = s->wn3_options;
                break;
            case 4:
                if (addr == 4) val = s->wn4_fifo_diag;
                else if (addr == 6) val = s->wn4_net_diag;
                else if (addr == 8) val = s->wn4_phy_mgmt;
                else if (addr == 10) val = s->wn4_media;
                else if (addr == 12) val = s->rx_bad_ssd;
                else if (addr == 13) val = s->up_stats;
                break;
            case 5:
                if (addr == 10) val = s->intr_enable;
                break;
            case 6:
                val = 0;
                break;
            case 7:
                if (addr == 0) val = s->wn7_master_addr;
                else if (addr == 4) val = s->wn7_vlan_ether_type;
                else if (addr == 6) val = s->wn7_master_len;
                else if (addr == 12) val = s->wn7_master_status;
                break;
        }
        return val;
    }

    switch (addr) {
        case EL3_STATUS: val = s->el3_status; break;
        case 0x10: val = 0; break;
        case 0x14: val = 0; break;
        case 0x18: val = s->rx_status; break;
        case 0x1A: val = 0; break;
        case 0x1B: val = s->tx_status; break;
        case 0x1C: val = s->tx_free; break;
        case 0x20: val = s->pkt_status; break;
        case 0x24: val = s->down_list_ptr; break;
        case 0x28: val = 0; break;
        case 0x2C: val = 0; break;
        case 0x2F: val = 0; break;
        case 0x30: val = 0; break;
        case 0x38: val = s->up_list_ptr; break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    if (addr <= 0x0D) {
        switch (s->window) {
            case 0:
                if (addr == 10) {
                    s->eeprom_cmd = val;
                    s->eeprom_cmd &= ~0x8000;
                    int offset = val & 0x3F;
                    uint16_t eeprom[0x40] = {0};
                    eeprom[10] = 0x5452;
                    eeprom[11] = 0x1200;
                    eeprom[12] = 0x3456;
                    eeprom[13] = 0x8000;
                    eeprom[15] = 0x0001;
                    eeprom[16] = 0x0020;
                    eeprom[23] = 0xF225;
                    s->eeprom_data = eeprom[offset];
                }
                break;
            case 2:
                if (addr < 6) {
                    for (int i = 0; i < size && (addr + i) < 6; i++) {
                        s->mac[addr + i] = (val >> (i * 8)) & 0xFF;
                    }
                }
                else if (addr == 12) s->wn2_reset_options = val;
                break;
            case 3:
                if (addr == 0) s->wn3_config = val;
                else if (addr == 4) s->wn3_max_pkt_size = val;
                else if (addr == 6) s->wn3_mac_ctrl = val;
                else if (addr == 8) s->wn3_options = val;
                break;
            case 4:
                if (addr == 4) s->wn4_fifo_diag = val;
                else if (addr == 6) s->wn4_net_diag = val;
                else if (addr == 8) s->wn4_phy_mgmt = val;
                else if (addr == 10) s->wn4_media = val;
                break;
            case 7:
                if (addr == 0) s->wn7_master_addr = val;
                else if (addr == 4) s->wn7_vlan_ether_type = val;
                else if (addr == 6) s->wn7_master_len = val;
                else if (addr == 12) s->wn7_master_status &= ~val;
                break;
        }
        return;
    }

    switch (addr) {
        case EL3_CMD: {
            s->el3_cmd = val;
            int cmd = val & 0xF800;
            switch (cmd) {
                case SelectWindow:
                    s->window = val & 0x7;
                    break;
                case AckIntr:
                    s->el3_status &= ~(val & 0x7FF);
                    pcibase_update_irq(s);
                    break;
                case SetIntrEnb:
                    s->intr_enable = val & 0x7FF;
                    pcibase_update_irq(s);
                    break;
                case SetStatusEnb:
                    s->status_enable = val & 0x7FF;
                    break;
                case TxEnable:
                    s->tx_free = 1536;
                    s->el3_status |= TxAvailable;
                    pcibase_update_irq(s);
                    break;
                case StartDMAUp:
                    if (val == StartDMADown) {
                        s->wn7_master_status |= 0x1000;
                        s->el3_status |= DMADone;
                        pcibase_update_irq(s);
                    } else if (val == StartDMAUp) {
                        s->wn7_master_status &= ~0x8000;
                        s->el3_status |= DMADone;
                        pcibase_update_irq(s);
                    }
                    break;
                case UpStall:
                    if (val == DownStall) {
                        s->el3_status |= DownComplete;
                        pcibase_update_irq(s);
                    } else if (val == DownUnstall) {
                        if (s->down_list_ptr != 0) pcibase_do_dma(s, true);
                    } else if (val == UpUnstall) {
                        if (s->up_list_ptr != 0) pcibase_do_dma(s, false);
                    }
                    break;
                case TotalReset:
                    pcibase_reset(DEVICE(s));
                    break;
            }
            break;
        }
        case 0x20:
            s->pkt_status = val;
            break;
        case 0x24:
            s->down_list_ptr = val;
            if (val != 0) pcibase_do_dma(s, true);
            break;
        case 0x38:
            s->up_list_ptr = val;
            if (val != 0) pcibase_do_dma(s, false);
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

    s->el3_status = 0;
    s->el3_cmd = 0;
    s->intr_enable = 0;
    s->status_enable = 0;
    s->window = 0;
    s->tx_free = 1536;
    s->wn3_config = 0x01000000;
    s->wn3_options = 0x40;
    s->wn4_media = 0;
    s->wn4_net_diag = 0;
    s->wn4_fifo_diag = 0;
    s->wn4_phy_mgmt = 0;
    s->wn7_master_status = 0;
    s->down_list_ptr = 0;
    s->up_list_ptr = 0;
    s->pkt_status = 0;
    s->rx_status = 0;
    s->tx_status = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x10B7 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x5900 );
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
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 64;
    s->bar_info[0].name = "3c59x-io";  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "3c59x_pci",
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
