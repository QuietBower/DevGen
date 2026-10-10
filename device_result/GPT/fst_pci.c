/*
 * QEMU PCI device model for FarSync (farsync.c)
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

/* Removed driver-specific include that is not available in QEMU build env */
/* #include "farsync.h" */

#define TYPE_PCIBASE_DEVICE "fst_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* VENDOR_ID and DEVICE_ID come from the first entry of fst_pci_dev_id */
/* Values taken from Linux driver farsync.c */
#define PCI_VENDOR_ID_FARSITE           0x1619
#define PCI_DEVICE_ID_FARSITE_T2P       0x0400

#define FST_PCI_VENDOR_ID    PCI_VENDOR_ID_FARSITE
#define FST_PCI_DEVICE_ID    PCI_DEVICE_ID_FARSITE_T2P

/* Class ID: network controller, other (generic) */
#define FST_PCI_CLASS_ID     PCI_CLASS_NETWORK_OTHER

/* Memory layout as used by the driver */
#define FST_MEMSIZE          0x00100000
#define SMC_BASE             0x00002000UL
#define BFM_BASE             0x00010000UL

/* DMA related PLX/bridge registers (offsets in control space) */
#define CNTRL_9052           0x50
#define CNTRL_9054           0x6c
#define INTCSR_9052          0x4c
#define INTCSR_9054          0x68
#define DMAMODE0             0x80
#define DMAPADR0             0x84
#define DMALADR0             0x88
#define DMASIZ0              0x8c
#define DMADPR0              0x90
#define DMAMODE1             0x94
#define DMAPADR1             0x98
#define DMALADR1             0x9c
#define DMASIZ1              0xa0
#define DMADPR1              0xa4
#define DMACSR0              0xa8
#define DMACSR1              0xa9
#define DMAARB               0xac
#define DMATHR               0xb0
#define DMADAC0              0xb4
#define DMADAC1              0xb8
#define DMAMARBR             0xac

/* Interrupt bits used in control space */
#define FST_RX_DMA_INT       0x01
#define FST_TX_DMA_INT       0x02
#define FST_CARD_INT         0x04

/* Simple control region size (IOBAR index 1 in driver) */
#define FST_CTL_SIZE         0x100

/* Some minimal shared memory offsets used by driver FST_RD*/
#define OFF_smcVersion            (SMC_BASE + 0x0000)
#define OFF_endOfSmcSignature     (SMC_BASE + 0x0004)
#define OFF_taskStatus            (SMC_BASE + 0x0008)
#define OFF_numberOfPorts         (SMC_BASE + 0x000c)
#define OFF_interruptHandshake    (SMC_BASE + 0x0010)
#define OFF_interruptRetryCount   (SMC_BASE + 0x0014)
#define OFF_interruptEvent_base   (SMC_BASE + 0x0100)
#define OFF_interruptEvent_rdidx  (OFF_interruptEvent_base + 0x00)
#define OFF_interruptEvent_wridx  (OFF_interruptEvent_base + 0x01)
#define OFF_interruptEvent_evnt0  (OFF_interruptEvent_base + 0x02)

#define MAX_CIRBUFF               32

/* Event codes used by ISR (subset) */
#define TE1_ALMA   0x10
#define CTLA_CHG   0x20
#define CTLB_CHG   0x21
#define CTLC_CHG   0x22
#define CTLD_CHG   0x23
#define ABTA_SENT  0x30
#define ABTB_SENT  0x31
#define ABTC_SENT  0x32
#define ABTD_SENT  0x33
#define TXA_UNDF   0x40
#define TXB_UNDF   0x41
#define TXC_UNDF   0x42
#define TXD_UNDF   0x43
#define INIT_CPLT  0x50
#define INIT_FAIL  0x51

