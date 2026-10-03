/*
 * QEMU PCI device model for Marvell SATA (sata_mv)
 * Phase 2: Functional behavior sufficient for Linux sata_mv probe/bind.
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

#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "sata_mv_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCIBASE_VENDOR_ID   PCI_VENDOR_ID_MARVELL
#define PCIBASE_DEVICE_ID   0x5040
#define PCIBASE_CLASS_ID    PCI_CLASS_STORAGE_SATA

#define WINDOW_CTRL(i)      (0x20030 + ((i) << 4))
#define WINDOW_BASE(i)      (0x20034 + ((i) << 4))

#define MV_MAX_Q_DEPTH      32
#define MV_MAX_SG_CT        128
#define MV_DMA_BOUNDARY     0xffffffffULL

#define ATA_PORT_TYPE_NAME  "ata_port"
#define REGS_PER_GTF        7

#define NO_PORT_MULT        0xffff

#define PMP_GSCR_SII_POL    129

/* Minimal subset of registers accessed early in mv_init_host path */
#define SATAHC0_REG_BASE           0x20000
#define MV_SATAHC_REG_SZ           0x2000
#define MV_SATAHC_ARBTR_REG_SZ     0x200
#define MV_PORT_REG_SZ             0x400

#define PCI_HC_MAIN_IRQ_CAUSE      0x1d64
#define PCI_HC_MAIN_IRQ_MASK       0x1d68

#define PCI_IRQ_CAUSE              0x1d08
#define PCI_IRQ_MASK               0x1d0c
#define PCI_UNMASK_ALL_IRQS        0x00000000U

#define HC_IRQ_CAUSE               0x14
#define HC_CFG                     0x20

#define EDMA_ERR_IRQ_CAUSE         0x8
#define EDMA_ERR_IRQ_MASK          0xc
#define EDMA_CFG                   0x28
#define EDMA_CMD                   0x28 /* same address, different bits */
#define EDMA_STATUS                0x2c
#define EDMA_IORDY_TMOUT           0x30
#define EDMA_REQ_Q_BASE_HI         0x10
#define EDMA_REQ_Q_IN_PTR          0x14
#define EDMA_REQ_Q_OUT_PTR         0x18
#define EDMA_RSP_Q_BASE_HI         0x1c
#define EDMA_RSP_Q_IN_PTR          0x20
#define EDMA_RSP_Q_OUT_PTR         0x24

#define SHD_BLK                    0x300

#define SATA_STATUS                0x300
#define SATA_CONTROL               0x304
#define SATA_ERROR                 0x308
#define SATA_ACTIVE                0x310

#define SATA_IFCTL                 0x40
#define SATA_IFSTAT                0x44
#define SATA_IFCFG                 0x48
#define VENDOR_UNIQUE_FIS          0x4c

#define BMDMA_CMD                  0x100
#define BMDMA_STATUS               0x104
#define BMDMA_PRD_LOW              0x108
#define BMDMA_PRD_HIGH             0x10c

#define FIS_IRQ_CAUSE              0x50

#define GPIO_PORT_CTL              0x1040

#define EDMA_ERR_IRQ_TRANSIENT     0x00000001U

#define ALL_PORTS_COAL_DONE        0x00010000U
#define PORTS_0_3_COAL_DONE        0x00000010U
#define PORTS_4_7_COAL_DONE        0x00000100U
#define ALL_PORTS_COAL_IRQ         0x00000001U
#define HC_COAL_IRQ                0x00000001U

#define DONE_IRQ                   0x1U
#define ERR_IRQ                    0x2U
#define DEV_IRQ                    0x1U
#define DMA_IRQ                    0x2U

#define HC0_IRQ_PEND               0x0000ffffU

#define EDMA_STATUS_CACHE_EMPTY    0x00000001U
#define EDMA_STATUS_IDLE           0x00000002U

#define EDMA_EN                    0x00000001U
#define EDMA_DS                    0x00000002U
#define EDMA_RESET                 0x00000004U

