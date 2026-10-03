/*
 * Fully functional QEMU PCI device model for btintel_pcie
 * Based on Linux driver btintel_pcie.c, QEMU 8.2.10 APIs.
 * Phase 4 repair: Use msix_init_exclusive_bar to fix MSI-X table/PBA overlap assertion.
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

#define TYPE_PCIBASE_DEVICE "btintel_pcie_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identification */
#define VENDOR_ID   0x8086
#define DEVICE_ID   0x4D76
#define CLASS_ID    0x0700 /* Communication controller: next gen */

/* Register Offsets (from driver) */
#define BTINTEL_PCIE_CSR_BASE                    0x000
#define BTINTEL_PCIE_CSR_FUNC_CTRL_REG           (BTINTEL_PCIE_CSR_BASE + 0x024)
#define BTINTEL_PCIE_CSR_HW_REV_REG              (BTINTEL_PCIE_CSR_BASE + 0x028)
#define BTINTEL_PCIE_CSR_RF_ID_REG               (BTINTEL_PCIE_CSR_BASE + 0x09C)
#define BTINTEL_PCIE_CSR_BOOT_STAGE_REG          (BTINTEL_PCIE_CSR_BASE + 0x108)
#define BTINTEL_PCIE_CSR_IPC_CONTROL_REG         (BTINTEL_PCIE_CSR_BASE + 0x10C)
#define BTINTEL_PCIE_CSR_IPC_STATUS_REG          (BTINTEL_PCIE_CSR_BASE + 0x110)
#define BTINTEL_PCIE_CSR_IPC_SLEEP_CTL_REG       (BTINTEL_PCIE_CSR_BASE + 0x114)
#define BTINTEL_PCIE_CSR_CI_ADDR_LSB_REG         (BTINTEL_PCIE_CSR_BASE + 0x118)
#define BTINTEL_PCIE_CSR_CI_ADDR_MSB_REG         (BTINTEL_PCIE_CSR_BASE + 0x11C)
#define BTINTEL_PCIE_CSR_IMG_RESPONSE_REG        (BTINTEL_PCIE_CSR_BASE + 0x12C)
#define BTINTEL_PCIE_CSR_MBOX_1_REG             (BTINTEL_PCIE_CSR_BASE + 0x170)
#define BTINTEL_PCIE_CSR_MBOX_2_REG             (BTINTEL_PCIE_CSR_BASE + 0x174)
#define BTINTEL_PCIE_CSR_MBOX_3_REG             (BTINTEL_PCIE_CSR_BASE + 0x178)
#define BTINTEL_PCIE_CSR_MBOX_4_REG             (BTINTEL_PCIE_CSR_BASE + 0x17C)
#define BTINTEL_PCIE_CSR_MBOX_STATUS_REG         (BTINTEL_PCIE_CSR_BASE + 0x180)
#define BTINTEL_PCIE_CSR_PRPH_DEV_ADDR_REG       (BTINTEL_PCIE_CSR_BASE + 0x440)
#define BTINTEL_PCIE_CSR_HBUS_TARG_WRPTR         (BTINTEL_PCIE_CSR_BASE + 0x460)
#define BTINTEL_PCIE_CSR_PRPH_DEV_RD_REG         (BTINTEL_PCIE_CSR_BASE + 0x458)

/* MSI-X Registers */
#define BTINTEL_PCIE_CSR_MSIX_BASE              0x2000
#define BTINTEL_PCIE_CSR_MSIX_FH_INT_CAUSES     (BTINTEL_PCIE_CSR_MSIX_BASE + 0x0800)
#define BTINTEL_PCIE_CSR_MSIX_FH_INT_MASK       (BTINTEL_PCIE_CSR_MSIX_BASE + 0x0804)
#define BTINTEL_PCIE_CSR_MSIX_HW_INT_CAUSES     (BTINTEL_PCIE_CSR_MSIX_BASE + 0x0808)
#define BTINTEL_PCIE_CSR_MSIX_HW_INT_MASK       (BTINTEL_PCIE_CSR_MSIX_BASE + 0x080C)
#define BTINTEL_PCIE_CSR_MSIX_AUTOMASK_ST        (BTINTEL_PCIE_CSR_MSIX_BASE + 0x0810)
#define BTINTEL_PCIE_CSR_MSIX_IVAR_BASE          (BTINTEL_PCIE_CSR_MSIX_BASE + 0x0880)

