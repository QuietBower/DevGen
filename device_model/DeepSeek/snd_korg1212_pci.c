
/*
 * QEMU device model for snd_korg1212 PCI audio card.
 * Based on driver at /home/eely/linux-7.1/sound/pci/korg1212/korg1212.c
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
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "snd_korg1212_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x10b5
#define DEVICE_ID 0x906d
#define CLASS_ID 0x0100  /* Subclass = Audio, Prog-IF = 0 */
#define BAR1_SIZE 0x100  /* I/O port size */

#define MAILBOX0_OFFSET      0x40
#define MAILBOX1_OFFSET      0x44
#define MAILBOX2_OFFSET      0x48
#define MAILBOX3_OFFSET      0x4c
#define OUT_DOORBELL_OFFSET  0x60
#define IN_DOORBELL_OFFSET   0x64
#define STATUS_REG_OFFSET    0x68
#define PCI_CONTROL_OFFSET   0x6c
#define SENS_CONTROL_OFFSET  0x6e
#define DEV_VEND_ID_OFFSET   0x70

/* Doorbell constants from driver enum */
#define K1212_DB_RequestForData        0
#define K1212_DB_TriggerPlay           1
#define K1212_DB_SelectPlayMode        2
#define K1212_DB_ConfigureBufferMemory 3
#define K1212_DB_RequestAdatTimecode   4
#define K1212_DB_SetClockSourceRate    5
#define K1212_DB_ConfigureMiscMemory   6
#define K1212_DB_TriggerFromAdat       7
#define K1212_DB_DMAERROR              0x80
#define K1212_DB_CARDSTOPPED           0x81
#define K1212_DB_RebootCard            0xA0
#define K1212_DB_BootFromDSPPage4      0xA4
#define K1212_DB_DSPDownloadDone       0xAE
#define K1212_DB_StartDSPDownload      0xAF

#define COMMAND_ACK_MASK     0x8000
#define DOORBELL_VAL_MASK    0x00FF

enum CardState {
    K1212_STATE_NONEXISTENT,
    K1212_STATE_UNINITIALIZED,
    K1212_STATE_DSP_IN_PROCESS,
    K1212_STATE_DSP_COMPLETE,
    K1212_STATE_READY,
    K1212_STATE_OPEN,
    K1212_STATE_SETUP,
    K1212_STATE_PLAYING,
    K1212_STATE_MONITOR,
    K1212_STATE_CALIBRATING,
    K1212_STATE_ERRORSTOP,
    K1212_STATE_MAX_STATE
};

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

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    uint8_t regs[0x1000];

    struct {
        dma_addr_t play_buf_addr;
        dma_addr_t rec_buf_addr;
        dma_addr_t shared_buf_addr;
        uint32_t dma_control;
    } dma;

    uint32_t card_state;
    bool boot_complete;
    bool dsp_loaded;

    QEMUTimer *dsp_timer;
};

static void pcibase_process_out_doorbell(PCIBaseState *s, uint32_t doorbell)
{
    switch (doorbell) {
    case K1212_DB_RebootCard:
    case K1212_DB_BootFromDSPPage4:
    case K1212_DB_StartDSPDownload:
        if (doorbell == K1212_DB_StartDSPDownload) {
            timer_mod(s->dsp_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 5);
        }
        break;
    default:
        {
            uint32_t ack = 0x8000 | (doorbell & 0xFF);
            stl_le_p(&s->regs[MAILBOX3_OFFSET], ack);
        }
        break;
    }
}

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t in_db = ldl_le_p(&s->regs[IN_DOORBELL_OFFSET]);
    uint32_t status = ldl_le_p(&s->regs[STATUS_REG_OFFSET]);
    int irq_state = (in_db != 0 && status != 0);
    pci_set_irq(pdev, irq_state ? 1 : 0);
}

