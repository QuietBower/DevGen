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
/* None required beyond standard includes */

#define TYPE_PCIBASE_DEVICE "pt3_driver_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PT3_VENDOR_ID 0x1172
#define PT3_DEVICE_ID 0x4c15
#define PT3_SUBVENDOR_ID 0xee8d
#define PT3_SUBDEVICE_ID 0x0368

/* Register Offsets */
#define REG_VERSION      0x00
#define REG_SYSTEM_W     0x08
#define REG_I2C_W        0x10
#define REG_I2C_R        0x14
#define REG_DMA_BASE     0x40

/* DMA Channel Register Offsets (relative to channel base) */
#define OFST_DMA_DESC_L   0x00
#define OFST_DMA_DESC_H   0x04
#define OFST_DMA_CTL      0x08
#define OFST_STATUS       0x10

/* Hardware descriptor structures */
struct xfer_desc {
    uint32_t addr_l; /* bus address of target data buffer */
    uint32_t addr_h;
    uint32_t size;
    uint32_t next_l; /* bus address of the next xfer_desc */
    uint32_t next_h;
};

/* DMA descriptor buffer (memory block) */
struct xfer_desc_buffer {
    dma_addr_t b_addr;
    struct xfer_desc *descs; /* PAGE_SIZE (xfer_desc[DESCS_IN_PAGE]) */
};

/* DMA data buffer */
struct dma_data_buffer {
    dma_addr_t b_addr;
    uint8_t *data; /* size: u8[PAGE_SIZE] */
};

/* BAR sizes - to be provided by supplementary sources */
#define PT3_BAR0_SIZE 0x1000
#define PT3_BAR2_SIZE 0x1000

/* Class ID: provided by supplementary source */
#define PCI_CLASS_MULTIMEDIA_OTHER 0x0480

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

/* DMA channel state */
typedef struct PT3DMAChannel {
    dma_addr_t desc_addr;    /* Current descriptor table address */
    uint32_t ctl;            /* DMA control register */
    uint32_t status;         /* DMA status register */
    bool active;
} PT3DMAChannel;

#define PT3_NUM_FE 4

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t irq_status; /* Interrupt status for legacy IRQ */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t version;      /* REG_VERSION */
    uint32_t system_w;     /* REG_SYSTEM_W */
    uint32_t i2c_w_data;   /* REG_I2C_W (updated to 32-bit) */
    uint32_t i2c_r_data;   /* REG_I2C_R (updated to 32-bit) */
    uint64_t dma_base;     /* REG_DMA_BASE (64-bit) */
    uint8_t i2c_internal_mem[4096]; /* placeholder, replaced by i2c_mem */
    uint8_t i2c_mem[4096];  /* BAR2 I2C memory region */

    /* DMA Context */
    PT3DMAChannel dma_ch[PT3_NUM_FE]; /* one DMA channel per FE */

    /* Operational status flags */
    uint32_t status;

    /* Power management state (D0-D3) */
    uint8_t pm_state;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Legacy IRQ signaling: raise if irq_status is non-zero */
    pci_set_irq(pdev, (s->irq_status != 0));
}

/* MMIO/PIO Handlers */

/* BAR0 MMIO handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case REG_VERSION:
        val = s->version;
        break;
    case REG_SYSTEM_W:
        val = s->system_w;
        break;
    case REG_I2C_W:
        val = s->i2c_w_data;
        break;
    case REG_I2C_R:
        val = s->i2c_r_data;
        break;
    case REG_DMA_BASE:
        val = (uint32_t)(s->dma_base);
        break;
    case REG_DMA_BASE + 4:
        val = (uint32_t)(s->dma_base >> 32);
        break;
    default:
        /* Handle DMA channel registers */
        if (addr >= 0x40 && addr < 0x40 + PT3_NUM_FE * 0x20) {
            int ch = (addr - 0x40) / 0x20;
            int offset = (addr - 0x40) % 0x20;
            switch (offset) {
            case OFST_DMA_DESC_L:
                val = (uint32_t)(s->dma_ch[ch].desc_addr);
                break;
            case OFST_DMA_DESC_H:
                val = (uint32_t)(s->dma_ch[ch].desc_addr >> 32);
                break;
            case OFST_DMA_CTL:
                val = s->dma_ch[ch].ctl;
                break;
            case OFST_STATUS:
                val = s->dma_ch[ch].status;
                break;
            default:
                val = 0;
                break;
            }
        } else {
            qemu_log_mask(LOG_UNIMP, "pt3: unimplemented MMIO read at 0x%" HWADDR_PRIx "\n", addr);
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case REG_VERSION:
        /* version is read-only, ignore writes */
        break;
    case REG_SYSTEM_W:
        s->system_w = (uint32_t)val;
        break;
    case REG_I2C_W:
        s->i2c_w_data = (uint32_t)val;
        /* Simulate I2C transaction: any write starts and completes instantly */
        /* Set result to 0 (idle, no error), satisfying the driver's wait_i2c_result */
        s->i2c_r_data = 0;
        break;
    case REG_DMA_BASE:
        s->dma_base = (s->dma_base & ~0xFFFFFFFFULL) | (val & 0xFFFFFFFFULL);
        break;
    case REG_DMA_BASE + 4:
        s->dma_base = (s->dma_base & 0xFFFFFFFFULL) | ((uint64_t)val << 32);
        break;
    default:
        /* Handle DMA channel registers */
        if (addr >= 0x40 && addr < 0x40 + PT3_NUM_FE * 0x20) {
            int ch = (addr - 0x40) / 0x20;
            int offset = (addr - 0x40) % 0x20;
            switch (offset) {
            case OFST_DMA_DESC_L:
                s->dma_ch[ch].desc_addr = (s->dma_ch[ch].desc_addr & ~0xFFFFFFFFULL) | (val & 0xFFFFFFFFULL);
                break;
            case OFST_DMA_DESC_H:
                s->dma_ch[ch].desc_addr = (s->dma_ch[ch].desc_addr & 0xFFFFFFFFULL) | ((uint64_t)val << 32);
                break;
            case OFST_DMA_CTL:
                s->dma_ch[ch].ctl = (uint32_t)val;
                if (val & 0x01) {
                    /* Start DMA if not already active */
                    if (!(s->dma_ch[ch].status & 0x01)) {
                        s->dma_ch[ch].status |= 0x01;
                        s->dma_ch[ch].active = true;
                    }
                } else if (val & 0x02) {
                    /* Stop DMA, clear active bit */
                    s->dma_ch[ch].status &= ~0x01;
                    s->dma_ch[ch].active = false;
                }
                break;
            case OFST_STATUS:
                /* Writes to status register are ignored (read-only) */
                break;
            default:
                qemu_log_mask(LOG_UNIMP, "pt3: unimplemented DMA write at channel %d offset 0x%x: 0x%" PRIx64 "\n",
                              ch, offset, val);
                break;
            }
        } else {
            qemu_log_mask(LOG_UNIMP, "pt3: unimplemented MMIO write at 0x%" HWADDR_PRIx " = 0x%" PRIx64 "\n", addr, val);
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

/* BAR2 MMIO handler (I2C registers) */
static uint64_t pcibase_mmio_bar2_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size > 0x1000) {
        return ~0ULL;
    }
    memcpy(&val, &s->i2c_mem[addr], size);
    return val;
}

