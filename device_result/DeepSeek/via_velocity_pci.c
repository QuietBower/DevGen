/*
 * QEMU VIA Velocity PCI/PCIe network device emulation
 * Based on Linux driver via-velocity.c
 * Implements necessary register accesses for driver probing and operation.
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

#define TYPE_PCIBASE_DEVICE "via_velocity_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1106
#define DEVICE_ID 0x3164
#define CLASS_ID 0x0200
#define VELOCITY_IO_SIZE 256

typedef struct __attribute__((packed)) {
    uint8_t PAR[6];        /* 0x00 */
    uint8_t RCR;
    uint8_t TCR;
    uint32_t CR0Set;       /* 0x08 */
    uint32_t CR0Clr;       /* 0x0C */
    uint8_t MARCAM[8];     /* 0x10 */
    uint32_t DecBaseHi;    /* 0x18 */
    uint16_t DbfBaseHi;    /* 0x1C */
    uint16_t reserved_1E;
    uint16_t ISRCTL;       /* 0x20 */
    uint8_t TXESR;
    uint8_t RXESR;
    uint32_t ISR;          /* 0x24 */
    uint32_t IMR;
    uint32_t TDStatusPort; /* 0x2C */
    uint16_t TDCSRSet;     /* 0x30 */
    uint8_t RDCSRSet;
    uint8_t reserved_33;
    uint16_t TDCSRClr;
    uint8_t RDCSRClr;
    uint8_t reserved_37;
    uint32_t RDBaseLo;     /* 0x38 */
    uint16_t RDIdx;        /* 0x3C */
    uint8_t TQETMR;        /* 0x3E */
    uint8_t RQETMR;        /* 0x3F */
    uint32_t TDBaseLo[4];  /* 0x40 */
    uint16_t RDCSize;      /* 0x50 */
    uint16_t TDCSize;      /* 0x52 */
    uint16_t TDIdx[4];     /* 0x54 */
    uint16_t tx_pause_timer; /* 0x5C */
    uint16_t RBRDU;        /* 0x5E */
    uint32_t FIFOTest0;    /* 0x60 */
    uint32_t FIFOTest1;    /* 0x64 */
    uint8_t CAMADDR;       /* 0x68 */
    uint8_t CAMCR;         /* 0x69 */
    uint8_t GFTEST;        /* 0x6A */
    uint8_t FTSTCMD;       /* 0x6B */
    uint8_t MIICFG;        /* 0x6C */
    uint8_t MIISR;
    uint8_t PHYSR0;
    uint8_t PHYSR1;
    uint8_t MIICR;
    uint8_t MIIADR;
    uint16_t MIIDATA;
    uint16_t SoftTimer0;   /* 0x74 */
    uint16_t SoftTimer1;
    uint8_t CFGA;          /* 0x78 */
    uint8_t CFGB;
    uint8_t CFGC;
    uint8_t CFGD;
    uint16_t DCFG;         /* 0x7C */
    uint16_t MCFG;
    uint8_t TBIST;         /* 0x80 */
    uint8_t RBIST;
    uint8_t PMCPORT;
    uint8_t STICKHW;
    uint8_t MIBCR;         /* 0x84 */
    uint8_t reserved_85;
    uint8_t rev_id;
    uint8_t PORSTS;
    uint32_t MIBData;      /* 0x88 */
    uint16_t EEWrData;
    uint8_t reserved_8E;
    uint8_t BPMDWr;
    uint8_t BPCMD;
    uint8_t BPMDRd;
    uint8_t EECHKSUM;      /* 0x92 */
    uint8_t EECSR;
    uint16_t EERdData;     /* 0x94 */
    uint8_t EADDR;
    uint8_t EMBCMD;
    uint8_t JMPSR0;        /* 0x98 */
    uint8_t JMPSR1;
    uint8_t JMPSR2;
    uint8_t JMPSR3;
    uint8_t CHIPGSR;       /* 0x9C */
    uint8_t TESTCFG;
    uint8_t DEBUG;
    uint8_t CHIPGCR;
    uint16_t WOLCRSet;     /* 0xA0 */
    uint8_t PWCFGSet;
    uint8_t WOLCFGSet;
    uint16_t WOLCRClr;     /* 0xA4 */
    uint8_t PWCFGCLR;
    uint8_t WOLCFGClr;
    uint16_t WOLSRSet;     /* 0xA8 */
    uint16_t reserved_AA;
    uint16_t WOLSRClr;     /* 0xAC */
    uint16_t reserved_AE;
    uint16_t PatternCRC[8]; /* 0xB0 */
    uint32_t ByteMask[4][4]; /* 0xC0 */
} MacRegs;

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

