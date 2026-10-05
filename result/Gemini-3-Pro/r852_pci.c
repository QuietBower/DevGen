/* Complete QEMU C file */
#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/pci/pci.h"
#include "hw/hw.h"
#include "hw/pci/msi.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "qemu/main-loop.h"
#include "qemu/log.h"
#include "sysemu/dma.h"

#define TYPE_PCIBASE_DEVICE "r852"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#ifndef PCI_VENDOR_ID_RICOH
#define PCI_VENDOR_ID_RICOH 0x1180
#endif

#define R852_DEVICE_ID      0x0852

/* Register Offsets */
#define R852_DATALINE		0x00
#define R852_CTL		    0x04
#define R852_CARD_STA		0x05
#define R852_CARD_IRQ_STA	0x06
#define R852_CARD_IRQ_ENABLE	0x07
#define R852_HW             0x08
#define R852_DMA_CAP		0x09
#define R852_DMA_ADDR		0x0C
#define R852_DMA_SETTINGS	0x10
#define R852_DMA_IRQ_STA	0x14
#define R852_DMA_IRQ_ENABLE	0x18
#define R852_DMA1		    0x40
#define R852_DMA2		    0x80

/* Constants */
#define R852_DMA_LEN		512
#define R852_DMA_READ		0x02
#define R852_DMA_INTERNAL	0x04
#define R852_DMA_MEMORY		0x01

#define R852_DMA_IRQ_INTERNAL	0x04
#define R852_DMA_IRQ_ERROR	    0x02
#define R852_DMA_IRQ_MEMORY	    0x01
#define R852_DMA_IRQ_MASK	    0x07

#define R852_CTL_WRITE		0x80
#define R852_CTL_ON		    0x04
#define R852_CTL_COMMAND 	0x01
#define R852_CTL_DATA		0x02
#define R852_CTL_CARDENABLE	0x10
#define R852_CTL_ECC_ENABLE	0x20
#define R852_CTL_ECC_ACCESS	0x40
#define R852_CTL_RESET		0x08

#define R852_CARD_STA_BUSY	0x80
#define R852_CARD_STA_PRESENT	0x04
#define R852_CARD_STA_RO	0x02
#define R852_CARD_STA_CD	0x01

#define R852_CARD_IRQ_REMOVE	0x04
#define R852_CARD_IRQ_GENABLE	0x80
#define R852_CARD_IRQ_INSERT	0x08
#define R852_CARD_IRQ_MASK	    0x1D

#define R852_ECC_ERR_BIT_MSK	0x07
#define R852_ECC_CORRECTABLE	0x20
#define R852_ECC_FAIL		    0x40

#define R852_HW_ENABLED		0x01
#define R852_HW_UNKNOWN		0x80
#define R852_SMBIT		    0x20

struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion mmio;

    /* Registers */
    uint8_t dataline;
    uint8_t ctl;
    uint8_t card_sta;
    uint8_t card_irq_sta;
    uint8_t card_irq_enable;
    uint32_t hw_reg;
    uint8_t dma_cap;
    uint32_t dma_addr;
    uint32_t dma_settings;
    uint32_t dma_irq_sta;
    uint32_t dma_irq_enable;

    /* Internal Buffer */
    uint8_t internal_buf[R852_DMA_LEN];

    /* NAND Emulation State */
    uint8_t nand_cmd;
    bool nand_addr_latch;
    uint8_t nand_id_idx;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    int level = 0;
    
    /* Card IRQ */
    if (s->card_irq_enable & R852_CARD_IRQ_GENABLE) {
        if (s->card_irq_sta & s->card_irq_enable & R852_CARD_IRQ_MASK) {
            level = 1;
        }
    }

    /* DMA IRQ */
    if (s->dma_irq_sta & s->dma_irq_enable & R852_DMA_IRQ_MASK) {
        level = 1;
    }

    pci_set_irq(PCI_DEVICE(s), level);
}

