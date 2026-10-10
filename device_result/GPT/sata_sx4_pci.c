/*
 * QEMU PCI device model for sata_sx4 (Promise 20621)
 * Phase 2: Minimal behavioral implementation to satisfy Linux driver probe.
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
#include "hw/irq.h"

#define TYPE_PCIBASE_DEVICE "sata_sx4_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_PROMISE        0x105a
#define PCI_CLASS_STORAGE_SATA       0x0106

#define PCIBASE_VENDOR_ID  PCI_VENDOR_ID_PROMISE
#define PCIBASE_DEVICE_ID  0x6622
#define PCIBASE_CLASS_ID   PCI_CLASS_STORAGE_SATA

/*
 * The real hw has multiple BARs and rich register set. The driver however
 * only requires that BARs exist, MMIO accesses succeed, and that interrupts
 * are generated so libata can complete commands. No exact register semantics
 * are required for basic probe to succeed.
 */

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

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Simple shadow space backing BAR0 MMIO */
    uint8_t *mmio_data;
    hwaddr mmio_size;

    /* Basic interrupt state */
    bool irq_level;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /*
     * Very simple policy: if any event wants an interrupt, assert INTx.
     * The driver only uses a shared legacy IRQ; MSI/MSI-X are not enabled
     * by sata_sx4, so we don't need to support them.
     */
    if (s->irq_level) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Helper to raise a one-shot interrupt (e.g. to complete a fake command). */
