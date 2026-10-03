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

#define TYPE_PCIBASE_DEVICE "rocker_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Offsets (updated from latest driver header snippets) */
#define PCI_VENDOR_ID_REDHAT          0x1b36
#define PCI_DEVICE_ID_REDHAT_ROCKER   0x0006
#define ROCKER_PCI_CLASS              0x020000
#define ROCKER_PCI_BAR0_SIZE          0x2000
#define ROCKER_CONTROL                0x0300
#define ROCKER_PORT_PHYS_COUNT        0x0304
#define ROCKER_TEST_REG               0x0010
#define ROCKER_TEST_REG64             0x0018
#define ROCKER_TEST_IRQ               0x0020
#define ROCKER_TEST_DMA_CTRL          0x0034
#define ROCKER_TEST_DMA_ADDR          0x0028
#define ROCKER_TEST_DMA_SIZE          0x0030
#define ROCKER_SWITCH_ID              0x0320
#define ROCKER_PORT_PHYS_ENABLE       0x0318
#define ROCKER_PORT_PHYS_LINK_STATUS  0x0310

/* Test DMA control bits (confirmed from driver source) */
#define ROCKER_TEST_DMA_CTRL_CLEAR    BIT(0)
#define ROCKER_TEST_DMA_CTRL_FILL     BIT(1)
#define ROCKER_TEST_DMA_CTRL_INVERT   BIT(2)

#define ROCKER_DMA_DESC_COMP_ERR_GEN  BIT(15)

/* Test fill pattern */
#define ROCKER_TEST_DMA_FILL_PATTERN  0x96

/* DMA ring register base and per-type layout */
#define ROCKER_DMA_RING_BASE          0x1000
#define ROCKER_DMA_RING_STRIDE        0x40
#define ROCKER_DMA_RING_HEAD_OFFSET   0x00
#define ROCKER_DMA_RING_CREDITS_OFFSET 0x04
#define ROCKER_DMA_RING_CTRL_OFFSET   0x08
#define ROCKER_DMA_RING_ADDR_OFFSET   0x10
#define ROCKER_DMA_RING_SIZE_OFFSET   0x18

#define DMA_DESC_HEAD(type)   (ROCKER_DMA_RING_BASE + (type) * ROCKER_DMA_RING_STRIDE + ROCKER_DMA_RING_HEAD_OFFSET)
#define DMA_DESC_CREDITS(type) (ROCKER_DMA_RING_BASE + (type) * ROCKER_DMA_RING_STRIDE + ROCKER_DMA_RING_CREDITS_OFFSET)
#define DMA_DESC_CTRL(type)   (ROCKER_DMA_RING_BASE + (type) * ROCKER_DMA_RING_STRIDE + ROCKER_DMA_RING_CTRL_OFFSET)
#define DMA_DESC_ADDR(type)   (ROCKER_DMA_RING_BASE + (type) * ROCKER_DMA_RING_STRIDE + ROCKER_DMA_RING_ADDR_OFFSET)
#define DMA_DESC_SIZE(type)   (ROCKER_DMA_RING_BASE + (type) * ROCKER_DMA_RING_STRIDE + ROCKER_DMA_RING_SIZE_OFFSET)

/* Ring types */
#define ROCKER_DMA_CMD        0
#define ROCKER_DMA_EVENT      1

/* Control register bits */
#define ROCKER_CONTROL_RESET          BIT(0)

/* MSI-X vectors (CMD=0, EVENT=1, TEST=2; per-port RX/TX follow) */
#define ROCKER_MSIX_VEC_CMD   0
#define ROCKER_MSIX_VEC_EVENT 1
#define ROCKER_MSIX_VEC_TEST  2
/* Correctly derive vector count based on port count to match driver expectation */
#define ROCKER_MSIX_VEC_COUNT(ports) (ROCKER_MSIX_VEC_TEST + 1 + 2 * (ports))

/* Misc driver constants */
#define ROCKER_DMA_SIZE_MAX           65536ul
#define ROCKER_DMA_SIZE_MIN           2ul
#define ROCKER_DMA_EVENT_DEFAULT_SIZE 32ul
#define ROCKER_DMA_CMD_DEFAULT_SIZE   32ul
#define ROCKER_DMA_RX_DESC_SIZE       256
#define ROCKER_DMA_TX_DESC_SIZE       256
#define ROCKER_DMA_TX_DEFAULT_SIZE    64ul
#define ROCKER_DMA_RX_DEFAULT_SIZE    64ul
#define ROCKER_DMA_DESC_CTRL_RESET    BIT(0)
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif
#define ROCKER_TEST_DMA_BUF_SIZE      (PAGE_SIZE * 4)

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int     index;
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

