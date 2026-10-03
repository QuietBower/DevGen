/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Designed for register-level modeling and driver probing.
 * NOTE: Do not directly fill this file with the Linux kernel source code.
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

#define TYPE_PCIBASE_DEVICE "skfddi_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_SK     0x1148
#define PCI_DEVICE_ID_SK_FP  0x4000
#define FP_IO_LEN            256
#define MMIO_SIZE            0x4000
#define CLASS_ID             0x0207  /* PCI class FDDI */

/* Register offsets from IO base (ADDR macro) */
#define B0_CTRL       0x0004
#define B0_DAS        0x0005
#define B0_LED        0x0006
#define B0_TST_CTRL   0x0007
#define B0_ISRC       0x0008
#define B0_IMSK       0x000c
#define B0_ST1U       0x0010
#define B0_ST1L       0x0014
#define B0_ST2U       0x0018
#define B0_ST2L       0x001c
#define B0_ST3U       0x0034
#define B0_ST3L       0x0038
#define B0_R1_CSR     0x0070
#define B0_R2_CSR     0x0074
#define B0_XA_CSR     0x0078
#define B0_XS_CSR     0x007c
#define B2_MAC_0      0x0100
#define B2_CONN_TYP   0x0108
#define B2_PMD_TYP    0x0109
#define B2_FAR        0x0110
#define B2_TI_INI     0x0120
#define B2_TI_VAL     0x0124
#define B2_TI_CRTL    0x0128
#define B2_WDOG_INI   0x0130
#define B2_WDOG_CRTL  0x0138
#define B2_RTM_INI    0x0140
#define B2_RTM_CRTL   0x0148
#define B3_CFG_SPC    0x180
#define B4_R1_DA      0x0210
#define B4_R1_F       0x0220
#define B4_R2_DA      0x0250
#define B5_XA_DA      0x0290
#define B5_XA_F       0x02a0
#define B5_XS_DA      0x02d0
#define B5_XS_F       0x02e0

/* P1 and P2 phy registers */
#define P1(a)  (0x0380|((a)<<2))
#define P2(a)  (0x0600|((a)<<2))

/* FM (formac) registers */
#define FMA(a) (0x0400|((a)<<2))

/* Bit definitions for control/status registers */
#define CTRL_RST_SET   (1<<0)
#define CTRL_RST_CLR   (1<<1)
#define CTRL_MRST_CLR  (1<<3)
#define CTRL_HPI_SET   (1<<4)
#define CTRL_HPI_CLR   (1<<5)
#define CSR_SET_RESET  (1<<20) /* actually CSR_DESC_SET, etc - composite */
#define CSR_CLR_RESET  (1<<21) /* etc */

/* Interrupt source bits in ISR (B0_ISRC) */
#define IS_XS_C        (1L<<0)
#define IS_XS_F        (1L<<1)
#define IS_XA_C        (1L<<4)
#define IS_XA_F        (1L<<5)
#define IS_R1_C        (1L<<12)
#define IS_R1_F        (1L<<13)
#define IS_R1_P        (1L<<15)
#define IS_MINTR1      (1L<<16)
#define IS_MINTR2      (1L<<17)
#define IS_MINTR3      (1L<<18)
#define IS_PLINT2      (1L<<19)
#define IS_PLINT1      (1L<<20)
#define IS_TOKEN       (1L<<21)
#define IS_TIMINT      (1L<<22)

/* DMA descriptor control bits */
#define BMU_OWN        (1UL<<31)
#define BMU_STF        (1L<<30)
#define BMU_EOF        (1L<<29)
#define BMU_SMT_TX     (1L<<25)
#define BMU_ST_BUF     (1L<<25)
#define BMU_CHECK      0x00550000L

/* Timer control bits */
#define TIM_CL_IRQ     (1<<0)
#define TIM_STOP       (1<<1)
#define TIM_START      (1<<2)

/* LED bits */
#define LED_0_OFF      (1<<0)
#define LED_0_ON       (1<<1)
#define LED_1_OFF      (1<<2)
#define LED_1_ON       (1<<3)
#define LED_2_OFF      (1<<4)
#define LED_2_ON       (1<<5)

/* DMA descriptor structures used by driver */
typedef struct s_smt_fp_txd {
    uint32_t txd_tbctrl;
    uint32_t txd_txdscr;
    uint32_t txd_tbadr;
    uint32_t txd_ntdadr;
    void *txd_virt;
    struct s_smt_fp_txd volatile *txd_next;
} s_smt_fp_txd;

typedef struct s_smt_fp_rxd {
    uint32_t rxd_rbctrl;
    uint32_t rxd_rfsw;
    uint32_t rxd_rbadr;
    uint32_t rxd_nrdadr;
    void *rxd_virt;
    struct s_smt_fp_rxd volatile *rxd_next;
} s_smt_fp_rxd;

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
    uint32_t intr_status;
    uint32_t intr_mask;
    bool irq_enabled;        /* controlled by HPI_SET/CLR */

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t io_regs[MMIO_SIZE];

    /* DMA Context */
    struct {
        dma_addr_t txd_phys;    /* physical address of Tx descriptor ring */
        dma_addr_t rxd_phys;    /* physical address of Rx descriptor ring */
        uint32_t dummy;         /* placeholder */
    } dma;

    /* Operational status flags */
    uint32_t status;
};

