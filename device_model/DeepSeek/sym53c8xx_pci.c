
/*
 * QEMU model of LSI 53C8xx SCSI controller
 * Based on Linux sym53c8xx_2 driver analysis
 * Phase 2: Functional implementation
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
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
/* none */

#define TYPE_PCIBASE_DEVICE "sym53c8xx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor and Device IDs */
#define PCI_VENDOR_ID_LSI_LOGIC 0x1000
#define PCI_DEVICE_ID_NCR_53C810 0x0001
#define PCI_CLASS_STORAGE_SCSI  0x0100

/* Register offsets and bit definitions from driver */
/* SCNTL0 */
/* SCNTL1 */
#define ISCON   0x10
#define CRST    0x08
#define IARB    0x02
/* SCNTL2 */
#define SDU     0x80
#define CHM     0x40
#define WSS     0x08
#define WSR     0x01
/* SCNTL3 */
#define EWS     0x08
#define ULTRA   0x80
/* SCID */
#define RRE     0x40
#define SRE     0x20
/* SOCL */
#define CREQ    0x80
#define CACK    0x40
#define CBSY    0x20
#define CSEL    0x10
#define CATN    0x08
#define CMSG    0x04
#define CC_D    0x02
#define CI_O    0x01
/* DSTAT */
#define DFE     0x80
#define MDPE    0x40
#define BF      0x20
#define ABRT    0x10
#define SSI     0x08
#define SIR     0x04
#define IID     0x01
/* SSTAT0 */
#define ILF     0x80
#define ORF     0x40
#define OLF     0x20
#define AIP     0x10
#define LOA     0x08
#define WOA     0x04
#define IRST    0x02
#define SDP     0x01
/* SSTAT1 */
#define FF3210  0xf0
/* SSTAT2 */
#define ILF1    0x80
#define ORF1    0x40
#define OLF1    0x20
#define DM      0x04
#define LDSC    0x02
/* ISTAT */
#define CABRT   0x80
#define SRST    0x40
#define SIGP    0x20
#define SEM     0x10
#define CON     0x08
#define INTF    0x04
#define SIP     0x02
#define DIP     0x01
/* ISTAT1 (896) */
#define FLSH    0x04
#define SCRUN   0x02
#define SIRQD   0x01
/* CTEST2 */
#define CSIGP   0x40
/* CTEST3 */
#define FLF     0x08
#define CLF     0x04
#define FM      0x02
#define WRIE    0x01
/* CTEST4 */
#define BDIS    0x80
#define MPEE    0x08
/* CTEST5 */
#define DFS     0x20
/* DMODE */
#define BL_2    0x80
#define BL_1    0x40
#define ERL     0x08
#define ERMP    0x04
#define BOF     0x02
/* DCNTL */
#define CLSE    0x80
#define PFF     0x40
#define PFEN    0x20
#define SSM     0x10
#define IRQM    0x08
#define STD     0x04
#define IRQD    0x02
#define NOCOM   0x01
/* SIEN / SIST */
#define SBMC    0x1000
#define STO     0x0400
#define GEN     0x0200
#define HTH     0x0100
#define MA      0x80
#define CMP     0x40
#define SEL     0x20
#define RSL     0x10
#define SGE     0x08
#define UDC     0x04
#define RST     0x02
#define PAR     0x01
/* STEST1 */
#define SCLK    0x80
#define DBLEN   0x08
#define DBLSEL  0x04
/* STEST2 */
#define ROF     0x40
#define EXT     0x02
/* STEST3 */
#define TE      0x80
#define HSC     0x20
#define CSF     0x02
/* STEST4 */
#define SMODE   0xc0
#define SMODE_HVD 0x40
#define SMODE_SE  0x80
#define SMODE_LVD 0xc0
#define LCKFRQ  0x20
/* CCNTL0 */
#define ENPMJ   0x80
#define PMJCTL  0x40
#define ENNDJ   0x20
#define DISFC   0x10
#define DILS    0x02
#define DPR     0x01
/* CCNTL1 */
#define ZMOD    0x80
#define DDAC    0x08
#define XTIMOD  0x04
#define EXTIBMV 0x02
#define EXDBMV  0x01
/* SCNTL4 */
#define U3EN    0x80
#define AIPCKEN 0x40
#define XCLKH_DT 0x08
#define XCLKH_ST 0x04
#define XCLKS_DT 0x02
#define XCLKS_ST 0x01
/* AIPCNTL1 */
#define DISAIP  0x08
/* CRCCNTL0 */
#define SNDCRC  0x10

