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

#define TYPE_PCIBASE_DEVICE "intel_vpu_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_MTL 0x7d1d
#define PCI_CLASS_PROCESSOR_CO 0x0b40

#define VPU_37XX_HOST_SS_ICB_CLEAR_0					0x00010220u
#define VPU_40XX_HOST_SS_ICB_CLEAR_0					0x00010220u
#define VPU_40XX_HOST_SS_FW_SOC_IRQ_EN					0x00000170u
#define VPU_37XX_HOST_SS_ICB_ENABLE_0					0x00010240u
#define VPU_40XX_HOST_SS_ICB_ENABLE_0					0x00010240u
#define VPU_37XX_HOST_SS_FW_SOC_IRQ_EN					0x00000170u
#define VPU_37XX_HOST_SS_ICB_STATUS_0					0x00010210u
#define VPU_40XX_HOST_SS_ICB_STATUS_0					0x00010210u
#define VPU_37XX_HOST_SS_TIM_IPC_FIFO_ATM				0x000200f4u
#define VPU_40XX_HOST_SS_TIM_IPC_FIFO_ATM				0x000200f4u
#define VPU_40XX_HOST_IF_TCU_PTW_OVERRIDES				0x00360000u
#define VPU_37XX_HOST_IF_TCU_PTW_OVERRIDES				0x00360000u
#define VPU_40XX_HOST_IF_TBU_MMUSSIDV					0x00360004u
#define VPU_37XX_HOST_IF_TBU_MMUSSIDV					0x00360004u
#define VPU_37XX_CPU_SS_MSSCPU_CPR_LEON_RT_VEC				0x06010040u
#define VPU_37XX_HOST_SS_LOADING_ADDRESS_LO				0x00041040u
#define VPU_40XX_CPU_SS_TIM_PERF_EXT_FREE_CNT				0x01029008u
#define VPU_37XX_CPU_SS_TIM_PERF_FREE_CNT				0x06029000u
#define VPU_37XX_HOST_SS_AON_DPU_ACTIVE					0x00030204u
#define VPU_40XX_HOST_SS_AON_PWR_ISLAND_STATUS0				0x0003002cu
#define VPU_37XX_HOST_SS_AON_PWR_ISLAND_STATUS0				0x0003002cu
#define VPU_37XX_HOST_SS_AON_VPU_IDLE_GEN				0x00030200u
#define VPU_40XX_HOST_SS_AON_IDLE_GEN					0x00030200u
#define VPU_37XX_HOST_SS_CPR_RST_CLR					0x00000098u
#define VPU_40XX_HOST_SS_TIM_IPC_FIFO_STAT				0x000200fcu
#define VPU_37XX_HOST_SS_TIM_IPC_FIFO_STAT				0x000200fcu
#define VPU_40XX_HOST_SS_ICB_STATUS_1					0x00010214u
#define VPU_37XX_HOST_SS_ICB_STATUS_1					0x00010214u
#define VPU_40XX_HOST_SS_VERIFICATION_ADDRESS_LO			0x00040040u
#define VPU_50XX_HOST_SS_AON_PWR_ISLAND_STATUS_DLY			0x0003006cu
#define VPU_50XX_HOST_SS_AON_PWR_ISLAND_EN_POST_DLY			0x00030068u
#define VPU_37XX_TOP_NOC_QREQN						0x00000160u
#define VPU_40XX_TOP_NOC_QREQN						0x00000160u
#define VPU_37XX_HOST_SS_AON_PWR_ISLAND_EN0				0x00030024u
#define VPU_40XX_HOST_SS_AON_PWR_ISLAND_TRICKLE_EN0			0x00030028u
#define VPU_37XX_HOST_SS_AON_PWR_ISLAND_TRICKLE_EN0			0x00030028u
#define VPU_40XX_HOST_SS_AON_PWR_ISLAND_EN0				0x00030024u
#define VPU_40XX_HOST_SS_NOC_QREQN					0x00000154u
#define VPU_37XX_HOST_SS_NOC_QREQN					0x00000154u
#define VPU_37XX_HOST_SS_NOC_QDENY					0x0000015cu
#define VPU_40XX_HOST_SS_NOC_QDENY					0x0000015cu
#define VPU_40XX_HOST_SS_NOC_QACCEPTN					0x00000158u
#define VPU_37XX_HOST_SS_NOC_QACCEPTN					0x00000158u
#define VPU_40XX_CPU_SS_CPR_NOC_QDENY					0x01010038u
#define VPU_40XX_CPU_SS_CPR_NOC_QREQN					0x01010030u
#define VPU_40XX_CPU_SS_CPR_NOC_QACCEPTN				0x01010034u
#define VPU_37XX_CPU_SS_TIM_WATCHDOG					0x0602009cu
#define VPU_37XX_CPU_SS_TIM_SAFE					0x060200a8u
#define VPU_37XX_CPU_SS_TIM_WDOG_EN					0x060200a4u
#define VPU_37XX_CPU_SS_TIM_GEN_CONFIG					0x06021008u
#define VPU_40XX_CPU_SS_TIM_SAFE					0x010200a8u
#define VPU_40XX_CPU_SS_TIM_WATCHDOG					0x0102009cu
#define VPU_40XX_CPU_SS_TIM_WDOG_EN					0x010200a4u
#define VPU_40XX_CPU_SS_TIM_GEN_CONFIG					0x01021008u
#define VPU_37XX_HOST_SS_CPR_CLK_SET					0x00000084u
#define VPU_40XX_HOST_SS_CPR_CLK_EN					0x00000080u
#define VPU_40XX_HOST_SS_CPR_RST_EN					0x00000090u
#define VPU_37XX_HOST_SS_CPR_RST_SET					0x00000094u
#define VPU_37XX_HOST_SS_AON_PWR_ISO_EN0				0x00030020u
#define VPU_40XX_HOST_SS_AON_PWR_ISO_EN0				0x00030020u
#define VPU_40XX_TOP_NOC_QDENY						0x00000168u
#define VPU_37XX_TOP_NOC_QDENY						0x00000168u
#define VPU_40XX_TOP_NOC_QACCEPTN					0x00000164u
#define VPU_37XX_TOP_NOC_QACCEPTN					0x00000164u
#define VPU_37XX_CPU_SS_TIM_IPC_FIFO					0x060200f0u
#define VPU_40XX_CPU_SS_TIM_IPC_FIFO					0x010200f0u

