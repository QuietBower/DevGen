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
#include "hw/irq.h"

#define TYPE_PCIBASE_DEVICE "ISDN_Infineon_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define DIVA_HSCX_PORT        0x00
#define DIVA_HSCX_ALE         0x04
#define DIVA_ISAC_PORT        0x08
#define DIVA_ISAC_ALE         0x0C
#define DIVA_PCI_CTRL         0x10
#define DIVA_IRQ_BIT          0x01
#define DIVA_RESET_BIT        0x08
#define DIVA_EEPROM_CLK       0x40
#define DIVA_LED_A            0x10
#define DIVA_LED_B            0x20
#define DIVA_IRQ_CLR          0x80

#define PITA_ICR_REG          0x00
#define PITA_INT0_STATUS      0x02
#define PITA_MISC_REG         0x1c
#define PITA_PARA_SOFTRESET   0x01000000
#define PITA_SER_SOFTRESET    0x02000000
#define PITA_PARA_MPX_MODE    0x04000000
#define PITA_INT0_ENABLE      0x00020000

#define TIGER_RESET_ADDR      0x00
#define TIGER_EXTERN_RESET    0x01
#define TIGER_AUX_CTRL        0x02
#define TIGER_AUX_DATA        0x03
#define TIGER_AUX_IRQMASK     0x05
#define TIGER_AUX_STATUS      0x07
#define TIGER_IOMASK          0xdd
#define TIGER_IRQ_BIT         0x02
#define TIGER_IPAC_ALE        0xC0
#define TIGER_IPAC_PORT       0xC8

#define ELSA_IRQ_ADDR         0x4c
#define ELSA_IRQ_MASK         0x04

#define QS1000_IRQ_OFF        0x01
#define QS3000_IRQ_OFF        0x03
#define QS1000_IRQ_ON         0x41
#define QS3000_IRQ_ON         0x43

#define NICCY_ISAC_PORT       0x00
#define NICCY_HSCX_PORT       0x01
#define NICCY_ISAC_ALE        0x02
#define NICCY_HSCX_ALE        0x03
#define NICCY_IRQ_CTRL_REG    0x38
#define NICCY_IRQ_ENABLE      0x001f00
#define NICCY_IRQ_DISABLE     0xff0000
#define NICCY_IRQ_BIT         0x800000

#define SCT_PLX_IRQ_ADDR      0x4c
#define SCT_PLX_RESET_ADDR    0x50
#define SCT_PLX_IRQ_ENABLE    0x41
#define SCT_PLX_RESET_BIT     0x04

#define GAZEL_IPAC_DATA_PORT  0x04
#define GAZEL_CNTRL           0x50
#define GAZEL_RESET           0x04
#define GAZEL_RESET_9050      0x40000000
#define GAZEL_INCSR           0x4C
#define GAZEL_ISAC_EN         0x08
#define GAZEL_INT_ISAC        0x20
#define GAZEL_HSCX_EN         0x01
#define GAZEL_INT_HSCX        0x04
#define GAZEL_PCI_EN          0x40
#define GAZEL_IPAC_EN         0x03

#define IPAC_ISTA             0xC1
#define IPAC_POTA2            0xC9
#define IPAC_MASK             0xC1
#define IPAC_CONF             0xC0
#define IPAC_ATX              0xC5
#define IPAC_AOE              0xC4
#define IPAC_ACFG             0xC3
#define IPAC_PCFG             0xCA
#define IPAC_TYPE_ISAC        0x0010
#define IPAC_TYPE_IPAC        0x0020
#define IPAC_TYPE_HSCX        0x0100
#define IPAC_TYPE_IPACX       0x0080
#define ISACX__ICD            0x01
#define IPAC_ISTAB            0x20
#define IPACX__ICB            0x40
#define IPACX__ICA            0x80
#define IPAC__EXA             0x04
#define IPAC__ICB             0x02
#define IPAC__EXD             0x10
#define ISAC_ISTA             0x20
#define IPAC__ICA             0x08
#define ISACX__CIC            0x10
#define ISACX_ISTA            0x60
#define IPAC_D_TIN2           0x01
#define IPAC__ICD             0x20
#define IPAC__EXB             0x01

#define IPACX_OFF_ICA         0x70
#define IPACX_OFF_ICB         0x80
#define IPAC_TYPE_ISACX       0x0040

#define ISACX_D_RFO           0x20
#define ISACX_D_XPR           0x10
#define ISACX_D_XMR           0x08
#define ISAC_EXIR             0x24
#define ISACX_CMDRD_RMC       0x80
#define ISACX_D_XDU           0x04
#define ISACX_ISTAD           0x20
#define ISACX_CMDRD           0x21
#define ISACX_D_RME           0x80
#define ISACX_D_RPF           0x40

