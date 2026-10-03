/*
 * QEMU PCI device model for Intel PRO/Wireless 2100 (ipw2100)
 * Based on Linux driver ipw2100.c
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

#define TYPE_PCIBASE_DEVICE "ipw2100_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define IPW2100_PCI_VENDOR_ID 0x8086
#define IPW2100_PCI_DEVICE_ID 0x1043
#define IPW2100_PCI_CLASS_ID  0x0280

/* MMIO register offsets (domain 0) */
#define IPW_REG_DOMAIN_0_OFFSET 0x0000
#define IPW_REG_INTA               0x0008
#define IPW_REG_INTA_MASK          0x000C
#define IPW_REG_INDIRECT_ACCESS_ADDRESS 0x0010
#define IPW_REG_INDIRECT_ACCESS_DATA    0x0014
#define IPW_REG_AUTOINCREMENT_ADDRESS   0x0018
#define IPW_REG_AUTOINCREMENT_DATA      0x001C
#define IPW_REG_RESET_REG             0x0020
#define IPW_REG_GP_CNTRL              0x0024
#define IPW_REG_GPIO                  0x0030
#define IPW_REG_DOA_DEBUG_AREA_START  0x0090
#define IPW_REG_DOA_DEBUG_AREA_END    0x00FF
#define IPW_MMIO_SIZE                 0x100

#define IPW_DATA_DOA_DEBUG_VALUE 0xd55555d5

/* NIC internal memory size - placeholder until defined */
#define NIC_MEM_SIZE 0x40000  /* tentative, needs IPW_HOST_FW_* defines */

#define IPW_AUX_HOST_RESET_REG_SW_RESET                  (0x00000080)
#define IPW_AUX_HOST_RESET_REG_PRINCETON_RESET           (0x00000001)
#define IPW_AUX_HOST_RESET_REG_STOP_MASTER               (0x00000200)
#define IPW_AUX_HOST_RESET_REG_MASTER_DISABLED           (0x00000100)

#define IPW_AUX_HOST_GP_CNTRL_BIT_INIT_DONE              (0x00000004)
#define IPW_AUX_HOST_GP_CNTRL_BIT_CLOCK_READY            (0x00000001)
#define IPW_AUX_HOST_GP_CNTRL_BIT_HOST_ALLOWS_STANDBY    (0x00000002)

#define IPW2100_INTA_FW_INIT_DONE              (0x01000000)
#define IPW2100_INTA_FATAL_ERROR               (0x40000000)
#define IPW2100_INTA_PARITY_ERROR              (0x80000000)
#define IPW2100_INTA_RX_TRANSFER               (0x00000002)
#define IPW2100_INTA_TX_TRANSFER               (0x00000001)
#define IPW2100_INTA_TX_COMPLETE               (0x00000004)
#define IPW2100_INTA_EVENT_INTERRUPT           (0x00000008)
#define IPW2100_INTA_STATUS_CHANGE             (0x00000010)
#define IPW2100_INTA_SLAVE_MODE_HOST_COMMAND_DONE  (0x00010000)

#define IPW_INTERRUPT_MASK         0xC1010013

#define IPW_BIT_GPIO_GPIO3_MASK    0x000000C0
#define IPW_BIT_GPIO_GPIO1_ENABLE  0x00000008
#define IPW_BIT_GPIO_LED_OFF       0x00002000
#define IPW_BIT_GPIO_RF_KILL       0x00010000
#define IPW_BIT_GPIO_GPIO1_MASK    0x0000000C

/* Placeholder memory shared region base addresses (missing lower bound defines) */
#define IPW_MEM_SRAM_HOST_SHARED_LOWER_BOUND			0x200
#define IPW_MEM_SRAM_HOST_INTERRUPT_AREA_LOWER_BOUND  	IPW_MEM_SRAM_HOST_SHARED_LOWER_BOUND + 0x0D80

#define IPW_MEM_HOST_SHARED_ORDINALS_TABLE_1  0x0000
#define IPW_MEM_HOST_SHARED_ORDINALS_TABLE_2  0x0000
#define IPW_MEM_HOST_SHARED_TX_QUEUE_BD_BASE      0x0000
#define IPW_MEM_HOST_SHARED_TX_QUEUE_BD_SIZE      0x0000
#define IPW_MEM_HOST_SHARED_TX_QUEUE_READ_INDEX    0x0000
#define IPW_MEM_HOST_SHARED_TX_QUEUE_WRITE_INDEX   0x0000
#define IPW_MEM_HOST_SHARED_RX_BD_BASE             0x0000
#define IPW_MEM_HOST_SHARED_RX_BD_SIZE             0x0000
#define IPW_MEM_HOST_SHARED_RX_READ_INDEX          0x0000
#define IPW_MEM_HOST_SHARED_RX_WRITE_INDEX         0x0000
#define IPW_MEM_HOST_SHARED_RX_STATUS_BASE         0x0000

