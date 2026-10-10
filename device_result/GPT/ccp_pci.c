/*
 * QEMU PCI device model for AMD CCP/PSP (based on sp-pci.c)
 * Phase 2: Minimal functional behavior sufficient for driver probe.
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
#include "hw/irq.h"

#ifndef PCI_CLASS_CRYPT
#define PCI_CLASS_CRYPT 0x1080
#endif

#define TYPE_PCIBASE_DEVICE "ccp_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* From sp_pci_table: first entry is AMD 0x1537 */
#define PCIBASE_VENDOR_ID 0x1022
#define PCIBASE_DEVICE_ID 0x1537

/* Crypto / misc class */
#define PCIBASE_CLASS_ID PCI_CLASS_CRYPT

/* MSIX vector count from driver */
#define PCIBASE_MSIX_VECTORS 2

/* Various hardware-related constants, offsets, and masks from sp-pci.c */
#define AA                                GENMASK(31, 24)
#define BB                                GENMASK(23, 16)
#define CC                                GENMASK(15, 8)
#define DD                                GENMASK(7, 0)
#define MSIX_VECTORS                      2
#define PLATFORM_FEATURE_HSTI             0x2
#define PLATFORM_FEATURE_DBC              0x1
#define MAX_PSP_NAME_LEN                  16
#define SP_MAX_NAME_LEN                   32
#define CCP5_RSA_MAX_WIDTH                16384
#define CCP_RSA_MAX_WIDTH                 4096
#define MAX_HW_QUEUES                     5
#define CMD5_REQID_CONFIG_OFFSET          0x08
#define CMD5_CLK_GATE_CTL_OFFSET          0x603C
#define LSB_PRIVATE_MASK_HI_OFFSET        0x24
#define CMD5_TRNG_CTL_OFFSET              0x6008
#define CMD5_CONFIG_0_OFFSET              0x6000
#define CMD5_QUEUE_PRIO_OFFSET            0x04
#define CMD5_CMD_TIMEOUT_OFFSET           0x10
#define LSB_PRIVATE_MASK_LO_OFFSET        0x20
#define CMD5_AES_MASK_OFFSET              0x6010
#define CMD5_QUEUE_MASK_OFFSET            0x00
#define TRNG_OUT_REG                      0x00c
#define MAX_CCP_NAME_LEN                  16
#define SLSB_MAP_SIZE                     (MAX_LSB_CNT * LSB_SIZE)
#define KSB_COUNT                         (KSB_END - KSB_START + 1)
#define LSB_ITEM_SIZE                     32
#define Q_DESC_SIZE                       sizeof(struct ccp5_desc)
#define LSB_SIZE                          16
#define MAX_LSB_CNT                       8
#define CMD5_Q_STATUS_INCR                0x1000
#define Q_MASK_REG                        0x000
#define CMD5_Q_DMA_STATUS_BASE            0x0108
#define CCP_DMAPOOL_MAX_SIZE              64
#define CMD5_Q_STATUS_BASE                0x0100
#define CMD5_Q_INTERRUPT_STATUS_BASE      0x0010
#define MAX_DMAPOOL_NAME_LEN              32
#define QUEUE_SIZE_VAL                    ((ffs(COMMANDS_PER_QUEUE) - 2) & \
                                           CMD5_Q_SIZE)
