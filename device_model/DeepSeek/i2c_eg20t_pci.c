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
#include "hw/pci/pci_ids.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "i2c_eg20t_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define NORMAL_MODE          0x0
#define CLR_REG              0x0
#define BUFFER_MODE          0x1
#define PCH_EVENT_SET        0
#define PCH_EVENT_NONE       1
#define PCH_MAX_CLK          100000
#define PCH_BUFFER_MODE_ENABLE 0x0002
#define PCH_EEPROM_SW_RST_MODE_ENABLE 0x0008
#define PCH_I2CSADR   0x00
#define PCH_I2CCTL   0x04
#define PCH_I2CSR    0x08
#define PCH_I2CDR    0x0C
#define PCH_I2CMON   0x10
#define PCH_I2CBC    0x14
#define PCH_I2CMOD   0x18
#define PCH_I2CBUFSLV 0x1C
#define PCH_I2CBUFSUB 0x20
#define PCH_I2CBUFFOR 0x24
#define PCH_I2CBUFCTL 0x28
#define PCH_I2CBUFMSK 0x2C
#define PCH_I2CBUFSTA 0x30
#define PCH_I2CBUFLEV 0x34
#define PCH_I2CESRFOR 0x38
#define PCH_I2CESRCTL 0x3C
#define PCH_I2CESRMSK 0x40
#define PCH_I2CESRSTA 0x44
#define PCH_I2CTMR    0x48
#define PCH_I2CSRST   0xFC
#define PCH_I2CNF     0xF8
#define BUS_IDLE_TIMEOUT 20
#define PCH_I2CCTL_I2CMEN 0x0080
#define PCH_START      0x0020
#define PCH_RESTART    0x0004
#define PCH_ESR_START  0x0001
#define PCH_BUFF_START 0x1
#define PCH_REPSTART   0x0004
#define PCH_ACK        0x0008
#define PCH_GETACK     0x0001
#define I2CMCF_BIT     0x0080
#define I2CMIF_BIT     0x0002
#define I2CMAL_BIT     0x0010
#define I2CBMFI_BIT    0x0001
#define I2CBMAL_BIT    0x0002
#define I2CBMNA_BIT    0x0004
#define I2CBMTO_BIT    0x0008
#define I2CBMIS_BIT    0x0010
#define I2CESRFI_BIT   0X0001
#define I2CESRTO_BIT   0x0002
#define I2CESRFIIE_BIT 0x1
#define I2CESRTOIE_BIT 0x2
#define I2CBMDZ_BIT    0x0040
#define I2CBMAG_BIT    0x0020
#define I2CMBB_BIT     0x0020
#define BUFFER_MODE_MASK (I2CBMFI_BIT | I2CBMAL_BIT | I2CBMNA_BIT | I2CBMTO_BIT | I2CBMIS_BIT)
#define FAST_MODE_CLK  400
#define FAST_MODE_EN   0x0001
#define SUB_ADDR_LEN_MAX 4
#define BUF_LEN_MAX    32
#define PCH_BUFFER_MODE 0x1
#define EEPROM_SW_RST_MODE 0x0002
#define NORMAL_INTR_ENBL 0x0300
#define EEPROM_RST_INTR_ENBL (I2CESRFIIE_BIT | I2CESRTOIE_BIT)
#define EEPROM_RST_INTR_DISBL 0x0
#define BUFFER_MODE_INTR_ENBL 0x001F
#define BUFFER_MODE_INTR_DISBL 0x0
#define EEPROM_SR_MODE 0x2
#define I2C_TX_MODE    0x0010
#define PCH_BUF_TX     0xFFF7
#define PCH_BUF_RD     0x0008
#define I2C_ERROR_MASK (I2CESRTO_EVENT | I2CBMIS_EVENT | I2CBMTO_EVENT | I2CBMNA_EVENT | I2CBMAL_EVENT | I2CMAL_EVENT)
#define I2CMAL_EVENT   0x0001
#define I2CMCF_EVENT   0x0002
#define I2CBMFI_EVENT  0x0004
#define I2CBMAL_EVENT  0x0008
#define I2CBMNA_EVENT  0x0010
#define I2CBMTO_EVENT  0x0020
#define I2CBMIS_EVENT  0x0040
#define I2CESRFI_EVENT 0x0080
#define I2CESRTO_EVENT 0x0100

/* Placeholder: BAR1 size, must be >= 0x100 * number of channels from first pci_device_id entry */
#define BAR1_SIZE 0x100

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

    /* Interrupt status for IRQ update logic */
    uint32_t irq_status;

    /* Hardware register shadows (driver-visible register map) */
    struct i2c_regs {
        uint32_t I2CSADR;
        uint32_t I2CCTL;
        uint32_t I2CSR;
        uint32_t I2CDR;
        uint32_t I2CMON;
        uint32_t I2CBC;
        uint32_t I2CMOD;
        uint32_t I2CBUFSLV;
        uint32_t I2CBUFSUB;
        uint32_t I2CBUFFOR;
        uint32_t I2CBUFCTL;
        uint32_t I2CBUFMSK;
        uint32_t I2CBUFSTA;
        uint32_t I2CBUFLEV;
        uint32_t I2CESRFOR;
        uint32_t I2CESRCTL;
        uint32_t I2CESRMSK;
        uint32_t I2CESRSTA;
        uint32_t I2CTMR;
        uint32_t I2CSRST;
        uint32_t I2CNF;
    } regs;
};

