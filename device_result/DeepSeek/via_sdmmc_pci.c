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

#define TYPE_PCIBASE_DEVICE "via_sdmmc_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_VIA     0x1106
#define PCI_DEVICE_ID_VIA_9530 0x9530
#define PCI_CLASS_SYSTEM_SDHCI 0x0805

/* Overall BAR offsets */
#define VIA_CRDR_SDC_OFF       0x200
#define VIA_CRDR_DDMA_OFF      0x400
#define VIA_CRDR_PCICTRL_OFF   0x600

/* SDC Block Registers (relative to SDC base) */
#define VIA_CRDR_SDCTRL               0x0
#define VIA_CRDR_SDCTRL_START         0x01
#define VIA_CRDR_SDCTRL_WRITE         0x04
#define VIA_CRDR_SDCTRL_SINGLE_WR     0x10
#define VIA_CRDR_SDCTRL_SINGLE_RD     0x20
#define VIA_CRDR_SDCTRL_MULTI_WR      0x30
#define VIA_CRDR_SDCTRL_MULTI_RD      0x40
#define VIA_CRDR_SDCTRL_STOP          0x70
#define VIA_CRDR_SDCTRL_RSP_NONE      0x0
#define VIA_CRDR_SDCTRL_RSP_R1        0x10000
#define VIA_CRDR_SDCTRL_RSP_R2        0x20000
#define VIA_CRDR_SDCTRL_RSP_R3        0x30000
#define VIA_CRDR_SDCTRL_RSP_R1B       0x90000
#define VIA_CRDR_SDCARG               0x4
#define VIA_CRDR_SDBUSMODE            0x8
#define VIA_CRDR_SDMODE_4BIT          0x02
#define VIA_CRDR_SDMODE_CLK_ON        0x40
#define VIA_CRDR_SDBLKLEN             0xc
#define VIA_CRDR_SDBLKLEN_GPIDET      0x2000
#define VIA_CRDR_SDBLKLEN_INTEN       0x8000
#define VIA_CRDR_MAX_BLOCK_COUNT      65536
#define VIA_CRDR_MAX_BLOCK_LENGTH     2048
#define VIA_CRDR_SDRESP0              0x10
#define VIA_CRDR_SDRESP1              0x14
#define VIA_CRDR_SDRESP2              0x18
#define VIA_CRDR_SDRESP3              0x1c
#define VIA_CRDR_SDCURBLKCNT          0x20
#define VIA_CRDR_SDINTMASK            0x24
#define VIA_CRDR_SDINTMASK_MBDIE      0x10
#define VIA_CRDR_SDINTMASK_BDDIE      0x20
#define VIA_CRDR_SDINTMASK_CIRIE      0x80
#define VIA_CRDR_SDINTMASK_CRDIE      0x200
#define VIA_CRDR_SDINTMASK_CRTOIE     0x400
#define VIA_CRDR_SDINTMASK_ASCRDIE    0x800
#define VIA_CRDR_SDINTMASK_DTIE       0x1000
#define VIA_CRDR_SDINTMASK_SCIE       0x2000
#define VIA_CRDR_SDINTMASK_RCIE       0x4000
#define VIA_CRDR_SDINTMASK_WCIE       0x8000
#define VIA_CRDR_SDACTIVE_INTMASK \
    (VIA_CRDR_SDINTMASK_MBDIE | VIA_CRDR_SDINTMASK_CIRIE \
    | VIA_CRDR_SDINTMASK_CRDIE | VIA_CRDR_SDINTMASK_CRTOIE \
    | VIA_CRDR_SDINTMASK_DTIE | VIA_CRDR_SDINTMASK_SCIE \
    | VIA_CRDR_SDINTMASK_RCIE | VIA_CRDR_SDINTMASK_WCIE)
#define VIA_CRDR_SDSTATUS             0x28
#define VIA_CRDR_SDSTS_CECC           0x01
#define VIA_CRDR_SDSTS_WP             0x02
#define VIA_CRDR_SDSTS_SLOTD          0x04
#define VIA_CRDR_SDSTS_SLOTG          0x08
#define VIA_CRDR_SDSTS_MBD            0x10
#define VIA_CRDR_SDSTS_BDD            0x20
#define VIA_CRDR_SDSTS_CD             0x40
#define VIA_CRDR_SDSTS_CIR            0x80
#define VIA_CRDR_SDSTS_IO             0x100
#define VIA_CRDR_SDSTS_CRD            0x200
#define VIA_CRDR_SDSTS_CRTO           0x400
#define VIA_CRDR_SDSTS_ASCRDIE        0x800
#define VIA_CRDR_SDSTS_DT             0x1000
#define VIA_CRDR_SDSTS_SC             0x2000
#define VIA_CRDR_SDSTS_RC             0x4000
#define VIA_CRDR_SDSTS_WC             0x8000
#define VIA_CRDR_SDSTS_IGN_MASK \
    (VIA_CRDR_SDSTS_BDD | VIA_CRDR_SDSTS_ASCRDIE | VIA_CRDR_SDSTS_IO)
