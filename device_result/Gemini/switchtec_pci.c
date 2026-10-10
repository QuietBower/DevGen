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
#include "qemu/bitops.h"

#define TYPE_PCIBASE_DEVICE "switchtec_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#ifndef PCI_VENDOR_ID_MICROSEMI
#define PCI_VENDOR_ID_MICROSEMI 0x11f8
#endif
#ifndef PCI_CLASS_MEMORY_OTHER
#define PCI_CLASS_MEMORY_OTHER 0x0580
#endif

#define SWITCHTEC_MRPC_PAYLOAD_SIZE 1024

#define SWITCHTEC_EVENT_CLEAR    (1 << 0)
#define SWITCHTEC_EVENT_EN_IRQ   (1 << 3)
#define SWITCHTEC_MAX_PFF_CSR    255

#define SWITCHTEC_DMA_MRPC_EN    (1 << 0)
#define SWITCHTEC_EVENT_OCCURRED (1 << 0)
#define SWITCHTEC_EVENT_NOT_SUPP (1U << 31)

enum switchtec_gen {
    SWITCHTEC_GEN3,
    SWITCHTEC_GEN4,
    SWITCHTEC_GEN5,
};

struct sys_info_regs_gen3 {
    uint32_t reserved1;
    uint32_t vendor_table_revision;
    uint32_t table_format_version;
    uint32_t partition_id;
    uint32_t cfg_file_fmt_version;
    uint16_t cfg_running;
    uint16_t img_running;
    uint32_t reserved2[57];
    char vendor_id[8];
    char product_id[16];
    char product_revision[4];
    char component_vendor[8];
    uint16_t component_id;
    uint8_t component_revision;
};

struct sys_info_regs_gen4 {
    uint16_t gas_layout_ver;
    uint8_t evlist_ver;
    uint8_t reserved1;
    uint16_t mgmt_cmd_set_ver;
    uint16_t fabric_cmd_set_ver;
    uint32_t reserved2[2];
    uint8_t mrpc_uart_ver;
    uint8_t mrpc_twi_ver;
    uint8_t mrpc_eth_ver;
    uint8_t mrpc_inband_ver;
    uint32_t reserved3[7];
    uint32_t fw_update_tmo;
    uint32_t xml_version_cfg;
    uint32_t xml_version_img;
    uint32_t partition_id;
    uint16_t bl2_running;
    uint16_t cfg_running;
    uint16_t img_running;
    uint16_t key_running;
    uint32_t reserved4[43];
    uint32_t vendor_seeprom_twi;
    uint32_t vendor_table_revision;
    uint32_t vendor_specific_info[2];
    uint16_t p2p_vendor_id;
    uint16_t p2p_device_id;
    uint8_t p2p_revision_id;
    uint8_t reserved5[3];
    uint32_t p2p_class_id;
    uint16_t subsystem_vendor_id;
    uint16_t subsystem_id;
    uint32_t p2p_serial_number[2];
    uint8_t mac_addr[6];
    uint8_t reserved6[2];
    uint32_t reserved7[3];
    char vendor_id[8];
    char product_id[24];
    char product_revision[2];
    uint16_t reserved8;
};

struct sys_info_regs {
    uint32_t device_id;
    uint32_t device_version;
    uint32_t firmware_version;
    union {
        struct sys_info_regs_gen3 gen3;
        struct sys_info_regs_gen4 gen4;
    };
};

struct mrpc_regs {
    uint8_t input_data[SWITCHTEC_MRPC_PAYLOAD_SIZE];
    uint8_t output_data[SWITCHTEC_MRPC_PAYLOAD_SIZE];
    uint32_t cmd;
    uint32_t status;
    uint32_t ret_value;
    uint32_t dma_en;
    uint64_t dma_addr;
    uint32_t dma_vector;
    uint32_t dma_ver;
};

struct nt_partition_info {
    uint32_t xlink_enabled;
    uint32_t target_part_low;
    uint32_t target_part_high;
    uint32_t reserved;
};

struct ntb_info_regs {
    uint8_t partition_count;
    uint8_t partition_id;
    uint16_t reserved1;
    uint64_t ep_map;
    uint16_t requester_id;
    uint16_t reserved2;
    uint32_t reserved3[4];
    struct nt_partition_info ntp_info[48];
};

struct dma_mrpc_output {
    uint32_t status;
    uint32_t cmd_id;
    uint32_t rtn_code;
    uint32_t output_size;
    uint8_t data[SWITCHTEC_MRPC_PAYLOAD_SIZE];
};

struct sw_event_regs {
    uint64_t event_report_ctrl;
    uint64_t reserved1;
    uint64_t part_event_bitmap;
    uint64_t reserved2;
    uint32_t global_summary;
    uint32_t reserved3[3];
    uint32_t stack_error_event_hdr;
    uint32_t stack_error_event_data;
    uint32_t reserved4[4];
    uint32_t ppu_error_event_hdr;
    uint32_t ppu_error_event_data;
    uint32_t reserved5[4];
    uint32_t isp_error_event_hdr;
    uint32_t isp_error_event_data;
    uint32_t reserved6[4];
    uint32_t sys_reset_event_hdr;
    uint32_t reserved7[5];
    uint32_t fw_exception_hdr;
    uint32_t reserved8[5];
    uint32_t fw_nmi_hdr;
    uint32_t reserved9[5];
    uint32_t fw_non_fatal_hdr;
    uint32_t reserved10[5];
    uint32_t fw_fatal_hdr;
    uint32_t reserved11[5];
    uint32_t twi_mrpc_comp_hdr;
    uint32_t twi_mrpc_comp_data;
    uint32_t reserved12[4];
    uint32_t twi_mrpc_comp_async_hdr;
    uint32_t twi_mrpc_comp_async_data;
    uint32_t reserved13[4];
    uint32_t cli_mrpc_comp_hdr;
    uint32_t cli_mrpc_comp_data;
    uint32_t reserved14[4];
    uint32_t cli_mrpc_comp_async_hdr;
    uint32_t cli_mrpc_comp_async_data;
    uint32_t reserved15[4];
    uint32_t gpio_interrupt_hdr;
    uint32_t gpio_interrupt_data;
    uint32_t reserved16[4];
    uint32_t gfms_event_hdr;
    uint32_t gfms_event_data;
    uint32_t reserved17[4];
};

