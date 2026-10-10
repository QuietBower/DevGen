#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"

#define TYPE_PCIBASE_DEVICE "isci_pci"

#define SCU_GEN_VALUE(name, value) (value)

#define SCU_GEN_BIT(name) \
	SCU_GEN_VALUE(name, ((uint32_t)1))

struct scu_transport_layer_registers {
	/* 0x0000 TLCR */
	uint32_t control;
	/* 0x0004 TLADTR */
	uint32_t arbitration_delay_timer;
	/* 0x0008 TLTTMR */
	uint32_t timer_test_mode;
	/* 0x000C reserved */
	uint32_t reserved_0C;
	/* 0x0010 STPTLDARNI */
	uint32_t stp_rni;
	/* 0x0014 TLFEWPORCTRL */
	uint32_t tlfe_wpo_read_control;
	/* 0x0018 TLFEWPORDATA */
	uint32_t tlfe_wpo_read_data;
	/* 0x001C RXTLSSCSR1 */
	uint32_t rxtl_single_step_control_status_1;
	/* 0x0020 RXTLSSCSR2 */
	uint32_t rxtl_single_step_control_status_2;
	/* 0x0024 AWTRDDCR */
	uint32_t tlfe_awt_retry_delay_debug_control;
	/* Remainder of TL memory space */
	uint32_t reserved_0028_007F[0x16];
};

struct scu_link_layer_registers {
/* 0x0000 SAS_SPDTOV */
	uint32_t speed_negotiation_timers;
/* 0x0004 SAS_LLSTA */
	uint32_t link_layer_status;
/* 0x0008 SATA_PSELTOV */
	uint32_t port_selector_timeout;
	uint32_t reserved0C;
/* 0x0010 SAS_TIMETOV */
	uint32_t timeout_unit_value;
/* 0x0014 SAS_RCDTOV */
	uint32_t rcd_timeout;
/* 0x0018 SAS_LNKTOV */
	uint32_t link_timer_timeouts;
/* 0x001C SAS_PHYTOV */
	uint32_t sas_phy_timeouts;
/* 0x0020 SAS_AFERCNT */
	uint32_t received_address_frame_error_counter;
/* 0x0024 SAS_WERCNT */
	uint32_t invalid_dword_counter;
/* 0x0028 SAS_TIID */
	uint32_t transmit_identification;
/* 0x002C SAS_TIDNH */
	uint32_t sas_device_name_high;
/* 0x0030 SAS_TIDNL */
	uint32_t sas_device_name_low;
/* 0x0034 SAS_TISSAH */
	uint32_t source_sas_address_high;
/* 0x0038 SAS_TISSAL */
	uint32_t source_sas_address_low;
/* 0x003C SAS_TIPID */
	uint32_t identify_frame_phy_id;
/* 0x0040 SAS_TIRES2 */
	uint32_t identify_frame_reserved;
/* 0x0044 SAS_ADRSTA */
	uint32_t received_address_frame;
/* 0x0048 SAS_MAWTTOV */
	uint32_t maximum_arbitration_wait_timer_timeout;
/* 0x004C SAS_PTxC */
	uint32_t transmit_primitive;
/* 0x0050 SAS_RORES */
	uint32_t error_counter_event_notification_control;
/* 0x0054 SAS_FRPLDFIL */
	uint32_t frxq_payload_fill_threshold;
/* 0x0058 SAS_LLHANG_TOT */
	uint32_t link_layer_hang_detection_timeout;
	uint32_t reserved_5C;
/* 0x0060 SAS_RFCNT */
	uint32_t received_frame_count;
/* 0x0064 SAS_TFCNT */
	uint32_t transmit_frame_count;
/* 0x0068 SAS_RFDCNT */
	uint32_t received_dword_count;
/* 0x006C SAS_TFDCNT */
	uint32_t transmit_dword_count;
/* 0x0070 SAS_LERCNT */
	uint32_t loss_of_sync_error_count;
/* 0x0074 SAS_RDISERRCNT */
	uint32_t running_disparity_error_count;
/* 0x0078 SAS_CRERCNT */
	uint32_t received_frame_crc_error_count;
/* 0x007C STPCTL */
	uint32_t stp_control;
/* 0x0080 SAS_PCFG */
	uint32_t phy_configuration;
/* 0x0084 SAS_CLKSM */
	uint32_t clock_skew_management;
/* 0x0088 SAS_TXCOMWAKE */
	uint32_t transmit_comwake_signal;
/* 0x008C SAS_TXCOMINIT */
	uint32_t transmit_cominit_signal;
/* 0x0090 SAS_TXCOMSAS */
	uint32_t transmit_comsas_signal;
/* 0x0094 SAS_COMINIT */
	uint32_t cominit_control;
/* 0x0098 SAS_COMWAKE */
	uint32_t comwake_control;
/* 0x009C SAS_COMSAS */
	uint32_t comsas_control;
/* 0x00A0 SAS_SFERCNT */
	uint32_t received_short_frame_count;
/* 0x00A4 SAS_CDFERCNT */
	uint32_t received_frame_without_credit_count;
/* 0x00A8 SAS_DNFERCNT */
	uint32_t received_frame_after_done_count;
/* 0x00AC SAS_PRSTERCNT */
	uint32_t phy_reset_problem_count;
/* 0x00B0 SAS_CNTCTL */
	uint32_t counter_control;
/* 0x00B4 SAS_SSPTOV */
	uint32_t ssp_timer_timeout_values;
/* 0x00B8 FTCTL */
	uint32_t ftx_control;
/* 0x00BC FRCTL */
	uint32_t frx_control;
/* 0x00C0 FTWMRK */
	uint32_t ftx_watermark;
/* 0x00C4 ENSPINUP */
	uint32_t notify_enable_spinup_control;
/* 0x00C8 SAS_TRNTOV */
	uint32_t sas_training_sequence_timer_values;
/* 0x00CC SAS_PHYCAP */
	uint32_t phy_capabilities;
/* 0x00D0 SAS_PHYCTL */
	uint32_t phy_control;
	uint32_t reserved_d4;
/* 0x00D8 LLCTL */
	uint32_t link_layer_control;
/* 0x00DC AFE_XCVRCR */
	uint32_t afe_xcvr_control;
/* 0x00E0 AFE_LUTCR */
	uint32_t afe_lookup_table_control;
/* 0x00E4 PSZGCR */
	uint32_t phy_source_zone_group_control;
/* 0x00E8 SAS_RECPHYCAP */
	uint32_t receive_phycap;
	uint32_t reserved_ec;
/* 0x00F0 SNAFERXRSTCTL */
	uint32_t speed_negotiation_afe_rx_reset_control;
/* 0x00F4 SAS_SSIPMCTL */
	uint32_t power_management_control;
/* 0x00F8 SAS_PSPREQ_PRIM */
	uint32_t sas_pm_partial_request_primitive;
/* 0x00FC SAS_PSSREQ_PRIM */
	uint32_t sas_pm_slumber_request_primitive;
/* 0x0100 SAS_PPSACK_PRIM */
	uint32_t sas_pm_ack_primitive_register;
/* 0x0104 SAS_PSNAK_PRIM */
	uint32_t sas_pm_nak_primitive_register;
/* 0x0108 SAS_SSIPMTOV */
	uint32_t sas_primitive_timeout;
	uint32_t reserved_10c;
/* 0x0110 - 0x011C PLAPRDCTRLxREG */
	uint32_t pla_product_control[4];
/* 0x0120 PLAPRDSUMREG */
	uint32_t pla_product_sum;
/* 0x0124 PLACONTROLREG */
	uint32_t pla_control;
/* Remainder of memory space 896 bytes */
	uint32_t reserved_0128_037f[0x96];
};