#define VIA_CRDR_SDSTS_INT_MASK \
    (VIA_CRDR_SDSTS_MBD | VIA_CRDR_SDSTS_BDD | VIA_CRDR_SDSTS_CD \
    | VIA_CRDR_SDSTS_CIR | VIA_CRDR_SDSTS_IO | VIA_CRDR_SDSTS_CRD \
    | VIA_CRDR_SDSTS_CRTO | VIA_CRDR_SDSTS_ASCRDIE | VIA_CRDR_SDSTS_DT \
    | VIA_CRDR_SDSTS_SC | VIA_CRDR_SDSTS_RC | VIA_CRDR_SDSTS_WC)
#define VIA_CRDR_SDSTS_W1C_MASK \
    (VIA_CRDR_SDSTS_CECC | VIA_CRDR_SDSTS_MBD | VIA_CRDR_SDSTS_BDD \
    | VIA_CRDR_SDSTS_CD | VIA_CRDR_SDSTS_CIR | VIA_CRDR_SDSTS_CRD \
    | VIA_CRDR_SDSTS_CRTO | VIA_CRDR_SDSTS_ASCRDIE | VIA_CRDR_SDSTS_DT \
    | VIA_CRDR_SDSTS_SC | VIA_CRDR_SDSTS_RC | VIA_CRDR_SDSTS_WC)
#define VIA_CRDR_SDSTS_CMD_MASK \
    (VIA_CRDR_SDSTS_CRD | VIA_CRDR_SDSTS_CRTO | VIA_CRDR_SDSTS_SC)
#define VIA_CRDR_SDSTS_DATA_MASK \
    (VIA_CRDR_SDSTS_MBD | VIA_CRDR_SDSTS_DT \
    | VIA_CRDR_SDSTS_RC | VIA_CRDR_SDSTS_WC)
#define VIA_CRDR_SDSTATUS2            0x2a
#define VIA_CRDR_SDSTS_CFE            0x80
#define VIA_CRDR_SDRSPTMO             0x2C
#define VIA_CRDR_SDCLKSEL             0x30
#define VIA_CRDR_SDEXTCTRL            0x34
#define VIS_CRDR_SDEXTCTRL_AUTOSTOP_SD   0x01
#define VIS_CRDR_SDEXTCTRL_SHIFT_9       0x02
#define VIS_CRDR_SDEXTCTRL_MMC_8BIT      0x04
#define VIS_CRDR_SDEXTCTRL_RELD_BLK      0x08
#define VIS_CRDR_SDEXTCTRL_BAD_CMDA      0x10
#define VIS_CRDR_SDEXTCTRL_BAD_DATA      0x20
#define VIS_CRDR_SDEXTCTRL_AUTOSTOP_SPI  0x40
#define VIA_CRDR_SDEXTCTRL_HISPD        0x80

/* DDMA Block Registers (relative to DDMA base) */
#define VIA_CRDR_DMABASEADD          0x0
#define VIA_CRDR_DMACOUNTER          0x4
#define VIA_CRDR_DMACTRL             0x8
#define VIA_CRDR_DMACTRL_DIR          0x100
#define VIA_CRDR_DMACTRL_ENIRQ        0x10000
#define VIA_CRDR_DMACTRL_SFTRST       0x1000000
#define VIA_CRDR_DMASTS              0xc
#define VIA_CRDR_DMASTART            0x10

