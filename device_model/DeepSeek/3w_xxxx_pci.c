/* QEMU 8.2.10 virtual PCI device for 3ware 9xxx/9750 SATA RAID controller */
#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "qemu/bitops.h"
#include "hw/qdev-properties.h"

#define TYPE_PCIBASE_DEVICE "3w-xxxx"

#define TW_VENDOR_ID 0x13C1
#define TW_DEVICE_ID 0x1000

/* Register offsets from the Base Address Register (BAR0, I/O space) */
#define TW_IO_REGISTER_STATUS      0x00
#define TW_IO_REGISTER_CONTROL     0x00
#define TW_IO_REGISTER_RESPQUEUE   0x04
#define TW_IO_REGISTER_COMMANDQUEUE 0x08
#define TW_IO_REGISTER_SIZE        0x10

/* Status register bits */
#define TW_STATUS_RESPONSE_INTERRUPT       0x00000001
#define TW_STATUS_HOST_INTERRUPT           0x00000002
#define TW_STATUS_RESPONSE_QUEUE_EMPTY     0x00004000
#define TW_STATUS_ATTENTION_INTERRUPT      0x00040000

/* Control register bits */
#define TW_CONTROL_MASK_RESPONSE_INTERRUPT 0x00000002
#define TW_CONTROL_CLEAR_HOST_INTERRUPT    0x00000004
#define TW_CONTROL_UNMASK_RESPONSE_INTERRUPT 0x00004000
#define TW_CONTROL_ENABLE_INTERRUPTS       0x00000080
#define TW_CONTROL_DISABLE_INTERRUPTS      0x00000040
#define TW_CONTROL_SOFT_RESET              0x00000001

/* Commands */
#define TW_CMD_READWRITE 0x01

#define TW_RESPONSE_QUEUE_SIZE 1

typedef struct PCITestDevState {
    PCIDevice pdev;
    MemoryRegion io;
    uint32_t status;
    uint32_t control;
    uint32_t response_queue[TW_RESPONSE_QUEUE_SIZE];
    uint8_t response_queue_head;
    uint8_t response_queue_tail;
    uint8_t command_queue[256];
} PCITestDevState;

static void pcibase_update_irq(PCITestDevState *s)
{
    if (!(s->control & TW_CONTROL_ENABLE_INTERRUPTS) ||
         (s->control & TW_CONTROL_DISABLE_INTERRUPTS)) {
        pci_set_irq(&s->pdev, 0);
        return;
    }

    if ((s->status & TW_STATUS_RESPONSE_INTERRUPT) &&
        (s->control & TW_CONTROL_UNMASK_RESPONSE_INTERRUPT) &&
        !(s->control & TW_CONTROL_MASK_RESPONSE_INTERRUPT)) {
        pci_set_irq(&s->pdev, 1);
    } else if (s->status & TW_STATUS_ATTENTION_INTERRUPT) {
        pci_set_irq(&s->pdev, 1);
    } else {
        pci_set_irq(&s->pdev, 0);
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCITestDevState *s = opaque;
    uint32_t val = 0;

    switch (addr) {
    case TW_IO_REGISTER_STATUS:
        val = s->status;
        if (s->response_queue_head == s->response_queue_tail) {
            val |= TW_STATUS_RESPONSE_QUEUE_EMPTY;
        } else {
            val &= ~TW_STATUS_RESPONSE_QUEUE_EMPTY;
        }
        break;
    case TW_IO_REGISTER_RESPQUEUE:
        if (s->response_queue_head != s->response_queue_tail) {
            val = s->response_queue[s->response_queue_head];
            s->response_queue_head = (s->response_queue_head + 1) % TW_RESPONSE_QUEUE_SIZE;
            if (s->response_queue_head == s->response_queue_tail) {
                s->status &= ~TW_STATUS_RESPONSE_INTERRUPT;
            }
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: read from unimplemented register 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }

    pcibase_update_irq(s);
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCITestDevState *s = opaque;

    switch (addr) {
    case TW_IO_REGISTER_CONTROL:
        if (val & TW_CONTROL_SOFT_RESET) {
            /* Soft reset: reset device state and post a dummy AEN */
            s->status = 0;
            s->control = 0;
            s->response_queue_head = 0;
            s->response_queue_tail = 0;
            s->response_queue[0] = 0x00000000;
            s->response_queue_tail = 1;
            s->status |= TW_STATUS_RESPONSE_INTERRUPT;
        } else {
            s->control = val;
            if (val & TW_CONTROL_CLEAR_HOST_INTERRUPT) {
                s->status &= ~TW_STATUS_HOST_INTERRUPT;
            }
        }
        break;
    case TW_IO_REGISTER_COMMANDQUEUE:
        /* Simple command processing: any command triggers a success response */
        if (s->response_queue_head == s->response_queue_tail) {
            /* Queue is empty, can enqueue */
            s->response_queue[s->response_queue_tail] = 0x00000000; /* success */
            s->response_queue_tail = (s->response_queue_tail + 1) % TW_RESPONSE_QUEUE_SIZE;
            s->status |= TW_STATUS_RESPONSE_INTERRUPT;
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: write to unimplemented register 0x%" HWADDR_PRIx "\n", __func__, addr);
        break;
    }

    pcibase_update_irq(s);
}

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCITestDevState *s = (PCITestDevState *)pdev;
    memory_region_init_io(&s->io, OBJECT(s), &pcibase_pio_ops, s, TYPE_PCIBASE_DEVICE "-io", TW_IO_REGISTER_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->io);
}

static void pcibase_reset(DeviceState *dev)
{
    PCITestDevState *s = (PCITestDevState *)dev;
    s->status = TW_STATUS_RESPONSE_QUEUE_EMPTY;
    s->control = 0;
    s->response_queue_head = 0;
    s->response_queue_tail = 0;
    pcibase_update_irq(s);
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = pcibase_realize;
    k->vendor_id = TW_VENDOR_ID;
    k->device_id = TW_DEVICE_ID;
    k->revision = 0x00;
    k->class_id = PCI_CLASS_STORAGE_RAID;
    dc->reset = pcibase_reset;
}

static const TypeInfo pcibase_type_info = {
    .name = TYPE_PCIBASE_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCITestDevState),
    .class_init = pcibase_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pcibase_register_types(void)
{
    type_register_static(&pcibase_type_info);
}

type_init(pcibase_register_types)
