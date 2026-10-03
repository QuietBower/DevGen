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

#define TYPE_PCIBASE_DEVICE "ioatdma_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_DEVICE_ID_INTEL_IOAT_TBG0 0x3430
#define IOAT_VENDOR_ID PCI_VENDOR_ID_INTEL
#define IOAT_DEVICE_ID PCI_DEVICE_ID_INTEL_IOAT_TBG0
#define IOAT_CLASS_ID 0x0880

#define IOAT_CHANCNT_OFFSET 0x00
#define IOAT_XFERCAP_OFFSET 0x01
#define IOAT_INTRCTRL_OFFSET 0x03
#define IOAT_INTRCTRL_MASTER_INT_EN 0x01
#define IOAT_INTRCTRL_MSIX_VECTOR_CONTROL 0x08
#define IOAT_VER_OFFSET 0x08
#define IOAT_DMA_CAP_OFFSET 0x10
#define IOAT_DCACTRL_OFFSET 0x30
#define IOAT_DCAOFFSET_OFFSET 0x14
#define IOAT_CHANCTRL_OFFSET 0x00
#define IOAT_CHANSTS_OFFSET 0x08
#define IOAT_CHANERR_OFFSET 0x28
#define IOAT_CHANERR_MASK_OFFSET 0x2C
#define IOAT_CHANCMP_OFFSET_LOW 0x18
#define IOAT_CHANCMP_OFFSET_HIGH 0x1C
#define IOAT_CHAN_LTR_SWSEL_OFFSET 0xBC
#define IOAT_CHAN_LTR_ACTIVE_OFFSET 0xC0
#define IOAT_CHAN_LTR_IDLE_OFFSET 0xC4
#define IOAT_CHAN_DRSCTL_OFFSET 0xB6
#define IOAT_CHAN_DRS_EN 0x0100
#define IOAT_CHAN_DRS_AUTOWRAP 0x0200
#define IOAT_CHAN_DRSZ_2MB 0x0009
#define IOAT3_CSI_CONTROL_OFFSET 0x0C
#define IOAT3_PCI_CONTROL_OFFSET 0x0E
#define IOAT3_PCI_CONTROL_MEMWR 0x1
#define IOAT3_CSI_CONTROL_PREFETCH 0x1
#define IOAT3_APICID_TAG_MAP_OFFSET_LOW 0x10
#define IOAT3_APICID_TAG_MAP_OFFSET_HIGH 0x14
#define IOAT_PREFETCH_LIMIT_OFFSET 0x4C
#define IOAT_PCI_CHANERR_INT_OFFSET 0x180
#define IOAT_PCI_DMAUNCERRSTS_OFFSET 0x148
#define IOAT_CHANCMD_OFFSET(ver) ((ver) < IOAT_VER_2_0 ? IOAT1_CHANCMD_OFFSET : IOAT2_CHANCMD_OFFSET)
#define IOAT1_CHANCMD_OFFSET 0x14
#define IOAT2_CHANCMD_OFFSET 0x04
#define IOAT_CHANCMD_RESET 0x20
#define IOAT_CHANCMD_SUSPEND 0x04

#define IOAT_CHANCTRL_RUN (IOAT_CHANCTRL_INT_REARM | \
                         IOAT_CHANCTRL_ERR_INT_EN | \
                         IOAT_CHANCTRL_ERR_COMPLETION_EN | \
                         IOAT_CHANCTRL_ANY_ERR_ABORT_EN)
#define IOAT_CHANCTRL_INT_REARM 0x0001
#define IOAT_CHANCTRL_ERR_INT_EN 0x0010
#define IOAT_CHANCTRL_ERR_COMPLETION_EN 0x0004
#define IOAT_CHANCTRL_ANY_ERR_ABORT_EN 0x0008

#define IOAT_CHANSTS_DONE 0x1
#define IOAT_CHANSTS_STATUS 0x7ULL
#define IOAT_CHANSTS_ACTIVE 0x0
#define IOAT_CHANSTS_HALTED 0x3
#define IOAT_CHANSTS_COMPLETED_DESCRIPTOR_ADDR (~0x3fULL)

#define IOAT_CHANERR_XOR_P_OR_CRC_ERR 0x10000
#define IOAT_CHANERR_XOR_Q_ERR 0x20000
#define IOAT_CHANERR_WRITE_DATA_ERR 0x0200
#define IOAT_CHANERR_READ_DATA_ERR 0x0100
#define IOAT_CHANERR_HANDLE_MASK (IOAT_CHANERR_XOR_P_OR_CRC_ERR | IOAT_CHANERR_XOR_Q_ERR)
#define IOAT_CHANERR_RECOVER_MASK (IOAT_CHANERR_READ_DATA_ERR | IOAT_CHANERR_WRITE_DATA_ERR)

