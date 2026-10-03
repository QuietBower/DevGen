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

#define TYPE_PCIBASE_DEVICE "mrvl_cn10k_dpi_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID 0x177d
#define DEVICE_ID 0xA080
#define CLASS_ID PCI_CLASS_OTHERS

#define PCI_DEVID_MRVL_CN10K_DPI_PF	0xA080
#define PCI_SUBDEVID_MRVL_CN10K_DPI_PF	0xB900
#define PCI_DPI_CFG_BAR	0
#define DPI_MAX_REQQ_INT	0x20
#define DPI_MAX_CC_INT		0x40
#define DPI_MBOX_PF_VF_INT_IDX	0x75
#define DPI_MAX_IRQS (DPI_MBOX_PF_VF_INT_IDX + 1)
#define DPI_MAX_VFS	0x20
#define DPI_MAX_ENG_FIFO_SZ	0x20
#define DPI_MAX_ENG_MOLR	0x400
#define DPI_MAX_ENGINES 6
#define DPI_DMA_IDS_DMA_NPA_PF_FUNC(x)	FIELD_PREP(GENMASK_ULL(31, 16), x)
#define DPI_DMA_IDS_INST_STRM(x)	FIELD_PREP(GENMASK_ULL(47, 40), x)
#define DPI_DMA_IDS_DMA_STRM(x)		FIELD_PREP(GENMASK_ULL(39, 32), x)
#define DPI_DMA_ENG_EN_MOLR(x)		FIELD_PREP(GENMASK_ULL(41, 32), x)
#define DPI_EBUS_PORTX_CFG_MPS(x)	FIELD_PREP(GENMASK(6, 4), x)
#define DPI_DMA_IDS_DMA_SSO_PF_FUNC(x)	FIELD_PREP(GENMASK(15, 0), x)
#define DPI_DMA_IDS2_INST_AURA(x)	FIELD_PREP(GENMASK(19, 0), x)
#define DPI_DMA_IBUFF_CSIZE_CSIZE(x)	FIELD_PREP(GENMASK(13, 0), x)
#define DPI_EBUS_PORTX_CFG_MRRS(x)	FIELD_PREP(GENMASK(2, 0), x)
#define DPI_ENG_BUF_BLKS(x)		FIELD_PREP(GENMASK(5, 0), x)
#define DPI_DMA_CONTROL_DMA_ENB		GENMASK_ULL(53, 48)
#define DPI_DMA_CONTROL_O_MODE		BIT_ULL(14)
#define DPI_DMA_CONTROL_LDWB		BIT_ULL(32)
#define DPI_DMA_CONTROL_WQECSMODE1	BIT_ULL(37)
#define DPI_DMA_CONTROL_ZBWCSEN		BIT_ULL(39)
#define DPI_DMA_CONTROL_WQECSOFF(ofst)	(((u64)ofst) << 40)
#define DPI_DMA_CONTROL_WQECSDIS	BIT_ULL(47)
#define DPI_DMA_CONTROL_PKT_EN		BIT_ULL(56)
#define DPI_DMA_IBUFF_CSIZE_NPA_FREE	BIT(16)
#define DPI_CTL_EN			BIT_ULL(0)
#define DPI_DMA_CC_INT			BIT_ULL(0)
#define DPI_DMA_QRST			BIT_ULL(0)
#define DPI_REQQ_INT_INSTRFLT		BIT_ULL(0)
#define DPI_REQQ_INT_RDFLT		BIT_ULL(1)
#define DPI_REQQ_INT_WRFLT		BIT_ULL(2)
#define DPI_REQQ_INT_CSFLT		BIT_ULL(3)
#define DPI_REQQ_INT_INST_DBO		BIT_ULL(4)
#define DPI_REQQ_INT_INST_ADDR_NULL	BIT_ULL(5)
#define DPI_REQQ_INT_INST_FILL_INVAL	BIT_ULL(6)
#define DPI_REQQ_INT_INSTR_PSN		BIT_ULL(7)
#define DPI_REQQ_INT \
	(DPI_REQQ_INT_INSTRFLT		| \
	DPI_REQQ_INT_RDFLT		| \
	DPI_REQQ_INT_WRFLT		| \
	DPI_REQQ_INT_CSFLT		| \
	DPI_REQQ_INT_INST_DBO		| \
	DPI_REQQ_INT_INST_ADDR_NULL	| \
	DPI_REQQ_INT_INST_FILL_INVAL	| \
	DPI_REQQ_INT_INSTR_PSN)