/* Additional structures from driver */
struct tx_desc {
    struct tdesc0 {
        uint16_t TSR;
        uint16_t len;
    } tdesc0;
    struct tdesc1 {
        uint16_t vlan;
        uint8_t TCR;
        uint8_t cmd;
    } tdesc1;
    struct td_buf {
        uint32_t pa_low;
        uint16_t pa_high;
        uint16_t size;
    } td_buf[7];
};

struct rx_desc {
    struct rdesc0 {
        uint16_t RSR;
        uint16_t len;
    } rdesc0;
    struct rdesc1 {
        uint16_t PQTAG;
        uint8_t CSM;
        uint8_t IPKT;
    } rdesc1;
    uint32_t pa_low;
    uint16_t pa_high;
    uint16_t size;
};

enum chip_type {
    CHIP_TYPE_VT6110 = 1,
};

enum velocity_init_type {
    VELOCITY_INIT_COLD = 0,
    VELOCITY_INIT_RESET,
    VELOCITY_INIT_WOL
};

enum speed_opt {
    SPD_DPX_AUTO = 0,
    SPD_DPX_100_HALF = 1,
    SPD_DPX_100_FULL = 2,
    SPD_DPX_10_HALF = 3,
    SPD_DPX_10_FULL = 4,
    SPD_DPX_1000_FULL = 5
};

/* Register bit definitions (extracted from driver usage) */
/* CR0 bits */
#define CR0_SFRST       (1u << 31)
#define CR0_FORSRST     (1u << 30)
#define CR0_STOP        (1u << 24)
#define CR0_DPOLL       (1u << 23)
#define CR0_TXON        (1u << 22)
#define CR0_RXON        (1u << 21)
#define CR0_STRT        (1u << 20)
#define CR0_FDXTFCEN    (1u << 19)
#define CR0_FDXRFCEN    (1u << 18)
#define CR0_HDXFCEN     (1u << 17)
#define CR0_XONEN       (1u << 16)
#define CR0_XHITH1      (1u << 15)
#define CR0_XHITH0      (1u << 14)
#define CR0_XLTH1       (1u << 13)
#define CR0_XLTH0       (1u << 12)

/* TDCSR bits */
#define TRDCSR_RUN      (1 << 0)

/* MIICR bits */
#define MIICR_MAUTO     (1 << 0)
#define MIICR_RCMD      (1 << 1)
#define MIICR_WCMD      (1 << 2)

/* MIISR bits */
#define MIISR_MIDLE     (1 << 0)

/* PHYSR0 bits */
#define PHYSR0_LINKGD   (1 << 0)
#define PHYSR0_FDPX     (1 << 1)
#define PHYSR0_SPDG     (1 << 2)
#define PHYSR0_SPD10    (1 << 3)
#define PHYSR0_RXFLC    0 /* placeholder */
#define PHYSR0_TXFLC    0 /* placeholder */

/* EECSR bits */
#define EECSR_RELOAD    (1 << 0)

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    MacRegs regs;

    /* Hidden registers for set/clear semantics */
    uint32_t cr0;         /* real CR0 value */
    uint16_t td_csr;      /* real TDCSR value */
    uint8_t  rd_csr;      /* real RDCSR value */
    uint16_t wol_cr;      /* real WOLCR value */
    uint8_t  pw_cfg;      /* real PWCFG value */
    uint8_t  wol_cfg;     /* real WOLCFG value */

    /* PHY registers (MII) */
    uint16_t phy_regs[32];

    /* MII command emulation */
    bool mii_midle;
    QEMUTimer *mii_timer;

    /* Soft reset emulation */
    QEMUTimer *sfrst_timer;

    /* EEPROM reload emulation */
    QEMUTimer *eecsr_timer;

    /* DMA Context */
    dma_addr_t tx_desc_base[4];
    dma_addr_t rx_desc_base;

    uint32_t status;      /* Operational status flags */
    uint8_t power_state;     /* Power management state (D0-D3) */
    uint32_t mii_status;
    uint8_t chip_rev_id;
};

/* Forward declarations */
static void velocity_soft_reset(PCIBaseState *s);
static void velocity_update_irq(PCIBaseState *s);
static void velocity_eeprom_reload(PCIBaseState *s);