/* Dummy constants to satisfy check_started_ok */
#define SMC_VERSION   1
#define END_SIG       0x1234abcd


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
    /* Interrupt status/mask shadow registers (control space) */
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    /* Shadow of key shared memory/control registers used by the driver */
    uint8_t task_status;
    uint8_t interrupt_handshake;
    uint16_t smc_version;
    uint32_t smc_firmware_version;

    /* DMA Context */
    void *rx_dma_handle_host;
    dma_addr_t rx_dma_handle_card;
    void *tx_dma_handle_host;
    dma_addr_t tx_dma_handle_card;
    int dma_len_rx;
    int dma_len_tx;

    /* Track DMA channels */
    bool dmarx_in_progress;
    bool dmatx_in_progress;
    uint32_t dma0_padr;
    uint32_t dma0_ladr;
    uint32_t dma0_siz;
    uint32_t dma0_dpr;
    uint8_t  dma0_csr;
    uint32_t dma1_padr;
    uint32_t dma1_ladr;
    uint32_t dma1_siz;
    uint32_t dma1_dpr;
    uint8_t  dma1_csr;

    /* Operational status flags */
    unsigned int state;

    /* State used to handle reset sequences */
    unsigned int card_mode;

    /* Power management state (D0-D3) */
    uint8_t pm_state;

    /* Additional card info */
    unsigned int nports;
    unsigned int type;

    /* Simple control region shadow for PLX regs and ctlmem */
    uint8_t ctl_space[FST_CTL_SIZE];

    /* interrupt retry counter */
    uint32_t interrupt_retry_count;

    /* event ring buffer (interruptEvent) */
    uint8_t event_rdidx;
    uint8_t event_wridx;
    uint8_t event_buf[MAX_CIRBUFF];
};

static inline uint32_t pcibase_ctl_readl(PCIBaseState *s, hwaddr off)
{
    if (off + 4 > FST_CTL_SIZE) {
        return 0;
    }
    return s->ctl_space[off] |
           (s->ctl_space[off + 1] << 8) |
           (s->ctl_space[off + 2] << 16) |
           (s->ctl_space[off + 3] << 24);
}

static inline void pcibase_ctl_writel(PCIBaseState *s, hwaddr off, uint32_t val)
{
    if (off + 4 > FST_CTL_SIZE) {
        return;
    }
    s->ctl_space[off]     = val & 0xff;
    s->ctl_space[off + 1] = (val >> 8) & 0xff;
    s->ctl_space[off + 2] = (val >> 16) & 0xff;
    s->ctl_space[off + 3] = (val >> 24) & 0xff;
}

static inline uint16_t pcibase_ctl_readw(PCIBaseState *s, hwaddr off)
{
    if (off + 2 > FST_CTL_SIZE) {
        return 0;
    }
    return s->ctl_space[off] | (s->ctl_space[off + 1] << 8);
}

static inline void pcibase_ctl_writew(PCIBaseState *s, hwaddr off, uint16_t val)
{
    if (off + 2 > FST_CTL_SIZE) {
        return;
    }
    s->ctl_space[off] = val & 0xff;
    s->ctl_space[off + 1] = (val >> 8) & 0xff;
}

static inline uint8_t pcibase_ctl_readb(PCIBaseState *s, hwaddr off)
{
    if (off >= FST_CTL_SIZE) {
        return 0;
    }
    return s->ctl_space[off];
}

static inline void pcibase_ctl_writeb(PCIBaseState *s, hwaddr off, uint8_t val)
{
    if (off >= FST_CTL_SIZE) {
        return;
    }
    s->ctl_space[off] = val;
}

static bool pcibase_msi_enabled(PCIBaseState *s)
{
    return msi_enabled(PCI_DEVICE(s));
}

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (!(s->intr_status & s->intr_mask)) {
        if (!pcibase_msi_enabled(s)) {
            pci_set_irq(pdev, 0);
        }
        return;
    }

    if (pcibase_msi_enabled(s)) {
        msi_notify(pdev, 0);
    } else {
        pci_set_irq(pdev, 1);
    }
}

static void pcibase_raise_irq_bits(PCIBaseState *s, uint32_t bits)
{
    s->intr_status |= bits;
    pcibase_update_irq(s);
}

