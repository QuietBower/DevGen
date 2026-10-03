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


#define TYPE_PCIBASE_DEVICE "skfddi_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_SK 0x1148
#define PCI_DEVICE_ID_SK_FP 0x4000

#define FP_IO_LEN 256

#define B0_CTRL 0x0004
#define B0_LED 0x0006
#define B0_TST_CTRL 0x0007
#define B0_ISRC 0x0008
#define B0_IMSK 0x000c
#define B0_ST1U 0x0010
#define B0_ST1L 0x0014
#define B0_ST2U 0x0018
#define B0_ST2L 0x001c
#define B0_ST3U 0x0034
#define B0_ST3L 0x0038
#define B0_R1_CSR 0x0070
#define B0_R2_CSR 0x0074
#define B0_XA_CSR 0x0078
#define B0_XS_CSR 0x007c
#define B2_MAC_0 0x0100
#define B2_CONN_TYP 0x0108
#define B2_PMD_TYP 0x0109
#define B2_FAR 0x0110
#define B2_TI_INI 0x0120
#define B2_TI_VAL 0x0124
#define B2_TI_CRTL 0x0128
#define B2_WDOG_INI 0x0130
#define B2_WDOG_CRTL 0x0138
#define B2_RTM_INI 0x0140
#define B2_RTM_CRTL 0x0148
#define B3_CFG_SPC 0x0180
#define B4_R1_DA 0x0210
#define B4_R1_F 0x0220
#define B4_R2_DA 0x0250
#define B5_XA_DA 0x0290
#define B5_XA_F 0x02a0
#define B5_XS_DA 0x02d0
#define B5_XS_F 0x02e0

#define ISR_A B0_ISRC

/* New macros from driver */
#define CSR_IRQ_CL_P	(1L<<3)
#define CSR_IRQ_CL_C	(1L<<0)
#define CSR_IRQ_CL_F	(1L<<1)
#define CSR_START	(1L<<4)

#define CTRL_HPI_SET	(1<<4)
#define CTRL_RST_SET	(1<<0)
#define CTRL_RST_CLR	(1<<1)

#define IS_PLINT1	(1L<<20)
#define IS_PLINT2	(1L<<19)
#define IS_MINTR1	(1L<<16)
#define IS_MINTR2	(1L<<17)
#define IS_MINTR3	(1L<<18)
#define IS_TIMINT	(1L<<22)
#define IS_TOKEN	(1L<<21)
#define IS_R1_P		(1L<<15)
#define IS_R1_C		(1L<<12)
#define IS_XA_C		(1L<<4)
#define IS_XS_C		(1L<<0)
#define IS_XS_F		(1L<<1)
#define IS_XA_F		(1L<<5)
#define IS_R1_F		(1L<<13)

#define IMASK_SLOW	(IS_PLINT1 | IS_PLINT2 | IS_TIMINT | IS_TOKEN | \
			 IS_MINTR1 | IS_MINTR2 | IS_MINTR3 | IS_R1_P | \
			 IS_R1_C | IS_XA_C | IS_XS_C)

#define BMU_OWN		(1UL<<31)
#define BMU_CHECK	0x00550000L
#define BMU_EN_IRQ_EOF	(1L<<27)
#define BMU_ST_BUF	(1L<<25)

