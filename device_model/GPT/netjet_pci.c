/*
 * QEMU PCI device model for Netjet ISDN card (minimal behavioral model)
 * Generated for Linux driver drivers/isdn/hardware/mISDN/netjet.c
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

#define TYPE_PCIBASE_DEVICE "netjet_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define NETJET_VENDOR_ID          0xe159
#define NETJET_DEVICE_ID          0x0001
#define NETJET_CLASS_ID           PCI_CLASS_NETWORK_ISDN

#define TX_IDLE                   0x0002
#define TX_UNDERRUN               0x0100
#define LOG_SIZE                  64
#define NETJET_REV                "2.0"
#define TX_INIT                   0x0001
#define TX_RUN                    0x0004
#define RX_OVERRUN                0x0100
#define NJ_DMA_RXSIZE             128
#define NJ_IRQMASK1               0x05
#define NJ_IRQMASK0               0x04
#define NJ_AUXDATA                0x03
#define NJ_ISAC_OFF               0xc0
#define NJ_DMACTRL                0x01
#define NJ_IRQSTAT0               0x06
#define NJ_DMA_WRITE_ADR          0x24
#define NJ_DMA_READ_ADR           0x14
#define NJ_AUXCTRL                0x02
#define NJ_ISACIRQ                0x10
#define NJ_CTRL                   0x00
#define NJ_DMA_WRITE_IRQ          0x1c
#define NJ_DMA_TXSIZE             128
#define NJ_DMA_READ_START         0x08
#define NJ_DMA_SIZE               4096
#define NJ_DMA_WRITE_END          0x20
#define NJ_DMA_WRITE_START        0x18
#define NJ_DMA_READ_END           0x10
#define NJ_DMA_READ_IRQ           0x0c
#define HDLC_LENGTH_ERROR         3
#define HDLC_CRC_ERROR            2
#define HDLC_FRAMING_ERROR        1
#define NJ_IRQM0_WR_MASK          0x0c
#define NJ_IRQM0_WR_END           0x08
#define NJ_IRQM0_RD_MASK          0x03
#define ISAC_ISTA                 0x20
#define NJ_IRQSTAT1               0x07
#define HDLC_DCHANNEL             0x02
#define HDLC_56KBIT               0x01
#define HDLC_BITREVERSE           0x04
#define ISACX__ICD                0x01
#define ISACX_D_RFO               0x20
#define ISACX_D_XPR               0x10
#define ISACX_D_XMR               0x08
#define ISAC_EXIR                 0x24
#define ISACX_CMDRD_RMC           0x80
#define ISACX__CIC                0x10
#define ISACX_D_XDU               0x04
#define IPAC_TYPE_ISACX           0x0040
#define ISACX_ISTAD               0x20
#define ISACX_CMDRD               0x21
#define ISACX_D_RME               0x80
#define ISACX_D_RPF               0x40
#define ISAC_CMD_RS               0x01
#define ISAC_SQXR                 0x3b
#define ISAC_ADF1                 0x38
#define ISACX_STARD               0x21
#define ISAC_SPCR                 0x30
#define ISACX_MASKD               0x20
#define ISACX_TR_CONF0            0x30
#define ISAC_ADF2                 0x39
#define ISAC_STAR                 0x21
#define ISACX_MASK                0x60
#define ISACX_MODED               0x22
#define IPACX__ON                 0x2C
#define ISACX_ID                  0x64
#define ISACX_ISTA                0x60
#define ISAC_MASK                 0x20
#define ISAC_TIMR                 0x23
#define ISACX_CIR0                0x2E
#define ISAC_CIR0                 0x31
#define ISAC_MODE                 0x22
#define ISACX_TR_CONF2            0x32
#define ISAC_STCR                 0x37
#define ISAC_RBCH                 0x2A
#define ISACX_CIR0_CIC0           0x08
#define ISAC_CMDR                 0x21
#define ISAC_RSTA                 0x27
#define ISAC_RBCL                 0x25
#define ISAC_MOR1                 0x34
#define MONITOR_TX_1              0x2001
#define ISAC_MOCR                 0x3a
#define MONITOR_RX_1              0x1001
#define ISAC_MOX1                 0x34
#define ISAC_MOR0                 0x32
#define MONITOR_RX_0              0x1000
#define MONITOR_TX_0              0x2000
#define ISAC_MOX0                 0x32
#define ISAC_MOSR                 0x3a
#define ISAC_CIR1                 0x33
#define ISACX_RSTAD_RAB           0x10
#define ISACX_RSTAD               0x28
#define ISACX_RSTAD_CRC           0x20
#define ISACX_RSTAD_VFR           0x80
#define ISACX_RBCLD               0x26
#define ISACX_RSTAD_RDO           0x40
#define ISAC_IND_EI               0x06
#define ISAC_CMD_DUI              0x0F
#define ISAC_IND_RS               0x01
#define ISACX_CIX0                0x2E
#define ISAC_CIX0                 0x31

typedef enum nj_types {
    NETJET_S_TJ300,
    NETJET_S_TJ320,
    ENTERNOW__TJ320,
} nj_types;

struct tiger_dma {
    size_t  size;
    uint32_t *start;
    int     idx;
    uint32_t dmastart;
    uint32_t dmairq;
    uint32_t dmaend;
    uint32_t dmacur;
};

struct tiger_hw_shadow {
    uint8_t  ctrlreg;
    uint8_t  dmactrl;
    uint8_t  auxd;
    uint8_t  last_is0;
    uint8_t  irqmask0;
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

    /* Hardware Register Shadows */
    struct tiger_hw_shadow regs;

    /* DMA Context (driver allocates system memory; here we only expose pointer regs) */
    struct tiger_dma send_dma;
    struct tiger_dma recv_dma;

    /* Simple ISAC register window (16 * 4 bytes) */
    uint8_t isac_regs[16];

    /* Latched interrupt status */
    uint8_t irqstat0;
    uint8_t irqstat1;
};

