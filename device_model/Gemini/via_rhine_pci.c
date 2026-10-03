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

#define TYPE_PCIBASE_DEVICE "via_rhine_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID 0x1106
#define DEVICE_ID 0x3043
#define CLASS_ID 0x0200

enum register_offsets {
    StationAddr=0x00, RxConfig=0x06, TxConfig=0x07, ChipCmd=0x08,
    ChipCmd1=0x09, TQWake=0x0A,
    IntrStatus=0x0C, IntrEnable=0x0E,
    MulticastFilter0=0x10, MulticastFilter1=0x14,
    RxRingPtr=0x18, TxRingPtr=0x1C, GFIFOTest=0x54,
    MIIPhyAddr=0x6C, MIIStatus=0x6D, PCIBusConfig=0x6E, PCIBusConfig1=0x6F,
    MIICmd=0x70, MIIRegAddr=0x71, MIIData=0x72, MACRegEEcsr=0x74,
    ConfigA=0x78, ConfigB=0x79, ConfigC=0x7A, ConfigD=0x7B,
    RxMissed=0x7C, RxCRCErrs=0x7E, MiscCmd=0x81,
    StickyHW=0x83, IntrStatus2=0x84,
    CamMask=0x88, CamCon=0x92, CamAddr=0x93,
    WOLcrSet=0xA0, PwcfgSet=0xA1, WOLcgSet=0xA3, WOLcrClr=0xA4,
    WOLcrClr1=0xA6, WOLcgClr=0xA7,
    PwrcsrSet=0xA8, PwrcsrSet1=0xA9, PwrcsrClr=0xAC, PwrcsrClr1=0xAD,
};

enum intr_status_bits {
    IntrRxDone    = 0x0001,
    IntrTxDone    = 0x0002,
    IntrRxErr     = 0x0004,
    IntrTxError   = 0x0008,
    IntrRxEmpty   = 0x0020,
    IntrPCIErr    = 0x0040,
    IntrStatsMax  = 0x0080,
    IntrRxEarly   = 0x0100,
    IntrTxUnderrun= 0x0210,
    IntrRxOverflow= 0x0400,
    IntrRxDropped = 0x0800,
    IntrRxNoBuf   = 0x1000,
    IntrTxAborted = 0x2000,
    IntrLinkChange= 0x4000,
    IntrRxWakeUp  = 0x8000,
    IntrTxDescRace= 0x080000,
    IntrNormalSummary = IntrRxDone | IntrTxDone,
    IntrTxErrSummary  = IntrTxDescRace | IntrTxAborted | IntrTxError | IntrTxUnderrun,
};

enum chip_cmd_bits {
    CmdInit=0x01, CmdStart=0x02, CmdStop=0x04, CmdRxOn=0x08,
    CmdTxOn=0x10, Cmd1TxDemand=0x20, CmdRxDemand=0x40,
    Cmd1EarlyRx=0x01, Cmd1EarlyTx=0x02, Cmd1FDuplex=0x04,
    Cmd1NoTxPoll=0x08, Cmd1Reset=0x80,
};

struct tx_desc {
    uint32_t tx_status;
    uint32_t desc_length;
    uint32_t addr;
    uint32_t next_desc;
};

struct rx_desc {
    uint32_t rx_status;
    uint32_t desc_length;
    uint32_t addr;
    uint32_t next_desc;
};

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
    uint32_t intr_enable;

    uint8_t regs[256];

    uint32_t rx_ring_ptr;
    uint32_t tx_ring_ptr;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint16_t isr = lduw_le_p(&s->regs[IntrStatus]);
    uint16_t ier = lduw_le_p(&s->regs[IntrEnable]);
    bool level = (isr & ier) != 0;
    pci_set_irq(pdev, level);
}

static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->regs[ChipCmd1] & Cmd1TxDemand || s->regs[TQWake]) {
        uint32_t tx_ring_ptr = ldl_le_p(&s->regs[TxRingPtr]);
        if (tx_ring_ptr != 0) {
            struct tx_desc desc;
            pci_dma_read(pdev, tx_ring_ptr, &desc, sizeof(desc));
            
            /* Emulate TX completion by clearing status (including DescOwn) */
            desc.tx_status = 0;
            pci_dma_write(pdev, tx_ring_ptr, &desc, sizeof(desc));
            
            /* Advance ring pointer */
            stl_le_p(&s->regs[TxRingPtr], le32_to_cpu(desc.next_desc));
            
            /* Raise TX Done interrupt */
            uint16_t isr = lduw_le_p(&s->regs[IntrStatus]);
            isr |= IntrTxDone;
            stw_le_p(&s->regs[IntrStatus], isr);
        }
        
        s->regs[ChipCmd1] &= ~Cmd1TxDemand;
        s->regs[TQWake] = 0;
        pcibase_update_irq(s);
    }
}

