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


#define TYPE_PCIBASE_DEVICE "via_sdmmc_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_VIA		0x1106
#define PCI_DEVICE_ID_VIA_9530	0x9530
#define VIA_CRDR_SDC_OFF	0x200
#define VIA_CRDR_DDMA_OFF	0x400
#define VIA_CRDR_PCICTRL_OFF	0x600
#define VIA_CRDR_MIN_CLOCK	375000
#define VIA_CRDR_MAX_CLOCK	48000000
#define VIA_CRDR_PCI_WORK_MODE	0x40
#define VIA_CRDR_PCI_DBG_MODE	0x41
#define VIA_CRDR_SDCTRL			0x0
#define VIA_CRDR_SDCTRL_START		0x01
#define VIA_CRDR_SDCTRL_WRITE		0x04
#define VIA_CRDR_SDCTRL_SINGLE_WR	0x10
#define VIA_CRDR_SDCTRL_SINGLE_RD	0x20
#define VIA_CRDR_SDCTRL_MULTI_WR	0x30
#define VIA_CRDR_SDCTRL_MULTI_RD	0x40
#define VIA_CRDR_SDCTRL_STOP		0x70
#define VIA_CRDR_SDCTRL_RSP_NONE	0x0
#define VIA_CRDR_SDCTRL_RSP_R1		0x10000
#define VIA_CRDR_SDCTRL_RSP_R2		0x20000
#define VIA_CRDR_SDCTRL_RSP_R3		0x30000
#define VIA_CRDR_SDCTRL_RSP_R1B		0x90000
#define VIA_CRDR_SDCARG 	0x4
#define VIA_CRDR_SDBUSMODE	0x8
#define VIA_CRDR_SDMODE_4BIT	0x02
#define VIA_CRDR_SDMODE_CLK_ON	0x40
#define VIA_CRDR_SDBLKLEN	0xc
#define VIA_CRDR_SDBLKLEN_GPIDET	0x2000
#define VIA_CRDR_SDBLKLEN_INTEN		0x8000
#define VIA_CRDR_MAX_BLOCK_COUNT	65536
#define VIA_CRDR_MAX_BLOCK_LENGTH	2048
#define VIA_CRDR_SDRESP0	0x10
#define VIA_CRDR_SDRESP1	0x14
#define VIA_CRDR_SDRESP2	0x18
#define VIA_CRDR_SDRESP3	0x1c
#define VIA_CRDR_SDCURBLKCNT	0x20
#define VIA_CRDR_SDINTMASK	0x24
#define VIA_CRDR_SDINTMASK_MBDIE	0x10
#define VIA_CRDR_SDINTMASK_BDDIE	0x20
#define VIA_CRDR_SDINTMASK_CIRIE	0x80
#define VIA_CRDR_SDINTMASK_CRDIE	0x200
#define VIA_CRDR_SDINTMASK_CRTOIE	0x400
#define VIA_CRDR_SDINTMASK_ASCRDIE	0x800
#define VIA_CRDR_SDINTMASK_DTIE		0x1000
#define VIA_CRDR_SDINTMASK_SCIE		0x2000
#define VIA_CRDR_SDINTMASK_RCIE		0x4000
#define VIA_CRDR_SDINTMASK_WCIE		0x8000
#define VIA_CRDR_SDACTIVE_INTMASK \
	(VIA_CRDR_SDINTMASK_MBDIE | VIA_CRDR_SDINTMASK_CIRIE \
	| VIA_CRDR_SDINTMASK_CRDIE | VIA_CRDR_SDINTMASK_CRTOIE \
	| VIA_CRDR_SDINTMASK_DTIE | VIA_CRDR_SDINTMASK_SCIE \
	| VIA_CRDR_SDINTMASK_RCIE | VIA_CRDR_SDINTMASK_WCIE)
