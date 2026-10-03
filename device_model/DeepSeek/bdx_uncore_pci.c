/*
 * This QEMU PCI device emulates the Intel uncore performance monitoring unit
 * for Broadwell EP (BDX) Home Agent (HA). It uses only PCI configuration space
 * registers, so no MMIO/PIO BARs are required.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "bdx_uncore_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs */
#define PCI_VENDOR_ID_INTEL 0x8086
#define PCI_DEVICE_ID_INTEL_UNC_HA 0x3c46

/* Register offset macros from driver */
#define SNBEP_CPUNODEID 0x40
#define SNBEP_GIDNIDMAP 0x54
#define SNBEP_PMON_BOX_CTL_RST_CTRL (1 << 0)
#define SNBEP_PMON_BOX_CTL_RST_CTRS (1 << 1)
#define SNBEP_PMON_BOX_CTL_FRZ (1 << 8)
#define SNBEP_PMON_BOX_CTL_FRZ_EN (1 << 16)
#define SNBEP_PMON_BOX_CTL_INT (SNBEP_PMON_BOX_CTL_RST_CTRL | \
                               SNBEP_PMON_BOX_CTL_RST_CTRS | \
                               SNBEP_PMON_BOX_CTL_FRZ_EN)
#define SNBEP_PMON_CTL_EV_SEL_MASK 0x000000ff
#define SNBEP_PMON_CTL_UMASK_MASK 0x0000ff00
#define SNBEP_PMON_CTL_RST (1 << 17)
#define SNBEP_PMON_CTL_EDGE_DET (1 << 18)
#define SNBEP_PMON_CTL_EV_SEL_EXT (1 << 21)
#define SNBEP_PMON_CTL_EN (1 << 22)
#define SNBEP_PMON_CTL_INVERT (1 << 23)
#define SNBEP_PMON_CTL_TRESH_MASK 0xff000000
#define SNBEP_PMON_RAW_EVENT_MASK (SNBEP_PMON_CTL_EV_SEL_MASK | \
                                  SNBEP_PMON_CTL_UMASK_MASK | \
                                  SNBEP_PMON_CTL_EDGE_DET | \
                                  SNBEP_PMON_CTL_INVERT | \
                                  SNBEP_PMON_CTL_TRESH_MASK)
#define SNBEP_U_MSR_PMON_CTL_TRESH_MASK 0x1f000000
#define SNBEP_U_MSR_PMON_RAW_EVENT_MASK \
    (SNBEP_PMON_CTL_EV_SEL_MASK | \
     SNBEP_PMON_CTL_UMASK_MASK | \
     SNBEP_PMON_CTL_EDGE_DET | \
     SNBEP_PMON_CTL_INVERT | \
     SNBEP_U_MSR_PMON_CTL_TRESH_MASK)
#define SNBEP_CBO_PMON_CTL_TID_EN (1 << 19)
#define SNBEP_CBO_MSR_PMON_RAW_EVENT_MASK (SNBEP_PMON_RAW_EVENT_MASK | \
                                           SNBEP_CBO_PMON_CTL_TID_EN)
#define SNBEP_PCU_MSR_PMON_CTL_OCC_SEL_MASK 0x0000c000
#define SNBEP_PCU_MSR_PMON_CTL_TRESH_MASK 0x1f000000
#define SNBEP_PCU_MSR_PMON_CTL_OCC_INVERT (1 << 30)
#define SNBEP_PCU_MSR_PMON_CTL_OCC_EDGE_DET (1 << 31)
#define SNBEP_PCU_MSR_PMON_RAW_EVENT_MASK \
    (SNBEP_PMON_CTL_EV_SEL_MASK | \
     SNBEP_PCU_MSR_PMON_CTL_OCC_SEL_MASK | \
     SNBEP_PMON_CTL_EDGE_DET | \
     SNBEP_PMON_CTL_INVERT | \
     SNBEP_PCU_MSR_PMON_CTL_TRESH_MASK | \
     SNBEP_PCU_MSR_PMON_CTL_OCC_INVERT | \
     SNBEP_PCU_MSR_PMON_CTL_OCC_EDGE_DET)
#define SNBEP_QPI_PCI_PMON_RAW_EVENT_MASK \
    (SNBEP_PMON_RAW_EVENT_MASK | \
     SNBEP_PMON_CTL_EV_SEL_EXT)
