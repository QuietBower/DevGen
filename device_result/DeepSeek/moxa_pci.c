/*
 * QEMU model for Moxa serial PCI devices (e.g., C218 Turbo PCI)
 * Based on driver: /home/eely/linux-7.1/drivers/tty/moxa.c
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

#define TYPE_PCIBASE_DEVICE "moxa_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_MOXA           0x1393
#define PCI_DEVICE_ID_MOXA_C218      0x1180
#define PCI_CLASS_MOXA               0x070002
#define BAR0_SIZE                    (128 * 1024)

#define Magic_code                   0x404

/* C218 series offsets */
#define C218_ConfBase                0x800
#define C218_status                  (C218_ConfBase + 0)
#define C218_diag                    (C218_ConfBase + 2)
#define C218_key                     (C218_ConfBase + 4)
#define C218DLoad_len                (C218_ConfBase + 6)
#define C218check_sum                (C218_ConfBase + 8)
#define C218chksum_ok                (C218_ConfBase + 0x0a)
#define C218_TestRx                  (C218_ConfBase + 0x10)
#define C218_TestTx                  (C218_ConfBase + 0x18)
#define C218_RXerr                   (C218_ConfBase + 0x20)
#define C218_ErrFlag                 (C218_ConfBase + 0x28)
#define C218_LoadBuf                 0x0F00
#define C218_KeyCode                 0x218

/* C320 series offsets (shared base) */
#define C320_ConfBase                0x800
#define C320_status                  (C320_ConfBase + 0)
#define C320_diag                    (C320_ConfBase + 2)
#define C320_key                     (C320_ConfBase + 4)
#define C320DLoad_len                (C320_ConfBase + 6)
#define C320check_sum                (C320_ConfBase + 8)
#define C320chksum_ok                (C320_ConfBase + 0x0a)
#define C320bapi_len                 (C320_ConfBase + 0x0c)
#define C320UART_no                  (C320_ConfBase + 0x0e)
#define C320_LoadBuf                 0x0f00
#define C320_KeyCode                 0x320

/* Global DRAM and config offsets */
#define FixPage_addr                 0x0000
#define DynPage_addr                 0x2000
#define C218_start                   0x3000
#define Control_reg                  0x1ff0
#define HW_reset                     0x80

/* Interrupt register offsets */
#define DRAM_global                  0
#define INT_data                     (DRAM_global + 0)
#define Config_base                  (DRAM_global + 0x108)
#define IRQindex                     (INT_data + 0)      /* 0 */
#define IRQpending                   (INT_data + 4)      /* 4 */
#define IRQtable                     (INT_data + 8)      /* 8 */

/* Interrupt flag bits */
#define IntrRx                       0x01
#define IntrTx                       0x02
#define IntrFunc                     0x04
#define IntrBreak                    0x08
#define IntrLine                     0x10
#define IntrIntr                     0x20
#define IntrQuit                     0x40
#define IntrEOF                      0x80
#define IntrRxTrigger                0x100
#define IntrTxTrigger                0x200

/* Config block offsets */
#define Magic_no                     (Config_base + 0)   /* 0x108 */
#define Card_model_no                (Config_base + 2)   /* 0x10A */
#define Total_ports                  (Config_base + 4)   /* 0x10C */
#define Module_cnt                   (Config_base + 8)   /* 0x110 */
#define Module_no                    (Config_base + 10)  /* 0x112 */
#define Timer_10ms                   (Config_base + 14)  /* 0x116 */
#define Disable_IRQ                  (Config_base + 20)  /* 0x11C */
#define TMS320_PORT1                 (Config_base + 22)  /* 0x11E */
#define TMS320_PORT2                 (Config_base + 24)  /* 0x120 */
#define TMS320_CLOCK                 (Config_base + 26)  /* 0x122 */

/* Extern table */
#define Extern_table                 0x400
#define Extern_size                  0x60

/* Per-port register offsets (relative to port tableAddr) */
#define RXrptr                       0x00
#define RXwptr                       0x02
#define TXrptr                       0x04
#define TXwptr                       0x06
#define HostStat                     0x08
#define FlagStat                     0x0A
#define FlowControl                  0x0C
#define Break_cnt                    0x0E
#define CD180TXirq                   0x10
#define RX_mask                      0x12
#define TX_mask                      0x14
#define Ofs_rxb                      0x16
#define Ofs_txb                      0x18
#define Page_rxb                     0x1A
#define Page_txb                     0x1C
#define EndPage_rxb                  0x1E
#define EndPage_txb                  0x20
#define Data_error                   0x22
#define RxTrigger                    0x28
#define TxTrigger                    0x2a
#define rRXwptr                      0x34
#define Low_water                    0x36
#define FuncCode                     0x40
#define FuncArg                      0x42
#define FuncArg1                     0x44

