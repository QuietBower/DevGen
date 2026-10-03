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

#define TYPE_PCIBASE_DEVICE "ath5k_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor and Device ID from the first entry in ath5k_pci_id_table */
#define VENDOR_ID 0x168c
#define DEVICE_ID 0x0207
#define CLASS_ID  0x0280

/* Hardware register offsets (from ath5k driver defines) */
#define AR5K_CR			0x0008
#define AR5K_RXDP		0x000c
#define AR5K_CFG		0x0014
#define AR5K_ISR		0x001c
#define AR5K_IMR		0x0020
#define AR5K_IER		0x0024
#define AR5K_RTSD0		0x0028
#define AR5K_RTSD1		0x002c
#define AR5K_TXCFG		0x0030
#define AR5K_RXCFG		0x0034
#define AR5K_RXJLA		0x0038
#define AR5K_MIBC		0x0040
#define AR5K_TOPS		0x0044
#define AR5K_RXNOFRM		0x0048
#define AR5K_TXNOFRM		0x004c
#define AR5K_RPGTO		0x0050
#define AR5K_RFCNT		0x0054
#define AR5K_MISC		0x0058
#define AR5K_QCUDCU_CLKGT	0x005c
#define AR5K_CCFG		0x0600
#define AR5K_CPC0		0x0610
#define AR5K_CPC1		0x0614
#define AR5K_CPC2		0x0618
#define AR5K_CPC3		0x061c
#define AR5K_CPCOVF		0x0620
#define AR5K_DCM_ADDR		0x0400
#define AR5K_DCCFG		0x0420
#define AR5K_PISR		0x0080
#define AR5K_SISR0		0x0084
#define AR5K_SISR1		0x0088
#define AR5K_SISR2		0x008c
#define AR5K_SISR3		0x0090
#define AR5K_SISR4		0x0094
#define AR5K_PIMR		0x00a0
#define AR5K_SIMR0		0x00a4
#define AR5K_SIMR1		0x00a8
#define AR5K_SIMR2		0x00ac
#define AR5K_SIMR3		0x00b0
#define AR5K_SIMR4		0x00b4
#define AR5K_RESET_CTL		0x4000
#define AR5K_SLEEP_CTL		0x4004
#define AR5K_INTPEND		0x4008
#define AR5K_SFR		0x400c
#define AR5K_PCICFG		0x4010
#define AR5K_GPIOCR		0x4014
#define AR5K_GPIODO		0x4018
#define AR5K_SREV		0x4020
#define AR5K_PCIE_SERDES	0x4080
#define AR5K_PCIE_SERDES_RESET	0x4084
#define AR5K_EEPROM_BASE	0x6000
#define AR5K_EEPROM_DATA_5210	0x6800
#define AR5K_EEPROM_STAT_5210	0x6c00
#define AR5K_EEPROM_CMD		0x6008
#define AR5K_EEPROM_DATA_5211	0x6004
#define AR5K_EEPROM_STAT_5211	0x600c
#define AR5K_STA_ID0		0x8000
#define AR5K_STA_ID1		0x8004
#define AR5K_BSS_ID0		0x8008
#define AR5K_BSS_ID1		0x800c
#define AR5K_BCR		0x0028
#define AR5K_BSR		0x002c
#define AR5K_BEACON_5211	0x8020
#define AR5K_BEACON_5210	0x8024
#define AR5K_PHY_BASE		0x9800
#define AR5K_PHY_TURBO		0x9804
#define AR5K_PHY_CHIP_ID	0x9818
#define AR5K_PHY_PLL		0x987c
#define AR5K_PHY_MODE		0x0a200
#define AR5K_PHY_ERR_FIL	0x810c
#define AR5K_QUEUE_REG(_r, _q)	(((_q) << 2) + _r)
#define AR5K_QCU_TXDP_BASE	0x0800
#define AR5K_QCU_TXE		0x0840
#define AR5K_QCU_TXD		0x0880
#define AR5K_NOQCU_TXDP0	0x0000
#define AR5K_NOQCU_TXDP1	0x0004

/* Radio revision register - needed for radio identification */
#define AR5K_PHY_RADIO		0x989c

