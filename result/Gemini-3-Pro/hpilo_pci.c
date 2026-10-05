/*
 * Generic QEMU PCI device template (QEMU 8.2.x)
 *
 * Phase 4 Fix: Corrected BAR sizes to be powers of 2 to satisfy pci_register_bar assertion.
 *              Added bounds checks for MMIO/RAM accesses.
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"

/* ------------------------------------------------------------------ */
/* Additional include files                                            */
/* ------------------------------------------------------------------ */

#ifndef PCI_VENDOR_ID_COMPAQ
#define PCI_VENDOR_ID_COMPAQ 0x0e11
#endif

#define TYPE_PCIBASE_DEVICE "hpilo_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver */
#define ENTRY_BITPOS_DESCRIPTOR  10
#define ENTRY_BITS_DESCRIPTOR    12
#define ENTRY_BITS_QWORDS        10
#define ENTRY_BITPOS_QWORDS      0
#define ENTRY_BITS_C             1
#define ENTRY_BITS_O             1
#define ENTRY_BITPOS_O           23
#define ENTRY_BITPOS_C           22
#define ENTRY_BITS_TOTAL \
    (ENTRY_BITS_C + ENTRY_BITS_O + \
     ENTRY_BITS_QWORDS + ENTRY_BITS_DESCRIPTOR)

#define ENTRY_MASK_DESCRIPTOR \
    (((1 << ENTRY_BITS_DESCRIPTOR) - 1) << ENTRY_BITPOS_DESCRIPTOR)
#define ENTRY_MASK_QWORDS \
    (((1 << ENTRY_BITS_QWORDS) - 1) << ENTRY_BITPOS_QWORDS)
#define ENTRY_MASK_NOSTATE (ENTRY_MASK >> (ENTRY_BITS_C + ENTRY_BITS_O))
#define ENTRY_MASK_O (((1 << ENTRY_BITS_O) - 1) << ENTRY_BITPOS_O)
#define ENTRY_MASK ((1 << ENTRY_BITS_TOTAL) - 1)
#define ENTRY_MASK_C (((1 << ENTRY_BITS_C) - 1) << ENTRY_BITPOS_C)

#define L2_QENTRY_SZ    12
#define SENDQ           1
#define RECVQ           2

#define CTRL_BITPOS_FIFOINDEXMASK    4
#define CTRL_BITPOS_DESCLIMIT        18
#define CTRL_BITPOS_G                31
#define CTRL_BITPOS_A                30
#define CTRL_BITPOS_L2SZ             0

#define ILO_CACHE_SZ     128
#define L2_DB_SIZE      14
#define NR_QENTRY       4
#define ILO_START_ALIGN 4096
#define ILOHW_CCB_SZ    128

#define DB_OUT      0xD4
#define DB_RESET    26
#define DB_IRQ      0xB2
#define ONE_DB_SIZE     (1 << L2_DB_SIZE)

#define MIN_CCB     8
#define MAX_CCB        24
#define MAX_ILO_DEV 1

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
#define FIFOBARTOHANDLE(_fifo) \
    ((struct fifo *)(((char *)(_fifo)) - FIFOHANDLESIZE))

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
        char *db_base;
        uint64_t padding5;
    } ccb_u5;

    uint64_t channel;

    /* unused context area (64 bytes) */
};