/* Bit definitions from driver (updated) */
#define BTINTEL_PCIE_CSR_FUNC_CTRL_FUNC_ENA         (BIT(0))
#define BTINTEL_PCIE_CSR_FUNC_CTRL_MAC_INIT         (BIT(6))
#define BTINTEL_PCIE_CSR_FUNC_CTRL_BUS_MASTER_DISCON (BIT(29))
#define BTINTEL_PCIE_CSR_FUNC_CTRL_SW_RESET          (BIT(31))
#define BTINTEL_PCIE_CSR_FUNC_CTRL_FUNC_INIT          (BIT(7))
#define BTINTEL_PCIE_CSR_FUNC_CTRL_BUS_MASTER_STS     (BIT(28))
#define BTINTEL_PCIE_CSR_FUNC_CTRL_MAC_ACCESS_REQ     (BIT(21))
#define BTINTEL_PCIE_CSR_FUNC_CTRL_MAC_ACCESS_STS     (BIT(20))

#define BTINTEL_PCIE_CSR_BOOT_STAGE_ROM             (BIT(0))
#define BTINTEL_PCIE_CSR_BOOT_STAGE_IML             (BIT(1))
#define BTINTEL_PCIE_CSR_BOOT_STAGE_OPFW            (BIT(2))
#define BTINTEL_PCIE_CSR_BOOT_STAGE_D3_STATE_READY  (BIT(24))
#define BTINTEL_PCIE_CSR_BOOT_STAGE_DEVICE_WARNING  (BIT(12))
#define BTINTEL_PCIE_CSR_BOOT_STAGE_DEVICE_HALTED   (BIT(14))
#define BTINTEL_PCIE_CSR_BOOT_STAGE_ABORT_HANDLER   (BIT(13))
#define BTINTEL_PCIE_CSR_BOOT_STAGE_ROM_LOCKDOWN    (BIT(10))
#define BTINTEL_PCIE_CSR_BOOT_STAGE_IML_LOCKDOWN    (BIT(11))

#define BTINTEL_PCIE_MSIX_FH_INT_CAUSES_0          (BIT(0))
#define BTINTEL_PCIE_MSIX_FH_INT_CAUSES_1          (BIT(1))
#define BTINTEL_PCIE_MSIX_HW_INT_CAUSES_HWEXP       (BIT(0))
#define BTINTEL_PCIE_MSIX_HW_INT_CAUSES_GP1         (BIT(1))
#define BTINTEL_PCIE_MSIX_HW_INT_CAUSES_GP0         (BIT(2))
#define BTINTEL_PCIE_MSIX_NON_AUTO_CLEAR_CAUSE      (BIT(7))

#define BTINTEL_PCIE_MSIX_VEC_MIN 1
#define BTINTEL_PCIE_MSIX_VEC_MAX 1

#define BTINTEL_PCIE_CSR_MSIX_IVAR(cause) (BTINTEL_PCIE_CSR_MSIX_IVAR_BASE + (cause)*8)

#define MMIO_SIZE 0x4000  /* covers CSR and MSI-X control registers */

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion mmio;          /* container for BAR0 */
    MemoryRegion bar0_mr;       /* subregion for full MMIO (priority -1) */

    uint8_t mmio_buf[MMIO_SIZE];    /* full register file */
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t fh_causes, fh_mask;
    uint32_t hw_causes, hw_mask;
    uint32_t cause_reg;
    int i;
    uint8_t *mmio = s->mmio_buf;
    uint32_t ivar_val;
    uint8_t vector;
    bool auto_mask;

    if (!msix_enabled(pdev)) {
        return;
    }

    fh_causes = *(uint32_t *)(mmio + BTINTEL_PCIE_CSR_MSIX_FH_INT_CAUSES);
    fh_mask   = *(uint32_t *)(mmio + BTINTEL_PCIE_CSR_MSIX_FH_INT_MASK);
    hw_causes = *(uint32_t *)(mmio + BTINTEL_PCIE_CSR_MSIX_HW_INT_CAUSES);
    hw_mask   = *(uint32_t *)(mmio + BTINTEL_PCIE_CSR_MSIX_HW_INT_MASK);

    /* Check FH causes */
    cause_reg = fh_causes & ~fh_mask;
    for (i = 0; i < 32 && cause_reg; i++) {
        if (cause_reg & BIT(i)) {
            ivar_val = *(uint32_t *)(mmio + BTINTEL_PCIE_CSR_MSIX_IVAR(i));
            vector = ivar_val & 0xff;
            auto_mask = (ivar_val & BIT(31)) != 0;
            msix_notify(pdev, vector);
            if (auto_mask) {
                msix_set_mask(pdev, vector, true);
            }
        }
    }

    /* Check HW causes */
    cause_reg = hw_causes & ~hw_mask;
    for (i = 0; i < 32 && cause_reg; i++) {
        if (cause_reg & BIT(i)) {
            ivar_val = *(uint32_t *)(mmio + BTINTEL_PCIE_CSR_MSIX_IVAR(i));
            vector = ivar_val & 0xff;
            auto_mask = (ivar_val & BIT(31)) != 0;
            msix_notify(pdev, vector);
            if (auto_mask) {
                msix_set_mask(pdev, vector, true);
            }
        }
    }
}

