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


#define TYPE_PCIBASE_DEVICE "ehci_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_DEVICE_ID_INTEL_CE4100_USB 0x2e70
#define PCI_DEVICE_ID_ASPEED_EHCI 0x2603
#define PCI_DEVICE_ID_INTEL_QUARK_X1000_SOC 0x0939
#define OHCI_HCCTRL_OFFSET 0x4
#define OHCI_HCCTRL_LEN 0x4
#define EHCI_BANDWIDTH_SIZE 64
#define EHCI_MAX_ROOT_PORTS 15

#define PCI_CLASS_SERIAL_USB_EHCI 0x0c0320
#define PCI_VENDOR_ID_STMICRO 0x104A
#define PCI_DEVICE_ID_STMICRO_USB_HOST 0xCC00

#define EHCI_TUNE_FLS 1
#define USBMODE_EX_HC (3<<0)
#define USBMODE_EX_VBPS (1<<5)
#define TXFIFO_DEFAULT (8<<16)
#define PORT_TEST_FORCE PORT_TEST(0x5)

struct ehci_caps {
    uint32_t hc_capbase;
#define HC_LENGTH(p)        (((p)>>00)&0x00ff)
#define HC_VERSION(p)       (((p)>>16)&0xffff)
    uint32_t hcs_params;
#define HCS_DEBUG_PORT(p)   (((p)>>20)&0xf)
#define HCS_INDICATOR(p)    ((p)&(1 << 16))
#define HCS_N_CC(p)         (((p)>>12)&0xf)
#define HCS_N_PCC(p)        (((p)>>8)&0xf)
#define HCS_PORTROUTED(p)   ((p)&(1 << 7))
#define HCS_PPC(p)          ((p)&(1 << 4))
#define HCS_N_PORTS(p)      (((p)>>0)&0xf)
    uint32_t hcc_params;
#define HCC_PER_PORT_CHANGE_EVENT(p)    ((p)&(1 << 18))
#define HCC_EXT_CAPS(p)     (((p)>>8)&0xff)
#define HCC_ISOC_CACHE(p)   ((p)&(1 << 7))
#define HCC_ISOC_THRES(p)   (((p)>>4)&0x7)
#define HCC_CANPARK(p)      ((p)&(1 << 2))
#define HCC_PGM_FRAMELISTLEN(p) ((p)&(1 << 1))
#define HCC_64BIT_ADDR(p)   ((p)&(1))
    uint8_t portroute[8];
};

struct ehci_regs {
    uint32_t command;
#define CMD_PPCEE   (1<<15)
#define CMD_PARK    (1<<11)
#define CMD_PARK_CNT(c) (((c)>>8)&3)
#define CMD_LRESET  (1<<7)
#define CMD_IAAD    (1<<6)
#define CMD_ASE     (1<<5)
#define CMD_PSE     (1<<4)
#define CMD_RESET   (1<<1)
#define CMD_RUN     (1<<0)
    uint32_t status;
#define STS_ASS     (1<<15)
#define STS_PSS     (1<<14)
#define STS_RECL    (1<<13)
#define STS_HALT    (1<<12)
#define STS_IAA     (1<<5)
#define STS_FATAL   (1<<4)
#define STS_FLR     (1<<3)
#define STS_PCD     (1<<2)
#define STS_ERR     (1<<1)
#define STS_INT     (1<<0)
#define INTR_MASK (STS_IAA | STS_FATAL | STS_PCD | STS_ERR | STS_INT)
    uint32_t intr_enable;
    uint32_t frame_index;
    uint32_t segment;
    uint32_t frame_list;
    uint32_t async_next;
    uint32_t reserved[9];
    uint32_t configured_flag;
#define FLAG_CF     (1<<0)
    uint32_t port_status[EHCI_MAX_ROOT_PORTS];
#define PORT_WKOC_E (1<<22)
#define PORT_WKDISC_E   (1<<21)
#define PORT_WKCONN_E   (1<<20)
#define PORT_LED_OFF    (0<<14)
#define PORT_LED_AMBER  (1<<14)
#define PORT_LED_GREEN  (2<<14)
#define PORT_LED_MASK   (3<<14)
#define PORT_OWNER  (1<<13)
#define PORT_POWER  (1<<12)
#define PORT_USB11(x) (((x)&(3<<10)) == (1<<10))
#define PORT_RESET  (1<<8)
#define PORT_SUSPEND    (1<<7)
#define PORT_RESUME (1<<6)
#define PORT_OCC    (1<<5)
#define PORT_OC     (1<<4)
#define PORT_PEC    (1<<3)
#define PORT_PE     (1<<2)
#define PORT_CSC    (1<<1)
#define PORT_CONNECT    (1<<0)
#define PORT_RWC_BITS   (PORT_CSC | PORT_PEC | PORT_OCC)
};

