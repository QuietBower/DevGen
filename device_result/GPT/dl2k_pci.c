/*
 * QEMU PCI device model for D-Link DL2K (dl2k.c based)
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
/* Removed missing header: hw/net/dl2k_regs.h */

#define TYPE_PCIBASE_DEVICE "dl2k_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define DL2K_VENDOR_ID 0x1186
#define DL2K_DEVICE_ID 0x4000
#define DL2K_CLASS_ID  PCI_CLASS_NETWORK_ETHERNET

/* Define register offsets used in this model (from dl2k driver register names) */
#define ResetBusy       0x0400
#define DMACtrl         0x0068
#define TFDListPtr0     0x0100
#define TFDListPtr1     0x0104
#define RFDListPtr0     0x0110
#define RFDListPtr1     0x0114

/* 修正后的真实 dl2k 硬件寄存器偏移量 */
#define ASICCtrl        0x0030
#define EepromData      0x0048
#define EepromCtrl      0x004a
#define CountDown       0x0054
#define IntEnable       0x005c
#define IntStatus       0x005e
#define TxStatus        0x0060
#define MACCtrl         0x006c
#define PhyCtrl         0x0076
#define ResetBusy       0x0400  /* 这是一个比特标志位，非偏移量 */

/* Descriptor structure used by the driver */
struct netdev_desc {
    uint64_t next_desc;
    uint64_t status;
    uint64_t fraginfo;
};

/* Interrupt-related default mask from driver */
#define DL2K_DEFAULT_INTR (RxDMAComplete | HostError | IntRequested | \
                           TxDMAComplete | UpdateStats | LinkEvent)

/* EEPROM and PHY control bits explicitly defined in driver */
#define DL2K_EEP_READ 0x0200
#define DL2K_EEP_BUSY 0x8000

#define DL2K_CHIP_IP1000A 1

#define DL2K_IPG_AC_LED_MODE       BIT(14)
#define DL2K_IPG_AC_LED_SPEED      BIT(27)
#define DL2K_IPG_AC_LED_MODE_BIT_1 BIT(29)

/* RX/TX ring and buffer related constants */
#define DL2K_RX_RING_SIZE   256
#define DL2K_TX_RING_SIZE   256
#define DL2K_MAX_JUMBO      8000
#define DL2K_PACKET_SIZE    1536

/* MII operation modes from driver */
#define MII_READ  1
#define MII_WRITE 0

/* BAR type enum */
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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    struct {
        uint32_t regs[0x1000 / 4]; /* simple flat 4KB register space */
    } regs;

    uint32_t status_flags;
    uint32_t reset_state;
    uint32_t power_state;
};