static bool pcibase_msi_enabled(PCIBaseState *s)
{
    return msi_enabled(PCI_DEVICE(s));
}

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool pending = (s->irqstat0 != 0) || (s->irqstat1 != 0);

    if (!pending) {
        if (!pcibase_msi_enabled(s)) {
            pci_set_irq(pdev, 0);
        }
        return;
    }

    if (pcibase_msi_enabled(s)) {
        msi_notify(pdev, 0);
    } else {
        pci_set_irq(pdev, 1);
    }
}

static uint8_t pcibase_io_read8(PCIBaseState *s, hwaddr addr)
{
    switch (addr) {
    case NJ_CTRL:
        return s->regs.ctrlreg;
    case NJ_DMACTRL:
        return s->regs.dmactrl;
    case NJ_AUXCTRL:
        /* driver writes only; reads not used */
        return 0;
    case NJ_AUXDATA:
        return s->regs.auxd;
    case NJ_IRQMASK0:
        return s->regs.irqmask0;
    case NJ_IRQMASK1:
        /* driver writes only; reads not used */
        return 0;
    case NJ_IRQSTAT0:
        /* Edge triggered: reading returns current latched status */
        return s->irqstat0;
    case NJ_IRQSTAT1:
        return s->irqstat1;
    default:
        /* ISAC window: base + NJ_ISAC_OFF + ((offset & 0x0f) << 2) */
        if (addr >= NJ_ISAC_OFF && addr < NJ_ISAC_OFF + 0x40) {
            unsigned index = (addr - NJ_ISAC_OFF) >> 2; /* 0..15 */
            if (index < 16) {
                return s->isac_regs[index];
            }
        }
        return 0xff;
    }
}

static void pcibase_io_write8(PCIBaseState *s, hwaddr addr, uint8_t val)
{
    switch (addr) {
    case NJ_CTRL:
        /*
         * Driver uses this to reset the card and set status behavior.
         * We simply store the value.
         */
        s->regs.ctrlreg = val;
        break;
    case NJ_DMACTRL:
        /* DMA enable/disable flag; we just latch it. */
        s->regs.dmactrl = val;
        break;
    case NJ_AUXCTRL:
        /* Edge/pin configuration, not modeled; ignore but latch for debug. */
        break;
    case NJ_AUXDATA:
        /* Upper bits select ISAC register page, lower two bits stored. */
        s->regs.auxd = val;
        break;
    case NJ_IRQMASK0:
        /* Interrupt mask 0; driver enables DMA IRQs here. */
        s->regs.irqmask0 = val;
        /* No immediate IRQ generation; handled when status bits are set. */
        break;
    case NJ_IRQMASK1:
        /* ISAC IRQ mask; driver sets NJ_ISACIRQ bit here. */
        break;
    case NJ_IRQSTAT0:
        /*
         * Driver writes back the value it read to clear asserted bits
         * (write-1-to-clear semantics).
         */
        s->irqstat0 &= ~val;
        pcibase_update_irq(s);
        break;
    case NJ_IRQSTAT1:
        /* No driver writes in provided code; ignore. */
        break;
    default:
        if (addr >= NJ_ISAC_OFF && addr < NJ_ISAC_OFF + 0x40) {
            unsigned index = (addr - NJ_ISAC_OFF) >> 2;
            if (index < 16) {
                s->isac_regs[index] = val;
            }
        }
        break;
    }
}

