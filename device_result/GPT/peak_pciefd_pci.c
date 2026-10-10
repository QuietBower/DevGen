/*
 * QEMU PCI device model for peak_pciefd
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
#include "hw/hw.h"

#define TYPE_PCIBASE_DEVICE "peak_pciefd_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PEAK_PCI_VENDOR_ID        0x001c
#define PEAK_PCIEFD_ID            0x0013

#define PCIEFD_BAR0_SIZE          (64 * 1024)

#define PCIEFD_REG_SYS_CTL_SET    0x0000
#define PCIEFD_REG_SYS_CTL_CLR    0x0004
#define PCIEFD_REG_SYS_VER1       0x0040
#define PCIEFD_REG_SYS_VER2       0x0044

#define PCIEFD_SYS_CTL_TS_RST     0x00000001
#define PCIEFD_SYS_CTL_CLK_EN     0x00000002

#define PCIEFD_CANX_OFF(c)        (((c) + 1) * 0x1000)

#define PCIEFD_REG_CAN_MISC              0x0000
#define PCIEFD_REG_CAN_CLK_SEL           0x0008
#define PCIEFD_REG_CAN_CMD_PORT_L        0x0010
#define PCIEFD_REG_CAN_CMD_PORT_H        0x0014
#define PCIEFD_REG_CAN_TX_REQ_ACC        0x0020
#define PCIEFD_REG_CAN_TX_CTL_SET        0x0030
#define PCIEFD_REG_CAN_TX_CTL_CLR        0x0038
#define PCIEFD_REG_CAN_TX_DMA_ADDR_L     0x0040
#define PCIEFD_REG_CAN_TX_DMA_ADDR_H     0x0044
#define PCIEFD_REG_CAN_RX_CTL_SET        0x0050
#define PCIEFD_REG_CAN_RX_CTL_CLR        0x0058
#define PCIEFD_REG_CAN_RX_CTL_WRT        0x0060
#define PCIEFD_REG_CAN_RX_CTL_ACK        0x0068
#define PCIEFD_REG_CAN_RX_DMA_ADDR_L     0x0070
#define PCIEFD_REG_CAN_RX_DMA_ADDR_H     0x0074

#define CANFD_MISC_TS_RST         0x00000001

#define CANFD_CLK_SEL_DIV_MASK    0x00000007
#define CANFD_CLK_SEL_DIV_60MHZ   0x00000000
#define CANFD_CLK_SEL_DIV_40MHZ   0x00000001
#define CANFD_CLK_SEL_DIV_30MHZ   0x00000002
#define CANFD_CLK_SEL_DIV_24MHZ   0x00000003
#define CANFD_CLK_SEL_DIV_20MHZ   0x00000004

#define CANFD_CLK_SEL_SRC_MASK        0x00000008
#define CANFD_CLK_SEL_SRC_240MHZ      0x00000008
#define CANFD_CLK_SEL_SRC_80MHZ       (~CANFD_CLK_SEL_SRC_240MHZ & \
                                       CANFD_CLK_SEL_SRC_MASK)

#define CANFD_CLK_SEL_20MHZ       (CANFD_CLK_SEL_SRC_240MHZ | \
                                   CANFD_CLK_SEL_DIV_20MHZ)
#define CANFD_CLK_SEL_24MHZ       (CANFD_CLK_SEL_SRC_240MHZ | \
                                   CANFD_CLK_SEL_DIV_24MHZ)
#define CANFD_CLK_SEL_30MHZ       (CANFD_CLK_SEL_SRC_240MHZ | \
                                   CANFD_CLK_SEL_DIV_30MHZ)
#define CANFD_CLK_SEL_40MHZ       (CANFD_CLK_SEL_SRC_240MHZ | \
                                   CANFD_CLK_SEL_DIV_40MHZ)
#define CANFD_CLK_SEL_60MHZ       (CANFD_CLK_SEL_SRC_240MHZ | \
                                   CANFD_CLK_SEL_DIV_60MHZ)
#define CANFD_CLK_SEL_80MHZ       (CANFD_CLK_SEL_SRC_80MHZ)

#define CANFD_CTL_UNC_BIT         0x00010000
#define CANFD_CTL_RST_BIT         0x00020000
#define CANFD_CTL_IEN_BIT         0x00040000
#define CANFD_CTL_IRQ_CL_DEF      16
#define CANFD_CTL_IRQ_TL_DEF      10

#define PCIEFD_RX_DMA_SIZE        (4 * 1024)
#define PCIEFD_TX_DMA_SIZE        (4 * 1024)
#define PCIEFD_TX_PAGE_SIZE       (2 * 1024)
#define PCIEFD_TX_PAGE_COUNT      (PCIEFD_TX_DMA_SIZE / PCIEFD_TX_PAGE_SIZE)

#define CANFD_MSG_LNK_TX          0x1001

#define PCANFD_ECHO_SKB_DEF       -1

#define PCIEFD_ECHO_SKB_MAX       PCANFD_ECHO_SKB_DEF

#define PCIEFD_FW_VERSION(x, y, z)    (((uint32_t)(x) << 24) | \
                                       ((uint32_t)(y) << 16) | \
                                       ((uint32_t)(z) << 8))

#define PCIBASE_VENDOR_ID PEAK_PCI_VENDOR_ID
#define PCIBASE_DEVICE_ID PEAK_PCIEFD_ID
#define PCIBASE_CLASS_ID  PCI_CLASS_OTHERS

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

    uint32_t sys_ctl;
    uint32_t sys_ver1;
    uint32_t sys_ver2;

    /* Simple shadow arrays for CAN block registers within BAR0 */
    uint32_t sys_regs[0x80 / 4];
    uint32_t can_regs[4][0x80 / 4];
};

