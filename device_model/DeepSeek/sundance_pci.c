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


#define TYPE_PCIBASE_DEVICE "sundance_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID 0x1186
#define DEVICE_ID 0x1002
#define CLASS_ID PCI_CLASS_NETWORK_ETHERNET
#define BAR0_SIZE 0x80

/* Register offsets */
#define DMACtrl         0x00
#define TxListPtr       0x04
#define TxDMABurstThresh 0x08
#define TxDMAUrgentThresh 0x09
#define TxDMAPollPeriod  0x0a
#define RxDMAStatus     0x0c
#define RxListPtr       0x10
#define DebugCtrl0      0x1a
#define DebugCtrl1      0x1c
#define RxDMABurstThresh 0x14
#define RxDMAUrgentThresh 0x15
#define RxDMAPollPeriod  0x16
#define LEDCtrl         0x1a
#define ASICCtrl        0x30
#define EEData          0x34
#define EECtrl          0x36
#define FlashAddr       0x40
#define FlashData       0x44
#define WakeEvent       0x45
#define TxStatus        0x46
#define TxFrameId       0x47
#define DownCounter     0x18
#define IntrClear       0x4a
#define IntrEnable      0x4c
#define IntrStatus      0x4e
#define MACCtrl0        0x50
#define MACCtrl1        0x52
#define StationAddr     0x54
#define MaxFrameSize    0x5A
#define RxMode          0x5c
#define MIICtrl         0x5e
#define MulticastFilter0 0x60
#define MulticastFilter1 0x64
#define RxOctetsLow     0x68
#define RxOctetsHigh    0x6a
#define TxOctetsLow     0x6c
#define TxOctetsHigh    0x6e
#define TxFramesOK      0x70
#define RxFramesOK      0x72
#define StatsCarrierError 0x74
#define StatsLateColl   0x75
#define StatsMultiColl  0x76
#define StatsOneColl    0x77
#define StatsTxDefer    0x78
#define RxMissed        0x79
#define StatsTxXSDefer  0x7a
#define StatsTxAbort    0x7b
#define StatsBcastTx    0x7c
#define StatsBcastRx    0x7d
#define StatsMcastTx    0x7e
#define StatsMcastRx    0x7f

#define MIICtrl_MDIO_ShiftClk  0x01
#define MIICtrl_MDIO_Data      0x02
#define MIICtrl_MDIO_EnbOutput 0x04

enum mii_state { MII_IDLE, MII_DATA_OUT };

#define MII_START_PATTERN 0x3D  /* 0b111101 */
#define MII_CMD_BITS 14

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[1];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    uint8_t mmio[BAR0_SIZE];

    dma_addr_t tx_ring_dma;
    dma_addr_t rx_ring_dma;

    bool wol_enabled;

    /* EEPROM state */
    uint16_t eeprom_data[256];
    bool eeprom_busy;
    uint8_t eeprom_busy_counter;
    bool eeprom_data_valid;
    uint8_t eeprom_read_addr;

    /* MII state */
    enum mii_state mii_state;
    uint8_t mii_pattern_shift;   /* 6-bit shift register to detect start pattern */
    bool mii_cmd_detected;
    int mii_cmd_remaining;       /* bits remaining to capture after pattern */
    uint16_t mii_cmd_reg;
    uint16_t mii_data_register;
    int mii_data_bit_count;
    bool mii_data_bits[17];
    bool mii_output_bit;
    uint8_t prev_mii_ctrl;
};

