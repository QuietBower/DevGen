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


#define TYPE_PCIBASE_DEVICE "hpilo_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_COMPAQ     0x0e11
#define ILO_DEVICE_ID            0xB204
#define PCI_REV_ID_NECHES        7

#define DB_OUT                   0xD4
#define DB_RESET                 26
#define DB_IRQ                   0xB2

#define ENTRY_BITS_DESCRIPTOR    12
#define ENTRY_BITS_QWORDS        10
#define ENTRY_BITS_O             1
#define ENTRY_BITS_C             1
#define ENTRY_BITPOS_DESCRIPTOR  10
#define ENTRY_BITPOS_QWORDS      0
#define ENTRY_BITPOS_O           23
#define ENTRY_BITPOS_C           22

#define ENTRY_BITS_TOTAL         (ENTRY_BITS_C + ENTRY_BITS_O + ENTRY_BITS_QWORDS + ENTRY_BITS_DESCRIPTOR)
#define ENTRY_MASK               ((1 << ENTRY_BITS_TOTAL) - 1)
#define ENTRY_MASK_DESCRIPTOR    (((1 << ENTRY_BITS_DESCRIPTOR) - 1) << ENTRY_BITPOS_DESCRIPTOR)
#define ENTRY_MASK_QWORDS        (((1 << ENTRY_BITS_QWORDS) - 1) << ENTRY_BITPOS_QWORDS)
#define ENTRY_MASK_O             (((1 << ENTRY_BITS_O) - 1) << ENTRY_BITPOS_O)
#define ENTRY_MASK_C             (((1 << ENTRY_BITS_C) - 1) << ENTRY_BITPOS_C)
#define ENTRY_MASK_NOSTATE       (ENTRY_MASK >> (ENTRY_BITS_C + ENTRY_BITS_O))

#define L2_QENTRY_SZ             12
#define SENDQ                    1
#define RECVQ                    2

#define CTRL_BITPOS_G            31
#define CTRL_BITPOS_DESCLIMIT    18
#define CTRL_BITPOS_A            30
#define CTRL_BITPOS_L2SZ         0
#define CTRL_BITPOS_FIFOINDEXMASK 4

#define ILO_START_ALIGN          4096
#define ILO_CACHE_SZ             128
#define NR_QENTRY                4
#define L2_DB_SIZE               14
#define ILOHW_CCB_SZ             128
#define ONE_DB_SIZE              (1 << L2_DB_SIZE)

#define MAX_ILO_DEV              1
#define MAX_CCB                  24
#define MIN_CCB                  8
#define MAX_OPEN                 (MAX_CCB * MAX_ILO_DEV)

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
    

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t db_out;
    uint32_t db_reset;
    uint32_t db_irq;

    /* DMA Context */
    

    
};

struct fifo {
    uint64_t nrents;
    uint64_t imask;
    uint64_t merge;
    uint64_t reset;
    uint8_t  pad_0[ILO_CACHE_SZ - (sizeof(uint64_t) * 4)];

    uint64_t head;
    uint8_t  pad_1[ILO_CACHE_SZ - (sizeof(uint64_t))];

    uint64_t tail;
    uint8_t  pad_2[ILO_CACHE_SZ - (sizeof(uint64_t))];

    uint64_t fifobar[];
};

#define FIFOHANDLESIZE (sizeof(struct fifo))

struct ccb {
    union {
        char *send_fifobar;
        uint64_t send_fifobar_pa;
    } ccb_u1;
    union {
        char *send_desc;
        uint64_t send_desc_pa;
    } ccb_u2;
    uint64_t send_ctrl;

    union {
        char *recv_fifobar;
        uint64_t recv_fifobar_pa;
    } ccb_u3;
    union {
        char *recv_desc;
        uint64_t recv_desc_pa;
    } ccb_u4;
    uint64_t recv_ctrl;

    union {
        uint64_t db_base;
        uint64_t padding5;
    } ccb_u5;

    uint64_t channel;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    bool should_assert = (s->db_out != 0) && (s->db_irq & 1);
    pci_set_irq(pdev, should_assert ? 1 : 0);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    MemoryRegion *ram_mr = &s->bar_regions[2];
    void *ram_ptr = memory_region_get_ram_ptr(ram_mr);
    if (!ram_ptr) return;