static inline bool pcibase_msi_enabled(PCIBaseState *s)
{
    return msi_enabled(PCI_DEVICE(s));
}

static void pcibase_raise_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (pcibase_msi_enabled(s)) {
        msi_notify(pdev, 0);
    } else {
        pci_set_irq(pdev, 1);
    }
}

static void pcibase_lower_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (!pcibase_msi_enabled(s)) {
        pci_set_irq(pdev, 0);
    }
}

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* The real hardware uses MSI/INTA based on DMA content; for now,
     * interrupt is raised when RX DMA is acknowledged once and then
     * kept simple. We only ensure IRQ line can be toggled.
     */
    (void)s;
}

G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* The driver only programs DMA addresses and uses coherent mappings
     * from host side. It never makes the device actively perform DMA
     * transfers on its own in the provided code, so no bus mastering
     * emulation is performed here.
     */
    (void)s;
    (void)is_write;
}

static uint32_t pcibase_sys_read(PCIBaseState *s, hwaddr offset)
{
    switch (offset) {
    case PCIEFD_REG_SYS_CTL_SET:
        return s->sys_ctl;
    case PCIEFD_REG_SYS_CTL_CLR:
        return s->sys_ctl;
    case PCIEFD_REG_SYS_VER1:
        /* simple non-zero value so read cycles succeed */
        return s->sys_ver1;
    case PCIEFD_REG_SYS_VER2:
        return s->sys_ver2;
    default: {
        unsigned idx = (offset & 0x7f) >> 2;
        if (idx < (sizeof(s->sys_regs) / sizeof(s->sys_regs[0]))) {
            return s->sys_regs[idx];
        }
        return 0;
    }
    }
}

static void pcibase_sys_write(PCIBaseState *s, hwaddr offset, uint32_t val)
{
    switch (offset) {
    case PCIEFD_REG_SYS_CTL_SET:
        s->sys_ctl |= val;
        break;
    case PCIEFD_REG_SYS_CTL_CLR:
        s->sys_ctl &= ~val;
        break;
    case PCIEFD_REG_SYS_VER1:
        s->sys_ver1 = val;
        break;
    case PCIEFD_REG_SYS_VER2:
        s->sys_ver2 = val;
        break;
    default: {
        unsigned idx = (offset & 0x7f) >> 2;
        if (idx < (sizeof(s->sys_regs) / sizeof(s->sys_regs[0]))) {
            s->sys_regs[idx] = val;
        }
        break;
    }
    }
}

