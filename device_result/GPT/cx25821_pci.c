/*
 * QEMU PCI device model for Conexant CX25821
 * Implementation Phase: basic MMIO register behavior and interrupt wiring
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

#define TYPE_PCIBASE_DEVICE "cx25821_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */

/* PCI IDs from cx25821_pci_tbl (first entry only) */
#define CX25821_PCI_VENDOR_ID 0x14f1
#define CX25821_PCI_DEVICE_ID 0x8210

/* Use generic multimedia/other class; driver doesn't specify explicit class id here */
#define CX25821_PCI_CLASS_ID  PCI_CLASS_OTHERS

/* Frequently used register offsets from cx25821-core.c */
#define CX25821_PCI_INT_MSK        0x040010
#define CX25821_PCI_INT_STAT       0x040014
#define CX25821_DEV_CNTRL2         0x040000
#define CX25821_AUD_A_INT_MSK      0x0400C0
#define CX25821_AUD_A_INT_STAT     0x0400C4
#define CX25821_AUD_B_INT_MSK      0x0400D0
#define CX25821_AUD_B_INT_STAT     0x0400D4
#define CX25821_AUD_B_INT_MSTAT    0x0400D8
#define CX25821_VID_A_INT_MSK      0x040020
#define CX25821_VID_A_INT_STAT     0x040024
#define CX25821_VID_A_INT_MSTAT    0x040028
#define CX25821_VID_B_INT_MSK      0x040030
#define CX25821_VID_B_INT_STAT     0x040034
#define CX25821_VID_B_INT_MSTAT    0x040038
#define CX25821_VID_C_INT_MSK      0x040040
#define CX25821_VID_C_INT_STAT     0x040044
#define CX25821_VID_C_INT_MSTAT    0x040048
#define CX25821_VID_D_INT_MSK      0x040050
#define CX25821_VID_D_INT_STAT     0x040054
#define CX25821_VID_D_INT_MSTAT    0x040058
#define CX25821_VID_E_INT_MSK      0x040060
#define CX25821_VID_E_INT_STAT     0x040064
#define CX25821_VID_E_INT_MSTAT    0x040068
#define CX25821_VID_F_INT_MSK      0x040070
#define CX25821_VID_F_INT_STAT     0x040074
#define CX25821_VID_F_INT_MSTAT    0x040078
#define CX25821_VID_G_INT_MSK      0x040080
#define CX25821_VID_G_INT_STAT     0x040084
#define CX25821_VID_G_INT_MSTAT    0x040088
#define CX25821_VID_H_INT_MSK      0x040090
#define CX25821_VID_H_INT_STAT     0x040094
#define CX25821_VID_H_INT_MSTAT    0x040098
#define CX25821_VID_I_INT_MSK      0x0400A0
#define CX25821_VID_I_INT_STAT     0x0400A4
#define CX25821_VID_I_INT_MSTAT    0x0400A8
#define CX25821_VID_J_INT_MSK      0x0400B0
#define CX25821_VID_J_INT_STAT     0x0400B4
#define CX25821_VID_J_INT_MSTAT    0x0400B8

/* GPIO and clock-related registers */
#define CX25821_GPIO_LO            0x110010
#define CX25821_GPIO_HI            0x110014
#define CX25821_GPIO_LO_OE         0x110018
#define CX25821_GPIO_HI_OE         0x11001C
#define CX25821_CLK_RST            0x11002C
#define CX25821_PAD_CTRL           0x110068
#define CX25821_VID_CH_MODE_SEL    0x110078
#define CX25821_VID_CH_CLK_SEL     0x11007C

/* I2C master 1 register offsets */
#define CX25821_I2C1_ADDR          0x180000
#define CX25821_I2C1_WDATA         0x180004
#define CX25821_I2C1_CTRL          0x180008
#define CX25821_I2C1_RDATA         0x18000C
#define CX25821_I2C1_STAT          0x180010

/* Some DMA pointer/count registers used in sram_channel table */
#define CX25821_DMA1_PTR1          0x100000
#define CX25821_DMA1_PTR2          0x100080
#define CX25821_DMA1_CNT1          0x100100
#define CX25821_DMA1_CNT2          0x100180

/* BAR index used for lmmio (driver uses dev->lmmio + (reg>>2)) is typically BAR0 */
#define CX25821_BAR_LMMIO_INDEX    0

/* We do not know the exact BAR size from this file; we'll expose a conservative region
 * and may refine later when more sources are available.
 */