/* Bitfield masks and constants */
#define AR5K_PCICFG_EEAE	0x00000001
#define AR5K_PCICFG_SPWR_DN	0x00010000
#define AR5K_PCICFG_LED		0x00000060
#define AR5K_PCICFG_LED_NONE	0x00000000
#define AR5K_PCICFG_LED_PEND	0x00000020
#define AR5K_PCICFG_LED_ASSOC	0x00000040
#define AR5K_PCICFG_LED_BCTL	0x00001000
#define AR5K_PCICFG_LEDMODE	0x000e0000
#define AR5K_PCICFG_LEDMODE_PROP 0x00000000
#define AR5K_PCICFG_LEDMODE_PROM 0x00020000
#define AR5K_PCICFG_RETRY_FIX	0x00001000
#define AR5K_RESET_CTL_PCU	0x00000001
#define AR5K_RESET_CTL_DMA	0x00000002
#define AR5K_RESET_CTL_BASEBAND 0x00000002
#define AR5K_RESET_CTL_MAC	0x00000004
#define AR5K_RESET_CTL_PHY	0x00000008
#define AR5K_RESET_CTL_PCI	0x00000010
#define AR5K_SLEEP_CTL_SLE_SLP	0x00010000
#define AR5K_SLEEP_CTL_SLE_WAKE 0x00000000
#define AR5K_SLEEP_CTL_SLE	0x00030000
#define AR5K_SLEEP_CTL_SLE_ALLOW 0x00020000
#define AR5K_STA_ID1_NO_PSPOLL	0x00100000
#define AR5K_STA_ID1_ADHOC	0x00020000
#define AR5K_STA_ID1_AP		0x00010000
#define AR5K_STA_ID1_PWR_SV	0x00040000
#define AR5K_STA_ID1_KEYSRCH_MODE 0x10000000
#define AR5K_STA_ID1_DEFAULT_ANTENNA 0x00200000
#define AR5K_STA_ID0		0x8000
#define AR5K_CFG_IBSS		0x00000020
#define AR5K_BCR_AP		0x00000000
#define AR5K_BCR_ADHOC		0x00000001
#define AR5K_BCR_TQ1V		0x00000008
#define AR5K_BCR_TQ1FV		0x00000004
#define AR5K_BCR_BDMAE		0x00000002
#define AR5K_CR_TXD0		0x00000008
#define AR5K_CR_TXE0		0x00000001
#define AR5K_CR_TXD1		0x00000010
#define AR5K_CR_TXE1		0x00000002
#define AR5K_RX_FILTER_5210	0x804c
#define AR5K_RX_FILTER_5211	0x803c
#define AR5K_RXCFG_ZLFDMA	0x00000008
#define AR5K_RX_FILTER_PROM	0x00000020
#define AR5K_RX_FILTER_RADARERR_5212 0x00000200
#define AR5K_RX_FILTER_RADARERR_5211 0x00000080
#define AR5K_RX_FILTER_PHYERR_5211 0x00000040
#define AR5K_RX_FILTER_PHYERR_5212 0x00000100
#define AR5K_PHY_ERR_FIL_OFDM	0x00020000
#define AR5K_PHY_ERR_FIL_CCK	0x02000000
#define AR5K_PHY_ERR_FIL_RADAR	0x00000020
#define AR5K_MISC_MODE_COMBINED_MIC 0x00000004
#define AR5K_TXCFG		0x0030
#define AR5K_TXQ_FLAG_TXDESCINT_ENABLE 0x0008
#define AR5K_TXQ_FLAG_TXEOLINT_ENABLE 0x0004
#define AR5K_TXQ_FLAG_POST_FR_BKOFF_DIS 0x1000
#define AR5K_EEPROM_CMD_READ	0x00000001
#define AR5K_EEPROM_STAT_RDDONE 0x00000002
#define AR5K_EEPROM_STAT_RDERR	0x00000001
#define AR5K_SREV_VER		0x000000ff

/* Interrupt bits (enum ath5k_int) */
enum ath5k_int {
	AR5K_INT_RXOK	= 0x00000001,
	AR5K_INT_RXDESC	= 0x00000002,
	AR5K_INT_RXERR	= 0x00000004,
	AR5K_INT_RXNOFRM = 0x00000008,
	AR5K_INT_RXEOL	= 0x00000010,
	AR5K_INT_RXORN	= 0x00000020,
	AR5K_INT_TXOK	= 0x00000040,
	AR5K_INT_TXDESC	= 0x00000080,
	AR5K_INT_TXERR	= 0x00000100,
	AR5K_INT_TXNOFRM = 0x00000200,
	AR5K_INT_TXEOL	= 0x00000400,
	AR5K_INT_TXURN	= 0x00000800,
	AR5K_INT_MIB	= 0x00001000,
	AR5K_INT_SWI	= 0x00002000,
	AR5K_INT_RXPHY	= 0x00004000,
	AR5K_INT_RXKCM	= 0x00008000,
	AR5K_INT_SWBA	= 0x00010000,
	AR5K_INT_BRSSI	= 0x00020000,
	AR5K_INT_BMISS	= 0x00040000,
	AR5K_INT_FATAL	= 0x00080000,
	AR5K_INT_BNR	= 0x00100000,
	AR5K_INT_TIM	= 0x00200000,
	AR5K_INT_DTIM	= 0x00400000,
	AR5K_INT_DTIM_SYNC =	0x00800000,
	AR5K_INT_GPIO	=	0x01000000,
	AR5K_INT_BCN_TIMEOUT =	0x02000000,
	AR5K_INT_CAB_TIMEOUT =	0x04000000,
	AR5K_INT_QCBRORN =	0x08000000,
	AR5K_INT_QCBRURN =	0x10000000,
	AR5K_INT_QTRIG	=	0x20000000,
	AR5K_INT_GLOBAL =	0x80000000,
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

