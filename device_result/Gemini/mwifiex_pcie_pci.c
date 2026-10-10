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
#include "qemu/bitops.h"
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


#define TYPE_PCIBASE_DEVICE "mwifiex_pcie_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIE_VENDOR_ID_MARVELL 0x11ab
#define PCIE_DEVICE_ID_MARVELL_88W8766P 0x2b30

#define PCIE_HOST_INT_STATUS_MASK 0xC3C
#define PCIE_HOST_INT_STATUS 0xC30
#define PCIE_CPU_INT_STATUS 0xC1C
#define PCIE_CPU_INT_EVENT 0xC18
#define PCIE_HOST_INT_MASK 0xC34

#define PCIE_SCRATCH_0_REG 0xC10
#define PCIE_SCRATCH_1_REG 0xC14
#define PCIE_SCRATCH_2_REG 0xC40
#define PCIE_SCRATCH_3_REG 0xC44
#define PCIE_SCRATCH_4_REG 0xCD0
#define PCIE_SCRATCH_5_REG 0xCD4
#define PCIE_SCRATCH_6_REG 0xCD8
#define PCIE_SCRATCH_7_REG 0xCDC
#define PCIE_SCRATCH_8_REG 0xCE0
#define PCIE_SCRATCH_9_REG 0xCE4
#define PCIE_SCRATCH_10_REG 0xCE8
#define PCIE_SCRATCH_11_REG 0xCEC
#define PCIE_SCRATCH_12_REG 0xCF0
#define PCIE_SCRATCH_13_REG 0xCF4
#define PCIE_SCRATCH_14_REG 0xCF8
#define PCIE_SCRATCH_15_REG 0xCFC

#define PCIE_WR_DATA_PTR_Q0_Q1 0xC05C
#define PCIE_RD_DATA_PTR_Q0_Q1 0xC08C

#define MWIFIEX_NUM_MSIX_VECTORS 4

#define HOST_INTR_DNLD_DONE BIT(0)
#define HOST_INTR_UPLD_RDY  BIT(1)
#define HOST_INTR_CMD_DONE  BIT(2)
#define HOST_INTR_EVENT_RDY BIT(3)

#define MWIFIEX_MAX_TXRX_BD 0x20
#define MWIFIEX_MAX_EVT_BD  0x08

#define HOST_INTR_MASK (HOST_INTR_DNLD_DONE | \
                        HOST_INTR_UPLD_RDY  | \
                        HOST_INTR_CMD_DONE  | \
                        HOST_INTR_EVENT_RDY)

#define FIRMWARE_READY_PCIE 0xfedcba00
#define CPU_INTR_DNLD_RDY BIT(0)
#define CPU_INTR_DOOR_BELL BIT(1)
#define CPU_INTR_SLEEP_CFM_DONE BIT(2)
#define CPU_INTR_EVENT_DONE BIT(5)

struct mwifiex_pcie_card_reg {
    uint16_t cmd_addr_lo;
    uint16_t cmd_addr_hi;
    uint16_t fw_status;
    uint16_t cmd_size;
    uint16_t cmdrsp_addr_lo;
    uint16_t cmdrsp_addr_hi;
    uint16_t tx_rdptr;
    uint16_t tx_wrptr;
    uint16_t rx_rdptr;
    uint16_t rx_wrptr;
    uint16_t evt_rdptr;
    uint16_t evt_wrptr;
    uint16_t drv_rdy;
    uint16_t tx_start_ptr;
    uint32_t tx_mask;
    uint32_t tx_wrap_mask;
    uint32_t rx_mask;
    uint32_t rx_wrap_mask;
    uint32_t tx_rollover_ind;
    uint32_t rx_rollover_ind;
    uint32_t evt_rollover_ind;
    uint8_t ring_flag_sop;
    uint8_t ring_flag_eop;
    uint8_t ring_flag_xs_sop;
    uint8_t ring_flag_xs_eop;
    uint32_t ring_tx_start_ptr;
    uint8_t pfu_enabled;
    uint8_t sleep_cookie;
    uint16_t fw_dump_ctrl;
    uint16_t fw_dump_start;
    uint16_t fw_dump_end;
    uint8_t fw_dump_host_ready;
    uint8_t fw_dump_read_done;
    uint8_t msix_support;
};

struct mwifiex_pcie_buf_desc {
    uint64_t paddr;
    uint16_t len;
    uint16_t flags;
};

struct mwifiex_evt_buf_desc {
    uint64_t paddr;
    uint16_t len;
    uint16_t flags;
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
    uint32_t host_int_status;
    uint32_t host_int_mask;
    uint32_t cpu_int_status;
    uint32_t cpu_int_event;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t scratch[16];
    uint32_t wr_data_ptr_q0_q1;
    uint32_t rd_data_ptr_q0_q1;

    /* DMA Context */
    uint64_t txbd_ring_pbase;
    uint64_t rxbd_ring_pbase;
    uint64_t evtbd_ring_pbase;
    uint64_t sleep_cookie_pbase;
    uint32_t txbd_ring_size;
    uint32_t rxbd_ring_size;
    uint32_t evtbd_ring_size;

