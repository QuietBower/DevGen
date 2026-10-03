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

#define TYPE_PCIBASE_DEVICE "isp1760_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_PLX 0x10b5
#define PCI_DEVICE_ID_ISP1760 0x5406
#define PCI_CLASS_ID 0x0680
#define PLX_INT_CSR_REG 0x68
#define ISP176x_HC_SCRATCH 0x78
#define ISP1760_FLAG_ANALOG_OC		0x00000008
#define ISP1760_FLAG_ISP1763		0x00000200
#define ISP1760_FLAG_DACK_POL_HIGH	0x00000010
#define ISP1760_FLAG_PERIPHERAL_EN	0x00000004
#define ISP1760_FLAG_DREQ_POL_HIGH	0x00000020
#define ISP1760_FLAG_BUS_WIDTH_8	0x00000400
#define ISP1760_FLAG_ISP1761		0x00000040
#define ISP1760_FLAG_BUS_WIDTH_16	0x00000002
#define ISP1760_FLAG_INTR_EDGE_TRIG	0x00000100
#define ISP1760_FLAG_INTR_POL_HIGH	0x00000080
#define ISP1763_HC_ATL_PTD_SKIPMAP	0xb2
#define ISP1763_HC_MEMORY		0xc4
#define ISP1763_HC_INT_IRQ_MASK_AND	0xe0
#define ISP1763_HC_OTG_CTRL_CLEAR	0xe6
#define ISP1763_HC_ATL_PTD_DONEMAP	0xb0
#define ISP1763_HC_INTERRUPT_ENABLE	0xd6
#define ISP1763_HC_ATL_PTD_LASTPTD	0xb4
#define ISP1763_HC_ISO_IRQ_MASK_OR	0xd8
#define ISP1763_HC_CHIP_ID		0x72
#define ISP1763_HC_ISO_IRQ_MASK_AND	0xde
#define ISP1763_HC_OTG_CTRL_SET		0xe4
#define ISP1763_HC_CONFIGFLAG		0x9c
#define ISP1763_HC_INTERRUPT		0xd4
#define ISP1763_HC_INT_IRQ_MASK_OR	0xda
#define ISP1763_HC_INT_PTD_DONEMAP	0xaa
#define ISP1763_HC_BUFFER_STATUS	0xba
#define ISP1763_HC_RESET		0xb8
#define ISP1763_HC_ISO_PTD_LASTPTD	0xa8
#define ISP1763_HC_USBSTS		0x90
#define ISP1763_HC_ATL_IRQ_MASK_AND	0xe2
#define ISP1763_HC_HW_MODE_CTRL		0xb6
#define ISP1763_HC_INT_PTD_LASTPTD	0xae
#define ISP1763_HC_USBCMD		0x8c
#define ISP1763_HC_ATL_IRQ_MASK_OR	0xdc
#define ISP1763_HC_PORTSC1		0xa0
#define ISP1763_HC_DATA			0xc6
#define ISP1763_HC_ISO_PTD_SKIPMAP	0xa6
#define ISP1763_HC_CHIP_REV		0x70
#define ISP1763_HC_INT_PTD_SKIPMAP	0xac
#define ISP1763_HC_SCRATCH		0x78
#define ISP1763_HC_FRINDEX		0x98
#define DEFAULT_I_TDPS			1024
#define ISP1763_DC_CHIPID_LOW		0x70
#define ISP1763_DC_CTRLFUNC		0x28
#define ISP1763_DC_EPINDEX		0x2c
#define ISP1763_DC_BUFLEN		0x1c
#define ISP1763_DC_EPTYPE		0x08
#define ISP1763_DC_MODE			0x0c
#define ISP1763_DC_CHIPID_HIGH		0x72
#define ISP1763_DC_ADDRESS		0x00
#define ISP1763_DC_FRAMENUM		0x74
#define ISP1763_DC_SCRATCH		0x78
#define ISP1763_DC_INTENABLE		0x14
#define ISP1763_DC_EPMAXPKTSZ		0x04
#define ISP1763_DC_INTCONF		0x10
#define DC_IEPRXTX(n)			(3 << (10 + 2 * (n)))

typedef struct {
    PCIBaseState *s;
    int bar_index;
} BAROpaque;

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
    BAROpaque bar_opaque[6];

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    /* Interrupt State: legacy IRQ requires at minimum intr_status and intr_mask */
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows */
    uint32_t plx_int_csr;
    uint32_t hc_scratch;

    /* Operational status flags */
    /* State used to handle reset sequences */

    /* Generic register file for BAR3 (ISP1763 HC/DC registers) */
    uint8_t bar3_mem[0x10000];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* No interrupt logic needed for probe */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    BAROpaque *bo = opaque;
    PCIBaseState *s = bo->s;
    int bar = bo->bar_index;
    uint64_t val = 0;

    switch (bar) {
    case 0:
        switch (addr) {
        case PLX_INT_CSR_REG:
            val = s->plx_int_csr;
            break;
        default:
            break;
        }
        break;
    case 3:
        if (addr + size <= sizeof(s->bar3_mem)) {
            switch (size) {
            case 1:
                val = s->bar3_mem[addr];
                break;
            case 2:
                val = lduw_le_p(s->bar3_mem + addr);
                break;
            case 4:
                val = ldl_le_p(s->bar3_mem + addr);
                break;
            case 8:
                val = ldq_le_p(s->bar3_mem + addr);
                break;
            default:
                break;
            }
        }
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    BAROpaque *bo = opaque;
    PCIBaseState *s = bo->s;
    int bar = bo->bar_index;

    switch (bar) {
    case 0:
        switch (addr) {
        case PLX_INT_CSR_REG:
            s->plx_int_csr = val;
            break;
        default:
            break;
        }
        break;
    case 3:
        if (addr + size <= sizeof(s->bar3_mem)) {
            switch (size) {
            case 1:
                s->bar3_mem[addr] = val;
                break;
            case 2:
                stw_le_p(s->bar3_mem + addr, val);
                break;
            case 4:
                stl_le_p(s->bar3_mem + addr, val);
                break;
            case 8:
                stq_le_p(s->bar3_mem + addr, val);
                break;
            default:
                break;
            }
        }
        break;
    default:
        break;
    }
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
    s->plx_int_csr = 0;
    s->hc_scratch = 0;
    memset(s->bar3_mem, 0, sizeof(s->bar3_mem));
    /* TODO: Initialize chip ID and other identification registers
     * to correct values expected by the driver. Currently set to zero. */
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
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, &s->bar_opaque[bi->index],
                              bi->name, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_PLX );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_ISP1760 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "isp1761-bar0";

    s->bar_info[1].index = 3;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x10000;
    s->bar_info[1].name = "isp1761-bar3";

    /* Initialize opaque structures for BAR-specific MMIO dispatch */
    for (int i = 0; i < s->num_bars; i++) {
        s->bar_opaque[s->bar_info[i].index].s = s;
        s->bar_opaque[s->bar_info[i].index].bar_index = s->bar_info[i].index;
    }

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
    .name = "isp1760_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(plx_int_csr, PCIBaseState),
        VMSTATE_UINT32(hc_scratch, PCIBaseState),
        VMSTATE_UINT8_ARRAY(bar3_mem, PCIBaseState, 0x10000),
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
