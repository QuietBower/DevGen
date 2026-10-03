/*
 * QEMU device model for IDT 89HPES24NT6AG2 NTB (Non-Transparent Bridge)
 * Based on Linux driver /home/eely/linux-7.1/drivers/ntb/hw/idt/ntb_hw_idt.c
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
#include "qemu/error-report.h"

#define TYPE_PCIBASE_DEVICE "ntb_hw_idt_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* ========== NTB register definitions from driver ========== */
/* PCI IDs */
#define PCI_VENDOR_ID_IDT           0x111d
#define PCI_DEVICE_ID_IDT_89HPES24NT6AG2  0x8091

#define IDT_NT_BARSETUP0        0x00470U
#define IDT_NT_PCIELCAP         0x0004cU
#define IDT_NT_NTCTL            0x00400U
#define IDT_NTCTL_CPEN          0x00000002U
#define IDT_NT_REQIDCAP         0x004dcU
#define IDT_NT_NTMTBLADDR       0x004d0U
#define IDT_NT_NTMTBLDATA       0x004d8U
#define IDT_NTMTBLDATA_VALID    0x00000001U
#define IDT_NT_NTGSIGNAL        0x00410U
#define IDT_NTGSIGNAL_SET       0x00000001U
#define IDT_NT_NTINTSTS         0x00404U
#define IDT_NTINTSTS_SEVENT     0x00000008U
#define IDT_NTINTSTS_MSG        0x00000001U
#define IDT_NTINTSTS_DBELL      0x00000002U
#define IDT_NT_NTINTMSK         0x00408U
#define IDT_NTINTMSK_SEVENT     0x00000008U
#define IDT_NTINTMSK_MSG        0x00000001U
#define IDT_NTINTMSK_DBELL      0x00000002U
#define IDT_NTINTMSK_ALL \
    (IDT_NTINTMSK_MSG | IDT_NTINTMSK_DBELL | IDT_NTINTMSK_SEVENT)
#define IDT_NT_INDBELLSTS       0x00428U
#define IDT_NT_INDBELLMSK       0x0042cU
#define IDT_NT_OUTDBELLSET      0x00420U
#define IDT_NT_MSGSTS           0x00460U
#define IDT_NT_MSGSTSMSK        0x00464U
#define IDT_MSG_MASK            ((u32)0x000f000fU)
#define IDT_NT_GASADATA         0x00ffcU
#define IDT_NT_GASAADDR         0x00ff8U
#define IDT_BARSETUP_EN         0x80000000U
#define IDT_BARSETUP_MODE_CFG   0x00000400U
#define IDT_BARSETUP_SIZE_MASK  0x000003f0U
#define IDT_BARSETUP_SIZE_CFG   0x000000c0U
#define IDT_REG_PCI_MAX         0x00fffU
#define IDT_REG_ALIGN           4
#define IDT_REG_SW_MAX          0x3ffffU
#define IDT_SW_SWPORT0STATUS    0x3e204U
#define IDT_SW_SWPORT2STATUS    0x3e244U
#define IDT_SW_SWPART0STATUS    0x3e104U

/* Register field definitions from supplementary driver source */
#define IDT_SWPORTxSTS_SWPART_MASK  0x00001C00U
#define IDT_SWPORTxSTS_SWPART_FLD   10
#define IDT_SWPORTxSTS_MODE_MASK    0x000003C0U
#define IDT_SWPORTxSTS_MODE_FLD     6
#define IDT_SWPORTxSTS_MODE_NT      0x000000C0U
#define IDT_SWPORTxSTS_MODE_USNT    0x00000100U
#define IDT_SWPORTxSTS_MODE_USNTDMA 0x000001C0U
#define IDT_SWPORTxSTS_MODE_NTDMA   0x00000200U
#define IDT_SWPARTxSTS_STATE_MASK   0x00000060U
#define IDT_SWPARTxSTS_STATE_FLD    5
#define IDT_SWPARTxSTS_STATE_ACT    0x00000020U
#define IDT_PCIELCAP_PORTNUM_MASK   0xFF000000U
#define IDT_PCIELCAP_PORTNUM_FLD    24
#define IDT_NTMTBLDATA_REQID_MASK   0x0001FFFEU
#define IDT_NTMTBLDATA_REQID_FLD    1
#define IDT_NTMTBLDATA_PART_MASK    0x000E0000U
#define IDT_NTMTBLDATA_PART_FLD     17
#define IDT_NTMTBLDATA_VALID        0x00000001U