#define DPI_PF_RAS_EBI_DAT_PSN	BIT_ULL(0)
#define DPI_PF_RAS_NCB_DAT_PSN	BIT_ULL(1)
#define DPI_PF_RAS_NCB_CMD_PSN	BIT_ULL(2)
#define DPI_PF_RAS_INT \
	(DPI_PF_RAS_EBI_DAT_PSN  | \
	 DPI_PF_RAS_NCB_DAT_PSN  | \
	 DPI_PF_RAS_NCB_CMD_PSN)
#define DPI_MBOX_VFID(msg)		FIELD_GET(GENMASK_ULL(7, 0), msg)
#define DPI_MBOX_CMD(msg)		FIELD_GET(GENMASK_ULL(11, 8), msg)
#define DPI_MBOX_CBUF_SIZE(msg)		FIELD_GET(GENMASK_ULL(27, 12), msg)
#define DPI_MBOX_CBUF_AURA(msg)		FIELD_GET(GENMASK_ULL(47, 28), msg)
#define DPI_MBOX_SSO_PFFUNC(msg)	FIELD_GET(GENMASK_ULL(63, 48), msg)
#define DPI_MBOX_NPA_PFFUNC(msg)	FIELD_GET(GENMASK_ULL(15, 0), msg)
#define DPI_MBOX_WQES_COMPL(msg)	FIELD_GET(GENMASK_ULL(16, 16), msg)
#define DPI_MBOX_WQES_OFFSET(msg)	FIELD_GET(GENMASK_ULL(23, 17), msg)
#define DPI_DMAX_IBUFF_CSIZE(x)	(0x0ULL | ((x) << 11))
#define DPI_DMAX_IDS(x)		(0x18ULL | ((x) << 11))
#define DPI_DMAX_IDS2(x)	(0x20ULL | ((x) << 11))
#define DPI_DMAX_QRST(x)	(0x30ULL | ((x) << 11))
#define DPI_CTL				0x10010ULL
#define DPI_DMA_CONTROL			0x10018ULL
#define DPI_PF_RAS			0x10308ULL
#define DPI_PF_RAS_ENA_W1C		0x10318ULL
#define DPI_MBOX_VF_PF_INT		0x16300ULL
#define DPI_MBOX_VF_PF_INT_W1S		0x16308ULL
#define DPI_MBOX_VF_PF_INT_ENA_W1C	0x16310ULL
#define DPI_MBOX_VF_PF_INT_ENA_W1S	0x16318ULL
#define DPI_DMA_ENGX_EN(x)		(0x10040ULL | ((x) << 3))
#define DPI_ENGX_BUF(x)			(0x100C0ULL | ((x) << 3))
#define DPI_EBUS_PORTX_CFG(x)		(0x10100ULL | ((x) << 3))
#define DPI_DMA_CCX_INT(x)		(0x11000ULL | ((x) << 3))
#define DPI_DMA_CCX_INT_ENA_W1C(x)	(0x11800ULL | ((x) << 3))
#define DPI_REQQX_INT(x)			(0x12C00ULL | ((x) << 5))
#define DPI_REQQX_INT_ENA_W1C(x)	(0x13800ULL | ((x) << 5))
#define DPI_MBOX_PF_VF_DATA0(x)		(0x16000ULL | ((x) << 4))
#define DPI_MBOX_PF_VF_DATA1(x)		(0x16008ULL | ((x) << 4))
#define DPI_WCTL_FIF_THR	0x17008ULL
#define DPI_EBUS_MAX_PORTS	2
#define DPI_EBUS_MRRS_MIN	128
#define DPI_EBUS_MRRS_MAX	1024
#define DPI_EBUS_MPS_MIN	128
#define DPI_EBUS_MPS_MAX	1024
#define DPI_WCTL_FIFO_THRESHOLD	0x30
#define DPI_QUEUE_OPEN		0x1
#define DPI_QUEUE_CLOSE		0x2
#define DPI_REG_DUMP		0x3
#define DPI_GET_REG_CFG		0x4
#define DPI_QUEUE_OPEN_V2	0x5

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

    /* Shadow register values */
    uint64_t dma_control;
    uint64_t ctl;
    uint64_t eng_buf[DPI_MAX_ENGINES];
    uint64_t eng_en[DPI_MAX_ENGINES];
    uint64_t ebus_port_cfg[DPI_EBUS_MAX_PORTS];
    uint64_t pf_ras;
    uint64_t pf_ras_ena;
    uint64_t reqq_int[DPI_MAX_REQQ_INT];
    uint64_t reqq_int_ena[DPI_MAX_REQQ_INT];
    uint64_t cc_int[DPI_MAX_CC_INT];
    uint64_t cc_int_ena[DPI_MAX_CC_INT];
    uint64_t dmax_qrst[DPI_MAX_VFS];
    uint64_t dmax_ids2[DPI_MAX_VFS];
    uint64_t dmax_ids[DPI_MAX_VFS];
    uint64_t dmax_ibuff_csize[DPI_MAX_VFS];
    uint64_t mbox_pf_vf_data0[DPI_MAX_VFS];
    uint64_t mbox_pf_vf_data1[DPI_MAX_VFS];
    uint64_t mbox_vf_pf_int;
    uint64_t mbox_vf_pf_int_ena;
    uint64_t wctl_fif_thr;
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    // Interrupt logic not used since driver only uses MSI-X.
}