/* Device-initiated DMA logic based on driver access patterns */
static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* PCIDevice *pdev = PCI_DEVICE(s); */

    if (!is_write) {
        /* readback does nothing */
        return;
    }

    /* Channel 0: RX (card -> host dma buffer) */
    if (s->dma0_csr & 0x03) {
        /* Perform DMA: src = card mem, dst = host DMA buffer */
        dma_addr_t src __attribute__((unused)) = s->dma0_ladr;
        dma_addr_t dst __attribute__((unused)) = s->rx_dma_handle_card;
        size_t len = s->dma0_siz;

        if (len && s->rx_dma_handle_host) {
            /* Read from emulated on-card memory into host DMA buffer */
            /* For now we simply zero-fill to satisfy driver expectations */
            memset(s->rx_dma_handle_host, 0, len);
            /* No real pci_dma_read because driver will read from coherent buffer */
        }

        s->dma0_csr &= ~0x03; /* clear start bits */
        s->dmarx_in_progress = false;

        /* Raise RX DMA interrupt via INTCSR_9054 bit 0x00200000 */
        uint32_t intcsr = pcibase_ctl_readl(s, INTCSR_9054);
        intcsr |= 0x00200000;
        pcibase_ctl_writel(s, INTCSR_9054, intcsr);
        pcibase_raise_irq_bits(s, FST_RX_DMA_INT);
    }

    /* Channel 1: TX (host dma buffer -> card) */
    if (s->dma1_csr & 0x03) {
        dma_addr_t src __attribute__((unused)) = s->tx_dma_handle_card;
        dma_addr_t dst __attribute__((unused)) = s->dma1_ladr;
        size_t len = s->dma1_siz;

        if (len && s->tx_dma_handle_host) {
            /* Data already prepared in host DMA buffer; card side just consumes it */
            (void)src;
            (void)dst;
        }

        s->dma1_csr &= ~0x03;
        s->dmatx_in_progress = false;

        /* Raise TX DMA interrupt via INTCSR_9054 bit 0x00400000 */
        uint32_t intcsr = pcibase_ctl_readl(s, INTCSR_9054);
        intcsr |= 0x00400000;
        pcibase_ctl_writel(s, INTCSR_9054, intcsr);
        pcibase_raise_irq_bits(s, FST_TX_DMA_INT);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* BAR0 maps card shared memory (mem) */
    if (addr + size > FST_MEMSIZE) {
        return 0;
    }

    /* We only emulate a few key locations used during probe/config */
    if (addr == OFF_smcVersion && size == 2) {
        return s->smc_version;
    }
    if (addr == OFF_endOfSmcSignature && size == 4) {
        return END_SIG;
    }
    if (addr == OFF_taskStatus && size == 1) {
        return s->task_status;
    }
    if (addr == OFF_numberOfPorts && size == 4) {
        return s->nports;
    }
    if (addr == OFF_interruptHandshake && size == 1) {
        return s->interrupt_handshake;
    }
    if (addr == OFF_interruptRetryCount && size == 4) {
        return s->interrupt_retry_count;
    }
    if (addr == OFF_interruptEvent_rdidx && size == 1) {
        return s->event_rdidx & 0x1f;
    }
    if (addr == OFF_interruptEvent_wridx && size == 1) {
        return s->event_wridx & 0x1f;
    }
    if (addr >= OFF_interruptEvent_evnt0 &&
        addr < OFF_interruptEvent_evnt0 + MAX_CIRBUFF && size == 1) {
        unsigned idx = addr - OFF_interruptEvent_evnt0;
        return s->event_buf[idx];
    }

    /* Default: zero */
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > FST_MEMSIZE) {
        return;
    }

    if (addr == OFF_interruptHandshake && size == 1) {
        /* Driver writes 0xEE as SW ack; we simply store it */
        s->interrupt_handshake = (uint8_t)val;
        return;
    }

    if (addr == OFF_interruptRetryCount && size == 4) {
        /* Driver writes 0 to clear */
        s->interrupt_retry_count = (uint32_t)val;
        return;
    }

    if (addr == OFF_interruptEvent_rdidx && size == 1) {
        s->event_rdidx = (uint8_t)(val & 0x1f);
        return;
    }

    /* Ignore all other writes for now */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* BAR1 IO space emulates pci_conf, ctlmem, PLX registers */
    uint64_t val = 0;

    if (size == 4) {
        val = pcibase_ctl_readl(s, addr);

        /* INTCSR_9054 special handling for DMA-complete bits */
        if (addr == INTCSR_9054) {
            /* Return current shadow */
            val = pcibase_ctl_readl(s, INTCSR_9054);
        }

        if (addr == CNTRL_9052) {
            val = pcibase_ctl_readl(s, CNTRL_9052);
        }
    } else if (size == 2) {
        val = pcibase_ctl_readw(s, addr);
    } else if (size == 1) {
        val = pcibase_ctl_readb(s, addr);

        if (addr == DMACSR0) {
            val = s->dma0_csr;
        } else if (addr == DMACSR1) {
            val = s->dma1_csr;
        }
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Handle PLX CNTRL reset sequences */
    if (size == 2) {
        if (addr == CNTRL_9054 + 2) {
            pcibase_ctl_writew(s, addr, (uint16_t)val);
            return;
        }
        if (addr == CNTRL_9052) {
            uint32_t cur = pcibase_ctl_readl(s, CNTRL_9052);
            uint32_t mask = (uint32_t)val;
            cur = (cur & ~0xffff0000u) | (mask & 0xffff0000u);
            pcibase_ctl_writel(s, CNTRL_9052, cur);
            return;
        }
        if (addr == INTCSR_9052) {
            pcibase_ctl_writew(s, INTCSR_9052, (uint16_t)val);
            /* enabling or disabling interrupts: track mask */
            if ((uint16_t)val) {
                s->intr_mask |= FST_CARD_INT | FST_RX_DMA_INT | FST_TX_DMA_INT;
            } else {
                s->intr_mask = 0;
                pcibase_update_irq(s);
            }
            return;
        }
    }

    if (size == 4) {
        if (addr == INTCSR_9054) {
            pcibase_ctl_writel(s, INTCSR_9054, (uint32_t)val);
            if (val) {
                s->intr_mask |= FST_CARD_INT | FST_RX_DMA_INT | FST_TX_DMA_INT;
            } else {
                s->intr_mask = 0;
                pcibase_update_irq(s);
            }
            return;
        }
        if (addr == DMAMODE0 || addr == DMAMODE1 || addr == DMATHR) {
            pcibase_ctl_writel(s, addr, (uint32_t)val);
            return;
        }
        if (addr == DMAPADR0) {
            s->dma0_padr = (uint32_t)val;
            return;
        }
        if (addr == DMALADR0) {
            s->dma0_ladr = (uint32_t)val;
            return;
        }
        if (addr == DMASIZ0) {
            s->dma0_siz = (uint32_t)val;
            return;
        }
        if (addr == DMADPR0) {
            s->dma0_dpr = (uint32_t)val;
            return;
        }
        if (addr == DMAPADR1) {
            s->dma1_padr = (uint32_t)val;
            return;
        }
        if (addr == DMALADR1) {
            s->dma1_ladr = (uint32_t)val;
            return;
        }
        if (addr == DMASIZ1) {
            s->dma1_siz = (uint32_t)val;
            return;
        }
        if (addr == DMADPR1) {
            s->dma1_dpr = (uint32_t)val;
            return;
        }
    }

    if (size == 1) {
        if (addr == DMACSR0) {
            s->dma0_csr = (uint8_t)val;
            if (s->dma0_csr & 0x03) {
                s->dmarx_in_progress = true;
                pcibase_do_dma(s, true);
            }
            return;
        }
        if (addr == DMACSR1) {
            s->dma1_csr = (uint8_t)val;
            if (s->dma1_csr & 0x03) {
                s->dmatx_in_progress = true;
                pcibase_do_dma(s, true);
            }
            return;
        }

        /* DMACSR0/1 also used for clearing DMA interrupt (driver writes 0x8) */
        if (addr == DMACSR0 || addr == DMACSR1) {
            return;
        }

        /* Generic ctlmem byte write */
        pcibase_ctl_writeb(s, addr, (uint8_t)val);
        return;
    }

    /* Default: generic writes */
    if (size == 4) {
        pcibase_ctl_writel(s, addr, (uint32_t)val);
    } else if (size == 2) {
        pcibase_ctl_writew(s, addr, (uint16_t)val);
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

    s->task_status = 0x00;
    s->interrupt_handshake = 0;
    s->smc_version = SMC_VERSION;
    s->smc_firmware_version = 0;
    s->intr_status = 0;
    s->intr_mask = 0;
    s->state = 0;
    s->card_mode = 0;
    s->pm_state = 0;

    s->interrupt_retry_count = 0;
    s->event_rdidx = 0;
    s->event_wridx = 0;
    memset(s->event_buf, 0, sizeof(s->event_buf));

    memset(s->ctl_space, 0, sizeof(s->ctl_space));

    s->dmarx_in_progress = false;
    s->dmatx_in_progress = false;
    s->dma0_padr = s->dma0_ladr = s->dma0_siz = s->dma0_dpr = 0;
    s->dma1_padr = s->dma1_ladr = s->dma1_siz = s->dma1_dpr = 0;
    s->dma0_csr = s->dma1_csr = 0;

    /* Initialize shared memory identity as firmware OK */
    s->task_status = 0x01; /* firmware OK */
    s->nports = 2;         /* minimal default consistent with T2P */
}

static hwaddr pcibase_pow2ceil(hwaddr val)
{
    if (val <= 1) {
        return 1;
    }
    val--;
    val |= val >> 1;
    val |= val >> 2;
    val |= val >> 4;
    val |= val >> 8;
    val |= val >> 16;
#if HOST_LONG_BITS == 64
    val |= val >> 32;
#endif
    return val + 1;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  FST_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  FST_PCI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, FST_PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: BAR0 MMIO (mem), BAR1 IO (pci_conf/ctlmem) */
    s->num_bars = 2;

    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = FST_MEMSIZE;
    s->bar_info[0].name = "fst-mmio";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_PIO;
    s->bar_info[1].size = FST_CTL_SIZE;
    s->bar_info[1].name = "fst-ctl";

    for (int i = 2; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Enable MSI if available */
    if (!msi_init(pdev, 0, 1, true, false, errp)) {
        s->has_msi = true;
    } else {
        s->has_msi = false;
    }
    s->has_msix = false;

    /* Initialize shadow state */
    s->task_status = 0x01; /* firmware OK */
    s->interrupt_handshake = 0;
    s->smc_version = SMC_VERSION;
    s->smc_firmware_version = 0;
    s->intr_status = 0;
    s->intr_mask = FST_CARD_INT | FST_RX_DMA_INT | FST_TX_DMA_INT;
    s->state = 0;
    s->card_mode = 0;
    s->pm_state = 0;
    s->nports = 2;
    s->type = 0;

    memset(s->ctl_space, 0, sizeof(s->ctl_space));
    s->interrupt_retry_count = 0;
    s->event_rdidx = 0;
    s->event_wridx = 0;
    memset(s->event_buf, 0, sizeof(s->event_buf));

    s->dmarx_in_progress = false;
    s->dmatx_in_progress = false;
    s->dma0_padr = s->dma0_ladr = s->dma0_siz = s->dma0_dpr = 0;
    s->dma1_padr = s->dma1_ladr = s->dma1_siz = s->dma1_dpr = 0;
    s->dma0_csr = s->dma1_csr = 0;

    (void)errp;
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "fst_pci",
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