struct ehci_dbg_port {
    uint32_t control;
#define DBGP_OWNER  (1<<30)
#define DBGP_ENABLED    (1<<28)
#define DBGP_DONE   (1<<16)
#define DBGP_INUSE  (1<<10)
#define DBGP_ERRCODE(x) (((x)>>7)&0x07)
#define DBGP_ERR_BAD    1
#define DBGP_ERR_SIGNAL 2
#define DBGP_ERROR  (1<<6)
#define DBGP_GO     (1<<5)
#define DBGP_OUT    (1<<4)
#define DBGP_LEN(x) (((x)>>0)&0x0f)
    uint32_t pids;
#define DBGP_PID_GET(x)     (((x)>>16)&0xff)
#define DBGP_PID_SET(data, tok) (((data)<<8)|(tok))
    uint32_t data03;
    uint32_t data47;
    uint32_t address;
#define DBGP_EPADDR(dev, ep)    (((dev)<<8)|(ep))
};

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct ehci_caps caps;
    struct ehci_regs regs;
    struct ehci_dbg_port dbg_port;

    /* DMA Context */
    dma_addr_t periodic_dma;

    uint32_t command;
    bool halted;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint32_t cap_len = HC_LENGTH(s->caps.hc_capbase);

    if (cap_len == 0) {
        cap_len = 0x20; /* Fallback to standard EHCI cap length */
    }

    if (addr < cap_len) {
        if (addr == offsetof(struct ehci_caps, hc_capbase)) {
            val = s->caps.hc_capbase;
        } else if (addr == offsetof(struct ehci_caps, hcs_params)) {
            val = s->caps.hcs_params;
        } else if (addr == offsetof(struct ehci_caps, hcc_params)) {
            val = s->caps.hcc_params;
        }
    } else {
        hwaddr reg_addr = addr - cap_len;
        if (reg_addr == offsetof(struct ehci_regs, command)) {
            val = s->regs.command;
        } else if (reg_addr == offsetof(struct ehci_regs, status)) {
            val = s->regs.status;
        } else if (reg_addr == offsetof(struct ehci_regs, intr_enable)) {
            val = s->regs.intr_enable;
        } else if (reg_addr == offsetof(struct ehci_regs, configured_flag)) {
            val = s->regs.configured_flag;
        } else if (reg_addr >= offsetof(struct ehci_regs, port_status) &&
                   reg_addr < offsetof(struct ehci_regs, port_status) + sizeof(s->regs.port_status)) {
            int index = (reg_addr - offsetof(struct ehci_regs, port_status)) / 4;
            val = s->regs.port_status[index];
        }
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t cap_len = HC_LENGTH(s->caps.hc_capbase);

    if (cap_len == 0) {
        cap_len = 0x20;
    }

    if (addr < cap_len) {
        if (addr == offsetof(struct ehci_caps, hcs_params)) {
            s->caps.hcs_params = val;
        }
    } else {
        hwaddr reg_addr = addr - cap_len;
        if (reg_addr == offsetof(struct ehci_regs, command)) {
            s->regs.command = val;
            if (val & CMD_RESET) {
                /* Hardware clears CMD_RESET when reset is complete */
                s->regs.command &= ~CMD_RESET;
                /* Set STS_HALT upon reset as per EHCI spec */
                s->regs.status |= STS_HALT;
            } else {
                if (!(val & CMD_RUN)) {
                    s->regs.status |= STS_HALT;
                } else {
                    s->regs.status &= ~STS_HALT;
                }
            }
        } else if (reg_addr == offsetof(struct ehci_regs, status)) {
            s->regs.status &= ~(val & INTR_MASK); /* W1C for interrupts */
        } else if (reg_addr == offsetof(struct ehci_regs, intr_enable)) {
            s->regs.intr_enable = val;
        } else if (reg_addr == offsetof(struct ehci_regs, configured_flag)) {
            s->regs.configured_flag = val;
        } else if (reg_addr >= offsetof(struct ehci_regs, port_status) &&
                   reg_addr < offsetof(struct ehci_regs, port_status) + sizeof(s->regs.port_status)) {
            int index = (reg_addr - offsetof(struct ehci_regs, port_status)) / 4;
            /* Handle W1C bits */
            s->regs.port_status[index] &= ~(val & PORT_RWC_BITS);
            /* Update other bits */
            s->regs.port_status[index] = (s->regs.port_status[index] & PORT_RWC_BITS) | (val & ~PORT_RWC_BITS);
        }
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

    /* SBRN (Serial Bus Release Number) at PCI config 0x60 */
    pci_set_byte(PCI_DEVICE(dev)->config + 0x60, 0x20);

    /* Initialize Capability Registers */
    s->caps.hc_capbase = 0x01000020; /* Version 1.0, Length 0x20 */
    s->caps.hcs_params = 0x00000000;
    s->caps.hcc_params = 0x00000000;

    /* Initialize Operational Registers */
    s->regs.command = 0;
    s->regs.status = STS_HALT; /* Halted by default */
    s->regs.intr_enable = 0;
    s->regs.configured_flag = 0;
    memset(s->regs.port_status, 0, sizeof(s->regs.port_status));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_STMICRO );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_STMICRO_USB_HOST );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SERIAL_USB_EHCI );
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "ehci-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msi_init(pdev, 0, 1, true, false, errp) == 0) {
        s->has_msi = true;
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
    .name = "ehci_pci_pci",
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
