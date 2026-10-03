/* QEMU 8.2.10 virtual PCI device model for Cavium LiquidIO CN68XX */
#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qemu/bswap.h"
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

/* Additional includes */

#ifndef BIT_ULL
#define BIT_ULL(nr) (1ULL << (nr))
#endif

#ifndef OCTEON_PCI_64BIT_SWAP
#define OCTEON_PCI_64BIT_SWAP 1
#endif

#define TYPE_PCIBASE_DEVICE "LiquidIO_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers */
#define PCI_VENDOR_ID_CAVIUM         0x177d
#define LIQUIDIO_DEVICE_ID            0x0091  /* CN68xx */
#define LIQUIDIO_CLASS                0x0200  /* Ethernet controller */

/* Chip IDs */
#define OCTEON_CN68XX                 0x0091

/* CN6XXX Register Offsets */
#define CN6XXX_SLI_SCRATCH1           0x03C0
#define CN6XXX_WIN_WR_ADDR_LO         0x0000
#define CN6XXX_WIN_WR_ADDR_HI         0x0004
#define CN6XXX_WIN_RD_ADDR_LO         0x0010
#define CN6XXX_WIN_RD_ADDR_HI         0x0014
#define CN6XXX_WIN_WR_DATA_LO         0x0020
#define CN6XXX_WIN_WR_DATA_HI         0x0024
#define CN6XXX_WIN_WR_MASK_LO         0x0030
#define CN6XXX_WIN_RD_DATA_LO         0x0040
#define CN6XXX_WIN_RD_DATA_HI         0x0044
#define CN6XXX_WIN_WR_ADDR64          CN6XXX_WIN_WR_ADDR_LO
#define CN6XXX_WIN_RD_ADDR64          CN6XXX_WIN_RD_ADDR_LO
#define CN6XXX_WIN_WR_DATA64          CN6XXX_WIN_WR_DATA_LO
#define CN6XXX_WIN_RD_DATA64          CN6XXX_WIN_RD_DATA_LO

#define CN6XXX_SLI_WINDOW_CTL         0x02E0
#define CN6XXX_SLI_INT_SUM64          0x0330
#define CN6XXX_SLI_INT_ENB64_PORT0    0x0340
#define CN6XXX_SLI_INT_ENB64_PORT1    0x0350

#define CN6XXX_SLI_MAC_NUMBER         0x3E00

#define CN6XXX_SLI_IQ_DOORBELL_START  0x2C00
#define CN6XXX_SLI_IQ_BASE_ADDR_START64     0x2800
#define CN6XXX_SLI_IQ_SIZE_START      0x3000
#define CN6XXX_SLI_IQ_INSTR_COUNT_START      0x2000
#define CN6XXX_SLI_IQ_PKT_INSTR_HDR_START64  0x3400
#define CN6XXX_IQ_OFFSET               0x10

#define CN6XXX_SLI_OQ0_BUFF_INFO_SIZE         0x0C00
#define CN6XXX_SLI_OQ_PKT_SENT_START          0x2400
#define CN6XXX_SLI_OQ_SIZE_START              0x1C00
#define CN6XXX_SLI_OQ_BASE_ADDR_START64       0x1400
#define CN6XXX_SLI_OQ_PKT_CREDITS_START       0x1800
#define CN6XXX_OQ_OFFSET                      0x10

#define CN6XXX_SLI_PKT_CNT_INT_ENB            0x1150
#define CN6XXX_SLI_PKT_TIME_INT_ENB           0x1160
#define CN6XXX_SLI_PKT_TIME_INT               0x1140
#define CN6XXX_SLI_PKT_CNT_INT                0x1130

#define CN6XXX_SLI_PKT_INSTR_RD_SIZE          0x11A0
#define CN6XXX_SLI_IN_PCIE_PORT               0x11B0
#define CN6XXX_SLI_PKT_INPUT_CONTROL          0x1170
#define CN6XXX_SLI_PKT_SLIST_ROR              0x1030
#define CN6XXX_SLI_PKT_SLIST_NS               0x1040
#define CN6XXX_SLI_PKT_PCIE_PORT64            0x10E0
#define CN6XXX_SLI_OQ_INT_LEVEL_TIME          0x1124
#define CN6XXX_SLI_OQ_WMARK                   0x1180
#define CN6XXX_SLI_PKT_DATA_OUT_ROR           0x1090
#define CN6XXX_SLI_PKT_DPADDR                 0x1080
#define CN6XXX_SLI_PKT_DATA_OUT_NS            0x10A0
#define CN6XXX_SLI_OQ_INT_LEVEL_PKTS          0x1120
#define CN6XXX_SLI_PKT_OUT_BMODE              0x10D0
#define CN6XXX_SLI_PKT_SLIST_ES64             0x1050
#define CN6XXX_SLI_PKT_DATA_OUT_ES64          0x10B0
#define CN6XXX_SLI_PKT_CTL                    0x1220