struct scu_port_task_scheduler_registers {
    uint32_t control;
    uint32_t status;
};

struct transport_link_layer_pair {
    struct scu_transport_layer_registers tl;
    struct scu_link_layer_registers ll;
};

struct scu_protocol_engine_group_registers {
    uint32_t table[0xE0];
};

struct scu_sgpio_registers {
    uint32_t interface_control;
    uint32_t blink_rate;
    uint32_t start_drive_lower;
    uint32_t start_drive_upper;
    uint32_t serial_input_lower;
    uint32_t serial_input_upper;
    uint32_t vendor_specific_code;
    uint32_t reserved_001c;
    uint32_t output_data_select[8];
    uint32_t reserved_1444_14ff[0x30];
};

struct scu_viit_entry {
    uint32_t status;
    uint32_t initiator_sas_address_hi;
    uint32_t initiator_sas_address_lo;
    uint32_t reserved;
};

struct scu_zone_partition_table {
    uint32_t table[2048];
};

#define SMU_SMUCSR_GEN_BIT(name) \
	SCU_GEN_BIT(SMU_CONTROL_STATUS_ ## name)

#define SMU_SMUCSR_CONTEXT_RAM_INIT_COMPLETED \
    (SMU_SMUCSR_GEN_BIT(CONTEXT_RAM_INIT_COMPLETED))

#define SMU_SMUCSR_SCHEDULER_RAM_INIT_COMPLETED \
    (SMU_SMUCSR_GEN_BIT(SCHEDULER_RAM_INIT_COMPLETED))

typedef struct IsciState {
    PCIDevice parent_obj;
    MemoryRegion mmio[2];
} IsciState;

#define ISCI_PCI_DEVICE(obj) \
    OBJECT_CHECK(IsciState, (obj), TYPE_PCIBASE_DEVICE)

static uint64_t isci_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void isci_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps isci_mmio_ops = {
    .read = isci_mmio_read,
    .write = isci_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
    },
};

static void isci_pci_realize(PCIDevice *pdev, Error **errp)
{
    IsciState *s = ISCI_PCI_DEVICE(pdev);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pci_config_set_interrupt_pin(pdev->config, 1);

    memory_region_init_io(&s->mmio[0], OBJECT(s), &isci_mmio_ops, s,
                          "isci-mmio-0", 0x400000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_64, &s->mmio[0]);

    memory_region_init_io(&s->mmio[1], OBJECT(s), &isci_mmio_ops, s,
                          "isci-mmio-1", 0x4000);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_64, &s->mmio[1]);

    msix_init_exclusive_bar(pdev, 16, 4, errp);
}

static void isci_pci_exit(PCIDevice *pdev)
{
    msix_uninit_exclusive_bar(pdev);
}

static void isci_pci_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = isci_pci_realize;
    k->exit = isci_pci_exit;
    k->vendor_id = PCI_VENDOR_ID_INTEL;
    k->device_id = 0x1d61;
    k->class_id = PCI_CLASS_STORAGE_SAS;
    k->revision = 0x00;
    dc->desc = "Intel C600 SAS Controller";
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo isci_pci_info = {
    .name          = TYPE_PCIBASE_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(IsciState),
    .class_init    = isci_pci_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { }
    },
};

static void isci_pci_register_types(void)
{
    type_register_static(&isci_pci_info);
}

type_init(isci_pci_register_types)
