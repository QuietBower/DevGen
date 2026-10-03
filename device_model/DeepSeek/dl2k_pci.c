/*
 * QEMU emulation of D-Link DL2K Gigabit Ethernet Controller.
 * Derived from Linux driver source (dl2k.c).
 * Provides MMIO registers, EEPROM, and MII PHY emulation for probing success.
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

#define TYPE_PCIBASE_DEVICE "dl2k_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor/Device IDs */
#define VENDOR_ID 0x1186
#define DEVICE_ID 0x4000
#define CLASS_ID 0x0200

/* Interrupt Status Register Bit Masks (from driver enum IntrStatus) */
#define INTR_TxIdle       0x40000
#define INTR_RxIdle       0x20000
#define INTR_IntrSummary  0x010000
#define INTR_PCIBusErr170 0x7000
#define INTR_PCIBusErr175 0x1000
#define INTR_PhyEvent175  0x8000
#define INTR_RxStarted    0x0800
#define INTR_RxEarlyWarn  0x0400
#define INTR_CntFull      0x0200
#define INTR_TxUnderrun   0x0100
#define INTR_TxEmpty      0x0080
#define INTR_TxDone       0x0020
#define INTR_RxError      0x0010
#define INTR_RxOverflow   0x0008
#define INTR_RxFull       0x0004
#define INTR_RxHeader     0x0002
#define INTR_RxDone       0x0001

/* EEPROM constants */
#define EEP_READ 0x02
#define EEP_BUSY 0x01

/* MII bit-banging constants */
#define MII_CLK   0x01
#define MII_DATA1 0x02
#define MII_WRITE 0x04
#define MII_READ  0x00

/* ASIC Reset bits */
#define GLOBAL_RESET  0x01
#define DMA_RESET     0x02
#define FIFO_RESET    0x04
#define NETWORK_RESET 0x08
#define HOST_RESET    0x10
#define RESET_BUSY    0x8000

/* MII state machine states */
#define MII_IDLE          0
#define MII_PREAMBLE_END  1
#define MII_COMMAND       2
#define MII_DATA          3
#define MII_WRITE_DATA    4

/* BAR0 size */
#define BAR0_SIZE 0x400