#define SNBEP_PCI_PMON_BOX_CTL 0xf4
#define SNBEP_PCI_PMON_CTL0 0xd8
#define SNBEP_PCI_PMON_CTR0 0xa0
#define SNBEP_HA_PCI_PMON_BOX_ADDRMATCH0 0x40
#define SNBEP_HA_PCI_PMON_BOX_ADDRMATCH1 0x44
#define SNBEP_HA_PCI_PMON_BOX_OPCODEMATCH 0x48
#define SNBEP_MC_CHy_PCI_PMON_FIXED_CTL 0xf0
#define SNBEP_MC_CHy_PCI_PMON_FIXED_CTR 0xd0
#define SNBEP_Q_Py_PCI_PMON_PKT_MATCH0 0x228
#define SNBEP_Q_Py_PCI_PMON_PKT_MATCH1 0x22c
#define SNBEP_Q_Py_PCI_PMON_PKT_MASK0 0x238
#define SNBEP_Q_Py_PCI_PMON_PKT_MASK1 0x23c

#define CLASS_ID (0xff << 8)

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Uncore PMU register state */
    uint32_t box_ctl;
    uint32_t event_ctl[4];
    uint64_t perf_ctr[4];
};

static uint32_t pcibase_pci_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint32_t val = pci_default_read_config(pdev, addr, len);

    /* Custom registers: counters 0xa0-0xbf, event control 0xd8-0xe7, box_ctl 0xf4 */
    if (addr >= 0xa0 && addr < 0xc0) {
        int idx = (addr - 0xa0) / 8;
        if (idx < 4) {
            if (len == 4) {
                uint32_t offset = (addr - 0xa0) % 8;
                if (offset == 0) {
                    val = (uint32_t)s->perf_ctr[idx];
                } else {
                    val = (uint32_t)(s->perf_ctr[idx] >> 32);
                }
            }
        }
    } else if (addr >= 0xd8 && addr < 0xe8) {
        int idx = (addr - 0xd8) / 4;
        if (idx < 4 && len == 4) {
            val = s->event_ctl[idx];
        }
    } else if (addr == 0xf4 && len == 4) {
        val = s->box_ctl;
    }

    return val;
}

static void pcibase_pci_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (addr >= 0xa0 && addr < 0xc0) {
        int idx = (addr - 0xa0) / 8;
        if (idx < 4) {
            if (len == 4) {
                uint32_t offset = (addr - 0xa0) % 8;
                if (offset == 0) {
                    s->perf_ctr[idx] &= 0xFFFFFFFF00000000ULL;
                    s->perf_ctr[idx] |= val;
                } else {
                    s->perf_ctr[idx] &= 0xFFFFFFFFULL;
                    s->perf_ctr[idx] |= ((uint64_t)val) << 32;
                }
            } else {
                pci_default_write_config(pdev, addr, val, len);
            }
        } else {
            pci_default_write_config(pdev, addr, val, len);
        }
    } else if (addr >= 0xd8 && addr < 0xe8) {
        int idx = (addr - 0xd8) / 4;
        if (idx < 4 && len == 4) {
            s->event_ctl[idx] = val;
        } else {
            pci_default_write_config(pdev, addr, val, len);
        }
    } else if (addr == 0xf4 && len == 4) {
        s->box_ctl = val;
        if (val & SNBEP_PMON_BOX_CTL_RST_CTRL) {
            memset(s->event_ctl, 0, sizeof(s->event_ctl));
        }
        if (val & SNBEP_PMON_BOX_CTL_RST_CTRS) {
            memset(s->perf_ctr, 0, sizeof(s->perf_ctr));
        }
    } else {
        pci_default_write_config(pdev, addr, val, len);
    }
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    s->box_ctl = 0;
    memset(s->event_ctl, 0, sizeof(s->event_ctl));
    memset(s->perf_ctr, 0, sizeof(s->perf_ctr));
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_INTEL_UNC_HA);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }
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

static const VMStateDescription vmstate_pcibase = {
    .name = "bdx_uncore_pci",
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
    k->config_read = pcibase_pci_config_read;
    k->config_write = pcibase_pci_config_write;
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