/* Internal helper for status-triggered signaling */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t status = ldl_le_p(s->io_regs + B0_ISRC);
    uint32_t mask   = ldl_le_p(s->io_regs + B0_IMSK);
    int level = (status & mask) && s->irq_enabled ? 1 : 0;
    pci_set_irq(pdev, level);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* DMA not required for probe */
}

/* Helper to read 32-bit little-endian from io_regs */
static uint32_t pio_read32(PCIBaseState *s, hwaddr addr)
{
    return (uint32_t)s->io_regs[addr] |
           ((uint32_t)s->io_regs[addr+1] << 8) |
           ((uint32_t)s->io_regs[addr+2] << 16) |
           ((uint32_t)s->io_regs[addr+3] << 24);
}

/* Helper to write 32-bit little-endian to io_regs */
static void pio_write32(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    s->io_regs[addr]   = val & 0xff;
    s->io_regs[addr+1] = (val >> 8)  & 0xff;
    s->io_regs[addr+2] = (val >> 16) & 0xff;
    s->io_regs[addr+3] = (val >> 24) & 0xff;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = ~0ULL;

    if (addr >= MMIO_SIZE) {
        val = 0;
    } else if (size == 4) {
        val = pio_read32(s, addr);
    } else {
        /* Unsupported access size for MMIO */
        val = 0;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= MMIO_SIZE) {
        return;
    }

    if (size != 4) {
        /* Only 32-bit accesses supported */
        return;
    }

    switch (addr) {
    case B0_CTRL:
        if (val & CTRL_HPI_SET)
            s->irq_enabled = true;
        if (val & CTRL_HPI_CLR)
            s->irq_enabled = false;
        pio_write32(s, addr, (uint32_t)val);
        pcibase_update_irq(s);
        break;
    case B0_IMSK:
        pio_write32(s, addr, (uint32_t)val);
        pcibase_update_irq(s);
        break;
    case B0_ISRC:
        /* Write-1-to-Clear */
        {
            uint32_t cur = pio_read32(s, addr);
            uint32_t clr = (uint32_t)val;
            pio_write32(s, addr, cur & ~clr);
            pcibase_update_irq(s);
        }
        break;
    default:
        pio_write32(s, addr, (uint32_t)val);
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = ~0ULL;

    if (addr >= FP_IO_LEN) {
        val = 0;
    } else if (size == 4) {
        val = pio_read32(s, addr);
    } else {
        /* Unsupported access size */
        val = 0;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= FP_IO_LEN) {
        return;
    }

    if (size != 4) {
        /* Only 32-bit accesses supported */
        return;
    }

    switch (addr) {
    case B0_CTRL:
        if (val & CTRL_HPI_SET)
            s->irq_enabled = true;
        if (val & CTRL_HPI_CLR)
            s->irq_enabled = false;
        pio_write32(s, addr, (uint32_t)val);
        pcibase_update_irq(s);
        break;
    case B0_IMSK:
        pio_write32(s, addr, (uint32_t)val);
        pcibase_update_irq(s);
        break;
    case B0_ISRC:
        /* Write-1-to-Clear */
        {
            uint32_t cur = pio_read32(s, addr);
            uint32_t clr = (uint32_t)val;
            pio_write32(s, addr, cur & ~clr);
            pcibase_update_irq(s);
        }
        break;
    default:
        pio_write32(s, addr, (uint32_t)val);
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
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->io_regs, 0, MMIO_SIZE);

    /* Default MAC address */
    s->io_regs[B2_MAC_0 + 0] = 0x00;
    s->io_regs[B2_MAC_0 + 1] = 0x00;
    s->io_regs[B2_MAC_0 + 2] = 0x00;
    s->io_regs[B2_MAC_0 + 3] = 0x00;
    s->io_regs[B2_MAC_0 + 4] = 0x00;
    s->io_regs[B2_MAC_0 + 5] = 0x01;

    /* Mark device as present with a non-zero configuration space */
    pio_write32(s, B3_CFG_SPC, 0x12345678);

    s->irq_enabled = false;
    pci_set_irq(PCI_DEVICE(s), 0);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_SK );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_SK_FP );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Explicitly enable memory space to ensure BAR 0 is recognized as MMIO */
    pci_set_word(pci_conf + PCI_COMMAND, PCI_COMMAND_MEMORY);

    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;            /* MMIO BAR at index 0 */
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = MMIO_SIZE;
    s->bar_info[0].name = "mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Field init: MAC address already set in reset, but we can clear irq_enabled */
    s->irq_enabled = false;
    memset(s->io_regs, 0, MMIO_SIZE);
    /* Default MAC address in io_regs */
    s->io_regs[B2_MAC_0 + 0] = 0x00;
    s->io_regs[B2_MAC_0 + 1] = 0x00;
    s->io_regs[B2_MAC_0 + 2] = 0x00;
    s->io_regs[B2_MAC_0 + 3] = 0x00;
    s->io_regs[B2_MAC_0 + 4] = 0x00;
    s->io_regs[B2_MAC_0 + 5] = 0x01;
    pio_write32(s, B3_CFG_SPC, 0x12345678);
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
    /* No extra uninit actions required */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "skfddi_pci",
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
