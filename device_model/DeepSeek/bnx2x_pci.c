/*
 * QEMU emulation of Broadcom NetXtreme II BCM57710 PCI-E device.
 * Based on Linux driver bnx2x_main.c. Generated for QEMU 8.2.10.
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
#include <glib.h>

#define TYPE_PCIBASE_DEVICE "bnx2x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_BROADCOM 0x14e4
#define PCI_DEVICE_ID_NX2_57710 0x164e
#define PCI_CLASS_NETWORK_ETHERNET 0x0200

/* Register offsets extracted from bnx2x_main.c */
#define MISC_REG_CHIP_NUM                      0xa408
#define MISC_REG_CHIP_REV                      0xa40c
#define MISC_REG_BOND_ID                       0xa400
#define MISC_REG_PORT4MODE_EN                  0xa750
#define MISC_REG_PORT4MODE_EN_OVWR             0xa720
#define MISC_REG_SHARED_MEM_ADDR               0xa2b4
#define MISC_REG_GENERIC_CR_0                  0xa460
#define MISC_REG_GENERIC_CR_1                  0xa464
#define MISC_REG_GPIO                           0xa490
#define MISC_REG_GPIO_INT                       0xa494
#define MISC_REG_SPIO                           0xa4fc
#define MISC_REG_SPIO_INT                       0xa500
#define MISC_REG_AEU_GENERAL_ATTN_0            0xa000
#define MISC_REG_AEU_MASK_ATTN_FUNC_0          0xa060
#define MISC_REG_AEU_ENABLE1_FUNC_0_OUT_0      0xa06c
#define MISC_REG_AEU_AFTER_INVERT_1_FUNC_0     0xa42c
#define MISC_REG_AEU_AFTER_INVERT_2_FUNC_0     0xa438
#define MISC_REG_AEU_AFTER_INVERT_3_FUNC_0     0xa444
#define MISC_REG_AEU_AFTER_INVERT_4_FUNC_0     0xa450
#define MISC_REG_AEU_CLR_LATCH_SIGNAL          0xa45c
#define MISC_REG_GRC_TIMEOUT_ATTN              0xa3c4
#define MISC_REG_GRC_RSV_ATTN                  0xa3c0
#define MISC_REG_PCIE_HOT_RESET                0xa618
#define MISC_REG_RESET_REG_1                   0xa580
#define MISC_REG_RESET_REG_2                   0xa590
#define MISC_REGISTERS_RESET_REG_1_SET         0x584
#define MISC_REGISTERS_RESET_REG_1_CLEAR       0x588
#define MISC_REGISTERS_RESET_REG_2_SET         0x594
#define MISC_REGISTERS_RESET_REG_2_CLEAR       0x598
#define MISC_REG_GPIO_EVENT_EN                 0xa2bc
#define MISC_REG_SPIO_EVENT_EN                 0xa2b8
#define MISC_REG_CHIP_TYPE                     0xac60
#define HC_REG_CONFIG_0                        0x108000
#define HC_REG_CONFIG_1                        0x108004
#define HC_REG_COMMAND_REG                     0x108180
#define HC_REG_MAIN_MEMORY                     0x108800
#define HC_REG_MAIN_MEMORY_SIZE                152
#define HC_REG_INT_MASK                        0x108108
#define HC_REG_LEADING_EDGE_0                  0x108040
#define HC_REG_TRAILING_EDGE_0                 0x108044
#define HC_REG_ATTN_MSG0_ADDR_L                0x108018
#define IGU_REG_BLOCK_CONFIGURATION            0x130000
#define IGU_REG_RESET_MEMORIES                 0x130090
#define IGU_REG_MAPPING_MEMORY                 0x131000
#define IGU_REG_PF_CONFIGURATION               0x130154
#define NIG_REG_PORT_SWAP                      0x10394
#define NIG_REG_STRAP_OVERRIDE                 0x10398
#define PGLUE_B_REG_INTERNAL_PFID_ENABLE_TARGET_READ 0x9430
#define PBF_REG_DISABLE_PF                     0x1402e8
#define CFC_REG_WEAK_ENABLE_PF                 0x104124
#define CFC_REG_NUM_LCIDS_INSIDE_PF            0x104120
#define DORQ_REG_PF_USAGE_CNT                  0x1701d0
#define QM_REG_PF_USG_CNT_0                    0x16e040
#define TM_REG_LIN0_VNIC_UC                    0x164128
#define DMAE_REG_GO_C0                         0x102080
#define MISC_REG_WC0_CTRL_PHY_ADDR             0xa9cc
#define MISC_REG_AEU_GENERAL_MASK              0xa61c
#define MISC_REG_UNPREPARED                    0xa424
#define MISC_REG_AEU_GENERAL_ATTN_12           0xa030
#define MISC_REG_DRIVER_CONTROL_1              0xa510
#define MISC_REG_DRIVER_CONTROL_7              0xa3c8
#define MISC_REG_AEU_SYS_KILL_STATUS_0         0xa600
#define MISC_REG_AEU_MASK_ATTN_FUNC_1          0xa064
#define MISC_REG_AEU_ENABLE1_FUNC_1_OUT_0      0xa10c
#define MISC_REG_AEU_ENABLE1_FUNC_1_OUT_1      0xa11c
#define MISC_REG_AEU_ENABLE1_FUNC_1_OUT_2      0xa12c
#define MISC_REG_AEU_ENABLE4_FUNC_0_OUT_0      0xa078
#define MISC_REG_AEU_ENABLE4_FUNC_1_OUT_0      0xa118
#define MISC_REG_AEU_ENABLE4_NIG_0             0xa0f8
#define MISC_REG_AEU_ENABLE4_NIG_1             0xa198
#define MISC_REG_AEU_ENABLE4_PXP_0             0xa108
#define MISC_REG_AEU_ENABLE4_PXP_1             0xa1a8
#define MISC_REG_AEU_ENABLE5_FUNC_0_OUT_0      0xa688
#define MISC_REG_AEU_ENABLE5_FUNC_1_OUT_0      0xa6b0
#define MISC_REG_AEU_AFTER_INVERT_1_FUNC_1     0xa430
#define MISC_REG_AEU_AFTER_INVERT_2_FUNC_1     0xa43c
#define MISC_REG_AEU_AFTER_INVERT_3_FUNC_1     0xa448
#define MISC_REG_AEU_AFTER_INVERT_4_FUNC_1     0xa454
#define MISC_REG_AEU_AFTER_INVERT_5_FUNC_0     0xa700
#define MISC_REG_RECOVERY_GLOB_REG            0xa9a4
#define PCICFG_GRC_ADDRESS                    0x78
#define PCICFG_GRC_DATA                       0x7c
#define PCICFG_VENDOR_ID_OFFSET               0x00

