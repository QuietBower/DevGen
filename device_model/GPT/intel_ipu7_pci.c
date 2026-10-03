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

#define TYPE_PCIBASE_DEVICE "intel_ipu7_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#ifndef PCI_VENDOR_ID_INTEL
#define PCI_VENDOR_ID_INTEL 0x8086
#endif
#ifndef PCI_CLASS_OTHERS
#define PCI_CLASS_OTHERS 0xff
#endif

#define IPU_PCI_BAR             0
#define IPU_PCI_PBBAR           4
#define IPU7_PSYS_SPC_OFFSET    0x118000
#define IPU7_ISYS_NUM_STREAMS   12
#define ISYS_MMID               0x1
#define PSYS_MMID               0x0
#define BUTTRESS_REG_FW_GP24    0x40a0
#define BUTTRESS_REG_FW_GP8     0x4060
#define DMA_ATTR_RESERVE_REGION (1U << 31)
#define IPU_FW_CODE_REGION_SIZE 0x1000000
#define IPU7P5_PCI_ID           0xb05d
#define IPU_DMA_MASK            39
#define IPU7_PCI_ID             0x645d
#define IPU8_PCI_ID             0xd719
#define BUTTRESS_REG_IS_WORKPOINT_REQ 0x2104
#define BUTTRESS_REG_PS_WORKPOINT_REQ 0x2100
#define BUTTRESS_REG_SECURITY_CTL 0x2318
#define BUTTRESS_REG_FW_SOURCE_BASE 0x233c
#define BUTTRESS_REG_FW_SOURCE_SIZE 0x2338
#define BUTTRESS_REG_IRQ_ENABLE 0x2008
#define BUTTRESS_REG_IRQ_CLEAR  0x200c
#define BUTTRESS_REG_IRQ_MASK   0x2010
#define BUTTRESS_REG_CAMERA_MASK 0x2310
#define BUTTRESS_REG_CSE2IUDB0  0x2500
#define BUTTRESS_REG_CSE2IUCSR  0x2508
#define BUTTRESS_REG_IU2CSEDB0  0x250c
#define BUTTRESS_REG_IU2CSEDATA0 0x2510
#define BUTTRESS_REG_IU2CSECSR  0x2514
#define BUTTRESS_REG_FW_BOOT_PARAMS7 0x401c
#define BUTTRESS_REG_IDLE_WDT   0x218c
#define BUTTRESS_REG_FW_RESET_CTL 0x2334
#define BUTTRESS_REG_IPU_SKU    0x3030
#define BUTTRESS_REG_CG_CTRL_BITS 0x3014
#define BAR2_MISC_CONFIG        0x64
#define GLOBAL_INTERRUPT_MASK   0x8
#define TLBID_HASH_ENABLE_63_32 0x34
#define TLBID_HASH_ENABLE_95_64 0x38
#define TLBID_HASH_ENABLE_127_96 0x3c

#define BUTTRESS_REG_IRQ_STATUS 0x2004

#define INTERRUPT_STATUS                0x0
#define BTRS_LOCAL_INTERRUPT_MASK       0x4
#define ATS_ERROR_LOG1                  0x8
#define ATS_ERROR_LOG2                  0xc
#define CFI_0_ERROR_LOG                 0x10
#define CFI_1_ERROR_LOGGING             0x14
#define IMR_ERROR_LOGGING_LOW           0x18
#define IMR_ERROR_LOGGING_HIGH          0x1c
#define IMR_ERROR_LOGGING_CFI_1_LOW     0x20
#define IMR_ERROR_LOGGING_CFI_1_HIGH    0x24

#define IPU_NAME                "intel-ipu7"

#define IPU7_PCI_VENDOR_ID      PCI_VENDOR_ID_INTEL
#define IPU7_PCI_DEVICE_ID      IPU7_PCI_ID

#define IPU7_PCI_CLASS_ID       PCI_CLASS_OTHERS

#define IPU_FW_CODE_REGION_START 0x4000000
#define IPU_FW_CODE_REGION_END   (IPU_FW_CODE_REGION_START + IPU_FW_CODE_REGION_SIZE)
#define IPU_MMU_ADDR_BITS_NON_SECURE 31
#define IPU_MMU_ADDR_BITS       32
#define IPU_MMUV2_MAX_L2_BLOCKS 2
#define IPU_MMUV2_L2_RANGE      (1024 * 4096ULL)
#define IPU_MMUV2_TRASH_RANGE   (IPU_MMUV2_L2_RANGE * IPU_MMUV2_MAX_L2_BLOCKS)

