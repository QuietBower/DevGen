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

/* Additional include files retrieved from driver context */

/* ========== Register Structures from switchtec.c ========== */
#define SWITCHTEC_MRPC_PAYLOAD_SIZE 1024

enum mrpc_state {
    MRPC_IDLE = 0,
    MRPC_QUEUED,
    MRPC_RUNNING,
    MRPC_DONE,
    MRPC_IO_ERROR,
};

struct sys_info_regs {
    uint32_t device_id;
    uint32_t device_version;
    uint32_t firmware_version;
    union {
        struct {
            uint8_t partition_id;
            uint8_t reserved[3];
            uint16_t component_vendor[32]; /* placeholder size */
            uint16_t component_id;
            uint8_t component_revision;
            uint8_t pad[3];
        } gen3;
        struct {
            uint8_t partition_id;
            uint8_t reserved[3];
        } gen4;
    };
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

/* Updated mrpc_regs based on driver struct definition */
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

struct ntb_regs {
    uint32_t partition_count;
    uint32_t reserved[0x1000 - 1]; /* placeholder */
};

/* Partition info struct from driver source */
struct partition_info {
    uint8_t flg;            /* bit 0: active; bit 7: bootable */
    char id[3];             /* "GEM", "BGM", "XGM", or other */
    uint32_t st;            /* start of partition, big-endian */
    uint32_t siz;           /* length of partition, big-endian */
} QEMU_PACKED;

/* Flash info structs (placeholders until full layout is provided) */
struct flash_info_regs_gen3 { uint8_t data[0x1000]; };
struct flash_info_regs_gen4 { uint8_t data[0x1000]; };

struct flash_info_regs {
    union {
        struct flash_info_regs_gen3 gen3;
        struct flash_info_regs_gen4 gen4;
    };
};

/* ========== End of Register Structures ========== */

/* Corrected definitions from driver source */
#define SWITCHTEC_GAS_MRPC_OFFSET         0x0000
#define SWITCHTEC_GAS_TOP_CFG_OFFSET      0x10000
#define SWITCHTEC_GAS_SYS_INFO_OFFSET     0x20000
#define SWITCHTEC_GAS_SW_EVENT_OFFSET     0x28000
#define SWITCHTEC_GAS_FLASH_INFO_OFFSET   0x30000
#define SWITCHTEC_GAS_NTB_OFFSET          0x40000
#define SWITCHTEC_GAS_PART_CFG_OFFSET     0x50000
#define SWITCHTEC_GAS_PFF_CSR_OFFSET      0x80000

#define SWITCHTEC_EVENT_OCCURRED   BIT(0)
#define SWITCHTEC_EVENT_EN_IRQ     BIT(3)
#define SWITCHTEC_EVENT_EN_LOG     BIT(1)
#define SWITCHTEC_EVENT_EN_CLI     BIT(2)
#define SWITCHTEC_EVENT_FATAL      BIT(4)
#define SWITCHTEC_EVENT_CLEAR      BIT(0)
#define SWITCHTEC_EVENT_NOT_SUPP   BIT(31)

#define SWITCHTEC_DMA_MRPC_EN      BIT(0)

#define PCI_VENDOR_ID_MICROSEMI    0x11f8
#define SWITCHTEC_MAX_PFF_CSR      255  /* updated from 256 */
#define SWITCHTEC_NUM_PARTITIONS_GEN3 13
#define SWITCHTEC_NUM_PARTITIONS_GEN4 19
#define MAX_PARTITIONS            19   /* max(13, 19) */

#define TYPE_PCIBASE_DEVICE "switchtec_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

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

#define BAR0_SIZE (1 * MiB)
#define MSIX_BAR_SIZE 0x2000

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
    struct mrpc_regs mrpc;
    struct sys_info_regs sys_info;
    struct sw_event_regs sw_event;
    struct part_cfg_regs part_cfg_all[MAX_PARTITIONS];
    struct part_cfg_regs *part_cfg; /* points to current partition */
    struct ntb_regs ntb;
    struct flash_info_regs flash_info;
    struct pff_csr_regs pff_csr[SWITCHTEC_MAX_PFF_CSR];

    /* DMA MRPC state */
    uint64_t dma_mrpc_dma_addr;
    uint32_t dma_mrpc_en;
    bool dma_mrpc_active;
};

/* Internal helper for status-triggered signaling */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool interrupt = false;

    /* Check MRPC completion interrupt */
    if (s->part_cfg->mrpc_comp_hdr & (SWITCHTEC_EVENT_OCCURRED | SWITCHTEC_EVENT_EN_IRQ)) {
        if ((s->part_cfg->mrpc_comp_hdr & SWITCHTEC_EVENT_OCCURRED) &&
            (s->part_cfg->mrpc_comp_hdr & SWITCHTEC_EVENT_EN_IRQ)) {
            interrupt = true;
        }
    }

    /* TODO: check other event sources */

    if (interrupt) {
        if (msix_enabled(pdev)) {
            msix_notify(pdev, 0); /* assuming event IRQ vector 0 */
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msix_enabled(pdev) && !msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
        /* for MSI/MSI-X, no need to lower */
    }
}

