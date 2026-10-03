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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "ntb_hw_idt_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_IDT		0x111d
#define PCI_DEVICE_ID_IDT_89HPES24NT6AG2  0x8091

/* Register Layout and Hardware Identifiers extracted from driver source */
#define IDT_REG_PCI_MAX			0x00FFFU
#define IDT_REG_ALIGN			4
#define IDT_REG_SW_MAX			0x3FFFFU
#define IDT_NT_GASADATA			0x00FFCU
#define IDT_NT_GASAADDR			0x00FF8U
#define IDT_NT_PCIELCAP			0x0004CU
#define IDT_MAX_NR_PORTS	24
#define IDT_SEMSK_GSIGNAL		0x00000020U
#define IDT_SW_SEGSIGMSK		0x3EC34U
#define IDT_SW_SEPMSK			0x3EC08U
#define IDT_SW_SEMSK			0x3EC04U
#define IDT_SW_SELINKUPSTS		0x3EC0CU
#define IDT_SW_SELINKUPMSK		0x3EC10U
#define IDT_SEMSK_LINKDN		0x00000002U
#define IDT_SEMSK_LINKUP		0x00000001U
#define IDT_SW_SELINKDNMSK		0x3EC18U
#define IDT_SW_SELINKDNSTS		0x3EC14U
#define IDT_SW_SEGSIGSTS		0x3EC30U
#define IDT_SW_SESTS			0x3EC00U
#define IDT_NT_NTINTSTS			0x00404U
#define IDT_NTINTSTS_SEVENT		0x00000008U
#define IDT_NT_REQIDCAP			0x004DCU
#define IDT_NT_NTGSIGNAL		0x00410U
#define IDT_NTGSIGNAL_SET		0x00000001U
#define IDT_NT_NTMTBLDATA		0x004D8U
#define IDT_NTMTBLDATA_VALID		0x00000001U
#define IDT_NT_NTMTBLADDR		0x004D0U
#define IDT_NT_NTCTL			0x00400U
#define IDT_NTCTL_CPEN			0x00000002U
#define IDT_NT_PCICMDSTS		0x00004U
#define IDT_PCICMDSTS_BME		0x00000004U
#define IDT_NT_PCIELCTLSTS		0x00050U
#define IDT_MAX_NR_MWS		29
#define IDT_BAR_CNT		6
#define IDT_BARSETUP_EN			0x80000000U
#define IDT_DIR_SIZE_ALIGN	1
#define IDT_TRANS_ALIGN		4
#define IDT_BARSETUP_MODE_CFG		0x00000400U
#define IDT_NT_LUTUDATA			0x004ECU
#define IDT_NT_LUTOFFSET		0x004E0U
#define IDT_LUTUDATA_VALID		0x80000000U
#define IDT_NT_LUTLDATA			0x004E4U
#define IDT_NT_LUTMDATA			0x004E8U
#define IDT_DBELL_MASK		((uint32_t)0xFFFFFFFFU)
#define IDT_NT_INDBELLSTS		0x00428U
#define IDT_NT_INDBELLMSK		0x0042CU
#define IDT_NT_OUTDBELLSET		0x00420U
#define IDT_MSG_CNT		4
#define IDT_INMSG_MASK		((uint32_t)0x000F0000U)
#define IDT_OUTMSG_MASK		((uint32_t)0x0000000FU)
#define IDT_NT_MSGSTS			0x00460U
#define IDT_NT_MSGSTSMSK		0x00464U
#define IDT_MSG_MASK		(IDT_INMSG_MASK | IDT_OUTMSG_MASK)
#define IDT_SW_TMPADJ			0x3F1E0U
#define IDT_SW_TMPSTS			0x3F1D8U
#define IDT_SW_TMPALARM			0x3F1DCU
#define IDT_TMPALARM_IRQ_MASK		0x3F000000U
#define IDT_TEMP_MAX_MDEG	127500
#define IDT_TEMP_MAX_OFFSET	63500
#define IDT_TEMP_MIN_MDEG	0
#define IDT_TEMP_MIN_OFFSET	-64000
#define IDT_SW_TMPCTL			0x3F1D4U
#define IDT_NTINTMSK_ALL \
	(IDT_NTINTMSK_MSG | IDT_NTINTMSK_DBELL | IDT_NTINTMSK_SEVENT)
