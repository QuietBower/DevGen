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
#include "qemu/bitops.h"

#define TYPE_PCIBASE_DEVICE "ni_660x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1093
#define DEVICE_ID 0x1310
#define CLASS_ID PCI_CLASS_OTHERS

#define NI660X_CLK_CFG_COUNTER_SWAP	BIT(21)
#define NI660X_GLOBAL_INT_COUNTER0	BIT(8)
#define NI660X_GLOBAL_INT_COUNTER1	BIT(9)
#define NI660X_GLOBAL_INT_COUNTER2	BIT(10)
#define NI660X_GLOBAL_INT_COUNTER3	BIT(11)
#define NI660X_GLOBAL_INT_CASCADE	BIT(29)
#define NI660X_GLOBAL_INT_GLOBAL_POL	BIT(30)
#define NI660X_GLOBAL_INT_GLOBAL	BIT(31)
#define NI660X_DMA_CFG_SEL(_c, _s)	(((_s) & 0x1f) << (8 * (_c)))
#define NI660X_DMA_CFG_SEL_MASK(_c)	NI660X_DMA_CFG_SEL((_c), 0x1f)
#define NI660X_DMA_CFG_SEL_NONE(_c)	NI660X_DMA_CFG_SEL((_c), 0x1f)
#define NI660X_DMA_CFG_RESET(_c)	NI660X_DMA_CFG_SEL((_c), 0x80)
#define NI660X_IO_CFG(x)		(NI660X_IO_CFG_0_1 + ((x) / 2))
#define NI660X_IO_CFG_OUT_SEL(_c, _s)	(((_s) & 0x3) << (((_c) % 2) ? 0 : 8))
#define NI660X_IO_CFG_OUT_SEL_MASK(_c)	NI660X_IO_CFG_OUT_SEL((_c), 0x3)
#define NI660X_IO_CFG_IN_SEL(_c, _s)	(((_s) & 0x7) << (((_c) % 2) ? 4 : 12))
#define NI660X_IO_CFG_IN_SEL_MASK(_c)	NI660X_IO_CFG_IN_SEL((_c), 0x7)
#define NI660X_CHIP_OFFSET		0x800
#define NI660X_NUM_PFI_CHANNELS		40
#define NI660X_MAX_DMA_CHANNEL		4
#define NI660X_COUNTERS_PER_CHIP	4
#define NI660X_MAX_CHIPS		2
#define NI660X_MAX_COUNTERS		(NI660X_MAX_CHIPS * NI660X_COUNTERS_PER_CHIP)
#define MAX_MITE_DMA_CHANNELS 8
#define MITE_IODWBSR		0xc0
#define MITE_IODWBSR_1		0xc4
#define WENAB			BIT(7)
#define MITE_IODWCR_1		0xf4

/* MITE_CHCR(x) requires MITE_CHAN(x) which is currently missing */
/* #define MITE_CHCR(x)		(0x04 + MITE_CHAN(x)) */
#define CHCR_CLR_DMA_IE		BIT(30)
#define CHCR_CLR_LINKP_IE	BIT(28)
#define CHCR_CLR_SAR_IE		BIT(26)
#define CHCR_CLR_DONE_IE	BIT(24)
#define CHCR_CLR_MRDY_IE	BIT(22)
#define CHCR_CLR_DRDY_IE	BIT(20)
#define CHCR_CLR_LC_IE		BIT(18)
#define CHCR_CLR_CONT_RB_IE	BIT(16)

#define NITIO_LOADA_REG(x)		(NITIO_G0_LOADA + (x))
#define NITIO_LOADB_REG(x)		(NITIO_G0_LOADB + (x))
#define NITIO_CMD_REG(x)		(NITIO_G0_CMD + (x))
#define NITIO_AUTO_INC_REG(x)		(NITIO_G0_AUTO_INC + (x))
#define NITIO_MODE_REG(x)		(NITIO_G0_MODE + (x))
#define NITIO_INPUT_SEL_REG(x)		(NITIO_G0_INPUT_SEL + (x))
#define NITIO_CNT_MODE_REG(x)		(NITIO_G0_CNT_MODE + (x))
#define NITIO_GATE2_REG(x)		(NITIO_G0_GATE2 + (x))
#define NITIO_DMA_CFG_REG(x)		(NITIO_G0_DMA_CFG + (x))
#define NITIO_INT_ENA_REG(x)		(NITIO_G0_INT_ENA + (x))
#define NITIO_SHARED_STATUS_REG(x)	(NITIO_G01_STATUS + ((x) / 2))
#define NITIO_DMA_STATUS_REG(x)		(NITIO_G0_DMA_STATUS + (x))