/* Internal helper for status-triggered signaling */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool pending = false;

    /* Check if any channel has an interrupt source */
    if (s->regs.I2CSR & (I2CMAL_BIT | I2CMCF_BIT | I2CMIF_BIT)) {
        pending = true;
    }

    if (pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad access size %u\n", __func__, size);
        return 0;
    }

    switch (addr) {
    case PCH_I2CSADR: val = s->regs.I2CSADR; break;
    case PCH_I2CCTL:  val = s->regs.I2CCTL; break;
    case PCH_I2CSR:   val = s->regs.I2CSR; break;
    case PCH_I2CDR:
        val = s->regs.I2CDR;
        /* Simulate data reception: after reading, indicate next byte available */
        if (s->regs.I2CCTL & PCH_I2CCTL_I2CMEN) {
            s->regs.I2CSR |= I2CMIF_BIT;
            pcibase_update_irq(s);
        }
        break;
    case PCH_I2CMON:  val = s->regs.I2CMON; break;
    case PCH_I2CBC:   val = s->regs.I2CBC; break;
    case PCH_I2CMOD:  val = s->regs.I2CMOD; break;
    case PCH_I2CBUFSLV: val = s->regs.I2CBUFSLV; break;
    case PCH_I2CBUFSUB: val = s->regs.I2CBUFSUB; break;
    case PCH_I2CBUFFOR: val = s->regs.I2CBUFFOR; break;
    case PCH_I2CBUFCTL: val = s->regs.I2CBUFCTL; break;
    case PCH_I2CBUFMSK: val = s->regs.I2CBUFMSK; break;
    case PCH_I2CBUFSTA: val = s->regs.I2CBUFSTA; break;
    case PCH_I2CBUFLEV: val = s->regs.I2CBUFLEV; break;
    case PCH_I2CESRFOR: val = s->regs.I2CESRFOR; break;
    case PCH_I2CESRCTL: val = s->regs.I2CESRCTL; break;
    case PCH_I2CESRMSK: val = s->regs.I2CESRMSK; break;
    case PCH_I2CESRSTA: val = s->regs.I2CESRSTA; break;
    case PCH_I2CTMR:   val = s->regs.I2CTMR; break;
    case PCH_I2CSRST:  val = s->regs.I2CSRST; break;
    case PCH_I2CNF:    val = s->regs.I2CNF; break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad access size %u\n", __func__, size);
        return;
    }

    switch (addr) {
    case PCH_I2CSADR: s->regs.I2CSADR = val; break;
    case PCH_I2CCTL:
        {
            uint32_t old = s->regs.I2CCTL;
            s->regs.I2CCTL = val;
            /* Detect stop condition (clearing START bit) */
            if ((old & PCH_START) && !(val & PCH_START)) {
                s->regs.I2CSR |= I2CMCF_BIT;
                pcibase_update_irq(s);
            }
        }
        break;
    case PCH_I2CSR:
        /* Write-1-to-clear for status bits */
        s->regs.I2CSR &= ~val;
        pcibase_update_irq(s);
        break;
    case PCH_I2CDR:
        s->regs.I2CDR = val;
        /* When writing data while master enabled and start active, trigger byte completion */
        if ((s->regs.I2CCTL & PCH_I2CCTL_I2CMEN) && (s->regs.I2CCTL & PCH_START)) {
            s->regs.I2CSR |= I2CMIF_BIT;
            /* Simulate ACK from slave (bit 0 = 0 = ACK) */
            s->regs.I2CSR &= ~PCH_GETACK;
            pcibase_update_irq(s);
        }
        break;
    case PCH_I2CMON:  s->regs.I2CMON = val; break;
    case PCH_I2CBC:   s->regs.I2CBC = val; break;
    case PCH_I2CMOD:  s->regs.I2CMOD = val; break;
    case PCH_I2CBUFSLV: s->regs.I2CBUFSLV = val; break;
    case PCH_I2CBUFSUB: s->regs.I2CBUFSUB = val; break;
    case PCH_I2CBUFFOR: s->regs.I2CBUFFOR = val; break;
    case PCH_I2CBUFCTL: s->regs.I2CBUFCTL = val; break;
    case PCH_I2CBUFMSK: s->regs.I2CBUFMSK = val; break;
    case PCH_I2CBUFSTA: s->regs.I2CBUFSTA = val; break;
    case PCH_I2CBUFLEV: s->regs.I2CBUFLEV = val; break;
    case PCH_I2CESRFOR: s->regs.I2CESRFOR = val; break;
    case PCH_I2CESRCTL: s->regs.I2CESRCTL = val; break;
    case PCH_I2CESRMSK: s->regs.I2CESRMSK = val; break;
    case PCH_I2CESRSTA: s->regs.I2CESRSTA = val; break;
    case PCH_I2CTMR:   s->regs.I2CTMR = val; break;
    case PCH_I2CSRST:
        /* Writing 1 triggers software reset for this channel */
        if (val & 0x1) {
            memset(&s->regs, 0, sizeof(s->regs));
        }
        /* Writing 0 de-asserts reset (no action needed) */
        s->regs.I2CSRST = val;
        break;
    case PCH_I2CNF:    s->regs.I2CNF = val; break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write offset 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
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

    /* Reset all registers to zero */
    memset(&s->regs, 0, sizeof(s->regs));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x00);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x02);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SERIAL_SMBUS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization */
    s->num_bars = 1;
    BARInfo *bi = &s->bar_info[0];
    bi->index = 1;
    bi->type = BAR_TYPE_MMIO;
    bi->size = BAR1_SIZE;
    bi->name = "mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize registers to default state (all zeros) */
    memset(&s->regs, 0, sizeof(s->regs));
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

    /* Nothing to clean up */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "i2c_eg20t_pci",
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

type_init(pcibase_register_types)
