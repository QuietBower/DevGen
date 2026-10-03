/*
 * QEMU ATI PATA/ACPI controller emulation for pata_acpi driver
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "hw/irq.h"

#define TYPE_PATA_ACPI "pata_acpi"
OBJECT_DECLARE_SIMPLE_TYPE(PATAACPIState, PATA_ACPI)

/* Standard IDE registers offsets */
#define REG_DATA        0x00
#define REG_FEATURES    0x01
#define REG_SECTOR_COUNT 0x02
#define REG_SECTOR_NUM  0x03
#define REG_CYLINDER_LOW 0x04
#define REG_CYLINDER_HIGH 0x05
#define REG_DEVICE_HEAD 0x06
#define REG_COMMAND     0x07
#define REG_ALT_STATUS  0x02  /* in control block */
#define REG_DEV_CONTROL 0x02  /* in control block */

/* Bus master DMA registers */
#define BM_COMMAND_REG   0x00
#define BM_STATUS_REG    0x02
#define BM_PRD_TABLE_REG 0x04

#define BM_CMD_START     0x01
#define BM_CMD_WRITE     0x08
#define BM_STATUS_ACTIVE 0x01
#define BM_STATUS_ERROR  0x02
#define BM_STATUS_INTR   0x04
#define BM_STATUS_DMA_CAP0 0x20
#define BM_STATUS_DMA_CAP1 0x40
#define BM_STATUS_SIMPLEX 0x80

/* ATA command codes */
#define CMD_IDENTIFY_DEVICE 0xEC

/* IDE device status bits */
#define ATA_SR_BSY  0x80
#define ATA_SR_DRDY 0x40
#define ATA_SR_DF   0x20
#define ATA_SR_ERR  0x01

#define IDENTIFY_SECTOR_SIZE 512

struct PATAACPIState {
    PCIDevice parent_obj;

    /* Primary channel command block (BAR0) */
    MemoryRegion iobar0;
    /* Primary channel control block (BAR1) */
    MemoryRegion iobar1;
    /* Secondary channel command block (BAR2) */
    MemoryRegion iobar2;
    /* Secondary channel control block (BAR3) */
    MemoryRegion iobar3;
    /* Bus master DMA (BAR4) */
    MemoryRegion iobar4;

    /* IDE registers for primary master */
    uint8_t  error;
    uint8_t  sector_count;
    uint8_t  sector_num;
    uint8_t  cylinder_low;
    uint8_t  cylinder_high;
    uint8_t  device_head;
    uint8_t  status;
    uint8_t  command;
    uint16_t data;

    /* Device control */
    uint8_t  dev_control;

    /* DMA registers */
    uint8_t  bm_command;
    uint8_t  bm_status;
    uint32_t bm_prd_table;

    /* Interrupt state */
    bool irq_pending;
    qemu_irq irq;

    /* Flag to indicate if a command is being processed */
    bool cmd_running;
    QEMUTimer *cmd_timer;
};

static void pata_acpi_update_irq(PATAACPIState *s)
{
    if (s->irq_pending) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static void pata_acpi_cmd_timer_cb(void *opaque)
{
    PATAACPIState *s = opaque;
    uint8_t cmd = s->command;

    s->cmd_running = false;

    if (cmd == CMD_IDENTIFY_DEVICE) {
        /* Handle IDENTIFY DEVICE: provide a dummy identify sector */
        s->status = ATA_SR_DRDY | ATA_SR_DF; /* DRDY + DF (Device Fault) to indicate something present */
        /* In a real device, data would be placed in the data register (16-bit) */
        /* For simplicity just set status bits */
    } else {
        s->status = ATA_SR_DRDY; /* Ready */
    }
    s->error = 0;

    /* Raise interrupt */
    s->irq_pending = true;
    pata_acpi_update_irq(s);
}

static uint64_t pata_acpi_iobar0_read(void *opaque, hwaddr addr, unsigned size)
{
    PATAACPIState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case REG_DATA:
        /* 16-bit data register */
        val = s->data;
        break;
    case REG_FEATURES:
        val = s->error;
        break;
    case REG_SECTOR_COUNT:
        val = s->sector_count;
        break;
    case REG_SECTOR_NUM:
        val = s->sector_num;
        break;
    case REG_CYLINDER_LOW:
        val = s->cylinder_low;
        break;
    case REG_CYLINDER_HIGH:
        val = s->cylinder_high;
        break;
    case REG_DEVICE_HEAD:
        val = s->device_head;
        break;
    case REG_COMMAND:
        val = s->status;
        /* Reading status clears interrupt */
        s->irq_pending = false;
        pata_acpi_update_irq(s);
        break;
    default:
        val = 0xff;
        break;
    }
    return val;
}

