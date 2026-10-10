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

#define TYPE_PCIBASE_DEVICE "e100_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#ifndef PCI_VENDOR_ID_INTEL
#define PCI_VENDOR_ID_INTEL 0x8086
#endif
#define E100_DEVICE_ID 0x1029
#define E100_CLASS_ID 0x0200

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

struct rfd {
    uint16_t status;
    uint16_t command;
    uint32_t link;
    uint32_t rbd;
    uint16_t actual_size;
    uint16_t size;
};

struct config {
    uint8_t bytes[23];
    uint8_t pad_d102[9];
};

struct stats {
    uint32_t tx_good_frames, tx_max_collisions, tx_late_collisions,
        tx_underruns, tx_lost_crs, tx_deferred, tx_single_collisions,
        tx_multiple_collisions, tx_total_collisions;
    uint32_t rx_good_frames, rx_crc_errors, rx_alignment_errors,
        rx_resource_errors, rx_overrun_errors, rx_cdt_errors,
        rx_short_frame_errors;
    uint32_t fc_xmt_pause, fc_rcv_pause, fc_rcv_unsupported;
    uint16_t xmt_tco_frames, rcv_tco_frames;
    uint32_t complete;
};

struct mem {
    struct {
        uint32_t signature;
        uint32_t result;
    } selftest;
    struct stats stats;
    uint8_t dump_buf[596];
};

enum eeprom_phy_iface {
    NoSuchPhy = 0,
    I82553AB,
    I82553C,
    I82503,
    DP83840,
    S80C240,
    S80C24,
    I82555,
    DP83840A = 10,
};

enum eeprom_id {
    eeprom_id_wol = 0x0020,
};

enum eeprom_config_asf {
    eeprom_asf = 0x8000,
    eeprom_gcl = 0x4000,
};

enum eeprom_cnfg_mdix {
    eeprom_mdix_enabled = 0x0080,
};

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
    struct csr {
        struct {
            uint8_t status;
            uint8_t stat_ack;
            uint8_t cmd_lo;
            uint8_t cmd_hi;
            uint32_t gen_ptr;
        } scb;
        uint32_t port;
        uint16_t flash_ctrl;
        uint8_t eeprom_ctrl_lo;
        uint8_t eeprom_ctrl_hi;
        uint32_t mdi_ctrl;
        uint32_t rx_dma_count;
    } csr;

    /* DMA Context */
    dma_addr_t cbs_dma_addr;
    uint32_t rx_dma_count;

    uint8_t ru_running;
    uint16_t eeprom[256];

    /* EEPROM State Machine */
    uint32_t eeprom_shift_in;
    uint8_t eeprom_read_bits;
    uint16_t eeprom_read_data;
    bool eeprom_do;
};

enum scb_status {
    rus_no_res       = 0x08,
    rus_ready        = 0x10,
    rus_mask         = 0x3C,
};

enum ru_state  {
    RU_SUSPENDED = 0,
    RU_RUNNING   = 1,
    RU_UNINITIALIZED = -1,
};

enum scb_stat_ack {
    stat_ack_not_ours    = 0x00,
    stat_ack_sw_gen      = 0x04,
    stat_ack_rnr         = 0x10,
    stat_ack_cu_idle     = 0x20,
    stat_ack_frame_rx    = 0x40,
    stat_ack_cu_cmd_done = 0x80,
    stat_ack_not_present = 0xFF,
    stat_ack_rx = (stat_ack_sw_gen | stat_ack_rnr | stat_ack_frame_rx),
    stat_ack_tx = (stat_ack_cu_idle | stat_ack_cu_cmd_done),
};

enum scb_cmd_hi {
    irq_mask_none = 0x00,
    irq_mask_all  = 0x01,
    irq_sw_gen    = 0x02,
};

enum scb_cmd_lo {
    cuc_nop        = 0x00,
    ruc_start      = 0x01,
    ruc_load_base  = 0x06,
    cuc_start      = 0x10,
    cuc_resume     = 0x20,
    cuc_dump_addr  = 0x40,
    cuc_dump_stats = 0x50,
    cuc_load_base  = 0x60,
    cuc_dump_reset = 0x70,
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool irq_enabled = !(s->csr.scb.cmd_hi & irq_mask_all);
    bool irq_pending = (s->csr.scb.stat_ack & 0xFC) != 0;

