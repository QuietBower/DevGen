/*
 * QEMU PCI device model for bt878 (Brooktree Bt878 Audio/Video Capture)
 *
 * Based on Linux driver: drivers/media/pci/bt8xx/bt878.c
 * Target QEMU: 8.2.10
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

#define TYPE_PCIBASE_DEVICE "bt878_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Related register/macros/constants from driver */
#define PCI_VENDOR_ID_BROOKTREE     0x109e
#define PCI_DEVICE_ID_BROOKTREE_878 0x0878

#define BT878_AINT_STAT     0x100
#define BT878_AINT_MASK     0x104
#define BT878_AGPIO_DMA_CTL 0x10c
#define BT878_APACK_LEN     0x110
#define BT878_ARISC_START   0x114
#define BT878_GPIO_OUT_EN   0x118
#define BT878_ARISC_PC      0x120
#define BT878_GPIO_DATA     0x200

/* Interrupt Status Bits */
#define BT878_ARISCI        (1<<11)
#define BT878_AFBUS         (1<<12)
#define BT878_AFTRGT        (1<<13)
#define BT878_AFDSR         (1<<14)
#define BT878_APPERR        (1<<15)
#define BT878_ARIPERR       (1<<16)
#define BT878_APABORT       (1<<17)
#define BT878_AOCERR        (1<<18)
#define BT878_ASCERR        (1<<19)
#define BT878_ARISC_EN      (1<<27)
#define BT878_ARISCS        (0xf<<28)

/* RISC Instructions (decoded from memory) */
#define RISC_OP_MASK        (0xF << 28)
#define RISC_WRITE          (0x1 << 28)
#define RISC_JUMP           (0x7 << 28)
#define RISC_SYNC           (0x8 << 28)
#define RISC_IRQ            (1 << 24)
#define RISC_CNT_MASK       0xFFFF

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

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Registers */
    uint32_t aint_stat;
    uint32_t aint_mask;
    uint32_t agpio_dma_ctl;
    uint32_t apack_len;
    uint32_t arisc_start;
    uint32_t arisc_pc;
    uint32_t gpio_out_en;
    uint32_t gpio_data;

    /* Internal State */
    QEMUTimer *dma_timer;
    bool risc_running;
};

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static void pcibase_update_irq(PCIBaseState *s);
static void pcibase_risc_timer(void *opaque);

/* ------------------------------------------------------------------ */
/* MMIO Handlers                                                       */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case BT878_AINT_STAT:
        val = s->aint_stat;
        break;
    case BT878_AINT_MASK:
        val = s->aint_mask;
        break;
    case BT878_AGPIO_DMA_CTL:
        val = s->agpio_dma_ctl;
        break;
    case BT878_APACK_LEN:
        val = s->apack_len;
        break;
    case BT878_ARISC_START:
        val = s->arisc_start;
        break;
    case BT878_ARISC_PC:
        val = s->arisc_pc;
        break;
    case BT878_GPIO_OUT_EN:
        val = s->gpio_out_en;
        break;
    case BT878_GPIO_DATA:
        val = s->gpio_data;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] mmio_read unhandled addr=0x%" PRIx64 "\n",
                      TYPE_PCIBASE_DEVICE, addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case BT878_AINT_STAT:
        /* W1C: Write 1 to Clear */
        s->aint_stat &= ~val;
        /* ARISC_EN (bit 27) is read-only status of the engine, preserve it */
        if (s->risc_running) {
            s->aint_stat |= BT878_ARISC_EN;
        } else {
            s->aint_stat &= ~BT878_ARISC_EN;
        }
        pcibase_update_irq(s);
        break;
    case BT878_AINT_MASK:
        s->aint_mask = val;
        pcibase_update_irq(s);
        break;
    case BT878_AGPIO_DMA_CTL:
        s->agpio_dma_ctl = val;
        /* 
         * Driver logic: 
         * Start: controlreg |= 0x1b (sets bit 0, 1, 3, 4)
         * Stop:  btand(~0x13) (clears bit 0, 1, 4)
         * We use bit 1 (0x02) as the trigger for RISC engine.
         */
        if (val & 0x02) {
            if (!s->risc_running) {
                s->risc_running = true;
                s->aint_stat |= BT878_ARISC_EN;
                s->arisc_pc = s->arisc_start;
                timer_mod(s->dma_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
            }
        } else {
            if (s->risc_running) {
                s->risc_running = false;
                s->aint_stat &= ~BT878_ARISC_EN;
                timer_del(s->dma_timer);
            }
        }
        break;
    case BT878_APACK_LEN:
        s->apack_len = val;
        break;
    case BT878_ARISC_START:
        s->arisc_start = val;
        s->arisc_pc = val;
        break;
    case BT878_ARISC_PC:
        /* Read-only usually, but allow write for debug/reset */
        s->arisc_pc = val;
        break;
    case BT878_GPIO_OUT_EN:
        s->gpio_out_en = val;
        break;
    case BT878_GPIO_DATA:
        s->gpio_data = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "[%s] mmio_write unhandled addr=0x%" PRIx64 " val=0x%" PRIx64 "\n",
                      TYPE_PCIBASE_DEVICE, addr, val);
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