static uint64_t pcibase_read_reg(PCIBaseState *s, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    if (addr + size > sizeof(s->regs)) {
        return 0;
    }
    switch (size) {
        case 1: val = s->regs[addr]; break;
        case 2: val = lduw_le_p(&s->regs[addr]); break;
        case 4: val = ldl_le_p(&s->regs[addr]); break;
        case 8: val = ldq_le_p(&s->regs[addr]); break;
    }
    return val;
}

static void pcibase_write_reg(PCIBaseState *s, hwaddr addr, uint64_t val, unsigned size)
{
    if (addr + size > sizeof(s->regs)) {
        return;
    }
    
    /* Handle W1C for IntrStatus (0x0C) */
    if (addr == IntrStatus && size == 2) {
        uint16_t current = lduw_le_p(&s->regs[IntrStatus]);
        current &= ~val;
        stw_le_p(&s->regs[IntrStatus], current);
        pcibase_update_irq(s);
        return;
    }
    /* Handle W1C for IntrStatus2 (0x84) */
    if (addr == IntrStatus2 && size == 1) {
        s->regs[IntrStatus2] &= ~val;
        pcibase_update_irq(s);
        return;
    }

    switch (size) {
        case 1: s->regs[addr] = val; break;
        case 2: stw_le_p(&s->regs[addr], val); break;
        case 4: stl_le_p(&s->regs[addr], val); break;
        case 8: stq_le_p(&s->regs[addr], val); break;
    }

    /* Post-write actions */
    if (addr == ChipCmd1 || (addr <= ChipCmd1 && addr + size > ChipCmd1)) {
        if (s->regs[ChipCmd1] & Cmd1Reset) {
            /* Reset device but keep PCI config */
            memset(s->regs, 0, sizeof(s->regs));
            s->regs[StationAddr + 0] = 0x52;
            s->regs[StationAddr + 1] = 0x54;
            s->regs[StationAddr + 2] = 0x00;
            s->regs[StationAddr + 3] = 0x12;
            s->regs[StationAddr + 4] = 0x34;
            s->regs[StationAddr + 5] = 0x56;
            s->regs[MIIPhyAddr] = 0x01;
            /* Cmd1Reset is cleared by memset */
        } else if (s->regs[ChipCmd1] & Cmd1TxDemand) {
            pcibase_do_dma(s, true);
        }
    }

    if (addr == MACRegEEcsr || (addr <= MACRegEEcsr && addr + size > MACRegEEcsr)) {
        if (s->regs[MACRegEEcsr] & 0x20) {
            s->regs[MACRegEEcsr] &= ~0x20; /* Auto-clear EEPROM reload */
        }
    }

    if (addr == MIICmd || (addr <= MIICmd && addr + size > MIICmd)) {
        if (s->regs[MIICmd] & 0x40) { /* Read */
            s->regs[MIICmd] &= ~0x40;
            uint8_t regnum = s->regs[MIIRegAddr];
            uint16_t data = 0xFFFF;
            if (regnum == 1) { /* MII_BMSR */
                data = 0x782D; /* Link up, autoneg complete */
            } else if (regnum == 2 || regnum == 3) { /* PHY ID */
                data = 0x0101;
            }
            stw_le_p(&s->regs[MIIData], data);
        }
        if (s->regs[MIICmd] & 0x20) { /* Write */
            s->regs[MIICmd] &= ~0x20;
        }
    }

    if (addr == IntrEnable || (addr <= IntrEnable && addr + size > IntrEnable)) {
        pcibase_update_irq(s);
    }
    
    if (addr == TQWake || (addr <= TQWake && addr + size > TQWake)) {
        if (s->regs[TQWake]) {
            pcibase_do_dma(s, true);
        }
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return pcibase_read_reg(opaque, addr, size);
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_write_reg(opaque, addr, val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return pcibase_read_reg(opaque, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_write_reg(opaque, addr, val, size);
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

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[StationAddr + 0] = 0x52;
    s->regs[StationAddr + 1] = 0x54;
    s->regs[StationAddr + 2] = 0x00;
    s->regs[StationAddr + 3] = 0x12;
    s->regs[StationAddr + 4] = 0x34;
    s->regs[StationAddr + 5] = 0x56;
    s->regs[MIIPhyAddr] = 0x01;
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

    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO,  .size = 256, .name = "via_rhine_pio" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 256, .name = "via_rhine_mmio" };
      
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

static const VMStateDescription vmstate_pcibase = {
    .name = "via_rhine_pci",
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
