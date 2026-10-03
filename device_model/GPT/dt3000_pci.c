/*
 * QEMU PCI device model for dt3000 (Comedi driver)
 * Phase 2: Behavioral implementation based strictly on driver-visible behavior.
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

#define TYPE_PCIBASE_DEVICE "dt3000_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define DT3000_VENDOR_ID 0x1116 /* DT (Data Translation) PCI vendor ID */
#define DT3000_DEVICE_ID_3001 0x0022

#define DPR_DAC_BUFFER        (4 * 0x000)
#define DPR_ADC_BUFFER        (4 * 0x800)
#define DPR_COMMAND           (4 * 0xfd3)
#define DPR_SUBSYS            (4 * 0xfd3)
#define DPR_SUBSYS_AI         0
#define DPR_SUBSYS_AO         1
#define DPR_SUBSYS_DIN        2
#define DPR_SUBSYS_DOUT       3
#define DPR_SUBSYS_MEM        4
#define DPR_SUBSYS_CT         5
#define DPR_ENCODE            (4 * 0xfd4)
#define DPR_PARAMS(x)         (4 * (0xfd5 + (x)))
#define DPR_TICK_REG_LO       (4 * 0xff5)
#define DPR_TICK_REG_HI       (4 * 0xff6)
#define DPR_DA_BUF_FRONT      (4 * 0xff7)
#define DPR_DA_BUF_REAR       (4 * 0xff8)
#define DPR_AD_BUF_FRONT      (4 * 0xff9)
#define DPR_AD_BUF_REAR       (4 * 0xffa)
#define DPR_INT_MASK          (4 * 0xffb)
#define DPR_INTR_FLAG         (4 * 0xffc)
#define DPR_INTR_CMDONE       (1U << 7)
#define DPR_INTR_CTDONE       (1U << 6)
#define DPR_INTR_DAHWERR      (1U << 5)
#define DPR_INTR_DASWERR      (1U << 4)
#define DPR_INTR_DAEMPTY      (1U << 3)
#define DPR_INTR_ADHWERR      (1U << 2)
#define DPR_INTR_ADSWERR      (1U << 1)
#define DPR_INTR_ADFULL       (1U << 0)
#define DPR_RESPONSE_MBX      (4 * 0xffe)
#define DPR_CMD_MBX           (4 * 0xfff)
#define DPR_CMD_COMPLETION(x) ((x) << 8)
#define DPR_CMD_NOTPROCESSED  DPR_CMD_COMPLETION(0x00)
#define DPR_CMD_NOERROR       DPR_CMD_COMPLETION(0x55)
#define DPR_CMD_ERROR         DPR_CMD_COMPLETION(0xaa)
#define DPR_CMD_NOTSUPPORTED  DPR_CMD_COMPLETION(0xff)
#define DPR_CMD_COMPLETION_MASK DPR_CMD_COMPLETION(0xff)
#define DPR_CMD(x)            ((x) << 0)
#define DPR_CMD_GETBRDINFO    DPR_CMD(0)
#define DPR_CMD_CONFIG        DPR_CMD(1)
#define DPR_CMD_GETCONFIG     DPR_CMD(2)
#define DPR_CMD_START         DPR_CMD(3)
#define DPR_CMD_STOP          DPR_CMD(4)
#define DPR_CMD_READSINGLE    DPR_CMD(5)
#define DPR_CMD_WRITESINGLE   DPR_CMD(6)
#define DPR_CMD_CALCCLOCK     DPR_CMD(7)
#define DPR_CMD_READEVENTS    DPR_CMD(8)
#define DPR_CMD_WRITECTCTRL   DPR_CMD(16)
#define DPR_CMD_READCTCTRL    DPR_CMD(17)
#define DPR_CMD_WRITECT       DPR_CMD(18)
#define DPR_CMD_READCT        DPR_CMD(19)
#define DPR_CMD_WRITEDATA     DPR_CMD(32)
#define DPR_CMD_READDATA      DPR_CMD(33)
#define DPR_CMD_WRITEIO       DPR_CMD(34)
#define DPR_CMD_READIO        DPR_CMD(35)
#define DPR_CMD_WRITECODE     DPR_CMD(36)
#define DPR_CMD_READCODE      DPR_CMD(37)
#define DPR_CMD_EXECUTE       DPR_CMD(38)
#define DPR_CMD_HALT          DPR_CMD(48)
#define DPR_CMD_MASK          DPR_CMD(0xff)
#define DPR_PARAM5_AD_TRIG(x)         (((x) & 0x7) << 2)
#define DPR_PARAM5_AD_TRIG_INT        DPR_PARAM5_AD_TRIG(0)
#define DPR_PARAM5_AD_TRIG_EXT        DPR_PARAM5_AD_TRIG(1)
#define DPR_PARAM5_AD_TRIG_INT_RETRIG DPR_PARAM5_AD_TRIG(2)
#define DPR_PARAM5_AD_TRIG_EXT_RETRIG DPR_PARAM5_AD_TRIG(3)
#define DPR_PARAM5_AD_TRIG_INT_RETRIG2 DPR_PARAM5_AD_TRIG(4)
#define DPR_PARAM6_AD_DIFF            (1U << 0)
#define DPR_AI_FIFO_DEPTH             2003
#define DPR_AO_FIFO_DEPTH             2048
#define DPR_EXTERNAL_CLOCK            1
#define DPR_RISING_EDGE               2
#define DPR_TMODE_MASK                0x1c
#define DPR_CMD_TIMEOUT               100

