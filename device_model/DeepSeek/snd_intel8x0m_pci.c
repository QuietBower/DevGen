/*
 * QEMU PCI device model for Intel ICH MODEM (snd_intel8x0m)
 * Generated from driver: sound/pci/intel8x0m.c
 * Target: QEMU 8.2.10
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

#define TYPE_PCIBASE_DEVICE "snd_intel8x0m_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs */
#define VENDOR_ID 0x8086
#define DEVICE_ID 0x2416
#define CLASS_ID  0x0703

/* Register offsets (from BM BAR) */
#define ICH_REG_GLOB_CNT   0x3c
#define ICH_REG_GLOB_STA   0x40
#define ICH_REG_ACC_SEMA   0x44

/* Bus master channel register offsets (from channel base: 0x00 or 0x10) */
#define ICH_REG_BDBAR   0x00
#define ICH_REG_CIV     0x04
#define ICH_REG_LVI     0x05
#define ICH_REG_SR      0x06
#define ICH_REG_PICB    0x08
#define ICH_REG_PIV     0x0a
#define ICH_REG_CR      0x0b

/* Bit definitions from the driver */
#define ICH_PCR		0x00000100
#define ICH_SCR		0x00000200
#define ICH_TCR		0x10000000
#define ICH_RCS		0x00008000
#define ICH_MIINT		0x00000002
#define ICH_MOINT		0x00000004
#define ICH_AC97COLD		0x00000002
#define ICH_AC97WARM		0x00000004
#define ICH_ACLINK		0x00000008
#define ICH_CAS		0x01
#define ICH_RESETREGS			0x02
#define ICH_STARTBM			0x01
#define ICH_IOCE			0x10
#define ICH_FIFOE			0x10
#define ICH_BCIS			0x08
#define ICH_LVBCI			0x04
#define ICH_DCH				0x01
#define ICH_SRI		0x00000800
#define ICH_PRI		0x00000400
#define ICH_TRI		0x20000000
#define ICH_GSCI		0x00000001
#define ICH_REG_LVI_MASK		0x1f

/* AC97 register space size in 16-bit words */
#define AC97_NUM_REGS 0x80

/* Channel indexes */
#define CHAN_PI 0
#define CHAN_PO 1

/* Register access helpers */
#define BM_GLOB_OFFSET 0x3c

/* Channel state per direction */
typedef struct {
    uint32_t bdbar;
    uint8_t civ;
    uint8_t lvi;
    uint8_t sr;
    uint16_t picb;
    uint8_t piv;
    uint8_t cr;
    bool running;
} Channel;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resources */
    MemoryRegion bar_regions[6];
    MemoryRegion ac97_mmio;
    MemoryRegion bm_mmio;

    /* Capability and interrupt state */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;   /* unused */
    uint32_t intr_mask;      /* unused */

    /* Global register shadows */
    uint32_t glob_cnt;
    uint32_t glob_sta;
    uint8_t acc_sema;

    /* Channel state */
    Channel ch[2];

    /* AC97 register file */
    uint16_t ac97_regs[AC97_NUM_REGS];
    uint16_t ac97_read_index;
    uint16_t ac97_write_index;

    /* Timer to emulate AC97 reset completion */
    QEMUTimer *reset_timer;

    /* Power management */
    uint8_t pm_state;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    /* Not used; driver handles interrupts via GLOB_STA status, but
     * no actual events are generated, so no IRQ raising needed. */
}

static void pcibase_reset_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;

    /* Clear warm/cold reset bits in GLOB_CNT */
    s->glob_cnt &= ~(ICH_AC97COLD | ICH_AC97WARM);

    /* Indicate primary codec ready */
    s->glob_sta |= ICH_PCR;

    /* Initialize AC97 codec to a valid state after reset */
    s->ac97_regs[0x00] = 0xC000; /* Codec ready (bit15), primary codec ready (bit14) */
    s->ac97_regs[0x7C] = 0x8086; /* Vendor ID1 */
    s->ac97_regs[0x7E] = 0x2416; /* Vendor ID2 (reuse PCI device ID) */
    s->ac97_regs[0x28] = 0x0200; /* Extended Audio ID: modem (bit 9) */
}