    uint32_t regs[0x4000];  /* 64KB register file */

    /* EEPROM simulation */
    uint16_t eeprom_data[256];
    uint32_t eeprom_offset;
    uint16_t eeprom_read_data;
    bool eeprom_read_active;
};

/* Forward declarations */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    /* Ensure alignment */
    addr &= ~0x3;
    if (addr >= sizeof(s->regs)) {
        return 0;
    }

    /* Special override registers */
    switch (addr) {
    case AR5K_SREV:
        /* Emulate AR5212 revision 0x50 */
        return 0x00000050;

    case AR5K_PHY_CHIP_ID:
        /* Emulate AR5112 radio chip */
        return 0x00000046;

    case AR5K_PHY_RADIO:
        /* Emulate AR5112 radio revision 2 */
        return 0x02000000;

    case 0x9900:
        /* Radio revision identification (AR5K_PHY_RADIO_REV) */
        return 0x04000000;

    case AR5K_EEPROM_BASE:
        return s->regs[addr >> 2];

    case AR5K_EEPROM_CMD:
        return s->regs[addr >> 2];

    case AR5K_EEPROM_STAT_5211:
    case AR5K_EEPROM_STAT_5210:
        /* Always return READ DONE with no error */
        return AR5K_EEPROM_STAT_RDDONE;

    case AR5K_EEPROM_DATA_5211:
    case AR5K_EEPROM_DATA_5210:
        if (s->eeprom_read_active) {
            s->eeprom_read_active = false;
            return s->eeprom_read_data;
        }
        return 0;

    default:
        return s->regs[addr >> 2];
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    addr &= ~0x3;
    if (addr >= sizeof(s->regs)) {
        return;
    }

    /* Override for EEPROM commands */
    switch (addr) {
    case AR5K_EEPROM_BASE:
        s->regs[addr >> 2] = val;
        s->eeprom_offset = val & 0xff;
        break;

    case AR5K_EEPROM_CMD:
        s->regs[addr >> 2] = val;
        if (val & AR5K_EEPROM_CMD_READ) {
            if (s->eeprom_offset < 256) {
                s->eeprom_read_data = s->eeprom_data[s->eeprom_offset];
                s->eeprom_read_active = true;
            }
        }
        break;

    case AR5K_SREV:
        /* Ignore writes to read-only SREV */
        break;

    case AR5K_PHY_CHIP_ID:
        /* Ignore writes to read-only PHY_CHIP_ID */
        break;

    case AR5K_PHY_RADIO:
        /* Radio revision is read-only, ignore writes */
        break;

    default:
        s->regs[addr >> 2] = val;
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    memset(s->regs, 0, sizeof(s->regs));
    s->eeprom_offset = 0;
    s->eeprom_read_active = false;
    s->eeprom_read_data = 0;
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
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
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

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 65536;
    s->bar_info[0].name = "ath5k_mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Legacy IRQ only, no MSI/MSI-X */
    s->has_msi = false;
    s->has_msix = false;

    /* Initialize EEPROM with known good MAC (00:11:22:33:44:55) */
    memset(s->eeprom_data, 0, sizeof(s->eeprom_data));
    s->eeprom_data[0x1d] = 0x5544;  /* mac[5]=0x55, mac[4]=0x44 */
    s->eeprom_data[0x1e] = 0x3322;  /* mac[3]=0x33, mac[2]=0x22 */
    s->eeprom_data[0x1f] = 0x1100;  /* mac[1]=0x11, mac[0]=0x00 */
    /* Other offsets remain 0 */
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
    .name = "ath5k_pci",
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