static void pata_acpi_iobar0_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PATAACPIState *s = opaque;

    switch (addr) {
    case REG_DATA:
        s->data = val;
        break;
    case REG_FEATURES:
        s->error = val;
        break;
    case REG_SECTOR_COUNT:
        s->sector_count = val;
        break;
    case REG_SECTOR_NUM:
        s->sector_num = val;
        break;
    case REG_CYLINDER_LOW:
        s->cylinder_low = val;
        break;
    case REG_CYLINDER_HIGH:
        s->cylinder_high = val;
        break;
    case REG_DEVICE_HEAD:
        s->device_head = val;
        break;
    case REG_COMMAND:
        s->command = val;
        s->status = ATA_SR_BSY; /* Busy */
        s->irq_pending = false;
        pata_acpi_update_irq(s);
        /* Start timer to finish command */
        if (!s->cmd_running) {
            s->cmd_running = true;
            timer_mod(s->cmd_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 100 * SCALE_MS);
        }
        break;
    default:
        break;
    }
}

static uint64_t pata_acpi_iobar1_read(void *opaque, hwaddr addr, unsigned size)
{
    PATAACPIState *s = opaque;
    if (addr == REG_ALT_STATUS) {
        return s->status; /* same as status register */
    }
    return 0xff;
}

static void pata_acpi_iobar1_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PATAACPIState *s = opaque;
    if (addr == REG_DEV_CONTROL) {
        s->dev_control = val;
        /* nIEN bit (bit 1) affects interrupt enable */
        /* For simplicity, ignore */
    }
}

static const MemoryRegionOps pata_acpi_iobar0_ops = {
    .read = pata_acpi_iobar0_read,
    .write = pata_acpi_iobar0_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 2 },
    .impl  = { .min_access_size = 1, .max_access_size = 2 },
};

static const MemoryRegionOps pata_acpi_iobar1_ops = {
    .read = pata_acpi_iobar1_read,
    .write = pata_acpi_iobar1_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

/* Secondary channel uses same handlers but with separate state? We reuse the same state for now */
static uint64_t pata_acpi_iobar2_read(void *opaque, hwaddr addr, unsigned size)
{
    /* Simplified: just return 0xff (not present) */
    return 0xff;
}

static void pata_acpi_iobar2_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* ignore */
}

static const MemoryRegionOps pata_acpi_iobar2_ops = {
    .read = pata_acpi_iobar2_read,
    .write = pata_acpi_iobar2_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static uint64_t pata_acpi_iobar3_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0xff;
}

static void pata_acpi_iobar3_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* ignore */
}

static const MemoryRegionOps pata_acpi_iobar3_ops = {
    .read = pata_acpi_iobar3_read,
    .write = pata_acpi_iobar3_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

static uint64_t pata_acpi_bmdma_read(void *opaque, hwaddr addr, unsigned size)
{
    PATAACPIState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case BM_COMMAND_REG:
        val = s->bm_command;
        break;
    case BM_STATUS_REG:
        val = s->bm_status;
        /* Reading status clears interrupt bit? */
        s->bm_status &= ~BM_STATUS_INTR;
        break;
    case BM_PRD_TABLE_REG:
        val = s->bm_prd_table;
        break;
    default:
        val = 0xff;
        break;
    }
    return val;
}

