/*
 * QEMU PCI device model for Initio INI-A100U2W (based on linux-6.18/drivers/scsi/a100u2w.c)
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

/* Additional include files retrieved from driver context */
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "inia100_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
/* Hardware register offsets and constants from a100u2w.c */
#define HOSTSTOP        0x02
#define ORC_HCTRL       0xA5
#define ORC_HSTUS       0xA6
#define RREADY          0x01
#define SCSIRST         0x80
#define HDO             0x40
#define HDI             0x02
#define ORC_CMD_VERSION 0x01
#define ORC_HDATA       0xA4
#define ORC_CMD_SET_NVM 0x03
#define ORC_CMD_GET_NVM 0x04
#define ORC_PQUEUE      0xA8
#define ORCSCB_POST     0x01
#define ORC_RISCCTL     0xE0
#define ORC_EBIOSDATA   0xB3
#define ORC_FWBASEADR   0xAC
#define ORC_RISCRAM     0xEC
#define ORC_EBIOSADR0   0xB0
#define ORC_EBIOSADR2   0xB2
#define EEPRG           0x01
#define PRGMRST         0x002
#define ORC_GCFG        0xA2
#define DOWNLOAD        0x001
#define ORC_MAXQUEUE    245
#define ORC_SCBSIZE     0xB7
#define ORC_SCBBASE1    0xBC
#define ORC_SCBBASE0    0xB8
#define MAX_CHANNELS    2
#define MAX_TARGETS     16
#define ORC_MAXTAGS     64
#define NCC_BUSRESET    0x01
#define DEVRST          0x01
#define ORC_GIMSK       0xA1
#define HCF_SCSI_RESET  0x01
#define ORC_BUSDEVRST   0x01
#define ORC_CMD_ABORT_SCB 0x06
#define ORC_RQUEUE      0xAA
#define ORC_RQUEUECNT   0xAB
#define SCF_NO_DCHK     0x00
#define DISC_ALLOW      0xC0
#define ORC_EXECSCSI    0x00
#define TOTAL_SG_ENTRY  32
#define SENSE_SIZE      14
#define IMAX_CDB        15

/* Additional constants that appear in driver logic but are not explicitly defined above */
#define SIMPLE_QUEUE_TAG 0x20

/* PCI identifiers: first entry from inia100_pci_tbl */
#define INIA100_VENDOR_ID 0x1101
#define INIA100_DEVICE_ID 0x1060

/* The driver is a SCSI HBA, so use the SCSI class code */
#define INIA100_PCI_CLASS PCI_CLASS_STORAGE_SCSI

#define VENDOR_ID  INIA100_VENDOR_ID
#define DEVICE_ID  INIA100_DEVICE_ID
#define CLASS_ID   INIA100_PCI_CLASS

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

/* Simple internal SCB representation index for queueing */
typedef struct OrScbShadow {
    bool in_use;
    uint8_t scbidx;
} OrScbShadow;

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* I/O register shadow space for the first I/O BAR (0-255) */
    uint8_t io_regs[0x100];

    /* Simple command/firmware emulation state */
    uint8_t last_cmd;
    uint8_t last_nv_addr;

    /* SCB queue emulation */
    OrScbShadow scb_array[ORC_MAXQUEUE];
    uint8_t rqueue_entries[ORC_MAXQUEUE];
    uint8_t rqueue_head;
    uint8_t rqueue_tail;
    uint8_t rqueue_cnt;

    /* Interrupt line asserted flag */
    bool irq_asserted;

    /* Other additional info from driver: minimal mapping of orc_host layout relevant to probe/init */
    unsigned long base;      /* Base I/O or MMIO address as used by driver */
    uint8_t index;           /* Channel index */
    uint8_t scsi_id;         /* H/A SCSI ID */
    uint8_t BIOScfg;         /* BIOS configuration */
    uint8_t flags;
    uint8_t max_targets;     /* SCSI0MaxTags in comments */
};


static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* For this simple model we assert INTA whenever there is at least one
     * completed SCB in the return queue and ORC_GIMSK is not fully masked.
     * Driver uses ORC_GIMSK only to disable/enable interrupts with 0xFF/0xFB.
     */
    uint8_t gimsk = s->io_regs[ORC_GIMSK];
    bool have_event = s->rqueue_cnt != 0;
    bool enabled = (gimsk != 0xFF); /* 0xFF used by driver to disable all */

    bool want_assert = have_event && enabled;

    if (want_assert && !s->irq_asserted) {
        pci_set_irq(pdev, 1);
        s->irq_asserted = true;
    } else if (!want_assert && s->irq_asserted) {
        pci_set_irq(pdev, 0);
        s->irq_asserted = false;
    }
}

