/*
 * QEMU PCI device model for Realtek RTL8192DE WLAN controller.
 * Generated based on driver source: rtl8192de/sw.c
 * Implements basic MMIO register file with interrupt support.
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

#define TYPE_PCIBASE_DEVICE "rtl8192de_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers from driver source */
#define PCI_VENDOR_ID_REALTEK 0x10EC
#define DEVICE_ID_8193 0x8193
#define CLASS_ID 0x0280

/* Register offsets from driver source */
#define REG_SYS_ISO_CTRL           0x0000
#define REG_SYS_FUNC_EN            0x0002
#define REG_SYS_CLKR               0x0008
#define REG_CR                     0x00
#define REG_MAC_PHY_CTRL_NORMAL    0x00f8
#define REG_DBI_FLAG               0x0352
#define REG_DBI_RDATA              0x034C
#define REG_DBI_CTRL               0x0350
#define REG_MAC1                   0x0053
#define REG_MAC0                   0x0081
#define REG_POWER_OFF_IN_PROCESS   0x0017
#define REG_DBI_WDATA              0x0348
#define REG_MAX_AGGR_NUM           0x04CA
#define REG_EARLY_MODE_CONTROL     0x4D0
#define REG_FTIMR                  0x0138
#define REG_RXPKT_NUM              0x0284
#define REG_ISR                    0xa4
#define REG_IMR                    0x1604
#define REG_IMR1                   0x1608
#define REG_VERSION                0x01
#define REG_EFUSE_CTRL             0x0030
#define REG_EFUSE_TEST             0x0034

/* Additional register aliases from supplementary source */
#define REG_HIMR   REG_IMR      /* Host Interrupt Mask Register */
#define REG_HIMRE  REG_IMR1     /* Host Interrupt Mask Register Extension */

/* Interrupt mask bits */
#define IMR_ROK                 BIT(0)
#define IMR_VODOK               BIT(1)
#define IMR_VIDOK               BIT(2)
#define IMR_BEDOK               BIT(3)
#define IMR_BKDOK               BIT(4)
#define IMR_HIGHDOK             BIT(5)
#define IMR_TBDOK               BIT(6)
#define IMR_TBDER               BIT(7)
#define IMR_MGNTDOK             BIT(8)
#define IMR_BDOK                BIT(9)
#define IMR_ATIMEND             BIT(10)
#define IMR_RDU                 BIT(11)
#define IMR_RXFOVW              BIT(12)
#define IMR_BCNINT              BIT(13)
#define IMR_PSTIMEOUT           BIT(14)
#define IMR_TXFOVW              BIT(15)
#define IMR_TIMEOUT1            BIT(16)
#define IMR_TIMEOUT2            BIT(17)
#define IMR_BCNDOK1             BIT(18)
#define IMR_BCNDOK2             BIT(19)
#define IMR_BCNDOK3             BIT(20)
#define IMR_BCNDOK4             BIT(21)
#define IMR_BCNDOK5             BIT(22)
#define IMR_BCNDOK6             BIT(23)
#define IMR_BCNDOK7             BIT(24)
#define IMR_BCNDOK8             BIT(25)
#define IMR_BCNDMAINT1          BIT(26)
#define IMR_BCNDMAINT2          BIT(27)
#define IMR_BCNDMAINT3          BIT(28)
#define IMR_BCNDMAINT4          BIT(29)
#define IMR_BCNDMAINT5          BIT(30)
#define IMR_BCNDMAINT6          BIT(31)

/* Additional definitions from supplementary source */
#define IMR_CPWM                BIT(8)
#define IMR_C2HCMD              BIT(9)

#define PWC_EV12V               BIT(15)
#define FEN_ELDR                BIT(12)
#define LOADER_CLK_EN           BIT(5)
#define SYS_CLK                 0x03
#define RCR_AM                  BIT(2)
#define RCR_AB                  BIT(3)
#define RCR_ACRC32              BIT(8)
#define RCR_ACF                 BIT(12)
#define RCR_AAP                 BIT(0)

