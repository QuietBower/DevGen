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

#define TYPE_PCIBASE_DEVICE "plx_dma_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#ifndef PCI_VENDOR_ID_PLX
#define PCI_VENDOR_ID_PLX 0x10b5
#endif

#define PLX_REG_DESC_RING_ADDR			0x214
#define PLX_REG_DESC_RING_ADDR_HI		0x218
#define PLX_REG_DESC_RING_NEXT_ADDR		0x21C
#define PLX_REG_DESC_RING_COUNT			0x220
#define PLX_REG_DESC_RING_LAST_ADDR		0x224
#define PLX_REG_DESC_RING_LAST_SIZE		0x228
#define PLX_REG_PREF_LIMIT			0x234
#define PLX_REG_CTRL				0x238
#define PLX_REG_CTRL2				0x23A
#define PLX_REG_INTR_CTRL			0x23C
#define PLX_REG_INTR_STATUS			0x23E

#define PLX_REG_PREF_LIMIT_PREF_FOUR		8
#define PLX_REG_CTRL_GRACEFUL_PAUSE		BIT(0)
#define PLX_REG_CTRL_ABORT			BIT(1)
#define PLX_REG_CTRL_WRITE_BACK_EN		BIT(2)
#define PLX_REG_CTRL_START			BIT(3)
#define PLX_REG_CTRL_RING_STOP_MODE		BIT(4)
#define PLX_REG_CTRL_DESC_MODE_BLOCK		(0 << 5)
#define PLX_REG_CTRL_DESC_MODE_ON_CHIP		(1 << 5)
#define PLX_REG_CTRL_DESC_MODE_OFF_CHIP		(2 << 5)
#define PLX_REG_CTRL_DESC_INVALID		BIT(8)
#define PLX_REG_CTRL_GRACEFUL_PAUSE_DONE	BIT(9)
#define PLX_REG_CTRL_ABORT_DONE			BIT(10)
#define PLX_REG_CTRL_IMM_PAUSE_DONE		BIT(12)
#define PLX_REG_CTRL_IN_PROGRESS		BIT(30)
#define PLX_REG_CTRL_RESET_VAL	(PLX_REG_CTRL_DESC_INVALID | \
				 PLX_REG_CTRL_GRACEFUL_PAUSE_DONE | \
				 PLX_REG_CTRL_ABORT_DONE | \
				 PLX_REG_CTRL_IMM_PAUSE_DONE)
#define PLX_REG_CTRL_START_VAL	(PLX_REG_CTRL_WRITE_BACK_EN | \
				 PLX_REG_CTRL_DESC_MODE_OFF_CHIP | \
				 PLX_REG_CTRL_START | \
				 PLX_REG_CTRL_RESET_VAL)
#define PLX_REG_CTRL2_MAX_TXFR_SIZE_64B		0
#define PLX_REG_CTRL2_MAX_TXFR_SIZE_128B	1
#define PLX_REG_CTRL2_MAX_TXFR_SIZE_256B	2
#define PLX_REG_CTRL2_MAX_TXFR_SIZE_512B	3
#define PLX_REG_CTRL2_MAX_TXFR_SIZE_1KB		4
#define PLX_REG_CTRL2_MAX_TXFR_SIZE_2KB		5
#define PLX_REG_CTRL2_MAX_TXFR_SIZE_4B		7
#define PLX_REG_INTR_CRTL_ERROR_EN		BIT(0)
#define PLX_REG_INTR_CRTL_INV_DESC_EN		BIT(1)
#define PLX_REG_INTR_CRTL_ABORT_DONE_EN		BIT(3)
#define PLX_REG_INTR_CRTL_PAUSE_DONE_EN		BIT(4)
#define PLX_REG_INTR_CRTL_IMM_PAUSE_DONE_EN	BIT(5)
#define PLX_REG_INTR_STATUS_ERROR		BIT(0)
#define PLX_REG_INTR_STATUS_INV_DESC		BIT(1)
#define PLX_REG_INTR_STATUS_DESC_DONE		BIT(2)
#define PLX_REG_INTR_CRTL_ABORT_DONE		BIT(3)
#define PLX_DESC_SIZE_MASK		0x7ffffff
#define PLX_DESC_FLAG_VALID		BIT(31)
#define PLX_DESC_FLAG_INT_WHEN_DONE	BIT(30)
#define PLX_DESC_WB_SUCCESS		BIT(30)
#define PLX_DESC_WB_RD_FAIL		BIT(29)
#define PLX_DESC_WB_WR_FAIL		BIT(28)
#define PLX_DMA_RING_COUNT		2048

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

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint16_t intr_ctrl;
    uint16_t intr_status;

    uint32_t desc_ring_addr;
    uint32_t desc_ring_addr_hi;
    uint32_t desc_ring_next_addr;
    uint32_t desc_ring_count;
    uint32_t desc_ring_last_addr;
    uint32_t desc_ring_last_size;
    uint32_t pref_limit;
    uint16_t ctrl;
    uint16_t ctrl2;
    
    uint32_t ring_idx;
};