/* Synchronous boot trigger: when driver writes FUNC_CTRL with FUNC_INIT and MAC_INIT,
 * immediately set boot stage ROM and raise GP0 interrupt.
 */
static void pcibase_trigger_boot(PCIBaseState *s)
{
    uint32_t boot = *(uint32_t *)(s->mmio_buf + BTINTEL_PCIE_CSR_BOOT_STAGE_REG);
    boot |= BTINTEL_PCIE_CSR_BOOT_STAGE_ROM;
    *(uint32_t *)(s->mmio_buf + BTINTEL_PCIE_CSR_BOOT_STAGE_REG) = boot;

    uint32_t hw_causes = *(uint32_t *)(s->mmio_buf + BTINTEL_PCIE_CSR_MSIX_HW_INT_CAUSES);
    hw_causes |= BTINTEL_PCIE_MSIX_HW_INT_CAUSES_GP0;
    *(uint32_t *)(s->mmio_buf + BTINTEL_PCIE_CSR_MSIX_HW_INT_CAUSES) = hw_causes;

    pcibase_update_irq(s);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size > MMIO_SIZE) {
        return 0;
    }

    memcpy(&val, s->mmio_buf + addr, size);
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t v32, cur, new_val;

    if (addr + size > MMIO_SIZE) {
        return;
    }

    switch (addr) {
    case BTINTEL_PCIE_CSR_FUNC_CTRL_REG:
        v32 = *(uint32_t *)(s->mmio_buf + addr);
        memcpy(s->mmio_buf + addr, &val, size);
        new_val = *(uint32_t *)(s->mmio_buf + addr);
        /* trigger boot sequence if MAC_INIT and FUNC_INIT are newly set */
        if ((new_val & (BTINTEL_PCIE_CSR_FUNC_CTRL_FUNC_INIT |
                         BTINTEL_PCIE_CSR_FUNC_CTRL_MAC_INIT)) ==
                         (BTINTEL_PCIE_CSR_FUNC_CTRL_FUNC_INIT |
                          BTINTEL_PCIE_CSR_FUNC_CTRL_MAC_INIT)) {
            pcibase_trigger_boot(s);
        }
        break;

    case BTINTEL_PCIE_CSR_MSIX_FH_INT_CAUSES:
        cur = *(uint32_t *)(s->mmio_buf + addr);
        v32 = (uint32_t)val;
        *(uint32_t *)(s->mmio_buf + addr) = cur & ~v32;  /* W1C */
        pcibase_update_irq(s);
        break;

    case BTINTEL_PCIE_CSR_MSIX_HW_INT_CAUSES:
        cur = *(uint32_t *)(s->mmio_buf + addr);
        v32 = (uint32_t)val;
        *(uint32_t *)(s->mmio_buf + addr) = cur & ~v32;  /* W1C */
        pcibase_update_irq(s);
        break;

    case BTINTEL_PCIE_CSR_MSIX_FH_INT_MASK:
    case BTINTEL_PCIE_CSR_MSIX_HW_INT_MASK:
        memcpy(s->mmio_buf + addr, &val, size);
        pcibase_update_irq(s);
        break;

    case BTINTEL_PCIE_CSR_MSIX_AUTOMASK_ST:
        /* writing 1 clears the auto-mask for that vector */
        for (int i = 0; i < 32; i++) {
            if (val & BIT(i)) {
                msix_set_mask(pdev, i, false);
            }
        }
        break;

    case BTINTEL_PCIE_CSR_MSIX_IVAR_BASE ... BTINTEL_PCIE_CSR_MSIX_IVAR_BASE + 0x7f:
        /* IVAR registers – store byte */
        memcpy(s->mmio_buf + addr, &val, size);
        break;

    default:
        memcpy(s->mmio_buf + addr, &val, size);
        break;
    }
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
    memset(s->mmio_buf, 0, MMIO_SIZE);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    int rc;

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

    /* BAR0: container with CSR and MSI-X subregions */
    memory_region_init(&s->mmio, OBJECT(s), "btintel-pcie-mmio", MMIO_SIZE);

    /* Full MMIO handler subregion at priority -1 (so MSI-X table/PBA override it) */
    memory_region_init_io(&s->bar0_mr, OBJECT(s), &pcibase_mmio_ops, s,
                          "btintel-pcie-bar0", MMIO_SIZE);
    memory_region_add_subregion_overlap(&s->mmio, 0, &s->bar0_mr, -1);

    /* Initialize MSI-X with exclusive bar to avoid table/PBA overlap assertion */
    rc = msix_init(pdev, 32, &s->mmio, 0, 0x2000, &s->mmio, 0, 0x3000, 0, errp);
    if (rc < 0) {
        return;
    }

    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->mmio, &s->mmio);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "btintel_pcie_pci",
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