/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->intr_status & s->intr_mask;

    if (pending) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Only little-endian 1/2/4-byte accesses are expected from driver */
    if (addr + size > sizeof(s->regs.regs)) {
        return 0;
    }

    /* 【核心修复】：拦截 PHY 寄存器读取，欺骗 MII 检测 */
    if (size == 1 && addr == PhyCtrl) {
        uint8_t ctrl = ((uint8_t *)s->regs.regs)[addr];
        if ((ctrl & 0x04) == 0) { // 0x04 是 MII_WRITE。当 MII_WRITE 为 0 时是读取周期
            /* 使用 LFSR 生成伪随机比特，避免全是 0 或 1，从而返回一个合法的伪造 PHY ID */
            static uint16_t lfsr = 0xACE1;
            unsigned bit = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5)) & 1;
            lfsr = (lfsr >> 1) | (bit << 15);
            
            ctrl &= ~0x02;      // 清除 MII_DATA1
            ctrl |= (bit << 1); // 插入伪装数据的 Bit
        }
        return ctrl;
    }

    switch (size) {
    case 1:
        val = ((uint8_t *)s->regs.regs)[addr];
        break;
    case 2:
        val = le16_to_cpu(*(uint16_t *)(((uint8_t *)s->regs.regs) + addr));
        break;
    case 4:
        val = le32_to_cpu(*(uint32_t *)(((uint8_t *)s->regs.regs) + addr));
        break;
    default:
        /* driver never does 8-byte MMIO */
        val = 0;
        break;
    }

    /* IntStatus is read via dr16(IntStatus) in ISR */
    if (size == 2 && addr == IntStatus) {
        /* return current status (already little-endian converted) */
    }

    /* EepromCtrl busy bit handling: clear BUSY when driver polls */
    if (size == 2 && addr == EepromCtrl) {
        uint16_t cur = (uint16_t)val;
        cur &= ~DL2K_EEP_BUSY;
        val = cur;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > sizeof(s->regs.regs)) {
        return;
    }

    /* generic register write */
    switch (size) {
    case 1:
        ((uint8_t *)s->regs.regs)[addr] = (uint8_t)val;
        break;
    case 2:
        *(uint16_t *)(((uint8_t *)s->regs.regs) + addr) = cpu_to_le16((uint16_t)val);
        break;
    case 4:
        *(uint32_t *)(((uint8_t *)s->regs.regs) + addr) = cpu_to_le32((uint32_t)val);
        break;
    default:
        return;
    }

    /* Interrupt enable register */
    if (size == 2 && addr == IntEnable) {
        s->intr_mask = (uint16_t)val;
        pcibase_update_irq(s);
        return;
    }

    /* Interrupt status: driver does dw16(IntStatus, int_status) to clear */
    if (size == 2 && addr == IntStatus) {
        uint32_t clear = (uint16_t)val;
        s->intr_status &= ~clear;
        pcibase_update_irq(s);
        return;
    }

    /* ASICCtrl + 2: various reset bits; we just clear busy immediately */
    if (size == 2 && addr == ASICCtrl + 2) {
        /* clear ResetBusy bit in our shadow */
        uint16_t reg = le16_to_cpu(*(uint16_t *)(((uint8_t *)s->regs.regs) + addr));
        reg &= ~ResetBusy;
        *(uint16_t *)(((uint8_t *)s->regs.regs) + addr) = cpu_to_le16(reg);
        return;
    }

    /* MACCtrl write: update shadow only, driver polls/sets bits */
    if ((size == 2 && addr == MACCtrl) || (size == 4 && addr == MACCtrl)) {
        return;
    }

    /* CountDown write: used to schedule ISR in HW; here it's just a no-op */
    if (size == 4 && addr == CountDown) {
        return;
    }

    /* DMACtrl, Tx/Rx DMA control registers: no real DMA in this minimal model */
    if (size == 4 && addr == DMACtrl) {
        return;
    }

    /* TFDListPtr0/1, RFDListPtr0/1: descriptor pointers; we ignore */
    if (size == 4 && (addr == TFDListPtr0 || addr == TFDListPtr1 ||
                      addr == RFDListPtr0 || addr == RFDListPtr1)) {
        return;
    }

    /* 【核心修复】：处理驱动向 EEPROM 发出的读命令 */
    if (size == 2 && addr == EepromCtrl) {
        uint16_t eep_addr = val & 0xff;
        
        // 1. 立即清除 BUSY 状态
        uint16_t cur = le16_to_cpu(*(uint16_t *)(((uint8_t *)s->regs.regs) + addr));
        cur &= ~DL2K_EEP_BUSY;
        *(uint16_t *)(((uint8_t *)s->regs.regs) + addr) = cpu_to_le16(cur);

        // 2. 填充伪造的 EEPROM MAC 地址 (52:54:00:12:34:56)
        uint16_t mock_data = 0x0000;
        if (eep_addr == 0) mock_data = 0x5452;
        else if (eep_addr == 1) mock_data = 0x1200;
        else if (eep_addr == 2) mock_data = 0x5634;
        
        *(uint16_t *)(((uint8_t *)s->regs.regs) + EepromData) = cpu_to_le16(mock_data);
        return;
    }

    /* RxDMAPollPeriod, TxDMAPollPeriod, RxDMABurstThresh, RxDMAUrgentThresh,
     * RxDMAIntCtrl, RmonStatMask, VLANId, VLANTag, HashTable0/1,
     * ReceiveMode, StationAddr0, MaxFrameSize, DebugCtrl, etc. are all
     * maintained as simple shadows with no additional behavior here. */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size > sizeof(s->regs.regs)) {
        return 0;
    }

    switch (size) {
    case 1:
        val = ((uint8_t *)s->regs.regs)[addr];
        break;
    case 2:
        val = le16_to_cpu(*(uint16_t *)(((uint8_t *)s->regs.regs) + addr));
        break;
    case 4:
        val = le32_to_cpu(*(uint32_t *)(((uint8_t *)s->regs.regs) + addr));
        break;
    default:
        val = 0;
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > sizeof(s->regs.regs)) {
        return;
    }

    switch (size) {
    case 1:
        ((uint8_t *)s->regs.regs)[addr] = (uint8_t)val;
        break;
    case 2:
        *(uint16_t *)(((uint8_t *)s->regs.regs) + addr) = cpu_to_le16((uint16_t)val);
        break;
    case 4:
        *(uint32_t *)(((uint8_t *)s->regs.regs) + addr) = cpu_to_le32((uint32_t)val);
        break;
    default:
        return;
    }

    if (size == 2 && addr == IntEnable) {
        s->intr_mask = (uint16_t)val;
        pcibase_update_irq(s);
        return;
    }
    if (size == 2 && addr == IntStatus) {
        uint32_t clear = (uint16_t)val;
        s->intr_status &= ~clear;
        pcibase_update_irq(s);
        return;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
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

    s->intr_status = 0;
    s->intr_mask = 0;
    s->status_flags = 0;
    s->reset_state = 0;
    s->power_state = 0;

    memset(s->regs.regs, 0, sizeof(s->regs.regs));

    /* mimic hardware reset defaults where visible in driver */
    /* Link down by default */
    s->regs.regs[ASICCtrl / 4] = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  DL2K_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DL2K_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, DL2K_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x0c);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    // 【修改点】将 BAR 数量改为 2，并初始化 BAR 1
    s->num_bars = 2; 

    // 原始 BAR 0
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x20000; /* 128 KiB */
    s->bar_info[0].name = "dl2k-mmio0";

    // 新增 BAR 1
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x20000; /* 128 KiB */
    s->bar_info[1].name = "dl2k-mmio1";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    s->intr_status = 0;
    s->intr_mask = 0;
    s->status_flags = 0;
    s->reset_state = 0;
    s->power_state = 0;
    memset(s->regs.regs, 0, sizeof(s->regs.regs));
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
    .name = "dl2k_pci",
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