enum intr_status_bits {
    IntrSummary=0x0001, IntrPCIErr=0x0002, IntrMACCtrl=0x0008,
    IntrTxDone=0x0004, IntrRxDone=0x0010, IntrRxStart=0x0020,
    IntrDrvRqst=0x0040,
    StatsMax=0x0080, LinkChange=0x0100,
    IntrTxDMADone=0x0200, IntrRxDMADone=0x0400,
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint16_t istatus = lduw_le_p(&s->mmio[IntrStatus]);
    uint16_t imask  = lduw_le_p(&s->mmio[IntrEnable]);
    if (istatus & imask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case EECtrl:
        if (s->eeprom_busy) {
            if (s->eeprom_busy_counter > 0) {
                s->eeprom_busy_counter--;
                val = 0x8000; /* busy */
            } else {
                s->eeprom_busy = false;
                s->eeprom_data_valid = true;
                s->mmio[EECtrl] &= ~0x02; /* clear busy bit in mmio */
                s->mmio[EECtrl + 1] &= ~0x02;
                val = lduw_le_p(&s->mmio[addr]);
            }
        } else {
            val = lduw_le_p(&s->mmio[addr]);
        }
        break;
    case EEData:
        if (s->eeprom_data_valid) {
            val = s->eeprom_data[s->eeprom_read_addr];
            s->eeprom_data_valid = false;
        } else {
            val = lduw_le_p(&s->mmio[addr]);
        }
        break;
    case MIICtrl:
        {
            uint8_t data = s->mmio[MIICtrl];
            /* Override MDIO_Data bit if PHY is driving */
            if ((data & MIICtrl_MDIO_EnbOutput) == 0 && s->mii_state == MII_DATA_OUT) {
                if (s->mii_output_bit) {
                    data |= MIICtrl_MDIO_Data;
                } else {
                    data &= ~MIICtrl_MDIO_Data;
                }
            }
            val = data;
        }
        break;
    default:
        switch (size) {
        case 1: val = s->mmio[addr]; break;
        case 2: val = lduw_le_p(&s->mmio[addr]); break;
        case 4: val = ldl_le_p(&s->mmio[addr]); break;
        case 8: val = ldq_le_p(&s->mmio[addr]); break;
        default: val = 0;
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case EECtrl:
        if (val & 0x0200) {
            s->eeprom_read_addr = val & 0xFF;
            s->eeprom_busy = true;
            s->eeprom_busy_counter = 1;
            s->eeprom_data_valid = false;
        }
        stw_le_p(&s->mmio[addr], val);
        break;
    case IntrStatus:
        {
            uint16_t istatus = lduw_le_p(&s->mmio[addr]);
            istatus &= ~(uint16_t)val;  /* W1C */
            stw_le_p(&s->mmio[addr], istatus);
            pcibase_update_irq(s);
        }
        break;
    case IntrEnable:
        stw_le_p(&s->mmio[addr], val);
        pcibase_update_irq(s);
        break;
    case MIICtrl:
        {
            uint8_t new_val = val;
            uint8_t old_val = s->mmio[addr];
            bool clk_rising  = (old_val & MIICtrl_MDIO_ShiftClk) == 0 && (new_val & MIICtrl_MDIO_ShiftClk) != 0;
            bool clk_falling = (old_val & MIICtrl_MDIO_ShiftClk) != 0 && (new_val & MIICtrl_MDIO_ShiftClk) == 0;

            s->mmio[addr] = new_val;
            s->prev_mii_ctrl = new_val;

            /* Handle clock rising edge: host is driving */
            if (clk_rising && (new_val & MIICtrl_MDIO_EnbOutput)) {
                if (!s->mii_cmd_detected) {
                    /* Shift in the data bit to the 6-bit pattern register */
                    s->mii_pattern_shift = ((s->mii_pattern_shift << 1) & 0x3F) | ((new_val & MIICtrl_MDIO_Data) ? 1 : 0);
                    if (s->mii_pattern_shift == MII_START_PATTERN) {
                        s->mii_cmd_detected = true;
                        s->mii_cmd_remaining = MII_CMD_BITS;
                        s->mii_cmd_reg = 0;
                    }
                } else if (s->mii_cmd_remaining > 0) {
                    /* Capturing command bits */
                    s->mii_cmd_reg = (s->mii_cmd_reg << 1) | ((new_val & MIICtrl_MDIO_Data) ? 1 : 0);
                    s->mii_cmd_remaining--;
                    if (s->mii_cmd_remaining == 0) {
                        /* Command complete, decode */
                        uint8_t phy = (s->mii_cmd_reg >> 5) & 0x1F;
                        uint8_t reg = s->mii_cmd_reg & 0x1F;
                        if (phy == 1 && reg == 0x01) {
                            /* MII_BMSR */
                            s->mii_data_register = 0x786d;
                        } else if (phy == 1 && reg == 0x04) {
                            /* MII_ADVERTISE */
                            s->mii_data_register = 0x01e1;
                        } else if (phy == 1 && reg == 0x05) {
                            /* MII_LPA */
                            s->mii_data_register = 0x01e1;
                        } else {
                            s->mii_data_register = 0x0000;
                        }
                        s->mii_state = MII_DATA_OUT;
                        s->mii_cmd_detected = false; /* ready for next command */
                        s->mii_pattern_shift = 0;
                        /* Prepare output bits: 1 TA bit + 16 data bits */
                        s->mii_data_bit_count = 0;
                        s->mii_output_bit = 0;
                        s->mii_data_bits[0] = 0; /* TA bit (PHY drives 0 to indicate presence) */
                        for (int i = 0; i < 16; i++) {
                            s->mii_data_bits[1 + i] = (s->mii_data_register >> (15 - i)) & 1;
                        }
                    }
                }
            }

            /* Handle clock falling edge: PHY may drive the line */
            if (clk_falling && (new_val & MIICtrl_MDIO_EnbOutput) == 0) {
                if (s->mii_state == MII_DATA_OUT) {
                    if (s->mii_data_bit_count < 17) {
                        s->mii_output_bit = s->mii_data_bits[s->mii_data_bit_count];
                        s->mii_data_bit_count++;
                    }
                }
            }
        }
        break;
    case TxStatus:
        /* Write 0 to clear */
        if (val == 0) {
            s->mmio[addr] = 0;
            s->mmio[addr + 1] = 0;
        } else {
            stw_le_p(&s->mmio[addr], val);
        }
        break;
    default:
        switch (size) {
        case 1: s->mmio[addr] = val; break;
        case 2: stw_le_p(&s->mmio[addr], val); break;
        case 4: stl_le_p(&s->mmio[addr], val); break;
        case 8: stq_le_p(&s->mmio[addr], val); break;
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

    memset(s->mmio, 0, BAR0_SIZE);
    s->intr_status = 0;
    s->intr_mask = 0;
    s->wol_enabled = false;

    /* Preload default MAC address into EEPROM */
    memset(s->eeprom_data, 0, sizeof(s->eeprom_data));
    uint8_t default_mac[6] = { 0x00, 0x0c, 0x29, 0x11, 0x22, 0x33 };
    for (int i = 0; i < 3; i++) {
        s->eeprom_data[i] = default_mac[2*i] | (default_mac[2*i+1] << 8);
    }
    s->eeprom_busy = false;
    s->eeprom_busy_counter = 0;
    s->eeprom_data_valid = false;
    s->eeprom_read_addr = 0;

    s->mii_state = MII_IDLE;
    s->mii_pattern_shift = 0;
    s->mii_cmd_detected = false;
    s->mii_cmd_remaining = 0;
    s->mii_cmd_reg = 0;
    s->mii_data_register = 0;
    s->mii_data_bit_count = 0;
    s->mii_output_bit = 0;
    s->prev_mii_ctrl = 0;
    memset(s->mii_data_bits, 0, sizeof(s->mii_data_bits));
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, int index, hwaddr size, const char *name, Error **errp)
{
    MemoryRegion *mr = &s->bar_regions[index];
    memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, name, pow2ceil(size));
    pci_register_bar(pdev, index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

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

    pcibase_register_bar(pdev, s, 0, BAR0_SIZE, "sundance-mmio", errp);
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
}

static const VMStateDescription vmstate_pcibase = {
    .name = "sundance_pci",
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