    for (int slot = 0; slot < MAX_CCB; slot++) {
        uint8_t *ccb_ptr = (uint8_t *)ram_ptr + slot * ILOHW_CCB_SZ;

        uint32_t send_ctrl = ldl_le_p(ccb_ptr + 16);
        uint32_t recv_ctrl = ldl_le_p(ccb_ptr + 40);

        bool active = false;

        if (!(send_ctrl & (1 << CTRL_BITPOS_G)) && (send_ctrl & (1 << CTRL_BITPOS_A))) {
            send_ctrl &= ~(1 << CTRL_BITPOS_A);
            stl_le_p(ccb_ptr + 16, send_ctrl);
        } else if ((send_ctrl & (1 << CTRL_BITPOS_G)) && !(send_ctrl & (1 << CTRL_BITPOS_A))) {
            send_ctrl |= (1 << CTRL_BITPOS_A);
            stl_le_p(ccb_ptr + 16, send_ctrl);
            active = true;
        } else if (send_ctrl & (1 << CTRL_BITPOS_A)) {
            active = true;
        }

        if (!(recv_ctrl & (1 << CTRL_BITPOS_G)) && (recv_ctrl & (1 << CTRL_BITPOS_A))) {
            recv_ctrl &= ~(1 << CTRL_BITPOS_A);
            stl_le_p(ccb_ptr + 40, recv_ctrl);
        } else if ((recv_ctrl & (1 << CTRL_BITPOS_G)) && !(recv_ctrl & (1 << CTRL_BITPOS_A))) {
            recv_ctrl |= (1 << CTRL_BITPOS_A);
            stl_le_p(ccb_ptr + 40, recv_ctrl);
        }

        if (!active) continue;

        uint64_t send_fifo_pa = ldq_le_p(ccb_ptr + 0);
        uint64_t recv_fifo_pa = ldq_le_p(ccb_ptr + 24);

        if (!send_fifo_pa || !recv_fifo_pa) continue;

        bool processed = false;

        for (int i = 0; i < NR_QENTRY; i++) {
            uint64_t entry;
            pci_dma_read(pdev, send_fifo_pa + i * 8, &entry, 8);
            entry = le64_to_cpu(entry);

            if ((entry & ENTRY_MASK_O) && !(entry & ENTRY_MASK_C)) {
                entry = (entry & ~ENTRY_MASK_O) | ENTRY_MASK_C;
                uint64_t le_entry = cpu_to_le64(entry);
                pci_dma_write(pdev, send_fifo_pa + i * 8, &le_entry, 8);
                processed = true;

                for (int j = 0; j < NR_QENTRY; j++) {
                    uint64_t rentry;
                    pci_dma_read(pdev, recv_fifo_pa + j * 8, &rentry, 8);
                    rentry = le64_to_cpu(rentry);

                    if ((rentry & ENTRY_MASK_O) && !(rentry & ENTRY_MASK_C)) {
                        rentry = (rentry & ~ENTRY_MASK_O) | ENTRY_MASK_C;
                        uint64_t le_rentry = cpu_to_le64(rentry);
                        pci_dma_write(pdev, recv_fifo_pa + j * 8, &le_rentry, 8);
                        break;
                    }
                }
            }
        }

        if (processed) {
            s->db_out |= (1 << slot);
            pcibase_update_irq(s);
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr == DB_OUT && size == 4) {
        val = s->db_out;
    } else if (addr == DB_IRQ && size == 1) {
        val = s->db_irq;
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr == DB_OUT && size == 4) {
        s->db_out &= ~val;
        pcibase_update_irq(s);
    } else if (addr == DB_IRQ && size == 1) {
        s->db_irq = val;
        pcibase_update_irq(s);
    } else if ((addr & (ONE_DB_SIZE - 1)) == 0 && size == 1) {
        pcibase_do_dma(s, false);
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    
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

    s->db_out = 1 << DB_RESET;
    s->db_irq = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_COMPAQ );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  ILO_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 6;
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "ilo-mmio" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_RAM, .size = MAX_CCB * ILOHW_CCB_SZ, .name = "ilo-ram" };
    s->bar_info[3] = (BARInfo){ .index = 3, .type = BAR_TYPE_MMIO, .size = MAX_CCB * ONE_DB_SIZE, .name = "ilo-db" };
    s->bar_info[5] = (BARInfo){ .index = 5, .type = BAR_TYPE_RAM, .size = 0x2000 + MAX_CCB * ILOHW_CCB_SZ, .name = "ilo-ram-neches" };
      
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
    .name = "hpilo_pci",
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