/* ------------------------------------------------------------------ */
/* BAR metadata definition                                             */
/* ------------------------------------------------------------------ */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM 
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions */
    MemoryRegion bar_regions[6];

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt state feature flags */
    bool has_msi;
    bool has_msix;
    
    /* Device specific state */
    uint32_t db_out;
    uint8_t db_irq;
    
    /* Shared memory (BAR 2) backing */
    /* Note: BAR size is 4096 (power of 2), but actual usage is MAX_CCB * 128 = 3072 */
    uint8_t ccb_ram[MAX_CCB * ILOHW_CCB_SZ];
};

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_ram_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_ram_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_doorbell_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_doorbell_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* ------------------------------------------------------------------ */
/* MemoryRegionOps                                                      */
/* ------------------------------------------------------------------ */
static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_ram_ops = {
    .read = pcibase_ram_read,
    .write = pcibase_ram_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_doorbell_ops = {
    .read = pcibase_doorbell_read,
    .write = pcibase_doorbell_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* Internal Logic                                                      */
/* ------------------------------------------------------------------ */

static void pcibase_update_irq(PCIBaseState *s)
{
    int level = (s->db_out != 0) && (s->db_irq & 1);
    pci_set_irq(PCI_DEVICE(s), level);
}

static void pcibase_process_ccb(PCIBaseState *s, int slot)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    struct ccb *ccb_ptr;
    uint64_t send_fifobar_pa, recv_fifobar_pa;
    uint64_t head, tail, imask, entry;
    uint64_t recv_tail, recv_imask;
    struct fifo fifo_hdr;
    int processed = 0;

    if (slot < 0 || slot >= MAX_CCB) return;

    /* Access CCB in internal RAM */
    ccb_ptr = (struct ccb *)&s->ccb_ram[slot * ILOHW_CCB_SZ];

    /* Check if channel is active (A bit set) */
    if (!((ccb_ptr->send_ctrl >> CTRL_BITPOS_A) & 1)) {
        /* If G bit is set, we should set A bit to acknowledge start */
        if ((ccb_ptr->send_ctrl >> CTRL_BITPOS_G) & 1) {
            ccb_ptr->send_ctrl |= (1 << CTRL_BITPOS_A);
            ccb_ptr->recv_ctrl |= (1 << CTRL_BITPOS_A);
        } else {
            return;
        }
    }

    send_fifobar_pa = ccb_ptr->ccb_u1.send_fifobar_pa;
    recv_fifobar_pa = ccb_ptr->ccb_u3.recv_fifobar_pa;

    /* Read SEND FIFO header */
    if (pci_dma_read(pdev, send_fifobar_pa, &fifo_hdr, sizeof(fifo_hdr))) return;

    head = fifo_hdr.head;
    tail = fifo_hdr.tail;
    imask = fifo_hdr.imask;

    /* Loopback: Move from SENDQ to RECVQ */
    while ((head & imask) != (tail & imask)) {
        /* Read entry from SENDQ */
        hwaddr entry_addr = send_fifobar_pa + sizeof(struct fifo) + (head & imask) * 8;
        if (pci_dma_read(pdev, entry_addr, &entry, 8)) break;

        /* Update SENDQ Head */
        head++;
        
        /* Read RECV FIFO header to get tail */
        if (pci_dma_read(pdev, recv_fifobar_pa, &fifo_hdr, sizeof(fifo_hdr))) break;
        recv_tail = fifo_hdr.tail;
        recv_imask = fifo_hdr.imask;

        /* Write entry to RECVQ with C bit set (Completion) */
        entry |= ENTRY_MASK_C;
        hwaddr recv_entry_addr = recv_fifobar_pa + sizeof(struct fifo) + (recv_tail & recv_imask) * 8;
        if (pci_dma_write(pdev, recv_entry_addr, &entry, 8)) break;

        /* Update RECVQ Tail */
        recv_tail++;
        if (pci_dma_write(pdev, recv_fifobar_pa + offsetof(struct fifo, tail), &recv_tail, 8)) break;

        processed = 1;
    }

    /* Update SENDQ Head in memory */
    if (processed) {
        pci_dma_write(pdev, send_fifobar_pa + offsetof(struct fifo, head), &head, 8);
        
        /* Notify driver via DB_OUT */
        s->db_out |= (1 << slot);
        pcibase_update_irq(s);
    }
}

/* ------------------------------------------------------------------ */
/* MMIO / PIO handlers                                                 */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case DB_OUT:
        val = s->db_out;
        break;
    case DB_IRQ:
        val = s->db_irq;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] mmio_read unhandled addr=0x%"HWADDR_PRIx"\n", TYPE_PCIBASE_DEVICE, addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case DB_OUT:
        /* Driver writes to clear bits */
        s->db_out &= ~val;
        pcibase_update_irq(s);
        break;
    case DB_IRQ:
        s->db_irq = val & 0xFF;
        pcibase_update_irq(s);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] mmio_write unhandled addr=0x%"HWADDR_PRIx"\n", TYPE_PCIBASE_DEVICE, addr);
        break;
    }
}

/* BAR 2: Shared RAM (CCBs) */
static uint64_t pcibase_ram_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size <= sizeof(s->ccb_ram)) {
        memcpy(&val, &s->ccb_ram[addr], size);
    }
    return val;
}