#define IOAT_CAP_DWBES 0x00002000
#define IOAT_CAP_DPS 0x00800000
#define IOAT_CAP_RAID16SS 0x00020000
#define IOAT_CAP_PQ 0x00000200
#define IOAT_CAP_XOR 0x00000100

#define IOAT_CHAN_LTR_SWSEL_IDLE 0x1
#define IOAT_CHAN_LTR_SWSEL_ACTIVE 0x0
#define IOAT_CHAN_LTR_IDLE_SNVAL 0x0258
#define IOAT_CHAN_LTR_IDLE_SNLATSCALE 0x0800
#define IOAT_CHAN_LTR_IDLE_SNREQMNT 0x8000
#define IOAT_CHAN_LTR_ACTIVE_SNVAL 0x0000
#define IOAT_CHAN_LTR_ACTIVE_SNLATSCALE 0x0800
#define IOAT_CHAN_LTR_ACTIVE_SNREQMNT 0x8000

#define IOAT_VER_2_0 0x20
#define IOAT_VER_3_0 0x30
#define IOAT_VER_3_2 0x32
#define IOAT_VER_3_3 0x33
#define IOAT_VER_3_4 0x34

#define IOAT_MAX_CHANS 4
#define IOAT_CHUNK_SIZE (SZ_512K)
#define IOAT_DESC_SZ 64
#define IOAT_MAX_DESCS (1 << IOAT_MAX_ORDER)
#define IOAT_MAX_ORDER 16
#define IOAT_DESCS_PER_CHUNK (IOAT_CHUNK_SIZE / IOAT_DESC_SZ)

#define IOAT_DCA_GREQID_LASTID 0x80000000
#define IOAT3_DCA_GREQID_OFFSET 0x02
#define IOAT_DMA_DCA_ANY_CPU ~0

#define IOAT_OP_COPY 0x00
#define IOAT_OP_XOR 0x87
#define IOAT_OP_XOR_VAL 0x88
#define IOAT_OP_PQ 0x89
#define IOAT_OP_PQ_VAL 0x8a
#define IOAT_OP_PQ_16S 0xa0
#define IOAT_OP_PQ_VAL_16S 0xa1
#define IOAT_OP_PQ_UP 0x8b

enum ioat_irq_mode {
    IOAT_NOIRQ = 0,
    IOAT_MSIX,
    IOAT_MSI,
    IOAT_INTX
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

#define IOAT_CHAINADDR_OFFSET_LOW  0x0C
#define IOAT_CHAINADDR_OFFSET_HIGH 0x10

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    uint8_t chancnt;
    uint8_t xfercap;
    uint8_t intrctrl;
    uint8_t version;
    uint32_t cap;
    uint8_t csi_control;
    uint8_t pci_control;
    uint32_t apicid_tag_map_low;
    uint32_t apicid_tag_map_high;
    uint32_t prefetch_limit;
    uint32_t pci_chanerr_int;
    uint32_t pci_dmauncerrsts;

    struct {
        uint32_t ctrl;
        uint32_t status;
        uint32_t error;
        uint32_t error_mask;
        uint64_t comp_addr;
        uint64_t chain_addr;
        uint16_t ltr_swsel;
        uint32_t ltr_active;
        uint32_t ltr_idle;
        uint16_t drsctl;
        uint8_t chan_cmd;
        bool comp_addr_low_written;
        bool chain_addr_low_written;
    } chan_regs[IOAT_MAX_CHANS];

    struct dma_state {
        uint64_t ring_base;
        uint32_t ring_size;
        uint16_t head;
        uint16_t tail;
    } dma;

    uint8_t device_state;
    uint8_t reset_pending;
    uint8_t pm_state;
};

typedef struct ioat_dma_descriptor {
    uint32_t size;
    union {
        uint32_t ctl;
        struct {
            unsigned int int_en:1;
            unsigned int src_snoop_dis:1;
            unsigned int dest_snoop_dis:1;
            unsigned int compl_write:1;
            unsigned int fence:1;
            unsigned int null:1;
            unsigned int src_brk:1;
            unsigned int dest_brk:1;
            unsigned int bundle:1;
            unsigned int dest_dca:1;
            unsigned int hint:1;
            unsigned int rsvd2:13;
            unsigned int op:8;
        } ctl_f;
    };
    uint64_t src_addr;
    uint64_t dst_addr;
    uint64_t next;
    uint64_t rsv1;
    uint64_t rsv2;
    union {
        uint64_t user1;
        uint64_t tx_cnt;
    };
    uint64_t user2;
} ioat_dma_descriptor;