#define GI_LOAD				BIT(2)
#define GI_SYNC_GATE			BIT(8)
#define GI_ARMED(x)			(((x) % 2) ? BIT(9) : BIT(8))
#define GI_COUNTING(x)			(((x) % 2) ? BIT(3) : BIT(2))
#define GI_DRQ_ERROR			BIT(14)
#define GI_GATE_INTERRUPT_ENABLE(x)	(((x) % 2) ? BIT(10) : BIT(8))

struct mite_dma_desc {
	uint32_t count;
	uint32_t addr;
	uint32_t next;
	uint32_t dar;
};

enum ni_gpct_register {
    NITIO_G0_AUTO_INC,
    NITIO_G1_AUTO_INC,
    NITIO_G2_AUTO_INC,
    NITIO_G3_AUTO_INC,
    NITIO_G0_CMD,
    NITIO_G1_CMD,
    NITIO_G2_CMD,
    NITIO_G3_CMD,
    NITIO_G0_HW_SAVE,
    NITIO_G1_HW_SAVE,
    NITIO_G2_HW_SAVE,
    NITIO_G3_HW_SAVE,
    NITIO_G0_SW_SAVE,
    NITIO_G1_SW_SAVE,
    NITIO_G2_SW_SAVE,
    NITIO_G3_SW_SAVE,
    NITIO_G0_MODE,
    NITIO_G1_MODE,
    NITIO_G2_MODE,
    NITIO_G3_MODE,
    NITIO_G0_LOADA,
    NITIO_G1_LOADA,
    NITIO_G2_LOADA,
    NITIO_G3_LOADA,
    NITIO_G0_LOADB,
    NITIO_G1_LOADB,
    NITIO_G2_LOADB,
    NITIO_G3_LOADB,
    NITIO_G0_INPUT_SEL,
    NITIO_G1_INPUT_SEL,
    NITIO_G2_INPUT_SEL,
    NITIO_G3_INPUT_SEL,
    NITIO_G0_CNT_MODE,
    NITIO_G1_CNT_MODE,
    NITIO_G2_CNT_MODE,
    NITIO_G3_CNT_MODE,
    NITIO_G0_GATE2,
    NITIO_G1_GATE2,
    NITIO_G2_GATE2,
    NITIO_G3_GATE2,
    NITIO_G01_STATUS,
    NITIO_G23_STATUS,
    NITIO_G01_RESET,
    NITIO_G23_RESET,
    NITIO_G01_STATUS1,
    NITIO_G23_STATUS1,
    NITIO_G01_STATUS2,
    NITIO_G23_STATUS2,
    NITIO_G0_DMA_CFG,
    NITIO_G1_DMA_CFG,
    NITIO_G2_DMA_CFG,
    NITIO_G3_DMA_CFG,
    NITIO_G0_DMA_STATUS,
    NITIO_G1_DMA_STATUS,
    NITIO_G2_DMA_STATUS,
    NITIO_G3_DMA_STATUS,
    NITIO_G0_ABZ,
    NITIO_G1_ABZ,
    NITIO_G0_INT_ACK,
    NITIO_G1_INT_ACK,
    NITIO_G2_INT_ACK,
    NITIO_G3_INT_ACK,
    NITIO_G0_STATUS,
    NITIO_G1_STATUS,
    NITIO_G2_STATUS,
    NITIO_G3_STATUS,
    NITIO_G0_INT_ENA,
    NITIO_G1_INT_ENA,
    NITIO_G2_INT_ENA,
    NITIO_G3_INT_ENA,
    NITIO_NUM_REGS,
};