static void pcibase_dma_transfer(PCIBaseState *s)
{
    /* Check for DMA Internal (Device <-> Buffer) */
    if (s->dma_settings & R852_DMA_INTERNAL) {
        /* 
         * In real HW, this moves data between NAND and internal buffer.
         * Here we just pretend it's done.
         * If reading from NAND, we should fill internal_buf with data.
         * If writing to NAND, we consume internal_buf.
         * For now, just fill with pattern on read.
         */
         if (s->dma_settings & R852_DMA_READ) {
             /* Read from Device */
             memset(s->internal_buf, 0xAA, R852_DMA_LEN);
         }
         
         s->dma_irq_sta |= R852_DMA_IRQ_INTERNAL;
    }

    /* Check for DMA Memory (Buffer <-> RAM) */
    if (s->dma_settings & R852_DMA_MEMORY) {
        PCIDevice *pdev = PCI_DEVICE(s);
        dma_addr_t addr = s->dma_addr;
        
        if (s->dma_settings & R852_DMA_READ) {
            /* Device -> Host (Read): Buffer -> RAM */
            pci_dma_write(pdev, addr, s->internal_buf, R852_DMA_LEN);
        } else {
            /* Host -> Device (Write): RAM -> Buffer */
            pci_dma_read(pdev, addr, s->internal_buf, R852_DMA_LEN);
        }
        
        s->dma_irq_sta |= R852_DMA_IRQ_MEMORY;
    }

    pcibase_update_irq(s);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case R852_DATALINE: /* 0x00 */
        /* NAND Data Read */
        if (s->nand_cmd == 0x90) { /* Read ID */
            if (s->nand_id_idx == 0) val = 0xEC; /* Samsung */
            else if (s->nand_id_idx == 1) val = 0x75; /* 32MB */
            else val = 0x00;
            s->nand_id_idx++;
        } else if (s->nand_cmd == 0x70) { /* Status */
            val = 0xE0; /* Ready, Not Protected */
        } else {
            val = 0xFF; /* Default bus value */
        }
        break;
    case R852_CTL: /* 0x04 */
        val = s->ctl;
        break;
    case R852_CARD_STA: /* 0x05 */
        val = s->card_sta;
        break;
    case R852_CARD_IRQ_STA: /* 0x06 */
        val = s->card_irq_sta;
        break;
    case R852_CARD_IRQ_ENABLE: /* 0x07 */
        val = s->card_irq_enable;
        break;
    case R852_HW: /* 0x08 */
        val = s->hw_reg;
        break;
    case R852_DMA_CAP: /* 0x09 */
        val = s->dma_cap;
        break;
    case R852_DMA_ADDR: /* 0x0C */
        val = s->dma_addr;
        break;
    case R852_DMA_SETTINGS: /* 0x10 */
        val = s->dma_settings;
        break;
    case R852_DMA_IRQ_STA: /* 0x14 */
        val = s->dma_irq_sta;
        break;
    case R852_DMA_IRQ_ENABLE: /* 0x18 */
        val = s->dma_irq_enable;
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case R852_DATALINE: /* 0x00 */
        s->dataline = val;
        /* Handle NAND Commands */
        if (s->ctl & R852_CTL_COMMAND) {
            s->nand_cmd = val;
            s->nand_id_idx = 0;
            if (val == 0xFF) { /* Reset */
                s->nand_cmd = 0;
            }
        }
        break;
    case R852_CTL: /* 0x04 */
        s->ctl = val;
        break;
    case R852_CARD_STA: /* 0x05 */
        /* Read-only usually, but maybe write to clear? Driver doesn't write here. */
        break;
    case R852_CARD_IRQ_STA: /* 0x06 */
        /* Write to clear bits */
        s->card_irq_sta &= ~val;
        pcibase_update_irq(s);
        break;
    case R852_CARD_IRQ_ENABLE: /* 0x07 */
        s->card_irq_enable = val;
        pcibase_update_irq(s);
        break;
    case R852_HW: /* 0x08 */
        s->hw_reg = val;
        if (val & R852_HW_ENABLED) {
            /* Reset logic if needed */
        }
        break;
    case R852_DMA_ADDR: /* 0x0C */
        s->dma_addr = val;
        break;
    case R852_DMA_SETTINGS: /* 0x10 */
        s->dma_settings = val;
        if (val & (R852_DMA_INTERNAL | R852_DMA_MEMORY)) {
            pcibase_dma_transfer(s);
        }
        break;
    case R852_DMA_IRQ_STA: /* 0x14 */
        /* Write to clear */
        s->dma_irq_sta &= ~val;
        pcibase_update_irq(s);
        break;
    case R852_DMA_IRQ_ENABLE: /* 0x18 */
        s->dma_irq_enable = val;
        pcibase_update_irq(s);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    
    s->ctl = 0;
    /* Simulate Card Present */
    s->card_sta = R852_CARD_STA_PRESENT | R852_CARD_STA_CD; 
    s->card_irq_sta = R852_CARD_IRQ_INSERT; /* Pending insert IRQ on reset/startup */
    s->card_irq_enable = 0;
    
    s->dma_cap = R852_DMA1 | R852_DMA2 | R852_SMBIT;
    s->dma_addr = 0;
    s->dma_settings = 0;
    s->dma_irq_sta = 0;
    s->dma_irq_enable = 0;
    s->hw_reg = 0;
    
    s->nand_cmd = 0;
    s->nand_id_idx = 0;
    
    pcibase_update_irq(s);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_RICOH);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  R852_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    memory_region_init_io(&s->mmio, OBJECT(s), &pcibase_mmio_ops, s, "r852-mmio", 4096);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No special cleanup needed */
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    k->vendor_id = PCI_VENDOR_ID_RICOH;
    k->device_id = R852_DEVICE_ID;
    k->class_id = PCI_CLASS_OTHERS;
    dc->reset  = pcibase_reset;
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

    /* Alias for r852_pci to match command line */
    static const TypeInfo r852_pci_info = {
        .name = "r852_pci",
        .parent = TYPE_PCIBASE_DEVICE,
    };
    type_register_static(&r852_pci_info);
}

type_init(pcibase_register_types);