static void pcibase_update_irq(PCIBaseState *s, int channel)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->intrctrl & IOAT_INTRCTRL_MASTER_INT_EN &&
        s->intrctrl & IOAT_INTRCTRL_MSIX_VECTOR_CONTROL) {
        msix_notify(pdev, channel);
    }
}

static void pcibase_process_chain(PCIBaseState *s, int channel)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint64_t addr = s->chan_regs[channel].chain_addr;
    uint32_t count = 0;
    bool int_en = false;
    bool null_found = false;
    uint64_t completed_addr = 0;
    ioat_dma_descriptor desc;

    while (addr != 0 && count < 256) {
        pci_dma_read(pdev, addr, &desc, sizeof(desc));
        if (desc.ctl_f.null) {
            completed_addr = addr;
            null_found = true;
            break;
        }
        if (desc.ctl_f.op == IOAT_OP_COPY) {
            uint32_t size = desc.size;
            uint8_t *buf = g_malloc(size);
            pci_dma_read(pdev, desc.src_addr, buf, size);
            pci_dma_write(pdev, desc.dst_addr, buf, size);
            g_free(buf);
        }
        int_en |= desc.ctl_f.int_en;
        addr = desc.next;
        count++;
    }

    if (null_found) {
        s->chan_regs[channel].comp_addr = completed_addr;
        s->chan_regs[channel].status = IOAT_CHANSTS_DONE;
        if (int_en) {
            pcibase_update_irq(s, channel);
        }
    }

    /* Clear the run bits after processing */
    s->chan_regs[channel].ctrl &= ~IOAT_CHANCTRL_RUN;
}

