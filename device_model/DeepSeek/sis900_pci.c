#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "qemu/units.h"
#include "qapi/error.h"

#define TYPE_PCIBASE_DEVICE "sis900-pci"
#define PCIBASE_DEVICE(obj) OBJECT_CHECK(PCIBaseState, (obj), TYPE_PCIBASE_DEVICE)

typedef struct {
    PCIDevice pdev;
    MemoryRegion mmio;
    uint32_t isr;
    uint32_t imr;
    uint32_t ier;
    uint32_t cr;
    uint16_t cfg;
    uint32_t mear;
    uint32_t rfcr;
    uint32_t rfdr;
    uint32_t txdp;
    uint32_t rxdp;
    uint32_t txcfg;
    uint32_t rxcfg;
    uint32_t cfgpmc;
    uint32_t pmctrl;
    uint16_t phy_id0;
    uint16_t phy_id1;
    uint16_t mii_control;
    uint16_t mii_status;
    uint16_t mii_anadv;
    uint16_t mii_anlpar;
} PCIBaseState;

#define REG_CR      0x00
#define REG_CFG     0xb0
#define REG_MEAR    0x08
#define REG_ISR     0xa4
#define REG_IMR     0x1604
#define REG_IER     0x04
#define REG_RFCR    0x10
#define REG_RFDR    0x14
#define REG_TXDP    0x18
#define REG_RXDP    0x1c
#define REG_TXCFG   0x20
#define REG_RXCFG   0x24
#define REG_PMCTRL  0x0c
#define REG_CFGPMC  0x28

#define MII_CONTROL  0
#define MII_STATUS   1
#define MII_PHY_ID0  2
#define MII_PHY_ID1  3
#define MII_ANADV    4
#define MII_ANLPAR   5

#define TxRCMP 0x80000000
#define RxRCMP 0x40000000

#define VENDOR_ID 0x1039
#define DEVICE_ID 0x0900
#define CLASS_ID  0x0200

static void pcibase_reset(PCIBaseState *s)
{
    s->isr = 0;
    s->imr = 0;
    s->ier = 0;
    s->cr = 0;
    s->cfg = 0;
    s->mear = 0;
    s->rfcr = 0;
    s->rfdr = 0;
    s->txdp = 0;
    s->rxdp = 0;
    s->txcfg = 0;
    s->rxcfg = 0;
    s->cfgpmc = 0;
    s->pmctrl = 0;
    s->phy_id0 = 0x1D00;
    s->phy_id1 = 0x0000;
    s->mii_control = 0x1000;
    s->mii_status = 0x7809;
    s->mii_anadv = 0x01E1;
    s->mii_anlpar = 0x0000;
}

static void pcibase_pio_read(void *opaque, hwaddr addr, uint64_t *val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t ret = 0;

    switch (addr) {
        case REG_CR:
            ret = s->cr;
            break;
        case REG_ISR:
            ret = s->isr;
            break;
        case REG_IMR:
            ret = s->imr;
            break;
        case REG_IER:
            ret = s->ier;
            break;
        case REG_RFCR:
            ret = s->rfcr;
            break;
        case REG_RFDR:
            ret = s->rfdr;
            break;
        case REG_TXDP:
            ret = s->txdp;
            break;
        case REG_RXDP:
            ret = s->rxdp;
            break;
        case REG_MEAR:
            ret = s->mear;
            break;
        case REG_TXCFG:
            ret = s->txcfg;
            break;
        case REG_RXCFG:
            ret = s->rxcfg;
            break;
        case REG_CFG:
            ret = s->cfg;
            break;
        case REG_PMCTRL:
            ret = s->pmctrl;
            break;
        case REG_CFGPMC:
            ret = s->cfgpmc;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "pcibase_pio_read: unknown addr 0x%"HWADDR_PRIx"\n", addr);
            break;
    }
    *val = ret;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
        case REG_CR:
            s->cr = val;
            if (val & 0x0001) {
                pcibase_reset(s);
            }
            break;
        case REG_ISR:
            s->isr &= ~(val & (TxRCMP | RxRCMP));
            s->isr |= (TxRCMP | RxRCMP);
            break;
        case REG_IMR:
            s->imr = val;
            break;
        case REG_IER:
            s->ier = val;
            break;
        case REG_RFCR:
            s->rfcr = val;
            break;
        case REG_RFDR:
            s->rfdr = val;
            break;
        case REG_TXDP:
            s->txdp = val;
            break;
        case REG_RXDP:
            s->rxdp = val;
            break;
        case REG_MEAR:
            s->mear = val;
            break;
        case REG_CFG:
            s->cfg = val;
            break;
        case REG_PMCTRL:
            s->pmctrl = val;
            break;
        case REG_CFGPMC:
            s->cfgpmc = val;
            break;
        case REG_TXCFG:
            s->txcfg = val;
            break;
        case REG_RXCFG:
            s->rxcfg = val;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "pcibase_pio_write: unknown addr 0x%"HWADDR_PRIx"\n", addr);
            break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, 0x1039);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x0900);

    memory_region_init_io(&s->mmio, OBJECT(s), &pcibase_mmio_ops, s, "sis900-pio", 0x2000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->mmio);

    pcibase_reset(s);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    msix_uninit(pdev, NULL, 0);
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    PCIDeviceClass *pci = PCI_DEVICE_CLASS(klass);
    pci->is_express = false;
    pci->realize = pcibase_realize;
    pci->exit = pcibase_uninit;
    pci->vendor_id = VENDOR_ID;
    pci->device_id = DEVICE_ID;
    pci->class_id = CLASS_ID;
}

static const TypeInfo pcibase_type_info = {
    .name          = TYPE_PCIBASE_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIBaseState),
    .class_init    = pcibase_class_init,
};

static void pcibase_register_types(void)
{
    type_register_static(&pcibase_type_info);
}

type_init(pcibase_register_types)