/* PCICTRL Block Registers (relative to PCICTRL base) */
#define VIA_CRDR_PCICLKGATT          0x2
#define VIA_CRDR_PCICLKGATT_SFTRST   0x01
#define VIA_CRDR_PCICLKGATT_3V3      0x10
#define VIA_CRDR_PCICLKGATT_PAD_PWRON 0x20
#define VIA_CRDR_PCISDCCLK           0x5
#define VIA_CRDR_PCIDMACLK           0x7
#define VIA_CRDR_PCIDMACLK_SDC       0x2
#define VIA_CRDR_PCIINTCTRL          0x8
#define VIA_CRDR_PCIINTCTRL_SDCIRQEN 0x04
#define VIA_CRDR_PCIINTSTATUS        0x9
#define VIA_CRDR_PCIINTSTATUS_SDC    0x04
#define VIA_CRDR_PCITMOCTRL          0xa
#define VIA_CRDR_PCITMOCTRL_NO       0x0
#define VIA_CRDR_PCITMOCTRL_32US     0x1
#define VIA_CRDR_PCITMOCTRL_256US    0x2
#define VIA_CRDR_PCITMOCTRL_1024US   0x3
#define VIA_CRDR_PCITMOCTRL_256MS    0x4
#define VIA_CRDR_PCITMOCTRL_512MS    0x5
#define VIA_CRDR_PCITMOCTRL_1024MS   0x6

#define VIA_CRDR_QUIRK_300MS_PWRDELAY 0x0001
#define VIA_CMD_TIMEOUT_MS            1000

/* Clock select enum from driver */
enum PCI_HOST_CLK_CONTROL {
    PCI_CLK_375K = 0x03,
    PCI_CLK_8M = 0x04,
    PCI_CLK_12M = 0x00,
    PCI_CLK_16M = 0x05,
    PCI_CLK_24M = 0x01,
    PCI_CLK_33M = 0x06,
    PCI_CLK_48M = 0x02
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

/* Register structs matching driver's definitions */
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
    struct sdhcreg sdhc_regs;
    struct pcictrlreg pcictrl_regs;
    uint32_t dmabaseadd;
    uint32_t dmacounter;
    uint32_t dmactrl;
    uint32_t dmasts;
    uint32_t dmastart;

    /* DMA Context */
    bool dma_active;

    uint8_t power_state;
    uint8_t pm_state;
};

/* Forward declaration */
static void pcibase_update_irq(PCIBaseState *s);

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint16_t sd_status = s->sdhc_regs.sdstatus_reg & 0xFFFF;
    uint32_t int_mask = s->sdhc_regs.sdintmask_reg;
    uint8_t pciintctrl = s->pcictrl_regs.pciintctrl_reg;
    bool raise = false;

    if ((pciintctrl & VIA_CRDR_PCIINTCTRL_SDCIRQEN) &&
        (int_mask & sd_status & VIA_CRDR_SDSTS_INT_MASK)) {
        s->pcictrl_regs.pciintstatus_reg |= VIA_CRDR_PCIINTSTATUS_SDC;
        raise = true;
    } else {
        s->pcictrl_regs.pciintstatus_reg &= ~VIA_CRDR_PCIINTSTATUS_SDC;
    }
    pci_set_irq(pdev, raise ? 1 : 0);
}

static uint64_t sdhc_mmio_read(PCIBaseState *s, hwaddr offset, unsigned size)
{
    uint64_t val = 0;
    uint32_t reg32;
    uint16_t reg16;
    switch (offset) {
    case VIA_CRDR_SDCTRL:
        if (size == 4) val = s->sdhc_regs.sdcontrol_reg;
        break;
    case VIA_CRDR_SDCARG:
        if (size == 4) val = s->sdhc_regs.sdcmdarg_reg;
        break;
    case VIA_CRDR_SDBUSMODE:
        if (size == 4) val = s->sdhc_regs.sdbusmode_reg;
        break;
    case VIA_CRDR_SDBLKLEN:
        if (size == 4) val = s->sdhc_regs.sdblklen_reg;
        break;
    case 0x10: case 0x14: case 0x18: case 0x1c:
        if (size == 4) {
            int idx = (offset - 0x10) >> 2;
            val = s->sdhc_regs.sdresp_reg[idx];
        }
        break;
    case VIA_CRDR_SDCURBLKCNT:
        if (size == 4) val = s->sdhc_regs.sdcurblkcnt_reg;
        break;
    case VIA_CRDR_SDINTMASK:
        if (size == 4) val = s->sdhc_regs.sdintmask_reg;
        break;
    case VIA_CRDR_SDSTATUS:
        reg16 = s->sdhc_regs.sdstatus_reg & 0xFFFF;
        if (size == 2) val = reg16;
        else if (size == 4) val = s->sdhc_regs.sdstatus_reg;
        else qemu_log_mask(LOG_GUEST_ERROR, "via-sdmmc: unsupported read size at SDSTATUS\n");
        break;
    case VIA_CRDR_SDSTATUS2:
        reg16 = (s->sdhc_regs.sdstatus_reg >> 16) & 0xFFFF;
        if (size == 2) val = reg16;
        else qemu_log_mask(LOG_GUEST_ERROR, "via-sdmmc: unsupported read size at SDSTATUS2\n");
        break;
    case VIA_CRDR_SDRSPTMO:
        if (size == 4) val = s->sdhc_regs.sdrsptmo_reg;
        break;
    case VIA_CRDR_SDCLKSEL:
        if (size == 4) val = s->sdhc_regs.sdclksel_reg;
        break;
    case VIA_CRDR_SDEXTCTRL:
        if (size == 4 || size == 1) val = s->sdhc_regs.sdextctrl_reg;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "via-sdmmc: unknown SDC read offset 0x%"PRIx64"\n", offset);
        break;
    }
    return val;
}