#define IPW_HOST_FW_SHARED_AREA0       0x0002f200
#define IPW_HOST_FW_SHARED_AREA0_END   0x0002f510
#define IPW_HOST_FW_SHARED_AREA1       0x0002f610
#define IPW_HOST_FW_SHARED_AREA1_END   0x0002f630
#define IPW_HOST_FW_SHARED_AREA2       0x0002fa00
#define IPW_HOST_FW_SHARED_AREA2_END   0x0002fa20
#define IPW_HOST_FW_SHARED_AREA3       0x0002fc00
#define IPW_HOST_FW_SHARED_AREA3_END   0x0002fc10
#define IPW_HOST_FW_INTERRUPT_AREA     0x0002ff80
#define IPW_HOST_FW_INTERRUPT_AREA_END 0x00030000

/* Domain 1 offset not yet defined, placeholder */
#define IPW_REG_DOMAIN_1_OFFSET        0x00000000

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

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t mmio[IPW_MMIO_SIZE / 4];

    /* NIC internal memory for indirect access */
    uint8_t nic_mem[NIC_MEM_SIZE];

    /* DMA Context */
    struct {
        dma_addr_t tx_ring_base;
        dma_addr_t rx_ring_base;
        uint32_t tx_ring_size;
        uint32_t rx_ring_size;
        uint32_t tx_read;
        uint32_t tx_write;
        uint32_t rx_read;
        uint32_t rx_write;
    } dma;

    uint32_t status;
    bool in_reset;
    uint32_t power_mode;

    /* State for reset sequencing */
    bool sw_reset_done;
    bool master_disabled;
    bool clock_ready;
    bool init_done_sent;

    /* Indirect access state */
    uint32_t indirect_addr;
    uint32_t autoinc_addr;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t active = s->intr_status & s->intr_mask;
    if (active) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (msi_enabled(pdev)) {
            /* No way to de-assert MSI, just leave it */
        } else {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* Not implemented during probe - placeholder */
}

/* Helper to read NIC memory via indirect access */
static uint32_t nic_mem_read(PCIBaseState *s, uint32_t addr, unsigned size)
{
    if (addr + size > NIC_MEM_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: NIC memory read out of bounds: addr=0x%x size=%d\n",
                      __func__, addr, size);
        return 0;
    }
    uint32_t val = 0;
    memcpy(&val, s->nic_mem + addr, size);
    return val;
}

