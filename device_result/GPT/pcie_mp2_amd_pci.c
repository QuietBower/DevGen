/*
 * QEMU PCI Device Model for AMD SFH MP2 (minimal behavior for driver probe)
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

#define TYPE_PCIBASE_DEVICE "pcie_mp2_amd_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define AMD_SFH_HID_VENDOR        0x1022
#define AMD_SFH_HID_PRODUCT       0x0001

#define PCI_DEVICE_ID_AMD_MP2     0x15E4
#define PCI_DEVICE_ID_AMD_MP2_1_1 0x164A

#define AMD_C2P_MSG0              0x10500
#define AMD_C2P_MSG1              0x10504
#define AMD_C2P_MSG2              0x10508

#define AMD_C2P_MSG(regno)        (0x10500 + ((regno) * 4))
#define AMD_P2C_MSG_V1(regno)     (0x10500 + ((regno) * 4))
#define AMD_P2C_MSG(regno)        (0x10680 + ((regno) * 4))
#define AMD_P2C_MSG3              0x1068C

#define SENSOR_DISABLED           5
#define SENSOR_ENABLED            4
#define SENSOR_DISCOVERY_STATUS_MASK  GENMASK(5, 3)
#define SENSOR_DISCOVERY_STATUS_SHIFT 3

#define ACEL_EN   BIT(0)
#define GYRO_EN   BIT(1)
#define MAGNO_EN  BIT(2)
#define OP_EN     BIT(15)
#define HPD_EN    BIT(16)
#define ALS_EN    BIT(19)
#define ACS_EN    BIT(22)

#define AMD_SFH_IDLE_LOOP         200

#define V2_STATUS                 0x2

#define MAX_HID_DEVICES           7

#define AMD_PCI_VENDOR_ID         AMD_SFH_HID_VENDOR
#define AMD_PCI_DEVICE_ID         PCI_DEVICE_ID_AMD_MP2
#define AMD_PCI_CLASS_ID          PCI_CLASS_OTHERS

/* Local helpers for GENMASK/BIT, since we are outside Linux headers */
#ifndef GENMASK
#define GENMASK(h, l) (((~0u) - (1u << (l)) + 1u) & (~0u >> (31 - (h))))
#endif
#ifndef BIT
#define BIT(nr) (1u << (nr))
#endif

/* Interrupt-related bits based on driver behavior (v2 ops expose clear_intr/init_intr) */
#define AMD_SFH_INT_STATUS_OFFSET   0x10700
#define AMD_SFH_INT_MASK_OFFSET     0x10704

#define AMD_SFH_INT_STATUS_CMD      BIT(0)
#define AMD_SFH_INT_MASK_CMD        BIT(0)


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

    /* Hardware Register Shadows (The 'Identity' of the device) */

    /* Simple shadow register space for BAR0 MMIO */
    uint8_t *mmio_regs;
    hwaddr mmio_size;

    /* Cached values used by driver interactions */
    uint32_t p2c_msg0;   /* AMD_P2C_MSG(0) */
    uint32_t p2c_msg1;   /* AMD_P2C_MSG(1) */
    uint32_t p2c_msg3;   /* AMD_P2C_MSG3  */

    /* Simple interrupt emulation state for v2 clear_intr/init_intr */
    uint32_t int_status;
    uint32_t int_mask;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /*
     * Driver side has mp2_ops->init_intr/clear_intr hooks for v2.
     * We model a simple single interrupt source: when any enabled
     * bit in int_status & int_mask is set, assert INTx; clear_intr
     * will clear status bits via MMIO and we deassert.
     */
    if ((s->int_status & s->int_mask) != 0) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    (void)pdev;
    (void)is_write;
}

/* Helpers to access shadow MMIO register array */
static inline bool pcibase_mmio_in_range(PCIBaseState *s, hwaddr addr, unsigned size)
{
    if (!s->mmio_regs) {
        return false;
    }
    if (addr + size > s->mmio_size) {
        return false;
    }
    return true;
}

