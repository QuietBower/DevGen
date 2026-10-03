/*
 * QEMU PCI device model for Pacific Digital Corporation ADMA ATA controller
 * Based on Linux driver: drivers/ata/pdc_adma.c
 * Target QEMU: 8.2.10
 */

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
#include "hw/qdev-properties.h"

#define TYPE_PCIBASE_DEVICE "pdc_adma_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Driver Constants */
#define DRV_NAME "pdc_adma"
#define PCI_VENDOR_ID_PDC 0x15e9
#define PDC_ADMA_DEVICE_ID 0x1841

/* BAR 4 is used for MMIO */
#define ADMA_MMIO_BAR 4
#define ADMA_MMIO_SIZE 4096

/* Register Offsets & Strides */
#define ADMA_PORTS 2
#define ADMA_ATA_REG_STRIDE 0x40
#define ADMA_ADMA_REG_STRIDE 0x20
#define ADMA_ADMA_REG_BASE  0x80

/* ADMA Register Offsets (Relative to Port ADMA Base) */
#define ADMA_CONTROL    0x00
#define ADMA_STATUS     0x02
#define ADMA_CPB_COUNT  0x04
#define ADMA_CPB_NEXT   0x08
#define ADMA_FIFO_IN    0x0C
#define ADMA_FIFO_OUT   0x0E

/* Global Registers */
#define ADMA_MODE_LOCK  0xC0

/* ADMA_CONTROL Bitmasks */
#define aGO      0x0080 /* (1 << 7) */
#define aNIEN    0x0100 /* (1 << 8) */
#define aRSTADM  0x0200 /* (1 << 9) */
#define aPIOMD4  0x0400 /* (1 << 10) */

/* ADMA_STATUS Bitmasks */
#define aPERR    0x01   /* (1 << 0) */
#define aPSD     0x02   /* (1 << 1) */
#define aUIRQ    0x04   /* (1 << 2) */
#define aBSD     0x08   /* (1 << 3) */
#define aPTC     0x10   /* (1 << 4) */
#define aDFSD    0x20   /* (1 << 5) */
#define aSCNT    0x40   /* (1 << 6) */

/* Packet Flags */
#define cDONE    0x01   /* (1 << 0) */
#define cDAT     0x02   /* (1 << 1) */
#define cVLD     0x04   /* (1 << 2) */
#define cATERR   0x08   /* (1 << 3) */
#define cIEN     0x10   /* (1 << 4) */

/* ATA Registers (Standard offsets relative to port base) */
#define ATA_REG_DATA       0x00
#define ATA_REG_ERROR      0x04
#define ATA_REG_FEATURE    0x04
#define ATA_REG_NSECT      0x08
#define ATA_REG_LBAL       0x0C
#define ATA_REG_LBAM       0x10
#define ATA_REG_LBAH       0x14
#define ATA_REG_DEVICE     0x18
#define ATA_REG_STATUS     0x1C
#define ATA_REG_CMD        0x1C
#define ATA_REG_ALTSTATUS  0x38
#define ATA_REG_CTL        0x38

/* Port State */
typedef struct PdcPort {
    /* ATA Registers */
    uint8_t ata_error;
    uint8_t ata_nsect;
    uint8_t ata_lbal;
    uint8_t ata_lbam;
    uint8_t ata_lbah;
    uint8_t ata_device;
    uint8_t ata_status;
    uint8_t ata_altstatus;
    
    /* ADMA Registers */
    uint16_t adma_control;
    uint8_t  adma_status;
    uint16_t cpb_count;
    uint32_t cpb_next;
    uint16_t fifo_in;
    uint16_t fifo_out;

} PdcPort;

struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion mmio;

    PdcPort ports[ADMA_PORTS];
    uint8_t mode_lock;
};