static uint64_t pcibase_ac97_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 2) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned AC97 read (size %u) at 0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        return 0;
    }

    switch (addr) {
    case 0x02: /* Read data register */
        if (s->ac97_read_index < AC97_NUM_REGS) {
            uint16_t val = s->ac97_regs[s->ac97_read_index];
            /* Bit 15 set indicates valid data */
            return val | 0x8000;
        }
        break;
    case 0x06: /* Write data register (reading returns 0) */
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unmapped AC97 read at 0x%" HWADDR_PRIx "\n", __func__, addr);
        return 0;
    }
    return 0;
}

static void pcibase_ac97_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 2) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned AC97 write (size %u) at 0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        return;
    }

    switch (addr) {
    case 0x00: /* Read index register */
        s->ac97_read_index = (uint16_t)val;
        break;
    case 0x04: /* Write index register */
        s->ac97_write_index = (uint16_t)val;
        break;
    case 0x02: /* Read data register (ignore writes) */
        break;
    case 0x06: /* Write data register */
        if (s->ac97_write_index < AC97_NUM_REGS) {
            s->ac97_regs[s->ac97_write_index] = (uint16_t)val;
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unmapped AC97 write at 0x%" HWADDR_PRIx "\n", __func__, addr);
    }
}

static const MemoryRegionOps ac97_ops = {
    .read = pcibase_ac97_read,
    .write = pcibase_ac97_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 2, .max_access_size = 2 },
    .impl  = { .min_access_size = 2, .max_access_size = 2 },
};

static uint64_t pcibase_bm_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    int ch = (addr < 0x10) ? 0 : ((addr < 0x20) ? 1 : -1);
    unsigned reg_offset = (ch >= 0) ? (addr - ch * 0x10) : 0;

    switch (addr) {
    case ICH_REG_GLOB_CNT:
        val = s->glob_cnt;
        break;
    case ICH_REG_GLOB_STA:
        val = s->glob_sta;
        break;
    case ICH_REG_ACC_SEMA:
        val = s->acc_sema;
        break;
    default:
        if (ch < 0) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unmapped BM read at 0x%" HWADDR_PRIx "\n", __func__, addr);
            break;
        }
        switch (reg_offset) {
        case ICH_REG_BDBAR:
            if (size >= 4) val = s->ch[ch].bdbar;
            break;
        case ICH_REG_CIV:
            val = s->ch[ch].civ;
            break;
        case ICH_REG_LVI:
            val = s->ch[ch].lvi;
            break;
        case ICH_REG_SR:
            val = s->ch[ch].sr;
            break;
        case ICH_REG_PICB:
            if (size >= 2) val = s->ch[ch].picb;
            break;
        case ICH_REG_PIV:
            val = s->ch[ch].piv;
            break;
        case ICH_REG_CR:
            val = s->ch[ch].cr;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unmapped channel BM read at 0x%" HWADDR_PRIx "\n", __func__, addr);
            break;
        }
        break;
    }

    return val;
}