#define VPU_HW_BTRS_MTL_VPU_TELEMETRY_SIZE			0x00000084u
#define VPU_HW_BTRS_LNL_VPU_TELEMETRY_SIZE			0x0000016cu
#define VPU_HW_BTRS_MTL_VPU_TELEMETRY_ENABLE			0x00000088u
#define VPU_HW_BTRS_LNL_VPU_TELEMETRY_ENABLE			0x00000170u
#define VPU_HW_BTRS_MTL_VPU_TELEMETRY_OFFSET			0x00000080u
#define VPU_HW_BTRS_LNL_VPU_TELEMETRY_OFFSET			0x00000168u
#define VPU_HW_BTRS_MTL_VPU_STATUS				0x00000044u
#define VPU_HW_BTRS_LNL_VPU_STATUS				0x00000154u

#define IVPU_IPC_CHAN_BOOT_MSG		0x3ff
#define IVPU_IPC_BOOT_MSG_DATA_ADDR	0x424f4f54

struct ivpu_ipc_hdr {
	uint32_t data_addr;
	uint32_t data_size;
	uint16_t channel;
	uint8_t src_node;
	uint8_t dst_node;
	uint8_t status;
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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* DMA Context */
    uint64_t dma_mask;

    /* Register State */
    uint32_t vpu_37xx_host_ss_cpr_rst_clr;
    uint32_t vpu_37xx_host_ss_aon_vpu_idle_gen;
    uint32_t vpu_40xx_host_ss_aon_idle_gen;
    uint32_t vpu_37xx_host_ss_aon_dpu_active;
    uint32_t vpu_37xx_host_if_tcu_ptw_overrides;
    uint32_t vpu_40xx_host_if_tcu_ptw_overrides;
    uint32_t vpu_37xx_host_if_tbu_mmussidv;
    uint32_t vpu_40xx_host_if_tbu_mmussidv;
    uint32_t vpu_37xx_cpu_ss_msscpu_cpr_leon_rt_vec;
    uint32_t vpu_37xx_host_ss_loading_address_lo;
};