/* Helper: Calculate port index and offset from address */
static int get_port_from_addr(hwaddr addr, bool *is_adma, hwaddr *rel_addr) {
    if (addr < ADMA_ADMA_REG_BASE) {
        /* ATA Registers: 0x00-0x3F (Port 0), 0x40-0x7F (Port 1) */
        int port = addr / ADMA_ATA_REG_STRIDE;
        if (port < ADMA_PORTS) {
            *is_adma = false;
            *rel_addr = addr % ADMA_ATA_REG_STRIDE;
            return port;
        }
    } else if (addr < ADMA_MODE_LOCK) {
        /* ADMA Registers: 0x80-0x9F (Port 0), 0xA0-0xBF (Port 1) */
        int offset = addr - ADMA_ADMA_REG_BASE;
        int port = offset / ADMA_ADMA_REG_STRIDE;
        if (port < ADMA_PORTS) {
            *is_adma = true;
            *rel_addr = offset % ADMA_ADMA_REG_STRIDE;
            return port;
        }
    }
    return -1;
}

static void pdc_update_irq(PCIBaseState *s) {
    int i;
    bool raise = false;
    for (i = 0; i < ADMA_PORTS; i++) {
        /* Interrupt if status bits are set and interrupts are enabled (aNIEN=0) */
        /* Note: aNIEN is in ADMA_CONTROL. If set, IRQ disabled. */
        bool int_enabled = !(s->ports[i].adma_control & aNIEN);
        
        if (s->ports[i].adma_status != 0 && int_enabled) {
            raise = true;
            break;
        }
        /* Also check ATA legacy interrupts if enabled (simplified) */
        /* In ADMA mode, we rely on ADMA status. */
    }
    pci_set_irq(PCI_DEVICE(s), raise);
}