#define CN6XXX_LMC0_RESET_CTL               0x0001180088000180ULL
#define CN6XXX_LMC0_RESET_CTL_DDR3RST_MASK  0x0000000000000001ULL

#define CN6XXX_SLI_PORT_IN_RST_OQ              0x11F0
#define CN6XXX_SLI_PORT_IN_RST_IQ              0x11F4
#define CN6XXX_SLI_PKT_OUT_ENB                 0x1010
#define CN6XXX_SLI_PKT_INSTR_ENB               0x1000

#define CN6XXX_DMA_INT_LEVEL_START             0x03E0
#define CN6XXX_DMA_CNT_START                   0x0400
#define CN6XXX_DMA_OFFSET                      0x10

/* Interrupt bits (CN6XXX) */
#define CN6XXX_INTR_PKT_COUNT                 BIT(4)
#define CN6XXX_INTR_PKT_TIME                  BIT(5)
#define CN6XXX_INTR_MIO_INT0                  BIT(16)
#define CN6XXX_INTR_MIO_INT1                  BIT(17)
#define CN6XXX_INTR_MAC_INT0                  BIT(18)
#define CN6XXX_INTR_MAC_INT1                  BIT(19)
#define CN6XXX_INTR_DMA0_FORCE                BIT_ULL(32)
#define CN6XXX_INTR_DMA1_FORCE                BIT_ULL(33)
#define CN6XXX_INTR_DMA0_TIME                 BIT_ULL(36)
#define CN6XXX_INTR_DMA1_TIME                 BIT_ULL(37)
#define CN6XXX_INTR_DMA0_DATA                 (CN6XXX_INTR_DMA0_TIME)
#define CN6XXX_INTR_DMA1_DATA                 (CN6XXX_INTR_DMA1_TIME)
#define CN6XXX_INTR_DMA_DATA                  (CN6XXX_INTR_DMA0_DATA | CN6XXX_INTR_DMA1_DATA)
#define CN6XXX_INTR_PKT_DATA                  (CN6XXX_INTR_PKT_TIME | CN6XXX_INTR_PKT_COUNT)
#define CN6XXX_INTR_PCIE_DATA                 (CN6XXX_INTR_DMA_DATA | CN6XXX_INTR_PKT_DATA)
#define CN6XXX_INTR_MAC                       (CN6XXX_INTR_MAC_INT0 | CN6XXX_INTR_MAC_INT1)
#define CN6XXX_INTR_MIO                       (CN6XXX_INTR_MIO_INT0 | CN6XXX_INTR_MIO_INT1)

#define CN6XXX_INTR_ERR                       \
	(CN6XXX_INTR_BAR0_RW_TIMEOUT_ERR    \
	   | CN6XXX_INTR_IO2BIG_ERR             \
	   | CN6XXX_INTR_M0UPB0_ERR             \
	   | CN6XXX_INTR_M0UPWI_ERR             \
	   | CN6XXX_INTR_M0UNB0_ERR             \
	   | CN6XXX_INTR_M0UNWI_ERR             \
	   | CN6XXX_INTR_M1UPB0_ERR             \
	   | CN6XXX_INTR_M1UPWI_ERR             \
	   | CN6XXX_INTR_M1UNB0_ERR             \
	   | CN6XXX_INTR_M1UNWI_ERR             \
	   | CN6XXX_INTR_INSTR_DB_OF_ERR        \
	   | CN6XXX_INTR_SLIST_DB_OF_ERR        \
	   | CN6XXX_INTR_POUT_ERR               \
	   | CN6XXX_INTR_PIN_BP_ERR             \
	   | CN6XXX_INTR_PGL_ERR                \
	   | CN6XXX_INTR_PDI_ERR                \
	   | CN6XXX_INTR_POP_ERR                \
	   | CN6XXX_INTR_PINS_ERR               \
	   | CN6XXX_INTR_SPRT0_ERR              \
	   | CN6XXX_INTR_SPRT1_ERR              \
	   | CN6XXX_INTR_ILL_PAD_ERR)

#define CN6XXX_INTR_MASK                      \
	(CN6XXX_INTR_PCIE_DATA              \
	   | CN6XXX_INTR_DMA0_FORCE             \
	   | CN6XXX_INTR_DMA1_FORCE             \
	   | CN6XXX_INTR_MIO                    \
	   | CN6XXX_INTR_MAC                    \
	   | CN6XXX_INTR_ERR)