static void sdhc_mmio_write(PCIBaseState *s, hwaddr offset, uint64_t val, unsigned size)
{
    uint32_t old_status;
    switch (offset) {
    case VIA_CRDR_SDCTRL:
        if (size == 4) {
            s->sdhc_regs.sdcontrol_reg = val;
        }
        break;
    case VIA_CRDR_SDCARG:
        if (size == 4) s->sdhc_regs.sdcmdarg_reg = val;
        break;
    case VIA_CRDR_SDBUSMODE:
        if (size == 4) s->sdhc_regs.sdbusmode_reg = val;
        break;
    case VIA_CRDR_SDBLKLEN:
        if (size == 4) s->sdhc_regs.sdblklen_reg = val;
        break;
    case VIA_CRDR_SDCURBLKCNT:
        if (size == 4) s->sdhc_regs.sdcurblkcnt_reg = val;
        break;
    case VIA_CRDR_SDINTMASK:
        if (size == 4) {
            s->sdhc_regs.sdintmask_reg = val;
            pcibase_update_irq(s);
        }
        break;
    case VIA_CRDR_SDSTATUS:
        old_status = s->sdhc_regs.sdstatus_reg;
        if (size == 2) {
            uint16_t val16 = val;
            s->sdhc_regs.sdstatus_reg &= ~((uint32_t)(val16 & VIA_CRDR_SDSTS_W1C_MASK));
        } else if (size == 4) {
            s->sdhc_regs.sdstatus_reg &= ~(val & VIA_CRDR_SDSTS_W1C_MASK);
        }
        if (s->sdhc_regs.sdstatus_reg != old_status) {
            pcibase_update_irq(s);
        }
        break;
    case VIA_CRDR_SDSTATUS2:
        if (size == 2) {
            uint32_t val_upper = (uint16_t)val;
            s->sdhc_regs.sdstatus_reg = (s->sdhc_regs.sdstatus_reg & 0xFFFF) | (val_upper << 16);
            pcibase_update_irq(s);
        }
        break;
    case VIA_CRDR_SDRSPTMO:
        if (size == 4) s->sdhc_regs.sdrsptmo_reg = val;
        break;
    case VIA_CRDR_SDCLKSEL:
        if (size == 4) s->sdhc_regs.sdclksel_reg = val;
        break;
    case VIA_CRDR_SDEXTCTRL:
        if (size == 4 || size == 1) s->sdhc_regs.sdextctrl_reg = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "via-sdmmc: unknown SDC write offset 0x%"PRIx64"\n", offset);
        break;
    }
}

static uint64_t ddma_mmio_read(PCIBaseState *s, hwaddr offset, unsigned size)
{
    uint64_t val = 0;
    switch (offset) {
    case VIA_CRDR_DMABASEADD:
        val = s->dmabaseadd;
        break;
    case VIA_CRDR_DMACOUNTER:
        val = s->dmacounter;
        break;
    case VIA_CRDR_DMACTRL:
        val = s->dmactrl;
        break;
    case VIA_CRDR_DMASTS:
        val = s->dmasts;
        break;
    case VIA_CRDR_DMASTART:
        val = s->dmastart;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "via-sdmmc: unknown DDMA read offset 0x%"PRIx64"\n", offset);
        break;
    }
    return val;
}

static void ddma_mmio_write(PCIBaseState *s, hwaddr offset, uint64_t val, unsigned size)
{
    switch (offset) {
    case VIA_CRDR_DMABASEADD:
        s->dmabaseadd = val;
        break;
    case VIA_CRDR_DMACOUNTER:
        s->dmacounter = val;
        break;
    case VIA_CRDR_DMACTRL:
        s->dmactrl = val;
        break;
    case VIA_CRDR_DMASTS:
        s->dmasts = val;
        break;
    case VIA_CRDR_DMASTART:
        if (size == 4 && (val & 1)) {
            if (!s->dma_active) {
                s->dma_active = true;
                s->dma_active = false;
                if (s->dmactrl & VIA_CRDR_DMACTRL_ENIRQ) {
                    s->sdhc_regs.sdstatus_reg |= VIA_CRDR_SDSTS_DT;
                    pcibase_update_irq(s);
                }
            }
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "via-sdmmc: unknown DDMA write offset 0x%"PRIx64"\n", offset);
        break;
    }
}