/* Script relocation constants */
#define RELOC_REGISTER 0x60000000
#define RELOC_LABEL_A  0x50000000
#define RELOC_LABEL_B  0x80000000
#define RELOC_SOFTC    0x40000000
#define RELOC_MASK     0xf0000000
#define OPC_MOVE       0x08000000

/* Feature flags */
#define FE_LED0     (1<<0)
#define FE_WIDE     (1<<1)
#define FE_ULTRA    (1<<2)
#define FE_ULTRA2   (1<<3)
#define FE_DBLR     (1<<4)
#define FE_QUAD     (1<<5)
#define FE_ERL      (1<<6)
#define FE_CLSE     (1<<7)
#define FE_WRIE     (1<<8)
#define FE_ERMP     (1<<9)
#define FE_BOF      (1<<10)
#define FE_DFS      (1<<11)
#define FE_PFEN     (1<<12)
#define FE_LDSTR    (1<<13)
#define FE_RAM      (1<<14)
#define FE_VARCLK   (1<<15)
#define FE_RAM8K    (1<<16)
#define FE_64BIT    (1<<17)
#define FE_IO256    (1<<18)
#define FE_NOPM     (1<<19)
#define FE_LEDC     (1<<20)
#define FE_ULTRA3   (1<<21)
#define FE_66MHZ    (1<<22)
#define FE_CRC      (1<<23)
#define FE_DIFF     (1<<24)
#define FE_DFBC     (1<<25)
#define FE_LCKFRQ   (1<<26)
#define FE_C10      (1<<27)
#define FE_U3EN     (1<<28)
#define FE_DAC      (1<<29)
#define FE_ISTAT1   (1<<30)

#define FE_CACHE_SET (FE_ERL|FE_CLSE|FE_WRIE|FE_ERMP)
#define FE_CACHE0_SET (FE_CACHE_SET & ~FE_ERL)