/* We only model the basic behavior required by the driver:
 * - 16-bit register accesses via dev->mmio (writel/readw in driver)
 * - A simple command mailbox completion protocol for dt3k_send_cmd()
 * - Basic ADC FIFO pointers and data so that dt3k_ai_empty_fifo() runs
 * - Simple interrupt flag and mask behavior so the ISR gets called
 *
 * No DMA engines, complex timing, or counter/timer behavior are implemented,
 * as the driver snippet does not expose their detailed hardware semantics.
 */

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

    /* Simple register space shadow (32-bit words) for BAR0 MMIO */
    uint32_t regs[0x10000];

    /* Cached interrupt mask and flags */
    uint16_t int_mask;     /* shadow of DPR_INT_MASK (low 16 bits) */
    uint16_t int_flags;    /* shadow of DPR_INTR_FLAG (low 16 bits) */

    /* ADC FIFO emulation */
    uint16_t adc_fifo[DPR_AI_FIFO_DEPTH];
    uint16_t ad_buf_front;   /* DPR_AD_BUF_FRONT */
    uint16_t ad_buf_rear;    /* DPR_AD_BUF_REAR */

    /* Subsystem register shadow */
    uint16_t subsys;       /* DPR_SUBSYS */

    /* Command mailbox shadow */
    uint16_t cmd_mbx;      /* DPR_CMD_MBX value last written */

    /* Simple AO readback for dt3k_writesingle() AO usage */
    uint16_t ao_values[2];
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Interrupt is asserted if any enabled flag is set. */
    uint16_t pending = s->int_flags & s->int_mask;

    if (pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO/PIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val32 = 0;

    /* The driver uses readw()/writew() (16-bit), via dev->mmio + offset. */

    if (size == 1) {
        uint32_t word = s->regs[addr >> 2];
        unsigned shift = (addr & 3) * 8;
        return (word >> shift) & 0xff;
    } else if (size == 2) {
        uint32_t word = s->regs[addr >> 2];
        if (addr & 0x2) {
            val32 = (word >> 16) & 0xffff;
        } else {
            val32 = word & 0xffff;
        }

        /* Handle special 16-bit-visible registers explicitly */
        switch (addr) {
        case DPR_SUBSYS:
            val32 = s->subsys;
            break;
        case DPR_AD_BUF_FRONT:
            val32 = s->ad_buf_front;
            break;
        case DPR_AD_BUF_REAR:
            val32 = s->ad_buf_rear;
            break;
        case DPR_INT_MASK:
            val32 = s->int_mask;
            break;
        case DPR_INTR_FLAG:
            val32 = s->int_flags;
            break;
        case DPR_CMD_MBX:
            val32 = s->cmd_mbx;
            break;
        default:
            break;
        }

        return val32 & 0xffffu;
    } else if (size == 4) {
        val32 = s->regs[addr >> 2];
        return val32;
    }

    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size == 1) {
        uint32_t word = s->regs[addr >> 2];
        unsigned shift = (addr & 3) * 8;
        uint32_t mask = 0xffu << shift;
        word = (word & ~mask) | ((uint32_t)(val & 0xffu) << shift);
        s->regs[addr >> 2] = word;
        return;
    } else if (size == 2) {
        uint16_t v = (uint16_t)val;
        uint32_t word = s->regs[addr >> 2];
        if (addr & 0x2) {
            word = (word & 0x0000ffffu) | ((uint32_t)v << 16);
        } else {
            word = (word & 0xffff0000u) | (uint32_t)v;
        }
        s->regs[addr >> 2] = word;

        /* Handle known registers */
        switch (addr) {
        case DPR_SUBSYS:
            s->subsys = v;
            break;
        case DPR_AD_BUF_FRONT:
            s->ad_buf_front = v % DPR_AI_FIFO_DEPTH;
            break;
        case DPR_AD_BUF_REAR:
            s->ad_buf_rear = v % DPR_AI_FIFO_DEPTH;
            break;
        case DPR_INT_MASK:
            s->int_mask = v;
            pcibase_update_irq(s);
            break;
        case DPR_INTR_FLAG:
            /* In real hardware this is likely W1C; driver never writes it,
             * so we avoid manipulating it here to stay minimal.
             */
            s->int_flags = v;
            pcibase_update_irq(s);
            break;
        case DPR_CMD_MBX: {
            /* dt3k_send_cmd() writes command in low 8 bits, then polls
             * DPR_CMD_MBX masked with DPR_CMD_COMPLETION_MASK until it is
             * != DPR_CMD_NOTPROCESSED and equals DPR_CMD_NOERROR for success.
             *
             * We emulate immediate, always-successful completion for any
             * command that the driver actually uses.
             */
            uint16_t cmd = v & DPR_CMD_MASK;
            uint16_t completion = DPR_CMD_NOERROR; /* 0x5500 */

            /* Simple data path behavior:
             * - READSINGLE: result already preloaded in DPR_PARAMS(2) by our
             *   internal state; we only need to set completion.
             * - WRITESINGLE: we accept data in DPR_PARAMS(2) and may update
             *   AO shadow state.
             * - CONFIG/START/STOP etc.: we do nothing aside from completion.
             */

            if (cmd == DPR_CMD_WRITESINGLE) {
                /* For AO subsystem, track last AO data by channel. The
                 * driver calls dt3k_writesingle() with:
                 *  DPR_SUBSYS_AO in DPR_SUBSYS
                 *  channel in DPR_PARAMS(0)
                 *  data in DPR_PARAMS(2)
                 */
                if (s->subsys == DPR_SUBSYS_AO) {
                    /* Offsets are in bytes; each DPR_PARAMS(x) is 16-bit. */
                    uint16_t chan;
                    uint16_t data;

                    chan = (uint16_t)(s->regs[DPR_PARAMS(0) >> 2] & 0xffffu);
                    data = (uint16_t)(s->regs[DPR_PARAMS(2) >> 2] & 0xffffu);
                    if (chan < 2) {
                        s->ao_values[chan] = data;
                    }
                }
            } else if (cmd == DPR_CMD_READSINGLE) {
                /* dt3k_readsingle() writes subsys and chan/gain, then
                 * dt3k_send_cmd(), and finally reads DPR_PARAMS(2).
                 * We need to place some deterministic value into
                 * DPR_PARAMS(2). Since there is no documented conversion,
                 * we simply return 0 to keep the driver satisfied.
                 */
                s->regs[DPR_PARAMS(2) >> 2] &= 0xffff0000u;
            } else if (cmd == DPR_CMD_READCODE) {
                /* dt3k_mem_insn_read() reads board memory via DPR_PARAMS(2).
                 * We just return 0 for all addresses, as no specific content
                 * is required for probing/initialization.
                 */
                s->regs[DPR_PARAMS(2) >> 2] &= 0xffff0000u;
            } else if (cmd == DPR_CMD_CONFIG) {
                /* For AI CONFIG, dt3k_ai_cmd() loads parameters and then
                 * enables interrupts via DPR_INT_MASK. No extra behavior
                 * needed for basic operation.
                 */
            } else if (cmd == DPR_CMD_START) {
                /* AI acquisition start. We'll emulate the FIFO gradually
                 * filling by manipulating ADC buffer pointers and optionally
                 * raising AD_FULL interrupts. For now, no immediate effect.
                 */
            } else if (cmd == DPR_CMD_STOP) {
                /* Stop acquisition: we clear AD-related flags. */
                s->int_flags &= ~(DPR_INTR_ADFULL | DPR_INTR_ADSWERR | DPR_INTR_ADHWERR);
                pcibase_update_irq(s);
            }

            /* Write combined command+completion back to mailbox shadow. */
            s->cmd_mbx = (v & DPR_CMD_MASK) | completion;
            s->regs[addr >> 2] = (s->regs[addr >> 2] & 0xffff0000u) | s->cmd_mbx;

            /* Optionally, we could raise CMDONE flag, but the driver does not
             * look at it; dt3k_send_cmd() only checks mailbox completion.
             */

            break;
        }
        default:
            break;
        }

        return;
    } else if (size == 4) {
        s->regs[addr >> 2] = (uint32_t)val;
        return;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* The dt3000 driver only uses MMIO (pci_ioremap_bar(0)), not PIO. */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO usage in driver. */
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

    memset(s->regs, 0, sizeof(s->regs));

    s->int_mask = 0;
    s->int_flags = 0;
    s->subsys = 0;
    s->cmd_mbx = DPR_CMD_NOTPROCESSED;

    memset(s->adc_fifo, 0, sizeof(s->adc_fifo));
    s->ad_buf_front = 0;
    s->ad_buf_rear = 0;

    s->ao_values[0] = 0;
    s->ao_values[1] = 0;

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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  DT3000_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DT3000_DEVICE_ID_3001);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* The Linux driver maps BAR0 with pci_ioremap_bar(pcidev, 0).
     * We expose a 64 KiB MMIO BAR0 which comfortably covers all
     * DPR_* offsets seen in the driver (up to 4 * 0xfff).
     */
    s->num_bars = 1;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }

    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x10000; /* 64 KiB */
    s->bar_info[0].name = "dt3000-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X usage in driver; leave disabled. */

    /* Initialize internal state */
    memset(s->regs, 0, sizeof(s->regs));
    s->int_mask = 0;
    s->int_flags = 0;
    s->subsys = 0;
    s->cmd_mbx = DPR_CMD_NOTPROCESSED;

    memset(s->adc_fifo, 0, sizeof(s->adc_fifo));
    s->ad_buf_front = 0;
    s->ad_buf_rear = 0;

    s->ao_values[0] = 0;
    s->ao_values[1] = 0;

    pcibase_update_irq(s);
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
    .name = "dt3000_pci",
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

