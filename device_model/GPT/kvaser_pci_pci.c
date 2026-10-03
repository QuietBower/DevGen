/*
 * QEMU PCI device model for kvaser_pci (SJA1000 based CAN)
 * Generated for behavioral compatibility with
 * linux/drivers/net/can/sja1000/kvaser_pci.c
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
/* Removed non-existent include: hw/net/can_sja1000_regs.h */

#define TYPE_PCIBASE_DEVICE "kvaser_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define KVASER_PCI_VENDOR_ID1     0x10e8
#define KVASER_PCI_DEVICE_ID1     0x8406
#define KVASER_PCI_VENDOR_ID2     0x1a07
#define KVASER_PCI_DEVICE_ID2     0x0008

#define KVASER_PCI_PORT_BYTES     0x20
#define PCI_CONFIG_PORT_SIZE      0x80
#define PCI_PORT_SIZE             0x80
#define PCI_PORT_XILINX_SIZE      0x08

#define S5920_INTCSR              0x38
#define S5920_PTCR                0x60
#define INTCSR_ADDON_INTENABLE_M  0x2000

#define KVASER_PCI_CAN_CLOCK      (16000000 / 2)

#define SJA1000_ECHO_SKB_MAX      1
#define SJA1000_QUIRK_NO_CDR_REG  (1U << 1)
#define SJA1000_CUSTOM_IRQ_HANDLER (1U << 0)

/* Use generic COMMUNICATION_CONTROLLER class as CAN-specific is unavailable */
#ifndef PCI_CLASS_COMMUNICATION_CONTROLLER
#define PCI_CLASS_COMMUNICATION_CONTROLLER 0x0700
#endif
#define KVASER_PCI_CLASS_ID       PCI_CLASS_COMMUNICATION_CONTROLLER

/* Minimal SJA1000 register offsets used by the driver */
#ifndef SJA1000_MOD
#define SJA1000_MOD 0x00
#endif

/* Xilinx version/interrupt register offset (from driver: XILINX_VERINT) */
#ifndef XILINX_VERINT
#define XILINX_VERINT 0x00
#endif

/* Kvaser specific OCR/CDR constants (driver uses KVASER_PCI_OCR/CDR) */
#ifndef KVASER_PCI_OCR
#define KVASER_PCI_OCR 0x00
#endif
#ifndef KVASER_PCI_CDR
#define KVASER_PCI_CDR 0x00
#endif

/* SJA1000 mode register bits */
#ifndef MOD_RM
#define MOD_RM 0x01
#endif

