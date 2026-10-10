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

#define TYPE_PCIBASE_DEVICE "netup_unidvb_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor and Device IDs from driver's first pci_device_id entry */
#define PCI_VENDOR_ID_NETUP  0x1b55
#define PCI_DEVICE_ID_UNIDVB 0x18f6

/* PCI Class: multimedia video controller, base class 0x04, sub-class 0x00, prog-if 0x00 */
#define PCI_CLASS_NETUP_BASE  0x04

/* BAR sizes (inferred from register layout) */
#define BAR0_SIZE 0x5000
#define BAR1_SIZE 0x1000

/* Register offsets extracted from driver headers */
#define AVL_PCIE_IENR       0x50
#define AVL_PCIE_ISR        0x40
#define AVL_IRQ_ENABLE      0x80    /* value written to IENR */
#define AVL_IRQ_ASSERTED    0x80    /* bit in ISR */
#define GPIO_REG_IO         0x4880
#define GPIO_REG_IO_TOGGLE  0x4882
#define GPIO_REG_IO_SET     0x4884
#define GPIO_REG_IO_CLEAR   0x4886
#define GPIO_FEA_RESET      (1 << 0)
#define GPIO_FEB_RESET      (1 << 1)
#define GPIO_RFA_CTL        (1 << 2)
#define GPIO_RFB_CTL        (1 << 3)
#define GPIO_FEA_TU_RESET   (1 << 4)
#define GPIO_FEB_TU_RESET   (1 << 5)
#define NETUP_DMA0_ADDR     0x4900
#define NETUP_DMA1_ADDR     0x4940
#define NETUP_DMA_BLOCKS_COUNT  8
#define NETUP_DMA_PACKETS_COUNT 128
#define BIT_DMA_RUN         1
#define BIT_DMA_ERROR       2
#define BIT_DMA_IRQ         0x200
#define REG_IMASK_SET       0x4894
#define REG_IMASK_CLEAR     0x4896
#define NETUP_UNIDVB_IRQ_DMA2   (1 << 9)
#define NETUP_UNIDVB_IRQ_DMA1   (1 << 8)
#define REG_ISR             0x4890
#define NETUP_UNIDVB_IRQ_I2C1   (1 << 2)
#define NETUP_UNIDVB_IRQ_I2C0   (1 << 1)
#define NETUP_UNIDVB_IRQ_SPI    (1 << 0)
#define NETUP_UNIDVB_IRQ_CI     (1 << 10)
#define NETUP_UNIDVB_NAME       "netup_unidvb"
#define NETUP_PCI_DEV_REVISION  0x2

/* Hardware revision enum from driver */
enum netup_hw_rev {
    NETUP_HW_REV_1_3 = 0x18F6,
    NETUP_HW_REV_1_4 = 0x18F7
};

enum netup_i2c_state {
    STATE_DONE,
    STATE_WAIT,
    STATE_WANT_READ,
    STATE_WANT_WRITE,
    STATE_ERROR
};

/* DMA control/status register layout from struct netup_dma_regs */
struct netup_dma_regs {
    uint32_t  ctrlstat_set;
    uint32_t  ctrlstat_clear;
    uint32_t  start_addr_lo;
    uint32_t  start_addr_hi;
    uint32_t  size;
    uint32_t  timeout;
    uint32_t  curr_addr_lo;
    uint32_t  curr_addr_hi;
    uint32_t  stat_pkt_received;
    uint32_t  stat_pkt_accepted;
    uint32_t  stat_pkt_overruns;
    uint32_t  stat_pkt_underruns;
    uint32_t  stat_fifo_overruns;
};

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
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

    uint32_t intr_status; /* IRQ status bits (AVL_PCIE_ISR, REG_ISR) */
    uint32_t intr_mask;   /* IRQ mask bits (AVL_PCIE_IENR, REG_IMASK) */

    /* Shadow of MMIO registers */
    uint8_t mmio[BAR0_SIZE];

    /* DMA engine register shadows (two channels) */
    struct netup_dma_regs dma_regs[2];

    /* Internal state for DMA engines */
    uint32_t dma_ctrl[2];       /* run/error/irq flags */
    QEMUTimer *dma_timer[2];

    /* Registers */
    uint32_t avl_ienr;          /* PCIe interrupt enable (0x50) */
    uint16_t reg_imask;         /* Peripheral interrupt mask (0x4894) */
    uint16_t irq_raw;           /* Raw peripheral IRQ requests before mask */
    uint8_t  gpio_io;           /* GPIO output register (0x4880) */
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint16_t reg_isr = s->irq_raw & s->reg_imask;
    uint32_t avl_isr = reg_isr ? AVL_IRQ_ASSERTED : 0;
    int level = 0;

    if ((s->avl_ienr & AVL_IRQ_ASSERTED) && avl_isr) {
        level = 1;
    }
    pci_set_irq(pdev, level);
}