#define BUTTRESS_IRQ_CSE_CSR_SET             (1U << 2)
#define BUTTRESS_IRQ_PS_IRQ                  (1U << 31)
#define BUTTRESS_IRQ_PUNIT_2_IUNIT_IRQ       (1U << 3)
#define BUTTRESS_IRQ_IS_IRQ                  (1U << 30)
#define BUTTRESS_IRQ_IPC_FROM_CSE_IS_WAITING (1U << 1)
#define BUTTRESS_IRQ_IPC_EXEC_DONE_BY_CSE    (1U << 0)

#define IPU_BUTTRESS_PWR_STATE_IS_PWR_SHIFT  0
#define IPU_BUTTRESS_PWR_STATE_IS_PWR_MASK   (0x3U << IPU_BUTTRESS_PWR_STATE_IS_PWR_SHIFT)
#define IPU_BUTTRESS_PWR_STATE_PS_PWR_SHIFT  4
#define IPU_BUTTRESS_PWR_STATE_PS_PWR_MASK   (0x3U << IPU_BUTTRESS_PWR_STATE_PS_PWR_SHIFT)
#define IPU_BUTTRESS_PWR_STATE_UP_DONE       0x3
#define IPU_BUTTRESS_PWR_STATE_DN_DONE       0x0

#define IPU_FREQ_CTL_RATIO_SHIFT             0x0
#define IPU_FREQ_CTL_CDYN                    0x80
#define IPU_FREQ_CTL_CDYN_SHIFT              0x8
#define IPU7_IS_FREQ_CTL_DEFAULT_RATIO       0x1b
#define IPU7_PS_FREQ_CTL_DEFAULT_RATIO       0x14
#define IPU8_IS_FREQ_CTL_DEFAULT_RATIO       0x10
#define IPU8_PS_FREQ_CTL_DEFAULT_RATIO       0x10

#define IPU_UNIFIED_OFFSET                   0
#define IPU_ISYS_DMEM_OFFSET                 0x200000
#define IPU_PSYS_DMEM_OFFSET                 0x100000
#define IPU_ISYS_SPC_OFFSET                  0x210000
#define IPU7_PSYS_SPC_OFFSET                 0x118000

#define IS_IO_BASE                           0x280000
#define IS_IO_CSI2_GPREGS_BASE               (IS_IO_BASE + 0x53400)
#define IPU8_IS_IO_CSI2_GPREGS_BASE          (IS_IO_BASE + 0x40e00)

#define IPU_MMU_MAX_TLB_L1_STREAMS           40U
#define IPU_MMU_MAX_TLB_L2_STREAMS           40U
#define IPU_MMU_MAX_NUM                      5U
#define IPU_ZLX_MAX_NUM                      32U
#define IPU_UAO_PLANE_MAX_NUM                64U
#define IPU_ZLX_POOL_NUM                     8U
#define IPU_MAX_VC_IOSF_PORTS                4

#define IPU_ISYS_OVERALLOC_MIN               1024

#define IPU_CSI_PORT_A_ADDR_OFFSET           0x0
#define IPU_CSI_PORT_B_ADDR_OFFSET           0x0
#define IPU_CSI_PORT_C_ADDR_OFFSET           0x0
#define IPU_CSI_PORT_D_ADDR_OFFSET           0x0

#define IPU7_BAR0_MMIO_SIZE                  0x1000
#define IPU7_BAR4_MMIO_SIZE                  0x1000

#define BUTTRESS_IRQS (BUTTRESS_IRQ_CSE_CSR_SET | BUTTRESS_IRQ_PS_IRQ | BUTTRESS_IRQ_PUNIT_2_IUNIT_IRQ | BUTTRESS_IRQ_IS_IRQ | BUTTRESS_IRQ_IPC_FROM_CSE_IS_WAITING | BUTTRESS_IRQ_IPC_EXEC_DONE_BY_CSE)

#define IPU7_IRQ_STATUS_DEFAULT 0

#define BUTTRESS_SECURITY_CTL_FW_SECURE_MODE        (1U << 16)
#define BUTTRESS_SECURITY_CTL_FW_SETUP_MASK         0x1FU
#define BUTTRESS_SECURITY_CTL_FW_SETUP_DONE         (1U << 0)
#define BUTTRESS_SECURITY_CTL_AUTH_FAILED           (1U << 3)
#define BUTTRESS_SECURITY_CTL_AUTH_DONE             (1U << 1)

