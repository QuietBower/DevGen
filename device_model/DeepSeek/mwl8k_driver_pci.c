/* Complete QEMU PCI device model for mwl8k driver. */
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

#define TYPE_PCIBASE_DEVICE "mwl8k_driver_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs from driver */
#define PCI_VENDOR_ID_MARVELL          0x11ab
#define MWL8K_DEVICE_ID                0x2a0a
#define MWL8K_CLASS_ID                 0x0280

/* Register offsets from mwl8k.h */
#define MWL8K_HIU_GEN_PTR                   0x00000c10
#define MWL8K_HIU_INT_CODE                  0x00000c14
#define MWL8K_HIU_SCRATCH                   0x00000c40
#define MWL8K_HIU_H2A_INTERRUPT_EVENTS      0x00000c18
#define MWL8K_HIU_H2A_INTERRUPT_STATUS      0x00000c1c
#define MWL8K_HIU_H2A_INTERRUPT_MASK        0x00000c20
#define MWL8K_HIU_H2A_INTERRUPT_CLEAR_SEL   0x00000c24
#define MWL8K_HIU_H2A_INTERRUPT_STATUS_MASK 0x00000c28
#define MWL8K_HIU_A2H_INTERRUPT_EVENTS      0x00000c2c
#define MWL8K_HIU_A2H_INTERRUPT_STATUS      0x00000c30
#define MWL8K_HIU_A2H_INTERRUPT_MASK        0x00000c34
#define MWL8K_HIU_A2H_INTERRUPT_CLEAR_SEL   0x00000c38
#define MWL8K_HIU_A2H_INTERRUPT_STATUS_MASK 0x00000c3c
#define MWL8K_HW_TIMER_REGISTER             0x0000a600
#define BBU_RXRDY_CNT_REG                   0x0000a860
#define NOK_CCA_CNT_REG                     0x0000a6a0

/* Interrupt bits */
#define MWL8K_H2A_INT_DUMMY          (1 << 20)
#define MWL8K_H2A_INT_RESET          (1 << 15)
#define MWL8K_H2A_INT_DOORBELL       (1 << 1)
#define MWL8K_H2A_INT_PPA_READY      (1 << 0)
#define MWL8K_A2H_INT_DUMMY          (1 << 20)
#define MWL8K_A2H_INT_BA_WATCHDOG    (1 << 14)
#define MWL8K_A2H_INT_CHNL_SWITCHED  (1 << 11)
#define MWL8K_A2H_INT_QUEUE_EMPTY    (1 << 10)
#define MWL8K_A2H_INT_RADAR_DETECT   (1 << 7)
#define MWL8K_A2H_INT_RADIO_ON       (1 << 6)
#define MWL8K_A2H_INT_RADIO_OFF      (1 << 5)
#define MWL8K_A2H_INT_MAC_EVENT      (1 << 3)
#define MWL8K_A2H_INT_OPC_DONE       (1 << 2)
#define MWL8K_A2H_INT_RX_READY       (1 << 1)
#define MWL8K_A2H_INT_TX_DONE        (1 << 0)

#define MWL8K_A2H_EVENTS  (MWL8K_A2H_INT_DUMMY | \
                           MWL8K_A2H_INT_CHNL_SWITCHED | \
                           MWL8K_A2H_INT_QUEUE_EMPTY | \
                           MWL8K_A2H_INT_RADAR_DETECT | \
                           MWL8K_A2H_INT_RADIO_ON | \
                           MWL8K_A2H_INT_RADIO_OFF | \
                           MWL8K_A2H_INT_MAC_EVENT | \
                           MWL8K_A2H_INT_OPC_DONE | \
                           MWL8K_A2H_INT_RX_READY | \
                           MWL8K_A2H_INT_TX_DONE | \
                           MWL8K_A2H_INT_BA_WATCHDOG)

/* Mode values */
#define MWL8K_MODE_STA   0x5a
#define MWL8K_MODE_AP    0xa5

/* Firmware ready signatures */
#define MWL8K_FWSTA_READY  0xf0f1f2f4
#define MWL8K_FWAP_READY   0xf1f2f4a5

/* BAR sizes */
#define MWL8K_BAR0_SIZE  0x10000  /* SRAM */
#define MWL8K_BAR1_SIZE  0x10000  /* Registers */

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows */
    uint32_t hiu_gen_ptr;
    uint32_t hiu_int_code;
    uint32_t hiu_scratch;
    uint32_t hiu_h2a_interrupt_events;
    uint32_t hiu_h2a_interrupt_status;
    uint32_t hiu_h2a_interrupt_mask;
    uint32_t hiu_h2a_interrupt_clear_sel;
    uint32_t hiu_h2a_interrupt_status_mask;
    uint32_t hiu_a2h_interrupt_events;
    uint32_t hiu_a2h_interrupt_status;
    uint32_t hiu_a2h_interrupt_mask;
    uint32_t hiu_a2h_interrupt_clear_sel;
    uint32_t hiu_a2h_interrupt_status_mask;
    uint32_t hw_timer_register;
    uint32_t bbu_rxrdy_cnt_reg;
    uint32_t nok_cca_cnt_reg;

    uint32_t status;

    dma_addr_t tx_desc_base[12];
    dma_addr_t rx_desc_base[1];
};