static int pcibase_can_index_from_offset(hwaddr offset)
{
    int i;
    for (i = 0; i < 4; i++) {
        hwaddr base = PCIEFD_CANX_OFF(i);
        if (offset >= base && offset < base + 0x1000) {
            return i;
        }
    }
    return -1;
}

static uint32_t *pcibase_can_reg_array(PCIBaseState *s, int can_idx)
{
    if (can_idx < 0 || can_idx >= 4) {
        return NULL;
    }
    return s->can_regs[can_idx];
}

static uint32_t pcibase_can_read(PCIBaseState *s, hwaddr offset)
{
    int can_idx = pcibase_can_index_from_offset(offset);
    if (can_idx < 0) {
        return 0;
    }

    uint32_t *regs = pcibase_can_reg_array(s, can_idx);
    hwaddr base = PCIEFD_CANX_OFF(can_idx);
    hwaddr rel = offset - base;

    switch (rel) {
    case PCIEFD_REG_CAN_MISC:
    case PCIEFD_REG_CAN_CLK_SEL:
    case PCIEFD_REG_CAN_CMD_PORT_L:
    case PCIEFD_REG_CAN_CMD_PORT_H:
    case PCIEFD_REG_CAN_TX_REQ_ACC:
    case PCIEFD_REG_CAN_TX_CTL_SET:
    case PCIEFD_REG_CAN_TX_CTL_CLR:
    case PCIEFD_REG_CAN_TX_DMA_ADDR_L:
    case PCIEFD_REG_CAN_TX_DMA_ADDR_H:
    case PCIEFD_REG_CAN_RX_CTL_SET:
    case PCIEFD_REG_CAN_RX_CTL_CLR:
    case PCIEFD_REG_CAN_RX_CTL_WRT:
    case PCIEFD_REG_CAN_RX_CTL_ACK:
    case PCIEFD_REG_CAN_RX_DMA_ADDR_L:
    case PCIEFD_REG_CAN_RX_DMA_ADDR_H: {
        unsigned idx = (rel & 0x7f) >> 2;
        return regs[idx];
    }
    default: {
        unsigned idx = (rel & 0x7f) >> 2;
        if (idx < (0x80 / 4)) {
            return regs[idx];
        }
        return 0;
    }
    }
}