#define BAR0_SIZE               0x2000  /* Size for BAR2 */

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_mmio;
    
    /* Interrupt state */
    uint32_t imr[2];      /* interrupt mask registers (IMR0 at 0x1604, IMR1 at 0x1608) */
    uint32_t isr;         /* interrupt status register (0xa4) */
    
    /* Flat register file for all other registers */
    uint8_t regs[BAR0_SIZE];
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t enabled_ints = (s->isr & s->imr[0]); /* Only IMR0 is considered for ISR */

    if (enabled_ints && msi_enabled(pdev)) {
        msi_notify(pdev, 0);
    } else if (!enabled_ints && msi_enabled(pdev)) {
        /* With MSI, no explicit deassertion needed; lowering edge not required */
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= BAR0_SIZE || addr + size > BAR0_SIZE) {
        return ~0ULL;
    }

    switch (addr) {
    case REG_ISR:
        if (size == 4) memcpy(&val, &s->isr, 4);
        else val = (uint8_t)(s->isr >> ((addr & 3) * 8)); /* sub-dword access */
        break;
    case REG_HIMR:
        if (size == 4) memcpy(&val, &s->imr[0], 4);
        else val = (uint8_t)(s->imr[0] >> ((addr & 3) * 8));
        break;
    case REG_HIMRE:
        if (size == 4) memcpy(&val, &s->imr[1], 4);
        else val = (uint8_t)(s->imr[1] >> ((addr & 3) * 8));
        break;
    default:
        /* Generic register read from flat array */
        if (size == 1) {
            val = s->regs[addr];
        } else if (size == 2) {
            val = lduw_le_p(&s->regs[addr]);
        } else if (size == 4) {
            val = ldl_le_p(&s->regs[addr]);
        } else if (size == 8) {
            val = ldq_le_p(&s->regs[addr]);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid size %u at addr 0x%"HWADDR_PRIx"\n", __func__, size, addr);
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= BAR0_SIZE || addr + size > BAR0_SIZE) {
        return;
    }

    switch (addr) {
    case REG_ISR:
        /* Write-1-to-clear */
        if (size == 4) {
            uint32_t cv = val;
            s->isr &= ~cv;
        } else {
            uint32_t mask = (0xff << ((addr & 3) * 8)) & (0xffffffff >> (4 - size) * 8);
            s->isr &= ~(((uint32_t)val) << ((addr & 3) * 8));
        }
        pcibase_update_irq(s);
        break;
    case REG_HIMR:
        if (size == 4) {
            memcpy(&s->imr[0], &val, 4);
        } else {
            /* sub-dword write to IMR: implement partial update */
            uint64_t offset = (addr & 3) * 8;
            uint64_t mask = ((1ULL << (size * 8)) - 1) << offset;
            s->imr[0] = (s->imr[0] & ~mask) | ((val << offset) & mask);
        }
        pcibase_update_irq(s);
        break;
    case REG_HIMRE:
        if (size == 4) {
            memcpy(&s->imr[1], &val, 4);
        } else {
            uint64_t offset = (addr & 3) * 8;
            uint64_t mask = ((1ULL << (size * 8)) - 1) << offset;
            s->imr[1] = (s->imr[1] & ~mask) | ((val << offset) & mask);
        }
        pcibase_update_irq(s);
        break;
    default:
        /* Generic register write to flat array */
        if (size == 1) {
            s->regs[addr] = (uint8_t)val;
        } else if (size == 2) {
            stw_le_p(&s->regs[addr], (uint16_t)val);
        } else if (size == 4) {
            stl_le_p(&s->regs[addr], (uint32_t)val);
        } else if (size == 8) {
            stq_le_p(&s->regs[addr], val);
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid size %u at addr 0x%"HWADDR_PRIx"\n", __func__, size, addr);
        }
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->regs, 0, sizeof(s->regs));
    s->isr = 0;
    s->imr[0] = 0;
    s->imr[1] = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_REALTEK);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID_8193);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    msi_init(pdev, 0, 1, true, false, errp);

    memory_region_init_io(&s->bar_mmio, OBJECT(s), &pcibase_mmio_ops, s,
                          "rtl8192de-mem", BAR0_SIZE);
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_mmio);
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
}

static const VMStateDescription vmstate_pcibase = {
    .name = "rtl8192de_pci",
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