#define EDMA_REQ_Q_PTR_SHIFT       5
#define EDMA_RSP_Q_PTR_SHIFT       5
#define MV_MAX_Q_DEPTH_MASK        (MV_MAX_Q_DEPTH - 1)
#define EDMA_REQ_Q_BASE_LO_MASK    0xfffffc00U
#define EDMA_RSP_Q_BASE_LO_MASK    0xffffff00U

#define SCR_STATUS                 0
#define SCR_CONTROL                1
#define SCR_ERROR                  2
#define SCR_ACTIVE                 3
#define SCR_NOTIFICATION           4

#define ATA_REG_DATA       0
#define ATA_REG_ERR        1
#define ATA_REG_NSECT      2
#define ATA_REG_LBAL       3
#define ATA_REG_LBAM       4
#define ATA_REG_LBAH       5
#define ATA_REG_DEVICE     6
#define ATA_REG_STATUS     7

#define SHD_CTL_AST        0x20

#define ATA_DMA_START      0x01
#define ATA_DMA_WR         0x08
#define ATA_DMA_ACTIVE     0x20
#define ATA_DMA_ERR        0x04
#define ATA_DMA_INTR       0x02

#define ATA_BUSY           0x80

#define MV_PORTS_PER_HC    4

#define MV_PORT_TO_SHIFT_AND_HARDPORT(port, shift, hardport) \
    do { \
        hardport = (port) & (MV_PORTS_PER_HC - 1); \
        shift = (hardport * 2); \
    } while (0)


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

/* Very small synthetic EDMA ring / DMA structures to satisfy driver expectations */

typedef struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    /* MMIO-backed register space */
    uint32_t main_irq_cause;
    uint32_t main_irq_mask;

    uint32_t pci_irq_cause;
    uint32_t pci_irq_mask;

    uint32_t hc_irq_cause[2];

    uint32_t edma_err_irq_cause[8];
    uint32_t edma_err_irq_mask[8];
    uint32_t edma_cfg[8];
    uint32_t edma_cmd[8];
    uint32_t edma_status[8];
    uint32_t edma_iordy_tmout[8];

    uint32_t scr_status[8][5];
    uint32_t scr_error[8];

    uint32_t bmdma_cmd[8];
    uint32_t bmdma_status_reg[8];
    uint32_t bmdma_prd_low[8];
    uint32_t bmdma_prd_high[8];

    uint32_t sata_ifctl[8];
    uint32_t sata_ifstat[8];

    /* Simple EDMA queues */
    uint32_t edma_req_q_in_ptr[8];
    uint32_t edma_req_q_out_ptr[8];
    uint32_t edma_rsp_q_in_ptr[8];
    uint32_t edma_rsp_q_out_ptr[8];

    uint64_t edma_req_q_base[8];
    uint64_t edma_rsp_q_base[8];

    /* per-port simple shadow of ATA status/altstatus */
    uint8_t ata_status[8];
    uint8_t ata_altstatus[8];

    uint32_t window_ctrl[4];
    uint32_t window_base[4];

    qemu_irq legacy_irq;
} PCIBaseState;


static inline PCIBaseState *pcibase_from_mmio(void *opaque)
{
    return (PCIBaseState *)opaque;
}

static void pcibase_update_irq(PCIBaseState *s)
{
    uint32_t pending = s->main_irq_cause & s->main_irq_mask;

    if (pending) {
        pci_set_irq(PCI_DEVICE(s), 1);
    } else {
        pci_set_irq(PCI_DEVICE(s), 0);
    }
}

static void pcibase_raise_port_irq(PCIBaseState *s, int port, uint32_t port_irqs)
{
    int shift, hardport;
    uint32_t bits = 0;

    MV_PORT_TO_SHIFT_AND_HARDPORT(port, shift, hardport);

    if (port_irqs & DONE_IRQ) {
        bits |= (DMA_IRQ << hardport);
        s->hc_irq_cause[port / MV_PORTS_PER_HC] |= (DONE_IRQ << shift);
    }
    if (port_irqs & ERR_IRQ) {
        bits |= (DEV_IRQ << hardport);
        s->hc_irq_cause[port / MV_PORTS_PER_HC] |= (ERR_IRQ << shift);
    }

    s->main_irq_cause |= bits;
    pcibase_update_irq(s);
}