/* Timer callbacks */
static void velocity_mii_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;

    if (s->regs.MIICR & (MIICR_RCMD | MIICR_WCMD)) {
        if (s->regs.MIICR & MIICR_RCMD) {
            /* complete read */
            uint8_t phy_addr = s->regs.MIIADR & 0x1f;
            if (phy_addr < 32) {
                s->regs.MIIDATA = s->phy_regs[phy_addr];
            } else {
                s->regs.MIIDATA = 0;
            }
            s->regs.MIICR &= ~MIICR_RCMD;
        } else if (s->regs.MIICR & MIICR_WCMD) {
            /* complete write */
            uint8_t phy_addr = s->regs.MIIADR & 0x1f;
            if (phy_addr < 32) {
                s->phy_regs[phy_addr] = s->regs.MIIDATA;
            }
            s->regs.MIICR &= ~MIICR_WCMD;
        }
        s->mii_midle = true;
        velocity_update_irq(s);
    }
}

static void velocity_sfrst_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    s->cr0 &= ~CR0_SFRST;
    velocity_update_irq(s);
}

static void velocity_eecsr_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    s->regs.EECSR &= ~EECSR_RELOAD;
    velocity_eeprom_reload(s);
    velocity_update_irq(s);
}

/* Soft reset function */
static void velocity_soft_reset(PCIBaseState *s)
{
    /* Reset to initial state */
    s->cr0 = 0;
    s->td_csr = 0;
    s->rd_csr = 0;
    s->wol_cr = 0;
    s->pw_cfg = 0;
    s->wol_cfg = 0;

    memset(&s->regs, 0, sizeof(s->regs));

    /* Set some initial values */
    s->regs.PAR[0] = 0x00;
    s->regs.PAR[1] = 0x11;
    s->regs.PAR[2] = 0x22;
    s->regs.PAR[3] = 0x33;
    s->regs.PAR[4] = 0x44;
    s->regs.PAR[5] = 0x55;
    s->regs.rev_id = 0x01;

    /* MII: set PHY ID to Marvell 88E1000 */
    s->phy_regs[2] = 0x0141;
    s->phy_regs[3] = 0x0C00;

    /* Initialize MII state */
    s->mii_midle = true;
    s->regs.MIISR = MIISR_MIDLE;
    s->regs.PHYSR0 = PHYSR0_LINKGD | PHYSR0_FDPX | PHYSR0_SPDG; /* link up, 1000M full */
    s->regs.PHYSR1 = 0;

    velocity_update_irq(s);
}

/* EEPROM reload: set MAC address and other defaults */
static void velocity_eeprom_reload(PCIBaseState *s)
{
    /* Already set by soft reset */
    s->regs.PAR[0] = 0x00;
    s->regs.PAR[1] = 0x11;
    s->regs.PAR[2] = 0x22;
    s->regs.PAR[3] = 0x33;
    s->regs.PAR[4] = 0x44;
    s->regs.PAR[5] = 0x55;
}

/* Update IRQ state based on ISR and IMR */
static void velocity_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->regs.ISR & s->regs.IMR) != 0;
    pci_set_irq(pdev, level ? 1 : 0);
}

/* Register read/write dispatch */
static uint64_t velocity_reg_read(PCIBaseState *s, hwaddr addr, unsigned size)
{
    uint64_t val = 0;

    /* Handle special registers */
    switch (addr) {
    case 0x08: /* CR0Set */
        val = s->cr0;
        break;
    case 0x0C: /* CR0Clr */
        val = ~s->cr0;
        break;
    case 0x24: /* ISR */
        val = s->regs.ISR;
        break;
    case 0x28: /* IMR */
        val = s->regs.IMR;
        break;
    case 0x30: /* TDCSRSet */
        val = s->td_csr;
        break;
    case 0x34: /* TDCSRClr */
        val = ~s->td_csr;
        break;
    case 0x32: /* RDCSRSet */
        val = s->rd_csr;
        break;
    case 0x36: /* RDCSRClr */
        val = ~s->rd_csr;
        break;
    case 0xA0: /* WOLCRSet */
        val = s->wol_cr;
        break;
    case 0xA4: /* WOLCRClr */
        val = ~s->wol_cr;
        break;
    case 0xA1: /* PWCFGSet */
        val = s->pw_cfg;
        break;
    case 0xA5: /* PWCFGCLR */
        val = ~s->pw_cfg;
        break;
    case 0xA2: /* WOLCFGSet */
        val = s->wol_cfg;
        break;
    case 0xA6: /* WOLCFGClr */
        val = ~s->wol_cfg;
        break;
    case 0x6D: /* MIISR */
        if (s->mii_midle) {
            val = MIISR_MIDLE;
        } else {
            val = 0;
        }
        break;
    case 0x70: /* MIICR */
        val = s->regs.MIICR;
        break;
    case 0x6F: /* MIIADR */
        val = s->regs.MIIADR;
        break;
    case 0x72: /* MIIDATA */
        val = s->regs.MIIDATA;
        break;
    default:
        if (addr + size <= sizeof(s->regs)) {
            /* generic read */
            memcpy(&val, (uint8_t *)&s->regs + addr, size);
        } else {
            val = 0;
        }
        break;
    }
    return val;
}