enum ni_660x_register {
    NI660X_STC_DIO_PARALLEL_INPUT = NITIO_NUM_REGS,
    NI660X_STC_DIO_OUTPUT,
    NI660X_STC_DIO_CONTROL,
    NI660X_STC_DIO_SERIAL_INPUT,
    NI660X_DIO32_INPUT,
    NI660X_DIO32_OUTPUT,
    NI660X_CLK_CFG,
    NI660X_GLOBAL_INT_STATUS,
    NI660X_DMA_CFG,
    NI660X_GLOBAL_INT_CFG,
    NI660X_IO_CFG_0_1,
    NI660X_IO_CFG_2_3,
    NI660X_IO_CFG_4_5,
    NI660X_IO_CFG_6_7,
    NI660X_IO_CFG_8_9,
    NI660X_IO_CFG_10_11,
    NI660X_IO_CFG_12_13,
    NI660X_IO_CFG_14_15,
    NI660X_IO_CFG_16_17,
    NI660X_IO_CFG_18_19,
    NI660X_IO_CFG_20_21,
    NI660X_IO_CFG_22_23,
    NI660X_IO_CFG_24_25,
    NI660X_IO_CFG_26_27,
    NI660X_IO_CFG_28_29,
    NI660X_IO_CFG_30_31,
    NI660X_IO_CFG_32_33,
    NI660X_IO_CFG_34_35,
    NI660X_IO_CFG_36_37,
    NI660X_IO_CFG_38_39,
    NI660X_NUM_REGS,
};

struct ni_660x_register_data {
    int offset;
    char size;
};