static void pata_acpi_bmdma_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PATAACPIState *s = opaque;

    switch (addr) {
    case BM_COMMAND_REG:
        s->bm_command = val & 0x0f;
        if (val & BM_CMD_START) {
            /* Start DMA transfer (simplified: just set active bit and finish immediately) */
            s->bm_status |= BM_STATUS_ACTIVE;
            /* Fake completion: clear active, set interrupt */
            s->bm_status &= ~BM_STATUS_ACTIVE;
            s->bm_status |= BM_STATUS_INTR;
            s->irq_pending = true;
            pata_acpi_update_irq(s);
        }
        break;
    case BM_STATUS_REG:
        /* Write 1 to clear bits */
        s->bm_status &= ~(val & (BM_STATUS_INTR | BM_STATUS_ERROR));
        if (!(s->bm_status & BM_STATUS_INTR)) {
            s->irq_pending = false;
            pata_acpi_update_irq(s);
        }
        break;
    case BM_PRD_TABLE_REG:
        s->bm_prd_table = val & 0xfffffffc;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pata_acpi_bmdma_ops = {
    .read = pata_acpi_bmdma_read,
    .write = pata_acpi_bmdma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pata_acpi_reset(DeviceState *dev)
{
    PATAACPIState *s = PATA_ACPI(dev);

    /* Reset IDE registers */
    s->data = 0;
    s->error = 0;
    s->sector_count = 0;
    s->sector_num = 0;
    s->cylinder_low = 0;
    s->cylinder_high = 0;
    s->device_head = 0xa0; /* master */
    s->status = ATA_SR_DRDY | ATA_SR_DF; /* Ready and Device Fault to indicate presence */
    s->irq_pending = false;
    pata_acpi_update_irq(s);

    /* Reset DMA registers */
    s->bm_command = 0;
    s->bm_status = BM_STATUS_DMA_CAP0 | BM_STATUS_DMA_CAP1; /* both drives DMA capable */
    s->bm_prd_table = 0;
}

static void pata_acpi_realize(PCIDevice *pdev, Error **errp)
{
    PATAACPIState *s = PATA_ACPI(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_config_set_vendor_id(pci_conf, 0x1002);
    pci_config_set_device_id(pci_conf, 0x0000);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_IDE);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Create I/O BARs */
    memory_region_init_io(&s->iobar0, OBJECT(s), &pata_acpi_iobar0_ops, s, "pata-acpi-cmd0", 8);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->iobar0);

    memory_region_init_io(&s->iobar1, OBJECT(s), &pata_acpi_iobar1_ops, s, "pata-acpi-ctrl0", 4);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_IO, &s->iobar1);

    memory_region_init_io(&s->iobar2, OBJECT(s), &pata_acpi_iobar2_ops, s, "pata-acpi-cmd1", 8);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_IO, &s->iobar2);

    memory_region_init_io(&s->iobar3, OBJECT(s), &pata_acpi_iobar3_ops, s, "pata-acpi-ctrl1", 4);
    pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_IO, &s->iobar3);

    memory_region_init_io(&s->iobar4, OBJECT(s), &pata_acpi_bmdma_ops, s, "pata-acpi-bmdma", 16);
    pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_IO, &s->iobar4);

    /* Create timer for command handling */
    s->cmd_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, pata_acpi_cmd_timer_cb, s);

    /* Initialize interrupt */
    s->irq = pci_allocate_irq(pdev);
    pata_acpi_reset(DEVICE(s));
}

static void pata_acpi_exit(PCIDevice *pdev)
{
    PATAACPIState *s = PATA_ACPI(pdev);
    timer_del(s->cmd_timer);
    timer_free(s->cmd_timer);
}

static const VMStateDescription vmstate_pata_acpi = {
    .name = "pata_acpi",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PATAACPIState),
        VMSTATE_UINT8(error, PATAACPIState),
        VMSTATE_END_OF_LIST()
    }
};

static void pata_acpi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pata_acpi_realize;
    k->exit = pata_acpi_exit;
    dc->reset = pata_acpi_reset;
    dc->vmsd = &vmstate_pata_acpi;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo pata_acpi_info = {
    .name = TYPE_PATA_ACPI,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PATAACPIState),
    .class_init = pata_acpi_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pata_acpi_register_types(void)
{
    type_register_static(&pata_acpi_info);
}

type_init(pata_acpi_register_types)
