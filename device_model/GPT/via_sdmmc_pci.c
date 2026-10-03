/*
 * QEMU PCI device model for VIA SDMMC (via-sdmmc.c)
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


#define TYPE_PCIBASE_DEVICE "via_sdmmc_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1106
#define DEVICE_ID 0x9530
#define CLASS_ID 0x0000
#define VIA_CRDR_SDC_OFF 0x200
#define VIA_CRDR_DDMA_OFF 0x400
#define VIA_CRDR_PCICTRL_OFF 0x600
#define VIA_CRDR_PCI_WORK_MODE 0x40
#define VIA_CRDR_PCI_DBG_MODE 0x41
#define VIA_CRDR_SDCTRL 0x0
#define VIA_CRDR_SDCTRL_START 0x01
#define VIA_CRDR_SDCTRL_WRITE 0x04
#define VIA_CRDR_SDCTRL_SINGLE_WR 0x10
#define VIA_CRDR_SDCTRL_SINGLE_RD 0x20
#define VIA_CRDR_SDCTRL_MULTI_WR 0x30
#define VIA_CRDR_SDCTRL_MULTI_RD 0x40
#define VIA_CRDR_SDCTRL_STOP 0x70
#define VIA_CRDR_SDCTRL_RSP_NONE 0x0
#define VIA_CRDR_SDCTRL_RSP_R1 0x10000
#define VIA_CRDR_SDCTRL_RSP_R2 0x20000
#define VIA_CRDR_SDCTRL_RSP_R3 0x30000
#define VIA_CRDR_SDCTRL_RSP_R1B 0x90000
#define VIA_CRDR_SDCARG 0x4
#define VIA_CRDR_SDBUSMODE 0x8
#define VIA_CRDR_SDMODE_4BIT 0x02
#define VIA_CRDR_SDMODE_CLK_ON 0x40
#define VIA_CRDR_SDBLKLEN 0xc
#define VIA_CRDR_SDBLKLEN_GPIDET 0x2000
#define VIA_CRDR_SDBLKLEN_INTEN 0x8000
#define VIA_CRDR_MAX_BLOCK_COUNT 65536
#define VIA_CRDR_MAX_BLOCK_LENGTH 2048
#define VIA_CRDR_SDRESP0 0x10
#define VIA_CRDR_SDRESP1 0x14
#define VIA_CRDR_SDRESP2 0x18
#define VIA_CRDR_SDRESP3 0x1c
#define VIA_CRDR_SDCURBLKCNT 0x20
#define VIA_CRDR_SDINTMASK 0x24
#define VIA_CRDR_SDINTMASK_MBDIE 0x10
#define VIA_CRDR_SDINTMASK_BDDIE 0x20
#define VIA_CRDR_SDINTMASK_CIRIE 0x80
#define VIA_CRDR_SDINTMASK_CRDIE 0x200
#define VIA_CRDR_SDINTMASK_CRTOIE 0x400
#define VIA_CRDR_SDINTMASK_ASCRDIE 0x800
#define VIA_CRDR_SDINTMASK_DTIE 0x1000
#define VIA_CRDR_SDINTMASK_SCIE 0x2000
#define VIA_CRDR_SDINTMASK_RCIE 0x4000
#define VIA_CRDR_SDINTMASK_WCIE 0x8000
#define VIA_CRDR_SDACTIVE_INTMASK (VIA_CRDR_SDINTMASK_MBDIE | VIA_CRDR_SDINTMASK_CIRIE | VIA_CRDR_SDINTMASK_CRDIE | VIA_CRDR_SDINTMASK_CRTOIE | VIA_CRDR_SDINTMASK_DTIE | VIA_CRDR_SDINTMASK_SCIE | VIA_CRDR_SDINTMASK_RCIE | VIA_CRDR_SDINTMASK_WCIE)
#define VIA_CRDR_SDSTATUS 0x28
#define VIA_CRDR_SDSTS_CECC 0x01
#define VIA_CRDR_SDSTS_WP 0x02
#define VIA_CRDR_SDSTS_SLOTD 0x04
#define VIA_CRDR_SDSTS_SLOTG 0x08
#define VIA_CRDR_SDSTS_MBD 0x10
#define VIA_CRDR_SDSTS_BDD 0x20
#define VIA_CRDR_SDSTS_CD 0x40
#define VIA_CRDR_SDSTS_CIR 0x80
#define VIA_CRDR_SDSTS_IO 0x100
#define VIA_CRDR_SDSTS_CRD 0x200
#define VIA_CRDR_SDSTS_CRTO 0x400
#define VIA_CRDR_SDSTS_ASCRDIE 0x800
#define VIA_CRDR_SDSTS_DT 0x1000
#define VIA_CRDR_SDSTS_SC 0x2000
#define VIA_CRDR_SDSTS_RC 0x4000
#define VIA_CRDR_SDSTS_WC 0x8000
#define VIA_CRDR_SDSTS_IGN_MASK (VIA_CRDR_SDSTS_BDD | VIA_CRDR_SDSTS_ASCRDIE | VIA_CRDR_SDSTS_IO)
#define VIA_CRDR_SDSTS_INT_MASK (VIA_CRDR_SDSTS_MBD | VIA_CRDR_SDSTS_BDD | VIA_CRDR_SDSTS_CD | VIA_CRDR_SDSTS_CIR | VIA_CRDR_SDSTS_IO | VIA_CRDR_SDSTS_CRD | VIA_CRDR_SDSTS_CRTO | VIA_CRDR_SDSTS_ASCRDIE | VIA_CRDR_SDSTS_DT | VIA_CRDR_SDSTS_SC | VIA_CRDR_SDSTS_RC | VIA_CRDR_SDSTS_WC)
#define VIA_CRDR_SDSTS_W1C_MASK (VIA_CRDR_SDSTS_CECC | VIA_CRDR_SDSTS_MBD | VIA_CRDR_SDSTS_BDD | VIA_CRDR_SDSTS_CD | VIA_CRDR_SDSTS_CIR | VIA_CRDR_SDSTS_CRD | VIA_CRDR_SDSTS_CRTO | VIA_CRDR_SDSTS_ASCRDIE | VIA_CRDR_SDSTS_DT | VIA_CRDR_SDSTS_SC | VIA_CRDR_SDSTS_RC | VIA_CRDR_SDSTS_WC)
#define VIA_CRDR_SDSTS_CMD_MASK (VIA_CRDR_SDSTS_CRD | VIA_CRDR_SDSTS_CRTO | VIA_CRDR_SDSTS_SC)
#define VIA_CRDR_SDSTS_DATA_MASK (VIA_CRDR_SDSTS_MBD | VIA_CRDR_SDSTS_DT | VIA_CRDR_SDSTS_RC | VIA_CRDR_SDSTS_WC)
#define VIA_CRDR_SDSTATUS2 0x2a
#define VIA_CRDR_SDSTS_CFE 0x80
#define VIA_CRDR_SDRSPTMO 0x2C
#define VIA_CRDR_SDCLKSEL 0x30
#define VIA_CRDR_SDEXTCTRL 0x34
#define VIS_CRDR_SDEXTCTRL_AUTOSTOP_SD 0x01
#define VIS_CRDR_SDEXTCTRL_SHIFT_9 0x02
#define VIS_CRDR_SDEXTCTRL_MMC_8BIT 0x04
#define VIS_CRDR_SDEXTCTRL_RELD_BLK 0x08
#define VIS_CRDR_SDEXTCTRL_BAD_CMDA 0x10
#define VIS_CRDR_SDEXTCTRL_BAD_DATA 0x20
#define VIS_CRDR_SDEXTCTRL_AUTOSTOP_SPI 0x40
#define VIA_CRDR_SDEXTCTRL_HISPD 0x80
#define VIA_CRDR_DMABASEADD 0x0
#define VIA_CRDR_DMACOUNTER 0x4
#define VIA_CRDR_DMACTRL 0x8
#define VIA_CRDR_DMACTRL_DIR 0x100
#define VIA_CRDR_DMACTRL_ENIRQ 0x10000
#define VIA_CRDR_DMACTRL_SFTRST 0x1000000
#define VIA_CRDR_DMASTS 0xc
#define VIA_CRDR_DMASTART 0x10
#define VIA_CRDR_PCICLKGATT 0x2
#define VIA_CRDR_PCICLKGATT_SFTRST 0x01
#define VIA_CRDR_PCICLKGATT_3V3 0x10
#define VIA_CRDR_PCICLKGATT_PAD_PWRON 0x20
#define VIA_CRDR_PCISDCCLK 0x5
#define VIA_CRDR_PCIDMACLK 0x7
#define VIA_CRDR_PCIDMACLK_SDC 0x2
#define VIA_CRDR_PCIINTCTRL 0x8
#define VIA_CRDR_PCIINTCTRL_SDCIRQEN 0x04
#define VIA_CRDR_PCIINTSTATUS 0x9
#define VIA_CRDR_PCIINTSTATUS_SDC 0x04
#define VIA_CRDR_PCITMOCTRL 0xa
#define VIA_CRDR_PCITMOCTRL_NO 0x0
#define VIA_CRDR_PCITMOCTRL_32US 0x1
#define VIA_CRDR_PCITMOCTRL_256US 0x2
#define VIA_CRDR_PCITMOCTRL_1024US 0x3
#define VIA_CRDR_PCITMOCTRL_256MS 0x4
#define VIA_CRDR_PCITMOCTRL_512MS 0x5
#define VIA_CRDR_PCITMOCTRL_1024MS 0x6
#define VIA_CRDR_QUIRK_300MS_PWRDELAY 0x0001
#define VIA_CMD_TIMEOUT_MS 1000

typedef enum {
    PCI_CLK_375K = 0x03,
    PCI_CLK_8M = 0x04,
    PCI_CLK_12M = 0x00,
    PCI_CLK_16M = 0x05,
    PCI_CLK_24M = 0x01,
    PCI_CLK_33M = 0x06,
    PCI_CLK_48M = 0x02
} PCI_HOST_CLK_CONTROL;

typedef enum BARType {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM,
} BARType;

typedef struct BARInfo {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
} BARInfo;

struct sdhcreg {
    uint32_t sdcontrol_reg;
    uint32_t sdcmdarg_reg;
    uint32_t sdbusmode_reg;
    uint32_t sdblklen_reg;
    uint32_t sdresp_reg[4];
    uint32_t sdcurblkcnt_reg;
    uint32_t sdintmask_reg;
    uint32_t sdstatus_reg;
    uint32_t sdrsptmo_reg;
    uint32_t sdclksel_reg;
    uint32_t sdextctrl_reg;
};

struct pcictrlreg {
    uint8_t reserve[2];
    uint8_t pciclkgat_reg;
    uint8_t pcinfcclk_reg;
    uint8_t pcimscclk_reg;
    uint8_t pcisdclk_reg;
    uint8_t pcicaclk_reg;
    uint8_t pcidmaclk_reg;
    uint8_t pciintctrl_reg;
    uint8_t pciintstatus_reg;
    uint8_t pcitmoctrl_reg;
    uint8_t Resv;
};

typedef struct via_crdr_mmc_host_state {
    /* SDHC regs shadow */
    uint32_t sdctrl;
    uint32_t sdcarg;
    uint32_t sdbusmode;
    uint32_t sdblklen;
    uint32_t sdresp[4];
    uint32_t sdcurblkcnt;
    uint32_t sdintmask;
    uint16_t sdstatus;
    uint8_t  sdstatus2;
    uint32_t sdrsptmo;
    uint32_t sdclksel;
    uint32_t sdextctrl;

    /* DDMA regs shadow */
    uint32_t dmabaseadd;
    uint32_t dmacounter;
    uint32_t dmactrl;
    uint32_t dmasts;

    /* PCI control regs shadow (byte accesses) */
    uint8_t pciclkgatt;
    uint8_t pcisdclk;
    uint8_t pcidmaclk;
    uint8_t pciintctrl;
    uint8_t pciintstatus;
    uint8_t pcitmoctrl;
} via_crdr_mmc_host_state;