/* MMIO/PIO Handlers */
static uint64_t pcibase_regs_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        return ~0ULL;
    }

    switch (addr) {
    case MWL8K_HIU_GEN_PTR:
        val = s->hiu_gen_ptr;
        break;
    case MWL8K_HIU_INT_CODE:
        val = s->hiu_int_code;
        break;
    case MWL8K_HIU_SCRATCH:
        val = s->hiu_scratch;
        break;
    case MWL8K_HIU_H2A_INTERRUPT_EVENTS:
        val = s->hiu_h2a_interrupt_events;
        break;
    case MWL8K_HIU_H2A_INTERRUPT_STATUS:
        val = s->hiu_h2a_interrupt_status;
        break;
    case MWL8K_HIU_H2A_INTERRUPT_MASK:
        val = s->hiu_h2a_interrupt_mask;
        break;
    case MWL8K_HIU_H2A_INTERRUPT_CLEAR_SEL:
        val = s->hiu_h2a_interrupt_clear_sel;
        break;
    case MWL8K_HIU_H2A_INTERRUPT_STATUS_MASK:
        val = s->hiu_h2a_interrupt_status_mask;
        break;
    case MWL8K_HIU_A2H_INTERRUPT_EVENTS:
        val = s->hiu_a2h_interrupt_events;
        break;
    case MWL8K_HIU_A2H_INTERRUPT_STATUS:
        val = s->hiu_a2h_interrupt_status;
        break;
    case MWL8K_HIU_A2H_INTERRUPT_MASK:
        val = s->hiu_a2h_interrupt_mask;
        break;
    case MWL8K_HIU_A2H_INTERRUPT_CLEAR_SEL:
        val = s->hiu_a2h_interrupt_clear_sel;
        break;
    case MWL8K_HIU_A2H_INTERRUPT_STATUS_MASK:
        val = s->hiu_a2h_interrupt_status_mask;
        break;
    case MWL8K_HW_TIMER_REGISTER:
        val = s->hw_timer_register;
        break;
    case BBU_RXRDY_CNT_REG:
        val = s->bbu_rxrdy_cnt_reg;
        break;
    case NOK_CCA_CNT_REG:
        val = s->nok_cca_cnt_reg;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "mwl8k_regs_read: unknown offset 0x%"PRIx64"\n", addr);
        val = 0;
        break;
    }

    return val;
}

static void pcibase_regs_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case MWL8K_HIU_GEN_PTR:
        s->hiu_gen_ptr = val;
        break;
    case MWL8K_HIU_INT_CODE:
        s->hiu_int_code = val;
        break;
    case MWL8K_HIU_SCRATCH:
        s->hiu_scratch = val;
        break;
    case MWL8K_HIU_H2A_INTERRUPT_EVENTS:
        s->hiu_h2a_interrupt_events = val;
        break;
    case MWL8K_HIU_H2A_INTERRUPT_STATUS:
        s->hiu_h2a_interrupt_status = val;
        break;
    case MWL8K_HIU_H2A_INTERRUPT_MASK:
        s->hiu_h2a_interrupt_mask = val;
        break;
    case MWL8K_HIU_H2A_INTERRUPT_CLEAR_SEL:
        s->hiu_h2a_interrupt_clear_sel = val;
        break;
    case MWL8K_HIU_H2A_INTERRUPT_STATUS_MASK:
        s->hiu_h2a_interrupt_status_mask = val;
        break;
    case MWL8K_HIU_A2H_INTERRUPT_EVENTS:
        s->hiu_a2h_interrupt_events = val;
        break;
    case MWL8K_HIU_A2H_INTERRUPT_STATUS:
        s->hiu_a2h_interrupt_status = val;
        break;
    case MWL8K_HIU_A2H_INTERRUPT_MASK:
        s->hiu_a2h_interrupt_mask = val;
        break;
    case MWL8K_HIU_A2H_INTERRUPT_CLEAR_SEL:
        s->hiu_a2h_interrupt_clear_sel = val;
        break;
    case MWL8K_HIU_A2H_INTERRUPT_STATUS_MASK:
        s->hiu_a2h_interrupt_status_mask = val;
        break;
    case MWL8K_HW_TIMER_REGISTER:
        s->hw_timer_register = val;
        break;
    case BBU_RXRDY_CNT_REG:
        s->bbu_rxrdy_cnt_reg = val;
        break;
    case NOK_CCA_CNT_REG:
        s->nok_cca_cnt_reg = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "mwl8k_regs_write: unknown offset 0x%"PRIx64" val=0x%"PRIx64"\n", addr, val);
        break;
    }
}

static const MemoryRegionOps pcibase_regs_ops = {
    .read = pcibase_regs_read,
    .write = pcibase_regs_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

/* SRAM is just a RAM region, no special ops */
static const MemoryRegionOps pcibase_sram_ops; /* unused, RAM region created directly */

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    // Reset all register shadows
    memset(&s->hiu_gen_ptr, 0, sizeof(*s) - offsetof(PCIBaseState, hiu_gen_ptr));
    // Set firmware ready signature to emulate preloaded firmware
    s->hiu_scratch = MWL8K_FWSTA_READY;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_regs_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_MARVELL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, MWL8K_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, MWL8K_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_RAM, .size = MWL8K_BAR0_SIZE, .name = "mwl8k-sram" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = MWL8K_BAR1_SIZE, .name = "mwl8k-regs" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    // No special cleanup needed
}

static const VMStateDescription vmstate_pcibase = {
    .name = "mwl8k_driver_pci",
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