static void pcibase_dsp_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    stl_le_p(&s->regs[IN_DOORBELL_OFFSET], K1212_DB_DSPDownloadDone);
    pcibase_update_irq(s);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    if (addr >= 0x1000) return ~0ULL;
    switch (addr) {
    case IN_DOORBELL_OFFSET ... IN_DOORBELL_OFFSET + 3:
        val = ldl_le_p(&s->regs[IN_DOORBELL_OFFSET]);
        break;
    default:
        if (size == 1) val = s->regs[addr];
        else if (size == 2) val = lduw_le_p(&s->regs[addr]);
        else if (size == 4) val = ldl_le_p(&s->regs[addr]);
        else if (size == 8) val = ldq_le_p(&s->regs[addr]);
        else qemu_log_mask(LOG_GUEST_ERROR, "unhandled mmio read size %d\n", size);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    if (addr >= 0x1000) return;
    switch (addr) {
    case MAILBOX0_OFFSET ... MAILBOX3_OFFSET + 3:
        if (size == 1) s->regs[addr] = val;
        else if (size == 2) stw_le_p(&s->regs[addr], val);
        else if (size == 4) stl_le_p(&s->regs[addr], val);
        else if (size == 8) stq_le_p(&s->regs[addr], val);
        else qemu_log_mask(LOG_GUEST_ERROR, "mailbox write size %d\n", size);
        break;
    case OUT_DOORBELL_OFFSET ... OUT_DOORBELL_OFFSET + 3:
        if (size == 4) {
            uint32_t db_val = val;
            stl_le_p(&s->regs[OUT_DOORBELL_OFFSET], db_val);
            if (db_val != 0) {
                pcibase_process_out_doorbell(s, db_val);
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "outdoorbell write size %d\n", size);
        }
        break;
    case IN_DOORBELL_OFFSET ... IN_DOORBELL_OFFSET + 3:
        if (size == 4) {
            stl_le_p(&s->regs[IN_DOORBELL_OFFSET], 0);
            pcibase_update_irq(s);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "indoorbell write size %d\n", size);
        }
        break;
    case STATUS_REG_OFFSET ... STATUS_REG_OFFSET + 3:
        if (size == 4) {
            stl_le_p(&s->regs[STATUS_REG_OFFSET], val);
            pcibase_update_irq(s);
        }
        break;
    case SENS_CONTROL_OFFSET ... SENS_CONTROL_OFFSET + 1:
        /* No side effects, just ignore writes */
        break;
    default:
        if (size == 1) s->regs[addr] = val;
        else if (size == 2) stw_le_p(&s->regs[addr], val);
        else if (size == 4) stl_le_p(&s->regs[addr], val);
        else if (size == 8) stq_le_p(&s->regs[addr], val);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Not used by driver */
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
    memset(s->regs, 0, sizeof(s->regs));
    s->card_state = K1212_STATE_UNINITIALIZED;
    s->boot_complete = false;
    s->dsp_loaded = false;
    timer_del(s->dsp_timer);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE,  CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pci_conf[0x09] = 0x04;   /* base class multimedia */

    s->num_bars = 3;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = 0x1000, .name = "bar0" };
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_PIO, .size = BAR1_SIZE, .name = "bar1" };
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_RAM, .size = 0x8000, .name = "bar2" };
    
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->dsp_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, pcibase_dsp_timer_cb, s);
    s->card_state = K1212_STATE_UNINITIALIZED;
    s->boot_complete = false;
    s->dsp_loaded = false;
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
    timer_del(s->dsp_timer);
    timer_free(s->dsp_timer);
}

static const VMStateDescription vmstate_pcibase = {
    .name = "snd_korg1212_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(regs, PCIBaseState, 0x1000),
        VMSTATE_UINT32(card_state, PCIBaseState),
        VMSTATE_BOOL(boot_complete, PCIBaseState),
        VMSTATE_BOOL(dsp_loaded, PCIBaseState),
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

type_init(pcibase_register_types);