/* Helper to write NIC memory via indirect access */
static void nic_mem_write(PCIBaseState *s, uint32_t addr, uint32_t val, unsigned size)
{
    if (addr + size > NIC_MEM_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: NIC memory write out of bounds: addr=0x%x size=%d val=0x%x\n",
                      __func__, addr, size, val);
        return;
    }
    memcpy(s->nic_mem + addr, &val, size);
}

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= IPW_REG_DOA_DEBUG_AREA_START && addr <= IPW_REG_DOA_DEBUG_AREA_END) {
        /* DOA debug area: return debug value for all reads */
        return IPW_DATA_DOA_DEBUG_VALUE;
    }

    if (addr >= sizeof(s->mmio)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds MMIO read at 0x%lx size %d\n",
                      __func__, addr, size);
        return 0;
    }

    if (size != 4 && addr >= 0x80) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned access at 0x%lx size %d\n",
                      __func__, addr, size);
        return 0;
    }

    switch (addr) {
    case IPW_REG_INTA:
        val = s->intr_status;
        break;
    case IPW_REG_INTA_MASK:
        val = s->intr_mask;
        break;
    case IPW_REG_INDIRECT_ACCESS_ADDRESS:
        val = s->indirect_addr;
        break;
    case IPW_REG_INDIRECT_ACCESS_DATA:
        val = nic_mem_read(s, s->indirect_addr, size);
        break;
    case IPW_REG_AUTOINCREMENT_ADDRESS:
        val = s->autoinc_addr;
        break;
    case IPW_REG_AUTOINCREMENT_DATA:
        val = nic_mem_read(s, s->autoinc_addr, size);
        s->autoinc_addr += size;
        break;
    case IPW_REG_RESET_REG:
    {
        uint32_t reset_val = 0;
        if (s->sw_reset_done) {
            reset_val |= IPW_AUX_HOST_RESET_REG_PRINCETON_RESET;
        }
        if (s->master_disabled) {
            reset_val |= IPW_AUX_HOST_RESET_REG_MASTER_DISABLED;
        }
        val = reset_val;
        break;
    }
    case IPW_REG_GPIO:
        val = s->mmio[addr/4];
        break;
    default:
        if (addr < IPW_MMIO_SIZE) {
            val = s->mmio[addr/4];
        }
        break;
    }
    return val;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= IPW_REG_DOA_DEBUG_AREA_START && addr <= IPW_REG_DOA_DEBUG_AREA_END) {
        /* DOA debug area: ignore writes */
        return;
    }

    if (addr >= sizeof(s->mmio)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds MMIO write at 0x%lx size %d val 0x%lx\n",
                      __func__, addr, size, val);
        return;
    }

    switch (addr) {
    case IPW_REG_INTA:
        /* Write-1-to-clear on status bits */
        s->intr_status &= ~(val & IPW_INTERRUPT_MASK);
        pcibase_update_irq(s);
        break;
    case IPW_REG_INTA_MASK:
        s->intr_mask = val;
        pcibase_update_irq(s);
        break;
    case IPW_REG_INDIRECT_ACCESS_ADDRESS:
        s->indirect_addr = val & 0xFFFFFFFC; /* align to 4? */
        break;
    case IPW_REG_INDIRECT_ACCESS_DATA:
        nic_mem_write(s, s->indirect_addr, val, size);
        break;
    case IPW_REG_AUTOINCREMENT_ADDRESS:
        s->autoinc_addr = val;
        break;
    case IPW_REG_AUTOINCREMENT_DATA:
        nic_mem_write(s, s->autoinc_addr, val, size);
        s->autoinc_addr += size;
        break;
    case IPW_REG_RESET_REG:
    {
        if (val & IPW_AUX_HOST_RESET_REG_SW_RESET) {
            s->sw_reset_done = true;
        }
        if (val & IPW_AUX_HOST_RESET_REG_STOP_MASTER) {
            s->master_disabled = true;
        }
        s->mmio[addr/4] = val;
        break;
    }
    case IPW_REG_GPIO:
        s->mmio[addr/4] = val;
        break;
    default:
        if (addr < IPW_MMIO_SIZE) {
            s->mmio[addr/4] = val;
            /* Check for GP_CNTRL writes */
            if (addr == IPW_REG_GP_CNTRL) {
                if (val & IPW_AUX_HOST_GP_CNTRL_BIT_INIT_DONE) {
                    s->init_done_sent = true;
                    s->clock_ready = true;
                    /* Set CLOCK_READY bit so driver can read it back */
                    s->mmio[addr/4] |= IPW_AUX_HOST_GP_CNTRL_BIT_CLOCK_READY;
                }
                if (val & IPW_AUX_HOST_GP_CNTRL_BIT_HOST_ALLOWS_STANDBY) {
                    /* placeholder: handle standby */
                }
            }
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
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

    /* Reset state: as per cold boot */
    s->intr_status = 0;
    s->intr_mask = 0;
    s->in_reset = false;
    s->sw_reset_done = false;
    s->master_disabled = false;
    s->clock_ready = false;
    s->init_done_sent = false;
    s->indirect_addr = 0;
    s->autoinc_addr = 0;
    memset(s->mmio, 0, sizeof(s->mmio));
    memset(s->nic_mem, 0, sizeof(s->nic_mem));

    /* Initialize DOA debug area with magic value */
    for (uint32_t addr = IPW_REG_DOA_DEBUG_AREA_START; addr <= IPW_REG_DOA_DEBUG_AREA_END; addr += 4) {
        s->mmio[addr/4] = IPW_DATA_DOA_DEBUG_VALUE;
    }

    /* Initialize NIC memory with firmware-related data? Set some flags to make driver happy. */
    /* For now, we rely on the driver to write and read back. */
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

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID,  IPW2100_PCI_VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  IPW2100_PCI_DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, IPW2100_PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x1000,
        .name = "ipw2100-mmio"
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize MSI support (optional, the driver uses line-based IRQ) */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        /* MSI not available, will use line-based IRQs; that's fine */
    }

    /* Initialize internal state */
    s->intr_status = 0;
    s->intr_mask = 0;
    s->master_disabled = false;
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

    /* No DMA mapping to undo */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ipw2100_pci",
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