#define VIA_CRDR_SDSTATUS	0x28
#define VIA_CRDR_SDSTS_CECC		0x01
#define VIA_CRDR_SDSTS_WP		0x02
#define VIA_CRDR_SDSTS_SLOTD		0x04
#define VIA_CRDR_SDSTS_SLOTG		0x08
#define VIA_CRDR_SDSTS_MBD		0x10
#define VIA_CRDR_SDSTS_BDD		0x20
#define VIA_CRDR_SDSTS_CD		0x40
#define VIA_CRDR_SDSTS_CIR		0x80
#define VIA_CRDR_SDSTS_IO		0x100
#define VIA_CRDR_SDSTS_CRD		0x200
#define VIA_CRDR_SDSTS_CRTO		0x400
#define VIA_CRDR_SDSTS_ASCRDIE		0x800
#define VIA_CRDR_SDSTS_DT		0x1000
#define VIA_CRDR_SDSTS_SC		0x2000
#define VIA_CRDR_SDSTS_RC		0x4000
#define VIA_CRDR_SDSTS_WC		0x8000
#define VIA_CRDR_SDSTS_IGN_MASK\
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
#define VIA_CRDR_SDSTS_DATA_MASK\
	(VIA_CRDR_SDSTS_MBD | VIA_CRDR_SDSTS_DT \
	| VIA_CRDR_SDSTS_RC | VIA_CRDR_SDSTS_WC)
#define VIA_CRDR_SDSTATUS2	0x2a
#define VIA_CRDR_SDSTS_CFE		0x80
#define VIA_CRDR_SDRSPTMO	0x2C
#define VIA_CRDR_SDCLKSEL	0x30
#define VIA_CRDR_SDEXTCTRL	0x34
#define VIS_CRDR_SDEXTCTRL_AUTOSTOP_SD	0x01
#define VIS_CRDR_SDEXTCTRL_SHIFT_9	0x02
#define VIS_CRDR_SDEXTCTRL_MMC_8BIT	0x04
#define VIS_CRDR_SDEXTCTRL_RELD_BLK	0x08
#define VIS_CRDR_SDEXTCTRL_BAD_CMDA	0x10
#define VIS_CRDR_SDEXTCTRL_BAD_DATA	0x20
#define VIS_CRDR_SDEXTCTRL_AUTOSTOP_SPI	0x40
#define VIA_CRDR_SDEXTCTRL_HISPD	0x80
#define VIA_CRDR_DMABASEADD	0x0
#define VIA_CRDR_DMACOUNTER	0x4
#define VIA_CRDR_DMACTRL	0x8
#define VIA_CRDR_DMACTRL_DIR		0x100
#define VIA_CRDR_DMACTRL_ENIRQ		0x10000
#define VIA_CRDR_DMACTRL_SFTRST		0x1000000
#define VIA_CRDR_DMASTS		0xc
#define VIA_CRDR_DMASTART	0x10
#define VIA_CRDR_PCICLKGATT	0x2
#define VIA_CRDR_PCICLKGATT_SFTRST	0x01
#define VIA_CRDR_PCICLKGATT_3V3	0x10
#define VIA_CRDR_PCICLKGATT_PAD_PWRON	0x20
#define VIA_CRDR_PCISDCCLK	0x5
#define VIA_CRDR_PCIDMACLK	0x7
#define VIA_CRDR_PCIDMACLK_SDC		0x2
#define VIA_CRDR_PCIINTCTRL	0x8
#define VIA_CRDR_PCIINTCTRL_SDCIRQEN	0x04
#define VIA_CRDR_PCIINTSTATUS	0x9
#define VIA_CRDR_PCIINTSTATUS_SDC	0x04
#define VIA_CRDR_PCITMOCTRL	0xa
#define VIA_CRDR_PCITMOCTRL_NO		0x0
#define VIA_CRDR_PCITMOCTRL_32US	0x1
#define VIA_CRDR_PCITMOCTRL_256US	0x2
#define VIA_CRDR_PCITMOCTRL_1024US	0x3
#define VIA_CRDR_PCITMOCTRL_256MS	0x4
#define VIA_CRDR_PCITMOCTRL_512MS	0x5
#define VIA_CRDR_PCITMOCTRL_1024MS	0x6
#define VIA_CRDR_QUIRK_300MS_PWRDELAY	0x0001
#define VIA_CMD_TIMEOUT_MS		1000

enum PCI_HOST_CLK_CONTROL {
	PCI_CLK_375K = 0x03,
	PCI_CLK_8M = 0x04,
	PCI_CLK_12M = 0x00,
	PCI_CLK_16M = 0x05,
	PCI_CLK_24M = 0x01,
	PCI_CLK_33M = 0x06,
	PCI_CLK_48M = 0x02
};

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
    

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct sdhcreg sdhc_reg;
    struct pcictrlreg pcictrl_reg;

    /* DMA Context */
    uint32_t dmabaseadd;
    uint32_t dmacounter;
    uint32_t dmactrl;
    uint32_t dmasts;

    
};



