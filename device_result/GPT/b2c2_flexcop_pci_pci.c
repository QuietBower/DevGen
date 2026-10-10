/*
 * QEMU PCI device model for b2c2_flexcop_pci
 * Phase 2: Behavioral implementation based strictly on provided driver.
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

#define TYPE_PCIBASE_DEVICE "b2c2_flexcop_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCIBASE_VENDOR_ID 0x13d0
#define PCIBASE_DEVICE_ID 0x2103
#define PCIBASE_CLASS_ID  PCI_CLASS_MULTIMEDIA_OTHER

/* flexcop_ibi_register offsets used by the driver */
#define REG_DMA1_000            0x000
#define REG_DMA1_004            0x004
#define REG_DMA1_008            0x008
#define REG_DMA1_00C            0x00c
#define REG_DMA2_010            0x010
#define REG_DMA2_014            0x014
#define REG_DMA2_018            0x018
#define REG_DMA2_01C            0x01c
#define REG_TW_SM_C_100         0x100
#define REG_TW_SM_C_104         0x104
#define REG_TW_SM_C_108         0x108
#define REG_TW_SM_C_10C         0x10c
#define REG_TW_SM_C_110         0x110
#define REG_LNB_SWITCH_FREQ_200 0x200
#define REG_MISC_204            0x204
#define REG_CTRL_208            0x208
#define REG_IRQ_20C             0x20c
#define REG_SW_RESET_210        0x210
#define REG_MISC_214            0x214
#define REG_MBOX_V8_TO_HOST_218 0x218
#define REG_MBOX_HOST_TO_V8_21C 0x21c
#define REG_PID_FILTER_300      0x300
#define REG_PID_FILTER_304      0x304
#define REG_PID_FILTER_308      0x308
#define REG_PID_FILTER_30C      0x30c
#define REG_INDEX_REG_310       0x310
#define REG_PID_N_REG_314       0x314
#define REG_MAC_LOW_REG_318     0x318
#define REG_MAC_HIGH_REG_31C    0x31c
#define REG_DATA_TAG_400        0x400
#define REG_CARD_ID_408         0x408
#define REG_CARD_ID_40C         0x40c
#define REG_MAC_ADDRESS_418     0x418
#define REG_MAC_ADDRESS_41C     0x41c
#define REG_CI_600              0x600
#define REG_PI_604              0x604
#define REG_PI_608              0x608
#define REG_DVB_REG_60C         0x60c
#define REG_SRAM_CTRL_REG_700   0x700
#define REG_NET_BUF_REG_704     0x704
#define REG_CAI_BUF_REG_708     0x708
#define REG_CAO_BUF_REG_70C     0x70c
#define REG_MEDIA_BUF_REG_710   0x710
#define REG_SRAM_DEST_REG_714   0x714
#define REG_NET_BUF_REG_718     0x718
#define REG_WAN_CTRL_REG_71C    0x71c

/* BAR description */
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

/*
 * The driver uses fc_pci->io_mem = pci_iomap(pdev, 0, 0x800);
 * and then readl/writel(fc_pci->io_mem + r) where r is flexcop_ibi_register.
 * We therefore model a single MMIO BAR[0] of at least 0x800 bytes.
 */

#define PCIBASE_MMIO_SIZE 0x800

/*
 * We now have the definition of flexcop_ibi_value and the register map.
 * We keep a simple shadow array of 32-bit registers and implement only
 * the behavior explicitly exercised by the new driver snippets:
 *  - sw_reset_210.reset_block_* fields are reflected in the shadow.
 *  - sram_dest_reg_714 fields are read/modified/written via helpers.
 *  - tw_sm_c_100 is used as an opaque 32-bit value for I2C operations.
 */

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Hardware Register Shadows */
    uint32_t mmio_regs[PCIBASE_MMIO_SIZE / 4];

    /* Simple interrupt emulation state */
    uint32_t irq_status_reg;   /* synthetic IRQ status mirror */
    uint32_t irq_enable_reg;   /* synthetic IRQ enable mirror */
};

static bool pcibase_msi_enabled(PCIBaseState *s)
{
    return msi_enabled(PCI_DEVICE(s));
}