/* Buffer sizes and masks */
#define C218rx_size                  0x2000
#define C218tx_size                  0x8000
#define C218rx_mask                  (C218rx_size - 1)
#define C218tx_mask                  (C218tx_size - 1)

#define Page_size                    0x2000U
#define Page_mask                    (Page_size - 1)

/* Board types (driver hardware identifiers) */
enum {
    MOXA_BOARD_C218_PCI = 0,
    MOXA_BOARD_C320_PCI,
    MOXA_BOARD_CP204J
};

/*--------------------------------------------------------------------*/
/* Hardware state structures */

/* Per-port state to handle Extern table read/writes */
typedef struct MoxaPortState {
    uint16_t rxb_rptr;
    uint16_t rxb_wptr;
    uint16_t txb_rptr;
    uint16_t txb_wptr;
    uint16_t host_stat;
    uint16_t flag_stat;
    uint16_t flow_control;
    uint16_t break_cnt;
    uint16_t cd180txirq;
    uint16_t rx_mask;
    uint16_t tx_mask;
    uint16_t ofs_rxb;
    uint16_t ofs_txb;
    uint16_t page_rxb;
    uint16_t page_txb;
    uint16_t endpage_rxb;
    uint16_t endpage_txb;
    uint16_t data_error;
    uint16_t rx_trigger;
    uint16_t tx_trigger;
    uint16_t r_rxwptr;
    uint16_t low_water;
    uint16_t func_code;
    uint16_t func_arg;
    uint16_t func_arg1;
} MoxaPortState;

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

    /* Hardware Register Shadows */
    uint16_t page_index;   /* Control_reg value */
    bool bios_loaded;
    bool reset_active;

    /* Per-port state (8 ports for C218) */
    MoxaPortState port[8];

    /* RAM buffers */
    uint8_t *fix_ram;      /* 0x0000-0x1FFF (excluding registers) */
    uint8_t *page_ram;     /* Paged RAM: Page_size * MAX_PAGES */
#define MAX_PAGES 64
};

/*--------------------------------------------------------------------*/
/* Helper to access port register as uint16_t array */
static inline uint16_t *port_regs_ptr(PCIBaseState *s, int port)
{
    return (uint16_t *)&s->port[port];
}

/* MMIO Read Handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    int port, off;

    /* Extern table area: 0x400 - 0x400+8*0x60 */
    if (addr >= Extern_table && addr < Extern_table + 8 * Extern_size) {
        port = (addr - Extern_table) / Extern_size;
        off = (addr - Extern_table) % Extern_size;
        if (port < 8 && off < sizeof(MoxaPortState) && (off & 1) == 0) {
            uint16_t *regs = port_regs_ptr(s, port);
            val = regs[off / 2];
            /* FuncCode special: always return 0 (cleared after write) */
            if (off == FuncCode) {
                val = 0;
            }
        }
        goto size_adj;
    }

    /* Special registers */
    switch (addr) {
    case IRQindex: /* 0x00 */
        val = 0; /* intNdx returns 0 */
        break;
    case IRQpending: /* 0x04 */
        val = 0; /* intPend returns 0, no interrupts */
        break;
    case Magic_no: /* 0x108 */
        val = Magic_code; /* always magic */
        break;
    case Card_model_no: /* 0x10A */
        val = 1; /* C218 model */
        break;
    case Total_ports: /* 0x10C */
        val = 8;
        break;
    case Module_cnt: /* 0x110 */
        val = 0;
        break;
    case Disable_IRQ: /* 0x11C */
        val = 0; /* stored but not used */
        break;
    case Control_reg: /* 0x1FF0 */
        val = s->page_index;
        break;
    case C218_status: /* 0x800 */
        val = 0;
        break;
    case C218_diag: /* 0x802 */
        val = 0;
        break;
    case C218_key: /* 0x804 */
        val = s->bios_loaded ? C218_KeyCode : 0;
        break;
    case C218DLoad_len: /* 0x806 */
        val = 0; /* not stored */
        break;
    case C218check_sum: /* 0x808 */
        val = 0;
        break;
    case C218chksum_ok: /* 0x80A */
        val = 1; /* always checksum ok */
        break;
    default:
        /* RAM access */
        if (addr < FixPage_addr + Page_size) {
            /* Fixed RAM region (0x0000-0x1FFF) */
            if (addr < sizeof(s->fix_ram)) {
                val = s->fix_ram[addr];
            }
        } else if (addr >= DynPage_addr && addr < DynPage_addr + Page_size) {
            /* Dynamic page window (0x2000-0x3FFF) */
            uint32_t eff_addr = (s->page_index * Page_size) + (addr - DynPage_addr);
            if (eff_addr < MAX_PAGES * Page_size) {
                val = s->page_ram[eff_addr];
            }
        }
        break;
    }

