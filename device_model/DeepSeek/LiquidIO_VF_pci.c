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
#include "qemu/bitops.h"

#define TYPE_PCIBASE_DEVICE "LiquidIO_VF_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Vendor and Device IDs */
#define VENDOR_ID 0x177d
#define DEVICE_ID 0x9712
#define CLASS_ID  0x0200

/* BAR Sizes */
#define VF_BAR0_SIZE  (16 * 1024 * 1024)
#define VF_MSIX_BAR_SIZE 0x1000

/* Maximum Queues */
#define CN23XX_MAX_RINGS_PER_PF 64
#define MAX_INPUT_QUEUES  CN23XX_MAX_RINGS_PER_PF
#define MAX_OUTPUT_QUEUES CN23XX_MAX_RINGS_PER_PF

/* Register Offsets (from lio_vf_main.c) */
#define CN23XX_VF_SLI_IQ_PKT_CONTROL_START64     0x10000
#define CN23XX_VF_IQ_OFFSET                     0x20000
#define CN23XX_VF_SLI_OQ_PKT_CONTROL_START       0x10050
#define CN23XX_VF_OQ_OFFSET                      0x20000
#define CN23XX_VF_SLI_OQ_PKT_INT_LEVELS_START64  0x100A0
#define CN23XX_VF_SLI_PKT_MBOX_INT_START            0x10210
#define CN23XX_VF_SLI_IQ_INSTR_COUNT_START64     0x10040
#define CN23XX_VF_SLI_IQ_SIZE_START              0x10030
#define CN23XX_VF_SLI_IQ_DOORBELL_START          0x10020
#define CN23XX_VF_SLI_IQ_BASE_ADDR_START64       0x10010
#define CN23XX_VF_SLI_OQ_BASE_ADDR_START64       0x10070
#define CN23XX_VF_SLI_OQ_SIZE_START              0x10090
#define CN23XX_VF_SLI_OQ_PKT_SENT_START          0x100B0
#define CN23XX_VF_SLI_OQ0_BUFF_INFO_SIZE         0x10060
#define CN23XX_VF_SLI_OQ_PKT_CREDITS_START       0x10080
#define CN23XX_SLI_MBOX_OFFSET                    0x20000

/* Device States (from driver) */
#define OCT_DEV_BEGIN_STATE            0x0
#define OCT_DEV_PCI_ENABLE_DONE        0x1
#define OCT_DEV_PCI_MAP_DONE           0x2
#define OCT_DEV_DISPATCH_INIT_DONE     0x3
#define OCT_DEV_INSTR_QUEUE_INIT_DONE  0x4
#define OCT_DEV_SC_BUFF_POOL_INIT_DONE 0x5
#define OCT_DEV_RESP_LIST_INIT_DONE    0x6
#define OCT_DEV_DROQ_INIT_DONE         0x7
#define OCT_DEV_MBOX_SETUP_DONE        0x8
#define OCT_DEV_MSIX_ALLOC_VECTOR_DONE 0x9
#define OCT_DEV_INTR_SET_DONE          0xa
#define OCT_DEV_IO_QUEUES_DONE         0xb
#define OCT_DEV_HOST_OK                0xd
#define OCT_DEV_CORE_OK                0xe
#define OCT_DEV_RUNNING                0xf
#define OCT_DEV_IN_RESET               0x10
#define OCT_DEV_STATE_INVALID          0x11

/* New definitions from supplementary driver source */
#define CN23XX_PKT_INPUT_CTL_RST                     BIT_ULL(23)
#define CN23XX_PKT_INPUT_CTL_QUIET                   BIT_ULL(28)
#define CN23XX_PKT_INPUT_CTL_RPVF_POS                (48)
#define CN23XX_PKT_INPUT_CTL_RPVF_MASK               (0x3F)
#define CN23XX_PKT_INPUT_CTL_PF_NUM_POS              (45)
#define CN23XX_PKT_INPUT_CTL_PF_NUM_MASK             (0x7)
#define CN23XX_PKT_INPUT_CTL_VF_NUM_POS              (32)
#define CN23XX_PKT_INPUT_CTL_VF_NUM_MASK             (0x1FFF)
#define LIQUIDIO_BASE_MAJOR_VERSION 1
#define LIQUIDIO_BASE_MINOR_VERSION 7
#define LIQUIDIO_BASE_MICRO_VERSION 2

