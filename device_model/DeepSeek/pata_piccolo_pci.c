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

#define TYPE_PCIBASE_DEVICE "pata_piccolo_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_TOSHIBA          0x1179
#define PCI_DEVICE_ID_TOSHIBA_PICCOLO_1 0x0101
#define PCI_CLASS_STORAGE_IDE          0x0101

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

#define NUM_BARS 5

typedef struct {
    PCIBaseState *s;
    enum {
        BAR_PRI_CMD = 0,
        BAR_PRI_CTL,
        BAR_SEC_CMD,
        BAR_SEC_CTL,
        BAR_BMDMA
    } bar_type;
} PCIBaseBarState;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    PCIBaseBarState bar_states[NUM_BARS];

    uint8_t  pri_cmd_regs[8];
    uint8_t  pri_altstatus;
    uint8_t  pri_devctrl;
    uint8_t  sec_cmd_regs[8];
    uint8_t  sec_altstatus;
    uint8_t  sec_devctrl;
    uint8_t  bmdma_cmd[2];
    uint8_t  bmdma_status[2];
    uint32_t bmdma_prdt[2];
    QEMUTimer *bmdma_timer[2];
    bool     bmdma_active[2];

    uint8_t  config_shadow[256];
};

static void pcibase_update_irq(PCIBaseState *s);
static void pcibase_update_bmdma_irq(PCIBaseState *s, int ch);

static void pcibase_update_bmdma_irq(PCIBaseState *s, int ch)
{
    if (s->bmdma_status[ch] & 0x04) {
        pci_set_irq(PCI_DEVICE(s), 1);
    } else {
        if (!(s->bmdma_status[0] & 0x04) && !(s->bmdma_status[1] & 0x04)) {
            pci_set_irq(PCI_DEVICE(s), 0);
        }
    }
}

static uint64_t pcibase_pri_cmd_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseBarState *bs = opaque;
    PCIBaseState *s = bs->s;
    uint64_t val = 0;
    addr &= 7;
    switch (addr) {
    case 0:
        if (size == 2) {
            val = 0;
        }
        break;
    case 1:
        val = 0;
        break;
    case 2:
        val = 0;
        break;
    case 3:
        val = 0;
        break;
    case 4:
        val = 0;
        break;
    case 5:
        val = 0;
        break;
    case 6:
        val = s->pri_cmd_regs[6];
        break;
    case 7:
        val = s->pri_cmd_regs[7];
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_pri_cmd_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseBarState *bs = opaque;
    PCIBaseState *s = bs->s;
    addr &= 7;
    switch (addr) {
    case 0:
        break;
    case 1:
        s->pri_cmd_regs[1] = val;
        break;
    case 2:
        s->pri_cmd_regs[2] = val;
        break;
    case 3:
        s->pri_cmd_regs[3] = val;
        break;
    case 4:
        s->pri_cmd_regs[4] = val;
        break;
    case 5:
        s->pri_cmd_regs[5] = val;
        break;
    case 6:
        s->pri_cmd_regs[6] = val;
        s->pri_cmd_regs[7] = 0x00;
        break;
    case 7:
        s->pri_cmd_regs[7] = 0x00;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_pri_cmd_ops = {
    .read = pcibase_pri_cmd_read,
    .write = pcibase_pri_cmd_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 2 },
    .impl  = { .min_access_size = 1, .max_access_size = 2 },
};

static uint64_t pcibase_pri_ctl_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseBarState *bs = opaque;
    PCIBaseState *s = bs->s;
    if (addr == 0) {
        return s->pri_altstatus;
    }
    return 0;
}

static void pcibase_pri_ctl_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseBarState *bs = opaque;
    PCIBaseState *s = bs->s;
    if (addr == 2) {
        s->pri_devctrl = val;
    }
}

static const MemoryRegionOps pcibase_pri_ctl_ops = {
    .read = pcibase_pri_ctl_read,
    .write = pcibase_pri_ctl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

static uint64_t pcibase_sec_cmd_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseBarState *bs = opaque;
    PCIBaseState *s = bs->s;
    uint64_t val = 0;
    addr &= 7;
    switch (addr) {
    case 0: if (size == 2) val = 0; break;
    case 1: val = 0; break;
    case 2: val = 0; break;
    case 3: val = 0; break;
    case 4: val = 0; break;
    case 5: val = 0; break;
    case 6: val = s->sec_cmd_regs[6]; break;
    case 7: val = s->sec_cmd_regs[7]; break;
    }
    return val;
}