static const struct ni_660x_register_data ni_660x_reg_data[NI660X_NUM_REGS] = {
    [NITIO_G0_INT_ACK]		= { 0x004, 2 },
    [NITIO_G0_STATUS]		= { 0x004, 2 },
    [NITIO_G1_INT_ACK]		= { 0x006, 2 },
    [NITIO_G1_STATUS]		= { 0x006, 2 },
    [NITIO_G01_STATUS]		= { 0x008, 2 },
    [NITIO_G0_CMD]			= { 0x00c, 2 },
    [NI660X_STC_DIO_PARALLEL_INPUT]	= { 0x00e, 2 },
    [NITIO_G1_CMD]			= { 0x00e, 2 },
    [NITIO_G0_HW_SAVE]		= { 0x010, 4 },
    [NITIO_G1_HW_SAVE]		= { 0x014, 4 },
    [NI660X_STC_DIO_OUTPUT]		= { 0x014, 2 },
    [NI660X_STC_DIO_CONTROL]	= { 0x016, 2 },
    [NITIO_G0_SW_SAVE]		= { 0x018, 4 },
    [NITIO_G1_SW_SAVE]		= { 0x01c, 4 },
    [NITIO_G0_MODE]			= { 0x034, 2 },
    [NITIO_G01_STATUS1]		= { 0x036, 2 },
    [NITIO_G1_MODE]			= { 0x036, 2 },
    [NI660X_STC_DIO_SERIAL_INPUT]	= { 0x038, 2 },
    [NITIO_G0_LOADA]		= { 0x038, 4 },
    [NITIO_G01_STATUS2]		= { 0x03a, 2 },
    [NITIO_G0_LOADB]		= { 0x03c, 4 },
    [NITIO_G1_LOADA]		= { 0x040, 4 },
    [NITIO_G1_LOADB]		= { 0x044, 4 },
    [NITIO_G0_INPUT_SEL]		= { 0x048, 2 },
    [NITIO_G1_INPUT_SEL]		= { 0x04a, 2 },
    [NITIO_G0_AUTO_INC]		= { 0x088, 2 },
    [NITIO_G1_AUTO_INC]		= { 0x08a, 2 },
    [NITIO_G01_RESET]		= { 0x090, 2 },
    [NITIO_G0_INT_ENA]		= { 0x092, 2 },
    [NITIO_G1_INT_ENA]		= { 0x096, 2 },
    [NITIO_G0_CNT_MODE]		= { 0x0b0, 2 },
    [NITIO_G1_CNT_MODE]		= { 0x0b2, 2 },
    [NITIO_G0_GATE2]		= { 0x0b4, 2 },
    [NITIO_G1_GATE2]		= { 0x0b6, 2 },
    [NITIO_G0_DMA_CFG]		= { 0x0b8, 2 },
    [NITIO_G0_DMA_STATUS]		= { 0x0b8, 2 },
    [NITIO_G1_DMA_CFG]		= { 0x0ba, 2 },
    [NITIO_G1_DMA_STATUS]		= { 0x0ba, 2 },
    [NITIO_G2_INT_ACK]		= { 0x104, 2 },
    [NITIO_G2_STATUS]		= { 0x104, 2 },
    [NITIO_G3_INT_ACK]		= { 0x106, 2 },
    [NITIO_G3_STATUS]		= { 0x106, 2 },
    [NITIO_G23_STATUS]		= { 0x108, 2 },
    [NITIO_G2_CMD]			= { 0x10c, 2 },
    [NITIO_G3_CMD]			= { 0x10e, 2 },
    [NITIO_G2_HW_SAVE]		= { 0x110, 4 },
    [NITIO_G3_HW_SAVE]		= { 0x114, 4 },
    [NITIO_G2_SW_SAVE]		= { 0x118, 4 },
    [NITIO_G3_SW_SAVE]		= { 0x11c, 4 },
    [NITIO_G2_MODE]			= { 0x134, 2 },
    [NITIO_G23_STATUS1]		= { 0x136, 2 },
    [NITIO_G3_MODE]			= { 0x136, 2 },
    [NITIO_G2_LOADA]		= { 0x138, 4 },
    [NITIO_G23_STATUS2]		= { 0x13a, 2 },
    [NITIO_G2_LOADB]		= { 0x13c, 4 },
    [NITIO_G3_LOADA]		= { 0x140, 4 },
    [NITIO_G3_LOADB]		= { 0x144, 4 },
    [NITIO_G2_INPUT_SEL]		= { 0x148, 2 },
    [NITIO_G3_INPUT_SEL]		= { 0x14a, 2 },
    [NITIO_G2_AUTO_INC]		= { 0x188, 2 },
    [NITIO_G3_AUTO_INC]		= { 0x18a, 2 },
    [NITIO_G23_RESET]		= { 0x190, 2 },
    [NITIO_G2_INT_ENA]		= { 0x192, 2 },
    [NITIO_G3_INT_ENA]		= { 0x196, 2 },
    [NITIO_G2_CNT_MODE]		= { 0x1b0, 2 },
    [NITIO_G3_CNT_MODE]		= { 0x1b2, 2 },
    [NITIO_G2_GATE2]		= { 0x1b4, 2 },
    [NITIO_G3_GATE2]		= { 0x1b6, 2 },
    [NITIO_G2_DMA_CFG]		= { 0x1b8, 2 },
    [NITIO_G2_DMA_STATUS]		= { 0x1b8, 2 },
    [NITIO_G3_DMA_CFG]		= { 0x1ba, 2 },
    [NITIO_G3_DMA_STATUS]		= { 0x1ba, 2 },
    [NI660X_DIO32_INPUT]		= { 0x414, 4 },
    [NI660X_DIO32_OUTPUT]		= { 0x510, 4 },
    [NI660X_CLK_CFG]		= { 0x73c, 4 },
    [NI660X_GLOBAL_INT_STATUS]	= { 0x754, 4 },
    [NI660X_DMA_CFG]		= { 0x76c, 4 },
    [NI660X_GLOBAL_INT_CFG]		= { 0x770, 4 },
    [NI660X_IO_CFG_0_1]		= { 0x77c, 2 },
    [NI660X_IO_CFG_2_3]		= { 0x77e, 2 },
    [NI660X_IO_CFG_4_5]		= { 0x780, 2 },
    [NI660X_IO_CFG_6_7]		= { 0x782, 2 },
    [NI660X_IO_CFG_8_9]		= { 0x784, 2 },
    [NI660X_IO_CFG_10_11]		= { 0x786, 2 },
    [NI660X_IO_CFG_12_13]		= { 0x788, 2 },
    [NI660X_IO_CFG_14_15]		= { 0x78a, 2 },
    [NI660X_IO_CFG_16_17]		= { 0x78c, 2 },
    [NI660X_IO_CFG_18_19]		= { 0x78e, 2 },
    [NI660X_IO_CFG_20_21]		= { 0x790, 2 },
    [NI660X_IO_CFG_22_23]		= { 0x792, 2 },
    [NI660X_IO_CFG_24_25]		= { 0x794, 2 },
    [NI660X_IO_CFG_26_27]		= { 0x796, 2 },
    [NI660X_IO_CFG_28_29]		= { 0x798, 2 },
    [NI660X_IO_CFG_30_31]		= { 0x79a, 2 },
    [NI660X_IO_CFG_32_33]		= { 0x79c, 2 },
    [NI660X_IO_CFG_34_35]		= { 0x79e, 2 },
    [NI660X_IO_CFG_36_37]		= { 0x7a0, 2 },
    [NI660X_IO_CFG_38_39]		= { 0x7a2, 2 }
};

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
    uint32_t global_int_status;
    uint32_t global_int_cfg;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t regs[NI660X_NUM_REGS];

    /* DMA Context */
    uint32_t dma_cfg[NI660X_MAX_CHIPS];

};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool irq_active = false;

    if (s->global_int_cfg & NI660X_GLOBAL_INT_GLOBAL) {
        if (s->global_int_status) {
            irq_active = true;
        }
    }

    pci_set_irq(pdev, irq_active ? 1 : 0);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    unsigned int chip = addr / NI660X_CHIP_OFFSET;
    unsigned int offset = addr % NI660X_CHIP_OFFSET;
    int reg;

    if (chip >= NI660X_MAX_CHIPS) {
        return 0;
    }

    for (reg = 0; reg < NI660X_NUM_REGS; reg++) {
        if (ni_660x_reg_data[reg].offset == offset) {
            if (reg == NI660X_DMA_CFG) {
                val = s->dma_cfg[chip];
            } else if (reg == NI660X_GLOBAL_INT_CFG) {
                val = s->global_int_cfg;
            } else if (reg == NI660X_GLOBAL_INT_STATUS) {
                val = s->global_int_status;
            } else {
                val = s->regs[reg];
            }
            break;
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    unsigned int chip = addr / NI660X_CHIP_OFFSET;
    unsigned int offset = addr % NI660X_CHIP_OFFSET;
    int reg;

    if (chip >= NI660X_MAX_CHIPS) {
        return;
    }

    for (reg = 0; reg < NI660X_NUM_REGS; reg++) {
        if (ni_660x_reg_data[reg].offset == offset) {
            if (reg == NI660X_DMA_CFG) {
                s->dma_cfg[chip] = val;
            } else if (reg == NI660X_GLOBAL_INT_CFG) {
                s->global_int_cfg = val;
                pcibase_update_irq(s);
            } else if (reg == NI660X_GLOBAL_INT_STATUS) {
                s->global_int_status &= ~val;
                pcibase_update_irq(s);
            } else {
                s->regs[reg] = val;
            }
            break;
        }
    }
}

static uint64_t pcibase_mite_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_mite_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
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

static const MemoryRegionOps pcibase_mite_ops = {
    .read = pcibase_mite_read,
    .write = pcibase_mite_write,
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

    memset(s->regs, 0, sizeof(s->regs));
    memset(s->dma_cfg, 0, sizeof(s->dma_cfg));
    s->global_int_status = 0;
    s->global_int_cfg = 0;

    s->regs[NI660X_STC_DIO_CONTROL] = 0;
    for (int i = 0; i < NI660X_MAX_CHIPS; i++) {
        s->dma_cfg[i] = 0;
        for (int chan = 0; chan < NI660X_MAX_DMA_CHANNEL; ++chan) {
            s->dma_cfg[i] |= NI660X_DMA_CFG_SEL_NONE(chan);
        }
    }
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
        if (bi->index == 1) {
            memory_region_init_io(mr, OBJECT(s), &pcibase_mite_ops, s, bi->name, aligned_size);
        }
        else {
            memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x1093 );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x1310 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_MMIO, 0x1000, "ni_660x.mmio"};
    s->bar_info[1] = (BARInfo){1, BAR_TYPE_MMIO, 0x1000, "ni_660x.mite"};
    
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ni_660x_pci",
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