    uint32_t fw_status;
};



/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->host_int_status & s->host_int_mask) != 0;
    
    if (msix_enabled(pdev)) {
        if (level) msix_notify(pdev, 0);
    } else if (msi_enabled(pdev)) {
        if (level) msi_notify(pdev, 0);
    } else {
        pci_set_irq(pdev, level);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint64_t cmd_addr = ((uint64_t)s->scratch[1] << 32) | s->scratch[0];
    uint32_t cmd_size = s->scratch[2];
    
    if (cmd_addr != 0 && cmd_size > 0) {
        uint8_t buf[4];
        uint32_t len = cmd_size > sizeof(buf) ? sizeof(buf) : cmd_size;
        pci_dma_read(pdev, cmd_addr, buf, len);
    }
}

static int get_scratch_idx(hwaddr addr) {
    switch(addr) {
        case PCIE_SCRATCH_0_REG: return 0;
        case PCIE_SCRATCH_1_REG: return 1;
        case PCIE_SCRATCH_2_REG: return 2;
        case PCIE_SCRATCH_3_REG: return 3;
        case PCIE_SCRATCH_4_REG: return 4;
        case PCIE_SCRATCH_5_REG: return 5;
        case PCIE_SCRATCH_6_REG: return 6;
        case PCIE_SCRATCH_7_REG: return 7;
        case PCIE_SCRATCH_8_REG: return 8;
        case PCIE_SCRATCH_9_REG: return 9;
        case PCIE_SCRATCH_10_REG: return 10;
        case PCIE_SCRATCH_11_REG: return 11;
        case PCIE_SCRATCH_12_REG: return 12;
        case PCIE_SCRATCH_13_REG: return 13;
        case PCIE_SCRATCH_14_REG: return 14;
        case PCIE_SCRATCH_15_REG: return 15;
        default: return -1;
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    int idx = get_scratch_idx(addr);

    if (idx >= 0) {
        return s->scratch[idx];
    }

    switch (addr) {
    case PCIE_HOST_INT_STATUS:
        val = s->host_int_status;
        break;
    case PCIE_HOST_INT_MASK:
        val = s->host_int_mask;
        break;
    case PCIE_CPU_INT_STATUS:
        val = s->cpu_int_status;
        break;
    case PCIE_CPU_INT_EVENT:
        val = s->cpu_int_event;
        break;
    case PCIE_WR_DATA_PTR_Q0_Q1:
        val = s->wr_data_ptr_q0_q1;
        break;
    case PCIE_RD_DATA_PTR_Q0_Q1:
        val = s->rd_data_ptr_q0_q1;
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int idx = get_scratch_idx(addr);

    if (idx >= 0) {
        s->scratch[idx] = val;
        if (addr == PCIE_SCRATCH_12_REG && val == FIRMWARE_READY_PCIE) {
            s->scratch[3] = FIRMWARE_READY_PCIE;
        }
        return;
    }

    switch (addr) {
    case PCIE_HOST_INT_STATUS:
        s->host_int_status &= val;
        pcibase_update_irq(s);
        break;
    case PCIE_HOST_INT_MASK:
        s->host_int_mask = val;
        pcibase_update_irq(s);
        break;
    case PCIE_CPU_INT_EVENT:
        s->cpu_int_event = val;
        if (val & CPU_INTR_DOOR_BELL) {
            pcibase_do_dma(s, false);
            s->host_int_status |= HOST_INTR_CMD_DONE;
            pcibase_update_irq(s);
        }
        if (val & CPU_INTR_DNLD_RDY) {
            s->scratch[6] = s->scratch[7]; /* tx_rdptr = tx_wrptr */
            s->host_int_status |= HOST_INTR_DNLD_DONE;
            pcibase_update_irq(s);
        }
        break;
    case PCIE_WR_DATA_PTR_Q0_Q1:
        s->wr_data_ptr_q0_q1 = val;
        break;
    case PCIE_RD_DATA_PTR_Q0_Q1:
        s->rd_data_ptr_q0_q1 = val;
        break;
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

    s->host_int_status = 0;
    s->host_int_mask = 0;
    s->cpu_int_status = 0;
    s->cpu_int_event = 0;
    memset(s->scratch, 0, sizeof(s->scratch));
    s->scratch[2] = 2048; /* cmd_size initial value to pass polling */
    s->wr_data_ptr_q0_q1 = 0;
    s->rd_data_ptr_q0_q1 = 0;
    s->fw_status = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIE_VENDOR_ID_MARVELL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIE_DEVICE_ID_MARVELL_88W8766P );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0280 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_MMIO, 0x1000, "bar0"};
    s->bar_info[1] = (BARInfo){1, BAR_TYPE_NONE, 0, ""};
    s->bar_info[2] = (BARInfo){2, BAR_TYPE_MMIO, 0x10000, "bar2"};
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    msi_init(pdev, 0, 1, true, false, errp);
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
    .name = "mwifiex_pcie_pci",
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
