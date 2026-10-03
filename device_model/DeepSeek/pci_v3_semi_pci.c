/*
 * QEMU model for V3 Semiconductor PCI bridge (pci-v3-semi)
 *
 * This device emulates the V3 PCI controller as seen by the pci-v3-semi driver.
 * It provides a register BAR and a config space BAR, and implements the
 * register-level behavior required for the driver to probe successfully.
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
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "pci_v3_semi_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define V3_PCI_VENDOR		0x00000000
#define V3_PCI_DEVICE		0x00000002
#define V3_PCI_CMD		0x00000004
#define V3_PCI_STAT		0x00000006
#define V3_PCI_CC_REV		0x00000008
#define V3_PCI_HDR_CFG		0x0000000C
#define V3_PCI_IO_BASE		0x00000010
#define V3_PCI_BASE0		0x00000014
#define V3_PCI_BASE1		0x00000018
#define V3_PCI_SUB_VENDOR		0x0000002C
#define V3_PCI_SUB_ID		0x0000002E
#define V3_PCI_ROM		0x00000030
#define V3_PCI_BPARAM		0x0000003C
#define V3_PCI_MAP0		0x00000040
#define V3_PCI_MAP1		0x00000044
#define V3_PCI_INT_STAT		0x00000048
#define V3_PCI_INT_CFG		0x0000004C
#define V3_LB_BASE0		0x00000054
#define V3_LB_BASE1		0x00000058
#define V3_LB_MAP0		0x0000005E
#define V3_LB_MAP1		0x00000062
#define V3_LB_BASE2		0x00000064
#define V3_LB_MAP2		0x00000066
#define V3_LB_SIZE		0x00000068
#define V3_LB_IO_BASE		0x0000006E
#define V3_FIFO_CFG		0x00000070
#define V3_FIFO_PRIORITY		0x00000072
#define V3_FIFO_STAT		0x00000074
#define V3_LB_ISTAT		0x00000076
#define V3_LB_IMASK		0x00000077
#define V3_SYSTEM		0x00000078
#define V3_LB_CFG		0x0000007A
#define V3_PCI_CFG		0x0000007C
#define V3_DMA_PCI_ADR0		0x00000080
#define V3_DMA_PCI_ADR1		0x00000090
#define V3_DMA_LOCAL_ADR0		0x00000084
#define V3_DMA_LOCAL_ADR1		0x00000094
#define V3_DMA_LENGTH0		0x00000088
#define V3_DMA_LENGTH1		0x00000098
#define V3_DMA_CSR0		0x0000008B
#define V3_DMA_CSR1		0x0000009B
#define V3_DMA_CTLB_ADR0		0x0000008C
#define V3_DMA_CTLB_ADR1		0x0000009C
#define V3_DMA_DELAY		0x000000E0
#define V3_MAIL_DATA		0x000000C0
#define V3_PCI_MAIL_IEWR		0x000000D0
#define V3_PCI_MAIL_IERD		0x000000D2
#define V3_LB_MAIL_IEWR		0x000000D4
#define V3_LB_MAIL_IERD		0x000000D6
#define V3_MAIL_WR_STAT		0x000000D8
#define V3_MAIL_RD_STAT		0x000000DA
#define V3_QBA_MAP		0x000000DC

#define V3_PCI_STAT_PAR_ERR		BIT(15)
#define V3_PCI_STAT_SYS_ERR		BIT(14)
#define V3_PCI_STAT_M_ABORT_ERR		BIT(13)
#define V3_PCI_STAT_T_ABORT_ERR		BIT(12)

#define V3_LB_ISTAT_MAILBOX		BIT(7)
#define V3_LB_ISTAT_PCI_RD		BIT(6)
#define V3_LB_ISTAT_PCI_WR		BIT(5)
#define V3_LB_ISTAT_PCI_INT		BIT(4)
#define V3_LB_ISTAT_PCI_PERR		BIT(3)
#define V3_LB_ISTAT_I2O_QWR		BIT(2)
#define V3_LB_ISTAT_DMA1		BIT(1)
#define V3_LB_ISTAT_DMA0		BIT(0)

#define V3_COMMAND_M_FBB_EN		BIT(9)
#define V3_COMMAND_M_SERR_EN		BIT(8)
#define V3_COMMAND_M_PAR_EN		BIT(6)
#define V3_COMMAND_M_MASTER_EN		BIT(2)
#define V3_COMMAND_M_MEM_EN		BIT(1)
#define V3_COMMAND_M_IO_EN		BIT(0)

#define V3_SYSTEM_M_RST_OUT		BIT(15)
#define V3_SYSTEM_M_LOCK		BIT(14)
#define V3_SYSTEM_UNLOCK		0xa05f

#define V3_PCI_CFG_M_I2O_EN		BIT(15)
#define V3_PCI_CFG_M_IO_REG_DIS		BIT(14)
#define V3_PCI_CFG_M_IO_DIS		BIT(13)
#define V3_PCI_CFG_M_EN3V		BIT(12)
#define V3_PCI_CFG_M_RETRY_EN		BIT(10)
#define V3_PCI_CFG_M_AD_LOW1		BIT(9)
#define V3_PCI_CFG_M_AD_LOW0		BIT(8)
#define V3_PCI_CFG_M_RTYPE_SHIFT		5
#define V3_PCI_CFG_M_WTYPE_SHIFT		1
#define V3_PCI_CFG_TYPE_DEFAULT		0x3

#define V3_PCI_BASE_M_ADR_BASE		0xFFF00000U
#define V3_PCI_BASE_M_ADR_BASEL		0x000FFF00U
#define V3_PCI_BASE_M_PREFETCH		BIT(3)
#define V3_PCI_BASE_M_TYPE		(3 << 1)
#define V3_PCI_BASE_M_IO		BIT(0)

#define V3_PCI_MAP_M_MAP_ADR		0xFFF00000U
#define V3_PCI_MAP_M_RD_POST_INH		BIT(15)
#define V3_PCI_MAP_M_ROM_SIZE		(3 << 10)
#define V3_PCI_MAP_M_SWAP		(3 << 8)
#define V3_PCI_MAP_M_ADR_SIZE		0x000000F0U
#define V3_PCI_MAP_M_REG_EN		BIT(1)
#define V3_PCI_MAP_M_ENABLE		BIT(0)

#define V3_LB_BASE_ADR_BASE		0xfff00000U
#define V3_LB_BASE_SWAP		(3 << 8)
#define V3_LB_BASE_ADR_SIZE		(15 << 4)
#define V3_LB_BASE_PREFETCH		BIT(3)
#define V3_LB_BASE_ENABLE		BIT(0)
#define V3_LB_BASE_ADR_SIZE_1MB		(0 << 4)
#define V3_LB_BASE_ADR_SIZE_2MB		(1 << 4)
#define V3_LB_BASE_ADR_SIZE_4MB		(2 << 4)
#define V3_LB_BASE_ADR_SIZE_8MB		(3 << 4)
#define V3_LB_BASE_ADR_SIZE_16MB		(4 << 4)
#define V3_LB_BASE_ADR_SIZE_32MB		(5 << 4)
#define V3_LB_BASE_ADR_SIZE_64MB		(6 << 4)
#define V3_LB_BASE_ADR_SIZE_128MB		(7 << 4)
#define V3_LB_BASE_ADR_SIZE_256MB		(8 << 4)
#define V3_LB_BASE_ADR_SIZE_512MB		(9 << 4)
#define V3_LB_BASE_ADR_SIZE_1GB		(10 << 4)
#define V3_LB_BASE_ADR_SIZE_2GB		(11 << 4)
#define v3_addr_to_lb_base(a)	((a) & V3_LB_BASE_ADR_BASE)

#define V3_LB_MAP_MAP_ADR		0xfff0U
#define V3_LB_MAP_TYPE		(7 << 1)
#define V3_LB_MAP_AD_LOW_EN		BIT(0)
#define V3_LB_MAP_TYPE_IACK		(0 << 1)
#define V3_LB_MAP_TYPE_IO		(1 << 1)
#define V3_LB_MAP_TYPE_MEM		(3 << 1)
#define V3_LB_MAP_TYPE_CONFIG		(5 << 1)
#define V3_LB_MAP_TYPE_MEM_MULTIPLE		(6 << 1)
#define v3_addr_to_lb_map(a)	(((a) >> 16) & V3_LB_MAP_MAP_ADR)

#define V3_LB_BASE2_ADR_BASE		0xff00U
#define V3_LB_BASE2_SWAP_AUTO		(3 << 6)
#define V3_LB_BASE2_ENABLE		BIT(0)
#define v3_addr_to_lb_base2(a)	(((a) >> 16) & V3_LB_BASE2_ADR_BASE)

#define V3_LB_MAP2_MAP_ADR		0xff00U
#define v3_addr_to_lb_map2(a)	(((a) >> 16) & V3_LB_MAP2_MAP_ADR)

#define V3_FIFO_PRIO_LOCAL		BIT(12)
#define V3_FIFO_PRIO_LB_RD1_FLUSH_EOB		BIT(10)
#define V3_FIFO_PRIO_LB_RD1_FLUSH_AP1		BIT(11)
#define V3_FIFO_PRIO_LB_RD1_FLUSH_ANY		(BIT(10)|BIT(11))
#define V3_FIFO_PRIO_LB_RD0_FLUSH_EOB		BIT(8)
#define V3_FIFO_PRIO_LB_RD0_FLUSH_AP1		BIT(9)
#define V3_FIFO_PRIO_LB_RD0_FLUSH_ANY		(BIT(8)|BIT(9))
#define V3_FIFO_PRIO_PCI		BIT(4)
#define V3_FIFO_PRIO_PCI_RD1_FLUSH_EOB		BIT(2)
#define V3_FIFO_PRIO_PCI_RD1_FLUSH_AP1		BIT(3)
#define V3_FIFO_PRIO_PCI_RD1_FLUSH_ANY		(BIT(2)|BIT(3))
#define V3_FIFO_PRIO_PCI_RD0_FLUSH_EOB		BIT(0)
#define V3_FIFO_PRIO_PCI_RD0_FLUSH_AP1		BIT(1)
#define V3_FIFO_PRIO_PCI_RD0_FLUSH_ANY		(BIT(0)|BIT(1))

#define V3_LB_CFG_LB_TO_64_CYCLES		0x0000
#define V3_LB_CFG_LB_TO_256_CYCLES		BIT(13)
#define V3_LB_CFG_LB_TO_512_CYCLES		BIT(14)
#define V3_LB_CFG_LB_TO_1024_CYCLES		(BIT(13)|BIT(14))
#define V3_LB_CFG_LB_RST		BIT(12)
#define V3_LB_CFG_LB_PPC_RDY		BIT(11)
#define V3_LB_CFG_LB_LB_INT		BIT(10)
#define V3_LB_CFG_LB_ERR_EN		BIT(9)
#define V3_LB_CFG_LB_RDY_EN		BIT(8)
#define V3_LB_CFG_LB_BE_IMODE		BIT(7)
#define V3_LB_CFG_LB_BE_OMODE		BIT(6)
#define V3_LB_CFG_LB_ENDIAN		BIT(5)
#define V3_LB_CFG_LB_PARK_EN		BIT(4)
#define V3_LB_CFG_LB_FBB_DIS		BIT(2)

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

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Hardware Register Shadows */
    uint8_t regs[0x100];

    /* Config space BAR memory region (separate from register BAR) */
    MemoryRegion config_mem;
};