struct part_cfg_regs {
    uint32_t status;
    uint32_t state;
    uint32_t port_cnt;
    uint32_t usp_port_mode;
    uint32_t usp_pff_inst_id;
    uint32_t vep_pff_inst_id;
    uint32_t dsp_pff_inst_id[47];
    uint32_t reserved1[11];
    uint16_t vep_vector_number;
    uint16_t usp_vector_number;
    uint32_t port_event_bitmap;
    uint32_t reserved2[3];
    uint32_t part_event_summary;
    uint32_t reserved3[3];
    uint32_t part_reset_hdr;
    uint32_t part_reset_data[5];
    uint32_t mrpc_comp_hdr;
    uint32_t mrpc_comp_data[5];
    uint32_t mrpc_comp_async_hdr;
    uint32_t mrpc_comp_async_data[5];
    uint32_t dyn_binding_hdr;
    uint32_t dyn_binding_data[5];
    uint32_t intercomm_notify_hdr;
    uint32_t intercomm_notify_data[5];
    uint32_t reserved4[153];
};

struct pff_csr_regs {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t pcicmd;
    uint16_t pcists;
    uint32_t pci_class;
    uint32_t pci_opts;
    union {
        uint32_t pci_bar[6];
        uint64_t pci_bar64[3];
    };
    uint32_t pci_cardbus;
    uint32_t pci_subsystem_id;
    uint32_t pci_expansion_rom;
    uint32_t pci_cap_ptr;
    uint32_t reserved1;
    uint32_t pci_irq;
    uint32_t pci_cap_region[48];
    uint32_t pcie_cap_region[448];
    uint32_t indirect_gas_window[128];
    uint32_t indirect_gas_window_off;
    uint32_t reserved[127];
    uint32_t pff_event_summary;
    uint32_t reserved2[3];
    uint32_t aer_in_p2p_hdr;
    uint32_t aer_in_p2p_data[5];
    uint32_t aer_in_vep_hdr;
    uint32_t aer_in_vep_data[5];
    uint32_t dpc_hdr;
    uint32_t dpc_data[5];
    uint32_t cts_hdr;
    uint32_t cts_data[5];
    uint32_t uec_hdr;
    uint32_t uec_data[5];
    uint32_t hotplug_hdr;
    uint32_t hotplug_data[5];
    uint32_t ier_hdr;
    uint32_t ier_data[5];
    uint32_t threshold_hdr;
    uint32_t threshold_data[5];
    uint32_t power_mgmt_hdr;
    uint32_t power_mgmt_data[5];
    uint32_t tlp_throttling_hdr;
    uint32_t tlp_throttling_data[5];
    uint32_t force_speed_hdr;
    uint32_t force_speed_data[5];
    uint32_t credit_timeout_hdr;
    uint32_t credit_timeout_data[5];
    uint32_t link_state_hdr;
    uint32_t link_state_data[5];
    uint32_t reserved4[174];
};

struct ntb_dbmsg_regs {
    uint32_t reserved1[1024];
    uint64_t odb;
    uint64_t odb_mask;
    uint64_t idb;
    uint64_t idb_mask;
    uint8_t  idb_vec_map[64];
    uint32_t msg_map;
    uint32_t reserved2;
    struct {
        uint32_t msg;
        uint32_t status;
    } omsg[4];

    struct {
        uint32_t msg;
        uint8_t  status;
        uint8_t  mask;
        uint8_t  src;
        uint8_t  reserved;
    } imsg[4];

    uint8_t reserved3[3928];
    uint8_t msix_table[1024];
    uint8_t reserved4[3072];
    uint8_t pba[24];
    uint8_t reserved5[4072];
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
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->has_msix) {
        msix_notify(pdev, 0);
    } else if (s->has_msi) {
        msi_notify(pdev, 0);
    } else {
        pci_set_irq(pdev, 1);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* 
     * Cannot implement specific register reads without the GAS offsets.
     * Returning 0 to allow compilation and basic execution until offsets are provided.
     */

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* 
     * Cannot implement specific register writes without the GAS offsets.
     */
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
    pci_device_reset(PCI_DEVICE(dev));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_MICROSEMI);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x8531);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MEMORY_OTHER);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize BAR 0 for MMIO */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000000; /* 16 MB to cover all offsets */
    s->bar_info[0].name = "switchtec-mmio";

    /* BAR Initialization */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize MSI/MSI-X */
    if (msix_init(pdev, 4, &s->bar_regions[0], 0, 0x800000, &s->bar_regions[0], 0, 0x801000, 0, errp) == 0) {
        s->has_msix = true;
    } else if (msi_init(pdev, 0, 4, true, false, errp) == 0) {
        s->has_msi = true;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->bar_regions[0], &s->bar_regions[0]);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "switchtec_pci",
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
