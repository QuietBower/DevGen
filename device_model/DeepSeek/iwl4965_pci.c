/*
 * QEMU PCI device model for Intel Wireless WiFi Link 4965AGN (iwl4965)
 * Generated from Linux driver 4965-mac.c, QEMU 8.2.10.
 *
 * This device emulates the CSR registers required for driver probe and initialization,
 * including EEPROM access, reset handling, and power management status.
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

#define TYPE_PCIBASE_DEVICE "iwl4965_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define VENDOR_ID 0x8086
#define DEVICE_ID 0x4229
#define CLASS_ID  0x0280

/* Register offsets from driver */
#define CSR_BASE                        0x000
#define CSR_HW_IF_CONFIG_REG            (CSR_BASE+0x000)
#define CSR_INT_COALESCING              (CSR_BASE+0x004)
#define CSR_INT                         (CSR_BASE+0x008)
#define CSR_INT_MASK                    (CSR_BASE+0x00c)
#define CSR_FH_INT_STATUS               (CSR_BASE+0x010)
#define CSR_RESET                       (CSR_BASE+0x020)
#define CSR_GP_CNTRL                    (CSR_BASE+0x024)
#define CSR_HW_REV                      (CSR_BASE+0x028)
#define CSR_EEPROM_REG                  (CSR_BASE+0x02c)
#define CSR_EEPROM_GP                   (CSR_BASE+0x030)
#define CSR_GIO_CHICKEN_BITS            (CSR_BASE+0x100)
#define CSR_ANA_PLL_CFG                 (CSR_BASE+0x20c)
#define CSR_DBG_HPET_MEM_REG            (CSR_BASE+0x240)
#define CSR_UCODE_DRV_GP1               (CSR_BASE+0x054)
#define CSR_UCODE_DRV_GP1_SET           (CSR_BASE+0x058)
#define CSR_UCODE_DRV_GP1_CLR           (CSR_BASE+0x05c)

/* Important bit masks */
#define CSR_HW_IF_CONFIG_REG_BIT_NIC_READY         (0x00400000)
#define CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY      (0x00000001)
#define CSR_GP_CNTRL_REG_FLAG_INIT_DONE            (0x00000004)
#define CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ       (0x00000008)
#define CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW        (0x08000000)
#define CSR_INT_BIT_ALIVE                          (1 << 0)
#define CSR_INT_BIT_WAKEUP                         (1 << 1)
#define CSR_INT_BIT_SW_RX                          (1 << 3)
#define CSR_INT_BIT_CT_KILL                        (1 << 6)
#define CSR_INT_BIT_RF_KILL                        (1 << 7)
#define CSR_INT_BIT_SW_ERR                         (1 << 25)
#define CSR_INT_BIT_SCD                            (1 << 26)
#define CSR_INT_BIT_FH_TX                          (1 << 27)
#define CSR_INT_BIT_HW_ERR                         (1 << 29)
#define CSR_INT_BIT_FH_RX                          (1 << 31)
#define CSR_FH_INT_BIT_TX_CHNL0                    (1 << 0)
#define CSR_FH_INT_BIT_TX_CHNL1                    (1 << 1)
#define CSR_FH_INT_BIT_RX_CHNL0                    (1 << 16)
#define CSR_FH_INT_BIT_RX_CHNL1                    (1 << 17)
#define CSR_FH_INT_BIT_HI_PRIOR                    (1 << 30)

/* EEPROM constants (corrected from supplementary source) */
#define CSR_EEPROM_REG_MSK_ADDR      0x0000FFFC
#define CSR_EEPROM_REG_READ_VALID_MSK 0x00000001
#define CSR_EEPROM_GP_VALID_MSK           0x00000007
#define CSR_EEPROM_GP_GOOD_SIG_EEP_LESS_THAN_4K  0x00000002
#define CSR_EEPROM_GP_GOOD_SIG_EEP_MORE_THAN_4K  0x00000004

/* Reset flags */
#define CSR_RESET_REG_FLAG_NEVO_RESET  0x00000001

#define PCI_CFG_RETRY_TIMEOUT          0x041

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
    uint32_t int_status;
    uint32_t int_mask;

    struct {
        uint32_t hw_if_config;
        uint32_t int_status;
        uint32_t int_mask;
        uint32_t fh_int_status;
        uint32_t reset;
        uint32_t gp_cntrl;
        uint32_t ucode_drv_gp1;
        uint32_t hw_rev;
        uint32_t eeprom_reg;
        uint32_t eeprom_gp;
        uint32_t int_coalescing;
        uint32_t ana_pll_cfg;
        uint32_t gio_chicken_bits;
        uint32_t dbg_hpet_mem_reg;
    } regs;

    uint32_t status;
    bool pm_enabled;

    uint8_t eeprom_data[1024];
    uint32_t eeprom_addr;
    bool eeprom_read_valid;
};

static void pcibase_reset(DeviceState *dev);