#define FIRST_FRAG	0x10
#define LAST_FRAG	0x08
#define EN_IRQ_EOF	0x02

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
    uint32_t isrc;
    uint32_t imsk;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t ctrl;
    uint32_t led;
    uint32_t tst_ctrl;
    uint32_t st1u, st1l, st2u, st2l, st3u, st3l;
    uint32_t r1_csr, r2_csr, xa_csr, xs_csr;
    uint32_t mac_0;
    uint32_t conn_typ;
    uint32_t pmd_typ;
    uint32_t far_reg;
    uint32_t ti_ini, ti_val, ti_crtl;
    uint32_t wdog_ini, wdog_crtl;
    uint32_t rtm_ini, rtm_crtl;
    uint32_t cfg_spc;
    uint32_t r1_da, r1_f;
    uint32_t r2_da;
    uint32_t xa_da, xa_f;
    uint32_t xs_da, xs_f;

    /* DMA Context */
    uint32_t rx_bmu_ctl;
    uint32_t rx_bmu_dsc;
    uint32_t tx_bmu_ctl;
    uint32_t tx_bmu_dsc;

    uint16_t hw_state;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->isrc & s->imsk) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
        case B0_CTRL: val = s->ctrl; break;
        case B0_LED: val = s->led; break;
        case B0_TST_CTRL: val = s->tst_ctrl; break;
        case B0_ISRC: val = s->isrc; break;
        case B0_IMSK: val = s->imsk; break;
        case B0_ST1U: val = s->st1u; break;
        case B0_ST1L: val = s->st1l; break;
        case B0_ST2U: val = s->st2u; break;
        case B0_ST2L: val = s->st2l; break;
        case B0_ST3U: val = s->st3u; break;
        case B0_ST3L: val = s->st3l; break;
        case B0_R1_CSR: val = s->r1_csr; break;
        case B0_R2_CSR: val = s->r2_csr; break;
        case B0_XA_CSR: val = s->xa_csr; break;
        case B0_XS_CSR: val = s->xs_csr; break;
        case B2_MAC_0:
        case B2_MAC_0 + 1:
        case B2_MAC_0 + 2:
        case B2_MAC_0 + 3:
        case B2_MAC_0 + 4:
        case B2_MAC_0 + 5:
            val = 0; /* Dummy MAC address */
            break;
        case B2_CONN_TYP: val = s->conn_typ; break;
        case B2_PMD_TYP: val = s->pmd_typ; break;
        case B2_FAR: val = s->far_reg; break;
        case B2_TI_INI: val = s->ti_ini; break;
        case B2_TI_VAL: val = s->ti_val; break;
        case B2_TI_CRTL: val = s->ti_crtl; break;
        case B2_WDOG_INI: val = s->wdog_ini; break;
        case B2_WDOG_CRTL: val = s->wdog_crtl; break;
        case B2_RTM_INI: val = s->rtm_ini; break;
        case B2_RTM_CRTL: val = s->rtm_crtl; break;
        case B3_CFG_SPC: val = s->cfg_spc; break;
        case B4_R1_DA: val = s->r1_da; break;
        case B4_R1_F: val = s->r1_f; break;
        case B4_R2_DA: val = s->r2_da; break;
        case B5_XA_DA: val = s->xa_da; break;
        case B5_XA_F: val = s->xa_f; break;
        case B5_XS_DA: val = s->xs_da; break;
        case B5_XS_F: val = s->xs_f; break;
        default: val = 0; break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
        case B0_CTRL: s->ctrl = val; break;
        case B0_LED: s->led = val; break;
        case B0_TST_CTRL: s->tst_ctrl = val; break;
        case B0_ISRC: 
            s->isrc = val; 
            pcibase_update_irq(s);
            break;
        case B0_IMSK: 
            s->imsk = val; 
            pcibase_update_irq(s);
            break;
        case B0_ST1U: s->st1u = val; break;
        case B0_ST1L: s->st1l = val; break;
        case B0_ST2U: s->st2u = val; break;
        case B0_ST2L: s->st2l = val; break;
        case B0_ST3U: s->st3u = val; break;
        case B0_ST3L: s->st3l = val; break;
        case B0_R1_CSR: s->r1_csr = val; break;
        case B0_R2_CSR: s->r2_csr = val; break;
        case B0_XA_CSR: s->xa_csr = val; break;
        case B0_XS_CSR: s->xs_csr = val; break;
        case B2_MAC_0: s->mac_0 = val; break;
        case B2_CONN_TYP: s->conn_typ = val; break;
        case B2_PMD_TYP: s->pmd_typ = val; break;
        case B2_FAR: s->far_reg = val; break;
        case B2_TI_INI: s->ti_ini = val; break;
        case B2_TI_VAL: s->ti_val = val; break;
        case B2_TI_CRTL: s->ti_crtl = val; break;
        case B2_WDOG_INI: s->wdog_ini = val; break;
        case B2_WDOG_CRTL: s->wdog_crtl = val; break;
        case B2_RTM_INI: s->rtm_ini = val; break;
        case B2_RTM_CRTL: s->rtm_crtl = val; break;
        case B3_CFG_SPC: s->cfg_spc = val; break;
        case B4_R1_DA: s->r1_da = val; break;
        case B4_R1_F: s->r1_f = val; break;
        case B4_R2_DA: s->r2_da = val; break;
        case B5_XA_DA: s->xa_da = val; break;
        case B5_XA_F: s->xa_f = val; break;
        case B5_XS_DA: s->xs_da = val; break;
        case B5_XS_F: s->xs_f = val; break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return pcibase_mmio_read(opaque, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_mmio_write(opaque, addr, val, size);
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

    s->ctrl = 0;
    s->led = 0;
    s->tst_ctrl = 0;
    s->isrc = 0;
    s->imsk = 0;
    s->st1u = 0;
    s->st1l = 0;
    s->st2u = 0;
    s->st2l = 0;
    s->st3u = 0;
    s->st3l = 0;
    s->r1_csr = 0;
    s->r2_csr = 0;
    s->xa_csr = 0;
    s->xs_csr = 0;
    s->mac_0 = 0;
    s->conn_typ = 0;
    s->pmd_typ = 0;
    s->far_reg = 0;
    s->ti_ini = 0;
    s->ti_val = 0;
    s->ti_crtl = 0;
    s->wdog_ini = 0;
    s->wdog_crtl = 0;
    s->rtm_ini = 0;
    s->rtm_crtl = 0;
    s->cfg_spc = 0;
    s->r1_da = 0;
    s->r1_f = 0;
    s->r2_da = 0;
    s->xa_da = 0;
    s->xa_f = 0;
    s->xs_da = 0;
    s->xs_f = 0;
    s->rx_bmu_ctl = 0;
    s->rx_bmu_dsc = 0;
    s->tx_bmu_ctl = 0;
    s->tx_bmu_dsc = 0;
    s->hw_state = 0;

    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_SK );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_SK_FP );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0202 );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->has_msi = false;
    s->has_msix = false;
    
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x4000;
    s->bar_info[0].name = "skfddi-mmio";
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "skfddi_pci",
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