static uint32_t pcibase_io_read32(PCIBaseState *s, hwaddr addr)
{
    switch (addr) {
    case NJ_DMA_READ_START:
        return s->send_dma.dmastart;
    case NJ_DMA_READ_IRQ:
        return s->send_dma.dmairq;
    case NJ_DMA_READ_END:
        return s->send_dma.dmaend;
    case NJ_DMA_READ_ADR:
        /* Current read DMA address; driver reads this to compute idx. */
        return s->send_dma.dmacur;
    case NJ_DMA_WRITE_START:
        return s->recv_dma.dmastart;
    case NJ_DMA_WRITE_IRQ:
        return s->recv_dma.dmairq;
    case NJ_DMA_WRITE_END:
        return s->recv_dma.dmaend;
    case NJ_DMA_WRITE_ADR:
        return s->recv_dma.dmacur;
    default:
        return 0;
    }
}

static void pcibase_io_write32(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    switch (addr) {
    case NJ_DMA_READ_START:
        s->send_dma.dmastart = val;
        s->send_dma.dmacur = val;
        break;
    case NJ_DMA_READ_IRQ:
        s->send_dma.dmairq = val;
        break;
    case NJ_DMA_READ_END:
        s->send_dma.dmaend = val;
        break;
    case NJ_DMA_READ_ADR:
        s->send_dma.dmacur = val;
        break;
    case NJ_DMA_WRITE_START:
        s->recv_dma.dmastart = val;
        s->recv_dma.dmacur = val;
        break;
    case NJ_DMA_WRITE_IRQ:
        s->recv_dma.dmairq = val;
        break;
    case NJ_DMA_WRITE_END:
        s->recv_dma.dmaend = val;
        break;
    case NJ_DMA_WRITE_ADR:
        s->recv_dma.dmacur = val;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* This device is IO-port based in the driver; MMIO is unused. */
    switch (size) {
    case 1:
        val = pcibase_io_read8(s, addr);
        break;
    case 4:
        val = pcibase_io_read32(s, addr);
        break;
    default:
        /* Unsupported size; return 0. */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (size) {
    case 1:
        pcibase_io_write8(s, addr, (uint8_t)val);
        break;
    case 4:
        pcibase_io_write32(s, addr, (uint32_t)val);
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (size) {
    case 1:
        val = pcibase_io_read8(s, addr);
        break;
    case 4:
        val = pcibase_io_read32(s, addr);
        break;
    default:
        val = 0xff;
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (size) {
    case 1:
        pcibase_io_write8(s, addr, (uint8_t)val);
        break;
    case 4:
        pcibase_io_write32(s, addr, (uint32_t)val);
        break;
    default:
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

    /* Reset shadow registers to power-on state expected by nj_reset() */
    s->regs.ctrlreg = 0x00;
    s->regs.dmactrl = 0x00;
    s->regs.auxd = 0x00;
    s->regs.last_is0 = 0x00;
    s->regs.irqmask0 = 0x00;

    s->irqstat0 = 0x00;
    s->irqstat1 = 0x00;

    memset(s->isac_regs, 0, sizeof(s->isac_regs));

    s->send_dma.size = 0;
    s->send_dma.start = NULL;
    s->send_dma.idx = 0;
    s->send_dma.dmastart = 0;
    s->send_dma.dmairq = 0;
    s->send_dma.dmaend = 0;
    s->send_dma.dmacur = 0;

    s->recv_dma.size = 0;
    s->recv_dma.start = NULL;
    s->recv_dma.idx = 0;
    s->recv_dma.dmastart = 0;
    s->recv_dma.dmairq = 0;
    s->recv_dma.dmaend = 0;
    s->recv_dma.dmacur = 0;

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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  NETJET_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  NETJET_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, NETJET_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "netjet-io";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    memset(&s->regs, 0, sizeof(s->regs));
    memset(&s->send_dma, 0, sizeof(s->send_dma));
    memset(&s->recv_dma, 0, sizeof(s->recv_dma));
    memset(s->isac_regs, 0, sizeof(s->isac_regs));

    s->irqstat0 = 0;
    s->irqstat1 = 0;
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "netjet_pci",
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