/* Commonly used driver constants */
#define LIO_FLAG_MSI_ENABLED                  (1 << 1)
#define LIO_FLAG_MSIX_ENABLED                 0x1

/* Device states */
#define OCT_DEV_BEGIN_STATE            0x0
#define OCT_DEV_PCI_MAP_DONE           0x2
#define OCT_DEV_INSTR_QUEUE_INIT_DONE  0x4
#define OCT_DEV_RESP_LIST_INIT_DONE    0x6
#define OCT_DEV_DROQ_INIT_DONE         0x7
#define OCT_DEV_MBOX_SETUP_DONE        0x8
#define OCT_DEV_MSIX_ALLOC_VECTOR_DONE 0x9
#define OCT_DEV_INTR_SET_DONE          0xa
#define OCT_DEV_IO_QUEUES_DONE         0xb
#define OCT_DEV_CONSOLE_INIT_DONE      0xc
#define OCT_DEV_HOST_OK                0xd
#define OCT_DEV_CORE_OK                0xe
#define OCT_DEV_RUNNING                0xf

/* BAR1 constants */
#define OCTEON_BAR1_ENTRY_SIZE         (4 * 1024 * 1024)
#define PCI_BAR1_ENABLE_CA            1
#define PCI_BAR1_ENDIAN_MODE          OCTEON_PCI_64BIT_SWAP
#define PCI_BAR1_ENTRY_VALID          1
#define PCI_BAR1_MASK                 ((PCI_BAR1_ENABLE_CA << 3)   \
					    | (PCI_BAR1_ENDIAN_MODE << 1) \
					    | PCI_BAR1_ENTRY_VALID)

/* MSI-X vectors */
#define MSIX_VECTORS			2


/* Indirect memory addresse */
#define BOOTLOADER_PCI_READ_BUFFER_OWNER_ADDR   0x0006c000

#define OCTEON_PCI_IO_BUF_OWNER_HOST      0x00000002


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

#define REG_MAX 0x100000  /* Covers entire BAR0 size (1MB) */

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint16_t msix_vectors;
    uint64_t intr_status;
    uint64_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint64_t regs[REG_MAX / 8];
    uint32_t scratch1;
    uint32_t scratch2;

    /* Indirect Access Window State */
    uint64_t win_wr_addr;
    uint64_t win_rd_addr;
    MemoryRegion core_mem;   /* Emulated core memory accessed via window */

    /* DMA Context */

    /* Status */
    uint32_t status;

    /* Reset */
    bool in_reset;

    /* Power Management */
    uint8_t pm_state;
};

/* Helper to read from core_mem at given offset */
static uint64_t pcibase_core_mem_read(PCIBaseState *s, hwaddr offset, unsigned size)
{
    uint64_t val = 0;
    uint8_t *ptr = memory_region_get_ram_ptr(&s->core_mem);
    switch (size) {
    case 1:
        val = ldub_p(ptr + offset);
        break;
    case 2:
        val = lduw_le_p(ptr + offset);
        break;
    case 4:
        val = ldl_le_p(ptr + offset);
        break;
    case 8:
        val = ldq_le_p(ptr + offset);
        break;
    default:
        val = 0;
    }
    return val;
}

/* Helper to write to core_mem at given offset */
static void pcibase_core_mem_write(PCIBaseState *s, hwaddr offset, uint64_t val, unsigned size)
{
    uint8_t *ptr = memory_region_get_ram_ptr(&s->core_mem);
    switch (size) {
    case 1:
        stb_p(ptr + offset, val);
        break;
    case 2:
        stw_le_p(ptr + offset, val);
        break;
    case 4:
        stl_le_p(ptr + offset, val);
        break;
    case 8:
        stq_le_p(ptr + offset, val);
        break;
    default:
        break;
    }
}

/* MMIO read handler: CPU reads from BAR0 */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= REG_MAX) {
        return 0xFFFFFFFFFFFFFFFFULL;
    }

    /* Indirect window read: when reading RD_DATA register, return value from core_mem */
    if (addr == CN6XXX_WIN_RD_DATA_LO && size == 4) {
        val = pcibase_core_mem_read(s, s->win_rd_addr, 4);
        return val;
    }
    if (addr == CN6XXX_WIN_RD_DATA_HI && size == 4) {
        val = pcibase_core_mem_read(s, s->win_rd_addr + 4, 4);
        return val;
    }
    if (addr == CN6XXX_WIN_RD_DATA64 && size == 8) {
        val = pcibase_core_mem_read(s, s->win_rd_addr, 8);
        return val;
    }

    /* Normal direct register read */
    if (size == 4) {
        val = *(uint32_t *)((uint8_t *)s->regs + addr);
    } else if (size == 8) {
        val = *(uint64_t *)((uint8_t *)s->regs + addr);
    } else {
        /* Unsupported size */
        val = 0xFFFFFFFFFFFFFFFFULL;
    }
    return val;
}