static void pcibase_bm_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    int ch = (addr < 0x10) ? 0 : ((addr < 0x20) ? 1 : -1);
    unsigned reg_offset = (ch >= 0) ? (addr - ch * 0x10) : 0;

    switch (addr) {
    case ICH_REG_GLOB_CNT:
        s->glob_cnt = (uint32_t)val;
        if (val & (ICH_AC97COLD | ICH_AC97WARM)) {
            /* Schedule reset completion after a short delay */
            timer_mod(s->reset_timer, qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) + 1);
        }
        break;
    case ICH_REG_GLOB_STA:
        /* W1C: clear bits written as 1 */
        s->glob_sta &= ~(uint32_t)val;
        break;
    case ICH_REG_ACC_SEMA:
        /* Codec access semaphore: driver writes 1 to acquire, 0 to release */
        s->acc_sema = (uint8_t)val;
        break;
    default:
        if (ch < 0) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unmapped BM write at 0x%" HWADDR_PRIx "\n", __func__, addr);
            break;
        }
        switch (reg_offset) {
        case ICH_REG_BDBAR:
            if (size >= 4) s->ch[ch].bdbar = (uint32_t)val;
            break;
        case ICH_REG_LVI:
            s->ch[ch].lvi = (uint8_t)val;
            break;
        case ICH_REG_SR:
            /* W1C for interrupt status bits, but also allow setting DCH via hardware */
            s->ch[ch].sr &= ~((uint8_t)val);
            break;
        case ICH_REG_PICB:
            /* Read-only? Ignore write */
            break;
        case ICH_REG_PIV:
            /* Read-only */
            break;
        case ICH_REG_CIV:
            /* Read-only */
            break;
        case ICH_REG_CR:
            s->ch[ch].cr = (uint8_t)val;
            if (val & ICH_STARTBM) {
                s->ch[ch].running = true;
                s->ch[ch].sr &= ~ICH_DCH;
            } else {
                s->ch[ch].running = false;
                s->ch[ch].sr |= ICH_DCH;
                if (val & ICH_RESETREGS) {
                    /* Reset channel registers to default (except BDBAR?) */
                    s->ch[ch].civ = 0;
                    s->ch[ch].lvi = ICH_REG_LVI_MASK;
                    s->ch[ch].sr = ICH_DCH;
                    s->ch[ch].picb = 0;
                    s->ch[ch].piv = 0;
                }
            }
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unmapped channel BM write at 0x%" HWADDR_PRIx "\n", __func__, addr);
            break;
        }
        break;
    }
}

static const MemoryRegionOps bm_ops = {
    .read = pcibase_bm_read,
    .write = pcibase_bm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

/* Unused PIO ops */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

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

    /* Reset global registers */
    s->glob_cnt = 0;
    s->glob_sta = ICH_PCR;  /* indicate codec ready by default */
    s->acc_sema = 0;

    /* Reset channels */
    for (int i = 0; i < 2; i++) {
        s->ch[i].bdbar = 0;
        s->ch[i].civ = 0;
        s->ch[i].lvi = ICH_REG_LVI_MASK;
        s->ch[i].sr = ICH_DCH;   /* DMA halted */
        s->ch[i].picb = 0;
        s->ch[i].piv = 0;
        s->ch[i].cr = 0;
        s->ch[i].running = false;
    }

    /* Reset AC97 registers to initial valid state */
    memset(s->ac97_regs, 0, sizeof(s->ac97_regs));
    s->ac97_read_index = 0;
    s->ac97_write_index = 0;
    s->ac97_regs[0x00] = 0xC000; /* Codec ready (bit15), primary codec ready (bit14) */
    s->ac97_regs[0x7C] = 0x8086; /* Vendor ID1 */
    s->ac97_regs[0x7E] = 0x2416; /* Vendor ID2 */
    s->ac97_regs[0x28] = 0x0200; /* Extended Audio ID: modem */

    timer_del(s->reset_timer);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* PCI Express capability (driver expects PCIe? Not required, but template includes it) */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Enable bus mastering (driver expects it) */
    pci_set_word(pci_conf + PCI_COMMAND,
                 pci_get_word(pci_conf + PCI_COMMAND) | PCI_COMMAND_MASTER);

    /* No MSI/MSI-X */

    /* BAR0: AC97 codec access (indexed registers region, 8 bytes) */
    memory_region_init_io(&s->ac97_mmio, OBJECT(s), &ac97_ops, s, "ac97-mem", 8);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->ac97_mmio);

    /* BAR1: Bus Master registers */
    memory_region_init_io(&s->bm_mmio, OBJECT(s), &bm_ops, s, "bm-mem", 256);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bm_mmio);

    /* Create timer for reset emulation */
    s->reset_timer = timer_new_us(QEMU_CLOCK_VIRTUAL, pcibase_reset_timer_cb, s);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    timer_del(s->reset_timer);
    timer_free(s->reset_timer);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "snd_intel8x0m_pci",
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