/* Rocker descriptor */
struct rocker_desc {
    uint64_t buf_addr;
    uint64_t cookie;
    uint16_t buf_size;
    uint16_t tlv_size;
    uint16_t resv[5];
    uint16_t comp_err;
} QEMU_PACKED;

/* Per-ring state */
struct dma_ring {
    dma_addr_t desc_bus;
    struct rocker_desc *desc;
    uint32_t size;
    uint32_t head;
    uint32_t tail;
    uint32_t type;
};

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    uint32_t regs[ROCKER_PCI_BAR0_SIZE / sizeof(uint32_t)];

    struct dma_ring cmd_ring, event_ring;
    unsigned int port_count;

    /* Test DMA buffer pointer for immediate operations */
    dma_addr_t test_dma_addr;
    uint32_t test_dma_size;
};

/* Forward declaration of reset to avoid implicit declaration warning */
static void pcibase_reset(DeviceState *dev);

/* --- MMIO read/write --- */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case ROCKER_CONTROL:
        if (size == 4) val = s->regs[addr / 4];
        break;
    case ROCKER_PORT_PHYS_COUNT:
        if (size == 4) val = s->port_count;
        break;
    case ROCKER_TEST_REG:
        if (size == 4) val = s->regs[addr / 4] * 2;
        break;
    case ROCKER_TEST_REG64:
        if (size == 8) {
            uint64_t tmp = *(uint64_t *)&s->regs[addr / 4];
            val = tmp * 2;
        }
        break;
    case ROCKER_SWITCH_ID:
        if (size == 8) val = 0x0000000100000001ULL;
        break;
    case ROCKER_PORT_PHYS_ENABLE:
        if (size == 8) {
            val = *(uint64_t *)&s->regs[(addr) / 4];
        }
        break;
    case ROCKER_PORT_PHYS_LINK_STATUS:
        if (size == 8) val = 0;
        break;
    default:
        if (addr >= ROCKER_DMA_RING_BASE && addr < ROCKER_DMA_RING_BASE + 2*ROCKER_DMA_RING_STRIDE) {
            int type = (addr - ROCKER_DMA_RING_BASE) / ROCKER_DMA_RING_STRIDE;
            uint32_t off = (addr - ROCKER_DMA_RING_BASE) % ROCKER_DMA_RING_STRIDE;
            struct dma_ring *ring = NULL;
            if (type == ROCKER_DMA_CMD) ring = &s->cmd_ring;
            else if (type == ROCKER_DMA_EVENT) ring = &s->event_ring;
            if (ring) {
                switch (off) {
                case ROCKER_DMA_RING_HEAD_OFFSET:
                    if (size == 4) val = ring->head;
                    break;
                case ROCKER_DMA_RING_CREDITS_OFFSET:
                    break;
                case ROCKER_DMA_RING_CTRL_OFFSET:
                    if (size == 4) val = 0;
                    break;
                case ROCKER_DMA_RING_ADDR_OFFSET:
                    if (size == 8) val = ring->desc_bus;
                    break;
                case ROCKER_DMA_RING_SIZE_OFFSET:
                    if (size == 4) val = ring->size;
                    break;
                }
            }
        } else if (addr < ROCKER_PCI_BAR0_SIZE) {
            if (size == 4) val = s->regs[addr / 4];
            else if (size == 8) val = *(uint64_t *)&s->regs[addr / 4];
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t *regs = s->regs;
    PCIDevice *pdev = PCI_DEVICE(s);

    switch (addr) {
    case ROCKER_CONTROL:
        if (size == 4) {
            regs[addr / 4] = val;
            if (val & ROCKER_CONTROL_RESET) {
                pcibase_reset(DEVICE(s));
            }
        }
        break;
    case ROCKER_TEST_REG:
        if (size == 4) regs[addr / 4] = val;
        break;
    case ROCKER_TEST_REG64:
        if (size == 8) {
            *(uint64_t *)&regs[addr / 4] = val;
        }
        break;
    case ROCKER_TEST_IRQ:
        if (size == 4) {
            uint32_t vector = val;
            if (msix_enabled(pdev)) {
                msix_notify(pdev, vector);
            }
        }
        break;
    case ROCKER_TEST_DMA_CTRL:
        if (size == 4) {
            if (val & ROCKER_TEST_DMA_CTRL_CLEAR) {
                if (s->test_dma_size > 0 && s->test_dma_size <= ROCKER_TEST_DMA_BUF_SIZE) {
                    uint8_t *buf = g_malloc0(s->test_dma_size);
                    pci_dma_write(pdev, s->test_dma_addr, buf, s->test_dma_size);
                    g_free(buf);
                }
            }
            if (val & ROCKER_TEST_DMA_CTRL_FILL) {
                if (s->test_dma_size > 0 && s->test_dma_size <= ROCKER_TEST_DMA_BUF_SIZE) {
                    uint8_t *buf = g_malloc(s->test_dma_size);
                    memset(buf, ROCKER_TEST_DMA_FILL_PATTERN, s->test_dma_size);
                    pci_dma_write(pdev, s->test_dma_addr, buf, s->test_dma_size);
                    g_free(buf);
                }
            }
            if (val & ROCKER_TEST_DMA_CTRL_INVERT) {
                if (s->test_dma_size > 0 && s->test_dma_size <= ROCKER_TEST_DMA_BUF_SIZE) {
                    uint8_t *buf = g_malloc(s->test_dma_size);
                    pci_dma_read(pdev, s->test_dma_addr, buf, s->test_dma_size);
                    for (uint32_t i = 0; i < s->test_dma_size; i++) {
                        buf[i] = ~buf[i];
                    }
                    pci_dma_write(pdev, s->test_dma_addr, buf, s->test_dma_size);
                    g_free(buf);
                }
            }
        }
        break;
    case ROCKER_TEST_DMA_ADDR:
        if (size == 8) {
            s->test_dma_addr = val;
        }
        break;
    case ROCKER_TEST_DMA_SIZE:
        if (size == 4) {
            s->test_dma_size = val;
        }
        break;
    case ROCKER_PORT_PHYS_ENABLE:
        if (size == 8) {
            *(uint64_t *)&regs[addr / 4] = val;
        }
        break;
    default:
        if (addr >= ROCKER_DMA_RING_BASE && addr < ROCKER_DMA_RING_BASE + 2*ROCKER_DMA_RING_STRIDE) {
            int type = (addr - ROCKER_DMA_RING_BASE) / ROCKER_DMA_RING_STRIDE;
            uint32_t off = (addr - ROCKER_DMA_RING_BASE) % ROCKER_DMA_RING_STRIDE;
            struct dma_ring *ring = NULL;
            if (type == ROCKER_DMA_CMD) ring = &s->cmd_ring;
            else if (type == ROCKER_DMA_EVENT) ring = &s->event_ring;
            if (ring) {
                switch (off) {
                case ROCKER_DMA_RING_HEAD_OFFSET:
                    if (size == 4) ring->head = val;
                    break;
                case ROCKER_DMA_RING_CREDITS_OFFSET:
                    if (size == 4) {
                        /* credits write: store but not used further */
                    }
                    break;
                case ROCKER_DMA_RING_CTRL_OFFSET:
                    if (size == 4) {
                        if (val & ROCKER_DMA_DESC_CTRL_RESET) {
                            ring->head = ring->tail = 0;
                        }
                    }
                    break;
                case ROCKER_DMA_RING_ADDR_OFFSET:
                    if (size == 8) ring->desc_bus = val;
                    break;
                case ROCKER_DMA_RING_SIZE_OFFSET:
                    if (size == 4) ring->size = val;
                    break;
                }
            }
        } else if (addr < ROCKER_PCI_BAR0_SIZE) {
            if (size == 4) regs[addr / 4] = val;
            else if (size == 8) *(uint64_t *)&regs[addr / 4] = val;
        }
        break;
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
    s->port_count = 1;   /* Set to 1 to ensure MSI-X vector count calculation is valid */
    s->test_dma_addr = 0;
    s->test_dma_size = 0;

    s->cmd_ring = (struct dma_ring){0};
    s->event_ring = (struct dma_ring){0};
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
        /* PIO not used */
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1b36);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0006);
    /* Set correct 24-bit class code (base class 0x02, subclass 0x00, prog-if 0x00) */
    pci_set_byte(pci_conf + 0x09, 0x02);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0000);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Only BAR 0 is explicitly registered; MSI-X BAR is handled by msix_init_exclusive_bar */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = ROCKER_PCI_BAR0_SIZE, .name = "bar0" };
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);

    /* Allocate MSI-X vectors matching driver expectation: CMD + EVENT + TEST + 2*ports */
    s->port_count = 1;  /* Ensure driver sees a valid port count */
    int msix_vec_count = ROCKER_MSIX_VEC_COUNT(s->port_count);
    msix_init_exclusive_bar(pdev, msix_vec_count, 1, errp);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "rocker_pci",
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