/* Device-initiated DMA logic is not modeled: the emulation only needs to
 * satisfy driver's expectations about register-visible state for probing.
 */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* No MMIO BARs defined; all driver accesses are PIO. */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* No MMIO BARs defined; all driver accesses are PIO. */
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t val8 = 0;
    uint16_t val16 = 0;
    uint32_t val32 = 0;

    /* We only support byte/word/dword reads depending on offset used by
     * the driver. */
    switch (size) {
    case 1:
        /* Byte-wide registers */
        switch (addr & 0xFF) {
        case ORC_HCTRL:
            val8 = s->io_regs[ORC_HCTRL];
            break;
        case ORC_HSTUS:
            val8 = s->io_regs[ORC_HSTUS];
            break;
        case ORC_HDATA:
            val8 = s->io_regs[ORC_HDATA];
            break;
            
        /* ---- 这里是我们修改的 RQUEUE 队列出队逻辑 ---- */
        case ORC_RQUEUE:
            if (s->rqueue_cnt > 0) {
                val8 = s->rqueue_entries[s->rqueue_head];
                s->rqueue_head = (s->rqueue_head + 1) % ORC_MAXQUEUE;
                s->rqueue_cnt--;
                s->io_regs[ORC_RQUEUECNT] = s->rqueue_cnt;
                /* 队列状态改变，更新中断引脚状态 */
                pcibase_update_irq(s); 
            } else {
                val8 = 0; /* 队列为空时的安全返回值 */
            }
            break;
        /* ------------------------------------------- */

        case ORC_GCFG:
        case ORC_EBIOSDATA:
        case ORC_GIMSK:
        case ORC_RISCCTL:
        case ORC_EBIOSADR2:
        case ORC_SCBSIZE:
        case ORC_RQUEUECNT:
        case ORC_PQUEUE:
            val8 = s->io_regs[addr & 0xFF];
            break;
        default:
            val8 = s->io_regs[addr & 0xFF];
            break;
        }
        return val8;

    case 2:
        /* Word registers: used in driver for BIOS read and EBIOSADR0 */
        switch (addr & 0xFF) {
        case 0x50:
            val16 = s->BIOScfg;
            break;
        case ORC_EBIOSADR0:
            val16 = s->io_regs[ORC_EBIOSADR0] |
                    (s->io_regs[ORC_EBIOSADR0 + 1] << 8);
            break;
        default:
            val16 = s->io_regs[addr & 0xFF] |
                    (s->io_regs[(addr + 1) & 0xFF] << 8);
            break;
        }
        return val16;

    case 4:
        /* Dword registers: used for FWBASEADR, SCBBASE, RISCRAM */
        switch (addr & 0xFF) {
        case ORC_FWBASEADR:
        case ORC_SCBBASE0:
        case ORC_SCBBASE1:
        case ORC_RISCRAM:
            val32 = s->io_regs[addr & 0xFF] |
                    (s->io_regs[(addr + 1) & 0xFF] << 8) |
                    (s->io_regs[(addr + 2) & 0xFF] << 16) |
                    (s->io_regs[(addr + 3) & 0xFF] << 24);
            break;
        default:
            val32 = s->io_regs[addr & 0xFF] |
                    (s->io_regs[(addr + 1) & 0xFF] << 8) |
                    (s->io_regs[(addr + 2) & 0xFF] << 16) |
                    (s->io_regs[(addr + 3) & 0xFF] << 24);
            break;
        }
        return val32;

    default:
        return 0;
    }
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t v8;

    switch (size) {
    case 1:
        v8 = val & 0xFF;
        switch (addr & 0xFF) {
        case ORC_HCTRL:
            /* Handle RESET/host control bits. Driver writes DEVRST
             * then polls HOSTSTOP, then writes 0 or HDO for various
             * reasons. We emulate by updating shadow and immediately
             * setting/clearing bits as if operation completed. */
            s->io_regs[ORC_HCTRL] = v8;

            if (v8 & DEVRST) {
                /* Host adapter reset: set HOSTSTOP, clear SCSIRST. */
                s->io_regs[ORC_HCTRL] |= HOSTSTOP;
                s->io_regs[ORC_HCTRL] &= ~SCSIRST;
                /* After reset we also clear READY until firmware is
                 * reloaded; for simplicity we set it immediately. */
                s->io_regs[ORC_HSTUS] |= RREADY;
            }
            if (v8 & SCSIRST) {
                /* SCSI bus reset: when driver polls until SCSIRST
                 * cleared, we just clear it immediately. */
                s->io_regs[ORC_HCTRL] &= ~SCSIRST;
            }
            if (v8 & HDO) {
                /* Host Data Out: command to firmware. In hardware this
                 * would start an operation and we would later clear HDO
                 * and set HDI; we shortcut that here. */
                s->io_regs[ORC_HCTRL] &= ~HDO;
                s->io_regs[ORC_HSTUS] |= HDI;
            }
            break;
        case ORC_HSTUS:
            /* Status register: driver writes back value it read when
             * HDI was set to clear the interrupt/condition. We simply
             * clear HDI bit when any value is written. */
            s->io_regs[ORC_HSTUS] = v8 & ~HDI;
            break;
        case ORC_HDATA:
            /* Command/data register; behaviour depends on last_cmd. */
            s->io_regs[ORC_HDATA] = v8;
            switch (s->last_cmd) {
            case ORC_CMD_VERSION:
                /* Version 命令不需要后续写入参数数据，安全起见重置状态 */
                s->last_cmd = 0;
                break;
            case ORC_CMD_SET_NVM:
                /* First HDATA after command is address. */
                s->last_nv_addr = v8;
                s->last_cmd = 0;  /* 参数接收完毕，清除命令状态 */
                break;
            case ORC_CMD_GET_NVM:
                /* First HDATA after command is NVRAM address; we
                 * remember it and prepare data/HDI. */
                s->last_nv_addr = v8;
                /* 修复：返回 0x01 以满足驱动中 nvramp->revision == 1 的断言要求。
                 * 0x01 对 EEPROM 里的其他设置（如 SCSI ID、flags）也是良性安全的值。 */
                s->io_regs[ORC_HDATA] = 0x01; 
                s->io_regs[ORC_HSTUS] |= HDI;
                s->last_cmd = 0;  /* 参数接收完毕，清除命令状态 */
                break;
            case ORC_CMD_ABORT_SCB:
                /* v8 is scbidx. Driver then waits for HDI and reads
                 * HDATA to get status (0 success, 1 fail). */
                s->io_regs[ORC_HDATA] = 0x00; /* status 0 = success */
                s->io_regs[ORC_HSTUS] |= HDI;
                s->last_cmd = 0;  /* 参数接收完毕，清除命令状态 */
                break;
            default:
                /* 修复：当无活动命令时，写入 HDATA 的字节即为新的命令 */
                s->last_cmd = v8;
                
                /* 如果是不需要后续写入参数的命令，立即将状态重置 */
                if (v8 == ORC_CMD_VERSION) {
                    s->last_cmd = 0;
                }
                break;
            }
            break;
        case ORC_GCFG:
            /* General config; driver toggles EEPRG bit. Just store. */
            s->io_regs[ORC_GCFG] = v8;
            break;
        case ORC_EBIOSDATA:
            /* In real hardware this reads from BIOS window; driver only
             * reads from this register, never writes, but we accept
             * writes to keep shadow coherent. */
            s->io_regs[ORC_EBIOSDATA] = v8;
            break;
        case ORC_RISCCTL:
            /* Firmware download control; driver writes PRGMRST|DOWNLOAD
             * and PRGMRST. We just store value. */
            s->io_regs[ORC_RISCCTL] = v8;
            break;
        case ORC_EBIOSADR2:
            s->io_regs[ORC_EBIOSADR2] = v8;
            break;
        case ORC_SCBSIZE:
            /* Total number of SCBs. */
            s->io_regs[ORC_SCBSIZE] = v8;
            break;
        case ORC_GIMSK:
            /* Interrupt mask register. */
            s->io_regs[ORC_GIMSK] = v8;
            pcibase_update_irq(s);
            break;
        case ORC_PQUEUE: {
            /* SCB execution queue: writing index posts SCB. We emulate
             * instant completion by pushing the same index into RQUEUE
             * and updating counters/interrupts. */
            uint8_t scbidx = v8;
            if (scbidx < ORC_MAXQUEUE) {
                s->scb_array[scbidx].in_use = true;
                s->scb_array[scbidx].scbidx = scbidx;
                /* Push to return queue */
                s->rqueue_entries[s->rqueue_tail] = scbidx;
                s->rqueue_tail = (s->rqueue_tail + 1) % ORC_MAXQUEUE;
                if (s->rqueue_cnt < ORC_MAXQUEUE) {
                    s->rqueue_cnt++;
                }
                s->io_regs[ORC_RQUEUECNT] = s->rqueue_cnt;
                /* In hardware, scb->status is set by firmware before
                 * interrupt; here we merely assume inia100_scb_handler
                 * will see a successful completion (status already
                 * cleared there in driver). */
                pcibase_update_irq(s);
            }
            break;
        }
        default:
            s->io_regs[addr & 0xFF] = v8;
            break;
        }
        break;

    case 2:
        /* 16-bit writes */
        switch (addr & 0xFF) {
        case 0x50:
            /* BIOS word is never written by the driver; accept for
             * completeness. */
            s->BIOScfg = (uint16_t)val & 0xFFFF;
            break;
        case ORC_EBIOSADR0:
            s->io_regs[ORC_EBIOSADR0] = val & 0xFF;
            s->io_regs[ORC_EBIOSADR0 + 1] = (val >> 8) & 0xFF;
            break;
        default:
            s->io_regs[addr & 0xFF] = val & 0xFF;
            s->io_regs[(addr + 1) & 0xFF] = (val >> 8) & 0xFF;
            break;
        }
        break;

    case 4:
        /* 32-bit writes */
        switch (addr & 0xFF) {
        case ORC_FWBASEADR:
        case ORC_SCBBASE0:
        case ORC_SCBBASE1:
        case ORC_RISCRAM:
            s->io_regs[addr & 0xFF] = val & 0xFF;
            s->io_regs[(addr + 1) & 0xFF] = (val >> 8) & 0xFF;
            s->io_regs[(addr + 2) & 0xFF] = (val >> 16) & 0xFF;
            s->io_regs[(addr + 3) & 0xFF] = (val >> 24) & 0xFF;
            break;
        default:
            s->io_regs[addr & 0xFF] = val & 0xFF;
            s->io_regs[(addr + 1) & 0xFF] = (val >> 8) & 0xFF;
            s->io_regs[(addr + 2) & 0xFF] = (val >> 16) & 0xFF;
            s->io_regs[(addr + 3) & 0xFF] = (val >> 24) & 0xFF;
            break;
        }
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
    int i;

    pci_device_reset(PCI_DEVICE(dev));

    /* Initialize register shadows to power-on defaults adequate for
     * driver probing. */
    memset(s->io_regs, 0, sizeof(s->io_regs));

    /* At power-on we consider the chip "not ready" until init_orchid()
     * is called and firmware is loaded. However, the driver handles both
     * states. For simplicity we report it as ready from start. */
    s->io_regs[ORC_HSTUS] |= RREADY;

    /* BIOS config word: driver reads at port+0x50 and stores in BIOScfg.
     * We just pick a fixed non-zero value. */
    s->BIOScfg = 0x0001;

    /* Program SCB size default, as done by driver in setup_SCBs(). */
    s->io_regs[ORC_SCBSIZE] = ORC_MAXQUEUE;

    /* Interrupt mask defaults: all disabled until driver sets 0xFB. */
    s->io_regs[ORC_GIMSK] = 0xFF;

    /* Clear SCB emulation structures. */
    for (i = 0; i < ORC_MAXQUEUE; i++) {
        s->scb_array[i].in_use = false;
        s->scb_array[i].scbidx = i;
        s->rqueue_entries[i] = 0;
    }
    s->rqueue_head = 0;
    s->rqueue_tail = 0;
    s->rqueue_cnt = 0;
    s->io_regs[ORC_RQUEUECNT] = 0;

    s->last_cmd = 0;
    s->last_nv_addr = 0;

    s->irq_asserted = false;
    pcibase_update_irq(s);
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    /* The driver uses pci_resource_start(pdev, 0) as an I/O port region of size 256. */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_PIO;
    s->bar_info[0].size  = 0x100;
    s->bar_info[0].name  = "inia100-io";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = "unused";
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X are not used in the driver; leave disabled. */
    s->has_msi = false;
    s->has_msix = false;

    /* Initialize runtime state to defaults. */
    pcibase_reset(DEVICE(pdev));
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

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "inia100_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(io_regs, PCIBaseState, 0x100),
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
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
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