/* Missing defines - placeholder values until resolved */
#define CN23XX_PKT_MBOX_INT_BIT 0
#define CN23XX_INTR_MBOX_INT BIT_ULL(61)

struct octeon_pf_vf_hs_word {
    uint64_t dummy; /* placeholder, not used in QEMU */
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

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t int_status;
    uint64_t droq_intr;

    /* DMA Context */
    uint64_t iq_base_addr[MAX_INPUT_QUEUES];
    uint64_t oq_base_addr[MAX_OUTPUT_QUEUES];

    uint32_t device_state;
    bool in_reset;
    uint64_t iq_enable;
    uint64_t oq_enable;

    /* Extended Register Shadows */
    uint64_t iq_pkt_control[MAX_INPUT_QUEUES];
    uint64_t iq_size[MAX_INPUT_QUEUES];
    uint64_t iq_instr_count[MAX_INPUT_QUEUES];
    uint64_t iq_doorbell[MAX_INPUT_QUEUES];

    uint64_t oq_pkt_control[MAX_OUTPUT_QUEUES];
    uint64_t oq_base_addr_shadow[MAX_OUTPUT_QUEUES];
    uint64_t oq_size[MAX_OUTPUT_QUEUES];
    uint64_t oq_pkt_sent[MAX_OUTPUT_QUEUES];
    uint64_t oq_pkt_credits[MAX_OUTPUT_QUEUES];
    uint64_t oq_buff_info_size[MAX_OUTPUT_QUEUES];

    uint64_t oq_pkt_int_levels[MAX_OUTPUT_QUEUES];
    uint64_t pkt_mbox_int;  /* Interrupt status / mask */

    /* Mailbox shadow */
    uint64_t mbox_data;  /* simplified */

    /* New fields for VF-specific state */
    uint32_t rings_per_vf;
    uint16_t pf_num;
    uint16_t vf_num;

    QEMUTimer mbox_timer;  /* Timer to simulate PF response to mailbox */
};