static void pcibase_sec_cmd_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseBarState *bs = opaque;
    PCIBaseState *s = bs->s;
    addr &= 7;
    switch (addr) {
    case 0: break;
    case 1: s->sec_cmd_regs[1] = val; break;
    case 2: s->sec_cmd_regs[2] = val; break;
    case 3: s->sec_cmd_regs[3] = val; break;
    case 4: s->sec_cmd_regs[4] = val; break;
    case 5: s->sec_cmd_regs[5] = val; break;
    case 6: s->sec_cmd_regs[6] = val; s->sec_cmd_regs[7] = 0x00; break;
    case 7: s->sec_cmd_regs[7] = 0x00; break;
    }
}

static const MemoryRegionOps pcibase_sec_cmd_ops = {
    .read = pcibase_sec_cmd_read,
    .write = pcibase_sec_cmd_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 2 },
    .impl  = { .min_access_size = 1, .max_access_size = 2 },
};

static uint64_t pcibase_sec_ctl_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseBarState *bs = opaque;
    PCIBaseState *s = bs->s;
    if (addr == 0) return s->sec_altstatus;
    return 0;
}

static void pcibase_sec_ctl_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseBarState *bs = opaque;
    PCIBaseState *s = bs->s;
    if (addr == 2) s->sec_devctrl = val;
}

static const MemoryRegionOps pcibase_sec_ctl_ops = {
    .read = pcibase_sec_ctl_read,
    .write = pcibase_sec_ctl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 1 },
    .impl  = { .min_access_size = 1, .max_access_size = 1 },
};

static uint64_t pcibase_bmdma_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseBarState *bs = opaque;
    PCIBaseState *s = bs->s;
    uint64_t val = 0;
    int ch = (addr < 8) ? 0 : 1;
    hwaddr offset = addr & 7;

    switch (offset) {
    case 0:
        if (size == 1) val = s->bmdma_cmd[ch];
        break;
    case 2:
        if (size == 1) val = s->bmdma_status[ch];
        break;
    case 4:
        if (size == 4) val = s->bmdma_prdt[ch];
        break;
    default:
        break;
    }
    return val;
}

static void pcibase_bmdma_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseBarState *bs = opaque;
    PCIBaseState *s = bs->s;
    int ch = (addr < 8) ? 0 : 1;
    hwaddr offset = addr & 7;

    switch (offset) {
    case 0:
        if (size == 1) {
            uint8_t old_cmd = s->bmdma_cmd[ch];
            s->bmdma_cmd[ch] = val;
            if (val & 0x01) {
                if (!(old_cmd & 0x01)) {
                    s->bmdma_active[ch] = true;
                    s->bmdma_status[ch] |= 0x01;
                    timer_mod(s->bmdma_timer[ch], qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 100000);
                }
            } else {
                if (old_cmd & 0x01) {
                    s->bmdma_active[ch] = false;
                    s->bmdma_status[ch] &= ~0x01;
                    timer_del(s->bmdma_timer[ch]);
                }
            }
        }
        break;
    case 2:
        if (size == 1) {
            if (val & 0x04) {
                s->bmdma_status[ch] &= ~0x04;
            }
            if (val & 0x02) {
                s->bmdma_status[ch] &= ~0x02;
            }
            if (val & 0x01) {
                s->bmdma_status[ch] &= ~0x01;
            }
            pcibase_update_bmdma_irq(s, ch);
        }
        break;
    case 4:
        if (size == 4) s->bmdma_prdt[ch] = val;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps pcibase_bmdma_ops = {
    .read = pcibase_bmdma_read,
    .write = pcibase_bmdma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_bmdma_timer0_cb(void *opaque)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);
    s->bmdma_active[0] = false;
    s->bmdma_status[0] &= ~0x01;
    s->bmdma_status[0] |= 0x04;
    pcibase_update_bmdma_irq(s, 0);
}

static void pcibase_bmdma_timer1_cb(void *opaque)
{
    PCIBaseState *s = PCIBASE_DEVICE(opaque);
    s->bmdma_active[1] = false;
    s->bmdma_status[1] &= ~0x01;
    s->bmdma_status[1] |= 0x04;
    pcibase_update_bmdma_irq(s, 1);
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (addr + len <= 256) {
        uint32_t val = 0;
        memcpy(&val, s->config_shadow + addr, MIN(len, sizeof(val)));
        return le32_to_cpu(val);
    }
    return pci_default_read_config(pdev, addr, len);
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *shadow = s->config_shadow;
    if (addr + len <= 256) {
        uint32_t le_val = cpu_to_le32(val);
        memcpy(shadow + addr, &le_val, len);
        pci_default_write_config(pdev, addr, val, len);
        memcpy(s->config_shadow, pdev->config, 256);
    } else {
        pci_default_write_config(pdev, addr, val, len);
    }
}