static int pcibase_port_from_addr(hwaddr addr)
{
    if (addr < SATAHC0_REG_BASE)
        return -1;
    hwaddr off = addr - SATAHC0_REG_BASE;
    int hc = off / MV_SATAHC_REG_SZ;
    hwaddr within_hc = off % MV_SATAHC_REG_SZ;

    if (hc < 0 || hc > 1)
        return -1;

    if (within_hc < MV_SATAHC_ARBTR_REG_SZ)
        return -1;

    within_hc -= MV_SATAHC_ARBTR_REG_SZ;
    int port = within_hc / MV_PORT_REG_SZ;
    if (port < 0 || port >= MV_PORTS_PER_HC)
        return -1;

    return hc * MV_PORTS_PER_HC + port;
}

static hwaddr pcibase_port_base(int port)
{
    int hc = port / MV_PORTS_PER_HC;
    int hardport = port & (MV_PORTS_PER_HC - 1);
    return SATAHC0_REG_BASE + hc * MV_SATAHC_REG_SZ +
           MV_SATAHC_ARBTR_REG_SZ + hardport * MV_PORT_REG_SZ;
}

static int pcibase_hc_from_addr(hwaddr addr)
{
    if (addr < SATAHC0_REG_BASE)
        return -1;
    hwaddr off = addr - SATAHC0_REG_BASE;
    int hc = off / MV_SATAHC_REG_SZ;
    if (hc < 0 || hc > 1)
        return -1;
    return hc;
}