/* Standard Ethernet CRC32 (polynomial 0xEDB88320) - returns non-inverted value */
static uint32_t ether_crc_le(const uint8_t *data, size_t len)
{
    const uint32_t poly = 0xEDB88320;
    uint32_t crc = ~0;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            uint32_t mask = -(crc & 1);
            crc = (crc >> 1) ^ (poly & mask);
        }
    }
    return crc;
}

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows */
    uint16_t eeprom_ctrl;
    uint16_t eeprom_data;
    uint8_t  phy_ctrl;
    uint32_t debug_ctrl;
    uint32_t mac_ctrl;
    uint32_t tx_status;
    uint32_t tfd_list_ptr0;
    uint32_t tfd_list_ptr1;
    uint32_t rfd_list_ptr0;
    uint32_t rfd_list_ptr1;
    uint16_t station_addr[3];
    uint32_t hash_table[2];
    uint16_t receive_mode;
    uint16_t vlan_id;
    uint32_t vlan_tag;
    uint32_t count_down;
    uint32_t dma_ctrl;
    uint32_t rx_dma_int_ctrl;
    uint8_t  rx_dma_poll_period;
    uint8_t  tx_dma_poll_period;
    uint8_t  rx_dma_burst_thresh;
    uint8_t  rx_dma_urgent_thresh;
    uint32_t rmon_stat_mask;
    uint16_t max_frame_size;
    uint16_t tx_start_thresh;
    uint16_t asic_ctrl_low;
    uint16_t asic_ctrl_high;

    /* EEPROM internal data */
    uint8_t eeprom_data_array[256];

    /* MII emulation state */
    int mii_state;
    int mii_preamble_cnt;
    int mii_bit_cnt;
    uint32_t mii_cmd_shift;
    uint32_t mii_rd_shift;
    uint32_t mii_wr_shift;
    int mii_out_bit;
    int mii_phy_addr;
    int mii_reg_num;
    uint16_t phy_regs[32];

    /* Operational status flags */

    /* State used to handle reset sequences */

    /* Power management state (D0-D3) */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->intr_status & s->intr_mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0x00: /* EepromCtrl */
        if (size == 2) val = s->eeprom_ctrl;
        break;
    case 0x04: /* EepromData */
        if (size == 2) val = s->eeprom_data;
        break;
    case 0x08: /* ASICCtrl low */
        if (size == 2) val = s->asic_ctrl_low;
        break;
    case 0x0A: /* ASICCtrl high (ASICCtrl+2) */
        if (size == 2) val = s->asic_ctrl_high;
        break;
    case 0x0C: /* PhyCtrl */
        if (size == 1) {
            val = (s->phy_ctrl & 0xF8) | (s->mii_out_bit << 1);
        }
        break;
    case 0x10: /* DebugCtrl */
        if (size == 4) val = s->debug_ctrl;
        break;
    case 0x14: /* MACCtrl */
        if (size == 4) val = s->mac_ctrl;
        else if (size == 2) val = s->mac_ctrl & 0xFFFF;
        break;
    case 0x18: /* IntStatus */
        if (size == 2) val = s->intr_status;
        break;
    case 0x1A: /* IntEnable */
        if (size == 2) val = s->intr_mask;
        break;
    case 0x1C: /* TxStatus */
        if (size == 4) val = s->tx_status;
        break;
    case 0x20: /* TFDListPtr0 */
        if (size == 4) val = s->tfd_list_ptr0;
        break;
    case 0x24: /* TFDListPtr1 */
        if (size == 4) val = s->tfd_list_ptr1;
        break;
    case 0x28: /* RFDListPtr0 */
        if (size == 4) val = s->rfd_list_ptr0;
        break;
    case 0x2C: /* RFDListPtr1 */
        if (size == 4) val = s->rfd_list_ptr1;
        break;
    case 0x30: /* StationAddr0 */
        if (size == 2) val = s->station_addr[0];
        break;
    case 0x32:
        if (size == 2) val = s->station_addr[1];
        break;
    case 0x34:
        if (size == 2) val = s->station_addr[2];
        break;
    case 0x38: /* HashTable0 */
        if (size == 4) val = s->hash_table[0];
        break;
    case 0x3C: /* HashTable1 */
        if (size == 4) val = s->hash_table[1];
        break;
    case 0x40: /* ReceiveMode */
        if (size == 2) val = s->receive_mode;
        break;
    case 0x44: /* VLANId */
        if (size == 2) val = s->vlan_id;
        break;
    case 0x48: /* VLANTag */
        if (size == 4) val = s->vlan_tag;
        break;
    case 0x4C: /* CountDown */
        if (size == 4) val = s->count_down;
        break;
    case 0x50: /* DMACtrl */
        if (size == 4) val = s->dma_ctrl;
        break;
    case 0x54: /* RxDMAIntCtrl */
        if (size == 4) val = s->rx_dma_int_ctrl;
        break;
    case 0x58: /* RxDMAPollPeriod */
        if (size == 1) val = s->rx_dma_poll_period;
        break;
    case 0x59: /* TxDMAPollPeriod */
        if (size == 1) val = s->tx_dma_poll_period;
        break;
    case 0x5A: /* RxDMABurstThresh */
        if (size == 1) val = s->rx_dma_burst_thresh;
        break;
    case 0x5B: /* RxDMAUrgentThresh */
        if (size == 1) val = s->rx_dma_urgent_thresh;
        break;
    case 0x5C: /* RmonStatMask */
        if (size == 4) val = s->rmon_stat_mask;
        break;
    case 0x60: /* MaxFrameSize */
        if (size == 2) val = s->max_frame_size;
        break;
    case 0x64: /* TxStartThresh */
        if (size == 2) val = s->tx_start_thresh;
        break;
    default:
        /* Statistics and RMON regions: return 0 */
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case 0x00: /* EepromCtrl */
        if (size == 2) {
            s->eeprom_ctrl = val;
            if (val & EEP_READ) {
                uint8_t eep_addr = val & 0xff;
                if (eep_addr < 128) {
                    s->eeprom_data = s->eeprom_data_array[eep_addr*2] |
                                     (s->eeprom_data_array[eep_addr*2+1] << 8);
                    s->eeprom_ctrl &= ~EEP_BUSY;
                }
            }
        }
        break;
    case 0x04: /* EepromData - read-only for driver */
        break;
    case 0x08: /* ASICCtrl low */
        if (size == 2) s->asic_ctrl_low = val;
        break;
    case 0x0A: /* ASICCtrl high (reset) */
        if (size == 2) {
            s->asic_ctrl_high = val;
            /* ResetBusy is never set, so read returns 0 immediately */
        }
        break;
    case 0x0C: /* PhyCtrl */
        if (size == 1) {
            s->phy_ctrl = val;
            if (val & MII_WRITE) {
                if (val & MII_CLK) {
                    int bit = (val & MII_DATA1) ? 1 : 0;
                    if (s->mii_state == MII_IDLE) {
                        if (bit == 1) {
                            s->mii_preamble_cnt++;
                            if (s->mii_preamble_cnt >= 32) {
                                s->mii_state = MII_PREAMBLE_END;
                            }
                        } else {
                            s->mii_preamble_cnt = 0;
                        }
                    } else if (s->mii_state == MII_PREAMBLE_END) {
                        if (bit == 0) {
                            s->mii_state = MII_COMMAND;
                            s->mii_bit_cnt = 0;
                            s->mii_cmd_shift = 0;
                        } else {
                            s->mii_state = MII_IDLE;
                            s->mii_preamble_cnt = 0;
                        }
                    } else if (s->mii_state == MII_COMMAND) {
                        s->mii_cmd_shift = (s->mii_cmd_shift << 1) | bit;
                        s->mii_bit_cnt++;
                        if (s->mii_bit_cnt == 13) {
                            uint32_t st = (s->mii_cmd_shift >> 12) & 1;
                            if (st == 1) {
                                uint32_t op = (s->mii_cmd_shift >> 10) & 3;
                                s->mii_phy_addr = (s->mii_cmd_shift >> 5) & 0x1F;
                                s->mii_reg_num = s->mii_cmd_shift & 0x1F;
                                if (op == 2) { /* read */
                                    uint16_t value = s->phy_regs[s->mii_reg_num & 0x1F];
                                    uint32_t out = 3 << 16; /* TA=1,0 */
                                    for (int i = 15; i >= 0; i--) {
                                        out = (out << 1) | ((value >> i) & 1);
                                    }
                                    s->mii_rd_shift = out;
                                    s->mii_state = MII_DATA;
                                    s->mii_bit_cnt = 0;
                                } else if (op == 1) { /* write */
                                    s->mii_state = MII_WRITE_DATA;
                                    s->mii_bit_cnt = 0;
                                    s->mii_wr_shift = 0;
                                } else {
                                    s->mii_state = MII_IDLE;
                                    s->mii_preamble_cnt = 0;
                                }
                            } else {
                                s->mii_state = MII_IDLE;
                                s->mii_preamble_cnt = 0;
                            }
                        }
                    } else if (s->mii_state == MII_WRITE_DATA) {
                        s->mii_wr_shift = (s->mii_wr_shift << 1) | bit;
                        s->mii_bit_cnt++;
                        if (s->mii_bit_cnt == 18) {
                            uint32_t ta = (s->mii_wr_shift >> 16) & 3;
                            uint16_t data = s->mii_wr_shift & 0xFFFF;
                            if (ta == 2) {
                                s->phy_regs[s->mii_reg_num & 0x1F] = data;
                            }
                            s->mii_state = MII_IDLE;
                            s->mii_preamble_cnt = 0;
                        }
                    }
                }
            } else { /* Read mode (MII_WRITE not set) */
                if (val & MII_CLK) {
                    if (s->mii_state == MII_DATA) {
                        if (s->mii_bit_cnt < 18) {
                            int out_bit = (s->mii_rd_shift >> (17 - s->mii_bit_cnt)) & 1;
                            s->mii_out_bit = out_bit;
                            s->mii_bit_cnt++;
                            if (s->mii_bit_cnt == 18) {
                                s->mii_state = MII_IDLE;
                                s->mii_preamble_cnt = 0;
                            }
                        }
                    } else {
                        s->mii_out_bit = 1;
                    }
                }
            }
        }
        break;
    case 0x10: /* DebugCtrl */
        if (size == 4) s->debug_ctrl = val;
        break;
    case 0x14: /* MACCtrl */
        if (size == 4) s->mac_ctrl = val;
        else if (size == 2) {
            s->mac_ctrl = (s->mac_ctrl & 0xFFFF0000) | (val & 0xFFFF);
        }
        break;
    case 0x18: /* IntStatus (write-1-to-clear) */
        if (size == 2) {
            s->intr_status &= ~val;
            pcibase_update_irq(s);
        }
        break;
    case 0x1A: /* IntEnable */
        if (size == 2) {
            s->intr_mask = val;
            pcibase_update_irq(s);
        }
        break;
    case 0x1C: /* TxStatus */
        if (size == 4) s->tx_status = val;
        break;
    case 0x20: /* TFDListPtr0 */
        if (size == 4) s->tfd_list_ptr0 = val;
        break;
    case 0x24: /* TFDListPtr1 */
        if (size == 4) s->tfd_list_ptr1 = val;
        break;
    case 0x28: /* RFDListPtr0 */
        if (size == 4) s->rfd_list_ptr0 = val;
        break;
    case 0x2C: /* RFDListPtr1 */
        if (size == 4) s->rfd_list_ptr1 = val;
        break;
    case 0x30: /* StationAddr */
        if (size == 2) s->station_addr[0] = val;
        break;
    case 0x32:
        if (size == 2) s->station_addr[1] = val;
        break;
    case 0x34:
        if (size == 2) s->station_addr[2] = val;
        break;
    case 0x38: /* HashTable0 */
        if (size == 4) s->hash_table[0] = val;
        break;
    case 0x3C: /* HashTable1 */
        if (size == 4) s->hash_table[1] = val;
        break;
    case 0x40: /* ReceiveMode */
        if (size == 2) s->receive_mode = val;
        break;
    case 0x44: /* VLANId */
        if (size == 2) s->vlan_id = val;
        break;
    case 0x48: /* VLANTag */
        if (size == 4) s->vlan_tag = val;
        break;
    case 0x4C: /* CountDown */
        if (size == 4) s->count_down = val;
        break;
    case 0x50: /* DMACtrl */
        if (size == 4) s->dma_ctrl = val;
        break;
    case 0x54: /* RxDMAIntCtrl */
        if (size == 4) s->rx_dma_int_ctrl = val;
        break;
    case 0x58: /* RxDMAPollPeriod */
        if (size == 1) s->rx_dma_poll_period = val;
        break;
    case 0x59: /* TxDMAPollPeriod */
        if (size == 1) s->tx_dma_poll_period = val;
        break;
    case 0x5A: /* RxDMABurstThresh */
        if (size == 1) s->rx_dma_burst_thresh = val;
        break;
    case 0x5B: /* RxDMAUrgentThresh */
        if (size == 1) s->rx_dma_urgent_thresh = val;
        break;
    case 0x5C: /* RmonStatMask */
        if (size == 4) s->rmon_stat_mask = val;
        break;
    case 0x60: /* MaxFrameSize */
        if (size == 2) s->max_frame_size = val;
        break;
    case 0x64: /* TxStartThresh */
        if (size == 2) s->tx_start_thresh = val;
        break;
    default:
        /* Ignore writes to unspecified areas */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* No PIO needed; driver uses MMIO */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO */
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

    /* Clear all register shadows */
    s->eeprom_ctrl = 0;
    s->eeprom_data = 0;
    s->phy_ctrl = 0;
    s->debug_ctrl = 0;
    s->mac_ctrl = 0;
    s->intr_status = 0;
    s->intr_mask = 0;
    s->tx_status = 0;
    s->tfd_list_ptr0 = 0;
    s->tfd_list_ptr1 = 0;
    s->rfd_list_ptr0 = 0;
    s->rfd_list_ptr1 = 0;
    s->station_addr[0] = 0;
    s->station_addr[1] = 0;
    s->station_addr[2] = 0;
    s->hash_table[0] = 0;
    s->hash_table[1] = 0;
    s->receive_mode = 0;
    s->vlan_id = 0;
    s->vlan_tag = 0;
    s->count_down = 0;
    s->dma_ctrl = 0;
    s->rx_dma_int_ctrl = 0;
    s->rx_dma_poll_period = 0;
    s->tx_dma_poll_period = 0;
    s->rx_dma_burst_thresh = 0;
    s->rx_dma_urgent_thresh = 0;
    s->rmon_stat_mask = 0;
    s->max_frame_size = 0;
    s->tx_start_thresh = 0;
    s->asic_ctrl_low = 0;
    s->asic_ctrl_high = 0;
    s->mii_state = MII_IDLE;
    s->mii_preamble_cnt = 0;
    s->mii_out_bit = 0;
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