G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    // DMA not used by this driver.
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= DPI_DMAX_IBUFF_CSIZE(0) && addr <= DPI_DMAX_QRST(DPI_MAX_VFS - 1)) {
        unsigned vf_index = (addr >> 11) & 0x1F;
        unsigned reg_offset = addr & 0x7FF;
        switch (reg_offset) {
        case 0x00: val = s->dmax_ibuff_csize[vf_index]; break;
        case 0x18: val = s->dmax_ids[vf_index]; break;
        case 0x20: val = s->dmax_ids2[vf_index]; break;
        case 0x30: val = s->dmax_qrst[vf_index]; break;
        default: val = 0; break;
        }
        return val;
    }

    if (addr >= DPI_ENGX_BUF(0) && addr <= DPI_ENGX_BUF(DPI_MAX_ENGINES - 1)) {
        unsigned engine = (addr - DPI_ENGX_BUF(0)) / 8;
        if (engine < DPI_MAX_ENGINES) {
            val = s->eng_buf[engine];
        }
    } else if (addr >= DPI_DMA_ENGX_EN(0) && addr <= DPI_DMA_ENGX_EN(DPI_MAX_ENGINES - 1)) {
        unsigned engine = (addr - DPI_DMA_ENGX_EN(0)) / 8;
        if (engine < DPI_MAX_ENGINES) {
            val = s->eng_en[engine];
        }
    } else if (addr >= DPI_EBUS_PORTX_CFG(0) && addr <= DPI_EBUS_PORTX_CFG(DPI_EBUS_MAX_PORTS - 1)) {
        unsigned port = (addr - DPI_EBUS_PORTX_CFG(0)) / 8;
        if (port < DPI_EBUS_MAX_PORTS) {
            val = s->ebus_port_cfg[port];
        }
    } else if (addr >= DPI_DMA_CCX_INT(0) && addr <= DPI_DMA_CCX_INT(DPI_MAX_CC_INT - 1)) {
        unsigned i = (addr - DPI_DMA_CCX_INT(0)) / 8;
        if (i < DPI_MAX_CC_INT) {
            val = s->cc_int[i];
        }
    } else if (addr >= DPI_DMA_CCX_INT_ENA_W1C(0) && addr <= DPI_DMA_CCX_INT_ENA_W1C(DPI_MAX_CC_INT - 1)) {
        unsigned i = (addr - DPI_DMA_CCX_INT_ENA_W1C(0)) / 8;
        if (i < DPI_MAX_CC_INT) {
            val = s->cc_int_ena[i];
        }
    } else if (addr >= DPI_REQQX_INT(0) && addr <= DPI_REQQX_INT(DPI_MAX_REQQ_INT - 1)) {
        unsigned i = (addr - DPI_REQQX_INT(0)) / 0x20;
        if (i < DPI_MAX_REQQ_INT) {
            val = s->reqq_int[i];
        }
    } else if (addr >= DPI_REQQX_INT_ENA_W1C(0) && addr <= DPI_REQQX_INT_ENA_W1C(DPI_MAX_REQQ_INT - 1)) {
        unsigned i = (addr - DPI_REQQX_INT_ENA_W1C(0)) / 0x20;
        if (i < DPI_MAX_REQQ_INT) {
            val = s->reqq_int_ena[i];
        }
    } else if (addr >= DPI_MBOX_PF_VF_DATA0(0) && addr <= DPI_MBOX_PF_VF_DATA0(DPI_MAX_VFS - 1)) {
        unsigned vf = (addr - DPI_MBOX_PF_VF_DATA0(0)) / 16;
        if (vf < DPI_MAX_VFS) {
            val = s->mbox_pf_vf_data0[vf];
        }
    } else if (addr >= DPI_MBOX_PF_VF_DATA1(0) && addr <= DPI_MBOX_PF_VF_DATA1(DPI_MAX_VFS - 1)) {
        unsigned vf = (addr - DPI_MBOX_PF_VF_DATA1(0)) / 16;
        if (vf < DPI_MAX_VFS) {
            val = s->mbox_pf_vf_data1[vf];
        }
    } else {
        switch (addr) {
        case DPI_CTL: val = s->ctl; break;
        case DPI_DMA_CONTROL: val = s->dma_control; break;
        case DPI_PF_RAS: val = s->pf_ras; break;
        case DPI_PF_RAS_ENA_W1C: val = s->pf_ras_ena; break;
        case DPI_MBOX_VF_PF_INT: val = s->mbox_vf_pf_int; break;
        case DPI_MBOX_VF_PF_INT_W1S: val = s->mbox_vf_pf_int; break;
        case DPI_MBOX_VF_PF_INT_ENA_W1C: val = s->mbox_vf_pf_int_ena; break;
        case DPI_MBOX_VF_PF_INT_ENA_W1S: val = s->mbox_vf_pf_int_ena; break;
        case DPI_WCTL_FIF_THR: val = s->wctl_fif_thr; break;
        default: val = 0; break;
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= DPI_DMAX_IBUFF_CSIZE(0) && addr <= DPI_DMAX_QRST(DPI_MAX_VFS - 1)) {
        unsigned vf_index = (addr >> 11) & 0x1F;
        unsigned reg_offset = addr & 0x7FF;
        switch (reg_offset) {
        case 0x00:
            s->dmax_ibuff_csize[vf_index] = val;
            break;
        case 0x18:
            s->dmax_ids[vf_index] = val;
            break;
        case 0x20:
            s->dmax_ids2[vf_index] = val;
            break;
        case 0x30:
            /* Queue reset: write triggers immediate clear of QRST bit */
            if (val & DPI_DMA_QRST) {
                s->dmax_qrst[vf_index] = val & ~DPI_DMA_QRST;
            } else {
                s->dmax_qrst[vf_index] = val;
            }
            break;
        default:
            break;
        }
        return;
    }

    if (addr >= DPI_ENGX_BUF(0) && addr <= DPI_ENGX_BUF(DPI_MAX_ENGINES - 1)) {
        unsigned engine = (addr - DPI_ENGX_BUF(0)) / 8;
        if (engine < DPI_MAX_ENGINES) {
            s->eng_buf[engine] = val;
        }
    } else if (addr >= DPI_DMA_ENGX_EN(0) && addr <= DPI_DMA_ENGX_EN(DPI_MAX_ENGINES - 1)) {
        unsigned engine = (addr - DPI_DMA_ENGX_EN(0)) / 8;
        if (engine < DPI_MAX_ENGINES) {
            s->eng_en[engine] = val;
        }
    } else if (addr >= DPI_EBUS_PORTX_CFG(0) && addr <= DPI_EBUS_PORTX_CFG(DPI_EBUS_MAX_PORTS - 1)) {
        unsigned port = (addr - DPI_EBUS_PORTX_CFG(0)) / 8;
        if (port < DPI_EBUS_MAX_PORTS) {
            s->ebus_port_cfg[port] = val;
        }
    } else if (addr >= DPI_DMA_CCX_INT(0) && addr <= DPI_DMA_CCX_INT(DPI_MAX_CC_INT - 1)) {
        unsigned i = (addr - DPI_DMA_CCX_INT(0)) / 8;
        if (i < DPI_MAX_CC_INT) {
            s->cc_int[i] &= ~val; /* W1C */
        }
    } else if (addr >= DPI_DMA_CCX_INT_ENA_W1C(0) && addr <= DPI_DMA_CCX_INT_ENA_W1C(DPI_MAX_CC_INT - 1)) {
        unsigned i = (addr - DPI_DMA_CCX_INT_ENA_W1C(0)) / 8;
        if (i < DPI_MAX_CC_INT) {
            s->cc_int_ena[i] &= ~val; /* W1C */
        }
    } else if (addr >= DPI_REQQX_INT(0) && addr <= DPI_REQQX_INT(DPI_MAX_REQQ_INT - 1)) {
        unsigned i = (addr - DPI_REQQX_INT(0)) / 0x20;
        if (i < DPI_MAX_REQQ_INT) {
            s->reqq_int[i] &= ~val; /* W1C */
        }
    } else if (addr >= DPI_REQQX_INT_ENA_W1C(0) && addr <= DPI_REQQX_INT_ENA_W1C(DPI_MAX_REQQ_INT - 1)) {
        unsigned i = (addr - DPI_REQQX_INT_ENA_W1C(0)) / 0x20;
        if (i < DPI_MAX_REQQ_INT) {
            s->reqq_int_ena[i] &= ~val; /* W1C */
        }
    } else if (addr >= DPI_MBOX_PF_VF_DATA0(0) && addr <= DPI_MBOX_PF_VF_DATA0(DPI_MAX_VFS - 1)) {
        unsigned vf = (addr - DPI_MBOX_PF_VF_DATA0(0)) / 16;
        if (vf < DPI_MAX_VFS) {
            s->mbox_pf_vf_data0[vf] = val;
        }
    } else if (addr >= DPI_MBOX_PF_VF_DATA1(0) && addr <= DPI_MBOX_PF_VF_DATA1(DPI_MAX_VFS - 1)) {
        unsigned vf = (addr - DPI_MBOX_PF_VF_DATA1(0)) / 16;
        if (vf < DPI_MAX_VFS) {
            s->mbox_pf_vf_data1[vf] = val;
        }
    } else {
        switch (addr) {
        case DPI_CTL:
            s->ctl = val;
            break;
        case DPI_DMA_CONTROL:
            s->dma_control = val;
            break;
        case DPI_PF_RAS:
            s->pf_ras &= ~val; /* W1C */
            break;
        case DPI_PF_RAS_ENA_W1C:
            s->pf_ras_ena &= ~val; /* W1C */
            break;
        case DPI_MBOX_VF_PF_INT:
            s->mbox_vf_pf_int &= ~val; /* W1C */
            break;
        case DPI_MBOX_VF_PF_INT_W1S:
            s->mbox_vf_pf_int |= val; /* W1S */
            break;
        case DPI_MBOX_VF_PF_INT_ENA_W1C:
            s->mbox_vf_pf_int_ena &= ~val; /* W1C */
            break;
        case DPI_MBOX_VF_PF_INT_ENA_W1S:
            s->mbox_vf_pf_int_ena |= val; /* W1S */
            break;
        case DPI_WCTL_FIF_THR:
            s->wctl_fif_thr = val;
            break;
        default:
            break;
        }
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 8, .max_access_size = 8 },
    .impl  = { .min_access_size = 8, .max_access_size = 8 },
};

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

    memset(&s->dma_control, 0,
           sizeof(*s) - offsetof(PCIBaseState, dma_control));
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    int msix_cap;

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x177d);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0xA080);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pdev, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* BAR0: MMIO register space */
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_mmio_ops, s,
                          "dpi_mmio", 0x20000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[0]);

    /* BAR4: MSI-X table and PBA */
    memory_region_init(&s->bar_regions[4], OBJECT(s), "dpi_msix", 0x2000);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[4]);

    msix_cap = pci_add_capability(pdev, PCI_CAP_ID_MSIX, 0, 12, errp);
    if (msix_cap < 0) {
        return;
    }
    if (msix_init(pdev, DPI_MAX_IRQS, &s->bar_regions[4], 4, 0,
                  &s->bar_regions[4], 4, 0x1000, msix_cap, errp)) {
        return;
    }

    s->num_bars = 2;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->bar_regions[4], &s->bar_regions[4]);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "mrvl_cn10k_dpi_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT64(dma_control, PCIBaseState),
        VMSTATE_UINT64(ctl, PCIBaseState),
        VMSTATE_UINT64_ARRAY(eng_buf, PCIBaseState, DPI_MAX_ENGINES),
        VMSTATE_UINT64_ARRAY(eng_en, PCIBaseState, DPI_MAX_ENGINES),
        VMSTATE_UINT64_ARRAY(ebus_port_cfg, PCIBaseState, DPI_EBUS_MAX_PORTS),
        VMSTATE_UINT64(pf_ras, PCIBaseState),
        VMSTATE_UINT64(pf_ras_ena, PCIBaseState),
        VMSTATE_UINT64_ARRAY(reqq_int, PCIBaseState, DPI_MAX_REQQ_INT),
        VMSTATE_UINT64_ARRAY(reqq_int_ena, PCIBaseState, DPI_MAX_REQQ_INT),
        VMSTATE_UINT64_ARRAY(cc_int, PCIBaseState, DPI_MAX_CC_INT),
        VMSTATE_UINT64_ARRAY(cc_int_ena, PCIBaseState, DPI_MAX_CC_INT),
        VMSTATE_UINT64_ARRAY(dmax_qrst, PCIBaseState, DPI_MAX_VFS),
        VMSTATE_UINT64_ARRAY(dmax_ids2, PCIBaseState, DPI_MAX_VFS),
        VMSTATE_UINT64_ARRAY(dmax_ids, PCIBaseState, DPI_MAX_VFS),
        VMSTATE_UINT64_ARRAY(dmax_ibuff_csize, PCIBaseState, DPI_MAX_VFS),
        VMSTATE_UINT64_ARRAY(mbox_pf_vf_data0, PCIBaseState, DPI_MAX_VFS),
        VMSTATE_UINT64_ARRAY(mbox_pf_vf_data1, PCIBaseState, DPI_MAX_VFS),
        VMSTATE_UINT64(mbox_vf_pf_int, PCIBaseState),
        VMSTATE_UINT64(mbox_vf_pf_int_ena, PCIBaseState),
        VMSTATE_UINT64(wctl_fif_thr, PCIBaseState),
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