#define BAR_IGU_INTMEM                        0x440000
#define BAR_USTRORM_INTMEM                    0x400000
#define BAR_CSTRORM_INTMEM                    0x410000
#define BAR_XSTRORM_INTMEM                    0x420000
#define BAR_TSTRORM_INTMEM                    0x430000

typedef struct {
    int     index;
    int     type; /* 0 = none, 1 = MMIO, 2 = PIO, 3 = RAM */
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
    GHashTable *regs; /* Register storage: addr -> value */
};

/* MMIO Read/Write Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    gpointer regval = g_hash_table_lookup(s->regs, GUINT_TO_POINTER(addr));
    if (regval) {
        val = GPOINTER_TO_UINT(regval);
    }
    if (size == 4) {
        return val;
    } else {
        qemu_log_mask(LOG_UNIMP, "bnx2x: unimplemented read size %u at addr 0x%" HWADDR_PRIx "\n", size, addr);
        return 0;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (size == 4) {
        g_hash_table_insert(s->regs, GUINT_TO_POINTER(addr), GUINT_TO_POINTER((guint)val));
    } else {
        qemu_log_mask(LOG_UNIMP, "bnx2x: unimplemented write size %u at addr 0x%" HWADDR_PRIx "\n", size, addr);
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset register hash table; clear all entries */
    g_hash_table_remove_all(s->regs);

    /* Set initial register values required for probe success */
    g_hash_table_insert(s->regs, GUINT_TO_POINTER(MISC_REG_CHIP_NUM), GUINT_TO_POINTER(0x0000164e));
    g_hash_table_insert(s->regs, GUINT_TO_POINTER(MISC_REG_CHIP_REV), GUINT_TO_POINTER(0));
    g_hash_table_insert(s->regs, GUINT_TO_POINTER(MISC_REG_BOND_ID), GUINT_TO_POINTER(0));
    g_hash_table_insert(s->regs, GUINT_TO_POINTER(MISC_REG_PORT4MODE_EN), GUINT_TO_POINTER(0));
    g_hash_table_insert(s->regs, GUINT_TO_POINTER(MISC_REG_PORT4MODE_EN_OVWR), GUINT_TO_POINTER(0));
    g_hash_table_insert(s->regs, GUINT_TO_POINTER(MISC_REG_SHARED_MEM_ADDR), GUINT_TO_POINTER(0));
    g_hash_table_insert(s->regs, GUINT_TO_POINTER(MISC_REG_GENERIC_CR_0), GUINT_TO_POINTER(0));
    g_hash_table_insert(s->regs, GUINT_TO_POINTER(MISC_REG_GENERIC_CR_1), GUINT_TO_POINTER(0));
    g_hash_table_insert(s->regs, GUINT_TO_POINTER(MISC_REG_CHIP_TYPE), GUINT_TO_POINTER(0));
    g_hash_table_insert(s->regs, GUINT_TO_POINTER(IGU_REG_BLOCK_CONFIGURATION), GUINT_TO_POINTER(0));
    g_hash_table_insert(s->regs, GUINT_TO_POINTER(HC_REG_CONFIG_0), GUINT_TO_POINTER(0));
    g_hash_table_insert(s->regs, GUINT_TO_POINTER(HC_REG_CONFIG_1), GUINT_TO_POINTER(0));
    g_hash_table_insert(s->regs, GUINT_TO_POINTER(HC_REG_COMMAND_REG), GUINT_TO_POINTER(0));
    g_hash_table_insert(s->regs, GUINT_TO_POINTER(HC_REG_INT_MASK), GUINT_TO_POINTER(0));
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == 0) {
        return;
    }
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == 1) { /* MMIO */
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == 2) { /* PIO */
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == 3) { /* RAM */
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    Error *local_err = NULL;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x14e4);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x164e);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0200);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, &local_err);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Initialize register hash table and initial values */
    s->regs = g_hash_table_new(g_direct_hash, g_direct_equal);
    /* Set initial values via reset */
    pcibase_reset(DEVICE(s));

    /* BAR configuration */
    s->bar_info[0] = (BARInfo){ .index = 0, .type = 1, .size = 1 * MiB, .name = "bnx2x-mmio" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = 3, .size = 0x20000, .name = "bnx2x-doorbell" };
    s->bar_info[4] = (BARInfo){ .index = 4, .type = 3, .size = 4096, .name = "bnx2x-msix" };
    s->num_bars = 3;

    /* Register all BARs before MSI-X initialization */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (*errp) return;
    }

    /* MSI-X setup: allocate 1 vector */
    if (msix_init(pdev, 1, &s->bar_regions[4], 4, 0, &s->bar_regions[4], 4, 0x800, 0x50, &local_err)) {
        error_propagate(errp, local_err);
        return;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    g_hash_table_destroy(s->regs);
}

static const VMStateDescription vmstate_pcibase = {
    .name = "bnx2x_pci",
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