static void pcibase_pulse_irq(PCIBaseState *s)
{
    s->irq_level = true;
    pcibase_update_irq(s);
    /* Immediately lower again; edge semantics are enough for Linux. */
    s->irq_level = false;
    pcibase_update_irq(s);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (!s->mmio_data || addr + size > s->mmio_size) {
        return 0;
    }

    uint64_t val = 0;

    /* Emulate little-endian byte loads from backing array */
    switch (size) {
    case 1:
        val = s->mmio_data[addr];
        break;
    case 2:
        val = s->mmio_data[addr] |
              ((uint16_t)s->mmio_data[addr + 1] << 8);
        break;
    case 4:
        val = s->mmio_data[addr] |
              ((uint32_t)s->mmio_data[addr + 1] << 8) |
              ((uint32_t)s->mmio_data[addr + 2] << 16) |
              ((uint32_t)s->mmio_data[addr + 3] << 24);
        break;
    case 8:
        val = (uint64_t)s->mmio_data[addr] |
              ((uint64_t)s->mmio_data[addr + 1] << 8) |
              ((uint64_t)s->mmio_data[addr + 2] << 16) |
              ((uint64_t)s->mmio_data[addr + 3] << 24) |
              ((uint64_t)s->mmio_data[addr + 4] << 32) |
              ((uint64_t)s->mmio_data[addr + 5] << 40) |
              ((uint64_t)s->mmio_data[addr + 6] << 48) |
              ((uint64_t)s->mmio_data[addr + 7] << 56);
        break;
    default:
        /* Unsupported size, return 0 */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (!s->mmio_data || addr + size > s->mmio_size) {
        return;
    }

    /* 1. 先将数据原样写入影子内存数组 */
    switch (size) {
    case 1: s->mmio_data[addr] = val & 0xff; break;
    case 2: 
        s->mmio_data[addr] = val & 0xff;
        s->mmio_data[addr + 1] = (val >> 8) & 0xff;
        break;
    case 4:
        s->mmio_data[addr] = val & 0xff;
        s->mmio_data[addr + 1] = (val >> 8) & 0xff;
        s->mmio_data[addr + 2] = (val >> 16) & 0xff;
        s->mmio_data[addr + 3] = (val >> 24) & 0xff;
        break;
    case 8:
        s->mmio_data[addr] = val & 0xff;
        s->mmio_data[addr + 1] = (val >> 8) & 0xff;
        s->mmio_data[addr + 2] = (val >> 16) & 0xff;
        s->mmio_data[addr + 3] = (val >> 24) & 0xff;
        s->mmio_data[addr + 4] = (val >> 32) & 0xff;
        s->mmio_data[addr + 5] = (val >> 40) & 0xff;
        s->mmio_data[addr + 6] = (val >> 48) & 0xff;
        s->mmio_data[addr + 7] = (val >> 56) & 0xff;
        break;
    }

    /* 2. 处理需要硬件响应的特殊控制寄存器 */
    hwaddr offset = addr;
    hwaddr base_addr = addr;
    if (offset >= 0xC0000) {
        offset -= 0xC0000;
        base_addr -= offset; /* 锁定芯片寄存器基址 */
    } else {
        base_addr = 0;
    }

    /* [拦截点 A] 真正的 I2C 控制寄存器 (0x48) */
    if (offset == 0x48 /* PDC_I2C_CONTROL */) {
        if (val & (1 << 7)) { /* 硬件检测到 PDC_I2C_START */
            
            /* 1. 瞬间完成 I2C，置位 PDC_I2C_COMPLETE (Bit 16 对应 0x48 的第 2 字节 bit 0) */
            s->mmio_data[base_addr + 0x48 + 2] |= 1; 

            /* 2. 从 0x4C (PDC_I2C_ADDR_DATA) 读出请求的 I2C 设备和子地址 */
            uint32_t i2c_data = s->mmio_data[base_addr + 0x4C] | 
                                (s->mmio_data[base_addr + 0x4D] << 8) | 
                                (s->mmio_data[base_addr + 0x4E] << 16) | 
                                (s->mmio_data[base_addr + 0x4F] << 24);
                                
            uint8_t device = (i2c_data >> 24) & 0xff;
            uint8_t subaddr = (i2c_data >> 16) & 0xff;
            
            /* 3. 如果正在读取 DIMM0 的 SPD 数据 (0x50) */
            if (device == 0x50 /* PDC_DIMM0_SPD_DEV_ADDRESS */) {
                uint8_t spd_val = 0;
                switch (subaddr) {
                    case 126: spd_val = 100; break; /* FREQ: 过 detect_dimm 校验 */
                    case 9:   spd_val = 0x70; break; /* 备用 FREQ 校验 */
                    case 4:   spd_val = 8; break;    /* COLUMN: 防止减 8 时下溢 */
                    case 3:   spd_val = 11; break;   /* ROW: 防止减 11 时下溢 */
                    case 17:  spd_val = 4; break;    /* BANK: 防止除以 4 为零 */
                    case 5:   spd_val = 2; break;    /* MODULE_ROW: 防止除以 2 为零 */
                    case 27:  spd_val = 20; break;   /* ROW_PRE_CHARGE */
                    case 28:  spd_val = 20; break;   /* ROW_ACTIVE_DELAY */
                    case 29:  spd_val = 20; break;   /* RAS_CAS_DELAY */
                    case 30:  spd_val = 40; break;   /* ACTIVE_PRECHARGE */
                    case 18:  spd_val = 4; break;    /* CAS_LATENCY */
                    case 11:  spd_val = 4; break;    /* TYPE: SDRAM (避开 0x02，绕过内核缓慢的 ECC 初始化) */
                    default:  spd_val = 0; break;
                }
                
                /* 将伪造的 SPD 数据写回 0x4C 的 Bits 8:15 中供内核读取 */
                s->mmio_data[base_addr + 0x4C + 1] = spd_val;
            }
        }
    } 
    /* [拦截点 B] SDRAM 内存控制器初始化 (0x88) */
    else if (offset == 0x88 /* PDC_SDRAM_CONTROL */) {
        /* 内核会写入 Bit 19，并死循环等待硬件将该位清零。
         * 这里主动在影子内存中清掉 Bit 19 (对应 0x8A 的 bit 3)，让内核瞬间结束 polling。
         */
        s->mmio_data[base_addr + 0x88 + 2] &= ~0x08; 
    }

    /* 模拟中断唤醒 (保留原有逻辑) */
    if (size == 4) {
        pcibase_pulse_irq(s);
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* The sata_sx4 driver uses only MMIO via pcim_iomap_regions; no IO ports
     * are referenced in the provided code. Return 0 for any stray access.
     */
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

    /* Clear MMIO backing store and drop pending interrupts. */
    if (s->mmio_data && s->mmio_size) {
        memset(s->mmio_data, 0, s->mmio_size);
    }
    s->irq_level = false;
    pcibase_update_irq(s);
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
        /* Backing store for MMIO BAR0 shadow */
        if (bi->index == 3) {
            s->mmio_size = aligned_size;
            s->mmio_data = g_malloc0(aligned_size);
        }
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

    /* Expose as a conventional PCI device; express cap present in template. */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR 3: MMIO 寄存器空间 (匹配驱动 PDC_MMIO_BAR = 3) */
    s->num_bars = 2;
    s->bar_info[0].index = 3;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x100000; /* 1MB，足以覆盖 0xC0000 的 PDC_CHIP0_OFS */
    s->bar_info[0].name  = "sata_sx4-mmio";

    /* BAR 4: DIMM 内存空间 (匹配驱动 PDC_DIMM_BAR = 4) */
    s->bar_info[1].index = 4;
    s->bar_info[1].type  = BAR_TYPE_RAM; /* 直接使用 QEMU RAM 处理大段内存更稳定 */
    s->bar_info[1].size  = 0x200000; /* 2MB DIMM，满足初始探测即可 */
    s->bar_info[1].name  = "sata_sx4-dimm";

    /* 填充剩余未使用的 BAR 槽位，避开索引 3 和 4，防止冲突 */
    int unused_indices[] = {0, 1, 2, 5};
    for (int i = 2; i < 6; i++) {
        s->bar_info[i].index = unused_indices[i - 2];
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X usage in sata_sx4 driver */
    s->has_msi = false;
    s->has_msix = false;

    s->irq_level = false;
    pcibase_update_irq(s);
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

    if (s->mmio_data) {
        g_free(s->mmio_data);
        s->mmio_data = NULL;
        s->mmio_size = 0;
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "sata_sx4_pci",
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
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
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

