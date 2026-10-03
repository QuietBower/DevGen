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
#include "qemu/error-report.h"

#define TYPE_PCIBASE_DEVICE "intel_vpu_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x8086
#define DEVICE_ID 0x7d1d
#define CLASS_ID 0x1200 /* Processing accelerators */

/* Registers */
#define VPU_37XX_HOST_SS_ICB_CLEAR_0                    0x00010220u
#define VPU_40XX_HOST_SS_ICB_CLEAR_0                    0x00010220u
#define VPU_40XX_HOST_SS_FW_SOC_IRQ_EN                  0x00000170u
#define VPU_40XX_HOST_SS_ICB_ENABLE_0                   0x00010240u
#define VPU_37XX_HOST_SS_ICB_ENABLE_0                   0x00010240u
#define VPU_37XX_HOST_SS_FW_SOC_IRQ_EN                  0x00000170u
#define VPU_37XX_HOST_SS_ICB_STATUS_0                   0x00010210u
#define VPU_40XX_HOST_SS_ICB_STATUS_0                   0x00010210u
#define VPU_37XX_HOST_SS_TIM_IPC_FIFO_ATM               0x000200f4u
#define VPU_40XX_HOST_SS_TIM_IPC_FIFO_ATM               0x000200f4u
#define VPU_40XX_HOST_IF_TCU_PTW_OVERRIDES              0x00360000u
#define VPU_37XX_HOST_IF_TCU_PTW_OVERRIDES              0x00360000u
#define VPU_40XX_HOST_IF_TBU_MMUSSIDV                   0x00360004u
#define VPU_37XX_HOST_IF_TBU_MMUSSIDV                   0x00360004u
#define VPU_37XX_CPU_SS_MSSCPU_CPR_LEON_RT_VEC          0x06010040u
#define VPU_37XX_HOST_SS_LOADING_ADDRESS_LO             0x00041040u
#define VPU_40XX_CPU_SS_TIM_PERF_EXT_FREE_CNT            0x01029008u
#define VPU_37XX_CPU_SS_TIM_PERF_FREE_CNT               0x06029000u
#define VPU_37XX_HOST_SS_AON_DPU_ACTIVE                 0x00030204u
#define VPU_40XX_HOST_SS_AON_PWR_ISLAND_STATUS0         0x0003002cu
#define VPU_37XX_HOST_SS_AON_PWR_ISLAND_STATUS0         0x0003002cu
#define VPU_37XX_HOST_SS_AON_VPU_IDLE_GEN               0x00030200u
#define VPU_40XX_HOST_SS_AON_IDLE_GEN                   0x00030200u
#define VPU_37XX_HOST_SS_CPR_RST_CLR                    0x00000098u
#define VPU_40XX_HOST_SS_TIM_IPC_FIFO_STAT              0x000200fcu
#define VPU_37XX_HOST_SS_TIM_IPC_FIFO_STAT              0x000200fcu
#define VPU_40XX_HOST_SS_ICB_STATUS_1                   0x00010214u
#define VPU_37XX_HOST_SS_ICB_STATUS_1                   0x00010214u
#define VPU_40XX_HOST_SS_VERIFICATION_ADDRESS_LO_IMAGE_LOCATION_MASK  GENMASK(31, 3)
#define VPU_40XX_HOST_SS_VERIFICATION_ADDRESS_LO        0x00040040u
#define VPU_50XX_HOST_SS_AON_PWR_ISLAND_STATUS_DLY      0x0003006cu
#define VPU_50XX_HOST_SS_AON_PWR_ISLAND_EN_POST_DLY     0x00030068u
#define VPU_37XX_TOP_NOC_QREQN                          0x00000160u
#define VPU_40XX_TOP_NOC_QREQN                          0x00000160u
#define VPU_37XX_HOST_SS_AON_PWR_ISLAND_EN0             0x00030024u
#define VPU_40XX_HOST_SS_AON_PWR_ISLAND_TRICKLE_EN0     0x00030028u
#define VPU_37XX_HOST_SS_AON_PWR_ISLAND_TRICKLE_EN0     0x00030028u
#define VPU_40XX_HOST_SS_AON_PWR_ISLAND_EN0             0x00030024u
#define VPU_40XX_HOST_SS_NOC_QREQN                      0x00000154u
#define VPU_37XX_HOST_SS_NOC_QREQN                      0x00000154u
#define VPU_37XX_HOST_SS_NOC_QDENY                      0x0000015cu
#define VPU_40XX_HOST_SS_NOC_QDENY                      0x0000015cu
#define VPU_40XX_HOST_SS_NOC_QACCEPTN                   0x00000158u
#define VPU_37XX_HOST_SS_NOC_QACCEPTN                   0x00000158u
#define VPU_40XX_CPU_SS_CPR_NOC_QDENY                   0x01010038u
#define VPU_40XX_CPU_SS_CPR_NOC_QREQN                   0x01010030u
#define VPU_40XX_CPU_SS_CPR_NOC_QACCEPTN                0x01010034u
#define VPU_37XX_CPU_SS_TIM_WATCHDOG                    0x0602009cu
#define VPU_37XX_CPU_SS_TIM_SAFE                        0x060200a8u
#define VPU_37XX_CPU_SS_TIM_WDOG_EN                     0x060200a4u
#define VPU_37XX_CPU_SS_TIM_GEN_CONFIG                  0x06021008u
#define VPU_40XX_CPU_SS_TIM_SAFE                        0x010200a8u
#define VPU_40XX_CPU_SS_TIM_WATCHDOG                    0x0102009cu
#define VPU_40XX_CPU_SS_TIM_WDOG_EN                     0x010200a4u
#define VPU_40XX_CPU_SS_TIM_GEN_CONFIG                  0x01021008u
#define VPU_37XX_HOST_SS_CPR_CLK_SET                    0x00000084u
#define VPU_40XX_HOST_SS_CPR_CLK_EN                     0x00000080u
#define VPU_40XX_HOST_SS_CPR_RST_EN                     0x00000090u
#define VPU_37XX_HOST_SS_CPR_RST_SET                    0x00000094u
#define VPU_37XX_HOST_SS_AON_PWR_ISO_EN0                0x00030020u
#define VPU_40XX_HOST_SS_AON_PWR_ISO_EN0                0x00030020u
#define VPU_40XX_TOP_NOC_QDENY                          0x00000168u
#define VPU_37XX_TOP_NOC_QDENY                          0x00000168u
#define VPU_40XX_TOP_NOC_QACCEPTN                       0x00000164u
#define VPU_37XX_TOP_NOC_QACCEPTN                       0x00000164u
#define VPU_37XX_CPU_SS_TIM_IPC_FIFO                    0x060200f0u
#define VPU_40XX_CPU_SS_TIM_IPC_FIFO                    0x010200f0u