static uint64_t pcibase_regv_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case VPU_37XX_HOST_SS_TIM_IPC_FIFO_ATM:
        val = 0;
        break;
    case VPU_37XX_HOST_SS_TIM_IPC_FIFO_STAT:
        val = 0; /* Mock fill level */
        break;
    case VPU_37XX_HOST_SS_CPR_RST_CLR:
        val = s->vpu_37xx_host_ss_cpr_rst_clr | 0xFFFFFFFF; /* Satisfy AON poll */
        break;
    case VPU_37XX_HOST_SS_AON_VPU_IDLE_GEN:
        val = s->vpu_37xx_host_ss_aon_vpu_idle_gen;
        break;
    case VPU_37XX_HOST_SS_AON_PWR_ISLAND_STATUS0:
        val = 0xFFFFFFFF; /* Satisfy MSS_CPU / CSS_CPU poll */
        break;
    case VPU_37XX_HOST_SS_AON_DPU_ACTIVE:
        val = s->vpu_37xx_host_ss_aon_dpu_active;
        break;
    case VPU_37XX_HOST_IF_TCU_PTW_OVERRIDES:
        val = s->vpu_37xx_host_if_tcu_ptw_overrides;
        break;
    case VPU_37XX_HOST_IF_TBU_MMUSSIDV:
        val = s->vpu_37xx_host_if_tbu_mmussidv;
        break;
    case VPU_37XX_CPU_SS_MSSCPU_CPR_LEON_RT_VEC:
        val = s->vpu_37xx_cpu_ss_msscpu_cpr_leon_rt_vec;
        break;
    case VPU_37XX_HOST_SS_LOADING_ADDRESS_LO:
        val = s->vpu_37xx_host_ss_loading_address_lo;
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_regv_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case VPU_37XX_HOST_SS_ICB_CLEAR_0:
        break;
    case VPU_37XX_CPU_SS_TIM_IPC_FIFO:
    case VPU_40XX_CPU_SS_TIM_IPC_FIFO:
        break;
    case VPU_37XX_HOST_SS_FW_SOC_IRQ_EN:
        break;
    case VPU_37XX_HOST_SS_ICB_ENABLE_0:
        break;
    case VPU_37XX_HOST_SS_CPR_RST_CLR:
        s->vpu_37xx_host_ss_cpr_rst_clr = val;
        break;
    case VPU_37XX_HOST_SS_AON_VPU_IDLE_GEN:
        s->vpu_37xx_host_ss_aon_vpu_idle_gen = val;
        s->vpu_40xx_host_ss_aon_idle_gen = val;
        break;
    case VPU_37XX_HOST_SS_AON_DPU_ACTIVE:
        s->vpu_37xx_host_ss_aon_dpu_active = val;
        break;
    case VPU_37XX_HOST_IF_TCU_PTW_OVERRIDES:
        s->vpu_37xx_host_if_tcu_ptw_overrides = val;
        s->vpu_40xx_host_if_tcu_ptw_overrides = val;
        break;
    case VPU_37XX_HOST_IF_TBU_MMUSSIDV:
        s->vpu_37xx_host_if_tbu_mmussidv = val;
        s->vpu_40xx_host_if_tbu_mmussidv = val;
        break;
    case VPU_37XX_CPU_SS_MSSCPU_CPR_LEON_RT_VEC:
        s->vpu_37xx_cpu_ss_msscpu_cpr_leon_rt_vec = val;
        break;
    case VPU_37XX_HOST_SS_LOADING_ADDRESS_LO:
        s->vpu_37xx_host_ss_loading_address_lo = val;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_regv_ops = {
    .read = pcibase_regv_read,
    .write = pcibase_regv_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static uint64_t pcibase_regb_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;

    switch (addr) {
    case VPU_HW_BTRS_MTL_VPU_STATUS:
    case VPU_HW_BTRS_LNL_VPU_STATUS:
        val = ~0ULL; /* Satisfy CLOCK_RESOURCE_OWN_ACK poll */
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_regb_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    switch (addr) {
    case VPU_HW_BTRS_MTL_VPU_TELEMETRY_ENABLE:
    case VPU_HW_BTRS_LNL_VPU_TELEMETRY_ENABLE:
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_regb_ops = {
    .read = pcibase_regb_read,
    .write = pcibase_regb_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
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
        const MemoryRegionOps *ops = &pcibase_regv_ops;
        if (bi->index == 4) {
            ops = &pcibase_regb_ops;
        }
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x8086 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x7d1d );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0b40 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 5;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x10000, .name = "regv" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_NONE, .size = 0, .name = NULL };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_NONE, .size = 0, .name = NULL };
    s->bar_info[3] = (BARInfo){ .index = 3, .type = BAR_TYPE_NONE, .size = 0, .name = NULL };
    s->bar_info[4] = (BARInfo){ .index = 4, .type = BAR_TYPE_MMIO, .size = 0x10000, .name = "regb" };
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* msi_init or msix_init calls */
    s->has_msi = true;
    s->has_msix = true;
    msi_init(pdev, 0, 1, true, false, errp);

    /* Set DMA masks or ring buffer limits */
    s->dma_mask = ~0ULL;
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

/* Minimal VMState to satisfy QEMU migration subsystems */
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