static void pcibase_ram_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int slot = addr / ILOHW_CCB_SZ;
    int offset = addr % ILOHW_CCB_SZ;

    /* Ensure write is within the actual backing store size */
    if (addr + size <= sizeof(s->ccb_ram)) {
        memcpy(&s->ccb_ram[addr], &val, size);

        /* Intercept writes to send_ctrl/recv_ctrl to handle G/A bits */
        if (offset == offsetof(struct ccb, send_ctrl) || offset == offsetof(struct ccb, recv_ctrl)) {
            struct ccb *ccb = (struct ccb *)&s->ccb_ram[slot * ILOHW_CCB_SZ];
            uint32_t ctrl = (offset == offsetof(struct ccb, send_ctrl)) ? ccb->send_ctrl : ccb->recv_ctrl;
            
            /* If G bit is cleared, clear A bit immediately (stop) */
            if (!((ctrl >> CTRL_BITPOS_G) & 1)) {
                if (offset == offsetof(struct ccb, send_ctrl))
                    ccb->send_ctrl &= ~(1 << CTRL_BITPOS_A);
                else
                    ccb->recv_ctrl &= ~(1 << CTRL_BITPOS_A);
            } else {
                /* If G bit is set, set A bit (start) */
                 if (offset == offsetof(struct ccb, send_ctrl))
                    ccb->send_ctrl |= (1 << CTRL_BITPOS_A);
                else
                    ccb->recv_ctrl |= (1 << CTRL_BITPOS_A);
            }
        }
    }
}

/* BAR 3: Doorbell */
static uint64_t pcibase_doorbell_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_doorbell_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int slot = addr >> L2_DB_SIZE;

    if (val == 1) {
        /* Doorbell Set: Process commands */
        /* pcibase_process_ccb handles bounds checking for slot */
        pcibase_process_ccb(s, slot);
    } else if (val == 2) {
        /* Doorbell Clear: No-op or ack */
    }
}

/* ------------------------------------------------------------------ */
/* Reset       */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    s->db_out = 0;
    s->db_irq = 0;
    memset(s->ccb_ram, 0, sizeof(s->ccb_ram));
}

/* ------------------------------------------------------------------ */
/* PCI config space access (class-level hooks)                         */
/* ------------------------------------------------------------------ */
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    pci_default_write_config(pdev, addr, val, len);
}

/* ------------------------------------------------------------------ */
/* Realize (device init)                                                */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_COMPAQ);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0xB204);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Define BARs */
    /* BAR 1: MMIO Registers (256 bytes) */
    s->bar_info[0].index = 1;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 256;
    s->bar_info[0].name = "hpilo-mmio";

    /* BAR 2: Shared RAM (CCBs) */
    /* Size must be power of 2. MAX_CCB(24)*128 = 3072 -> 4096 */
    s->bar_info[1].index = 2;
    s->bar_info[1].type = BAR_TYPE_RAM;
    s->bar_info[1].size = 4096;
    s->bar_info[1].name = "hpilo-ram";

    /* BAR 3: Doorbell (Sparse) */
    /* Size must be power of 2. MAX_CCB(24)*16384 = 393216 -> 524288 */
    s->bar_info[2].index = 3;
    s->bar_info[2].type = BAR_TYPE_MMIO;
    s->bar_info[2].size = 524288;
    s->bar_info[2].name = "hpilo-doorbell";
    
    /* Register BARs */
    s->num_bars = 3;
    for (int i = 0; i < s->num_bars; i++) {
        if (s->bar_info[i].type == BAR_TYPE_RAM) {
            /* Use custom ops for RAM to intercept writes */
            memory_region_init_io(&s->bar_regions[s->bar_info[i].index], OBJECT(s), 
                                  &pcibase_ram_ops, s, s->bar_info[i].name, s->bar_info[i].size);
            pci_register_bar(pdev, s->bar_info[i].index, PCI_BASE_ADDRESS_SPACE_MEMORY, 
                             &s->bar_regions[s->bar_info[i].index]);
        } else if (s->bar_info[i].type == BAR_TYPE_MMIO) {
            const MemoryRegionOps *ops = (s->bar_info[i].index == 3) ? &pcibase_doorbell_ops : &pcibase_mmio_ops;
            memory_region_init_io(&s->bar_regions[s->bar_info[i].index], OBJECT(s), 
                                  ops, s, s->bar_info[i].name, s->bar_info[i].size);
            pci_register_bar(pdev, s->bar_info[i].index, PCI_BASE_ADDRESS_SPACE_MEMORY, 
                             &s->bar_regions[s->bar_info[i].index]);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                      */
/* ------------------------------------------------------------------ */
static void pcibase_uninit(PCIDevice *pdev)
{
    /* No special cleanup needed for simple regions */
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                      */
/* ------------------------------------------------------------------ */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static void pcibase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
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