#define COMMANDS_PER_QUEUE                16
#define CMD5_Q_INT_STATUS_BASE            0x0104
#define LSB_PUBLIC_MASK_LO_OFFSET         0x18
#define CMD5_Q_TAIL_LO_BASE               0x0004
#define CMD5_Q_SHIFT                      3
#define CMD5_Q_DMA_READ_STATUS_BASE       0x010C
#define CMD5_Q_HEAD_LO_BASE               0x0008
#define CMD5_Q_INT_ENABLE_BASE            0x000C
#define LSB_PUBLIC_MASK_HI_OFFSET         0x1C
#define CMD5_Q_SIZE                       0x1F
#define CMD5_Q_DMA_WRITE_STATUS_BASE      0x0110
#define SUPPORTED_INTERRUPTS              (INT_COMPLETION | INT_ERROR)
#define CCP_DMAPOOL_ALIGN                 BIT(5)
#define Q_SIZE(n)                         (COMMANDS_PER_QUEUE*(n))
#define CMD5_Q_RUN                        0x1
#define CCP_SB_BYTES                      32
#define REQ6_MEMTYPE_SHIFT                16
#define REQ4_KSB_SHIFT                    18
#define REQ1_EOM                          0x00000002
#define REQ1_RSA_MOD_SIZE_SHIFT           10
#define REQ4_MEMTYPE_SHIFT                16
#define REQ1_ENGINE_SHIFT                 23
#define REQ1_KEY_KSB_SHIFT                2
#define KSB_START                         77
#define REQ1_PT_BW_SHIFT                  12
#define REQ1_PT_BS_SHIFT                  10
#define REQ1_SHA_TYPE_SHIFT               21
#define REQ1_INIT                         0x00000001
#define CMD_Q_STATUS_INCR                 0x20
#define CMD_Q_STATUS_BASE                 0x210
#define CMD_Q_CACHE_BASE                  0x228
#define CMD_Q_INT_STATUS_BASE             0x214
#define IRQ_STATUS_REG                    0x200
#define CMD_Q_CACHE_INC                   0x20
#define CMD_Q_DEPTH(__qs)                 (((__qs) >> 12) & 0x0000000f)
#define REQ1_ECC_AFFINE_CONVERT           0x00200000
#define REQ1_ECC_FUNCTION_SHIFT           18
#define REQ1_AES_CFB_SIZE_SHIFT           10
#define REQ1_AES_TYPE_SHIFT               21
#define REQ1_AES_MODE_SHIFT               18
#define REQ1_AES_ACTION_SHIFT             17
#define REQ1_XTS_AES_SIZE_SHIFT           10
#define PLSB_MAP_SIZE                     (LSB_SIZE)
#define KSB_END                           127
#define LSB_REGION_WIDTH                  5
#define INT_ERROR                         0x2
#define INT_COMPLETION                    0x1
#define CMD_REQ_INCR                      0x04
#define REQ0_CMD_Q_SHIFT                  9
#define DEL_CMD_Q_JOB                     0x124
#define REQ0_JOBID_SHIFT                  3
#define CMD_REQ0                          0x180
#define REQ0_WAIT_FOR_WRITE               0x00000004
#define DEL_Q_ACTIVE                      0x00000200
#define REQ0_INT_ON_COMPLETE              0x00000002
#define REQ0_STOP_ON_COMPLETE             0x00000001
#define DEL_Q_ID_SHIFT                    6
#define IRQ_MASK_REG                      0x040
#define CMD_Q_ERROR(__qs)                 ((__qs) & 0x0000003f)
#define TRNG_RETRIES                      10
#define CCP_DMA_PUB                       0x2
#define CCP_DMA_PRIV                      0x1
#define CCP_DMA_DFLT                      0x0
#define MAX_CMD_QLEN                      100

/* MMIO offsets from psp_vdata / sev_vdata / tee_vdata / platform_access_vdata */
#define PSP_BOOTLOADER_INFO_REG_V1 0x105ec
#define PSP_BOOTLOADER_INFO_REG_V2 0x109ec
#define PSP_FEATURE_REG_V1         0x105fc
#define PSP_FEATURE_REG_V2         0x109fc
#define PSP_INTEN_REG_V1           0x10610
#define PSP_INTSTS_REG_V1          0x10614
#define PSP_INTEN_REG_V2           0x10690
#define PSP_INTSTS_REG_V2          0x10694
#define PSP_INTEN_REG_V3           0x10510
#define PSP_INTSTS_REG_V3          0x10514

/* We model only the register addresses that the driver clearly reads: */
#define PSP_BOOTLOADER_INFO_REG    PSP_BOOTLOADER_INFO_REG_V2
#define PSP_TEE_INFO_REG           0x109e8

/* Minimal encoding structs copied from driver */

enum ccp_memtype {
    CCP_MEMTYPE_SYSTEM = 0,
    CCP_MEMTYPE_SB,
    CCP_MEMTYPE_LOCAL,
    CCP_MEMTYPE__LAST,
};

union dword5 {
    struct {
        unsigned int  dst_hi:16;
        unsigned int  dst_mem:2;
        unsigned int  rsvd1:13;
        unsigned int  fixed:1;
    } fields;
    uint32_t sha_len_hi;
};

struct dword7 {
    unsigned int  key_hi:16;
    unsigned int  key_mem:2;
    unsigned int  rsvd1:14;
};

union dword4 {
    uint32_t dst_lo;      /* NON-SHA */
    uint32_t sha_len_lo;  /* SHA */
};

struct dword3 {
    unsigned int  src_hi:16;
    unsigned int  src_mem:2;
    unsigned int  lsb_cxt_id:8;
    unsigned int  rsvd1:5;
    unsigned int  fixed:1;
};

struct dword0 {
    unsigned int soc:1;
    unsigned int ioc:1;
    unsigned int rsvd1:1;
    unsigned int init:1;
    unsigned int eom:1;      /* AES/SHA only */
    unsigned int function:15;
    unsigned int engine:4;
    unsigned int prot:1;
    unsigned int rsvd2:7;
};