#define BUTTRESS_CSE2IUDATA0_IPC_BOOT_LOAD_DONE     (1U << 0)
#define BUTTRESS_IU2CSEDATA0_IPC_BOOT_LOAD          1
#define BUTTRESS_CSE2IUDATA0_IPC_AUTH_RUN_DONE      (1U << 1)
#define BUTTRESS_IU2CSEDATA0_IPC_AUTH_RUN           2

#define BOOTLOADER_STATUS_OFFSET                    BUTTRESS_REG_FW_BOOT_PARAMS7
#define BOOTLOADER_MAGIC_KEY                        0xb00710adU

#define BUTTRESS_FW_RESET_CTL_START                 (1U << 0)
#define BUTTRESS_FW_RESET_CTL_DONE                  (1U << 1)

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

    uint8_t hw_ver;

    uint32_t bar0_regs[IPU7_BAR0_MMIO_SIZE / 4];

    uint32_t bar4_regs[IPU7_BAR4_MMIO_SIZE / 4];

    uint32_t pb_interrupt_status;
    uint32_t pb_local_interrupt_mask;

    uint32_t buttress_irq_status;
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    uint32_t enable = 0;
    if (BUTTRESS_REG_IRQ_ENABLE < IPU7_BAR0_MMIO_SIZE) {
        enable = s->bar0_regs[BUTTRESS_REG_IRQ_ENABLE >> 2];
    }

    if ((s->buttress_irq_status & BUTTRESS_IRQS) && (enable & BUTTRESS_IRQS)) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)s;
    (void)is_write;
    (void)pdev;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size < 1 || size > 8) {
        return 0;
    }

    if (addr < IPU7_BAR0_MMIO_SIZE) {
        hwaddr index = addr >> 2;
        if (size == 4 && (addr & 3) == 0 && index < (IPU7_BAR0_MMIO_SIZE / 4)) {
            switch (addr) {
            case BUTTRESS_REG_IPU_SKU:
                val = s->bar0_regs[index];
                if (val == 0) {
                    val = 0x21;
                    s->bar0_regs[index] = (uint32_t)val;
                }
                return val;
            case BUTTRESS_REG_CAMERA_MASK:
                val = s->bar0_regs[index];
                if (val == 0) {
                    val = 0x1;
                    s->bar0_regs[index] = (uint32_t)val;
                }
                return val;
            case BUTTRESS_REG_IDLE_WDT:
                val = s->bar0_regs[index];
                if (val == 0) {
                    val = 0x100;
                    s->bar0_regs[index] = (uint32_t)val;
                }
                return val;
            case BUTTRESS_REG_SECURITY_CTL:
                val = s->bar0_regs[index];
                return val;
            case BUTTRESS_REG_IRQ_STATUS:
                val = s->buttress_irq_status;
                return val;
            case BUTTRESS_REG_FW_BOOT_PARAMS7:
                val = s->bar0_regs[index];
                if (val == 0) {
                    val = BOOTLOADER_MAGIC_KEY;
                    s->bar0_regs[index] = (uint32_t)val;
                }
                return val;
            case BUTTRESS_REG_IU2CSEDATA0:
                val = s->bar0_regs[index];
                return val;
            case BUTTRESS_REG_CSE2IUDB0:
                val = s->bar0_regs[index];
                return val;
            default:
                break;
            }

            val = s->bar0_regs[index];
            return val;
        }

        return 0;
    }

    if (addr < IPU7_BAR4_MMIO_SIZE) {
        hwaddr index = addr >> 2;
        if (size == 4 && (addr & 3) == 0 && index < (IPU7_BAR4_MMIO_SIZE / 4)) {
            switch (addr) {
            case INTERRUPT_STATUS:
                val = s->pb_interrupt_status;
                return val;
            case BTRS_LOCAL_INTERRUPT_MASK:
                val = s->pb_local_interrupt_mask;
                return val;
            default:
                break;
            }

            switch (addr) {
            case GLOBAL_INTERRUPT_MASK:
            case BAR2_MISC_CONFIG:
            case TLBID_HASH_ENABLE_63_32:
            case TLBID_HASH_ENABLE_95_64:
            case TLBID_HASH_ENABLE_127_96:
            case BUTTRESS_REG_IS_WORKPOINT_REQ:
            case BUTTRESS_REG_PS_WORKPOINT_REQ:
            case BUTTRESS_REG_IRQ_ENABLE:
            case BUTTRESS_REG_IRQ_MASK:
            case BUTTRESS_REG_IRQ_CLEAR:
            case BUTTRESS_REG_SECURITY_CTL:
            case BUTTRESS_REG_FW_SOURCE_BASE:
            case BUTTRESS_REG_FW_SOURCE_SIZE:
            case BUTTRESS_REG_CAMERA_MASK:
            case BUTTRESS_REG_CSE2IUDB0:
            case BUTTRESS_REG_CSE2IUCSR:
            case BUTTRESS_REG_IU2CSEDB0:
            case BUTTRESS_REG_IU2CSEDATA0:
            case BUTTRESS_REG_IU2CSECSR:
            case BUTTRESS_REG_FW_BOOT_PARAMS7:
            case BUTTRESS_REG_IDLE_WDT:
            case BUTTRESS_REG_FW_RESET_CTL:
            case BUTTRESS_REG_IPU_SKU:
            case BUTTRESS_REG_CG_CTRL_BITS:
            case BUTTRESS_REG_FW_GP24:
            case BUTTRESS_REG_FW_GP8:
                break;
            default:
                break;
            }
            val = s->bar4_regs[index];
            return val;
        }
        return 0;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size < 1 || size > 8) {
        return;
    }

    if (addr < IPU7_BAR0_MMIO_SIZE) {
        hwaddr index = addr >> 2;
        if (size == 4 && (addr & 3) == 0 && index < (IPU7_BAR0_MMIO_SIZE / 4)) {
            switch (addr) {
            case BUTTRESS_REG_IRQ_ENABLE:
                s->bar0_regs[index] = (uint32_t)val;
                pcibase_update_irq(s);
                break;
            case BUTTRESS_REG_IRQ_CLEAR:
                s->bar0_regs[index] = (uint32_t)val;
                s->buttress_irq_status &= ~((uint32_t)val);
                pcibase_update_irq(s);
                break;
            case BUTTRESS_REG_IRQ_MASK:
                s->bar0_regs[index] = (uint32_t)val;
                s->intr_mask = (uint32_t)val;
                pcibase_update_irq(s);
                break;
            case BUTTRESS_REG_FW_RESET_CTL:
                s->bar0_regs[index] = (uint32_t)val;
                if (val & BUTTRESS_FW_RESET_CTL_START) {
                    s->bar0_regs[index] |= BUTTRESS_FW_RESET_CTL_DONE;
                }
                break;
            case BUTTRESS_REG_SECURITY_CTL:
                s->bar0_regs[index] = (uint32_t)val;
                break;
            case BUTTRESS_REG_FW_SOURCE_BASE:
            case BUTTRESS_REG_FW_SOURCE_SIZE:
            case BUTTRESS_REG_CAMERA_MASK:
            case BUTTRESS_REG_CSE2IUDB0:
            case BUTTRESS_REG_CSE2IUCSR:
            case BUTTRESS_REG_IU2CSEDB0:
            case BUTTRESS_REG_IU2CSEDATA0:
            case BUTTRESS_REG_IU2CSECSR:
            case BUTTRESS_REG_FW_BOOT_PARAMS7:
            case BUTTRESS_REG_IDLE_WDT:
            case BUTTRESS_REG_IPU_SKU:
            case BUTTRESS_REG_CG_CTRL_BITS:
            case BUTTRESS_REG_FW_GP24:
            case BUTTRESS_REG_FW_GP8:
                s->bar0_regs[index] = (uint32_t)val;
                break;
            default:
                s->bar0_regs[index] = (uint32_t)val;
                break;
            }
        }
        return;
    }

    if (addr < IPU7_BAR4_MMIO_SIZE) {
        hwaddr index = addr >> 2;
        if (!(size == 4 && (addr & 3) == 0 && index < (IPU7_BAR4_MMIO_SIZE / 4))) {
            return;
        }

        switch (addr) {
        case GLOBAL_INTERRUPT_MASK:
            s->bar4_regs[index] = (uint32_t)val;
            break;
        case BAR2_MISC_CONFIG:
            s->bar4_regs[index] = (uint32_t)val;
            break;
        case TLBID_HASH_ENABLE_63_32:
        case TLBID_HASH_ENABLE_95_64:
        case TLBID_HASH_ENABLE_127_96:
            s->bar4_regs[index] = (uint32_t)val;
            break;
        case INTERRUPT_STATUS:
            s->pb_interrupt_status &= ~((uint32_t)val);
            s->bar4_regs[index] = s->pb_interrupt_status;
            break;
        case BTRS_LOCAL_INTERRUPT_MASK:
            s->pb_local_interrupt_mask = (uint32_t)val;
            s->bar4_regs[index] = (uint32_t)val;
            break;
        case ATS_ERROR_LOG2:
        case CFI_0_ERROR_LOG:
        case CFI_1_ERROR_LOGGING:
        case IMR_ERROR_LOGGING_LOW:
        case IMR_ERROR_LOGGING_HIGH:
        case IMR_ERROR_LOGGING_CFI_1_LOW:
        case IMR_ERROR_LOGGING_CFI_1_HIGH:
            s->bar4_regs[index] = (uint32_t)val;
            break;
        case BUTTRESS_REG_IRQ_ENABLE:
            s->bar4_regs[index] = (uint32_t)val;
            break;
        case BUTTRESS_REG_IRQ_MASK:
            s->bar4_regs[index] = (uint32_t)val;
            s->intr_mask = (uint32_t)val;
            break;
        case BUTTRESS_REG_IRQ_CLEAR:
            s->bar4_regs[index] &= ~(uint32_t)val;
            s->intr_status &= ~(uint32_t)val;
            pcibase_update_irq(s);
            break;
        case BUTTRESS_REG_IS_WORKPOINT_REQ:
        case BUTTRESS_REG_PS_WORKPOINT_REQ:
        case BUTTRESS_REG_SECURITY_CTL:
        case BUTTRESS_REG_FW_SOURCE_BASE:
        case BUTTRESS_REG_FW_SOURCE_SIZE:
        case BUTTRESS_REG_CAMERA_MASK:
        case BUTTRESS_REG_CSE2IUDB0:
        case BUTTRESS_REG_CSE2IUCSR:
        case BUTTRESS_REG_IU2CSEDB0:
        case BUTTRESS_REG_IU2CSEDATA0:
        case BUTTRESS_REG_IU2CSECSR:
        case BUTTRESS_REG_FW_BOOT_PARAMS7:
        case BUTTRESS_REG_IDLE_WDT:
        case BUTTRESS_REG_FW_RESET_CTL:
        case BUTTRESS_REG_IPU_SKU:
        case BUTTRESS_REG_CG_CTRL_BITS:
        case BUTTRESS_REG_FW_GP24:
        case BUTTRESS_REG_FW_GP8:
            s->bar4_regs[index] = (uint32_t)val;
            break;
        default:
            s->bar4_regs[index] = (uint32_t)val;
            break;
        }

        return;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;
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

    s->intr_status = 0;
    s->intr_mask = 0;
    s->hw_ver = 0;

    s->pb_interrupt_status = 0;
    s->pb_local_interrupt_mask = 0;
    s->buttress_irq_status = IPU7_IRQ_STATUS_DEFAULT;

    memset(s->bar0_regs, 0, sizeof(s->bar0_regs));
    memset(s->bar4_regs, 0, sizeof(s->bar4_regs));

    pcibase_update_irq(s);
}