#define REG_B_SIZE 0x08000000u  /* 128 MB */
#define REG_V_SIZE 0x08000000u  /* 128 MB */

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

    uint8_t regb_data[REG_B_SIZE];
    uint8_t regv_data[REG_V_SIZE];

    struct {
        dma_addr_t src;
        dma_addr_t dst;
        dma_addr_t cnt;
        uint32_t cmd;
    } dma;

    uint32_t device_status;
    bool reset_in_progress;
    uint32_t power_state;

    QEMUTimer boot_timer;
    bool firmware_ready;

    uint32_t fw_soc_irq_en;
    uint32_t icb_enable;
    uint32_t icb_status;
    uint32_t icb_clear;
    uint64_t loading_address_lo;
    uint32_t power_island_en0;
    uint32_t power_island_status0;
    uint32_t ipc_fifo_stat;
    uint32_t ipc_fifo_data;
    bool clock_enabled;
    uint32_t idle_gen;
};

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

struct vpu_firmware_header {
    u32 header_version;
    u32 image_format;
    u64 image_load_address;
    u32 image_size;
    u64 entry_point;
    u8 vpu_version[32];
    u32 compression_type;
    u64 firmware_version_load_address;
    u32 firmware_version_size;
    u64 boot_params_load_address;
    u32 api_version[16];
    u32 runtime_size;
    u32 shave_nn_fw_size;
    u32 preemption_buffer_1_size;
    u32 preemption_buffer_2_size;
    u32 preemption_buffer_1_max_size;
    u32 preemption_buffer_2_max_size;
    u32 preemption_reserved[4];
    u64 ro_section_start_address;
    u32 ro_section_size;
    u32 reserved;
};

struct vpu_boot_params {
    u32 magic;
    u32 vpu_id;
    u32 vpu_count;
    u32 reserved_0[5];
    u32 frequency;
    u32 reserved_1[12];
    u32 perf_clk_frequency;
    u32 reserved_2[42];
    u64 ipc_header_area_start;
    u32 ipc_header_area_size;
    u64 shared_region_base;
    u32 shared_region_size;
    u64 ipc_payload_area_start;
    u32 ipc_payload_area_size;
    u64 global_aliased_pio_base;
    u32 global_aliased_pio_size;
    u32 autoconfig;
};

struct vpu_job_queue_header {
    u32 engine_idx;
    u32 head;
    u32 tail;
    u32 flags;
    u32 priority_band_valid;
    u32 priority_band;
    u32 realtime_priority_level;
    u32 reserved_0[9];
};