/* MMIO write handler: CPU writes to BAR0 */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= REG_MAX) {
        return;
    }

    /* Indirect window address registers: set the pending address */
    if (addr == CN6XXX_WIN_WR_ADDR_LO && size == 4) {
        s->win_wr_addr = (s->win_wr_addr & 0xFFFFFFFF00000000ULL) | (val & 0xFFFFFFFFULL);
        return;
    }
    if (addr == CN6XXX_WIN_WR_ADDR_HI && size == 4) {
        s->win_wr_addr = (s->win_wr_addr & 0xFFFFFFFFULL) | ((uint64_t)val << 32);
        return;
    }
    if (addr == CN6XXX_WIN_WR_ADDR64 && size == 8) {
        s->win_wr_addr = val;
        return;
    }
    if (addr == CN6XXX_WIN_RD_ADDR_LO && size == 4) {
        s->win_rd_addr = (s->win_rd_addr & 0xFFFFFFFF00000000ULL) | (val & 0xFFFFFFFFULL);
        return;
    }
    if (addr == CN6XXX_WIN_RD_ADDR_HI && size == 4) {
        s->win_rd_addr = (s->win_rd_addr & 0xFFFFFFFFULL) | ((uint64_t)val << 32);
        return;
    }
    if (addr == CN6XXX_WIN_RD_ADDR64 && size == 8) {
        s->win_rd_addr = val;
        return;
    }

    /* Indirect window data registers: write to core_mem */
    if (addr == CN6XXX_WIN_WR_DATA_LO && size == 4) {
        pcibase_core_mem_write(s, s->win_wr_addr, val, 4);
        return;
    }
    if (addr == CN6XXX_WIN_WR_DATA_HI && size == 4) {
        pcibase_core_mem_write(s, s->win_wr_addr + 4, val, 4);
        return;
    }
    if (addr == CN6XXX_WIN_WR_DATA64 && size == 8) {
        pcibase_core_mem_write(s, s->win_wr_addr, val, 8);
        return;
    }

    /* Normal direct register write */
    if (size == 4) {
        *(uint32_t *)((uint8_t *)s->regs + addr) = (uint32_t)val;
    } else if (size == 8) {
        *(uint64_t *)((uint8_t *)s->regs + addr) = val;
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

    /* Initialize register shadows to a ready state where possible */
    memset(s->regs, 0, sizeof(s->regs));
    s->intr_status = 0;
    s->intr_mask = 0;
    s->status = 0;
    s->in_reset = false;

    /* Set some guess values to avoid driver loops (e.g., DDR ready) */
    s->regs[CN6XXX_SLI_SCRATCH1 / 8] = 0xFFFFFFFFFFFFFFFFULL;
    /* Bootloader PCI read buffer owner address: set to HOST owner */
    *(uint32_t *)((uint8_t *)s->regs + BOOTLOADER_PCI_READ_BUFFER_OWNER_ADDR) = cpu_to_le32(OCTEON_PCI_IO_BUF_OWNER_HOST);

    /* Initialize core_mem with default zero, will be populated when we have offsets */
    memset(memory_region_get_ram_ptr(&s->core_mem), 0, memory_region_size(&s->core_mem));

    /* Reset indirect window addresses */
    s->win_wr_addr = 0;
    s->win_rd_addr = 0;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_CAVIUM );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  LIQUIDIO_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, LIQUIDIO_CLASS );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR0: MMIO registers, BAR1: RAM */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x100000;  /* 1MB BAR0 */
    s->bar_info[0].name = "bar0";
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_RAM;
    s->bar_info[1].size = 0x800000;  /* 8MB BAR1 */
    s->bar_info[1].name = "bar1";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI-X support */
    if (msix_init(pdev, MSIX_VECTORS,
                  &s->bar_regions[0], 0, 0,
                  &s->bar_regions[0], 0, 0x1000, 0, errp) < 0) {
        msix_uninit(pdev, &s->bar_regions[0], &s->bar_regions[0]);
        /* Fallback to MSI? */
    } else {
        s->has_msix = true;
    }

    /* Allocate core memory for indirect accesses (16MB) */
    memory_region_init_ram(&s->core_mem, OBJECT(s), "core_mem", 0x1000000, errp);

    /* Final state initialization */
    pcibase_reset(DEVICE(pdev));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->bar_regions[0], &s->bar_regions[0]);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "LiquidIO_pci",
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