static inline hwaddr pcibase_pow2ceil(hwaddr x)
{
    if (x <= 1) {
        return 1;
    }
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
#if HWADDR_BITS > 32
    x |= x >> 32;
#endif
    x++;
    return x;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    hwaddr aligned_size = pcibase_pow2ceil(bi->size);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  IPU7_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  IPU7_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, IPU7_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 2;
    s->bar_info[0].index = IPU_PCI_BAR;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = IPU7_BAR0_MMIO_SIZE;
    s->bar_info[0].name = "ipu7-bar0";

    s->bar_info[1].index = IPU_PCI_PBBAR;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = IPU7_BAR4_MMIO_SIZE;
    s->bar_info[1].name = "ipu7-bar4";

    for (int i = 2; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;
    s->intr_status = 0;
    s->intr_mask = 0;
    s->hw_ver = 0;

    s->pb_interrupt_status = 0;
    s->pb_local_interrupt_mask = 0;
    s->buttress_irq_status = IPU7_IRQ_STATUS_DEFAULT;

    memset(s->bar0_regs, 0, sizeof(s->bar0_regs));
    memset(s->bar4_regs, 0, sizeof(s->bar4_regs));

    pcibase_update_irq(s);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, 0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    (void)s;
}

static const VMStateDescription vmstate_pcibase = {
    .name = "intel_ipu7_pci",
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