/* Register structure definition */
struct SymReg {
    /*00*/ uint8_t nc_scntl0;
    /*01*/ uint8_t nc_scntl1;
    /*02*/ uint8_t nc_scntl2;
    /*03*/ uint8_t nc_scntl3;
    /*04*/ uint8_t nc_scid;
    /*05*/ uint8_t nc_sxfer;
    /*06*/ uint8_t nc_sdid;
    /*07*/ uint8_t nc_gpreg;
    /*08*/ uint8_t nc_sfbr;
    /*09*/ uint8_t nc_socl;
    /*0a*/ uint8_t nc_ssid;
    /*0b*/ uint8_t nc_sbcl;
    /*0c*/ uint8_t nc_dstat;
    /*0d*/ uint8_t nc_sstat0;
    /*0e*/ uint8_t nc_sstat1;
    /*0f*/ uint8_t nc_sstat2;
    /*10*/ uint8_t nc_dsa;
    /*11*/ uint8_t nc_dsa1;
    /*12*/ uint8_t nc_dsa2;
    /*13*/ uint8_t nc_dsa3;
    /*14*/ uint8_t nc_istat;
    /*15*/ uint8_t nc_istat1;  /* 896 only */
    /*16*/ uint8_t nc_mbox0;   /* 896 only */
    /*17*/ uint8_t nc_mbox1;   /* 896 only */
    /*18*/ uint8_t nc_ctest0;
    /*19*/ uint8_t nc_ctest1;
    /*1a*/ uint8_t nc_ctest2;
    /*1b*/ uint8_t nc_ctest3;
    /*1c*/ uint32_t nc_temp;
    /*20*/ uint8_t nc_dfifo;
    /*21*/ uint8_t nc_ctest4;
    /*22*/ uint8_t nc_ctest5;
    /*23*/ uint8_t nc_ctest6;
    /*24*/ uint32_t nc_dbc;
    /*28*/ uint32_t nc_dnad;
    /*2c*/ uint32_t nc_dsp;
    /*30*/ uint32_t nc_dsps;
    /*34*/ uint8_t nc_scratcha;
    /*35*/ uint8_t nc_scratcha1;
    /*36*/ uint8_t nc_scratcha2;
    /*37*/ uint8_t nc_scratcha3;
    /*38*/ uint8_t nc_dmode;
    /*39*/ uint8_t nc_dien;
    /*3a*/ uint8_t nc_sbr;
    /*3b*/ uint8_t nc_dcntl;
    /*3c*/ uint32_t nc_adder;
    /*40*/ uint16_t nc_sien;
    /*42*/ uint16_t nc_sist;
    /*44*/ uint8_t nc_slpar;
    /*45*/ uint8_t nc_swide;
    /*46*/ uint8_t nc_macntl;
    /*47*/ uint8_t nc_gpcntl;
    /*48*/ uint8_t nc_stime0;
    /*49*/ uint8_t nc_stime1;
    /*4a*/ uint16_t nc_respid;
    /*4c*/ uint8_t nc_stest0;
    /*4d*/ uint8_t nc_stest1;
    /*4e*/ uint8_t nc_stest2;
    /*4f*/ uint8_t nc_stest3;
    /*50*/ uint16_t nc_sidl;
    /*52*/ uint8_t nc_stest4;
    /*53*/ uint8_t nc_53_;
    /*54*/ uint16_t nc_sodl;
    /*56*/ uint8_t nc_ccntl0;
    /*57*/ uint8_t nc_ccntl1;
    /*58*/ uint16_t nc_sbdl;
    /*5a*/ uint16_t nc_5a_;
    /*5c*/ uint8_t nc_scr0;
    /*5d*/ uint8_t nc_scr1;
    /*5e*/ uint8_t nc_scr2;
    /*5f*/ uint8_t nc_scr3;
    /*60*/ uint8_t nc_scrx[64];
    /*a0*/ uint32_t nc_mmrs;
    /*a4*/ uint32_t nc_mmws;
    /*a8*/ uint32_t nc_sfs;
    /*ac*/ uint32_t nc_drs;
    /*b0*/ uint32_t nc_sbms;
    /*b4*/ uint32_t nc_dbms;
    /*b8*/ uint32_t nc_dnad64;
    /*bc*/ uint16_t nc_scntl4;
    /*be*/ uint8_t nc_aipcntl0;
    /*bf*/ uint8_t nc_aipcntl1;
    /*c0*/ uint32_t nc_pmjad1;
    /*c4*/ uint32_t nc_pmjad2;
    /*c8*/ uint8_t nc_rbc;
    /*c9*/ uint8_t nc_rbc1;
    /*ca*/ uint8_t nc_rbc2;
    /*cb*/ uint8_t nc_rbc3;
    /*cc*/ uint8_t nc_ua;
    /*cd*/ uint8_t nc_ua1;
    /*ce*/ uint8_t nc_ua2;
    /*cf*/ uint8_t nc_ua3;
    /*d0*/ uint32_t nc_esa;
    /*d4*/ uint8_t nc_ia;
    /*d5*/ uint8_t nc_ia1;
    /*d6*/ uint8_t nc_ia2;
    /*d7*/ uint8_t nc_ia3;
    /*d8*/ uint32_t nc_sbc;
    /*dc*/ uint32_t nc_csbc;
    /*e0*/ uint16_t nc_crcpad;
    /*e2*/ uint8_t nc_crccntl0;
    /*e3*/ uint8_t nc_crccntl1;
    /*e4*/ uint32_t nc_crcdata;
    /*e8*/ uint32_t nc_e8_;
    /*ec*/ uint32_t nc_ec_;
    /*f0*/ uint16_t nc_dfbc;
} __attribute__((packed));

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

    /* Resource Management */
    MemoryRegion mmio_region;
    MemoryRegion pio_region;

    /* Hardware Register Shadows */
    struct SymReg reg;

    /* State for reset */
    int reset_seq;
};

/* Forward declaration */
static void pcibase_reset(DeviceState *dev);