static void pcibase_can_write(PCIBaseState *s, hwaddr offset, uint32_t val)
{
    int can_idx = pcibase_can_index_from_offset(offset);
    if (can_idx < 0) {
        return;
    }

    uint32_t *regs = pcibase_can_reg_array(s, can_idx);
    hwaddr base = PCIEFD_CANX_OFF(can_idx);
    hwaddr rel = offset - base;
    unsigned idx = (rel & 0x7f) >> 2;

    switch (rel) {
    case PCIEFD_REG_CAN_MISC:
        regs[idx] = val;
        break;
    case PCIEFD_REG_CAN_CLK_SEL:
        regs[idx] = val;
        break;
    case PCIEFD_REG_CAN_CMD_PORT_L:
    case PCIEFD_REG_CAN_CMD_PORT_H:
        regs[idx] = val;
        break;
    case PCIEFD_REG_CAN_TX_CTL_SET:
        regs[idx] |= val;
        break;
    case PCIEFD_REG_CAN_TX_CTL_CLR:
        regs[idx] &= ~val;
        break;
    case PCIEFD_REG_CAN_TX_DMA_ADDR_L:
    case PCIEFD_REG_CAN_TX_DMA_ADDR_H:
        regs[idx] = val;
        break;
    case PCIEFD_REG_CAN_RX_CTL_SET:
        regs[idx] |= val;
        break;
    case PCIEFD_REG_CAN_RX_CTL_CLR:
        regs[idx] &= ~val;
        break;
    case PCIEFD_REG_CAN_RX_CTL_WRT:
    case PCIEFD_REG_CAN_RX_CTL_ACK:
        regs[idx] = val;
        /* acknowledgements could be used to trigger IRQs; here we
         * simply pulse the interrupt line once to satisfy driver.
         */
        pcibase_raise_irq(s);
        pcibase_lower_irq(s);
        break;
    case PCIEFD_REG_CAN_RX_DMA_ADDR_L:
    case PCIEFD_REG_CAN_RX_DMA_ADDR_H:
        regs[idx] = val;
        break;
    case PCIEFD_REG_CAN_TX_REQ_ACC:
        regs[idx] = val;
        break;
    default:
        if (idx < (0x80 / 4)) {
            regs[idx] = val;
        }
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    if (size != 4 && size != 1 && size != 2 && size != 8) {
        return 0;
    }

    if (addr < 0x1000) {
        val = pcibase_sys_read(s, addr);
    } else {
        val = pcibase_can_read(s, addr);
    }

    if (size == 1) {
        unsigned shift = (addr & 3) * 8;
        return (val >> shift) & 0xff;
    } else if (size == 2) {
        unsigned shift = (addr & 2) * 8;
        return (val >> shift) & 0xffff;
    } else if (size == 4) {
        return val;
    } else if (size == 8) {
        uint32_t v2;
        if ((addr + 4) < 0x1000) {
            v2 = pcibase_sys_read(s, addr + 4);
        } else {
            v2 = pcibase_can_read(s, addr + 4);
        }
        return ((uint64_t)v2 << 32) | val;
    }

    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v32;

    if (size == 1) {
        v32 = pcibase_mmio_read(opaque, addr & ~0x3ULL, 4);
        unsigned shift = (addr & 3) * 8;
        uint32_t mask = 0xffu << shift;
        v32 = (v32 & ~mask) | (((uint32_t)val & 0xffu) << shift);
    } else if (size == 2) {
        v32 = pcibase_mmio_read(opaque, addr & ~0x3ULL, 4);
        unsigned shift = (addr & 2) * 8;
        uint32_t mask = 0xffffu << shift;
        v32 = (v32 & ~mask) | (((uint32_t)val & 0xffffu) << shift);
    } else if (size == 4) {
        v32 = (uint32_t)val;
    } else if (size == 8) {
        /* write lower 32 bits to addr */
        v32 = (uint32_t)val;
    } else {
        return;
    }

    if (addr < 0x1000) {
        pcibase_sys_write(s, addr & ~0x3ULL, v32);
    } else {
        pcibase_can_write(s, addr, v32);
    }

    if (size == 8) {
        uint32_t v2 = (uint32_t)(val >> 32);
        hwaddr addr2 = addr + 4;
        if (addr2 < 0x1000) {
            pcibase_sys_write(s, addr2 & ~0x3ULL, v2);
        } else {
            pcibase_can_write(s, addr2, v2);
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
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

    s->sys_ctl = 0;
    /* Provide a valid firmware version >= 3.3.0 so that the driver
     * does not try to restrict DMA mask. Use 3.3.0 encoded via macro
     * PCIEFD_FW_VERSION(x,y,z) where low byte is unused.
     */
    s->sys_ver1 = 0;
    s->sys_ver2 = PCIEFD_FW_VERSION(3, 3, 0);

    memset(s->sys_regs, 0, sizeof(s->sys_regs));
    memset(s->can_regs, 0, sizeof(s->can_regs));

    /* Set default CAN clock selection to 80MHz for all channels */
    for (int c = 0; c < 4; c++) {
        uint32_t *regs = s->can_regs[c];
        unsigned idx_clk = (PCIEFD_REG_CAN_CLK_SEL & 0x7f) >> 2;
        regs[idx_clk] = CANFD_CLK_SEL_80MHZ;
    }
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = PCIEFD_BAR0_SIZE;
    s->bar_info[0].name = "peak_pciefd-bar0";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Enable MSI with a single vector, as driver prefers MSI when available */
    if (!msi_init(pdev, 0, 1, true, false, errp)) {
        s->has_msi = true;
    } else {
        s->has_msi = false;
    }

    s->has_msix = false;

    pcibase_reset(DEVICE(pdev));
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
    .name = "peak_pciefd_pci",
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