struct vpu_job_queue_entry {
    u64 batch_buf_addr;
    u32 job_id;
    u32 flags;
    u64 doorbell_timestamp;
    u64 host_tracking_id;
    u64 primary_preempt_buf_addr;
    u32 primary_preempt_buf_size;
    u32 secondary_preempt_buf_size;
    u64 secondary_preempt_buf_addr;
    u64 reserved_0;
};

struct vpu_inline_cmd {
    u64 reserved_0;
    u32 type;
    u32 flags;
    union payload {
        struct fence {
            u64 fence_handle;
            u64 current_value_va;
            u64 monitored_value_va;
            u64 value;
            u64 log_buffer_va;
            u64 npu_private_data;
        } fence;
        u64 reserved_1[6];
    } payload;
};

union vpu_jobq_slot {
    struct vpu_job_queue_entry job;
    struct vpu_inline_cmd inline_cmd;
};

struct vpu_job_queue {
    struct vpu_job_queue_header header;
    union vpu_jobq_slot slot[];
};

struct ivpu_ipc_hdr {
    u32 data_addr;
    u32 data_size;
    u16 channel;
    u8 src_node;
    u8 dst_node;
    u8 status;
};

struct vpu_ipc_msg_payload_engine_reset {
    u32 engine_idx;
    u32 reserved_0;
};

struct vpu_ipc_msg_payload_engine_preempt {
    u32 engine_idx;
    u32 preempt_id;
};

union vpu_ipc_msg_payload {
    struct vpu_ipc_msg_payload_engine_reset engine_reset;
    struct vpu_ipc_msg_payload_engine_preempt engine_preempt;
};

struct vpu_jsm_msg {
    u64 reserved_0;
    u32 type;
    u32 status;
    u32 request_id;
    u32 result;
    u64 reserved_1;
    union vpu_ipc_msg_payload payload;
};

#define IPC_IRQ_BIT         0x1
#define IVPU_IPC_CHAN_BOOT_MSG  0x01
#define IVPU_IPC_BOOT_MSG_DATA_ADDR 0x424f4f54