static uint32_t pcibase_mmio_load32(PCIBaseState *s, hwaddr addr)
{
    uint32_t v = 0;
    if (!pcibase_mmio_in_range(s, addr, 4)) {
        return 0;
    }
    memcpy(&v, s->mmio_regs + addr, sizeof(v));
    return le32_to_cpu(v);
}

static void pcibase_mmio_store32(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    uint32_t v = cpu_to_le32(val);
    if (!pcibase_mmio_in_range(s, addr, 4)) {
        return;
    }
    memcpy(s->mmio_regs + addr, &v, sizeof(v));
}

static uint64_t pcibase_mmio_load64(PCIBaseState *s, hwaddr addr)
{
    uint64_t v = 0;
    if (!pcibase_mmio_in_range(s, addr, 8)) {
        return 0;
    }
    memcpy(&v, s->mmio_regs + addr, sizeof(v));
    return le64_to_cpu(v);
}

static void pcibase_mmio_store64(PCIBaseState *s, hwaddr addr, uint64_t val)
{
    uint64_t v = cpu_to_le64(val);
    if (!pcibase_mmio_in_range(s, addr, 8)) {
        return;
    }
    memcpy(s->mmio_regs + addr, &v, sizeof(v));
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (!pcibase_mmio_in_range(s, addr, size)) {
        return 0;
    }

    /* Handle specific registers used by the driver */
    if (size == 4) {
        switch (addr) {
        case AMD_P2C_MSG(0):
            /*
             * Driver uses this for command responses and status polling.
             * We simply return the cached shadow value.
             */
            val = s->p2c_msg0;
            break;
        case AMD_P2C_MSG(1):
            /* Discovery status and other bits, driver masks with
             * SENSOR_DISCOVERY_STATUS_MASK and shifts.
             */
            val = s->p2c_msg1;
            break;
        case AMD_P2C_MSG3:
            /* mp2_acs / version status register used by mp2_select_ops() */
            val = s->p2c_msg3;
            break;
        case AMD_SFH_INT_STATUS_OFFSET:
            /* Expose interrupt status to driver clear_intr() hook */
            val = s->int_status;
            break;
        case AMD_SFH_INT_MASK_OFFSET:
            val = s->int_mask;
            break;
        default:
            val = pcibase_mmio_load32(s, addr);
            break;
        }
    } else if (size == 8) {
        /* readq() for 64-bit registers like AMD_C2P_MSG2/1 */
        val = pcibase_mmio_load64(s, addr);
    } else {
        /* For other sizes, fall back to byte-wise copy */
        if (size == 1 || size == 2) {
            if (!pcibase_mmio_in_range(s, addr, size)) {
                return 0;
            }
            memcpy(&val, s->mmio_regs + addr, size);
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (!pcibase_mmio_in_range(s, addr, size)) {
        return;
    }

    if (size == 4) {
        uint32_t v32 = (uint32_t)val;
        switch (addr) {
        case AMD_C2P_MSG0:
        case AMD_C2P_MSG1:
            /*
             * Command and parameter registers for v1/v2 start/stop commands.
             * Driver writes ENABLE/DISABLE/STOP commands here.
             */
            pcibase_mmio_store32(s, addr, v32);
            break;
        case AMD_C2P_MSG2:
            /* Lower 32 bits of the 64-bit DMA address for v1 commands. */
            pcibase_mmio_store32(s, addr, v32);
            break;
        case AMD_P2C_MSG(0):
            /*
             * Response/status register. Driver normally only reads this,
             * but we keep a shadow in case of test writes.
             */
            s->p2c_msg0 = v32;
            pcibase_mmio_store32(s, addr, v32);
            break;
        case AMD_P2C_MSG(1):
            s->p2c_msg1 = v32;
            pcibase_mmio_store32(s, addr, v32);
            break;
        case AMD_P2C_MSG3:
            /* mp2_acs written by host only in synthetic scenarios. */
            s->p2c_msg3 = v32;
            pcibase_mmio_store32(s, addr, v32);
            break;
        case AMD_SFH_INT_STATUS_OFFSET:
            /*
             * W1C-style clear for interrupt status bits used by
             * amd_sfh_clear_intr_v2() in the driver.
             */
            s->int_status &= ~v32;
            pcibase_mmio_store32(s, addr, s->int_status);
            pcibase_update_irq(s);
            break;
        case AMD_SFH_INT_MASK_OFFSET:
            /*
             * Interrupt mask register programmed by init_intr_v2().
             */
            s->int_mask = v32;
            pcibase_mmio_store32(s, addr, s->int_mask);
            pcibase_update_irq(s);
            break;
        default:
            pcibase_mmio_store32(s, addr, v32);
            break;
        }
    } else if (size == 8) {
        /* writeq() used for DMA addresses in AMD_C2P_MSG1/2 */
        pcibase_mmio_store64(s, addr, val);
    } else {
        /* 1- or 2-byte generic writes */
        if (!pcibase_mmio_in_range(s, addr, size)) {
            return;
        }
        memcpy(s->mmio_regs + addr, &val, size);
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
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

    if (s->mmio_regs && s->mmio_size) {
        memset(s->mmio_regs, 0, s->mmio_size);
    }

    /* Initialize P2C/ACS related state so that driver selects V2 ops */
    s->p2c_msg0 = 0;
    /* Discovery status: set bits so that SENSOR_ENABLED is returned */
    s->p2c_msg1 = (SENSOR_ENABLED << SENSOR_DISCOVERY_STATUS_SHIFT);
    /*
     * mp2_acs: lower bits indicate V2_STATUS so mp2_select_ops() picks v2 ops.
     * Also set higher bits so that amd_mp2_get_sensor_num() sees sensors
     * enabled via mp2_acs >> 4.
     * Use driver-defined enable bits to advertise a representative set
     * of sensors without inventing new semantics.
     */
    s->p2c_msg3 = V2_STATUS;
    s->p2c_msg3 |= (ACEL_EN | GYRO_EN | MAGNO_EN | ALS_EN | HPD_EN | ACS_EN);

    /* Interrupt state defaults: all masked, no pending interrupt */
    s->int_status = 0;
    s->int_mask = 0;

    if (s->mmio_regs && s->mmio_size) {
        if (AMD_P2C_MSG(0) + 4 <= s->mmio_size) {
            pcibase_mmio_store32(s, AMD_P2C_MSG(0), s->p2c_msg0);
        }
        if (AMD_P2C_MSG(1) + 4 <= s->mmio_size) {
            pcibase_mmio_store32(s, AMD_P2C_MSG(1), s->p2c_msg1);
        }
        if (AMD_P2C_MSG3 + 4 <= s->mmio_size) {
            pcibase_mmio_store32(s, AMD_P2C_MSG3, s->p2c_msg3);
        }
        if (AMD_SFH_INT_STATUS_OFFSET + 4 <= s->mmio_size) {
            pcibase_mmio_store32(s, AMD_SFH_INT_STATUS_OFFSET, s->int_status);
        }
        if (AMD_SFH_INT_MASK_OFFSET + 4 <= s->mmio_size) {
            pcibase_mmio_store32(s, AMD_SFH_INT_MASK_OFFSET, s->int_mask);
        }
    }

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
        s->mmio_size = aligned_size;
        s->mmio_regs = g_new0(uint8_t, aligned_size);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  AMD_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  AMD_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, AMD_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 3;

    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_NONE;
    s->bar_info[0].size = 0;
    s->bar_info[0].name = "unused-bar0";

    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_NONE;
    s->bar_info[1].size = 0;
    s->bar_info[1].name = "unused-bar1";

    s->bar_info[2].index = 2;
    s->bar_info[2].type = BAR_TYPE_MMIO;
    s->bar_info[2].size = 1 * MiB;
    s->bar_info[2].name = "amd-mp2-bar2";

    for (int i = 3; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "unused";
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = false;
    s->has_msix = false;

    /* Initialize MMIO contents and P2C/ACS state */
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

    if (s->mmio_regs) {
        g_free(s->mmio_regs);
        s->mmio_regs = NULL;
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "pcie_mp2_amd_pci",
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