/* Update IRQ based on ISTAT and enable bits */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t istat = s->reg.nc_istat;
    uint16_t sist = s->reg.nc_sist;
    uint16_t sien = s->reg.nc_sien;
    int level = 0;

    /* Check if any enabled interrupt condition is present */
    if ((istat & (CABRT | SRST | SIGP | CON | INTF | SIP | DIP)) &&
        (istat & INTF)) {
        level = 1;
    }
    if (sist & sien) {
        level = 1;
    }

    pci_set_irq(pdev, level);
}

/* Handle write to ISTAT register */
static void pcibase_write_istat(PCIBaseState *s, uint8_t val)
{
    uint8_t old = s->reg.nc_istat;
    uint8_t new_val;

    /* SRST: soft reset */
    if (val & SRST) {
        /* Perform chip reset */
        pcibase_reset(DEVICE(s));
        return;
    }

    /* Some bits are write-1-to-clear */
    /* CABRT, INTF, SIP, DIP are typically cleared by writing 1 to the respective bit.
       The driver writes the bit to clear it.*/
    new_val = old;
    if (val & CABRT) {
        new_val &= ~CABRT;
    }
    if (val & INTF) {
        new_val &= ~INTF;
    }
    if (val & SIP) {
        new_val &= ~SIP;
    }
    if (val & DIP) {
        new_val &= ~DIP;
    }
    /* Other bits like SIGP, SEM, CON may be set by driver */
    new_val |= (val & (SIGP | SEM | CON));

    s->reg.nc_istat = new_val;
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= sizeof(struct SymReg)) {
        /* Out of range, return 0 */
        return 0;
    }

    /* Direct read from shadow register bytes */
    uint8_t *reg_bytes = (uint8_t *)&s->reg;
    switch (size) {
    case 1:
        val = reg_bytes[addr];
        break;
    case 2:
        /* Handle endianness: assume little-endian, word at addr and addr+1 */
        memcpy(&val, &reg_bytes[addr], 2);
        break;
    case 4:
        memcpy(&val, &reg_bytes[addr], 4);
        break;
    case 8:
        memcpy(&val, &reg_bytes[addr], 8);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "sym53c8xx: unimplemented read size %u\n", size);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= sizeof(struct SymReg)) {
        return;
    }

    uint8_t *reg_bytes = (uint8_t *)&s->reg;

    switch (size) {
    case 1:
        if (addr == offsetof(struct SymReg, nc_istat)) {
            pcibase_write_istat(s, val);
        } else {
            reg_bytes[addr] = val;
        }
        break;
    case 2:
        memcpy(&reg_bytes[addr], &val, 2);
        break;
    case 4:
        memcpy(&reg_bytes[addr], &val, 4);
        break;
    case 8:
        memcpy(&reg_bytes[addr], &val, 8);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "sym53c8xx: unimplemented write size %u\n", size);
        break;
    }

    /* Update IRQ after any write that may affect interrupt status */
    pcibase_update_irq(s);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* PIO same as MMIO */
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

    /* Reset all registers to default values */
    memset(&s->reg, 0, sizeof(s->reg));

    /* Set default values for specific registers per chip reset state */
    /* SCNTL1: IARB set (arbitration enabled) */
    s->reg.nc_scntl1 = IARB;
    /* SCNTL3: typically ULTRA clear, etc. */
    /* SSTAT1: default value could be 0 */
    /* STEST1: SCLK set if external clock, but we keep off */
    /* ISTAT: after reset, should be 0 */
    /* CTEST: some defaults */

    s->reset_seq = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_LSI_LOGIC );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_NCR_53C810 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_SCSI );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Configure PCIe capability */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR0: PIO, size 256 */
    memory_region_init_io(&s->pio_region, OBJECT(s), &pcibase_pio_ops, s,
                          "sym53c8xx-pio", 0x100);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->pio_region);

    /* BAR1: MMIO, size 256 */
    memory_region_init_io(&s->mmio_region, OBJECT(s), &pcibase_mmio_ops, s,
                          "sym53c8xx-mmio", 0x100);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio_region);

    /* BAR2 not used (no RAM on 53C810) */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No MSI or extra resources */
}

/* Minimal VMState */
static const VMStateDescription vmstate_pcibase = {
    .name = "sym53c8xx_pci",
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