size_adj:
    /* Adjust return value for access size */
    switch (size) {
    case 1:
        val &= 0xFF;
        break;
    case 2:
        val &= 0xFFFF;
        break;
    case 4:
        val &= 0xFFFFFFFF;
        break;
    default:
        break;
    }
    return val;
}

/* MMIO Write Handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int port, off;
    uint16_t val16 = val & 0xFFFF; /* commonly used 16-bit value */

    /* Extern table area */
    if (addr >= Extern_table && addr < Extern_table + 8 * Extern_size) {
        port = (addr - Extern_table) / Extern_size;
        off = (addr - Extern_table) % Extern_size;
        if (port < 8 && off < sizeof(MoxaPortState) && (off & 1) == 0) {
            uint16_t *regs = port_regs_ptr(s, port);
            if (off == FuncCode) {
                /* Writing to FuncCode clears it immediately */
                regs[off / 2] = 0;
            } else {
                regs[off / 2] = val16;
            }
        }
        return;
    }

    /* Special registers */
    switch (addr) {
    case IRQindex: /* 0x00 - ignore */
        break;
    case IRQpending: /* 0x04 - ack interrupt, ignore */
        break;
    case Magic_no: /* 0x108 - ignore */
        break;
    case Card_model_no: /* 0x10A - ignore */
        break;
    case Total_ports: /* 0x10C - ignore */
        break;
    case Disable_IRQ: /* 0x11C - store */
        /* Not used */
        break;
    case Control_reg: /* 0x1FF0 */
        if (size == 1 || size == 2) {
            uint16_t new_page = val16;
            if (s->reset_active && new_page == 0) {
                s->bios_loaded = true;
                s->reset_active = false;
            } else if (new_page == HW_reset) {
                s->reset_active = true;
            }
            s->page_index = new_page;
        }
        break;
    case C218_key: /* 0x804 - ignore */
        break;
    case C218DLoad_len: /* 0x806 - ignore */
        break;
    case C218check_sum: /* 0x808 - ignore */
        break;
    case C218chksum_ok: /* 0x80A - ignore */
        break;
    default:
        /* RAM access */
        if (addr < FixPage_addr + Page_size) {
            /* Fixed RAM region */
            if (addr < sizeof(s->fix_ram)) {
                if (size == 1) s->fix_ram[addr] = val & 0xFF;
                else if (size == 2) {
                    s->fix_ram[addr] = val & 0xFF;
                    s->fix_ram[addr+1] = (val >> 8) & 0xFF;
                } else {
                    /* For larger sizes, write byte by byte */
                    for (unsigned i = 0; i < size; i++) {
                        if (addr + i < sizeof(s->fix_ram)) {
                            s->fix_ram[addr + i] = (val >> (i * 8)) & 0xFF;
                        }
                    }
                }
            }
        } else if (addr >= DynPage_addr && addr < DynPage_addr + Page_size) {
            /* Dynamic page window */
            uint32_t eff_addr = (s->page_index * Page_size) + (addr - DynPage_addr);
            if (eff_addr < MAX_PAGES * Page_size) {
                if (size == 1) s->page_ram[eff_addr] = val & 0xFF;
                else if (size == 2) {
                    s->page_ram[eff_addr] = val & 0xFF;
                    s->page_ram[eff_addr+1] = (val >> 8) & 0xFF;
                } else {
                    for (unsigned i = 0; i < size; i++) {
                        if (eff_addr + i < MAX_PAGES * Page_size) {
                            s->page_ram[eff_addr + i] = (val >> (i * 8)) & 0xFF;
                        }
                    }
                }
            }
        }
        break;
    }
}

/* PIO handlers (unused) */
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
    s->page_index = 0;
    s->bios_loaded = false;
    s->reset_active = false;
    memset(s->port, 0, sizeof(s->port));
    memset(s->fix_ram, 0, sizeof(s->fix_ram));
    memset(s->page_ram, 0, MAX_PAGES * Page_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_MOXA );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_MOXA_C218 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MOXA );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization: Driver uses BAR2 */
    s->num_bars = 1;
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_MMIO, .size = 0x4000, .name = "moxa-mmio" };
    pcibase_register_bar(pdev, s, &s->bar_info[2], errp);

    /* MSI/MSI-X not used by driver */

    /* Allocate RAM for fixed and paged areas */
    s->fix_ram = g_malloc0(Page_size); /* 0x2000 bytes */
    s->page_ram = g_malloc0(MAX_PAGES * Page_size); /* 512KB */

    /* Final state initialization */
    s->page_index = 0;
    s->bios_loaded = false;
    s->reset_active = false;
    memset(s->port, 0, sizeof(s->port));
    /* Note: fix_ram and page_ram are already zeroed */
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
    g_free(s->fix_ram);
    g_free(s->page_ram);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "moxa_pci",
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
