/*
 * QEMU PCI device model for Nozomi card (minimal behavior for driver probe)
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

#include <stdint.h>

#define TYPE_PCIBASE_DEVICE "nozomi_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* IDs taken from driver */
#define NOZOMI_PCI_VENDOR_ID 0x1931
#define NOZOMI_PCI_DEVICE_ID 0x000c
#define NOZOMI_PCI_CLASS_ID  PCI_CLASS_COMMUNICATION_SERIAL

/* Control register offsets (relative) */
#define NOZOMI_REG_R_IIR 0x0000
#define NOZOMI_REG_R_FCR 0x0000
#define NOZOMI_REG_R_IER 0x0004

#define NOZOMI_RESET            0x8000
#define NOZOMI_CONFIG_MAGIC     0xEFEFFEFE

#define NOZOMI_TOGGLE_VALID     0x0000
#define NOZOMI_MDM_DL1          0x0001
#define NOZOMI_MDM_UL1          0x0002
#define NOZOMI_MDM_DL2          0x0004
#define NOZOMI_MDM_UL2          0x0008
#define NOZOMI_DIAG_DL1         0x0010
#define NOZOMI_DIAG_DL2         0x0020
#define NOZOMI_DIAG_UL          0x0040
#define NOZOMI_APP1_DL          0x0080
#define NOZOMI_APP1_UL          0x0100
#define NOZOMI_APP2_DL          0x0200
#define NOZOMI_APP2_UL          0x0400
#define NOZOMI_CTRL_DL          0x0800
#define NOZOMI_CTRL_UL          0x1000

#define NOZOMI_MDM_DL   (NOZOMI_MDM_DL1  | NOZOMI_MDM_DL2)
#define NOZOMI_MDM_UL   (NOZOMI_MDM_UL1  | NOZOMI_MDM_UL2)
#define NOZOMI_DIAG_DL  (NOZOMI_DIAG_DL1 | NOZOMI_DIAG_DL2)

#define NOZOMI_CTRL_DSR 0x0001
#define NOZOMI_CTRL_DCD 0x0002
#define NOZOMI_CTRL_RI  0x0004
#define NOZOMI_CTRL_CTS 0x0008
#define NOZOMI_CTRL_DTR 0x0001
#define NOZOMI_CTRL_RTS 0x0002

#define NOZOMI_MAX_PORT        4
#define NOZOMI_MAX_PORTS       5
#define NOZOMI_NTTY_TTY_MAXMINORS 256
#define NOZOMI_MAX_CARDS       (NOZOMI_NTTY_TTY_MAXMINORS / NOZOMI_MAX_PORT)

typedef enum {
    NOZOMI_CARD_F32_2 = 2048,
    NOZOMI_CARD_F32_8 = 8192,
} NozomiCardType;

typedef enum {
    NOZOMI_PORT_MDM   = 0,
    NOZOMI_PORT_DIAG  = 1,
    NOZOMI_PORT_APP1  = 2,
    NOZOMI_PORT_APP2  = 3,
    NOZOMI_PORT_CTRL  = 4,
    NOZOMI_PORT_ERROR = -1,
} NozomiPortType;

typedef enum {
    NOZOMI_STATE_UNKNOWN   = 0,
    NOZOMI_STATE_ENABLED   = 1,
    NOZOMI_STATE_ALLOCATED = 2,
    NOZOMI_STATE_READY     = 3,
} NozomiCardState;

typedef enum {
    NOZOMI_CH_A = 0,
    NOZOMI_CH_B = 1,
} NozomiChannelType;

typedef enum {
    NOZOMI_CTRL_CMD   = 0,
    NOZOMI_CTRL_MDM   = 1,
    NOZOMI_CTRL_DIAG  = 2,
    NOZOMI_CTRL_APP1  = 3,
    NOZOMI_CTRL_APP2  = 4,
    NOZOMI_CTRL_ERROR = -1,
} NozomiCtrlPortType;

typedef struct NozomiToggles {
    unsigned int mdm_ul:1;
    unsigned int mdm_dl:1;
    unsigned int diag_dl:1;
    unsigned int enabled:5;
} NozomiToggles;

typedef struct NozomiConfigTable {
    uint32_t signature;
    uint16_t version;
    uint16_t product_information;
    NozomiToggles toggle;
    uint8_t pad1[7];
    uint16_t dl_start;
    uint16_t dl_mdm_len1;
    uint16_t dl_mdm_len2;
    uint16_t dl_diag_len1;
    uint16_t dl_diag_len2;
    uint16_t dl_app1_len;
    uint16_t dl_app2_len;
    uint16_t dl_ctrl_len;
    uint8_t pad2[16];
    uint16_t ul_start;
    uint16_t ul_mdm_len2;
    uint16_t ul_mdm_len1;
    uint16_t ul_diag_len;
    uint16_t ul_app1_len;
    uint16_t ul_app2_len;
    uint16_t ul_ctrl_len;
} NozomiConfigTable;

typedef struct NozomiCtrlDL {
    unsigned int DSR:1;
    unsigned int DCD:1;
    unsigned int RI:1;
    unsigned int CTS:1;
    unsigned int reserved:4;
    uint8_t port;
} NozomiCtrlDL;