typedef struct via_crdr_mmc_host_save {
    struct sdhcreg pm_sdhc_reg;
    struct pcictrlreg pm_pcictrl_reg;
} via_crdr_mmc_host_save;

typedef struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;
    struct sdhcreg pm_sdhc_reg;
    struct pcictrlreg pm_pcictrl_reg;

    via_crdr_mmc_host_state regs;
} PCIBaseState;

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Interrupt is raised when PCI status SDC bit is set and enabled */
    if ((s->regs.pciintctrl & VIA_CRDR_PCIINTCTRL_SDCIRQEN) &&
        (s->regs.pciintstatus & VIA_CRDR_PCIINTSTATUS_SDC)) {
        if (msi_enabled(pdev)) {
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

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)is_write;

    /*
     * Driver only checks for timeouts and uses SFTRST on errors.
     * We simply mark transfer done immediately and set appropriate status
     * bits so that the ISR path sees data-complete and/or command-complete.
     */
    if (!(s->regs.dmactrl & VIA_CRDR_DMACTRL_SFTRST) &&
        (s->regs.dmacounter != 0)) {
        /* Fake DMA completion, do not touch guest memory (format unknown) */

        /* Clear DMA counter and mark some completion in DMASTS (opaque) */
        s->regs.dmacounter = 0;
        s->regs.dmasts = 0x1;

        /* Signal data done in SDSTATUS (DT flag) */
        s->regs.sdstatus |= VIA_CRDR_SDSTS_DT;
        /* Optionally mark block done */
        s->regs.sdstatus |= VIA_CRDR_SDSTS_MBD;

        /* For commands with data, also set command done */
        s->regs.sdstatus |= VIA_CRDR_SDSTS_CRD;

        /* Set PCI interrupt status SDC bit */
        s->regs.pciintstatus |= VIA_CRDR_PCIINTSTATUS_SDC;
        pcibase_update_irq(s);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* BAR0: treat as single MMIO space; sub-blocks via offsets */
    if (addr < VIA_CRDR_SDC_OFF) {
        /* unmapped region, return 0 */
        return 0;
    }

    if (addr >= VIA_CRDR_SDC_OFF && addr < VIA_CRDR_DDMA_OFF) {
        /* SDHC block */
        hwaddr o = addr - VIA_CRDR_SDC_OFF;
        switch (o) {
        case VIA_CRDR_SDCTRL:
            val = s->regs.sdctrl;
            break;
        case VIA_CRDR_SDCARG:
            val = s->regs.sdcarg;
            break;
        case VIA_CRDR_SDBUSMODE:
            val = s->regs.sdbusmode;
            break;
        case VIA_CRDR_SDBLKLEN:
            val = s->regs.sdblklen;
            break;
        case VIA_CRDR_SDRESP0:
            val = s->regs.sdresp[0];
            break;
        case VIA_CRDR_SDRESP1:
            val = s->regs.sdresp[1];
            break;
        case VIA_CRDR_SDRESP2:
            val = s->regs.sdresp[2];
            break;
        case VIA_CRDR_SDRESP3:
            val = s->regs.sdresp[3];
            break;
        case VIA_CRDR_SDCURBLKCNT:
            val = s->regs.sdcurblkcnt;
            break;
        case VIA_CRDR_SDINTMASK:
            val = s->regs.sdintmask;
            break;
        case VIA_CRDR_SDSTATUS:
            /* 16-bit */
            val = s->regs.sdstatus;
            break;
        case VIA_CRDR_SDSTATUS2:
            /* 8-bit at +0x2a; honor size */
            if (size == 1) {
                val = s->regs.sdstatus2;
            } else {
                val = s->regs.sdstatus2;
            }
            break;
        case VIA_CRDR_SDRSPTMO:
            val = s->regs.sdrsptmo;
            break;
        case VIA_CRDR_SDCLKSEL:
            val = s->regs.sdclksel;
            break;
        case VIA_CRDR_SDEXTCTRL:
            val = s->regs.sdextctrl;
            break;
        default:
            val = 0;
            break;
        }
        return val;
    }

    if (addr >= VIA_CRDR_DDMA_OFF && addr < VIA_CRDR_PCICTRL_OFF) {
        /* DDMA block */
        hwaddr o = addr - VIA_CRDR_DDMA_OFF;
        switch (o) {
        case VIA_CRDR_DMABASEADD:
            val = s->regs.dmabaseadd;
            break;
        case VIA_CRDR_DMACOUNTER:
            val = s->regs.dmacounter;
            break;
        case VIA_CRDR_DMACTRL:
            val = s->regs.dmactrl;
            break;
        case VIA_CRDR_DMASTS:
            val = s->regs.dmasts;
            break;
        case VIA_CRDR_DMASTART:
            /* DMASTART is write-only, return 0 */
            val = 0;
            break;
        default:
            val = 0;
            break;
        }
        return val;
    }

    if (addr >= VIA_CRDR_PCICTRL_OFF) {
        /* PCI control block */
        hwaddr o = addr - VIA_CRDR_PCICTRL_OFF;
        switch (o) {
        case VIA_CRDR_PCICLKGATT:
            if (size == 1) {
                val = s->regs.pciclkgatt;
            }
            break;
        case VIA_CRDR_PCISDCCLK:
            if (size == 1) {
                val = s->regs.pcisdclk;
            }
            break;
        case VIA_CRDR_PCIDMACLK:
            if (size == 1) {
                val = s->regs.pcidmaclk;
            }
            break;
        case VIA_CRDR_PCIINTCTRL:
            if (size == 1) {
                val = s->regs.pciintctrl;
            }
            break;
        case VIA_CRDR_PCIINTSTATUS:
            if (size == 1) {
                val = s->regs.pciintstatus;
            }
            break;
        case VIA_CRDR_PCITMOCTRL:
            if (size == 1) {
                val = s->regs.pcitmoctrl;
            }
            break;
        default:
            val = 0;
            break;
        }
        return val;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, uint64_t addr64, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    hwaddr addr = (hwaddr)addr64;

    if (addr < VIA_CRDR_SDC_OFF) {
        /* unmapped */
        return;
    }

    if (addr >= VIA_CRDR_SDC_OFF && addr < VIA_CRDR_DDMA_OFF) {
        /* SDHC block */
        hwaddr o = addr - VIA_CRDR_SDC_OFF;
        switch (o) {
        case VIA_CRDR_SDCTRL:
            /* 32-bit */
            s->regs.sdctrl = (uint32_t)val;
            /* If START bit set, simulate immediate command completion by
             * setting command done flag and raising interrupt. Response
             * contents are opaque to the driver for most commands; we
             * just leave zeros.
             */
            if (s->regs.sdctrl & VIA_CRDR_SDCTRL_START) {
                s->regs.sdstatus |= VIA_CRDR_SDSTS_CRD;
                /* Also mark slot present & changed */
                s->regs.sdstatus |= VIA_CRDR_SDSTS_SLOTG;
                s->regs.sdstatus |= VIA_CRDR_SDSTS_CD;
                /* Trigger PCI interrupt */
                s->regs.pciintstatus |= VIA_CRDR_PCIINTSTATUS_SDC;
                pcibase_update_irq(s);
            }
            break;
        case VIA_CRDR_SDCARG:
            s->regs.sdcarg = (uint32_t)val;
            break;
        case VIA_CRDR_SDBUSMODE:
            s->regs.sdbusmode = (uint32_t)val;
            break;
        case VIA_CRDR_SDBLKLEN:
            s->regs.sdblklen = (uint32_t)val;
            break;
        case VIA_CRDR_SDCURBLKCNT:
            s->regs.sdcurblkcnt = (uint32_t)val;
            break;
        case VIA_CRDR_SDINTMASK:
            s->regs.sdintmask = (uint32_t)val;
            break;
        case VIA_CRDR_SDSTATUS:
            /* 16-bit W1C for masked bits */
            {
                uint16_t w = (uint16_t)val;
                uint16_t w1c = w & VIA_CRDR_SDSTS_W1C_MASK;
                s->regs.sdstatus &= ~w1c;
            }
            break;
        case VIA_CRDR_SDSTATUS2:
            /* write 8-bit; driver sets CFE bit */
            if (size == 1) {
                uint8_t w = (uint8_t)val;
                /* This register seems to be plain RW, driver ORs CFE and
                 * writes back. We emulate simple write.
                 */
                s->regs.sdstatus2 = w;
            }
            break;
        case VIA_CRDR_SDRSPTMO:
            s->regs.sdrsptmo = (uint32_t)val;
            break;
        case VIA_CRDR_SDCLKSEL:
            s->regs.sdclksel = (uint32_t)val;
            break;
        case VIA_CRDR_SDEXTCTRL:
            s->regs.sdextctrl = (uint32_t)val;
            break;
        default:
            break;
        }
        return;
    }

    if (addr >= VIA_CRDR_DDMA_OFF && addr < VIA_CRDR_PCICTRL_OFF) {
        /* DDMA block */
        hwaddr o = addr - VIA_CRDR_DDMA_OFF;
        switch (o) {
        case VIA_CRDR_DMABASEADD:
            s->regs.dmabaseadd = (uint32_t)val;
            break;
        case VIA_CRDR_DMACOUNTER:
            s->regs.dmacounter = (uint32_t)val;
            break;
        case VIA_CRDR_DMACTRL:
            s->regs.dmactrl = (uint32_t)val;
            break;
        case VIA_CRDR_DMASTART:
            /* any write starts DMA */
            pcibase_do_dma(s, (s->regs.dmactrl & VIA_CRDR_DMACTRL_DIR) != 0);
            break;
        case VIA_CRDR_DMASTS:
            /* assume W1C for all bits */
            s->regs.dmasts &= ~(uint32_t)val;
            break;
        default:
            break;
        }
        return;
    }

    if (addr >= VIA_CRDR_PCICTRL_OFF) {
        /* PCI control block */
        hwaddr o = addr - VIA_CRDR_PCICTRL_OFF;
        switch (o) {
        case VIA_CRDR_PCICLKGATT:
            if (size == 1) {
                s->regs.pciclkgatt = (uint8_t)val;
            }
            break;
        case VIA_CRDR_PCISDCCLK:
            if (size == 1) {
                s->regs.pcisdclk = (uint8_t)val;
            }
            break;
        case VIA_CRDR_PCIDMACLK:
            if (size == 1) {
                s->regs.pcidmaclk = (uint8_t)val;
            }
            break;
        case VIA_CRDR_PCIINTCTRL:
            if (size == 1) {
                s->regs.pciintctrl = (uint8_t)val;
                pcibase_update_irq(s);
            }
            break;
        case VIA_CRDR_PCIINTSTATUS:
            if (size == 1) {
                /* treat as W1C */
                uint8_t w = (uint8_t)val;
                s->regs.pciintstatus &= ~w;
                pcibase_update_irq(s);
            }
            break;
        case VIA_CRDR_PCITMOCTRL:
            if (size == 1) {
                s->regs.pcitmoctrl = (uint8_t)val;
            }
            break;
        default:
            break;
        }
        return;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* This device does not use IO-space BARs in the driver; keep stub. */
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Unused in driver */
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
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

static hwaddr pcibase_pow2ceil(hwaddr size)
{
    if (size <= 1) {
        return 1;
    }
    return 1ULL << (64 - clz64(size - 1));
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Initialize shadow registers to power-on values expected by driver */
    memset(&s->regs, 0, sizeof(s->regs));

    /* SDSTATUS2 CFE bit set via init */
    s->regs.sdstatus2 = 0;

    /* Card present: SLOTG and CD bits set so that request() sees medium */
    s->regs.sdstatus |= VIA_CRDR_SDSTS_SLOTG;
    s->regs.sdstatus |= VIA_CRDR_SDSTS_CD;

    /* PCICLKGATT default: 3V3 + PAD_PWRON */
    s->regs.pciclkgatt = VIA_CRDR_PCICLKGATT_3V3 | VIA_CRDR_PCICLKGATT_PAD_PWRON;

    /* SDINTMASK cleared at init, then SDC_ACTIVE_INTMASK written later */
    s->regs.sdintmask = 0;

    /* Timeout control default */
    s->regs.pcitmoctrl = VIA_CRDR_PCITMOCTRL_NO;

    /* Clear PCI interrupt status */
    s->regs.pciintstatus = 0;
    pcibase_update_irq(s);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    /* CRITICAL: PCI requires BAR sizes to be a power of 2. Prevent QEMU assert crash. */
    hwaddr aligned_size = pcibase_pow2ceil(bi->size);
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

    /* BAR0 only is used by the driver */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    /* Minimal region large enough for SDC + DDMA + PCICTRL offsets */
    s->bar_info[0].size = 0x800; /* 2KB, rounded to power of two later */
    s->bar_info[0].name = "via-sdmmc-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X described in driver; leave disabled */

    /* Reset internal state */
    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, 0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "via_sdmmc_pci",
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
