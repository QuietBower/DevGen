
/*
 * QEMU Intel QAT 6xxx virtual device model
 * Based on Linux driver qat_6xxx/adf_drv.c
 * QEMU 8.2.10
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "6xxx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Registers and identifiers from driver source */
#define VENDOR_ID 0x8086
#define PCI_DEVICE_ID_INTEL_QAT_6XXX 0x4948
#define QAT_DEVICE_CLASS 0x0b4000

#define ADF_GEN6_FUSECTL1_OFFSET      0x2CC
#define ADF_GEN6_FUSECTL4_OFFSET      0x2D8
#define ADF_GEN6_FUSECTL0_OFFSET      0x2C8
#define ADF_GEN6_ERRMSK2              0x41A218
#define ADF_GEN6_ERRMSK3              0x41A21C
#define ADF_GEN6_SMIAPF_MASK_OFFSET   0x41A084
#define ADF_GEN6_SMIAPF_RP_X0_MASK_OFFSET 0x41A040
#define ADF_GEN6_SMIAPF_RP_X1_MASK_OFFSET 0x41A044
#define ADF_GEN6_MSIX_RTTABLE_OFFSET(i)   (0x409000 + ((i) * 4))
#define ADF_GEN6_ADMINMSGUR_OFFSET    0x500574
#define ADF_GEN6_ADMINMSGLR_OFFSET    0x500578
#define ADF_GEN6_MAILBOX_BASE_OFFSET  0x600970
#define ADF_GEN6_ARB_OFFSET           0x000
#define ADF_GEN6_ARB_WRK_2_SER_MAP_OFFSET 0x400
#define ADF_WQM_CSR_RPRESETCTL(bank)  (0x6000 + (bank) * 8)
#define ADF_WQM_CSR_RPRESETSTS(bank)  (ADF_WQM_CSR_RPRESETCTL(bank) + 4)
#define ADF_GEN6_CSR_RINGMODECTL(bank) (0x9000 + (bank) * 4)

/* Newly added registers */
#define ADF_GEN6_PM_INTERRUPT          0x50A028
#define ADF_GEN6_PM_STATUS             0x50A00C
#define ADF_GEN6_PM_DRV_ACTIVE         BIT(20)
#define ADF_GEN6_PM_INIT_STATE         BIT(21)
#define ADF_GEN6_PM_POLL_DELAY_US      20
#define ADF_GEN6_PM_POLL_TIMEOUT_US    USEC_PER_SEC
#define ADF_GEN6_PM_SOU                BIT(18)

#define BAR0_SIZE 0x400000   /* 4 MB SRAM */
#define BAR2_SIZE 0x800000   /* 8 MB PMISC MMIO */
#define BAR4_SIZE 0x800000   /* 8 MB ETR MMIO */

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion sram;        /* BAR0 */
    MemoryRegion pmisc_mmio;  /* BAR2 */
    MemoryRegion etr_mmio;    /* BAR4 */

    uint32_t *pmisc_regs;
    uint32_t *etr_regs;
    uint64_t admin_phy_addr;
};

static uint64_t pcibase_pmisc_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t offset = addr >> 2;
    if (offset < BAR2_SIZE / 4) {
        return s->pmisc_regs[offset];
    }
    qemu_log_mask(LOG_GUEST_ERROR, "%s: out of bounds read at 0x%"PRIx64"\n", __func__, addr);
    return 0;
}

static void pcibase_pmisc_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t offset = addr >> 2;
    if (offset >= BAR2_SIZE / 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out of bounds write at 0x%"PRIx64"\n", __func__, addr);
        return;
    }
    s->pmisc_regs[offset] = val;

    if (addr == ADF_GEN6_ADMINMSGUR_OFFSET) {
        s->admin_phy_addr = (s->admin_phy_addr & 0xFFFFFFFFULL) | ((uint64_t)val << 32);
    } else if (addr == ADF_GEN6_ADMINMSGLR_OFFSET) {
        s->admin_phy_addr = (s->admin_phy_addr & ~0xFFFFFFFFULL) | val;
    }
}

static const MemoryRegionOps pcibase_pmisc_ops = {
    .read = pcibase_pmisc_read,
    .write = pcibase_pmisc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static uint64_t pcibase_etr_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t offset = addr >> 2;
    if (offset < BAR4_SIZE / 4) {
        return s->etr_regs[offset];
    }
    qemu_log_mask(LOG_GUEST_ERROR, "%s: out of bounds read at 0x%"PRIx64"\n", __func__, addr);
    return 0;
}

static void pcibase_etr_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t offset = addr >> 2;
    if (offset >= BAR4_SIZE / 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out of bounds write at 0x%"PRIx64"\n", __func__, addr);
        return;
    }
    s->etr_regs[offset] = val;

    if (addr >= 0x6000 && addr < 0x6000 + 64 * 8) {
        uint32_t bank = (addr - 0x6000) / 8;
        if (addr == 0x6000 + bank * 8) {
            if (val & 1) {
                s->etr_regs[(0x6004 + bank * 8) >> 2] |= 1;
            }
        }
    }
}

static const MemoryRegionOps pcibase_etr_ops = {
    .read = pcibase_etr_read,
    .write = pcibase_etr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    /* Initialize hardware state visible to driver */
    if (s->pmisc_regs) {
        memset(s->pmisc_regs, 0, BAR2_SIZE);
    }
    if (s->etr_regs) {
        memset(s->etr_regs, 0, BAR4_SIZE);
    }
    s->admin_phy_addr = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_INTEL_QAT_6XXX);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, QAT_DEVICE_CLASS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR0: SRAM (32-bit) */
    memory_region_init_ram(&s->sram, OBJECT(s), "qat-sram", BAR0_SIZE, errp);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->sram);

    /* BAR2: PMISC MMIO (32-bit) */
    s->pmisc_regs = g_malloc0(BAR2_SIZE);
    memory_region_init_io(&s->pmisc_mmio, OBJECT(s), &pcibase_pmisc_ops, s, "qat-pmisc", BAR2_SIZE);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->pmisc_mmio);

    /* BAR4: ETR MMIO (32-bit) */
    s->etr_regs = g_malloc0(BAR4_SIZE);
    memory_region_init_io(&s->etr_mmio, OBJECT(s), &pcibase_etr_ops, s, "qat-etr", BAR4_SIZE);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->etr_mmio);

    /* MSI-X: use an exclusive BAR for the table and PBA */
    if (msix_init_exclusive_bar(pdev, 65, 1, errp)) {
        return;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (s->pmisc_regs) {
        g_free(s->pmisc_regs);
    }
    if (s->etr_regs) {
        g_free(s->etr_regs);
    }
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "6xxx_pci",
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

type_init(pcibase_register_types)