/* ------------------------------------------------------------------ */
/* IRQ Logic                                                           */
/* ------------------------------------------------------------------ */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->aint_stat & s->aint_mask) != 0;
    pci_set_irq(pdev, level);
}

/* ------------------------------------------------------------------ */
/* RISC Engine Simulation                                              */
/* ------------------------------------------------------------------ */
static void pcibase_risc_timer(void *opaque)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);
    AddressSpace *as = pci_get_address_space(pdev);
    uint32_t instr, next_word;
    int steps = 0;
    const int max_steps = 64; /* Limit execution per tick */

    if (!s->risc_running) {
        return;
    }

    while (steps < max_steps && s->risc_running) {
        /* Fetch instruction */
        if (dma_memory_read(as, s->arisc_pc, &instr, 4, MEMTXATTRS_UNSPECIFIED)) {
            /* Bus error or invalid memory */
            s->risc_running = false;
            s->aint_stat &= ~BT878_ARISC_EN;
            s->aint_stat |= BT878_APABORT;
            pcibase_update_irq(s);
            return;
        }
        
        /* Advance PC past instruction word */
        s->arisc_pc += 4;

        /* Decode */
        uint32_t opcode = instr & RISC_OP_MASK;
        bool irq = (instr & RISC_IRQ) != 0;
        
        /* Handle IRQ flag in instruction */
        if (irq) {
            s->aint_stat |= BT878_ARISCI;
            /* 
             * Driver expects status bits in top nibble of AINT_STAT.
             * The driver encodes them in bits 16-19 of the instruction.
             */
            uint32_t status_nibble = (instr >> 16) & 0xF;
            s->aint_stat &= ~BT878_ARISCS;
            s->aint_stat |= (status_nibble << 28);
            pcibase_update_irq(s);
        }

        switch (opcode) {
        case RISC_WRITE:
            /* Next word is destination address */
            if (dma_memory_read(as, s->arisc_pc, &next_word, 4, MEMTXATTRS_UNSPECIFIED)) {
                s->risc_running = false;
                break;
            }
            s->arisc_pc += 4;
            
            /* Simulate data write (dummy data) */
            /* Length is in bits 0-15 of instr */
            /* We don't actually write data to avoid corrupting host memory with garbage,
               but we could write zeros if strictly required. For now, just advance. */
            break;

        case RISC_JUMP:
            /* Next word is jump target */
            if (dma_memory_read(as, s->arisc_pc, &next_word, 4, MEMTXATTRS_UNSPECIFIED)) {
                s->risc_running = false;
                break;
            }
            s->arisc_pc = next_word;
            /* Don't advance PC after jump */
            break;

        case RISC_SYNC:
            /* Next word is usually 0 or status, skip it */
            s->arisc_pc += 4;
            break;

        default:
            /* Unknown opcode, stop */
            // s->risc_running = false;
            break;
        }

        steps++;
    }

    if (s->risc_running) {
        timer_mod(s->dma_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 1);
    } else {
        s->aint_stat &= ~BT878_ARISC_EN;
    }
}

/* ------------------------------------------------------------------ */
/* Reset                                                               */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);

    s->aint_stat = 0;
    s->aint_mask = 0;
    s->agpio_dma_ctl = 0;
    s->apack_len = 0;
    s->arisc_start = 0;
    s->arisc_pc = 0;
    s->gpio_out_en = 0;
    s->gpio_data = 0;
    s->risc_running = false;
    timer_del(s->dma_timer);
    
    pcibase_update_irq(s);
}

/* ------------------------------------------------------------------ */
/* Realize                                                             */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_BROOKTREE);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_BROOKTREE_878);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, 0x0071 );
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID,        0x0101 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_AUDIO);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    
    /* BAR 0: MMIO 4KB */
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_mmio_ops, s, "bt878.mmio", 0x1000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[0]);

    s->dma_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, pcibase_risc_timer, s);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->dma_timer) {
        timer_del(s->dma_timer);
        timer_free(s->dma_timer);
        s->dma_timer = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Class init                                                          */
/* ------------------------------------------------------------------ */
static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

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