static void pcibase_update_irq(PCIBaseState *s)
{
    /* No IRQ logic needed for probe */
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    /* Allow any access size */
    switch (addr) {
    case CSR_HW_IF_CONFIG_REG:
        val = s->regs.hw_if_config;
        break;
    case CSR_INT_COALESCING:
        val = s->regs.int_coalescing;
        break;
    case CSR_INT:
        val = s->regs.int_status;
        break;
    case CSR_INT_MASK:
        val = s->regs.int_mask;
        break;
    case CSR_FH_INT_STATUS:
        val = s->regs.fh_int_status;
        break;
    case CSR_RESET:
        val = 0;  /* always idle after processing */
        break;
    case CSR_GP_CNTRL:
        val = s->regs.gp_cntrl;
        break;
    case CSR_HW_REV:
        val = s->regs.hw_rev;
        break;
    case CSR_EEPROM_REG:
        if (s->eeprom_addr < sizeof(s->eeprom_data) &&
            s->eeprom_addr + 1 < sizeof(s->eeprom_data)) {
            uint16_t word;
            memcpy(&word, s->eeprom_data + s->eeprom_addr, sizeof(word));
            val = ((uint32_t)word << 16) | CSR_EEPROM_REG_READ_VALID_MSK;
            s->eeprom_read_valid = true;
        } else {
            val = 0;
        }
        break;
    case CSR_EEPROM_GP:
        /* Always return the good signature to satisfy the driver */
        val = CSR_EEPROM_GP_GOOD_SIG_EEP_LESS_THAN_4K;
        break;
    case CSR_ANA_PLL_CFG:
        val = s->regs.ana_pll_cfg;
        break;
    case CSR_GIO_CHICKEN_BITS:
        val = s->regs.gio_chicken_bits;
        break;
    case CSR_DBG_HPET_MEM_REG:
        val = s->regs.dbg_hpet_mem_reg;
        break;
    case CSR_UCODE_DRV_GP1:
        val = s->regs.ucode_drv_gp1;
        break;
    case CSR_UCODE_DRV_GP1_SET:
        val = s->regs.ucode_drv_gp1;
        break;
    case CSR_UCODE_DRV_GP1_CLR:
        val = s->regs.ucode_drv_gp1;
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case CSR_HW_IF_CONFIG_REG:
        /* NIC_READY must be always set to keep the device operational */
        s->regs.hw_if_config = val | CSR_HW_IF_CONFIG_REG_BIT_NIC_READY;
        break;
    case CSR_INT_COALESCING:
        s->regs.int_coalescing = val;
        break;
    case CSR_INT:
        s->regs.int_status &= ~val;
        pcibase_update_irq(s);
        break;
    case CSR_INT_MASK:
        s->regs.int_mask = val;
        break;
    case CSR_FH_INT_STATUS:
        s->regs.fh_int_status &= ~val;
        break;
    case CSR_RESET:
        /* Trigger full device reset on NEVO reset bit */
        if (val & CSR_RESET_REG_FLAG_NEVO_RESET) {
            pcibase_reset(DEVICE(s));
        }
        /* Clear register to prevent timeouts on master disable etc. */
        s->regs.reset = 0;
        break;
    case CSR_GP_CNTRL:
        s->regs.gp_cntrl = val;
        break;
    case CSR_EEPROM_REG:
        s->eeprom_addr = val & CSR_EEPROM_REG_MSK_ADDR;
        s->eeprom_read_valid = false;
        break;
    case CSR_EEPROM_GP:
        /* Read-only register, ignore writes */
        break;
    case CSR_ANA_PLL_CFG:
        s->regs.ana_pll_cfg = val;
        break;
    case CSR_GIO_CHICKEN_BITS:
        s->regs.gio_chicken_bits = val;
        break;
    case CSR_DBG_HPET_MEM_REG:
        s->regs.dbg_hpet_mem_reg = val;
        break;
    case CSR_UCODE_DRV_GP1:
        s->regs.ucode_drv_gp1 = val;
        break;
    case CSR_UCODE_DRV_GP1_SET:
        s->regs.ucode_drv_gp1 |= val;
        break;
    case CSR_UCODE_DRV_GP1_CLR:
        s->regs.ucode_drv_gp1 &= ~val;
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

    memset(&s->regs, 0, sizeof(s->regs));
    s->regs.hw_if_config = CSR_HW_IF_CONFIG_REG_BIT_NIC_READY;
    s->regs.gp_cntrl = CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY |
                       CSR_GP_CNTRL_REG_FLAG_INIT_DONE |
                       CSR_GP_CNTRL_REG_FLAG_HW_RF_KILL_SW;
    s->regs.hw_rev = 0x00;

    memset(s->eeprom_data, 0, sizeof(s->eeprom_data));
    s->eeprom_data[0] = 0x5A;
    s->eeprom_data[1] = 0x5A;
    s->regs.eeprom_gp = CSR_EEPROM_GP_GOOD_SIG_EEP_LESS_THAN_4K;
    s->eeprom_addr = 0;
    s->eeprom_read_valid = false;
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
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x2000;
    s->bar_info[0].name = "iwl4965-mmio";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    pcibase_reset(DEVICE(s));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "iwl4965_pci",
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