struct plx_dma_hw_std_desc {
    uint32_t flags_and_size;
    uint16_t dst_addr_hi;
    uint16_t src_addr_hi;
    uint32_t dst_addr_lo;
    uint32_t src_addr_lo;
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->intr_status != 0);
    
    if (msi_enabled(pdev)) {
        if (level) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, level);
    }
}

G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (!s->desc_ring_count) return;
    
    uint64_t ring_base = ((uint64_t)s->desc_ring_addr_hi << 32) | s->desc_ring_addr;
    bool irq_needed = false;
    
    while (1) {
        struct plx_dma_hw_std_desc desc;
        uint64_t desc_addr = ring_base + s->ring_idx * sizeof(desc);
        
        pci_dma_read(pdev, desc_addr, &desc, sizeof(desc));
        
        uint32_t flags = le32_to_cpu(desc.flags_and_size);
        if (!(flags & PLX_DESC_FLAG_VALID)) {
            break;
        }
        
        uint32_t size = flags & PLX_DESC_SIZE_MASK;
        uint64_t src = ((uint64_t)le16_to_cpu(desc.src_addr_hi) << 32) | le32_to_cpu(desc.src_addr_lo);
        uint64_t dst = ((uint64_t)le16_to_cpu(desc.dst_addr_hi) << 32) | le32_to_cpu(desc.dst_addr_lo);
        
        uint8_t buf[4096];
        uint32_t remain = size;
        uint64_t csrc = src, cdst = dst;
        while (remain > 0) {
            uint32_t chunk = remain > sizeof(buf) ? sizeof(buf) : remain;
            pci_dma_read(pdev, csrc, buf, chunk);
            pci_dma_write(pdev, cdst, buf, chunk);
            csrc += chunk;
            cdst += chunk;
            remain -= chunk;
        }
        
        flags &= ~PLX_DESC_FLAG_VALID;
        flags |= PLX_DESC_WB_SUCCESS;
        desc.flags_and_size = cpu_to_le32(flags);
        
        pci_dma_write(pdev, desc_addr, &desc, sizeof(desc));
        
        if (flags & PLX_DESC_FLAG_INT_WHEN_DONE) {
            s->intr_status |= PLX_REG_INTR_STATUS_DESC_DONE;
            irq_needed = true;
        }
        
        s->ring_idx = (s->ring_idx + 1) % s->desc_ring_count;
    }
    
    if (irq_needed) {
        pcibase_update_irq(s);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    switch (addr) {
    case PLX_REG_DESC_RING_ADDR: val = s->desc_ring_addr; break;
    case PLX_REG_DESC_RING_ADDR_HI: val = s->desc_ring_addr_hi; break;
    case PLX_REG_DESC_RING_NEXT_ADDR: val = s->desc_ring_next_addr; break;
    case PLX_REG_DESC_RING_COUNT: val = s->desc_ring_count; break;
    case PLX_REG_DESC_RING_LAST_ADDR: val = s->desc_ring_last_addr; break;
    case PLX_REG_DESC_RING_LAST_SIZE: val = s->desc_ring_last_size; break;
    case PLX_REG_PREF_LIMIT: val = s->pref_limit; break;
    case PLX_REG_CTRL: 
        val = s->ctrl;
        if (size == 4) val |= ((uint32_t)s->ctrl2 << 16);
        break;
    case PLX_REG_CTRL2: val = s->ctrl2; break;
    case PLX_REG_INTR_CTRL: 
        val = s->intr_ctrl;
        if (size == 4) val |= ((uint32_t)s->intr_status << 16);
        break;
    case PLX_REG_INTR_STATUS: val = s->intr_status; break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (addr) {
    case PLX_REG_DESC_RING_ADDR: s->desc_ring_addr = val; break;
    case PLX_REG_DESC_RING_ADDR_HI: s->desc_ring_addr_hi = val; break;
    case PLX_REG_DESC_RING_NEXT_ADDR: s->desc_ring_next_addr = val; break;
    case PLX_REG_DESC_RING_COUNT: 
        s->desc_ring_count = val; 
        if (val == 0) s->ring_idx = 0;
        break;
    case PLX_REG_DESC_RING_LAST_ADDR: s->desc_ring_last_addr = val; break;
    case PLX_REG_DESC_RING_LAST_SIZE: s->desc_ring_last_size = val; break;
    case PLX_REG_PREF_LIMIT: s->pref_limit = val; break;
    case PLX_REG_CTRL:
        s->ctrl = val & 0xFFFF;
        if (size == 4) {
            s->ctrl2 = (val >> 16) & 0xFFFF;
        }
        if (s->ctrl & PLX_REG_CTRL_GRACEFUL_PAUSE) {
            s->ctrl |= PLX_REG_CTRL_GRACEFUL_PAUSE_DONE;
        }
        if (s->ctrl & PLX_REG_CTRL_START) {
            pcibase_do_dma(s, false);
        }
        break;
    case PLX_REG_CTRL2:
        s->ctrl2 = val & 0xFFFF;
        break;
    case PLX_REG_INTR_CTRL:
        s->intr_ctrl = val & 0xFFFF;
        if (size == 4) {
            s->intr_status &= ~(val >> 16);
            pcibase_update_irq(s);
        }
        break;
    case PLX_REG_INTR_STATUS:
        s->intr_status &= ~val;
        pcibase_update_irq(s);
        break;
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
    
    s->ctrl = PLX_REG_CTRL_RESET_VAL;
    s->ctrl2 = 0;
    s->intr_ctrl = 0;
    s->intr_status = 0;
    s->desc_ring_addr = 0;
    s->desc_ring_addr_hi = 0;
    s->desc_ring_next_addr = 0;
    s->desc_ring_count = 0;
    s->desc_ring_last_addr = 0;
    s->desc_ring_last_size = 0;
    s->pref_limit = 0;
    s->ring_idx = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_PLX );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x87D0 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SYSTEM_OTHER );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_MMIO, 0x1000, "bar0"};
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
    
    msi_init(pdev, 0, 1, true, false, errp);
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

static const VMStateDescription vmstate_pcibase = {
    .name = "plx_dma_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT16(intr_ctrl, PCIBaseState),
        VMSTATE_UINT16(intr_status, PCIBaseState),
        VMSTATE_UINT32(desc_ring_addr, PCIBaseState),
        VMSTATE_UINT32(desc_ring_addr_hi, PCIBaseState),
        VMSTATE_UINT32(desc_ring_next_addr, PCIBaseState),
        VMSTATE_UINT32(desc_ring_count, PCIBaseState),
        VMSTATE_UINT32(desc_ring_last_addr, PCIBaseState),
        VMSTATE_UINT32(desc_ring_last_size, PCIBaseState),
        VMSTATE_UINT32(pref_limit, PCIBaseState),
        VMSTATE_UINT16(ctrl, PCIBaseState),
        VMSTATE_UINT16(ctrl2, PCIBaseState),
        VMSTATE_UINT32(ring_idx, PCIBaseState),
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