static void pcibase_update_irq(PCIBaseState *s)
{
    bool irq_raised = (s->bmdma_status[0] & 0x04) || (s->bmdma_status[1] & 0x04);
    pci_set_irq(PCI_DEVICE(s), irq_raised ? 1 : 0);
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->pri_cmd_regs, 0, sizeof(s->pri_cmd_regs));
    s->pri_altstatus = 0x00;
    s->pri_devctrl = 0;
    memset(s->sec_cmd_regs, 0, sizeof(s->sec_cmd_regs));
    s->sec_altstatus = 0x00;
    s->sec_devctrl = 0;

    memset(s->bmdma_cmd, 0, sizeof(s->bmdma_cmd));
    memset(s->bmdma_status, 0, sizeof(s->bmdma_status));
    s->bmdma_status[0] = 0x60;
    s->bmdma_status[1] = 0x60;
    s->bmdma_prdt[0] = s->bmdma_prdt[1] = 0;
    s->bmdma_active[0] = s->bmdma_active[1] = false;

    timer_del(s->bmdma_timer[0]);
    timer_del(s->bmdma_timer[1]);

    memcpy(s->config_shadow, PCI_DEVICE(s)->config, 256);
}

G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x1179);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x0101);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0101);
    pci_set_byte(pci_conf + PCI_CLASS_PROG, 0x85); /* Native mode, bus master capable */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    pci_set_word(pci_conf + PCI_COMMAND, PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);

    pdev->config_read = pcibase_config_read;
    pdev->config_write = pcibase_config_write;

    {
        PCIBaseBarState *bs = &s->bar_states[0];
        bs->s = s; bs->bar_type = BAR_PRI_CMD;
        memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_pri_cmd_ops, bs,
                              "pri-cmd", 8);
        pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[0]);
    }
    {
        PCIBaseBarState *bs = &s->bar_states[1];
        bs->s = s; bs->bar_type = BAR_PRI_CTL;
        memory_region_init_io(&s->bar_regions[1], OBJECT(s), &pcibase_pri_ctl_ops, bs,
                              "pri-ctl", 4);
        pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[1]);
    }
    {
        PCIBaseBarState *bs = &s->bar_states[2];
        bs->s = s; bs->bar_type = BAR_SEC_CMD;
        memory_region_init_io(&s->bar_regions[2], OBJECT(s), &pcibase_sec_cmd_ops, bs,
                              "sec-cmd", 8);
        pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[2]);
    }
    {
        PCIBaseBarState *bs = &s->bar_states[3];
        bs->s = s; bs->bar_type = BAR_SEC_CTL;
        memory_region_init_io(&s->bar_regions[3], OBJECT(s), &pcibase_sec_ctl_ops, bs,
                              "sec-ctl", 4);
        pci_register_bar(pdev, 3, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[3]);
    }
    {
        PCIBaseBarState *bs = &s->bar_states[4];
        bs->s = s; bs->bar_type = BAR_BMDMA;
        memory_region_init_io(&s->bar_regions[4], OBJECT(s), &pcibase_bmdma_ops, bs,
                              "bmdma", 16);
        pci_register_bar(pdev, 4, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[4]);
    }

    /* Update shadow after all BAR registrations to capture BAR registers */
    memcpy(s->config_shadow, pdev->config, 256);

    s->bmdma_timer[0] = timer_new_ns(QEMU_CLOCK_VIRTUAL, pcibase_bmdma_timer0_cb, s);
    s->bmdma_timer[1] = timer_new_ns(QEMU_CLOCK_VIRTUAL, pcibase_bmdma_timer1_cb, s);

    pcibase_reset(DEVICE(s));
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
    timer_del(s->bmdma_timer[0]);
    timer_free(s->bmdma_timer[0]);
    timer_del(s->bmdma_timer[1]);
    timer_free(s->bmdma_timer[1]);
}

static const VMStateDescription vmstate_pcibase = {
    .name = "pata_piccolo_pci",
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