#define CX25821_BAR_LMMIO_SIZE     (1 * MiB)


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
        uint32_t pci_int_msk;
        uint32_t pci_int_stat;
        uint32_t dev_cntrl2;
        uint32_t gpio_lo;
        uint32_t gpio_hi;
        uint32_t gpio_lo_oe;
        uint32_t gpio_hi_oe;
        uint32_t clk_rst;
        uint32_t pad_ctrl;
        uint32_t vid_ch_mode_sel;
        uint32_t vid_ch_clk_sel;
        uint32_t i2c1_addr;
        uint32_t i2c1_wdata;
        uint32_t i2c1_ctrl;
        uint32_t i2c1_rdata;
        uint32_t i2c1_stat;
    } regs;

    /* DMA Context */




};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The driver treats PCI_INT_STAT bits as per-channel interrupt summary,
     * masked by PCI_INT_MSK via cx_set/cx_clear operations. It clears
     * interrupts by writing the handled bits back to PCI_INT_STAT.
     *
     * We do not emulate internal DMA engines here, so we only need a very
     * simple mapping: if any enabled bit is set in regs.pci_int_stat, then
     * the PCI INTx line is asserted; clearing all bits deasserts it.
     */

    uint32_t pending = s->regs.pci_int_stat & s->regs.pci_int_msk;

    if (pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns
 *
 * The Linux driver sets up RISC programs and SRAM channel descriptors that
 * drive DMA, but the actual hardware DMA format and behaviour are
 * implemented in the driver only as bus-master descriptors; the device-initiated
 * DMA engine is not manipulated via explicit MMIO registers that would
 * require emulation here. Consequently, we do not implement any active
 * pci_dma_read/pci_dma_write engine in this model.
 */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* No device-initiated DMA implemented: the driver programs RISC
     * instructions and expects the hardware engine to fetch them, but the
     * core.c file does not directly expose MMIO registers that control
     * DMA transfers in a way we can safely emulate without additional
     * register documentation.
     */
    (void)s;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* The driver uses cx_read(), which reads 32-bit little-endian values
     * from dev->lmmio + (reg >> 2). That implies that cx_read expects
     * 32-bit accesses. We therefore only implement 4-byte accesses here.
     */

    if (size != 4) {
        /* For unsupported sizes, return 0 to be safe. */
        return 0;
    }

    switch (addr) {
    case CX25821_DEV_CNTRL2:
        val = s->regs.dev_cntrl2;
        break;
    case CX25821_PCI_INT_MSK:
        val = s->regs.pci_int_msk;
        break;
    case CX25821_PCI_INT_STAT:
        val = s->regs.pci_int_stat;
        break;

    case CX25821_GPIO_LO:
        val = s->regs.gpio_lo;
        break;
    case CX25821_GPIO_HI:
        val = s->regs.gpio_hi;
        break;
    case CX25821_GPIO_LO_OE:
        val = s->regs.gpio_lo_oe;
        break;
    case CX25821_GPIO_HI_OE:
        val = s->regs.gpio_hi_oe;
        break;
    case CX25821_CLK_RST:
        val = s->regs.clk_rst;
        break;
    case CX25821_PAD_CTRL:
        val = s->regs.pad_ctrl;
        break;
    case CX25821_VID_CH_MODE_SEL:
        val = s->regs.vid_ch_mode_sel;
        break;
    case CX25821_VID_CH_CLK_SEL:
        val = s->regs.vid_ch_clk_sel;
        break;

    case CX25821_I2C1_ADDR:
        val = s->regs.i2c1_addr;
        break;
    case CX25821_I2C1_WDATA:
        val = s->regs.i2c1_wdata;
        break;
    case CX25821_I2C1_CTRL:
        val = s->regs.i2c1_ctrl;
        break;
    case CX25821_I2C1_RDATA:
        /* The driver expects actual data from the external Medusa/Athena
         * device; without modelling the external I2C target, we simply
         * return the shadow register, which remains at its reset value.
         */
        val = s->regs.i2c1_rdata;
        break;
    case CX25821_I2C1_STAT:
        /* Bit 0 is used by i2c_slave_did_ack(); bit 1 by i2c_is_busy().
         * With no backing I2C engine, we simply report "not busy, no ACK".
         */
        val = s->regs.i2c1_stat & ~0x3;
        break;

    default:
        /* For unknown addresses within the BAR, just return 0. */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        /* Ignore non-32-bit accesses */
        return;
    }

    switch (addr) {
    case CX25821_DEV_CNTRL2:
        /* Driver writes 0x20 to enable RUN_RISC, and 0 to disable in
         * cx25821_shutdown(). We merely store the value.
         */
        s->regs.dev_cntrl2 = (uint32_t)val;
        break;

    case CX25821_PCI_INT_MSK:
        /* Master interrupt mask. The driver uses cx_write() to program
         * 0x2001FFFF and cx_set/cx_clear to manipulate bits.
         */
        s->regs.pci_int_msk = (uint32_t)val;
        pcibase_update_irq(s);
        break;

    case CX25821_PCI_INT_STAT:
        /* Interrupt status: the driver writes the mask of handled
         * channels to clear them (W1C). Exactly which bits are set is
         * determined by the hardware, but in the model the only source
         * of bits is software (possibly later test code), so we simply
         * implement W1C semantics.
         */
        s->regs.pci_int_stat &= ~((uint32_t)val);
        pcibase_update_irq(s);
        break;

    case CX25821_GPIO_LO:
        s->regs.gpio_lo = (uint32_t)val;
        break;
    case CX25821_GPIO_HI:
        s->regs.gpio_hi = (uint32_t)val;
        break;
    case CX25821_GPIO_LO_OE:
        s->regs.gpio_lo_oe = (uint32_t)val;
        break;
    case CX25821_GPIO_HI_OE:
        s->regs.gpio_hi_oe = (uint32_t)val;
        break;
    case CX25821_CLK_RST:
        s->regs.clk_rst = (uint32_t)val;
        break;
    case CX25821_PAD_CTRL:
        s->regs.pad_ctrl = (uint32_t)val;
        break;
    case CX25821_VID_CH_MODE_SEL:
        /* The driver masks and sets bits in this register to configure
         * channel modes and explicitly clears some upstream mode bits.
         * We just store the value.
         */
        s->regs.vid_ch_mode_sel = (uint32_t)val;
        break;
    case CX25821_VID_CH_CLK_SEL:
        s->regs.vid_ch_clk_sel = (uint32_t)val;
        break;

    case CX25821_I2C1_ADDR:
        s->regs.i2c1_addr = (uint32_t)val;
        break;
    case CX25821_I2C1_WDATA:
        s->regs.i2c1_wdata = (uint32_t)val;
        break;
    case CX25821_I2C1_CTRL:
        /* Driver uses this to initiate transfers and polls I2C1_STAT via
         * i2c_wait_done()/i2c_is_busy(). We don't model transfer timing,
         * so we simply store ctrl and immediately clear busy in STAT.
         */
        s->regs.i2c1_ctrl = (uint32_t)val;
        /* Clear BUSY bit (bit 1), keep ACK bit 0 cleared */
        s->regs.i2c1_stat &= ~0x2;
        break;
    case CX25821_I2C1_RDATA:
        /* RDATA is read-only from HW point of view; ignore writes but
         * keep shadow for completeness.
         */
        s->regs.i2c1_rdata = (uint32_t)val;
        break;
    case CX25821_I2C1_STAT:
        /* Status is mostly HW-controlled; ignore writes except for
         * updating the shadow copy.
         */
        s->regs.i2c1_stat = (uint32_t)val;
        break;

    default:
        /* Many registers used by other driver files (PLL_xxx, AUD_xxx,
         * channel dma_ctl/int_msk/int_stat/gpcnt_ctl etc.) reside inside
         * this BAR but are not defined in this skeleton. To remain
         * source-driven, we do not fabricate them; writes simply fall
         * into a black hole.
         */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;

    /* Driver does not use any PIO ports for this device. */

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;

    /* Driver does not use any PIO ports for this device. */
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

    /* Initialize register shadows to a sane reset state that matches the
     * expectations of cx25821_initialize() and cx25821_registers_init().
     */

    s->regs.pci_int_msk = 0x00000000;  /* All interrupts masked */
    s->regs.pci_int_stat = 0x00000000; /* No pending interrupts */
    s->regs.dev_cntrl2 = 0x00000000;   /* RISC controller disabled */

    s->regs.gpio_lo = 0x00000000;
    s->regs.gpio_hi = 0x00000000;
    s->regs.gpio_lo_oe = 0x00000000;
    s->regs.gpio_hi_oe = 0x00000000;

    s->regs.clk_rst = 0x00000000;
    s->regs.pad_ctrl = 0x00000000;
    s->regs.vid_ch_mode_sel = 0x00000000;
    s->regs.vid_ch_clk_sel = 0x00000000;

    s->regs.i2c1_addr = 0x00000000;
    s->regs.i2c1_wdata = 0x00000000;
    s->regs.i2c1_ctrl = 0x00000000;
    s->regs.i2c1_rdata = 0x00000000;
    s->regs.i2c1_stat = 0x00000000;    /* not busy, no ack */

    s->intr_status = 0;
    s->intr_mask = 0;

    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  CX25821_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  CX25821_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, 0x14f1);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x0920);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CX25821_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }
    s->bar_info[0].index = CX25821_BAR_LMMIO_INDEX;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = CX25821_BAR_LMMIO_SIZE;
    s->bar_info[0].name  = "cx25821-bar0";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = msi_present(pdev);
    s->has_msix = msix_present(pdev);
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "cx25821_pci",
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