typedef struct NozomiCtrlUL {
    unsigned int DTR:1;
    unsigned int RTS:1;
    unsigned int reserved:6;
    uint8_t port;
} NozomiCtrlUL;

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

    /* Hardware Register Shadows */
    struct {
        uint16_t r_iir;   /* Interrupt Identification Register */
        uint16_t r_fcr;   /* Flow Control Register (W1C) */
        uint16_t r_ier;   /* Interrupt Enable Register */
        NozomiConfigTable config_table;
        NozomiCardType card_type;
        NozomiCardState state;
    } regs;

    /* Simple memory backing for config/data table (BAR0) */
    uint8_t *mem;
    hwaddr mem_size;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* IRQ asserted when any enabled interrupt bit is set in IIR & IER */
    uint16_t pending = s->regs.r_iir & s->regs.r_ier;
    if (pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Minimal DMA hook: not used by this driver, keep empty */
static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* 16-bit register space at end of BAR: card_type/2 base. */
    hwaddr reg_base = s->regs.card_type / 2;

    if (addr >= reg_base) {
        hwaddr roff = addr - reg_base;
        switch (roff) {
        case NOZOMI_REG_R_IIR: /* 0x0 */
            if (size == 2) {
                val = s->regs.r_iir;
            }
            break;
        case NOZOMI_REG_R_IER: /* 0x4 */
            if (size == 2) {
                val = s->regs.r_ier;
            }
            break;
        default:
            break;
        }
        return val;
    }

    /* Below reg_base: configuration table / data area. */
    if (addr + size <= s->mem_size && s->mem) {
        switch (size) {
        case 1:
            val = s->mem[addr];
            break;
        case 2:
            val = *(uint16_t *)(s->mem + addr);
            break;
        case 4:
            val = *(uint32_t *)(s->mem + addr);
            break;
        default:
            break;
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    hwaddr reg_base = s->regs.card_type / 2;

    if (addr >= reg_base) {
        hwaddr roff = addr - reg_base;
        switch (roff) {
        case NOZOMI_REG_R_FCR: /* 0x0: W1C for IIR bits */
            if (size == 2) {
                uint16_t w = (uint16_t)val;
                s->regs.r_fcr = w;
                /* Clear corresponding bits in IIR */
                s->regs.r_iir &= ~w;
                pcibase_update_irq(s);
            }
            break;
        case NOZOMI_REG_R_IER: /* 0x4: interrupt enable */
            if (size == 2) {
                s->regs.r_ier = (uint16_t)val;
                pcibase_update_irq(s);
            }
            break;
        default:
            break;
        }
        return;
    }

    /* Writes into data/config area */
    if (addr + size <= s->mem_size && s->mem) {
        switch (size) {
        case 1:
            s->mem[addr] = (uint8_t)val;
            break;
        case 2:
            *(uint16_t *)(s->mem + addr) = (uint16_t)val;
            break;
        case 4:
            *(uint32_t *)(s->mem + addr) = (uint32_t)val;
            break;
        default:
            break;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
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

    /* Reset register state */
    s->regs.r_iir = 0;
    s->regs.r_fcr = 0;
    s->regs.r_ier = 0;
    s->regs.state = NOZOMI_STATE_UNKNOWN;

    /* On reset, hardware will later issue RESET interrupt when driver
     * enables RESET bit in IER. For now, keep IIR clear. */

    pcibase_update_irq(s);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  NOZOMI_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  NOZOMI_PCI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, NOZOMI_PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Determine card_type as driver does based on total BAR size. Here we
     * expose a single BAR of size 8192 (F32_8) by default. */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = NOZOMI_CARD_F32_8; /* 8192 */
    s->bar_info[0].name = "nozomi-mmio";

    for (int i = 1; i < 6; i++) {
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

    /* Allocate backing memory for BAR0 space */
    s->mem_size = s->bar_info[0].size;
    s->mem = g_malloc0(s->mem_size);

    /* Initialize configuration table in the first bytes of BAR0. Driver
     * reads this via read_mem32(). */
    NozomiConfigTable *ct = &s->regs.config_table;
    memset(ct, 0, sizeof(*ct));

    ct->signature = NOZOMI_CONFIG_MAGIC;
    /* Two-phase init: start with version>0 and toggle.enabled != TOGGLE_VALID
     * so that first interrupt RESET causes first-phase path. */
    ct->version = 1;
    ct->toggle.enabled = 1; /* != TOGGLE_VALID (0) */

    /* Layout: place config table at offset 0 */
    memcpy(s->mem, ct, sizeof(*ct));

    /* card_type determined as sum of BAR sizes: we only have 1 BAR */
    s->regs.card_type = NOZOMI_CARD_F32_8;

    /* Initial state */
    s->regs.state = NOZOMI_STATE_ENABLED;

    /* Start with no interrupts pending or enabled */
    s->regs.r_iir = 0;
    s->regs.r_fcr = 0;
    s->regs.r_ier = 0;

    /* The real hardware would assert RESET interrupt when enabled; here we
     * leave IIR clear and rely on later phases to generate RESET if needed. */

    pcibase_update_irq(s);
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

    if (s->mem) {
        g_free(s->mem);
        s->mem = NULL;
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "nozomi_pci",
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