/* Config space BAR: always return 0xFFFFFFFF on reads, ignore writes */
static uint64_t v3_config_read(void *opaque, hwaddr addr, unsigned size)
{
    return ~0ULL;
}

static void v3_config_write(void *opaque, hwaddr addr, uint64_t val,
                            unsigned size)
{
    /* Drop writes */
}

static const MemoryRegionOps v3_config_ops = {
    .read = v3_config_read,
    .write = v3_config_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* MMIO/PIO Handlers for the register BAR */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size > sizeof(s->regs)) {
        return ~0ULL;
    }

    /* Assemble value from byte array, little-endian */
    for (unsigned i = 0; i < size; i++) {
        val |= (uint64_t)s->regs[addr + i] << (i * 8);
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val,
                               unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t *p = &s->regs[addr];

    if (addr + size > sizeof(s->regs)) {
        return;
    }

    /* Handle special registers */
    if (addr <= V3_SYSTEM + 1 && addr + size > V3_SYSTEM) {
        /* V3_SYSTEM: 16-bit at 0x78; handle unlock */
        if (addr == V3_SYSTEM && size >= 2) {
            uint16_t val16 = val & 0xFFFF;
            if (val16 == V3_SYSTEM_UNLOCK) {
                val16 &= ~V3_SYSTEM_M_LOCK;
                stw_le_p(p, val16);
                
                for (unsigned i = 2; i < size; i++) {
                    p[i] = (val >> (i * 8)) & 0xFF;
                }
            } else {
                stw_le_p(p, val16);
                
                for (unsigned i = 2; i < size; i++) {
                    p[i] = (val >> (i * 8)) & 0xFF;
                }
            }
            
            for (unsigned i = 0; i < 2; i++) {
                p[i] = (val >> (i * 8)) & 0xFF;
            }
            return;
        } else {
            
            for (unsigned i = 0; i < size; i++) {
                p[i] = (val >> (i * 8)) & 0xFF;
            }
            return;
        }
    }

    if (addr <= V3_PCI_STAT + 1 && addr + size > V3_PCI_STAT) {
        
        if (addr == V3_PCI_STAT && size >= 2) {
            uint16_t stat = lduw_le_p(&s->regs[V3_PCI_STAT]);
            uint16_t val16 = val & 0xFFFF;
            stat &= ~val16;
            stw_le_p(&s->regs[V3_PCI_STAT], stat);
            
            for (unsigned i = 2; i < size; i++) {
                p[i] = (val >> (i * 8)) & 0xFF;
            }
        } else {
            
            for (unsigned i = 0; i < size; i++) {
                p[i] = (val >> (i * 8)) & 0xFF;
            }
        }
        return;
    }

    if (addr == V3_LB_ISTAT && size >= 1) {
        
        p[0] = val & 0xFF;
        for (unsigned i = 1; i < size; i++) {
            p[i] = (val >> (i * 8)) & 0xFF;
        }
        return;
    }

    
    for (unsigned i = 0; i < size; i++) {
        p[i] = (val >> (i * 8)) & 0xFF;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* Dummy PIO ops for future use; same handlers as MMIO */
static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    
    memset(s->regs, 0, sizeof(s->regs));
    
    /* Copy initial PCI config values to register shadow (first 0x40 bytes) */
    PCIDevice *pdev = PCI_DEVICE(s);
    memcpy(s->regs, pdev->config, 0x40);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];
    bool already_init = memory_region_size(mr) != 0;

    if (bi->type == BAR_TYPE_MMIO) {
        if (!already_init) {
            memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        }
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        if (!already_init) {
            memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size);
        }
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        if (!already_init) {
            memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        }
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x1057);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x01);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_BRIDGE_HOST);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    
    memcpy(s->regs, pci_conf, 0x40);

    /* BAR Initialization */
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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
    .name = "pci_v3_semi_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void pcibase_instance_init(Object *obj)
{
    PCIBaseState *s = PCIBASE_DEVICE(obj);

    s->num_bars = 2;

    /* BAR 0: Register space (256 bytes) */
    s->bar_info[0] = (BARInfo) {
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x100,
        .name = "v3-regs",
    };

    /* BAR 1: Config space window (16 MB) */
    s->bar_info[1] = (BARInfo) {
        .index = 1,
        .type = BAR_TYPE_MMIO,
        .size = 16 * 1024 * 1024,
        .name = "v3-config",
    };

    /* Pre-initialize config BAR with custom ops (always returns 0xFF) */
    memory_region_init_io(&s->config_mem, obj, &v3_config_ops, s,
                          "v3-config", 16 * 1024 * 1024);
    s->bar_regions[1] = s->config_mem;
}

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
        .instance_init = pcibase_instance_init,
        .class_init = pcibase_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&pcibase_info);
}

type_init(pcibase_register_types);