#define GET_FIELD(field, data) \
    (((u32)(data) & IDT_##field##_MASK) >> IDT_##field##_FLD)
#define SET_FIELD(field, data, value) \
    (((u32)(data) & ~IDT_##field##_MASK) | \
     ((u32)(value) << IDT_##field##_FLD))
#define IS_FLD_SET(field, data, value) \
    (((u32)(data) & IDT_##field##_MASK) == IDT_##field##_##value)

/* BAR configuration */
#define BAR0_SIZE (256 * KiB)   /* Must be at least 256KB to map all registers up to IDT_REG_SW_MAX */
#define NT_REGS_SIZE (0x10000)  /* 65536 dwords = 256KB combined NT/SW register space */

typedef struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar0;

    uint32_t sw_addr;    /* GASA address register */
    uint32_t regs[NT_REGS_SIZE]; /* register file: 256KB dwords */
} PCIBaseState;

static void pcibase_update_irq(PCIBaseState *s)
{
    uint32_t int_mask = s->regs[IDT_NT_NTINTMSK >> 2];
    uint32_t int_status = s->regs[IDT_NT_NTINTSTS >> 2];
    bool raise;

    /* if any unmasked interrupt status bit is set, raise IRQ */
    raise = (int_status & ~int_mask) != 0;

    if (msi_enabled(&s->parent_obj)) {
        if (raise) {
            msi_notify(&s->parent_obj, 0);
        }
    } else {
        pci_set_irq(&s->parent_obj, raise ? 1 : 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read from invalid address 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return ~0ULL;
    }

    switch (addr) {
    case IDT_NT_GASAADDR:
        val = s->sw_addr;
        break;
    case IDT_NT_GASADATA:
        if ((s->sw_addr & 3) == 0) {
            uint32_t idx = s->sw_addr >> 2;
            if (idx < ARRAY_SIZE(s->regs)) {
                val = s->regs[idx];
            } else {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: GASA read beyond regs: sw_addr=0x%x\n",
                              __func__, s->sw_addr);
                val = 0xFFFFFFFF;
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: GASA read unaligned sw_addr=0x%x\n",
                          __func__, s->sw_addr);
            val = 0xFFFFFFFF;
        }
        break;
    default:
        if ((addr & 3) == 0) {
            uint32_t idx = addr >> 2;
            if (idx < ARRAY_SIZE(s->regs)) {
                val = s->regs[idx];
            } else {
                val = 0xFFFFFFFF;
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: unaligned read at 0x%" HWADDR_PRIx "\n",
                          __func__, addr);
            val = 0xFFFFFFFF;
        }
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to invalid address 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return;
    }

    switch (addr) {
    case IDT_NT_GASAADDR:
        s->sw_addr = (uint32_t)val;
        break;
    case IDT_NT_GASADATA:
        if ((s->sw_addr & 3) == 0) {
            uint32_t idx = s->sw_addr >> 2;
            if (idx < ARRAY_SIZE(s->regs)) {
                s->regs[idx] = (uint32_t)val;
            } else {
                qemu_log_mask(LOG_GUEST_ERROR,
                              "%s: GASA write beyond regs: sw_addr=0x%x val=0x%" PRIx64 "\n",
                              __func__, s->sw_addr, val);
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: GASA write unaligned sw_addr=0x%x\n",
                          __func__, s->sw_addr);
        }
        break;
    case IDT_NT_NTINTSTS:
        /* W1C: write 1 to clear */
        s->regs[IDT_NT_NTINTSTS >> 2] &= ~((uint32_t)val);
        pcibase_update_irq(s);
        break;
    case IDT_NT_INDBELLSTS:
        s->regs[IDT_NT_INDBELLSTS >> 2] &= ~((uint32_t)val);
        pcibase_update_irq(s);
        break;
    case IDT_NT_MSGSTS:
        s->regs[IDT_NT_MSGSTS >> 2] &= ~((uint32_t)val);
        pcibase_update_irq(s);
        break;
    default:
        if ((addr & 3) == 0) {
            uint32_t idx = addr >> 2;
            if (idx < ARRAY_SIZE(s->regs)) {
                s->regs[idx] = (uint32_t)val;
            }
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: unaligned write at 0x%" HWADDR_PRIx "\n",
                          __func__, addr);
        }
        break;
    }

    /* Handle interrupt status changes (if any status bit set via writes) */
    pcibase_update_irq(s);
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 4 },
    .impl  = { .min_access_size = 4, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);
    pci_device_reset(pdev);
    memset(s->regs, 0, sizeof(s->regs));
    s->sw_addr = 0;

    /* Set up power-on defaults for required registers */
    /* BAR0 setup: enabled, configuration space with expected size */
    uint32_t barsetup0_val = IDT_BARSETUP_EN | IDT_BARSETUP_MODE_CFG |
                             (IDT_BARSETUP_SIZE_CFG & IDT_BARSETUP_SIZE_MASK);
    s->regs[IDT_NT_BARSETUP0 >> 2] = barsetup0_val;
    /* Also set directly in PCI config space for default config reads */
    pci_set_long(pdev->config + IDT_NT_BARSETUP0, barsetup0_val);

    /* PCIe capability: port number to 0 */
    s->regs[IDT_NT_PCIELCAP >> 2] = 0x0;

    /* SW registers accessible via GASA: set up local port 0 status (partition 0) */
    {
        uint32_t port0_val = 0;
        port0_val = (port0_val & ~IDT_SWPORTxSTS_SWPART_MASK) | (0 << IDT_SWPORTxSTS_SWPART_FLD);
        s->regs[IDT_SW_SWPORT0STATUS >> 2] = port0_val;
    }

    /* Set peer port 2 status: mode = NT, partition = 0 */
    {
        uint32_t port2_val = 0;
        port2_val = (port2_val & ~(IDT_SWPORTxSTS_SWPART_MASK | IDT_SWPORTxSTS_MODE_MASK))
                    | (0 << IDT_SWPORTxSTS_SWPART_FLD)
                    | IDT_SWPORTxSTS_MODE_NT;
        s->regs[IDT_SW_SWPORT2STATUS >> 2] = port2_val;
    }

    /* Mark partition 0 as active */
    {
        uint32_t part0_val = 0;
        part0_val = (part0_val & ~IDT_SWPARTxSTS_STATE_MASK) | IDT_SWPARTxSTS_STATE_ACT;
        s->regs[IDT_SW_SWPART0STATUS >> 2] = part0_val;
    }
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t address, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
 
    /* Intercept reads to the extended configuration register BARSETUP0 (offset 0x470) */
    if (address + len <= IDT_NT_BARSETUP0 + 4 && address >= IDT_NT_BARSETUP0) {
        uint32_t val = s->regs[IDT_NT_BARSETUP0 >> 2];
        uint32_t shift = (address - IDT_NT_BARSETUP0) * 8;
        return (val >> shift) & ((1ULL << (len * 8)) - 1);
    }

    /* Fall back to default handling for standard and other extended registers */
    return pci_default_read_config(pdev, address, len);
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t address, uint32_t val, int len)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    /* Intercept writes to BARSETUP0 to keep the internal state in sync */
    if (address + len <= IDT_NT_BARSETUP0 + 4 && address >= IDT_NT_BARSETUP0) {
        uint32_t shift = (address - IDT_NT_BARSETUP0) * 8;
        uint32_t mask = ((1ULL << (len * 8)) - 1) << shift;
        uint32_t old = s->regs[IDT_NT_BARSETUP0 >> 2];
        s->regs[IDT_NT_BARSETUP0 >> 2] = (old & ~mask) | ((val << shift) & mask);
        /* Also update the config space byte array to stay consistent */
        uint32_t new_val = s->regs[IDT_NT_BARSETUP0 >> 2];
        pci_set_long(pdev->config + IDT_NT_BARSETUP0, new_val);
        return;
    }

    /* Default write for all other registers */
    pci_default_write_config(pdev, address, val, len);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_IDT);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_IDT_89HPES24NT6AG2);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0680);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Initialize MSI */
    if (msi_init(pdev, 0, 1, true, false, errp) != 0) {
        error_report("Failed to initialize MSI");
        return;
    }

    /* Register BAR0 as memory region for configuration space */
    memory_region_init_io(&s->bar0, OBJECT(s), &pcibase_mmio_ops, s,
                          "ntb-bar0", BAR0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);

    /* Force the config read/write handlers to our implementations */
    pdev->config_read = pcibase_config_read;
    pdev->config_write = pcibase_config_write;

    pcibase_reset(DEVICE(s));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    msi_uninit(pdev);
}

static const VMStateDescription vmstate_pcibase = {
    .name = "ntb_hw_idt_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32_ARRAY(regs, PCIBaseState, NT_REGS_SIZE),
        VMSTATE_UINT32(sw_addr, PCIBaseState),
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
    k->config_read = pcibase_config_read;
    k->config_write = pcibase_config_write;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo pcibase_info = {
    .name          = TYPE_PCIBASE_DEVICE,
    .parent        = TYPE_PCI_DEVICE,
    .instance_size = sizeof(PCIBaseState),
    .class_init    = pcibase_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void pcibase_register_types(void)
{
    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types)
