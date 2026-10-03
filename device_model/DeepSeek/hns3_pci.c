/* This template provides a robust skeleton for hardware emulation.
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
/* Deleted: #HeadFile# */

#define TYPE_PCIBASE_DEVICE "hns3_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* #Related_Config_Info# */
#define PCI_VENDOR_ID_HUAWEI 0x19e5  /* from Linux pci_ids.h, standard */
#define HNAE3_DEV_ID_GE      0xA220  /* defined in driver source */
#define CLASS_ID             PCI_CLASS_NETWORK_ETHERNET

/* Register macros from hns3_enet.c */
#define HNS3_RING_EN_B                  0
#define HNS3_RING_EN_REG                0x00090
#define HNS3_RING_TX_RING_TAIL_REG      0x00058
#define HNS3_RING_TX_RING_BD_ERR_REG    0x00074
#define HNS3_RING_TX_RING_FBDNUM_REG    0x00060
#define HNS3_RING_TX_RING_TC_REG        0x00050
#define HNS3_RING_TX_RING_OFFSET_REG    0x00064
#define HNS3_RING_TX_RING_HEAD_REG      0x0005C
#define HNS3_RING_TX_RING_EBDNUM_REG    0x00068
#define HNS3_RING_TX_RING_BD_NUM_REG    0x00048
#define HNS3_RING_TX_RING_EBD_OFFSET_REG 0x00070
#define HNS3_RING_RX_RING_HEAD_REG      0x0001C
#define HNS3_RING_TX_RING_BASEADDR_H_REG 0x00044
#define HNS3_RING_TX_RING_BASEADDR_L_REG 0x00040
#define HNS3_RING_RX_RING_BASEADDR_L_REG 0x00000
#define HNS3_RING_RX_RING_BASEADDR_H_REG 0x00004
#define HNS3_RING_RX_RING_BD_LEN_REG    0x0000C
#define HNS3_RING_RX_RING_BD_NUM_REG    0x00008
#define HNS3_GL1_CQ_MODE_REG            0x20d04
#define HNS3_GL0_CQ_MODE_REG            0x20d00
#define HNS3_VECTOR_RL_OFFSET           0x900
#define HNS3_INT_RL_ENABLE_MASK         0x40
#define HNS3_VECTOR_GL0_OFFSET          0x100
#define HNS3_VECTOR_GL1_OFFSET          0x200
#define HNS3_VECTOR_TX_QL_OFFSET        0xe00
#define HNS3_VECTOR_RX_QL_OFFSET        0xf00
#define HNS3_INT_GL_50K                 0x0014
#define HNS3_INT_QL_DEFAULT_CFG         0x20
#define HNS3_BD_SIZE_512_TYPE           0
#define HNS3_BD_SIZE_4096_TYPE          3
#define HNS3_BD_SIZE_2048_TYPE          2
#define HNS3_BD_SIZE_1024_TYPE          1

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
    /* #Interrupt_Stru# */
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    /* #Reg_Stru# */
    uint32_t *regs;

    /* DMA Context */
    /* #DMA_Info_Stru# */ /* Deleted */

    /* #Status_Stru# */
    uint32_t status;

    /* #Probe_Reset_Stru# */
    bool reset_pending;

    /* #Pow_Man_Stru# */
    uint32_t pm_state;

    /* #Other_Addition_Info_Stru# */
    uint32_t caps;

    /* MSI-X specific */
    MemoryRegion msix_mmio;
};

/* #Other_Addition_Info_Defin# */ /* Deleted */

#define REGION_SIZE 0x100000  /* 1 MiB for all registers */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < REGION_SIZE && (addr & 3) == 0) {
        val = s->regs[addr >> 2];
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < REGION_SIZE && (addr & 3) == 0) {
        s->regs[addr >> 2] = (uint32_t)val;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* dummy ops for MSI-X BAR, actual access handled by QEMU's MSI-X emulation */
static uint64_t pcibase_msix_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_msix_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static const MemoryRegionOps pcibase_msix_ops = {
    .read = pcibase_msix_read,
    .write = pcibase_msix_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* #Reset_Func# */
    if (s->regs) {
        memset(s->regs, 0, REGION_SIZE);
    }
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
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_HUAWEI);
    pci_set_word(pci_conf + PCI_DEVICE_ID, HNAE3_DEV_ID_GE);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization */
    /* #BAR_CONFIG_INIT# */
    s->num_bars = 2;

    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = REGION_SIZE;
    s->bar_info[0].name = "hns3-mmio";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x1000;
    s->bar_info[1].name = "hns3-msix";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* #MSI_OR_MSIX_INIT# */
    memory_region_init_io(&s->msix_mmio, OBJECT(s), &pcibase_msix_ops, s, "hns3-msix", 0x1000);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->msix_mmio);
    msix_init(pdev, 32, &s->msix_mmio, 1, 0, &s->msix_mmio, 1, 0x800, 0, errp);

    /* #DMA_Config_Real# */ /* Deleted */
    /* #Timer_Config_Real# */ /* Deleted */
    /* #Field_Init_Real# */
    s->regs = g_malloc0(REGION_SIZE);
    s->reset_pending = false;
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
    g_free(s->regs);
    /* #Uninit_Func# */ /* Deleted */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "hns3_pci",
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