#define IDT_NT_NTINTMSK			0x00408U
#define IDT_NTINTSTS_DBELL		0x00000002U
#define IDT_NTINTSTS_MSG		0x00000001U
#define IDT_SW_GDBELLSTS		0x3EC3CU
#define IDT_MTBL_ENTRY_CNT	64
#define IDT_BARSETUP_SIZE_MASK		0x000003F0U
#define IDT_NT_BARSETUP0		0x00470U
#define IDT_BARSETUP_SIZE_CFG		0x000000C0U
#define IDT_NT_BARUTBASE4		0x004BCU
#define IDT_NT_OUTMSG0			0x00430U
#define IDT_NT_OUTMSG3			0x0043CU
#define IDT_NT_BARLIMIT5		0x004C4U
#define IDT_NT_BARUTBASE3		0x004ACU
#define IDT_NT_OUTMSG2			0x00438U
#define IDT_NT_INMSG1			0x00444U
#define IDT_NT_BARUTBASE0		0x0047CU
#define IDT_NT_BARSETUP2		0x00490U
#define IDT_NT_BARSETUP4		0x004B0U
#define IDT_NT_BARLTBASE2		0x00498U
#define IDT_NT_BARLIMIT4		0x004B4U
#define IDT_NT_OUTMSG1			0x00434U
#define IDT_NT_BARUTBASE2		0x0049CU
#define IDT_NT_BARLTBASE1		0x00488U
#define IDT_NT_INMSGSRC3		0x0045CU
#define IDT_NT_BARLTBASE5		0x004C8U
#define IDT_NT_BARLIMIT3		0x004A4U
#define IDT_NT_BARUTBASE5		0x004CCU
#define IDT_NT_BARLIMIT2		0x00494U
#define IDT_NT_BARLIMIT1		0x00484U
#define IDT_NT_BARLIMIT0		0x00474U
#define IDT_NT_INMSGSRC1		0x00454U
#define IDT_NT_BARLTBASE4		0x004B8U
#define IDT_NT_BARSETUP3		0x004A0U
#define IDT_NT_INMSG2			0x00448U
#define IDT_NT_BARLTBASE0		0x00478U
#define IDT_NT_INMSGSRC0		0x00450U
#define IDT_NT_BARLTBASE3		0x004A8U
#define IDT_NT_INMSGSRC2		0x00458U
#define IDT_NT_BARUTBASE1		0x0048CU
#define IDT_NT_BARSETUP5		0x004C0U
#define IDT_NT_BARSETUP1		0x00480U
#define IDT_NT_INMSG3			0x0044CU
#define IDT_NT_INMSG0			0x00440U
#define IDT_MAX_NR_PARTS	8
#define IDT_MAX_NR_PEERS	8
#define IDT_NTINTMSK_SEVENT		0x00000008U
#define IDT_NTINTMSK_MSG		0x00000001U
#define IDT_NTINTMSK_DBELL		0x00000002U
#define IDT_SWPORTxSTS_LINKUP		0x00000010U

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
    uint32_t ntint_sts;
    uint32_t ntint_msk;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t regs[IDT_REG_SW_MAX / 4 + 1];
};