static void pcibase_mmio_bar2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > 0x1000) {
        return;
    }
    memcpy(&s->i2c_mem[addr], &val, size);
}

static const MemoryRegionOps pcibase_mmio_bar2_ops = {
    .read = pcibase_mmio_bar2_read,
    .write = pcibase_mmio_bar2_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* PIO not used by this driver */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* PCIBaseState *s = opaque; */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PCIBaseState *s = opaque; */
}

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

    /* Reset device-specific state */
    s->version = 0x03010000; /* Required for driver probe (PT3, fw 0x01, I/F 0x03) */
    s->system_w = 0;
    s->i2c_w_data = 0;
    s->i2c_r_data = 0;
    s->dma_base = 0;
    s->irq_status = 0;
    s->status = 0;
    s->pm_state = 0;
    memset(s->i2c_internal_mem, 0, sizeof(s->i2c_internal_mem));
    memset(s->i2c_mem, 0, sizeof(s->i2c_mem));
    for (int i = 0; i < PT3_NUM_FE; i++) {
        s->dma_ch[i].desc_addr = 0;
        s->dma_ch[i].ctl = 0;
        s->dma_ch[i].status = 0;
        s->dma_ch[i].active = false;
    }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PT3_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PT3_DEVICE_ID );
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, PT3_SUBVENDOR_ID);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, PT3_SUBDEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_OTHER );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Add PCI Express capability to support 64-bit DMA mask */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0);

    /* Set 64-bit Address Space Support in Device Capabilities */
    {
        uint8_t cap = pci_find_capability(pdev, PCI_CAP_ID_EXP);
        if (cap) {
            uint32_t dev_cap = pci_get_long(pci_conf + cap + PCI_EXP_DEVCAP);
            dev_cap |= (1 << 4); /* 64-bit Address Space Support */
            pci_set_long(pci_conf + cap + PCI_EXP_DEVCAP, dev_cap);
        }
    }

    /* BAR Initialization: adjust to match driver's pci_iomap calls */
    s->num_bars = 2;
    /* BAR0: control registers */
    s->bar_info[0] = (BARInfo) { .index = 0, .type = BAR_TYPE_MMIO, .size = PT3_BAR0_SIZE, .name = "pt3-bar0" };
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);

    /* BAR1 is not used by the driver; it maps BAR2 instead, so we configure BAR2 */
    hwaddr aligned_size = pow2ceil(PT3_BAR2_SIZE);
    memory_region_init_io(&s->bar_regions[2], OBJECT(s), &pcibase_mmio_bar2_ops, s, "pt3-bar2", aligned_size);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[2]);

    /* MSI/MSI-X not enabled by driver; legacy IRQ used */

    /* Final state initialization */
    pcibase_reset(DEVICE(pdev));
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

    /* No additional cleanup required */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "pt3_driver_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(version, PCIBaseState),
        VMSTATE_UINT32(system_w, PCIBaseState),
        VMSTATE_UINT32(i2c_w_data, PCIBaseState),
        VMSTATE_UINT32(i2c_r_data, PCIBaseState),
        VMSTATE_UINT64(dma_base, PCIBaseState),
        VMSTATE_UINT32(irq_status, PCIBaseState),
        VMSTATE_UINT32(status, PCIBaseState),
        VMSTATE_UINT8(pm_state, PCIBaseState),
        VMSTATE_STRUCT_ARRAY(dma_ch, PCIBaseState, PT3_NUM_FE, 0, vmstate_info_uint64, PT3DMAChannel),
        VMSTATE_UINT8_ARRAY(i2c_mem, PCIBaseState, 4096),
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