/* Device-initiated DMA logic */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* DMA MRPC handling would go here, not needed for probe */
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    hwaddr offset;

    if (addr >= SWITCHTEC_GAS_PFF_CSR_OFFSET) {
        offset = addr - SWITCHTEC_GAS_PFF_CSR_OFFSET;
        if (offset < sizeof(struct pff_csr_regs) * SWITCHTEC_MAX_PFF_CSR) {
            int idx = offset / sizeof(struct pff_csr_regs);
            int reg_off = offset % sizeof(struct pff_csr_regs);
            if (reg_off + size <= sizeof(struct pff_csr_regs)) {
                memcpy(&val, (uint8_t *)&s->pff_csr[idx] + reg_off, size);
            }
        }
    } else if (addr >= SWITCHTEC_GAS_PART_CFG_OFFSET) {
        offset = addr - SWITCHTEC_GAS_PART_CFG_OFFSET;
        if (offset < sizeof(struct part_cfg_regs) * MAX_PARTITIONS) {
            int idx = offset / sizeof(struct part_cfg_regs);
            int reg_off = offset % sizeof(struct part_cfg_regs);
            if (reg_off + size <= sizeof(struct part_cfg_regs)) {
                memcpy(&val, (uint8_t *)&s->part_cfg_all[idx] + reg_off, size);
            }
        }
    } else if (addr >= SWITCHTEC_GAS_NTB_OFFSET) {
        offset = addr - SWITCHTEC_GAS_NTB_OFFSET;
        if (offset + size <= sizeof(struct ntb_regs)) {
            memcpy(&val, (uint8_t *)&s->ntb + offset, size);
        }
    } else if (addr >= SWITCHTEC_GAS_FLASH_INFO_OFFSET) {
        offset = addr - SWITCHTEC_GAS_FLASH_INFO_OFFSET;
        if (offset + size <= sizeof(struct flash_info_regs)) {
            memcpy(&val, (uint8_t *)&s->flash_info + offset, size);
        }
    } else if (addr >= SWITCHTEC_GAS_SW_EVENT_OFFSET) {
        offset = addr - SWITCHTEC_GAS_SW_EVENT_OFFSET;
        if (offset + size <= sizeof(struct sw_event_regs)) {
            memcpy(&val, (uint8_t *)&s->sw_event + offset, size);
        }
    } else if (addr >= SWITCHTEC_GAS_SYS_INFO_OFFSET) {
        offset = addr - SWITCHTEC_GAS_SYS_INFO_OFFSET;
        if (offset + size <= sizeof(struct sys_info_regs)) {
            memcpy(&val, (uint8_t *)&s->sys_info + offset, size);
        }
    } else if (addr >= SWITCHTEC_GAS_TOP_CFG_OFFSET) {
        /* unhandled region, return 0 */
    } else {
        /* MRPC region */
        offset = addr - SWITCHTEC_GAS_MRPC_OFFSET;
        if (offset + size <= sizeof(struct mrpc_regs)) {
            memcpy(&val, (uint8_t *)&s->mrpc + offset, size);
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    hwaddr offset;

    if (addr >= SWITCHTEC_GAS_PFF_CSR_OFFSET) {
        offset = addr - SWITCHTEC_GAS_PFF_CSR_OFFSET;
        if (offset < sizeof(struct pff_csr_regs) * SWITCHTEC_MAX_PFF_CSR) {
            int idx = offset / sizeof(struct pff_csr_regs);
            int reg_off = offset % sizeof(struct pff_csr_regs);
            if (reg_off + size <= sizeof(struct pff_csr_regs)) {
                memcpy((uint8_t *)&s->pff_csr[idx] + reg_off, &val, size);
            }
        }
    } else if (addr >= SWITCHTEC_GAS_PART_CFG_OFFSET) {
        offset = addr - SWITCHTEC_GAS_PART_CFG_OFFSET;
        if (offset < sizeof(struct part_cfg_regs) * MAX_PARTITIONS) {
            int idx = offset / sizeof(struct part_cfg_regs);
            int reg_off = offset % sizeof(struct part_cfg_regs);
            if (reg_off + size <= sizeof(struct part_cfg_regs)) {
                memcpy((uint8_t *)&s->part_cfg_all[idx] + reg_off, &val, size);
                /* Special handling for mrpc_comp_hdr: write-1-to-clear occurred */
                if (idx == 0 && reg_off == offsetof(struct part_cfg_regs, mrpc_comp_hdr)) {
                    /* Writing 1 to OCCURRED bit clears it */
                    s->part_cfg_all[idx].mrpc_comp_hdr &= ~SWITCHTEC_EVENT_OCCURRED;
                }
            }
        }
    } else if (addr >= SWITCHTEC_GAS_NTB_OFFSET) {
        offset = addr - SWITCHTEC_GAS_NTB_OFFSET;
        if (offset + size <= sizeof(struct ntb_regs)) {
            memcpy((uint8_t *)&s->ntb + offset, &val, size);
        }
    } else if (addr >= SWITCHTEC_GAS_FLASH_INFO_OFFSET) {
        offset = addr - SWITCHTEC_GAS_FLASH_INFO_OFFSET;
        if (offset + size <= sizeof(struct flash_info_regs)) {
            memcpy((uint8_t *)&s->flash_info + offset, &val, size);
        }
    } else if (addr >= SWITCHTEC_GAS_SW_EVENT_OFFSET) {
        offset = addr - SWITCHTEC_GAS_SW_EVENT_OFFSET;
        if (offset + size <= sizeof(struct sw_event_regs)) {
            memcpy((uint8_t *)&s->sw_event + offset, &val, size);
        }
    } else if (addr >= SWITCHTEC_GAS_SYS_INFO_OFFSET) {
        offset = addr - SWITCHTEC_GAS_SYS_INFO_OFFSET;
        if (offset + size <= sizeof(struct sys_info_regs)) {
            memcpy((uint8_t *)&s->sys_info + offset, &val, size);
        }
    } else if (addr >= SWITCHTEC_GAS_TOP_CFG_OFFSET) {
        /* unhandled region */
    } else {
        /* MRPC region */
        offset = addr - SWITCHTEC_GAS_MRPC_OFFSET;
        if (offset + size <= sizeof(struct mrpc_regs)) {
            memcpy((uint8_t *)&s->mrpc + offset, &val, size);
            /* Handle DMA MRPC enable/address updates */
            if (offset == offsetof(struct mrpc_regs, dma_en)) {
                s->dma_mrpc_en = val;
            } else if (offset == offsetof(struct mrpc_regs, dma_addr)) {
                s->dma_mrpc_dma_addr = val;
            }
        }
    }

    /* After any write, check if we need to update IRQ */
    pcibase_update_irq(s);
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

    /* Reset registers to power-on defaults */
    memset(&s->mrpc, 0, sizeof(s->mrpc));
    s->mrpc.dma_ver = 1; /* indicate DMA MRPC support */

    memset(&s->sys_info, 0, sizeof(s->sys_info));
    s->sys_info.device_id = 0x8531;
    s->sys_info.device_version = 0x0100;
    s->sys_info.firmware_version = 0x01000000;
    s->sys_info.gen3.partition_id = 0;

    memset(&s->sw_event, 0, sizeof(s->sw_event));

    for (int i = 0; i < MAX_PARTITIONS; i++) {
        memset(&s->part_cfg_all[i], 0, sizeof(struct part_cfg_regs));
        if (i == 0) {
            s->part_cfg_all[i].usp_pff_inst_id = 0;
            s->part_cfg_all[i].vep_pff_inst_id = 1;
            s->part_cfg_all[i].dsp_pff_inst_id[0] = 2;
            s->part_cfg_all[i].dsp_pff_inst_id[1] = 3;
            s->part_cfg_all[i].vep_vector_number = 0;
            s->part_cfg_all[i].usp_vector_number = 1;
        }
    }
    s->part_cfg = &s->part_cfg_all[0];

    memset(&s->ntb, 0, sizeof(s->ntb));
    s->ntb.partition_count = 4;

    memset(&s->flash_info, 0, sizeof(s->flash_info));

    memset(s->pff_csr, 0, sizeof(s->pff_csr));
    for (int i = 0; i < 8; i++) {
        s->pff_csr[i].vendor_id = PCI_VENDOR_ID_MICROSEMI;
        s->pff_csr[i].device_id = 0x8531;
    }

    s->dma_mrpc_en = 0;
    s->dma_mrpc_dma_addr = 0;
    s->dma_mrpc_active = false;
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x11f8 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x8531 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x5800 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_set_byte(pci_conf + PCI_INTERRUPT_PIN, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);

    /* BAR Initialization */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "bar0-mmio";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_RAM;
    s->bar_info[1].size = MSIX_BAR_SIZE;
    s->bar_info[1].name = "bar1-msix";

    s->num_bars = 2;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization */
    if (msix_init(pdev, 4,
                  &s->bar_regions[1], 1, 0,
                  &s->bar_regions[1], 1, 0x1000,
                  0, errp)) {
        return;
    }
    s->has_msix = true;

    /* No DMA setup required for probe */
    /* No timers */

    /* Final state initialization */
    pcibase_reset(DEVICE(s));
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
