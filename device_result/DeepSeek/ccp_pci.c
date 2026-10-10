/*
 * QEMU AMD CCP/PSP PCI device emulation (functional implementation)
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

#define TYPE_PCIBASE_DEVICE "ccp_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_AMD       0x1022
#define PCI_DEVICE_ID_CCP_0     0x1537
#define PCI_CLASS_ID            0x108000  /* Encryption/decryption controller */

/* BAR definitions */
#define SP_BAR_INDEX            2
#define BAR2_SIZE               0x40000  /* 256KB to cover PSP/CCP registers */

/* Register offsets from driver */
#define CMD5_REQID_CONFIG_OFFSET   0x08
#define CMD5_AES_MASK_OFFSET       0x6010
#define LSB_PRIVATE_MASK_LO_OFFSET 0x20
#define LSB_PRIVATE_MASK_HI_OFFSET 0x24
#define CMD5_CMD_TIMEOUT_OFFSET    0x10
#define CMD5_CLK_GATE_CTL_OFFSET   0x603C
#define CMD5_TRNG_CTL_OFFSET       0x6008
#define CMD5_QUEUE_PRIO_OFFSET     0x04
#define TRNG_OUT_REG               0x00c
#define CMD5_CONFIG_0_OFFSET       0x6000
#define CMD5_QUEUE_MASK_OFFSET     0x00
#define CMD_Q_STATUS_BASE          0x210
#define CMD_Q_INT_STATUS_BASE      0x214
#define IRQ_STATUS_REG             0x200
#define IRQ_MASK_REG               0x040
#define Q_MASK_REG                 0x000
#define CMD_Q_CACHE_BASE           0x228
#define CMD5_Q_HEAD_LO_BASE        0x0008
#define CMD5_Q_TAIL_LO_BASE        0x0004
#define CMD5_Q_INTERRUPT_STATUS_BASE 0x0010
#define LSB_PUBLIC_MASK_HI_OFFSET  0x1C
#define LSB_PUBLIC_MASK_LO_OFFSET  0x18
#define CMD5_Q_STATUS_BASE         0x0100
#define CMD5_Q_INT_STATUS_BASE     0x0104
#define CMD5_Q_INT_ENABLE_BASE     0x000C
#define CMD5_Q_DMA_STATUS_BASE     0x0108
#define CMD5_Q_DMA_WRITE_STATUS_BASE 0x0110
#define CMD5_Q_DMA_READ_STATUS_BASE  0x010C
#define CMD_REQ0                    0x180
#define DEL_CMD_Q_JOB               0x124
#define CMD_REQ_INCR                0x04
#define CMD_Q_CACHE_INC             0x20
#define CMD_Q_STATUS_INCR           0x20
#define CMD5_Q_STATUS_INCR          0x1000
#define CMD5_Q_SHIFT                3
#define KSB_START                   77
#define KSB_END                     127
#define LSB_SIZE                    16
#define MAX_LSB_CNT                 8
#define COMMANDS_PER_QUEUE          16
#define CCP_SB_BYTES                32
#define MAX_HW_QUEUES               5
#define SP_MAX_NAME_LEN             32
#define MSIX_VECTORS                2
#define SUPPORTED_INTERRUPTS        (INT_COMPLETION | INT_ERROR)
#define INT_COMPLETION              0x1
#define INT_ERROR                   0x2

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

    /* Hardware Register Shadows */
    uint32_t regs[BAR2_SIZE / sizeof(uint32_t)];

    /* DMA Context - unused, driver does not expose DMA in sp-pci.c */

    /* Operational status flags */

    /* State used to handle reset sequences */

    /* Power management state (D0-D3) */

    /* Other additions */
};

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* All reads are serviced from the register shadow array.
     * Accesses outside the BAR range are impossible due to memory region sizing.
     */
    if ((addr + size) > BAR2_SIZE) {
        return ~0ULL;
    }

    /* For simplicity, return the full 32-bit value at the 4-byte-aligned address.
     * The driver only performs 32-bit reads (ioread32).
     */
    if (size == 4) {
        val = s->regs[addr / sizeof(uint32_t)];
    } else if (size == 8) {
        val = (uint64_t)s->regs[addr / sizeof(uint32_t)] |
              ((uint64_t)s->regs[addr / sizeof(uint32_t) + 1] << 32);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported read size %u at 0x" HWADDR_FMT_plx "\n",
                      __func__, size, addr);
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if ((addr + size) > BAR2_SIZE) {
        return;
    }

    /* Write to the register shadow. The driver uses writel (32-bit writes). */
    if (size == 4) {
        s->regs[addr / sizeof(uint32_t)] = (uint32_t)val;
    } else if (size == 8) {
        s->regs[addr / sizeof(uint32_t)] = (uint32_t)(val & 0xFFFFFFFF);
        s->regs[addr / sizeof(uint32_t) + 1] = (uint32_t)(val >> 32);
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported write size %u at 0x" HWADDR_FMT_plx "\n",
                      __func__, size, addr);
    }

    /* No side effects are implemented yet; future register-specific logic goes here. */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* PIO is not used by this driver, so return all-ones. */
    return ~0ULL;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PIO is not used, ignore. */
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

    /* Reset all register shadows to zero. */
    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;

    /* Reset MSI-X configuration if necessary (handled by PCI core). */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_AMD);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_CCP_0);
    pci_config_set_class(pci_conf, PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = SP_BAR_INDEX;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR2_SIZE;
    s->bar_info[0].name = "ccp-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X initialization */
    Error *local_err = NULL;
    int ret = msix_init(pdev, MSIX_VECTORS,
                        &s->bar_regions[SP_BAR_INDEX], SP_BAR_INDEX, 0x10000,
                        &s->bar_regions[SP_BAR_INDEX], SP_BAR_INDEX, 0x11000,
                        0, &local_err);
    if (ret < 0) {
        error_report_err(local_err);
        return;
    }

    /* DMA not initialized (no evidence in driver code) */

    /* Register shadow already zero-initialized. */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->bar_regions[SP_BAR_INDEX], &s->bar_regions[SP_BAR_INDEX]);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ccp_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32_ARRAY(regs, PCIBaseState, BAR2_SIZE / sizeof(uint32_t)),
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