struct ccp5_desc {
    struct dword0 dw0;
    uint32_t length;
    uint32_t src_lo;
    struct dword3 dw3;
    union dword4 dw4;
    union dword5 dw5;
    uint32_t key_lo;
    struct dword7 dw7;
};

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

    /* Simple MMIO register shadow for the PSP/CCP region.
     * We implement a coarse 32-bit array covering BAR2 size. */
    uint32_t *mmio32;
    size_t mmio32_len; /* number of 32-bit words */

    /* Simple interrupt emulation state: mask/status for PSP */
    uint32_t irq_mask;
    uint32_t irq_status;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Generate an interrupt if any unmasked bits are set in irq_status. */
    if (s->irq_status & s->irq_mask) {
        if (msix_enabled(pdev)) {
            /* Use vector 0 for PSP */
            msix_notify(pdev, 0);
        } else if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msix_enabled(pdev) && !msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (size != 1 && size != 2 && size != 4 && size != 8) {
        return 0;
    }

    /* Map addr into mmio32 shadow if aligned */
    if (addr + size <= (hwaddr)(s->mmio32_len * 4)) {
        if (size == 4) {
            unsigned index = addr >> 2;
            val = s->mmio32[index];
        } else if (size == 1 || size == 2) {
            /* Perform little-endian sub-access via 32-bit word */
            unsigned index = addr >> 2;
            unsigned shift = (addr & 3) * 8;
            uint32_t word = s->mmio32[index];
            uint32_t mask = (size == 1) ? 0xffu : 0xffffu;
            val = (word >> shift) & mask;
        } else if (size == 8) {
            /* 8-byte read: compose from two 32-bit words if in range */
            if (addr + 8 <= (hwaddr)(s->mmio32_len * 4)) {
                uint32_t lo = s->mmio32[addr >> 2];
                uint32_t hi = s->mmio32[(addr >> 2) + 1];
                val = ((uint64_t)hi << 32) | lo;
            } else {
                val = 0;
            }
        }
    }

    /* Special behavior for bootloader / tee info registers used by sysfs
     * visibility check: default to 0xffffffff unless explicitly set. */
    if (size == 4) {
        if (addr == PSP_BOOTLOADER_INFO_REG || addr == PSP_TEE_INFO_REG) {
            if (val == 0) {
                /* By default make them visible (non-0xffffffff). */
                val = 0x00000001;
            }
        }
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1 && size != 2 && size != 4 && size != 8) {
        return;
    }

    /* Handle interrupt mask/status registers that are explicitly used in
     * the psp_vdata definitions as inten/intsts. We implement basic
     * write-1-to-set and write-1-to-clear semantics. */
    if (size == 4) {
        if (addr == PSP_INTEN_REG_V1 || addr == PSP_INTEN_REG_V2 ||
            addr == PSP_INTEN_REG_V3) {
            /* Treat inten as a simple interrupt mask. */
            s->irq_mask = (uint32_t)val;
            pcibase_update_irq(s);
        } else if (addr == PSP_INTSTS_REG_V1 || addr == PSP_INTSTS_REG_V2 ||
                   addr == PSP_INTSTS_REG_V3) {
            /* W1C for irq_status */
            s->irq_status &= ~((uint32_t)val);
            if ((uint32_t)val) {
                pcibase_update_irq(s);
            }
        }
    }

    /* Store into mmio shadow. */
    if (addr + size <= (hwaddr)(s->mmio32_len * 4)) {
        if (size == 4) {
            unsigned index = addr >> 2;
            s->mmio32[index] = (uint32_t)val;
        } else if (size == 1 || size == 2) {
            unsigned index = addr >> 2;
            unsigned shift = (addr & 3) * 8;
            uint32_t mask = (size == 1) ? 0xffu : 0xffffu;
            uint32_t cur = s->mmio32[index];
            cur &= ~(mask << shift);
            cur |= ((uint32_t)val & mask) << shift;
            s->mmio32[index] = cur;
        } else if (size == 8) {
            if (addr + 8 <= (hwaddr)(s->mmio32_len * 4)) {
                uint32_t lo = (uint32_t)(val & 0xffffffffu);
                uint32_t hi = (uint32_t)(val >> 32);
                s->mmio32[addr >> 2] = lo;
                s->mmio32[(addr >> 2) + 1] = hi;
            }
        }
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

    /* Reset interrupt state and register shadows */
    s->irq_mask = 0;
    s->irq_status = 0;

    if (s->mmio32 && s->mmio32_len) {
        memset(s->mmio32, 0, s->mmio32_len * sizeof(uint32_t));
    }
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR2 holds the register space. */
    s->num_bars = 1;
    s->bar_info[0].index = 2;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    s->bar_info[0].size  = 0x20000; /* covers all known offsets */
    s->bar_info[0].name  = "ccp-mmio-bar2";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize MMIO shadow array for BAR2. */
    s->mmio32_len = s->bar_info[0].size / 4;
    s->mmio32 = g_new0(uint32_t, s->mmio32_len);

    /* Enable MSI-X (preferred) then MSI to match driver expectations. */
    s->has_msi = true;
    s->has_msix = true;

    if (s->has_msix) {
        if (msix_init_exclusive_bar(pdev, PCIBASE_MSIX_VECTORS, 4, NULL)) {
            s->has_msix = false;
        }
    }

    if (!s->has_msix && s->has_msi) {
        if (msi_init(pdev, 0, 1, true, false, errp)) {
            s->has_msi = false;
        }
    }

    /* Clear state */
    s->irq_mask = 0;
    s->irq_status = 0;
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

    if (s->mmio32) {
        g_free(s->mmio32);
        s->mmio32 = NULL;
        s->mmio32_len = 0;
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "ccp_pci",
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