static uint64_t pcictrl_mmio_read(PCIBaseState *s, hwaddr offset, unsigned size)
{
    uint64_t val = 0;
    if (size != 1) {
        qemu_log_mask(LOG_GUEST_ERROR, "via-sdmmc: pcictrl read with size %d\n", size);
        return val;
    }
    switch (offset) {
    case 0x2: val = s->pcictrl_regs.pciclkgat_reg; break;
    case 0x3: val = s->pcictrl_regs.pcinfcclk_reg; break;
    case 0x4: val = s->pcictrl_regs.pcimscclk_reg; break;
    case 0x5: val = s->pcictrl_regs.pcisdclk_reg; break;
    case 0x6: val = s->pcictrl_regs.pcicaclk_reg; break;
    case 0x7: val = s->pcictrl_regs.pcidmaclk_reg; break;
    case 0x8: val = s->pcictrl_regs.pciintctrl_reg; break;
    case 0x9: val = s->pcictrl_regs.pciintstatus_reg; break;
    case 0xa: val = s->pcictrl_regs.pcitmoctrl_reg; break;
    default: qemu_log_mask(LOG_GUEST_ERROR, "via-sdmmc: unknown pcictrl read offset 0x%"PRIx64"\n", offset); break;
    }
    return val;
}

static void pcictrl_mmio_write(PCIBaseState *s, hwaddr offset, uint64_t val, unsigned size)
{
    if (size != 1) {
        qemu_log_mask(LOG_GUEST_ERROR, "via-sdmmc: pcictrl write with size %d\n", size);
        return;
    }
    switch (offset) {
    case 0x2: s->pcictrl_regs.pciclkgat_reg = val; break;
    case 0x3: s->pcictrl_regs.pcinfcclk_reg = val; break;
    case 0x4: s->pcictrl_regs.pcimscclk_reg = val; break;
    case 0x5: s->pcictrl_regs.pcisdclk_reg = val; break;
    case 0x6: s->pcictrl_regs.pcicaclk_reg = val; break;
    case 0x7: s->pcictrl_regs.pcidmaclk_reg = val; break;
    case 0x8: s->pcictrl_regs.pciintctrl_reg = val; pcibase_update_irq(s); break;
    case 0x9: s->pcictrl_regs.pciintstatus_reg = val; break;
    case 0xa: s->pcictrl_regs.pcitmoctrl_reg = val; break;
    default: qemu_log_mask(LOG_GUEST_ERROR, "via-sdmmc: unknown pcictrl write offset 0x%"PRIx64"\n", offset); break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= 0x200 && addr < 0x400) {
        return sdhc_mmio_read(s, addr - 0x200, size);
    } else if (addr >= 0x400 && addr < 0x600) {
        return ddma_mmio_read(s, addr - 0x400, size);
    } else if (addr >= 0x600 && addr < 0x800) {
        return pcictrl_mmio_read(s, addr - 0x600, size);
    }
    qemu_log_mask(LOG_GUEST_ERROR, "via-sdmmc: MMIO read out of range: 0x%"PRIx64"\n", addr);
    return ~0ULL;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= 0x200 && addr < 0x400) {
        sdhc_mmio_write(s, addr - 0x200, val, size);
    } else if (addr >= 0x400 && addr < 0x600) {
        ddma_mmio_write(s, addr - 0x400, val, size);
    } else if (addr >= 0x600 && addr < 0x800) {
        pcictrl_mmio_write(s, addr - 0x600, val, size);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "via-sdmmc: MMIO write out of range: 0x%"PRIx64"\n", addr);
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
    memset(&s->sdhc_regs, 0, sizeof(s->sdhc_regs));
    memset(&s->pcictrl_regs, 0, sizeof(s->pcictrl_regs));
    s->dmabaseadd = 0;
    s->dmacounter = 0;
    s->dmactrl = 0;
    s->dmasts = 0;
    s->dmastart = 0;
    s->dma_active = false;
    s->intr_status = 0;
    s->intr_mask = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_VIA );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_VIA_9530 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SYSTEM_SDHCI );
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
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "via-sdmmc-bar0";

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