static void velocity_reg_write(PCIBaseState *s, hwaddr addr, uint64_t val, unsigned size)
{
    /* Handle special write registers */
    switch (addr) {
    case 0x08: /* CR0Set */
        s->cr0 |= val;
        if (val & CR0_SFRST) {
            velocity_soft_reset(s);
            /* Schedule timer to clear SFRST after short delay */
            timer_mod(s->sfrst_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000);
        }
        velocity_update_irq(s);
        return;
    case 0x0C: /* CR0Clr */
        s->cr0 &= ~val;
        velocity_update_irq(s);
        return;
    case 0x24: /* ISR (W1C) */
        s->regs.ISR &= ~val;
        velocity_update_irq(s);
        return;
    case 0x28: /* IMR */
        s->regs.IMR = val;
        velocity_update_irq(s);
        return;
    case 0x30: /* TDCSRSet */
        s->td_csr |= val;
        velocity_update_irq(s);
        return;
    case 0x34: /* TDCSRClr */
        s->td_csr &= ~val;
        velocity_update_irq(s);
        return;
    case 0x32: /* RDCSRSet */
        s->rd_csr |= val;
        velocity_update_irq(s);
        return;
    case 0x36: /* RDCSRClr */
        s->rd_csr &= ~val;
        velocity_update_irq(s);
        return;
    case 0xA0: /* WOLCRSet */
        s->wol_cr |= val;
        velocity_update_irq(s);
        return;
    case 0xA4: /* WOLCRClr */
        s->wol_cr &= ~val;
        velocity_update_irq(s);
        return;
    case 0xA1: /* PWCFGSet */
        s->pw_cfg |= val;
        velocity_update_irq(s);
        return;
    case 0xA5: /* PWCFGCLR */
        s->pw_cfg &= ~val;
        velocity_update_irq(s);
        return;
    case 0xA2: /* WOLCFGSet */
        s->wol_cfg |= val;
        velocity_update_irq(s);
        return;
    case 0xA6: /* WOLCFGClr */
        s->wol_cfg &= ~val;
        velocity_update_irq(s);
        return;
    case 0x70: /* MIICR */
        s->regs.MIICR = val;
        if (val & (MIICR_RCMD | MIICR_WCMD)) {
            s->mii_midle = false;
            timer_mod(s->mii_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000);
        } else if (val & MIICR_MAUTO) {
            s->mii_midle = false;
        } else {
            s->mii_midle = true;
        }
        velocity_update_irq(s);
        return;
    case 0x93: /* EECSR */
        s->regs.EECSR = val;
        if (val & EECSR_RELOAD) {
            /* Start reload timer */
            timer_mod(s->eecsr_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000);
        }
        velocity_update_irq(s);
        return;
    default:
        if (addr + size <= sizeof(s->regs)) {
            /* generic write */
            memcpy((uint8_t *)&s->regs + addr, &val, size);
        }
        break;
    }
    velocity_update_irq(s);
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return velocity_reg_read(s, addr, size);
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    velocity_reg_write(s, addr, val, size);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return velocity_reg_read(s, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    velocity_reg_write(s, addr, val, size);
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

    /* Reset our state */
    velocity_soft_reset(s);
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

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_PIO,
        .size = VELOCITY_IO_SIZE,
        .name = "io",
    };
    s->bar_info[1] = (BARInfo){
        .index = 1,
        .type = BAR_TYPE_MMIO,
        .size = VELOCITY_IO_SIZE,
        .name = "mmio",
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize timers */
    s->sfrst_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, velocity_sfrst_timer_cb, s);
    s->mii_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, velocity_mii_timer_cb, s);
    s->eecsr_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, velocity_eecsr_timer_cb, s);

    /* Perform initial soft reset */
    velocity_soft_reset(s);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    timer_free(s->sfrst_timer);
    timer_free(s->mii_timer);
    timer_free(s->eecsr_timer);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "via_velocity_pci",
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