static void boot_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;

    s->ipc_fifo_stat = 0x1;
    s->ipc_fifo_data = 0;
    s->firmware_ready = true;

    if (s->icb_enable & 0x1) {
        s->icb_status |= IPC_IRQ_BIT;
        if (msi_enabled(PCI_DEVICE(s))) {
            msi_notify(PCI_DEVICE(s), 0);
        } else {
            pci_set_irq(PCI_DEVICE(s), 1);
        }
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case VPU_40XX_HOST_SS_FW_SOC_IRQ_EN:
        val = s->fw_soc_irq_en;
        break;
    case VPU_40XX_HOST_SS_ICB_ENABLE_0:
        val = s->icb_enable;
        break;
    case VPU_40XX_HOST_SS_ICB_STATUS_0:
        val = s->icb_status;
        break;
    case VPU_40XX_HOST_SS_ICB_STATUS_1:
        val = 0;
        break;
    case VPU_40XX_HOST_SS_TIM_IPC_FIFO_ATM:
        if (s->firmware_ready) {
            struct ivpu_ipc_hdr hdr = {
                .data_addr = IVPU_IPC_BOOT_MSG_DATA_ADDR,
                .data_size = 0,
                .channel = IVPU_IPC_CHAN_BOOT_MSG,
                .src_node = 0,
                .dst_node = 0,
                .status = 0
            };
            val = hdr.data_addr;
        } else {
            val = 0;
        }
        break;
    case VPU_40XX_HOST_SS_TIM_IPC_FIFO_STAT:
        val = s->ipc_fifo_stat;
        break;

    case VPU_40XX_HOST_SS_AON_PWR_ISLAND_EN0:
        val = s->power_island_en0;
        break;
    case VPU_40XX_HOST_SS_AON_PWR_ISLAND_STATUS0:
        val = 0;
        if (s->power_island_en0 & 0x1) {
            val |= 0x2; /* power good at bit1 */
        }
        if (s->clock_enabled) {
            val |= 0x1; /* PLL lock at bit0 */
        }
        break;
    case VPU_37XX_HOST_SS_AON_PWR_ISLAND_TRICKLE_EN0:
        val = 0;
        break;
    case VPU_40XX_HOST_SS_AON_PWR_ISO_EN0:
        val = 0;
        break;
    case VPU_40XX_HOST_SS_AON_IDLE_GEN:
        val = s->idle_gen;
        break;
    case VPU_50XX_HOST_SS_AON_PWR_ISLAND_STATUS_DLY:
        val = 0;
        break;

    case VPU_40XX_HOST_SS_NOC_QREQN:
        val = 0;
        break;
    case VPU_40XX_TOP_NOC_QREQN:
        val = 0;
        break;
    case VPU_40XX_HOST_SS_NOC_QDENY:
        val = 0;
        break;
    case VPU_40XX_TOP_NOC_QDENY:
        val = 0;
        break;
    case VPU_40XX_HOST_SS_NOC_QACCEPTN:
        val = 1;
        break;
    case VPU_40XX_TOP_NOC_QACCEPTN:
        val = 1;
        break;
    case VPU_40XX_CPU_SS_CPR_NOC_QREQN:
        val = 0;
        break;
    case VPU_40XX_CPU_SS_CPR_NOC_QDENY:
        val = 0;
        break;
    case VPU_40XX_CPU_SS_CPR_NOC_QACCEPTN:
        val = 1;
        break;

    case VPU_40XX_HOST_SS_CPR_CLK_EN:
        val = s->clock_enabled ? 1 : 0;
        break;
    case VPU_40XX_HOST_SS_CPR_RST_EN:
        val = 0;
        break;
    case VPU_37XX_HOST_SS_CPR_RST_CLR:
        val = 0;
        break;
    case VPU_37XX_HOST_SS_LOADING_ADDRESS_LO:
        val = (uint32_t)s->loading_address_lo;
        break;
    case VPU_40XX_HOST_SS_VERIFICATION_ADDRESS_LO:
        val = 0;
        break;

    case VPU_37XX_CPU_SS_TIM_WATCHDOG:
        val = 0;
        break;
    case VPU_40XX_CPU_SS_TIM_WATCHDOG:
        val = 0;
        break;
    case VPU_37XX_CPU_SS_TIM_SAFE:
        val = 0;
        break;
    case VPU_40XX_CPU_SS_TIM_SAFE:
        val = 0;
        break;
    case VPU_37XX_CPU_SS_TIM_WDOG_EN:
        val = 0;
        break;
    case VPU_40XX_CPU_SS_TIM_WDOG_EN:
        val = 0;
        break;
    case VPU_37XX_CPU_SS_TIM_GEN_CONFIG:
        val = 0;
        break;
    case VPU_40XX_CPU_SS_TIM_GEN_CONFIG:
        val = 0;
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

    switch (addr) {
    case VPU_40XX_HOST_SS_FW_SOC_IRQ_EN:
        s->fw_soc_irq_en = val;
        break;
    case VPU_40XX_HOST_SS_ICB_ENABLE_0:
        s->icb_enable = val;
        break;
    case VPU_40XX_HOST_SS_ICB_CLEAR_0:
        s->icb_status &= ~(val & 0x1);
        if (s->icb_status == 0) {
            if (msi_enabled(PCI_DEVICE(s))) {
                /* msi_notify only raises, no need to lower */
            } else {
                pci_set_irq(PCI_DEVICE(s), 0);
            }
        }
        break;
    case VPU_40XX_HOST_SS_AON_PWR_ISLAND_EN0:
        s->power_island_en0 = val;
        if (val & 0x1) {
            s->clock_enabled = true;
        } else {
            s->clock_enabled = false;
        }
        break;
    case VPU_37XX_HOST_SS_LOADING_ADDRESS_LO:
        s->loading_address_lo = val;
        if (!s->firmware_ready) {
            timer_mod(&s->boot_timer,
                      qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 10);
        }
        break;
    case VPU_40XX_HOST_SS_VERIFICATION_ADDRESS_LO:
        break;
    case VPU_40XX_HOST_SS_CPR_CLK_EN:
        if (val & 0x1) {
            s->clock_enabled = true;
        } else {
            s->clock_enabled = false;
        }
        break;
    case VPU_40XX_HOST_SS_CPR_RST_EN:
        break;
    case VPU_37XX_HOST_SS_CPR_RST_CLR:
        break;
    case VPU_40XX_HOST_SS_AON_IDLE_GEN:
        s->idle_gen = (uint32_t)val;
        break;
    default:
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

    s->fw_soc_irq_en = 0;
    s->icb_enable = 0;
    s->icb_status = 0;
    s->icb_clear = 0;
    s->loading_address_lo = 0;
    s->power_island_en0 = 0;
    s->power_island_status0 = 0;
    s->ipc_fifo_stat = 0;
    s->ipc_fifo_data = 0;
    s->firmware_ready = false;
    s->clock_enabled = false;
    s->idle_gen = 0;
    timer_del(&s->boot_timer);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        error_report("Failed to initialize MSI");
        return;
    }

    timer_init_ms(&s->boot_timer, QEMU_CLOCK_VIRTUAL, boot_timer_cb, s);

    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = REG_V_SIZE;
    s->bar_info[0].name = "regv";
    s->bar_info[1].index = 4;
    s->bar_info[1].type = BAR_TYPE_MMIO; /* Changed from RAM to MMIO */
    s->bar_info[1].size = REG_B_SIZE;
    s->bar_info[1].name = "regb";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    timer_del(&s->boot_timer);

    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "intel_vpu_pci",
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