enum idt_mw_type {
	IDT_MW_DIR = 0x0,
	IDT_MW_LUT12 = 0x1,
	IDT_MW_LUT24 = 0x2
};
enum idt_temp_val {
	IDT_TEMP_CUR,
	IDT_TEMP_LOW,
	IDT_TEMP_HIGH,
	IDT_TEMP_OFFSET
};
struct idt_ntb_bar {
	unsigned int setup;
	unsigned int limit;
	unsigned int ltbase;
	unsigned int utbase;
};
struct idt_mw_cfg {
	enum idt_mw_type type;
	unsigned char bar;
	unsigned char idx;
	uint64_t addr_align;
	uint64_t size_align;
	uint64_t size_max;
};
struct idt_ntb_peer {
	unsigned char port;
	unsigned char part;
	unsigned char mw_cnt;
	struct idt_mw_cfg *mws;
};
struct idt_89hpes_cfg {
	char *name;
	unsigned char port_cnt;
	unsigned char ports[];
};
struct idt_ntb_msg {
	unsigned int in;
	unsigned int out;
	unsigned int src;
};
struct idt_ntb_regs {
	struct idt_ntb_bar bars[IDT_BAR_CNT];
	struct idt_ntb_msg msgs[IDT_MSG_CNT];
};
struct idt_ntb_port {
	unsigned int pcicmdsts;
	unsigned int pcielctlsts;
	unsigned int ntctl;
	unsigned int ctl;
	unsigned int sts;
	struct idt_ntb_bar bars[IDT_BAR_CNT];
};
struct idt_ntb_part {
	unsigned int ctl;
	unsigned int sts;
	unsigned int msgctl[IDT_MSG_CNT];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t sts = s->regs[IDT_NT_NTINTSTS / 4];
    uint32_t msk = s->regs[IDT_NT_NTINTMSK / 4];
    bool level = (sts & ~msk) != 0;

    if (level) {
        if (s->has_msi) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!s->has_msi) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    if (addr > IDT_REG_PCI_MAX) {
        return 0;
    }

    if (addr == IDT_NT_GASADATA) {
        uint32_t gasa_addr = s->regs[IDT_NT_GASAADDR / 4];
        if (gasa_addr <= IDT_REG_SW_MAX) {
            val = s->regs[gasa_addr / 4];
        }
    } else {
        val = s->regs[addr / 4];
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr > IDT_REG_PCI_MAX) {
        return;
    }

    if (addr == IDT_NT_GASADATA) {
        uint32_t gasa_addr = s->regs[IDT_NT_GASAADDR / 4];
        if (gasa_addr <= IDT_REG_SW_MAX) {
            s->regs[gasa_addr / 4] = val;
        }
    } else if (addr == IDT_NT_NTINTSTS || addr == IDT_NT_INDBELLSTS || addr == IDT_NT_MSGSTS) {
        s->regs[addr / 4] &= ~val; /* W1C */
        pcibase_update_irq(s);
    } else if (addr == IDT_NT_OUTDBELLSET) {
        s->regs[IDT_NT_INDBELLSTS / 4] |= val;
        s->regs[IDT_NT_NTINTSTS / 4] |= IDT_NTINTSTS_DBELL;
        pcibase_update_irq(s);
    } else if (addr == IDT_NT_OUTMSG0 || addr == IDT_NT_OUTMSG1 || 
               addr == IDT_NT_OUTMSG2 || addr == IDT_NT_OUTMSG3) {
        s->regs[addr / 4] = val;
        s->regs[IDT_NT_MSGSTS / 4] |= IDT_NTINTSTS_MSG;
        s->regs[IDT_NT_NTINTSTS / 4] |= IDT_NTINTSTS_MSG;
        pcibase_update_irq(s);
    } else if (addr == IDT_NT_NTINTMSK) {
        s->regs[addr / 4] = val;
        pcibase_update_irq(s);
    } else {
        s->regs[addr / 4] = val;
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

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[IDT_NT_BARSETUP0 / 4] = IDT_BARSETUP_EN | IDT_BARSETUP_MODE_CFG | IDT_BARSETUP_SIZE_CFG;
    s->regs[IDT_NT_NTINTMSK / 4] = IDT_NTINTMSK_ALL;
    s->regs[IDT_NT_PCICMDSTS / 4] = IDT_PCICMDSTS_BME;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_IDT );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_IDT_89HPES24NT6AG2 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_OTHER );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize BARSETUP0 to pass idt_check_setup */
    pci_set_long(pci_conf + IDT_NT_BARSETUP0, IDT_BARSETUP_EN | IDT_BARSETUP_MODE_CFG | IDT_BARSETUP_SIZE_CFG);

    if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
    }

    /* BAR Initialization */
    s->num_bars = IDT_BAR_CNT;  
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "idt-ntb-cfg";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (s->has_msix) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (s->has_msi) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ntb_hw_idt_pci",
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