static void pdc_process_dma(PCIBaseState *s, int port_no) {
    PdcPort *p = &s->ports[port_no];
    uint8_t pkt_header[32];
    
    /* Read packet header from cpb_next */
    if (dma_memory_read(&address_space_memory, p->cpb_next, pkt_header, sizeof(pkt_header), MEMTXATTRS_UNSPECIFIED)) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] DMA read failed at 0x%x\n", TYPE_PCIBASE_DEVICE, p->cpb_next);
        p->adma_status |= aPERR;
        pdc_update_irq(s);
        return;
    }

    /* 
     * Driver expects pkt[0] to be updated to cDONE (0x01) on success.
     * We simulate immediate success.
     */
    pkt_header[0] = cDONE;
    
    /* Write back status to packet in memory */
    if (dma_memory_write(&address_space_memory, p->cpb_next, pkt_header, 1, MEMTXATTRS_UNSPECIFIED)) {
        qemu_log_mask(LOG_GUEST_ERROR, "[%s] DMA write failed at 0x%x\n", TYPE_PCIBASE_DEVICE, p->cpb_next);
        p->adma_status |= aPERR;
    } else {
        /* Set status bits indicating completion */
        p->adma_status |= (aPSD | aUIRQ);
    }

    /* Clear GO bit */
    p->adma_control &= ~aGO;
    
    pdc_update_irq(s);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    bool is_adma = false;
    hwaddr rel_addr = 0;
    int port = get_port_from_addr(addr, &is_adma, &rel_addr);

    if (addr == ADMA_MODE_LOCK) {
        return s->mode_lock;
    }

    if (port < 0) {
        qemu_log_mask(LOG_UNIMP, "[%s] Read unknown addr 0x%"HWADDR_PRIx"\n", TYPE_PCIBASE_DEVICE, addr);
        return 0;
    }

    PdcPort *p = &s->ports[port];

    if (is_adma) {
        switch (rel_addr) {
        case ADMA_CONTROL:
            return p->adma_control;
        case ADMA_STATUS:
            return p->adma_status;
        case ADMA_CPB_COUNT:
            return p->cpb_count;
        case ADMA_CPB_NEXT:
            return p->cpb_next;
        case ADMA_FIFO_IN:
            return p->fifo_in;
        case ADMA_FIFO_OUT:
            return p->fifo_out;
        default:
            qemu_log_mask(LOG_UNIMP, "[%s] ADMA Read port %d off 0x%"HWADDR_PRIx"\n", TYPE_PCIBASE_DEVICE, port, rel_addr);
            return 0;
        }
    } else {
        /* ATA Registers */
        switch (rel_addr) {
        case ATA_REG_DATA:
            return 0; 
        case ATA_REG_ERROR: 
            return p->ata_error;
        case ATA_REG_NSECT:
            return p->ata_nsect;
        case ATA_REG_LBAL:
            return p->ata_lbal;
        case ATA_REG_LBAM:
            return p->ata_lbam;
        case ATA_REG_LBAH:
            return p->ata_lbah;
        case ATA_REG_DEVICE:
            return p->ata_device;
        case ATA_REG_STATUS: 
            return p->ata_status;
        case ATA_REG_ALTSTATUS: 
            return p->ata_altstatus;
        default:
            return 0;
        }
    }
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    bool is_adma = false;
    hwaddr rel_addr = 0;
    int port = get_port_from_addr(addr, &is_adma, &rel_addr);

    if (addr == ADMA_MODE_LOCK) {
        s->mode_lock = val & 0xFF;
        return;
    }

    if (port < 0) {
        qemu_log_mask(LOG_UNIMP, "[%s] Write unknown addr 0x%"HWADDR_PRIx"\n", TYPE_PCIBASE_DEVICE, addr);
        return;
    }

    PdcPort *p = &s->ports[port];

    if (is_adma) {
        switch (rel_addr) {
        case ADMA_CONTROL:
            p->adma_control = val & 0xFFFF;
            if (val & aRSTADM) {
                /* Reset ADMA state */
                p->adma_status = 0;
                p->adma_control &= ~aGO;
            }
            if (val & aGO) {
                /* Start DMA */
                pdc_process_dma(s, port);
            }
            pdc_update_irq(s);
            break;
        case ADMA_STATUS:
            /* Write 1 to clear status bits */
            p->adma_status &= ~(val & 0xFF);
            pdc_update_irq(s);
            break;
        case ADMA_CPB_COUNT:
            p->cpb_count = val & 0xFFFF;
            break;
        case ADMA_CPB_NEXT:
            p->cpb_next = val;
            break;
        case ADMA_FIFO_IN:
            p->fifo_in = val & 0xFFFF;
            break;
        case ADMA_FIFO_OUT:
            p->fifo_out = val & 0xFFFF;
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "[%s] ADMA Write port %d off 0x%"HWADDR_PRIx"\n", TYPE_PCIBASE_DEVICE, port, rel_addr);
        }
    } else {
        /* ATA Registers */
        switch (rel_addr) {
        case ATA_REG_DATA:
            break;
        case ATA_REG_FEATURE:
            p->ata_error = val & 0xFF; 
            break;
        case ATA_REG_NSECT:
            p->ata_nsect = val & 0xFF;
            break;
        case ATA_REG_LBAL:
            p->ata_lbal = val & 0xFF;
            break;
        case ATA_REG_LBAM:
            p->ata_lbam = val & 0xFF;
            break;
        case ATA_REG_LBAH:
            p->ata_lbah = val & 0xFF;
            break;
        case ATA_REG_DEVICE:
            p->ata_device = val & 0xFF;
            break;
        case ATA_REG_CMD:
            /* Command write triggers ATA logic. 
             * For this model, we just set status to READY/DONE to satisfy probe.
             */
            p->ata_status = 0x50; /* DRDY | DSC */
            pdc_update_irq(s);
            break;
        case ATA_REG_CTL:
            p->ata_altstatus = val & 0xFF;
            break;
        default:
            break;
        }
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
    int i;

    for (i = 0; i < ADMA_PORTS; i++) {
        memset(&s->ports[i], 0, sizeof(PdcPort));
        /* Default ATA status: DRDY */
        s->ports[i].ata_status = 0x50;
        s->ports[i].ata_altstatus = 0x50;
    }
    s->mode_lock = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_PDC);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PDC_ADMA_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_SATA);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR 4: MMIO */
    memory_region_init_io(&s->mmio, OBJECT(s), &pcibase_mmio_ops, s, "pdc-adma-mmio", ADMA_MMIO_SIZE);
    pci_register_bar(pdev, ADMA_MMIO_BAR, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* No special cleanup needed for this model */
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    k->vendor_id = PCI_VENDOR_ID_PDC;
    k->device_id = PDC_ADMA_DEVICE_ID;
    k->class_id = PCI_CLASS_STORAGE_SATA;
    dc->reset  = pcibase_reset;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
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

type_init(pcibase_register_types);