#define IPAC_ID               0xC2
#define IPAC__ON              0xC0
#define ISACX_CIR0_CIC0       0x08
#define ISACX_CIR0            0x2E
#define ISAC_CMDR             0x21
#define ISAC_RSTA             0x27
#define ISAC_RBCL             0x25
#define ISAC_MOR1             0x34
#define MONITOR_TX_1          0x2001
#define ISAC_MOCR             0x3a
#define MONITOR_RX_1          0x1001
#define ISAC_MOX1             0x34
#define ISAC_MOR0             0x32
#define MONITOR_RX_0          0x1000
#define MONITOR_TX_0          0x2000
#define ISAC_MOX0             0x32
#define ISAC_MOSR             0x3a
#define ISAC_CIR1             0x33
#define ISAC_CIR0             0x31
#define ISACX_RSTAD_RAB       0x10
#define ISACX_RSTAD           0x28
#define ISACX_RSTAD_CRC       0x20
#define ISACX_RSTAD_VFR       0x80
#define ISACX_RBCLD           0x26
#define ISACX_RSTAD_RDO       0x40
#define ISACX_MASK            0x60
#define ISAC_MASK             0x20
#define ISAC_CMD_RS           0x01
#define ISAC_SQXR             0x3b
#define ISAC_ADF1             0x38
#define ISACX_STARD           0x21
#define ISAC_SPCR             0x30
#define ISACX_MASKD           0x20
#define ISACX_TR_CONF0        0x30
#define ISAC_ADF2             0x39
#define ISAC_STAR             0x21
#define ISACX_MODED           0x22
#define IPACX__ON             0x2C
#define ISACX_ID              0x64
#define ISAC_TIMR             0x23
#define ISAC_MODE             0x22
#define ISACX_TR_CONF2        0x32
#define ISAC_STCR             0x37
#define ISAC_RBCH             0x2A
#define IPACX_EXMB            0x03
#define ISACX_BCHB_TSDP_BC1   0x4A
#define IPACX_MASKB           0x00
#define IPAC_RCCR             0x33
#define ISACX_BCHA_CR         0x51
#define IPAC_MODEB            0x22
#define IPACX_MODEB           0x02
#define IPAC_TSAR             0x31
#define IPAC_XCCR             0x32
#define IPAC_TSAX             0x30
#define IPAC_CCR1             0x2F
#define ISACX_BCHB_CR         0x52
#define IPAC_CCR2             0x2C
#define IPAC_MASKB            0x20
#define ISACX_BCHA_TSDP_BC1   0x48
#define IPACX_B_ON            0x0B
#define IPACX_XFIFOB          0x0A
#define IPAC_XBCH             0x2D
#define IPAC_RAH2             0x27
#define HSCX_VSTR             0x2E
#define IPAC_RLCR             0x2E
#define ISAC_IND_EI           0x06
#define ISAC_CMD_DUI          0x0F
#define ISAC_IND_RS           0x01
#define ISACX_CIX0            0x2E
#define ISAC_CIX0             0x31
#define IPAC_CMDRB            0x21
#define IPACX_CMDRB           0x01
#define IPAC_STARB            0x21

