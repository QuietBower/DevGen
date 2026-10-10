/*
 * VIA Rhine PCI NIC minimal emulation for Linux via-rhine driver
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

#include <linux/pci_regs.h>

#define TYPE_PCIBASE_DEVICE "via_rhine_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define RHINE_VENDOR_ID 0x1106
#define RHINE_DEVICE_ID 0x3043
#define RHINE_CLASS_ID  PCI_CLASS_NETWORK_ETHERNET

#define RHINE_INTR_RX_DONE        0x0001
#define RHINE_INTR_TX_DONE        0x0002
#define RHINE_INTR_RX_ERR         0x0004
#define RHINE_INTR_TX_ERROR       0x0008
#define RHINE_INTR_RX_EMPTY       0x0020
#define RHINE_INTR_PCI_ERR        0x0040
#define RHINE_INTR_STATS_MAX      0x0080
#define RHINE_INTR_RX_EARLY       0x0100
#define RHINE_INTR_TX_UNDERRUN    0x0210
#define RHINE_INTR_RX_OVERFLOW    0x0400
#define RHINE_INTR_RX_DROPPED     0x0800
#define RHINE_INTR_RX_NO_BUF      0x1000
#define RHINE_INTR_TX_ABORTED     0x2000
#define RHINE_INTR_LINK_CHANGE    0x4000
#define RHINE_INTR_RX_WAKEUP      0x8000
#define RHINE_INTR_TX_DESC_RACE   0x080000

#define RHINE_REG_STATION_ADDR     0x00
#define RHINE_REG_RX_CONFIG        0x06
#define RHINE_REG_TX_CONFIG        0x07
#define RHINE_REG_CHIP_CMD         0x08
#define RHINE_REG_CHIP_CMD1        0x09
#define RHINE_REG_TQ_WAKE          0x0A
#define RHINE_REG_INTR_STATUS      0x0C
#define RHINE_REG_INTR_ENABLE      0x0E
#define RHINE_REG_MCAST_FILTER0    0x10
#define RHINE_REG_MCAST_FILTER1    0x14
#define RHINE_REG_RX_RING_PTR      0x18
#define RHINE_REG_TX_RING_PTR      0x1C
#define RHINE_REG_GFIFO_TEST       0x54
#define RHINE_REG_MII_PHY_ADDR     0x6C
#define RHINE_REG_MII_STATUS       0x6D
#define RHINE_REG_PCI_BUS_CONFIG   0x6E
#define RHINE_REG_PCI_BUS_CONFIG1  0x6F
#define RHINE_REG_MII_CMD          0x70
#define RHINE_REG_MII_REG_ADDR     0x71
#define RHINE_REG_MII_DATA         0x72
#define RHINE_REG_MAC_REG_EECSR    0x74
#define RHINE_REG_CONFIG_A         0x78
#define RHINE_REG_CONFIG_B         0x79
#define RHINE_REG_CONFIG_C         0x7A
#define RHINE_REG_CONFIG_D         0x7B
#define RHINE_REG_RX_MISSED        0x7C
#define RHINE_REG_RX_CRC_ERRS      0x7E
#define RHINE_REG_MISC_CMD         0x81
#define RHINE_REG_STICKY_HW        0x83
#define RHINE_REG_INTR_STATUS2     0x84
#define RHINE_REG_CAM_MASK         0x88
#define RHINE_REG_CAM_CON          0x92
#define RHINE_REG_CAM_ADDR         0x93
#define RHINE_REG_WOLCR_SET        0xA0
#define RHINE_REG_PWCFG_SET        0xA1
#define RHINE_REG_WOLCG_SET        0xA3
#define RHINE_REG_WOLCR_CLR        0xA4
#define RHINE_REG_WOLCR_CLR1       0xA6
#define RHINE_REG_WOLCG_CLR        0xA7
#define RHINE_REG_PWRCSR_SET       0xA8
#define RHINE_REG_PWRCSR_SET1      0xA9
#define RHINE_REG_PWRCSR_CLR       0xAC
#define RHINE_REG_PWRCSR_CLR1      0xAD

#define RHINE_TCR_PQEN   0x01
#define RHINE_TCR_LB0    0x02
#define RHINE_TCR_LB1    0x04
#define RHINE_TCR_OFSET  0x08
#define RHINE_TCR_RTGOPT 0x10
#define RHINE_TCR_RTFT0  0x20
#define RHINE_TCR_RTFT1  0x40
#define RHINE_TCR_RTSF   0x80

#define RHINE_CAMC_CAMEN   0x01
#define RHINE_CAMC_VCAMSL  0x02
#define RHINE_CAMC_CAMWR   0x04
#define RHINE_CAMC_CAMRD   0x08

#define RHINE_WOL_UCAST  0x10
#define RHINE_WOL_MAGIC  0x20
#define RHINE_WOL_BMCAST 0x30
#define RHINE_WOL_LNKON  0x40
#define RHINE_WOL_LNKOFF 0x80

#define RHINE_CMD_INIT       0x01
#define RHINE_CMD_START      0x02
#define RHINE_CMD_STOP       0x04
#define RHINE_CMD_RX_ON      0x08
#define RHINE_CMD_TX_ON      0x10
#define RHINE_CMD1_TX_DEMAND 0x20
#define RHINE_CMD_RX_DEMAND  0x40
#define RHINE_CMD1_EARLY_RX  0x01
#define RHINE_CMD1_EARLY_TX  0x02
#define RHINE_CMD1_FDUPLEX   0x04
#define RHINE_CMD1_NO_TXPOLL 0x08
#define RHINE_CMD1_RESET     0x80

#define RHINE_DESC_OWN 0x80000000U
#define RHINE_DESC_TAG 0x00010000

#define RHINE_RX_OK        0x8000
#define RHINE_RX_WHOLE_PKT 0x0300
#define RHINE_RX_ERR       0x008F

#define RHINE_RQ_WOL          0x0001
#define RHINE_RQ_FORCE_RESET  0x0002
#define RHINE_RQ_6PATTERNS    0x0040
#define RHINE_RQ_STATUS_WB_RACE 0x0080
#define RHINE_RQ_RHINE_I      0x0100
#define RHINE_RQ_INT_PHY      0x0200
#define RHINE_RQ_MGMT         0x0400
#define RHINE_RQ_NEED_EN_MMIO 0x0800

struct rhine_tx_desc {
    uint32_t tx_status;
    uint32_t desc_length;
    uint32_t addr;
    uint32_t next_desc;
};

struct rhine_rx_desc {
    uint32_t rx_status;
    uint32_t desc_length;
    uint32_t addr;
    uint32_t next_desc;
};

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
    uint32_t intr_status;
    uint32_t intr_mask;

    uint8_t regs[0x100];

    uint32_t rx_ring_ptr;
    uint32_t tx_ring_ptr;

    uint32_t status_flags;
    uint32_t reset_state;
    uint8_t pm_state;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = (s->intr_status & s->intr_mask) != 0;

    if (msi_enabled(pdev)) {
        if (level) {
            msi_notify(pdev, 0);
        }
    } else {
        pci_set_irq(pdev, level);
    }
}

static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (!s->rx_ring_ptr && !s->tx_ring_ptr) {
        return;
    }

    if (!is_write) {
        if (!s->rx_ring_ptr) {
            return;
        }
        hwaddr desc_addr = s->rx_ring_ptr;
        struct rhine_rx_desc desc;
        pci_dma_read(pdev, desc_addr, &desc, sizeof(desc));
        le32_to_cpus(&desc.rx_status);
        le32_to_cpus(&desc.desc_length);
        le32_to_cpus(&desc.addr);
        le32_to_cpus(&desc.next_desc);
        if (!(desc.rx_status & RHINE_DESC_OWN)) {
            return;
        }

        uint32_t buf_len = desc.desc_length & 0xFFFF;
        if (buf_len > 1518) {
            buf_len = 1518;
        }
        uint8_t *tmp = g_malloc0(buf_len);
        for (uint32_t i = 0; i < buf_len; i++) {
            tmp[i] = i & 0xff;
        }
        pci_dma_write(pdev, desc.addr, tmp, buf_len);
        g_free(tmp);

        desc.rx_status &= ~RHINE_DESC_OWN;
        desc.rx_status |= (buf_len + 4) << 16;
        cpu_to_le32s(&desc.rx_status);
        cpu_to_le32s(&desc.desc_length);
        cpu_to_le32s(&desc.addr);
        cpu_to_le32s(&desc.next_desc);
        pci_dma_write(pdev, desc_addr, &desc, sizeof(desc));

        s->intr_status |= RHINE_INTR_RX_DONE;
        pcibase_update_irq(s);
    } else {
        if (!s->tx_ring_ptr) {
            return;
        }
        hwaddr desc_addr = s->tx_ring_ptr;
        struct rhine_tx_desc desc;
        pci_dma_read(pdev, desc_addr, &desc, sizeof(desc));
        le32_to_cpus(&desc.tx_status);
        le32_to_cpus(&desc.desc_length);
        le32_to_cpus(&desc.addr);
        le32_to_cpus(&desc.next_desc);
        if (!(desc.tx_status & RHINE_DESC_OWN)) {
            return;
        }

        uint32_t buf_len = desc.desc_length & 0xFFFF;
        if (!buf_len) {
            buf_len = 60;
        }
        uint8_t *tmp = g_malloc0(buf_len);
        pci_dma_read(pdev, desc.addr, tmp, buf_len);
        g_free(tmp);

        desc.tx_status &= ~RHINE_DESC_OWN;
        cpu_to_le32s(&desc.tx_status);
        cpu_to_le32s(&desc.desc_length);
        cpu_to_le32s(&desc.addr);
        cpu_to_le32s(&desc.next_desc);
        pci_dma_write(pdev, desc_addr, &desc, sizeof(desc));

        s->intr_status |= RHINE_INTR_TX_DONE;
        pcibase_update_irq(s);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= sizeof(s->regs)) {
        return 0;
    }

    switch (size) {
    case 1:
        return s->regs[addr];
    case 2:
        return s->regs[addr] | ((uint16_t)s->regs[addr + 1] << 8);
    case 4:
        return s->regs[addr] |
               ((uint32_t)s->regs[addr + 1] << 8) |
               ((uint32_t)s->regs[addr + 2] << 16) |
               ((uint32_t)s->regs[addr + 3] << 24);
    default:
        return 0;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= sizeof(s->regs)) {
        return;
    }

    switch (size) {
    case 1:
        s->regs[addr] = val & 0xff;
        break;
    case 2:
        s->regs[addr] = val & 0xff;
        s->regs[addr + 1] = (val >> 8) & 0xff;
        break;
    case 4:
        s->regs[addr] = val & 0xff;
        s->regs[addr + 1] = (val >> 8) & 0xff;
        s->regs[addr + 2] = (val >> 16) & 0xff;
        s->regs[addr + 3] = (val >> 24) & 0xff;
        break;
    default:
        return;
    }

    if (addr == RHINE_REG_INTR_STATUS && size == 2) {
        uint16_t mask = val & 0xFFFF;
        uint16_t cur = s->intr_status & 0xFFFF;
        cur &= ~mask;
        s->intr_status = (s->intr_status & 0xFFFF0000u) | cur;
        s->regs[RHINE_REG_INTR_STATUS] = cur & 0xff;
        s->regs[RHINE_REG_INTR_STATUS + 1] = (cur >> 8) & 0xff;
        pcibase_update_irq(s);
        return;
    }

    if (addr == RHINE_REG_INTR_ENABLE && size == 2) {
        uint16_t en = val & 0xFFFF;
        s->intr_mask = en;
        s->regs[RHINE_REG_INTR_ENABLE] = en & 0xff;
        s->regs[RHINE_REG_INTR_ENABLE + 1] = (en >> 8) & 0xff;
        pcibase_update_irq(s);
        return;
    }

    if (addr == RHINE_REG_RX_RING_PTR && size == 4) {
        s->rx_ring_ptr = val;
        return;
    }

    if (addr == RHINE_REG_TX_RING_PTR && size == 4) {
        s->tx_ring_ptr = val;
        return;
    }

    if (addr == RHINE_REG_CHIP_CMD && size == 2) {
        uint8_t cmd = s->regs[RHINE_REG_CHIP_CMD];
        uint8_t cmd1 = s->regs[RHINE_REG_CHIP_CMD1];

        if (cmd & RHINE_CMD_RX_ON) {
            pcibase_do_dma(s, false);
        }
        if (cmd & RHINE_CMD_TX_ON || (cmd1 & RHINE_CMD1_TX_DEMAND)) {
            pcibase_do_dma(s, true);
        }
        return;
    }

    if (addr == RHINE_REG_MII_CMD && size == 1) {
        uint8_t cmd = val & 0xff;
        s->regs[RHINE_REG_MII_CMD] = cmd;
        if (cmd & 0x40) {
            uint8_t reg = s->regs[RHINE_REG_MII_REG_ADDR];
            uint16_t data = 0xffff;
            if (reg == 1) {
                data = 0x7849;
            }
            s->regs[RHINE_REG_MII_DATA] = data & 0xff;
            s->regs[RHINE_REG_MII_DATA + 1] = (data >> 8) & 0xff;
            s->regs[RHINE_REG_MII_CMD] &= ~0x40;
        } else if (cmd & 0x20) {
            s->regs[RHINE_REG_MII_CMD] &= ~0x20;
        }
        return;
    }

    if (addr == RHINE_REG_STICKY_HW && size == 1) {
        s->regs[RHINE_REG_STICKY_HW] = val & 0xfc;
        return;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    return pcibase_mmio_read(s, addr, size);
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    pcibase_mmio_write(s, addr, val, size);
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

    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->rx_ring_ptr = 0;
    s->tx_ring_ptr = 0;
    s->status_flags = 0;
    s->reset_state = 0;
    s->pm_state = 0;

    s->regs[RHINE_REG_MII_STATUS] = 0;
    s->regs[RHINE_REG_MII_PHY_ADDR] = 0;
    s->regs[RHINE_REG_CONFIG_A] = 0;
    s->regs[RHINE_REG_CONFIG_B] = 0;
    s->regs[RHINE_REG_CONFIG_C] = 0;
    s->regs[RHINE_REG_CONFIG_D] = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  RHINE_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  RHINE_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, RHINE_CLASS_ID);
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
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "via-rhine-io";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x1000;
    s->bar_info[1].name = "via-rhine-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->rx_ring_ptr = 0;
    s->tx_ring_ptr = 0;
    s->status_flags = 0;
    s->reset_state = 0;
    s->pm_state = 0;
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
    .name = "via_rhine_pci",
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
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
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