static void pcibase_mbox_response(void *opaque);

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->int_status) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msix_enabled(pdev) && !msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* DMA logic not yet inferred from driver */
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= CN23XX_VF_SLI_IQ_PKT_CONTROL_START64 &&
        addr < CN23XX_VF_SLI_IQ_PKT_CONTROL_START64 + MAX_INPUT_QUEUES * 8) {
        int idx = (addr - CN23XX_VF_SLI_IQ_PKT_CONTROL_START64) / 8;
        val = s->iq_pkt_control[idx];
    } else if (addr >= CN23XX_VF_SLI_IQ_DOORBELL_START &&
               addr < CN23XX_VF_SLI_IQ_DOORBELL_START + MAX_INPUT_QUEUES * 8) {
        int idx = (addr - CN23XX_VF_SLI_IQ_DOORBELL_START) / 8;
        val = s->iq_doorbell[idx];
    } else if (addr >= CN23XX_VF_SLI_IQ_BASE_ADDR_START64 &&
               addr < CN23XX_VF_SLI_IQ_BASE_ADDR_START64 + MAX_INPUT_QUEUES * 8) {
        int idx = (addr - CN23XX_VF_SLI_IQ_BASE_ADDR_START64) / 8;
        val = s->iq_base_addr[idx];
    } else if (addr >= CN23XX_VF_SLI_IQ_SIZE_START &&
               addr < CN23XX_VF_SLI_IQ_SIZE_START + MAX_INPUT_QUEUES * 8) {
        int idx = (addr - CN23XX_VF_SLI_IQ_SIZE_START) / 8;
        val = s->iq_size[idx];
    } else if (addr >= CN23XX_VF_SLI_IQ_INSTR_COUNT_START64 &&
               addr < CN23XX_VF_SLI_IQ_INSTR_COUNT_START64 + MAX_INPUT_QUEUES * 8) {
        int idx = (addr - CN23XX_VF_SLI_IQ_INSTR_COUNT_START64) / 8;
        val = s->iq_instr_count[idx];
    } else if (addr >= CN23XX_VF_SLI_OQ_PKT_CONTROL_START &&
               addr < CN23XX_VF_SLI_OQ_PKT_CONTROL_START + MAX_OUTPUT_QUEUES * 8) {
        int idx = (addr - CN23XX_VF_SLI_OQ_PKT_CONTROL_START) / 8;
        val = s->oq_pkt_control[idx];
    } else if (addr >= CN23XX_VF_SLI_OQ_BASE_ADDR_START64 &&
               addr < CN23XX_VF_SLI_OQ_BASE_ADDR_START64 + MAX_OUTPUT_QUEUES * 8) {
        int idx = (addr - CN23XX_VF_SLI_OQ_BASE_ADDR_START64) / 8;
        val = s->oq_base_addr[idx];
    } else if (addr >= CN23XX_VF_SLI_OQ_SIZE_START &&
               addr < CN23XX_VF_SLI_OQ_SIZE_START + MAX_OUTPUT_QUEUES * 8) {
        int idx = (addr - CN23XX_VF_SLI_OQ_SIZE_START) / 8;
        val = s->oq_size[idx];
    } else if (addr >= CN23XX_VF_SLI_OQ_PKT_SENT_START &&
               addr < CN23XX_VF_SLI_OQ_PKT_SENT_START + MAX_OUTPUT_QUEUES * 8) {
        int idx = (addr - CN23XX_VF_SLI_OQ_PKT_SENT_START) / 8;
        val = s->oq_pkt_sent[idx];
    } else if (addr >= CN23XX_VF_SLI_OQ_PKT_CREDITS_START &&
               addr < CN23XX_VF_SLI_OQ_PKT_CREDITS_START + MAX_OUTPUT_QUEUES * 8) {
        int idx = (addr - CN23XX_VF_SLI_OQ_PKT_CREDITS_START) / 8;
        val = s->oq_pkt_credits[idx];
    } else if (addr >= CN23XX_VF_SLI_OQ0_BUFF_INFO_SIZE &&
               addr < CN23XX_VF_SLI_OQ0_BUFF_INFO_SIZE + MAX_OUTPUT_QUEUES * 8) {
        int idx = (addr - CN23XX_VF_SLI_OQ0_BUFF_INFO_SIZE) / 8;
        val = s->oq_buff_info_size[idx];
    } else if (addr >= CN23XX_VF_SLI_OQ_PKT_INT_LEVELS_START64 &&
               addr < CN23XX_VF_SLI_OQ_PKT_INT_LEVELS_START64 + MAX_OUTPUT_QUEUES * 8) {
        int idx = (addr - CN23XX_VF_SLI_OQ_PKT_INT_LEVELS_START64) / 8;
        val = s->oq_pkt_int_levels[idx];
    } else if (addr >= CN23XX_VF_SLI_PKT_MBOX_INT_START &&
               addr < CN23XX_VF_SLI_PKT_MBOX_INT_START + 8) {
        val = s->pkt_mbox_int;
    } else if (addr >= CN23XX_SLI_MBOX_OFFSET && addr < CN23XX_SLI_MBOX_OFFSET + 8) {
        val = s->mbox_data;
    } else {
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented read from 0x%lx size %d\n",
                      __func__, addr, size);
        val = 0;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= CN23XX_VF_SLI_IQ_PKT_CONTROL_START64 &&
        addr < CN23XX_VF_SLI_IQ_PKT_CONTROL_START64 + MAX_INPUT_QUEUES * 8) {
        int idx = (addr - CN23XX_VF_SLI_IQ_PKT_CONTROL_START64) / 8;
        s->iq_pkt_control[idx] = val; /* directly store value; driver handles RST clearing */
    } else if (addr >= CN23XX_VF_SLI_IQ_DOORBELL_START &&
               addr < CN23XX_VF_SLI_IQ_DOORBELL_START + MAX_INPUT_QUEUES * 8) {
        int idx = (addr - CN23XX_VF_SLI_IQ_DOORBELL_START) / 8;
        s->iq_doorbell[idx] = val;
        /* Actual doorbell ring: could trigger DMA to fetch instructions */
    } else if (addr >= CN23XX_VF_SLI_IQ_BASE_ADDR_START64 &&
               addr < CN23XX_VF_SLI_IQ_BASE_ADDR_START64 + MAX_INPUT_QUEUES * 8) {
        int idx = (addr - CN23XX_VF_SLI_IQ_BASE_ADDR_START64) / 8;
        s->iq_base_addr[idx] = val;
    } else if (addr >= CN23XX_VF_SLI_IQ_SIZE_START &&
               addr < CN23XX_VF_SLI_IQ_SIZE_START + MAX_INPUT_QUEUES * 8) {
        int idx = (addr - CN23XX_VF_SLI_IQ_SIZE_START) / 8;
        s->iq_size[idx] = val;
    } else if (addr >= CN23XX_VF_SLI_IQ_INSTR_COUNT_START64 &&
               addr < CN23XX_VF_SLI_IQ_INSTR_COUNT_START64 + MAX_INPUT_QUEUES * 8) {
        int idx = (addr - CN23XX_VF_SLI_IQ_INSTR_COUNT_START64) / 8;
        s->iq_instr_count[idx] = val;
    } else if (addr >= CN23XX_VF_SLI_OQ_PKT_CONTROL_START &&
               addr < CN23XX_VF_SLI_OQ_PKT_CONTROL_START + MAX_OUTPUT_QUEUES * 8) {
        int idx = (addr - CN23XX_VF_SLI_OQ_PKT_CONTROL_START) / 8;
        s->oq_pkt_control[idx] = val;
    } else if (addr >= CN23XX_VF_SLI_OQ_BASE_ADDR_START64 &&
               addr < CN23XX_VF_SLI_OQ_BASE_ADDR_START64 + MAX_OUTPUT_QUEUES * 8) {
        int idx = (addr - CN23XX_VF_SLI_OQ_BASE_ADDR_START64) / 8;
        s->oq_base_addr[idx] = val;
    } else if (addr >= CN23XX_VF_SLI_OQ_SIZE_START &&
               addr < CN23XX_VF_SLI_OQ_SIZE_START + MAX_OUTPUT_QUEUES * 8) {
        int idx = (addr - CN23XX_VF_SLI_OQ_SIZE_START) / 8;
        s->oq_size[idx] = val;
    } else if (addr >= CN23XX_VF_SLI_OQ_PKT_SENT_START &&
               addr < CN23XX_VF_SLI_OQ_PKT_SENT_START + MAX_OUTPUT_QUEUES * 8) {
        int idx = (addr - CN23XX_VF_SLI_OQ_PKT_SENT_START) / 8;
        s->oq_pkt_sent[idx] = val; /* driver writes to clear interrupt bits; simple overwrite for now */
    } else if (addr >= CN23XX_VF_SLI_OQ_PKT_CREDITS_START &&
               addr < CN23XX_VF_SLI_OQ_PKT_CREDITS_START + MAX_OUTPUT_QUEUES * 8) {
        int idx = (addr - CN23XX_VF_SLI_OQ_PKT_CREDITS_START) / 8;
        s->oq_pkt_credits[idx] = val;
        /* Credits write: could enable OQ processing */
    } else if (addr >= CN23XX_VF_SLI_OQ0_BUFF_INFO_SIZE &&
               addr < CN23XX_VF_SLI_OQ0_BUFF_INFO_SIZE + MAX_OUTPUT_QUEUES * 8) {
        int idx = (addr - CN23XX_VF_SLI_OQ0_BUFF_INFO_SIZE) / 8;
        s->oq_buff_info_size[idx] = val;
    } else if (addr >= CN23XX_VF_SLI_OQ_PKT_INT_LEVELS_START64 &&
               addr < CN23XX_VF_SLI_OQ_PKT_INT_LEVELS_START64 + MAX_OUTPUT_QUEUES * 8) {
        int idx = (addr - CN23XX_VF_SLI_OQ_PKT_INT_LEVELS_START64) / 8;
        s->oq_pkt_int_levels[idx] = val;
    } else if (addr >= CN23XX_VF_SLI_PKT_MBOX_INT_START &&
               addr < CN23XX_VF_SLI_PKT_MBOX_INT_START + 8) {
        /* Write-1-to-clear interrupt bits */
        s->pkt_mbox_int &= ~val;
    } else if (addr >= CN23XX_SLI_MBOX_OFFSET && addr < CN23XX_SLI_MBOX_OFFSET + 8) {
        s->mbox_data = val;
        /* PF emulation: after VF writes a mailbox command, simulate response */
        timer_mod(&s->mbox_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 10);
    } else {
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented write to 0x%lx val 0x%lx size %d\n",
                      __func__, addr, val, size);
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    int i;

    pci_device_reset(PCI_DEVICE(dev));
    memset(s->iq_pkt_control, 0, sizeof(s->iq_pkt_control));
    memset(s->iq_size, 0, sizeof(s->iq_size));
    memset(s->iq_instr_count, 0, sizeof(s->iq_instr_count));
    memset(s->iq_doorbell, 0, sizeof(s->iq_doorbell));
    memset(s->iq_base_addr, 0, sizeof(s->iq_base_addr));
    memset(s->oq_pkt_control, 0, sizeof(s->oq_pkt_control));
    memset(s->oq_base_addr, 0, sizeof(s->oq_base_addr));
    memset(s->oq_size, 0, sizeof(s->oq_size));
    memset(s->oq_pkt_sent, 0, sizeof(s->oq_pkt_sent));
    memset(s->oq_pkt_credits, 0, sizeof(s->oq_pkt_credits));
    memset(s->oq_buff_info_size, 0, sizeof(s->oq_buff_info_size));
    memset(s->oq_pkt_int_levels, 0, sizeof(s->oq_pkt_int_levels));
    s->pkt_mbox_int = 0;
    s->mbox_data = 0;
    s->int_status = 0;
    s->droq_intr = 0;
    s->device_state = OCT_DEV_BEGIN_STATE;
    s->in_reset = false;
    s->iq_enable = 0;
    s->oq_enable = 0;
    s->rings_per_vf = 1;
    s->pf_num = 0;
    s->vf_num = 0;

    /* Initialize IQ_PKT_CONTROL registers with default values
     * - RST = 1, QUIET = 1 (so that while loop in octeon_set_io_queues_off exits immediately)
     * - Rings per VF = 1, PF number = 0, VF number = 0
     */
    for (i = 0; i < MAX_INPUT_QUEUES; i++) {
        s->iq_pkt_control[i] = CN23XX_PKT_INPUT_CTL_RST |
                               CN23XX_PKT_INPUT_CTL_QUIET |
                               ((uint64_t)s->rings_per_vf << CN23XX_PKT_INPUT_CTL_RPVF_POS) |
                               ((uint64_t)s->pf_num << CN23XX_PKT_INPUT_CTL_PF_NUM_POS) |
                               ((uint64_t)s->vf_num << CN23XX_PKT_INPUT_CTL_VF_NUM_POS);
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
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_mbox_response(void *opaque)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Simulate PF response: write version info to mailbox data,
     * set interrupt bit in pkt_mbox_int and OQ_PKT_SENT for vector 0,
     * then trigger MSI-X vector 0.
     */
    s->mbox_data = (LIQUIDIO_BASE_MAJOR_VERSION << 16) |
                   (LIQUIDIO_BASE_MINOR_VERSION << 8) |
                   LIQUIDIO_BASE_MICRO_VERSION;

    /* Set mailbox interrupt pending bit in pkt_mbox_int (placeholder value) */
    s->pkt_mbox_int |= (1 << CN23XX_PKT_MBOX_INT_BIT);

    /* Set MBOX_INT bit in OQ_PKT_SENT[0] (placeholder value 0, no effect) */
    s->oq_pkt_sent[0] |= CN23XX_INTR_MBOX_INT;

    /* Raise interrupt if MSI-X enabled */
    if (msix_enabled(pdev)) {
        msix_notify(pdev, 0);  /* vector 0 used for mailbox */
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = VF_BAR0_SIZE, .name = "vf-mmio" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = VF_MSIX_BAR_SIZE, .name = "vf-msix" };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    msix_init(pdev, 16, &s->bar_regions[1], 1, 0, &s->bar_regions[1], 1, 0, 0, errp);

    s->device_state = OCT_DEV_BEGIN_STATE;
    s->in_reset = false;

    timer_init_ms(&s->mbox_timer, QEMU_CLOCK_VIRTUAL, pcibase_mbox_response, s);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    timer_del(&s->mbox_timer);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "LiquidIO_VF_pci",
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