static hwaddr pcibase_hc_base(int hc)
{
    return SATAHC0_REG_BASE + hc * MV_SATAHC_REG_SZ;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = pcibase_from_mmio(opaque);
    uint64_t val = 0;

    if (size == 1) {
        int port = pcibase_port_from_addr(addr);
        if (port >= 0) {
            hwaddr port_base = pcibase_port_base(port);
            hwaddr off = addr - port_base;

            if (off == SHD_BLK + ATA_REG_STATUS * sizeof(uint32_t)) {
                val = s->ata_status[port];
                return val;
            }
            if (off == SHD_BLK + SHD_CTL_AST) {
                val = s->ata_altstatus[port];
                return val;
            }
        }
        return 0xff;
    }

    if (size != 4) {
        return 0;
    }

    /* PCI host-level IRQ registers */
    if (addr == PCI_HC_MAIN_IRQ_CAUSE) {
        val = s->main_irq_cause;
        return val;
    }
    if (addr == PCI_HC_MAIN_IRQ_MASK) {
        val = s->main_irq_mask;
        return val;
    }
    if (addr == PCI_IRQ_CAUSE) {
        val = s->pci_irq_cause;
        return val;
    }
    if (addr == PCI_IRQ_MASK) {
        val = s->pci_irq_mask;
        return val;
    }

    /* MBUS window setup */
    if (addr >= WINDOW_CTRL(0) && addr <= WINDOW_CTRL(3)) {
        int i = (addr - WINDOW_CTRL(0)) >> 4;
        if (i >= 0 && i < 4) {
            val = s->window_ctrl[i];
            return val;
        }
    }
    if (addr >= WINDOW_BASE(0) && addr <= WINDOW_BASE(3)) {
        int i = (addr - WINDOW_BASE(0)) >> 4;
        if (i >= 0 && i < 4) {
            val = s->window_base[i];
            return val;
        }
    }

    /* HC space */
    int hc = pcibase_hc_from_addr(addr);
    if (hc >= 0) {
        hwaddr hc_base = pcibase_hc_base(hc);
        hwaddr hoff = addr - hc_base;
        if (hoff == HC_IRQ_CAUSE) {
            val = s->hc_irq_cause[hc];
            return val;
        }
        if (hoff == HC_CFG) {
            val = 0;
            return val;
        }
    }

    /* per-port space */
    int port = pcibase_port_from_addr(addr);
    if (port >= 0 && port < 8) {
        hwaddr port_base = pcibase_port_base(port);
        hwaddr off = addr - port_base;

        if (off == EDMA_ERR_IRQ_CAUSE) {
            val = s->edma_err_irq_cause[port];
            return val;
        }
        if (off == EDMA_ERR_IRQ_MASK) {
            val = s->edma_err_irq_mask[port];
            return val;
        }
        if (off == EDMA_CFG) {
            val = s->edma_cfg[port];
            return val;
        }
        if (off == EDMA_CMD) {
            val = s->edma_cmd[port];
            return val;
        }
        if (off == EDMA_STATUS) {
            val = s->edma_status[port];
            return val;
        }
        if (off == EDMA_IORDY_TMOUT) {
            val = s->edma_iordy_tmout[port];
            return val;
        }
        if (off == EDMA_REQ_Q_IN_PTR) {
            val = s->edma_req_q_in_ptr[port];
            return val;
        }
        if (off == EDMA_REQ_Q_OUT_PTR) {
            val = s->edma_req_q_out_ptr[port];
            return val;
        }
        if (off == EDMA_RSP_Q_IN_PTR) {
            val = s->edma_rsp_q_in_ptr[port];
            return val;
        }
        if (off == EDMA_RSP_Q_OUT_PTR) {
            val = s->edma_rsp_q_out_ptr[port];
            return val;
        }
        if (off == EDMA_REQ_Q_BASE_HI) {
            val = (uint32_t)(s->edma_req_q_base[port] >> 32);
            return val;
        }
        if (off == EDMA_RSP_Q_BASE_HI) {
            val = (uint32_t)(s->edma_rsp_q_base[port] >> 32);
            return val;
        }

        if (off == SATA_STATUS) {
            val = s->scr_status[port][SCR_STATUS];
            return val;
        }
        if (off == SATA_CONTROL) {
            val = s->scr_status[port][SCR_CONTROL];
            return val;
        }
        if (off == SATA_ERROR) {
            val = s->scr_status[port][SCR_ERROR];
            return val;
        }
        if (off == SATA_ACTIVE) {
            val = s->scr_status[port][SCR_ACTIVE];
            return val;
        }

        if (off == SATA_IFCTL) {
            val = s->sata_ifctl[port];
            return val;
        }
        if (off == SATA_IFSTAT) {
            val = s->sata_ifstat[port];
            return val;
        }

        if (off == BMDMA_CMD) {
            val = s->bmdma_cmd[port];
            return val;
        }
        if (off == BMDMA_STATUS) {
            val = s->bmdma_status_reg[port];
            return val;
        }
        if (off == BMDMA_PRD_LOW) {
            val = s->bmdma_prd_low[port];
            return val;
        }
        if (off == BMDMA_PRD_HIGH) {
            val = s->bmdma_prd_high[port];
            return val;
        }

        if (off == FIS_IRQ_CAUSE) {
            val = 0;
            return val;
        }
    }

    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = pcibase_from_mmio(opaque);

    if (size == 1) {
        int port = pcibase_port_from_addr(addr);
        if (port >= 0) {
            hwaddr port_base = pcibase_port_base(port);
            hwaddr off = addr - port_base;

            if (off == SHD_BLK + SHD_CTL_AST) {
                s->ata_altstatus[port] = (uint8_t)val;
                return;
            }
        }
        return;
    }

    if (size != 4) {
        return;
    }

    if (addr == PCI_HC_MAIN_IRQ_MASK) {
        s->main_irq_mask = (uint32_t)val;
        pcibase_update_irq(s);
        return;
    }
    if (addr == PCI_HC_MAIN_IRQ_CAUSE) {
        s->main_irq_cause &= ~(uint32_t)val;
        pcibase_update_irq(s);
        return;
    }
    if (addr == PCI_IRQ_MASK) {
        s->pci_irq_mask = (uint32_t)val;
        return;
    }
    if (addr == PCI_IRQ_CAUSE) {
        s->pci_irq_cause &= ~(uint32_t)val;
        return;
    }

    if (addr >= WINDOW_CTRL(0) && addr <= WINDOW_CTRL(3)) {
        int i = (addr - WINDOW_CTRL(0)) >> 4;
        if (i >= 0 && i < 4) {
            s->window_ctrl[i] = (uint32_t)val;
            return;
        }
    }
    if (addr >= WINDOW_BASE(0) && addr <= WINDOW_BASE(3)) {
        int i = (addr - WINDOW_BASE(0)) >> 4;
        if (i >= 0 && i < 4) {
            s->window_base[i] = (uint32_t)val;
            return;
        }
    }

    int hc = pcibase_hc_from_addr(addr);
    if (hc >= 0) {
        hwaddr hc_base = pcibase_hc_base(hc);
        hwaddr hoff = addr - hc_base;
        if (hoff == HC_IRQ_CAUSE) {
            s->hc_irq_cause[hc] &= ~(uint32_t)val;
            return;
        }
    }

    int port = pcibase_port_from_addr(addr);
    if (port >= 0 && port < 8) {
        hwaddr port_base = pcibase_port_base(port);
        hwaddr off = addr - port_base;

        if (off == EDMA_ERR_IRQ_CAUSE) {
            uint32_t mask = (uint32_t)val;
            s->edma_err_irq_cause[port] &= mask;
            return;
        }
        if (off == EDMA_ERR_IRQ_MASK) {
            s->edma_err_irq_mask[port] = (uint32_t)val;
            return;
        }
        if (off == EDMA_CFG) {
            s->edma_cfg[port] = (uint32_t)val;
            return;
        }
        if (off == EDMA_CMD) {
            uint32_t old = s->edma_cmd[port];
            s->edma_cmd[port] = (uint32_t)val;

            if (val & EDMA_RESET) {
                s->edma_status[port] = EDMA_STATUS_CACHE_EMPTY | EDMA_STATUS_IDLE;
                s->edma_cmd[port] &= ~(EDMA_RESET | EDMA_EN | EDMA_DS);
            }
            if ((val & EDMA_DS) && (old & EDMA_EN)) {
                s->edma_cmd[port] &= ~EDMA_EN;
            }
            if (val & EDMA_EN) {
                s->edma_cmd[port] |= EDMA_EN;
                s->edma_status[port] = EDMA_STATUS_CACHE_EMPTY | EDMA_STATUS_IDLE;
            }
            return;
        }
        if (off == EDMA_STATUS) {
            s->edma_status[port] = (uint32_t)val;
            return;
        }
        if (off == EDMA_IORDY_TMOUT) {
            s->edma_iordy_tmout[port] = (uint32_t)val;
            return;
        }
        if (off == EDMA_REQ_Q_IN_PTR) {
            s->edma_req_q_in_ptr[port] = (uint32_t)val;
            pcibase_raise_port_irq(s, port, DONE_IRQ);
            return;
        }
        if (off == EDMA_REQ_Q_OUT_PTR) {
            s->edma_req_q_out_ptr[port] = (uint32_t)val;
            return;
        }
        if (off == EDMA_RSP_Q_IN_PTR) {
            s->edma_rsp_q_in_ptr[port] = (uint32_t)val;
            return;
        }
        if (off == EDMA_RSP_Q_OUT_PTR) {
            s->edma_rsp_q_out_ptr[port] = (uint32_t)val;
            return;
        }
        if (off == EDMA_REQ_Q_BASE_HI) {
            s->edma_req_q_base[port] &= 0xffffffffULL;
            s->edma_req_q_base[port] |= ((uint64_t)val) << 32;
            return;
        }
        if (off == EDMA_RSP_Q_BASE_HI) {
            s->edma_rsp_q_base[port] &= 0xffffffffULL;
            s->edma_rsp_q_base[port] |= ((uint64_t)val) << 32;
            return;
        }

        if (off == SATA_STATUS) {
            s->scr_status[port][SCR_STATUS] = (uint32_t)val;
            return;
        }
        if (off == SATA_CONTROL) {
            s->scr_status[port][SCR_CONTROL] = (uint32_t)val;
            if ((val & 0x301) == 0x301) {
                s->scr_status[port][SCR_STATUS] = 0x123;
            }
            return;
        }
        if (off == SATA_ERROR) {
            s->scr_status[port][SCR_ERROR] = (uint32_t)val;
            return;
        }
        if (off == SATA_ACTIVE) {
            s->scr_status[port][SCR_ACTIVE] = (uint32_t)val;
            return;
        }

        if (off == SATA_IFCTL) {
            s->sata_ifctl[port] = (uint32_t)val;
            return;
        }
        if (off == SATA_IFSTAT) {
            s->sata_ifstat[port] = (uint32_t)val;
            return;
        }

        if (off == BMDMA_CMD) {
            uint32_t old = s->bmdma_cmd[port];
            s->bmdma_cmd[port] = (uint32_t)val;

            if ((!(old & ATA_DMA_START)) && (val & ATA_DMA_START)) {
                s->bmdma_status_reg[port] |= ATA_DMA_ACTIVE;
                pcibase_raise_port_irq(s, port, DONE_IRQ);
                s->bmdma_status_reg[port] &= ~ATA_DMA_ACTIVE;
                s->bmdma_status_reg[port] |= ATA_DMA_INTR;
            }
            if (!(val & ATA_DMA_START)) {
                s->bmdma_status_reg[port] &= ~ATA_DMA_ACTIVE;
            }
            return;
        }
        if (off == BMDMA_STATUS) {
            s->bmdma_status_reg[port] &= ~((uint32_t)val);
            return;
        }
        if (off == BMDMA_PRD_LOW) {
            s->bmdma_prd_low[port] = (uint32_t)val;
            return;
        }
        if (off == BMDMA_PRD_HIGH) {
            s->bmdma_prd_high[port] = (uint32_t)val;
            return;
        }

        if (off == EDMA_ERR_IRQ_CAUSE + 0x4) {
            uint32_t mask = (uint32_t)val;
            s->edma_err_irq_cause[port] &= mask;
            return;
        }

        if (off == EDMA_ERR_IRQ_CAUSE + 0x4 || off == FIS_IRQ_CAUSE) {
            return;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    if (size == 1) {
        return 0xff;
    }
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->main_irq_cause = 0;
    s->main_irq_mask = 0;
    s->pci_irq_cause = 0;
    s->pci_irq_mask = 0;

    for (int i = 0; i < 2; i++) {
        s->hc_irq_cause[i] = 0;
    }
    for (int p = 0; p < 8; p++) {
        s->edma_err_irq_cause[p] = 0;
        s->edma_err_irq_mask[p] = ~EDMA_ERR_IRQ_TRANSIENT;
        s->edma_cfg[p] = 0;
        s->edma_cmd[p] = 0;
        s->edma_status[p] = EDMA_STATUS_CACHE_EMPTY | EDMA_STATUS_IDLE;
        s->edma_iordy_tmout[p] = 0;
        s->edma_req_q_in_ptr[p] = 0;
        s->edma_req_q_out_ptr[p] = 0;
        s->edma_rsp_q_in_ptr[p] = 0;
        s->edma_rsp_q_out_ptr[p] = 0;
        s->edma_req_q_base[p] = 0;
        s->edma_rsp_q_base[p] = 0;

        for (int r = 0; r < 5; r++) {
            s->scr_status[p][r] = 0;
        }
        s->scr_status[p][SCR_STATUS] = 0x123;

        s->ata_status[p] = 0x50;
        s->ata_altstatus[p] = 0x50;

        s->bmdma_cmd[p] = 0;
        s->bmdma_status_reg[p] = 0;
        s->bmdma_prd_low[p] = 0;
        s->bmdma_prd_high[p] = 0;

        s->sata_ifctl[p] = 0;
        s->sata_ifstat[p] = 0;
    }

    for (int i = 0; i < 4; i++) {
        s->window_ctrl[i] = 0;
        s->window_base[i] = 0;
    }
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

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x10000;
    s->bar_info[0].name  = "sata_mv-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    pcibase_reset(DEVICE(pdev));
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
    .name = "sata_mv_pci",
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