static void pcibase_start_if_ready(PCIBaseState *s, int channel)
{
    if ((s->chan_regs[channel].chain_addr != 0) &&
        (s->chan_regs[channel].ctrl & IOAT_CHANCTRL_RUN)) {
        pcibase_process_chain(s, channel);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < 0x80) {
        switch (addr) {
        case IOAT_CHANCNT_OFFSET:
            val = s->chancnt;
            break;
        case IOAT_XFERCAP_OFFSET:
            val = s->xfercap;
            break;
        case IOAT_INTRCTRL_OFFSET:
            val = s->intrctrl;
            break;
        case IOAT_VER_OFFSET:
            if (size == 1) {
                val = s->version;
            } else {
                val = ~0ULL;
            }
            break;
        case IOAT_DMA_CAP_OFFSET:
            val = s->cap;
            break;
        case IOAT3_CSI_CONTROL_OFFSET:
            val = s->csi_control;
            break;
        case IOAT3_PCI_CONTROL_OFFSET:
            val = s->pci_control;
            break;
        case IOAT3_APICID_TAG_MAP_OFFSET_HIGH:
            val = s->apicid_tag_map_high;
            break;
        case IOAT_PREFETCH_LIMIT_OFFSET:
            val = s->prefetch_limit;
            break;
        case IOAT_PCI_CHANERR_INT_OFFSET:
            val = s->pci_chanerr_int;
            break;
        case IOAT_PCI_DMAUNCERRSTS_OFFSET:
            val = s->pci_dmauncerrsts;
            break;
        default:
            val = 0;
            break;
        }
    } else {
        int channel = (addr / 0x80) - 1;
        if (channel < 0 || channel >= IOAT_MAX_CHANS) {
            return ~0ULL;
        }
        uint32_t chan_offset = addr % 0x80;
        switch (chan_offset) {
        case IOAT_CHANCTRL_OFFSET:
            val = s->chan_regs[channel].ctrl;
            break;
        case IOAT_CHANSTS_OFFSET:
            if (size == 8) {
                val = (s->chan_regs[channel].comp_addr & ~0x3fULL) |
                      (s->chan_regs[channel].status & 0x3f);
            } else {
                val = s->chan_regs[channel].status & 0x3f;
            }
            break;
        case IOAT_CHANERR_OFFSET:
            val = s->chan_regs[channel].error;
            break;
        case IOAT_CHANERR_MASK_OFFSET:
            val = s->chan_regs[channel].error_mask;
            break;
        case IOAT_CHANCMP_OFFSET_LOW:
            if (size == 8) {
                val = s->chan_regs[channel].comp_addr;
            } else {
                val = (uint32_t)s->chan_regs[channel].comp_addr;
            }
            break;
        case IOAT_CHANCMP_OFFSET_HIGH:
            val = (uint32_t)(s->chan_regs[channel].comp_addr >> 32);
            break;
        case IOAT_CHAINADDR_OFFSET_LOW:
            if (size == 8) {
                val = s->chan_regs[channel].chain_addr;
            } else {
                val = (uint32_t)s->chan_regs[channel].chain_addr;
            }
            break;
        case IOAT_CHAINADDR_OFFSET_HIGH:
            val = (uint32_t)(s->chan_regs[channel].chain_addr >> 32);
            break;
        case IOAT_CHAN_LTR_SWSEL_OFFSET:
            val = s->chan_regs[channel].ltr_swsel;
            break;
        case IOAT_CHAN_LTR_ACTIVE_OFFSET:
            val = s->chan_regs[channel].ltr_active;
            break;
        case IOAT_CHAN_LTR_IDLE_OFFSET:
            val = s->chan_regs[channel].ltr_idle;
            break;
        case IOAT_CHAN_DRSCTL_OFFSET:
            val = s->chan_regs[channel].drsctl;
            break;
        case IOAT2_CHANCMD_OFFSET:
            val = s->chan_regs[channel].chan_cmd;
            break;
        default:
            val = 0;
            break;
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x80) {
        switch (addr) {
        case IOAT_CHANCNT_OFFSET:
            s->chancnt = val;
            break;
        case IOAT_XFERCAP_OFFSET:
            s->xfercap = val;
            break;
        case IOAT_INTRCTRL_OFFSET:
            if (size == 1) {
                s->intrctrl = val;
            }
            break;
        case IOAT_VER_OFFSET:
            break;
        case IOAT_DMA_CAP_OFFSET:
            s->cap = val;
            break;
        case IOAT3_CSI_CONTROL_OFFSET:
            s->csi_control = val;
            break;
        case IOAT3_PCI_CONTROL_OFFSET:
            s->pci_control = val;
            break;
        case IOAT3_APICID_TAG_MAP_OFFSET_HIGH:
            s->apicid_tag_map_high = val;
            break;
        case IOAT_PREFETCH_LIMIT_OFFSET:
            s->prefetch_limit = val;
            break;
        case IOAT_PCI_CHANERR_INT_OFFSET:
            s->pci_chanerr_int = val;
            break;
        case IOAT_PCI_DMAUNCERRSTS_OFFSET:
            s->pci_dmauncerrsts = val;
            break;
        case IOAT_DCACTRL_OFFSET:
            break;
        default:
            break;
        }
    } else {
        int channel = (addr / 0x80) - 1;
        if (channel < 0 || channel >= IOAT_MAX_CHANS) {
            return;
        }
        uint32_t chan_offset = addr % 0x80;
        switch (chan_offset) {
        case IOAT_CHANCTRL_OFFSET:
            s->chan_regs[channel].ctrl = val;
            pcibase_start_if_ready(s, channel);
            break;
        case IOAT_CHANSTS_OFFSET:
            if (size == 8) {
                /* Write-1-to-clear status bits */
                s->chan_regs[channel].status &= ~((uint32_t)val & 0x3f);
            } else {
                s->chan_regs[channel].status &= ~((uint32_t)val & 0x3f);
            }
            break;
        case IOAT_CHANERR_OFFSET:
            if (val) {
                s->chan_regs[channel].error &= ~val;
            }
            break;
        case IOAT_CHANERR_MASK_OFFSET:
            s->chan_regs[channel].error_mask = val;
            break;
        case IOAT_CHANCMP_OFFSET_LOW:
            /* CHANCMP is read-only; ignore writes */
            break;
        case IOAT_CHANCMP_OFFSET_HIGH:
            /* CHANCMP is read-only; ignore writes */
            break;
        case IOAT_CHAINADDR_OFFSET_LOW:
            if (size == 8) {
                s->chan_regs[channel].chain_addr = val;
                s->chan_regs[channel].chain_addr_low_written = false;
            } else {
                s->chan_regs[channel].chain_addr = (s->chan_regs[channel].chain_addr & 0xffffffff00000000ULL) | (val & 0xffffffff);
                s->chan_regs[channel].chain_addr_low_written = true;
            }
            pcibase_start_if_ready(s, channel);
            break;
        case IOAT_CHAINADDR_OFFSET_HIGH:
            s->chan_regs[channel].chain_addr = (s->chan_regs[channel].chain_addr & 0xffffffff) | ((uint64_t)val << 32);
            if (s->chan_regs[channel].chain_addr_low_written) {
                s->chan_regs[channel].chain_addr_low_written = false;
                pcibase_start_if_ready(s, channel);
            }
            break;
        case IOAT_CHAN_LTR_SWSEL_OFFSET:
            s->chan_regs[channel].ltr_swsel = val;
            break;
        case IOAT_CHAN_LTR_ACTIVE_OFFSET:
            s->chan_regs[channel].ltr_active = val;
            break;
        case IOAT_CHAN_LTR_IDLE_OFFSET:
            s->chan_regs[channel].ltr_idle = val;
            break;
        case IOAT_CHAN_DRSCTL_OFFSET:
            s->chan_regs[channel].drsctl = val;
            break;
        case IOAT2_CHANCMD_OFFSET:
            if (val == IOAT_CHANCMD_RESET) {
                s->chan_regs[channel].status = IOAT_CHANSTS_ACTIVE;
                s->chan_regs[channel].error = 0;
                s->chan_regs[channel].ctrl = 0;
                s->chan_regs[channel].chan_cmd = 0;
                s->chan_regs[channel].comp_addr = 0;
                s->chan_regs[channel].comp_addr_low_written = false;
                s->chan_regs[channel].chain_addr = 0;
                s->chan_regs[channel].chain_addr_low_written = false;
            } else if (val == IOAT_CHANCMD_SUSPEND) {
                s->chan_regs[channel].status = IOAT_CHANSTS_HALTED;
            } else {
                s->chan_regs[channel].chan_cmd = val;
            }
            break;
        default:
            break;
        }
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

    s->chancnt = IOAT_MAX_CHANS;
    s->xfercap = 0x0c;
    s->intrctrl = 0;
    s->version = IOAT_VER_3_4;
    s->cap = IOAT_CAP_DWBES | IOAT_CAP_DPS;
    s->csi_control = 0;
    s->pci_control = 0;
    s->apicid_tag_map_low = 0;
    s->apicid_tag_map_high = 0;
    s->prefetch_limit = 0;
    s->pci_chanerr_int = 0;
    s->pci_dmauncerrsts = 0;

    for (int i = 0; i < IOAT_MAX_CHANS; i++) {
        s->chan_regs[i].ctrl = 0;
        s->chan_regs[i].status = IOAT_CHANSTS_ACTIVE;
        s->chan_regs[i].error = 0;
        s->chan_regs[i].error_mask = 0;
        s->chan_regs[i].comp_addr = 0;
        s->chan_regs[i].chain_addr = 0;
        s->chan_regs[i].ltr_swsel = 0;
        s->chan_regs[i].ltr_active = 0;
        s->chan_regs[i].ltr_idle = 0;
        s->chan_regs[i].drsctl = 0;
        s->chan_regs[i].chan_cmd = 0;
        s->chan_regs[i].comp_addr_low_written = false;
        s->chan_regs[i].chain_addr_low_written = false;
    }

    s->reset_pending = 0;
    s->device_state = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, IOAT_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, IOAT_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, IOAT_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    #define IOAT_MMIO_BAR_SIZE 0x4000
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = IOAT_MMIO_BAR_SIZE,
        .name = "ioat-mmio",
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msix = true;
    s->has_msi = false;
    if (s->has_msix) {
        if (msix_init_exclusive_bar(pdev, IOAT_MAX_CHANS, 1, errp) < 0) {
            error_setg(errp, "Failed to initialize MSI-X");
            return;
        }
    }

    s->version = IOAT_VER_3_4;
    s->chancnt = IOAT_MAX_CHANS;
    s->xfercap = 0x0c;
    s->cap = IOAT_CAP_DWBES | IOAT_CAP_DPS;
    s->intrctrl = 0;
    for (int i = 0; i < IOAT_MAX_CHANS; i++) {
        s->chan_regs[i].status = IOAT_CHANSTS_ACTIVE;
        s->chan_regs[i].comp_addr = 0;
        s->chan_regs[i].chain_addr = 0;
        s->chan_regs[i].drsctl = 0;
        s->chan_regs[i].chan_cmd = 0;
        s->chan_regs[i].comp_addr_low_written = false;
        s->chan_regs[i].chain_addr_low_written = false;
    }
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
    .name = "ioatdma_pci",
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