static void pcibase_dma_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    int i;
    bool any_pending = false;

    for (i = 0; i < 2; i++) {
        if (s->dma_ctrl[i] & BIT_DMA_RUN) {
            /* Simulate DMA progress: advance current address by one block */
            uint32_t size_val = s->dma_regs[i].size;
            uint32_t block_count = (size_val >> 24) & 0xFF;
            uint32_t packet_count = (size_val >> 8) & 0xFF;
            uint32_t packet_size = size_val & 0xFF;
            uint32_t block_size = packet_count * packet_size;
            uint32_t ring_buffer_size = block_count * block_size;

            uint32_t curr = s->dma_regs[i].curr_addr_lo;
            uint32_t start = s->dma_regs[i].start_addr_lo & 0x3FFFFFFF;
            uint32_t next = curr + block_size;
            if (next >= start + ring_buffer_size) {
                next = start;
            }
            s->dma_regs[i].curr_addr_lo = next;

            /* Set DMA IRQ and raw interrupt */
            s->dma_ctrl[i] |= BIT_DMA_IRQ;
            if (i == 0) {
                s->irq_raw |= NETUP_UNIDVB_IRQ_DMA1;
            } else {
                s->irq_raw |= NETUP_UNIDVB_IRQ_DMA2;
            }
            any_pending = true;
        }
    }

    if (any_pending) {
        pcibase_update_irq(s);
    }

    /* Re-arm timer for next check */
    for (i = 0; i < 2; i++) {
        if (s->dma_ctrl[i] & BIT_DMA_RUN) {
            timer_mod(s->dma_timer[i],
                      qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);
        }
    }
}

static void pcibase_dma_start(PCIBaseState *s, int ch)
{
    if (!(s->dma_ctrl[ch] & BIT_DMA_RUN)) {
        s->dma_ctrl[ch] |= BIT_DMA_RUN;
        /* Reset current address to start address if not already set? */
        /* For simplicity, just start the timer */
        timer_mod(s->dma_timer[ch],
                  qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 100);
    }
}

static void pcibase_dma_stop(PCIBaseState *s, int ch)
{
    if (s->dma_ctrl[ch] & BIT_DMA_RUN) {
        s->dma_ctrl[ch] &= ~BIT_DMA_RUN;
        timer_del(s->dma_timer[ch]);
    }
}