#define PCIBASE_VENDOR_ID  0x1133
#define PCIBASE_DEVICE_ID  0xe002
#define PCIBASE_CLASS_ID   0x0703

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

    uint8_t cfg_space[0x100];
    uint8_t addr_space[0x100];

    uint8_t diva_pci_ctrl;
    uint32_t pita_misc;
    uint32_t pita_icr;
    uint32_t niccy_irq_ctrl;
    uint16_t sct_plx_irq;
    uint16_t sct_plx_reset;
    uint32_t gazel_cntrl;
    uint8_t gazel_incsr;

    bool irq_enabled;
    bool pending_irq;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->irq_enabled && s->pending_irq) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size == 1) {
        if (addr < sizeof(s->cfg_space)) {
            switch (addr) {
            case DIVA_PCI_CTRL:
                val = s->diva_pci_ctrl;
                break;
            default:
                val = s->cfg_space[addr];
                break;
            }
        }
    } else if (size == 2) {
        uint16_t tmp = 0;
        if (addr + 1 < sizeof(s->cfg_space)) {
            tmp = s->cfg_space[addr] | (s->cfg_space[addr + 1] << 8);
        }
        val = tmp;
    } else if (size == 4) {
        uint32_t tmp = 0;
        if (addr + 3 < sizeof(s->cfg_space)) {
            tmp = s->cfg_space[addr] |
                  (s->cfg_space[addr + 1] << 8) |
                  (s->cfg_space[addr + 2] << 16) |
                  (s->cfg_space[addr + 3] << 24);
        }
        val = tmp;
        if (addr == PITA_MISC_REG) {
            val = s->pita_misc;
        } else if (addr == PITA_ICR_REG) {
            val = s->pita_icr;
        } else if (addr == GAZEL_CNTRL) {
            val = s->gazel_cntrl;
        } else if (addr == NICCY_IRQ_CTRL_REG) {
            val = s->niccy_irq_ctrl;
            /* expose pending IRQ via NICCY specific status bit */
            if (s->pending_irq && s->irq_enabled) {
                val |= NICCY_IRQ_BIT;
            }
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 1) {
        uint8_t v = val & 0xff;
        if (addr < sizeof(s->cfg_space)) {
            switch (addr) {
            case DIVA_PCI_CTRL:
                s->diva_pci_ctrl = v;
                /* emulate reset and IRQ clear behaviour */
                if (v & DIVA_RESET_BIT) {
                    s->pending_irq = true;
                }
                if (v & DIVA_IRQ_CLR) {
                    s->pending_irq = false;
                }
                pcibase_update_irq(s);
                break;
            case TIGER_AUX_IRQMASK:
                if (v & TIGER_IRQ_BIT) {
                    s->irq_enabled = true;
                } else {
                    s->irq_enabled = false;
                }
                pcibase_update_irq(s);
                s->cfg_space[addr] = v;
                break;
            case ELSA_IRQ_ADDR:
                if (v == QS1000_IRQ_ON || v == QS3000_IRQ_ON) {
                    s->irq_enabled = true;
                } else if (v == QS1000_IRQ_OFF || v == QS3000_IRQ_OFF) {
                    s->irq_enabled = false;
                }
                pcibase_update_irq(s);
                s->cfg_space[addr] = v;
                break;
            default:
                s->cfg_space[addr] = v;
                break;
            }
        }
    } else if (size == 2) {
        uint16_t v = val & 0xffff;
        if (addr == SCT_PLX_IRQ_ADDR) {
            s->sct_plx_irq = v;
            if (v & SCT_PLX_IRQ_ENABLE) {
                s->irq_enabled = true;
            } else {
                s->irq_enabled = false;
            }
            pcibase_update_irq(s);
        } else if (addr == SCT_PLX_RESET_ADDR) {
            s->sct_plx_reset = v;
        }
        if (addr + 1 < sizeof(s->cfg_space)) {
            s->cfg_space[addr] = v & 0xff;
            s->cfg_space[addr + 1] = (v >> 8) & 0xff;
        }
    } else if (size == 4) {
        uint32_t v = val & 0xffffffff;
        if (addr == PITA_MISC_REG) {
            s->pita_misc = v;
        } else if (addr == PITA_ICR_REG) {
            s->pita_icr = v;
        } else if (addr == GAZEL_CNTRL) {
            s->gazel_cntrl = v;
        } else if (addr == NICCY_IRQ_CTRL_REG) {
            s->niccy_irq_ctrl = v;
            if (v & NICCY_IRQ_ENABLE) {
                s->irq_enabled = true;
            }
            if (v & NICCY_IRQ_DISABLE) {
                s->irq_enabled = false;
            }
            pcibase_update_irq(s);
        }
        if (addr + 3 < sizeof(s->cfg_space)) {
            s->cfg_space[addr] = v & 0xff;
            s->cfg_space[addr + 1] = (v >> 8) & 0xff;
            s->cfg_space[addr + 2] = (v >> 16) & 0xff;
            s->cfg_space[addr + 3] = (v >> 24) & 0xff;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size == 1) {
        if (addr < sizeof(s->addr_space)) {
            switch (addr) {
            case DIVA_PCI_CTRL:
                val = s->diva_pci_ctrl;
                if (s->pending_irq) {
                    val |= DIVA_IRQ_BIT;
                }
                break;
            case TIGER_AUX_STATUS:
                /* emulate Tiger AUX status: IRQ bit low when pending */
                if (s->pending_irq && s->irq_enabled) {
                    val = 0;
                } else {
                    val = TIGER_IRQ_BIT;
                }
                break;
            case ELSA_IRQ_ADDR:
                if (s->pending_irq && s->irq_enabled) {
                    val = ELSA_IRQ_MASK;
                } else {
                    val = 0;
                }
                break;
            default:
                val = s->addr_space[addr];
                break;
            }
        }
    } else if (size == 2) {
        uint16_t tmp = 0;
        if (addr == SCT_PLX_IRQ_ADDR) {
            tmp = s->sct_plx_irq;
        } else if (addr == SCT_PLX_RESET_ADDR) {
            tmp = s->sct_plx_reset;
        } else if (addr + 1 < sizeof(s->addr_space)) {
            tmp = s->addr_space[addr] | (s->addr_space[addr + 1] << 8);
        }
        val = tmp;
    } else if (size == 4) {
        uint32_t tmp = 0;
        if (addr == NICCY_IRQ_CTRL_REG) {
            tmp = s->niccy_irq_ctrl;
            if (s->pending_irq && s->irq_enabled) {
                tmp |= NICCY_IRQ_BIT;
            }
        } else if (addr == GAZEL_CNTRL) {
            tmp = s->gazel_cntrl;
        } else if (addr + 3 < sizeof(s->addr_space)) {
            tmp = s->addr_space[addr] |
                  (s->addr_space[addr + 1] << 8) |
                  (s->addr_space[addr + 2] << 16) |
                  (s->addr_space[addr + 3] << 24);
        }
        val = tmp;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 1) {
        uint8_t v = val & 0xff;
        if (addr < sizeof(s->addr_space)) {
            switch (addr) {
            case DIVA_PCI_CTRL:
                s->diva_pci_ctrl = v;
                if (v & DIVA_RESET_BIT) {
                    s->pending_irq = true;
                }
                if (v & DIVA_IRQ_CLR) {
                    s->pending_irq = false;
                }
                pcibase_update_irq(s);
                s->addr_space[addr] = v;
                break;
            case TIGER_AUX_IRQMASK:
                if (v & TIGER_IRQ_BIT) {
                    s->irq_enabled = true;
                } else {
                    s->irq_enabled = false;
                }
                pcibase_update_irq(s);
                s->addr_space[addr] = v;
                break;
            case ELSA_IRQ_ADDR:
                if (v == QS1000_IRQ_ON || v == QS3000_IRQ_ON) {
                    s->irq_enabled = true;
                } else if (v == QS1000_IRQ_OFF || v == QS3000_IRQ_OFF) {
                    s->irq_enabled = false;
                }
                pcibase_update_irq(s);
                s->addr_space[addr] = v;
                break;
            default:
                s->addr_space[addr] = v;
                break;
            }
        }
    } else if (size == 2) {
        uint16_t v = val & 0xffff;
        if (addr == SCT_PLX_IRQ_ADDR) {
            s->sct_plx_irq = v;
            if (v & SCT_PLX_IRQ_ENABLE) {
                s->irq_enabled = true;
            } else {
                s->irq_enabled = false;
            }
            pcibase_update_irq(s);
        } else if (addr == SCT_PLX_RESET_ADDR) {
            s->sct_plx_reset = v;
        }
        if (addr + 1 < sizeof(s->addr_space)) {
            s->addr_space[addr] = v & 0xff;
            s->addr_space[addr + 1] = (v >> 8) & 0xff;
        }
    } else if (size == 4) {
        uint32_t v = val & 0xffffffff;
        if (addr == NICCY_IRQ_CTRL_REG) {
            s->niccy_irq_ctrl = v;
            if (v & NICCY_IRQ_ENABLE) {
                s->irq_enabled = true;
            }
            if (v & NICCY_IRQ_DISABLE) {
                s->irq_enabled = false;
            }
            pcibase_update_irq(s);
        } else if (addr == GAZEL_CNTRL) {
            s->gazel_cntrl = v;
        }
        if (addr + 3 < sizeof(s->addr_space)) {
            s->addr_space[addr] = v & 0xff;
            s->addr_space[addr + 1] = (v >> 8) & 0xff;
            s->addr_space[addr + 2] = (v >> 16) & 0xff;
            s->addr_space[addr + 3] = (v >> 24) & 0xff;
        }
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

    memset(s->cfg_space, 0, sizeof(s->cfg_space));
    memset(s->addr_space, 0, sizeof(s->addr_space));

    s->diva_pci_ctrl = 0;
    s->pita_misc = 0;
    s->pita_icr = 0;
    s->niccy_irq_ctrl = 0;
    s->sct_plx_irq = 0;
    s->sct_plx_reset = 0;
    s->gazel_cntrl = 0;
    s->gazel_incsr = 0;

    s->irq_enabled = false;
    s->pending_irq = true;

    pcibase_update_irq(s);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x80;
    s->bar_info[0].name = "infineon-pio";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x100;
    s->bar_info[1].name = "infineon-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->irq_enabled = true;
    s->pending_irq = true;
    pcibase_update_irq(s);
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

    (void)s;
}

static const VMStateDescription vmstate_pcibase = {
    .name = "ISDN_Infineon_pci_pci",
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

type_init(pcibase_register_types)