/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    uint32_t sd_int = s->sdhc_reg.sdstatus_reg & s->sdhc_reg.sdintmask_reg & VIA_CRDR_SDSTS_INT_MASK;
    
    if (sd_int) {
        s->pcictrl_reg.pciintstatus_reg |= VIA_CRDR_PCIINTSTATUS_SDC;
    } else {
        s->pcictrl_reg.pciintstatus_reg &= ~VIA_CRDR_PCIINTSTATUS_SDC;
    }
    
    if ((s->pcictrl_reg.pciintstatus_reg & VIA_CRDR_PCIINTSTATUS_SDC) && 
        (s->pcictrl_reg.pciintctrl_reg & VIA_CRDR_PCIINTCTRL_SDCIRQEN)) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->dmacounter == 0) return;
    
    uint32_t len = s->dmacounter;
    uint32_t offset = 0;
    uint8_t dummy_buf[512];
    memset(dummy_buf, 0, sizeof(dummy_buf));
    
    while (len > 0) {
        uint32_t chunk = (len > sizeof(dummy_buf)) ? sizeof(dummy_buf) : len;
        if (is_write) {
            pci_dma_read(pdev, s->dmabaseadd + offset, dummy_buf, chunk);
        } else {
            pci_dma_write(pdev, s->dmabaseadd + offset, dummy_buf, chunk);
        }
        len -= chunk;
        offset += chunk;
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= VIA_CRDR_SDC_OFF && addr < VIA_CRDR_DDMA_OFF) {
        uint32_t offset = addr - VIA_CRDR_SDC_OFF;
        uint32_t aligned_offset = offset & ~3U;
        uint32_t *reg_ptr = (uint32_t *)&s->sdhc_reg;
        if (aligned_offset < sizeof(struct sdhcreg)) {
            val = reg_ptr[aligned_offset / 4];
            val >>= (offset % 4) * 8;
            val &= ((1ULL << (size * 8)) - 1);
        }
    } else if (addr >= VIA_CRDR_DDMA_OFF && addr < VIA_CRDR_PCICTRL_OFF) {
        uint32_t offset = addr - VIA_CRDR_DDMA_OFF;
        uint32_t aligned_offset = offset & ~3U;
        uint32_t reg_val = 0;
        
        if (aligned_offset == VIA_CRDR_DMABASEADD) reg_val = s->dmabaseadd;
        else if (aligned_offset == VIA_CRDR_DMACOUNTER) reg_val = s->dmacounter;
        else if (aligned_offset == VIA_CRDR_DMACTRL) reg_val = s->dmactrl;
        else if (aligned_offset == VIA_CRDR_DMASTS) reg_val = s->dmasts;
        
        val = reg_val >> ((offset % 4) * 8);
        val &= ((1ULL << (size * 8)) - 1);
    } else if (addr >= VIA_CRDR_PCICTRL_OFF && addr < VIA_CRDR_PCICTRL_OFF + sizeof(struct pcictrlreg)) {
        uint32_t offset = addr - VIA_CRDR_PCICTRL_OFF;
        uint8_t *reg_ptr = (uint8_t *)&s->pcictrl_reg;
        for (unsigned i = 0; i < size; i++) {
            if (offset + i < sizeof(struct pcictrlreg)) {
                val |= ((uint64_t)reg_ptr[offset + i]) << (i * 8);
            }
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= VIA_CRDR_SDC_OFF && addr < VIA_CRDR_DDMA_OFF) {
        uint32_t offset = addr - VIA_CRDR_SDC_OFF;
        uint32_t aligned_offset = offset & ~3U;
        uint32_t *reg_ptr = (uint32_t *)&s->sdhc_reg;
        
        if (aligned_offset < sizeof(struct sdhcreg)) {
            uint32_t mask = ((1ULL << (size * 8)) - 1) << ((offset % 4) * 8);
            uint32_t val32 = (val << ((offset % 4) * 8)) & mask;
            
            if (aligned_offset == VIA_CRDR_SDSTATUS || aligned_offset == VIA_CRDR_SDSTATUS2) {
                uint32_t w1c_mask = VIA_CRDR_SDSTS_W1C_MASK;
                uint32_t clear_bits = val32 & w1c_mask;
                uint32_t new_status = s->sdhc_reg.sdstatus_reg & ~clear_bits;
                new_status = (new_status & w1c_mask) | (val32 & ~w1c_mask);
                new_status |= VIA_CRDR_SDSTS_SLOTG;
                s->sdhc_reg.sdstatus_reg = new_status;
                pcibase_update_irq(s);
            } else {
                reg_ptr[aligned_offset / 4] = (reg_ptr[aligned_offset / 4] & ~mask) | val32;
                
                if (aligned_offset == VIA_CRDR_SDCTRL) {
                    if (s->sdhc_reg.sdcontrol_reg & VIA_CRDR_SDCTRL_START) {
                        s->sdhc_reg.sdcontrol_reg &= ~VIA_CRDR_SDCTRL_START;
                        s->sdhc_reg.sdstatus_reg |= VIA_CRDR_SDSTS_CRD;
                        
                        if (s->sdhc_reg.sdcontrol_reg & (VIA_CRDR_SDCTRL_SINGLE_WR | VIA_CRDR_SDCTRL_SINGLE_RD | 
                                                         VIA_CRDR_SDCTRL_MULTI_WR | VIA_CRDR_SDCTRL_MULTI_RD)) {
                            s->sdhc_reg.sdstatus_reg |= VIA_CRDR_SDSTS_MBD;
                        }
                        pcibase_update_irq(s);
                    }
                } else if (aligned_offset == VIA_CRDR_SDINTMASK) {
                    pcibase_update_irq(s);
                }
            }
        }
    } else if (addr >= VIA_CRDR_DDMA_OFF && addr < VIA_CRDR_PCICTRL_OFF) {
        uint32_t offset = addr - VIA_CRDR_DDMA_OFF;
        uint32_t aligned_offset = offset & ~3U;
        uint32_t mask = ((1ULL << (size * 8)) - 1) << ((offset % 4) * 8);
        uint32_t val32 = (val << ((offset % 4) * 8)) & mask;
        
        if (aligned_offset == VIA_CRDR_DMABASEADD) {
            s->dmabaseadd = (s->dmabaseadd & ~mask) | val32;
        } else if (aligned_offset == VIA_CRDR_DMACOUNTER) {
            s->dmacounter = (s->dmacounter & ~mask) | val32;
        } else if (aligned_offset == VIA_CRDR_DMACTRL) {
            s->dmactrl = (s->dmactrl & ~mask) | val32;
            if (s->dmactrl & VIA_CRDR_DMACTRL_SFTRST) {
                s->dmactrl &= ~VIA_CRDR_DMACTRL_SFTRST;
            }
        } else if (aligned_offset == VIA_CRDR_DMASTART) {
            uint32_t start_val = (val32 >> ((offset % 4) * 8)) & ((1ULL << (size * 8)) - 1);
            if (start_val == 1) {
                pcibase_do_dma(s, (s->dmactrl & VIA_CRDR_DMACTRL_DIR) != 0);
            }
        }
    } else if (addr >= VIA_CRDR_PCICTRL_OFF && addr < VIA_CRDR_PCICTRL_OFF + sizeof(struct pcictrlreg)) {
        uint32_t offset = addr - VIA_CRDR_PCICTRL_OFF;
        uint8_t *reg_ptr = (uint8_t *)&s->pcictrl_reg;
        for (unsigned i = 0; i < size; i++) {
            if (offset + i < sizeof(struct pcictrlreg)) {
                reg_ptr[offset + i] = (val >> (i * 8)) & 0xFF;
            }
        }
        
        if (s->pcictrl_reg.pciclkgat_reg & VIA_CRDR_PCICLKGATT_SFTRST) {
            s->pcictrl_reg.pciclkgat_reg &= ~VIA_CRDR_PCICLKGATT_SFTRST;
            memset(&s->sdhc_reg, 0, sizeof(s->sdhc_reg));
            s->sdhc_reg.sdstatus_reg = VIA_CRDR_SDSTS_SLOTG;
        }
        
        if (offset <= VIA_CRDR_PCIINTCTRL && offset + size > VIA_CRDR_PCIINTCTRL) {
            pcibase_update_irq(s);
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
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

    memset(&s->sdhc_reg, 0, sizeof(s->sdhc_reg));
    memset(&s->pcictrl_reg, 0, sizeof(s->pcictrl_reg));
    
    s->dmabaseadd = 0;
    s->dmacounter = 0;
    s->dmactrl = 0;
    s->dmasts = 0;
    
    s->sdhc_reg.sdstatus_reg = VIA_CRDR_SDSTS_SLOTG;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_VIA );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_VIA_9530 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0805 );
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
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "via-sdmmc-mmio";  
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