/* Internal helper for status-triggered signaling. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool pending = (s->irq_status_reg & s->irq_enable_reg) != 0;

    if (pending) {
        if (pcibase_msi_enabled(s)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!pcibase_msi_enabled(s)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns.
 * The provided driver code configures DMA related registers and uses
 * cpu_addr0/1 directly in software. No explicit hardware DMA reads/writes
 * are visible in the snippets, especially not during probe or basic
 * initialization. Therefore we keep this helper as a stub.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size > PCIBASE_MMIO_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "b2c2_flexcop_pci: MMIO read out of range addr=0x%" HWADDR_PRIx " size=%u\n",
                      addr, size);
        return 0;
    }

    /* Use register shadows. driver mainly uses readl (32-bit). */
    switch (size) {
    case 1: {
        uint32_t word = s->mmio_regs[addr >> 2];
        uint32_t shift = (addr & 3) * 8;
        val = (word >> shift) & 0xffu;
        break;
    }
    case 2: {
        uint32_t word = s->mmio_regs[addr >> 2];
        uint32_t shift = (addr & 2) * 8; /* bits 1:0 -> 0 or 16 */
        val = (word >> shift) & 0xffffu;
        break;
    }
    case 4: {
        val = s->mmio_regs[addr >> 2];
        break;
    }
    case 8: {
        /* Compose two consecutive 32-bit registers */
        uint32_t lo = s->mmio_regs[addr >> 2];
        uint32_t hi = s->mmio_regs[(addr >> 2) + 1];
        val = ((uint64_t)hi << 32) | lo;
        break;
    }
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "b2c2_flexcop_pci: invalid MMIO read size %u\n", size);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > PCIBASE_MMIO_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "b2c2_flexcop_pci: MMIO write out of range addr=0x%" HWADDR_PRIx " size=%u val=0x%" PRIx64 "\n",
                      addr, size, val);
        return;
    }

    /* Generic shadow update */
    switch (size) {
    case 1: {
        uint32_t word = s->mmio_regs[addr >> 2];
        uint32_t shift = (addr & 3) * 8;
        uint32_t mask = 0xffu << shift;
        word = (word & ~mask) | (((uint32_t)val & 0xffu) << shift);
        s->mmio_regs[addr >> 2] = word;
        break;
    }
    case 2: {
        uint32_t word = s->mmio_regs[addr >> 2];
        uint32_t shift = (addr & 2) * 8;
        uint32_t mask = 0xffffu << shift;
        word = (word & ~mask) | (((uint32_t)val & 0xffffu) << shift);
        s->mmio_regs[addr >> 2] = word;
        break;
    }
    case 4:
        s->mmio_regs[addr >> 2] = (uint32_t)val;
        break;
    case 8: {
        uint32_t lo = (uint32_t)(val & 0xffffffffu);
        uint32_t hi = (uint32_t)(val >> 32);
        s->mmio_regs[addr >> 2] = lo;
        s->mmio_regs[(addr >> 2) + 1] = hi;
        break;
    }
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "b2c2_flexcop_pci: invalid MMIO write size %u\n", size);
        return;
    }

    /*
     * Explicit behaviors derived from provided driver code:
     *
     * - sw_reset_210: flexcop_reset(fc) and flexcop_reset_block_300() write
     *   specific bitfields, but the driver only performs writes and optional
     *   later reads; no further side effects are required for probe/init.
     *   Keeping the raw value in the shadow is sufficient.
     *
     * - misc_204: flexcop_reset() reads and then toggles Per_reset_sig and
     *   later flexcop_determine_revision() reads the revision/capability
     *   bits. With no explicit reset values given, we keep it as simple
     *   R/W storage; higher level code must program the expected contents.
     *
     * - sram_dest_reg_714: flexcop_sram_set_dest() performs a read-modify-
     *   write sequence via read_ibi_reg()/write_ibi_reg(). Our generic
     *   shadow logic already supports this: reads return the last value
     *   written, and writes update the value.
     *
     * - tw_sm_c_100 (0x100) and tw_sm_c_104 (0x104): flexcop_i2c_read4() and
     *   flexcop_i2c_write4() use these registers purely as containers for
     *   software-managed bitfields and do explicit data copying through
     *   flexcop_ibi_value. No side effects are specified, so the shadow
     *   storage is sufficient.
     *
     * - flexcop_i2c_operation(): the driver sequences writes and reads of
     *   tw_sm_c_100, polling for bits no_base_addr_ack_error and st_done.
     *   The hardware behavior (how these bits change over time) is not
     *   described in the provided code, so we cannot emulate state changes
     *   without guessing. Therefore we keep the simple R/W shadow and do
     *   not modify these bits automatically.
     */

     /* * ==========================================
     * 关键修复：I2C State Machine Auto-Complete
     * ==========================================
     * Guest 驱动会写入 tw_sm_c_100 (0x100) 来发起 I2C 操作并轮询。
     * 根据 Linux 驱动定义:
     * - Bit 31: st_done (操作完成)
     * - Bit 30: no_base_addr_ack_error (无响应错误)
     * 我们在此拦截，强制标记完成且无错误，以防止 guest 陷入死循环。
     */
    if ((addr & ~3) == REG_TW_SM_C_100) {
        s->mmio_regs[REG_TW_SM_C_100 >> 2] |= (1U << 31);   /* 强制设置 st_done = 1 */
        s->mmio_regs[REG_TW_SM_C_100 >> 2] &= ~(1U << 30);  /* 强制清除 error = 0 */
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)size;
    /* Driver never uses I/O-port space; return 0. */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    (void)opaque;
    (void)addr;
    (void)val;
    (void)size;
    /* Driver never uses I/O-port space. */
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

    memset(s->mmio_regs, 0, sizeof(s->mmio_regs));

    /* * ==========================================
     * 关键修复：初始化硬件 MAC 地址
     * ==========================================
     * 提供一个合法的虚拟 MAC (例如: 12:34:56:78:9A:BC)，
     * 防止 guest 内核因 MAC 全零或读取错误而拒绝挂载网卡。
     */
    s->mmio_regs[REG_MAC_ADDRESS_418 >> 2] = 0x78563412; 
    s->mmio_regs[REG_MAC_ADDRESS_41C >> 2] = 0x0000BC9A;

    
    s->irq_status_reg = 0;
    s->irq_enable_reg = 0;
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Basic PCIe capability; the real card is conventional PCI, but the
     * template already enabled PCIe. Keeping it harmless. */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize BAR 0 as the MMIO region used by the driver. */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = PCIBASE_MMIO_SIZE;
    s->bar_info[0].name  = "b2c2_flexcop_pci-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize internal state */
    memset(s->mmio_regs, 0, sizeof(s->mmio_regs));
    s->irq_status_reg = 0;
    s->irq_enable_reg = 0;

    /* Optional MSI support; not required by the driver but harmless. */
    Error *local_err = NULL;
    if (msi_init(pdev, 0, 1, true, false, &local_err) == 0) {
        s->has_msi = true;
    } else {
        s->has_msi = false;
        if (local_err) {
            error_free(local_err);
        }
    }

    s->has_msix = false;
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

    (void)s;
}

static const VMStateDescription vmstate_pcibase = {
    .name = "b2c2_flexcop_pci_pci",
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