static void pcibase_dma_clear_irq(PCIBaseState *s, int ch)
{
    if (s->dma_ctrl[ch] & BIT_DMA_IRQ) {
        s->dma_ctrl[ch] &= ~BIT_DMA_IRQ;
        if (ch == 0) {
            s->irq_raw &= ~NETUP_UNIDVB_IRQ_DMA1;
        } else {
            s->irq_raw &= ~NETUP_UNIDVB_IRQ_DMA2;
        }
        pcibase_update_irq(s);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds read at 0x%" HWADDR_PRIx "\n", __func__, addr);
        return 0;
    }

    switch (addr) {
    case AVL_PCIE_IENR:
        if (size == 4) val = s->avl_ienr;
        break;
    case AVL_PCIE_ISR:
        if (size == 4) {
            uint16_t reg_isr = s->irq_raw & s->reg_imask;
            val = reg_isr ? AVL_IRQ_ASSERTED : 0;
        }
        break;
    case GPIO_REG_IO:
        if (size == 1) val = s->gpio_io;
        break;
    case REG_ISR:
        if (size == 2) {
            val = s->irq_raw & s->reg_imask;
        }
        break;
    default:
        /* DMA0 region */
        if (addr >= NETUP_DMA0_ADDR && addr < NETUP_DMA0_ADDR + sizeof(struct netup_dma_regs)) {
            int ch = 0;
            hwaddr offset = addr - NETUP_DMA0_ADDR;
            struct netup_dma_regs *regs = &s->dma_regs[ch];
            switch (offset) {
            case 0: /* ctrlstat_set - write-only, return 0 */
                val = 0;
                break;
            case 4: /* ctrlstat_clear - write-only */
                val = 0;
                break;
            case 8: /* start_addr_lo */
                if (size == 4) val = regs->start_addr_lo;
                break;
            case 0xC: /* start_addr_hi */
                if (size == 4) val = regs->start_addr_hi;
                break;
            case 0x10: /* size */
                if (size == 4) val = regs->size;
                break;
            case 0x14: /* timeout */
                if (size == 4) val = regs->timeout;
                break;
            case 0x18: /* curr_addr_lo */
                if (size == 4) val = regs->curr_addr_lo;
                break;
            case 0x1C: /* curr_addr_hi */
                if (size == 4) val = regs->curr_addr_hi;
                break;
            case 0x20: /* stat_pkt_received */
                if (size == 4) val = regs->stat_pkt_received;
                break;
            case 0x24: /* stat_pkt_accepted */
                if (size == 4) val = regs->stat_pkt_accepted;
                break;
            case 0x28: /* stat_pkt_overruns */
                if (size == 4) val = regs->stat_pkt_overruns;
                break;
            case 0x2C: /* stat_pkt_underruns */
                if (size == 4) val = regs->stat_pkt_underruns;
                break;
            case 0x30: /* stat_fifo_overruns */
                if (size == 4) val = regs->stat_fifo_overruns;
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR, "%s: unhandled DMA0 read at 0x%" HWADDR_PRIx "\n", __func__, addr);
                val = 0;
            }
        }
        /* DMA1 region */
        else if (addr >= NETUP_DMA1_ADDR && addr < NETUP_DMA1_ADDR + sizeof(struct netup_dma_regs)) {
            int ch = 1;
            hwaddr offset = addr - NETUP_DMA1_ADDR;
            struct netup_dma_regs *regs = &s->dma_regs[ch];
            switch (offset) {
            case 0:
                val = 0;
                break;
            case 4:
                val = 0;
                break;
            case 8:
                if (size == 4) val = regs->start_addr_lo;
                break;
            case 0xC:
                if (size == 4) val = regs->start_addr_hi;
                break;
            case 0x10:
                if (size == 4) val = regs->size;
                break;
            case 0x14:
                if (size == 4) val = regs->timeout;
                break;
            case 0x18:
                if (size == 4) val = regs->curr_addr_lo;
                break;
            case 0x1C:
                if (size == 4) val = regs->curr_addr_hi;
                break;
            case 0x20:
                if (size == 4) val = regs->stat_pkt_received;
                break;
            case 0x24:
                if (size == 4) val = regs->stat_pkt_accepted;
                break;
            case 0x28:
                if (size == 4) val = regs->stat_pkt_overruns;
                break;
            case 0x2C:
                if (size == 4) val = regs->stat_pkt_underruns;
                break;
            case 0x30:
                if (size == 4) val = regs->stat_fifo_overruns;
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR, "%s: unhandled DMA1 read at 0x%" HWADDR_PRIx "\n", __func__, addr);
                val = 0;
            }
        }
        else {
            qemu_log_mask(LOG_UNIMP, "%s: unimplemented read at 0x%" HWADDR_PRIx " size %u\n", __func__, addr, size);
            val = 0;
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds write at 0x%" HWADDR_PRIx "\n", __func__, addr);
        return;
    }

    switch (addr) {
    case AVL_PCIE_IENR:
        if (size == 4) {
            s->avl_ienr = val;
            pcibase_update_irq(s);
        }
        break;
    case GPIO_REG_IO:
        if (size == 1) s->gpio_io = val;
        break;
    case REG_IMASK_SET:
        if (size == 2) {
            s->reg_imask |= val;
            pcibase_update_irq(s);
        }
        break;
    case REG_IMASK_CLEAR:
        if (size == 2) {
            s->reg_imask &= ~val;
            pcibase_update_irq(s);
        }
        break;
    default:
        if (addr >= NETUP_DMA0_ADDR && addr < NETUP_DMA0_ADDR + sizeof(struct netup_dma_regs)) {
            int ch = 0;
            hwaddr offset = addr - NETUP_DMA0_ADDR;
            switch (offset) {
            case 0: /* ctrlstat_set */
                if (size == 4) {
                    s->dma_ctrl[ch] |= val;
                    if (val & BIT_DMA_RUN) {
                        pcibase_dma_start(s, ch);
                    }
                }
                break;
            case 4: /* ctrlstat_clear */
                if (size == 4) {
                    s->dma_ctrl[ch] &= ~val;
                    if (val & BIT_DMA_RUN) {
                        pcibase_dma_stop(s, ch);
                    }
                    if (val & BIT_DMA_IRQ) {
                        pcibase_dma_clear_irq(s, ch);
                    }
                }
                break;
            case 8: /* start_addr_lo */
                if (size == 4) s->dma_regs[ch].start_addr_lo = val & 0x3FFFFFFF;
                break;
            case 0xC: /* start_addr_hi */
                if (size == 4) s->dma_regs[ch].start_addr_hi = val;
                break;
            case 0x10: /* size */
                if (size == 4) s->dma_regs[ch].size = val;
                break;
            case 0x14: /* timeout */
                if (size == 4) s->dma_regs[ch].timeout = val;
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR, "%s: unhandled DMA0 write at 0x%" HWADDR_PRIx "\n", __func__, addr);
            }
        }
        else if (addr >= NETUP_DMA1_ADDR && addr < NETUP_DMA1_ADDR + sizeof(struct netup_dma_regs)) {
            int ch = 1;
            hwaddr offset = addr - NETUP_DMA1_ADDR;
            switch (offset) {
            case 0:
                if (size == 4) {
                    s->dma_ctrl[ch] |= val;
                    if (val & BIT_DMA_RUN) {
                        pcibase_dma_start(s, ch);
                    }
                }
                break;
            case 4:
                if (size == 4) {
                    s->dma_ctrl[ch] &= ~val;
                    if (val & BIT_DMA_RUN) {
                        pcibase_dma_stop(s, ch);
                    }
                    if (val & BIT_DMA_IRQ) {
                        pcibase_dma_clear_irq(s, ch);
                    }
                }
                break;
            case 8:
                if (size == 4) s->dma_regs[ch].start_addr_lo = val & 0x3FFFFFFF;
                break;
            case 0xC:
                if (size == 4) s->dma_regs[ch].start_addr_hi = val;
                break;
            case 0x10:
                if (size == 4) s->dma_regs[ch].size = val;
                break;
            case 0x14:
                if (size == 4) s->dma_regs[ch].timeout = val;
                break;
            default:
                qemu_log_mask(LOG_GUEST_ERROR, "%s: unhandled DMA1 write at 0x%" HWADDR_PRIx "\n", __func__, addr);
            }
        }
        else {
            qemu_log_mask(LOG_UNIMP, "%s: unimplemented write at 0x%" HWADDR_PRIx " size %u val 0x%" PRIx64 "\n", __func__, addr, size, val);
        }
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset hardware registers to power-on defaults */
    s->avl_ienr = 0;
    s->reg_imask = 0;
    s->irq_raw = 0;
    s->gpio_io = 0;
    memset(&s->dma_regs, 0, sizeof(s->dma_regs));
    memset(&s->dma_ctrl, 0, sizeof(s->dma_ctrl));
    for (int i = 0; i < 2; i++) {
        timer_del(s->dma_timer[i]);
    }
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
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_NETUP );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_UNIDVB );
    /* Set class code: base 0x04 (multimedia), sub-class 0x00, programming interface 0x00 */
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, (PCI_CLASS_NETUP_BASE << 8) | 0x00);
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x00);
    pci_set_byte(pci_conf + PCI_REVISION_ID, NETUP_PCI_DEV_REVISION);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR initialization */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "bar0";
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = BAR1_SIZE;
    s->bar_info[1].name = "bar1";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* DMA timers */
    for (int i = 0; i < 2; i++) {
        s->dma_timer[i] = timer_new_ms(QEMU_CLOCK_VIRTUAL,
                                       (QEMUTimerCB *)pcibase_dma_timer_cb, s);
    }

    /* Initial state */
    s->avl_ienr = 0;
    s->reg_imask = 0;
    s->irq_raw = 0;
    s->gpio_io = 0;
    memset(&s->dma_regs, 0, sizeof(s->dma_regs));
    memset(&s->dma_ctrl, 0, sizeof(s->dma_ctrl));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    for (int i = 0; i < 2; i++) {
        timer_del(s->dma_timer[i]);
        timer_free(s->dma_timer[i]);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "netup_unidvb_pci",
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