/* Generic BAR info */
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
    /* BAR0: S5920 config space (at least INTCSR/PTCR within first 0x80 bytes) */
    uint8_t bar0_conf[PCI_CONFIG_PORT_SIZE];

    /* BAR1: SJA1000 channels (PIO/MMIO space used via ioread8/iowrite8) */
    uint8_t bar1_can[PCI_PORT_SIZE];

    /* BAR2: Xilinx board-wide area (at least 0x8 bytes) */
    uint8_t bar2_xilinx[PCI_PORT_XILINX_SIZE];

    /* Interrupt state */
    uint32_t intcsr_shadow;   /* Mirrors S5920_INTCSR (offset 0x38) */
    uint32_t ptcr_shadow;     /* Mirrors S5920_PTCR (offset 0x60) */
    bool irq_asserted;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The driver only enables/disables interrupts via INTCSR_ADDON_INTENABLE_M.
     * We model a simple level-triggered interrupt: if the enable bit is set,
     * assert INTx; otherwise deassert.
     */
    bool enabled = (s->intcsr_shadow & INTCSR_ADDON_INTENABLE_M) != 0;

    if (enabled && !s->irq_asserted) {
        s->irq_asserted = true;
        pci_set_irq(pdev, 1);
    } else if (!enabled && s->irq_asserted) {
        s->irq_asserted = false;
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns
 * The kvaser_pci driver does not program any DMA engine directly for this
 * device model, so we leave this function empty.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

/* Helpers to map guest physical address to our BAR regions.
 * We rely on QEMU to dispatch to per-BAR MemoryRegionOps, so the
 * MMIO handlers only see offsets within that BAR.
 */

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Default read returns zero if out of range */

    /* BAR0 config space (S5920) */
    if (addr < PCI_CONFIG_PORT_SIZE) {
        switch (size) {
        case 1:
            val = s->bar0_conf[addr];
            break;
        case 2:
            if (addr + 1 < PCI_CONFIG_PORT_SIZE) {
                val = s->bar0_conf[addr] |
                      ((uint16_t)s->bar0_conf[addr + 1] << 8);
            }
            break;
        case 4:
            if (addr + 3 < PCI_CONFIG_PORT_SIZE) {
                val = s->bar0_conf[addr] |
                      ((uint32_t)s->bar0_conf[addr + 1] << 8) |
                      ((uint32_t)s->bar0_conf[addr + 2] << 16) |
                      ((uint32_t)s->bar0_conf[addr + 3] << 24);
            }
            break;
        default:
            break;
        }

        /* Provide direct shadow read for specific known registers */
        if (addr == S5920_INTCSR && size == 4) {
            val = s->intcsr_shadow;
        } else if (addr == S5920_PTCR && size == 4) {
            val = s->ptcr_shadow;
        }

        return val;
    }

    /* BAR1 CAN controller address space: driver uses ioread8 per byte */
    if (addr >= PCI_CONFIG_PORT_SIZE && addr < PCI_CONFIG_PORT_SIZE + PCI_PORT_SIZE) {
        hwaddr off = addr - PCI_CONFIG_PORT_SIZE;
        if (off < PCI_PORT_SIZE) {
            if (size == 1) {
                val = s->bar1_can[off];
            } else if (size == 2 && off + 1 < PCI_PORT_SIZE) {
                val = s->bar1_can[off] |
                      ((uint16_t)s->bar1_can[off + 1] << 8);
            } else if (size == 4 && off + 3 < PCI_PORT_SIZE) {
                val = s->bar1_can[off] |
                      ((uint32_t)s->bar1_can[off + 1] << 8) |
                      ((uint32_t)s->bar1_can[off + 2] << 16) |
                      ((uint32_t)s->bar1_can[off + 3] << 24);
            }
            return val;
        }
    }

    /* BAR2 Xilinx space: small 8-byte region; only ioread8(XILINX_VERINT) >> 4
     * is used. We'll return a fixed version value in upper nibble.
     */
    if (addr >= PCI_CONFIG_PORT_SIZE + PCI_PORT_SIZE &&
        addr < PCI_CONFIG_PORT_SIZE + PCI_PORT_SIZE + PCI_PORT_XILINX_SIZE) {
        hwaddr off = addr - (PCI_CONFIG_PORT_SIZE + PCI_PORT_SIZE);
        if (off < PCI_PORT_XILINX_SIZE) {
            uint8_t b = s->bar2_xilinx[off];
            /* For XILINX_VERINT, ensure a non-zero version in high nibble */
            if (off == XILINX_VERINT) {
                b = (0x1u << 4); /* version = 1 */
                s->bar2_xilinx[off] = b;
            }
            if (size == 1) {
                val = b;
            } else if (size == 2 && off + 1 < PCI_PORT_XILINX_SIZE) {
                val = b |
                      ((uint16_t)s->bar2_xilinx[off + 1] << 8);
            } else if (size == 4 && off + 3 < PCI_PORT_XILINX_SIZE) {
                val = b |
                      ((uint32_t)s->bar2_xilinx[off + 1] << 8) |
                      ((uint32_t)s->bar2_xilinx[off + 2] << 16) |
                      ((uint32_t)s->bar2_xilinx[off + 3] << 24);
            }
            return val;
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* BAR0: S5920 registers (INTCSR/PTCR) */
    if (addr < PCI_CONFIG_PORT_SIZE) {
        uint32_t v32 = (uint32_t)val;
        switch (size) {
        case 1:
            s->bar0_conf[addr] = (uint8_t)v32;
            break;
        case 2:
            if (addr + 1 < PCI_CONFIG_PORT_SIZE) {
                s->bar0_conf[addr] = (uint8_t)(v32 & 0xff);
                s->bar0_conf[addr + 1] = (uint8_t)((v32 >> 8) & 0xff);
            }
            break;
        case 4:
            if (addr + 3 < PCI_CONFIG_PORT_SIZE) {
                s->bar0_conf[addr] = (uint8_t)(v32 & 0xff);
                s->bar0_conf[addr + 1] = (uint8_t)((v32 >> 8) & 0xff);
                s->bar0_conf[addr + 2] = (uint8_t)((v32 >> 16) & 0xff);
                s->bar0_conf[addr + 3] = (uint8_t)((v32 >> 24) & 0xff);
            }
            break;
        default:
            break;
        }

        if (addr == S5920_INTCSR && size == 4) {
            s->intcsr_shadow = v32;
            pcibase_update_irq(s);
        } else if (addr == S5920_PTCR && size == 4) {
            s->ptcr_shadow = v32;
            /* The driver asserts PTADR# by writing 0x80808080; no
             * further emulation is required for probe to succeed.
             */
        }
        return;
    }

    /* BAR1: SJA1000 CAN space */
    if (addr >= PCI_CONFIG_PORT_SIZE && addr < PCI_CONFIG_PORT_SIZE + PCI_PORT_SIZE) {
        hwaddr off = addr - PCI_CONFIG_PORT_SIZE;
        if (off < PCI_PORT_SIZE) {
            uint32_t v32 = (uint32_t)val;
            switch (size) {
            case 1:
                s->bar1_can[off] = (uint8_t)v32;
                /* Emulate basic SJA1000 behavior for MOD_RM bit: when
                 * driver writes MOD_RM (reset mode) and then reads back,
                 * it expects the bit to remain set during probe.
                 * We keep the value as written, so readback matches.
                 */
                break;
            case 2:
                if (off + 1 < PCI_PORT_SIZE) {
                    s->bar1_can[off] = (uint8_t)(v32 & 0xff);
                    s->bar1_can[off + 1] = (uint8_t)((v32 >> 8) & 0xff);
                }
                break;
            case 4:
                if (off + 3 < PCI_PORT_SIZE) {
                    s->bar1_can[off] = (uint8_t)(v32 & 0xff);
                    s->bar1_can[off + 1] = (uint8_t)((v32 >> 8) & 0xff);
                    s->bar1_can[off + 2] = (uint8_t)((v32 >> 16) & 0xff);
                    s->bar1_can[off + 3] = (uint8_t)((v32 >> 24) & 0xff);
                }
                break;
            default:
                break;
            }
            return;
        }
    }

    /* BAR2: Xilinx space */
    if (addr >= PCI_CONFIG_PORT_SIZE + PCI_PORT_SIZE &&
        addr < PCI_CONFIG_PORT_SIZE + PCI_PORT_SIZE + PCI_PORT_XILINX_SIZE) {
        hwaddr off = addr - (PCI_CONFIG_PORT_SIZE + PCI_PORT_SIZE);
        if (off < PCI_PORT_XILINX_SIZE) {
            uint32_t v32 = (uint32_t)val;
            switch (size) {
            case 1:
                s->bar2_xilinx[off] = (uint8_t)v32;
                break;
            case 2:
                if (off + 1 < PCI_PORT_XILINX_SIZE) {
                    s->bar2_xilinx[off] = (uint8_t)(v32 & 0xff);
                    s->bar2_xilinx[off + 1] = (uint8_t)((v32 >> 8) & 0xff);
                }
                break;
            case 4:
                if (off + 3 < PCI_PORT_XILINX_SIZE) {
                    s->bar2_xilinx[off] = (uint8_t)(v32 & 0xff);
                    s->bar2_xilinx[off + 1] = (uint8_t)((v32 >> 8) & 0xff);
                    s->bar2_xilinx[off + 2] = (uint8_t)((v32 >> 16) & 0xff);
                    s->bar2_xilinx[off + 3] = (uint8_t)((v32 >> 24) & 0xff);
                }
                break;
            default:
                break;
            }
            return;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Logic for Port I/O (Legacy support)
     * The kvaser_pci driver uses ioread/iowrite on mapped BARs (MMIO),
     * not legacy inb/outb PIO, so we simply mirror BAR1 CAN space here
     * for safety if PIO is ever used.
     */
    if (addr < PCI_PORT_SIZE) {
        if (size == 1) {
            val = s->bar1_can[addr];
        } else if (size == 2 && addr + 1 < PCI_PORT_SIZE) {
            val = s->bar1_can[addr] |
                  ((uint16_t)s->bar1_can[addr + 1] << 8);
        } else if (size == 4 && addr + 3 < PCI_PORT_SIZE) {
            val = s->bar1_can[addr] |
                  ((uint32_t)s->bar1_can[addr + 1] << 8) |
                  ((uint32_t)s->bar1_can[addr + 2] << 16) |
                  ((uint32_t)s->bar1_can[addr + 3] << 24);
        }
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Logic for Port I/O (Legacy support)
     * Mirror writes into BAR1 CAN space.
     */
    if (addr < PCI_PORT_SIZE) {
        uint32_t v32 = (uint32_t)val;
        switch (size) {
        case 1:
            s->bar1_can[addr] = (uint8_t)v32;
            break;
        case 2:
            if (addr + 1 < PCI_PORT_SIZE) {
                s->bar1_can[addr] = (uint8_t)(v32 & 0xff);
                s->bar1_can[addr + 1] = (uint8_t)((v32 >> 8) & 0xff);
            }
            break;
        case 4:
            if (addr + 3 < PCI_PORT_SIZE) {
                s->bar1_can[addr] = (uint8_t)(v32 & 0xff);
                s->bar1_can[addr + 1] = (uint8_t)((v32 >> 8) & 0xff);
                s->bar1_can[addr + 2] = (uint8_t)((v32 >> 16) & 0xff);
                s->bar1_can[addr + 3] = (uint8_t)((v32 >> 24) & 0xff);
            }
            break;
        default:
            break;
        }
    }
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

    /* Revert registers to power-on defaults */
    memset(s->bar0_conf, 0, sizeof(s->bar0_conf));
    memset(s->bar1_can, 0, sizeof(s->bar1_can));
    memset(s->bar2_xilinx, 0, sizeof(s->bar2_xilinx));
    s->intcsr_shadow = 0;
    s->ptcr_shadow = 0;
    s->irq_asserted = false;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  KVASER_PCI_VENDOR_ID1 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  KVASER_PCI_DEVICE_ID1 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, KVASER_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization
     * The driver expects:
     *  BAR0: S5920 config (INTCSR/PTCR), size >= 0x80
     *  BAR1: SJA1000 channels, size >= 0x80
     *  BAR2: Xilinx area, size >= 0x08
     */
    s->num_bars = 3;

    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = PCI_CONFIG_PORT_SIZE + PCI_PORT_SIZE + PCI_PORT_XILINX_SIZE; /* we map all in one MR */
    s->bar_info[0].name  = "kvaser-pci-bar0";

    /* We do not expose additional BARs as the driver only ioremap BAR0/1/2
     * and we include all 3 logical regions into BAR0's MemoryRegion. The
     * pci_iomap indices in the guest are still 0,1,2, but QEMU's mapping is
     * implementation detail.
     *
     * However, to match the driver's pci_iomap(pdev, 1, PCI_PORT_SIZE) and
     * pci_iomap(pdev, 2, PCI_PORT_XILINX_SIZE), we should provide BAR1 and
     * BAR2 as separate MMIO regions as well.
     */
    s->bar_info[1].index = 1;
    s->bar_info[1].type  = BAR_TYPE_MMIO;
    s->bar_info[1].size  = PCI_PORT_SIZE;
    s->bar_info[1].name  = "kvaser-pci-bar1";

    s->bar_info[2].index = 2;
    s->bar_info[2].type  = BAR_TYPE_MMIO;
    s->bar_info[2].size  = PCI_PORT_XILINX_SIZE;
    s->bar_info[2].name  = "kvaser-pci-bar2";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize internal register images */
    memset(s->bar0_conf, 0, sizeof(s->bar0_conf));
    memset(s->bar1_can, 0, sizeof(s->bar1_can));
    memset(s->bar2_xilinx, 0, sizeof(s->bar2_xilinx));
    s->intcsr_shadow = 0;
    s->ptcr_shadow = 0;
    s->irq_asserted = false;

    /* No MSI/MSI-X or DMA initialization required by the kvaser_pci driver */
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

    /* Free buffers, stop timers, etc. */
    s->irq_asserted = false;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "kvaser_pci_pci",
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