    if (irq_enabled && irq_pending) {
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

    if (addr == 0x00 && size == 1) {
        val = s->csr.scb.status;
    } else if (addr == 0x01 && size == 1) {
        val = s->csr.scb.stat_ack;
    } else if (addr == 0x02 && size == 1) {
        val = s->csr.scb.cmd_lo;
    } else if (addr == 0x03 && size == 1) {
        val = s->csr.scb.cmd_hi;
    } else if (addr == 0x00 && size == 2) {
        val = s->csr.scb.status | (s->csr.scb.stat_ack << 8);
    } else if (addr == 0x02 && size == 2) {
        val = s->csr.scb.cmd_lo | (s->csr.scb.cmd_hi << 8);
    } else if (addr == 0x04 && size == 4) {
        val = s->csr.scb.gen_ptr;
    } else if (addr == 0x08 && size == 4) {
        val = s->csr.port;
    } else if (addr == 0x0C && size == 2) {
        val = s->csr.flash_ctrl;
    } else if (addr == 0x0E && size == 1) {
        val = s->csr.eeprom_ctrl_lo | (s->eeprom_do ? 0x08 : 0x00);
    } else if (addr == 0x0F && size == 1) {
        val = s->csr.eeprom_ctrl_hi;
    } else if (addr == 0x10 && size == 4) {
        val = s->csr.mdi_ctrl;
    } else if (addr == 0x14 && size == 4) {
        val = s->csr.rx_dma_count;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr == 0x00 && size == 1) {
        s->csr.scb.status = val;
    } else if (addr == 0x01 && size == 1) {
        s->csr.scb.stat_ack &= ~val;
        pcibase_update_irq(s);
    } else if (addr == 0x02 && size == 1) {
        uint8_t cmd = val;
        if (cmd == cuc_dump_addr) {
            s->cbs_dma_addr = s->csr.scb.gen_ptr;
        } else if (cmd == cuc_dump_reset) {
            struct stats st;
            memset(&st, 0, sizeof(st));
            pci_dma_write(PCI_DEVICE(s), s->cbs_dma_addr, &st, sizeof(st));
            s->csr.scb.stat_ack |= stat_ack_cu_cmd_done;
            pcibase_update_irq(s);
        } else if (cmd == cuc_load_base) {
            s->cbs_dma_addr = s->csr.scb.gen_ptr;
        }
        s->csr.scb.cmd_lo = 0;
    } else if (addr == 0x03 && size == 1) {
        s->csr.scb.cmd_hi = val & ~irq_sw_gen;
        if (val & irq_sw_gen) {
            s->csr.scb.stat_ack |= stat_ack_sw_gen;
        }
        pcibase_update_irq(s);
    } else if (addr == 0x04 && size == 4) {
        s->csr.scb.gen_ptr = val;
    } else if (addr == 0x08 && size == 4) {
        s->csr.port = val;
    } else if (addr == 0x0C && size == 2) {
        s->csr.flash_ctrl = val;
    } else if (addr == 0x0E && size == 1) {
        uint8_t old_val = s->csr.eeprom_ctrl_lo;
        uint8_t new_val = val & ~0x08; // EE_DO is read-only
        s->csr.eeprom_ctrl_lo = new_val;

        if (!(new_val & 0x02)) { // EE_CS
            s->eeprom_shift_in = 0;
            s->eeprom_read_bits = 0;
            s->eeprom_do = true;
        } else if ((new_val & 0x01) && !(old_val & 0x01)) { // Rising edge of EE_SK
            s->eeprom_shift_in = (s->eeprom_shift_in << 1) | ((new_val & 0x04) ? 1 : 0); // EE_DI
            
            if (s->eeprom_read_bits == 0) {
                if ((s->eeprom_shift_in >= 0x600 && s->eeprom_shift_in <= 0x6FF) ||
                    (s->eeprom_shift_in >= 0x200 && s->eeprom_shift_in <= 0x2FF)) {
                    uint8_t eeprom_addr = s->eeprom_shift_in & 0xFF;
                    s->eeprom_read_data = s->eeprom[eeprom_addr];
                    s->eeprom_read_bits = 17;
                    s->eeprom_do = false;
                    s->eeprom_shift_in = 0;
                } else {
                    s->eeprom_do = true;
                }
            } else {
                s->eeprom_read_bits--;
                if (s->eeprom_read_bits > 0 && s->eeprom_read_bits <= 16) {
                    s->eeprom_do = (s->eeprom_read_data & (1 << (s->eeprom_read_bits - 1))) != 0;
                } else {
                    s->eeprom_do = true;
                }
            }
        }
    } else if (addr == 0x0F && size == 1) {
        s->csr.eeprom_ctrl_hi = val;
    } else if (addr == 0x10 && size == 4) {
        s->csr.mdi_ctrl = val;
    } else if (addr == 0x14 && size == 4) {
        s->csr.rx_dma_count = val;
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

    s->csr.scb.status = 0;
    s->csr.scb.stat_ack = 0;
    s->csr.scb.cmd_lo = 0;
    s->csr.scb.cmd_hi = 0;
    s->csr.scb.gen_ptr = 0;
    s->csr.port = 0;
    s->csr.flash_ctrl = 0;
    s->csr.eeprom_ctrl_lo = 0;
    s->csr.eeprom_ctrl_hi = 0;
    s->csr.mdi_ctrl = 0;
    s->csr.rx_dma_count = 0;
    s->ru_running = RU_UNINITIALIZED;

    s->eeprom_shift_in = 0;
    s->eeprom_read_bits = 0;
    s->eeprom_read_data = 0;
    s->eeprom_do = true;

    uint16_t checksum = 0;
    memset(s->eeprom, 0, sizeof(s->eeprom));
    /* Set a valid MAC address */
    s->eeprom[0] = 0x5452;
    s->eeprom[1] = 0x1200;
    s->eeprom[2] = 0x5634;
    for (int i = 0; i < 255; i++) {
        checksum += s->eeprom[i];
    }
    s->eeprom[255] = 0xBABA - checksum;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INTEL );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  E100_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, E100_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = sizeof(struct csr);
    s->bar_info[0].name = "e100-mmio";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_PIO;
    s->bar_info[1].size = sizeof(struct csr);
    s->bar_info[1].name = "e100-pio";

    /* BAR Initialization */
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
    .name = "e100_pci",
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