#define MII_BMCR        0
#define MII_BMSR        1
#define MII_ADVERTISE   4
#define MII_LPA         5
#define MII_PHY_SCR     0x10
#define MII_CTRL1000    9
#define MII_STAT1000    10

#define BMSR_ANEGCOMPLETE   0x0020
#define BMSR_LSTATUS        0x0004
#define BMSR_100FULL        0x0008
#define BMSR_100HALF        0x0010
#define BMSR_10FULL         0x0020
#define BMSR_10HALF         0x0040
#define BMCR_ANENABLE       0x1000
#define BMCR_SPEED1000      0x0040
#define BMCR_FULLDPLX       0x0100

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
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
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "bar0";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X: driver uses INTx */

    /* Initialize EEPROM data with a valid MAC and CRC */
    memset(s->eeprom_data_array, 0, sizeof(s->eeprom_data_array));
    uint8_t mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };
    memcpy(s->eeprom_data_array, mac, 6);
    /* Compute CRC over first 252 bytes and store at offset 252 */
    uint32_t crc = ether_crc_le(s->eeprom_data_array, 252);
    uint32_t le_crc = cpu_to_le32(~crc);
    memcpy(s->eeprom_data_array + 252, &le_crc, 4);

    /* Initialize PHY regs to a valid state: link up, 1000 FD, auto-negotiation complete */
    s->phy_regs[MII_BMSR] = BMSR_ANEGCOMPLETE | BMSR_LSTATUS | BMSR_100FULL | BMSR_100HALF | BMSR_10FULL | BMSR_10HALF;
    s->phy_regs[MII_BMCR] = BMCR_ANENABLE | BMCR_SPEED1000 | BMCR_FULLDPLX;
    /* Set valid PHY ID registers to pass MII detection */
    s->phy_regs[2] = 0x0141;  /* PHY ID1 (Marvell-like) */
    s->phy_regs[3] = 0x0C20;  /* PHY ID2 */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No MSI/MSI-X to uninit */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "dl2k_pci",
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
