/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
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
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "rt2400pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define RT2400PCI_VENDOR_ID 0x1814
#define RT2400PCI_DEVICE_ID 0x0101
#define RT2400PCI_CLASS_ID  0x0280

#define CSR_REG_BASE 0x0000
#define CSR_REG_SIZE 0x014c

#define CSR0 0x0000
#define CSR1 0x0004
#define CSR3 0x000c
#define CSR5 0x0014
#define CSR7 0x001c
#define CSR8 0x0020
#define CSR9 0x0024
#define CSR11 0x002c
#define CSR12 0x0030
#define CSR14 0x0038
#define CSR15 0x003c
#define CSR16 0x0040
#define CSR17 0x0044
#define CSR18 0x0048
#define CSR19 0x004c
#define CSR20 0x0050
#define CSR21 0x0054
#define TXCSR0 0x0060
#define TXCSR1 0x0064
#define TXCSR2 0x0068
#define TXCSR3 0x006c
#define TXCSR4 0x0070
#define TXCSR5 0x0074
#define TXCSR6 0x0078
#define RXCSR0 0x0080
#define RXCSR1 0x0084
#define RXCSR2 0x0088
#define RXCSR3 0x0090
#define ARCSR0 0x0098
#define ARCSR1 0x009c
#define CNT0 0x00a0
#define CNT3 0x00b8
#define CNT4 0x00bc
#define PWRCSR0 0x00c4
#define PSCSR0 0x00c8
#define PSCSR1 0x00cc
#define PSCSR2 0x00d0
#define PSCSR3 0x00d4
#define PWRCSR1 0x00d8
#define TIMECSR 0x00dc
#define MACCSR0 0x00e0
#define MACCSR1 0x00e4
#define RALINKCSR 0x00e8
#define BBPCSR 0x00f0
#define RFCSR 0x00f4
#define LEDCSR 0x00f8
#define GPIOCSR 0x0120
#define BCNCSR1 0x0130
#define MACCSR2 0x0134
#define ARCSR2 0x013c
#define ARCSR3 0x0140
#define ARCSR4 0x0144
#define ARCSR5 0x0148

#define EEPROM_BASE 0x0000
#define EEPROM_SIZE 0x0100

#define CSR7_TBCN_EXPIRE 0x00000001
#define CSR7_RXDONE 0x00000040
#define CSR7_TXDONE_ATIMRING 0x00000010
#define CSR7_TXDONE_PRIORING 0x00000020
#define CSR7_TXDONE_TXRING 0x00000008

#define CSR8_TBCN_EXPIRE 0x00000001
#define CSR8_RXDONE 0x00000040
#define CSR8_TXDONE_ATIMRING 0x00000010
#define CSR8_TXDONE_PRIORING 0x00000020
#define CSR8_TXDONE_TXRING 0x00000008

#define BBPCSR_BUSY 0x00008000
#define BBPCSR_VALUE 0x000000ff
#define BBPCSR_REGNUM 0x00007f00
#define BBPCSR_WRITE_CONTROL 0x00010000

#define RFCSR_BUSY 0x80000000
#define RFCSR_VALUE 0x00ffffff
#define RFCSR_NUMBER_OF_BITS 0x1f000000
#define RFCSR_IF_SELECT 0x20000000

#define PWRCSR1_SET_STATE 0x00000001
#define PWRCSR1_BBP_DESIRE_STATE 0x00000006
#define PWRCSR1_RF_DESIRE_STATE 0x00000018
#define PWRCSR1_PUT_TO_SLEEP 0x00000200
#define PWRCSR1_BBP_CURR_STATE 0x00000060
#define PWRCSR1_RF_CURR_STATE 0x00000180

#define TXCSR0_KICK_TX 0x00000001
#define TXCSR0_KICK_ATIM 0x00000002
#define TXCSR0_KICK_PRIO 0x00000004
#define TXCSR0_ABORT 0x00000008

#define TXD_W0_OWNER_NIC 0x00000001
#define TXD_W0_VALID 0x00000002
#define TXD_W0_RESULT 0x0000001c
#define TXD_W0_RETRY_COUNT 0x000000e0

#define RXD_W0_OWNER_NIC 0x00000001
#define RXD_W0_CRC_ERROR 0x00000020
#define RXD_W0_PHYSICAL_ERROR 0x00000080

#define EEPROM_ANTENNA 0x0b
#define EEPROM_ANTENNA_RF_TYPE 0x0040
#define RF2420 0x0000
#define RF2421 0x0001

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
    uint32_t intr_status;
    uint32_t intr_mask;

    uint32_t csr[CSR_REG_SIZE / 4];
    uint16_t eeprom[EEPROM_SIZE / 2];

    uint32_t tx_ring_reg;
    uint32_t rx_ring_reg;
    uint32_t prio_ring_reg;
    uint32_t atim_ring_reg;
    uint32_t beacon_ring_reg;

    uint32_t status;
    bool reset_pending;
    uint32_t pm_state;
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    
    /* 
     * Driver logic: if (CSR7 & ~CSR8) != 0, raise IRQ.
     * CSR8 acts as the interrupt mask register.
     */
    uint32_t csr7 = s->csr[CSR7 / 4];
    uint32_t csr8 = s->csr[CSR8 / 4];
    
    if (csr7 & ~csr8) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* DMA logic requires TXD/RXD descriptor bitfields (e.g., TXD_W0_OWNER_NIC) */
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < CSR_REG_SIZE) {
        val = s->csr[addr / 4];
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < CSR_REG_SIZE) {
        uint32_t index = addr / 4;
        
        switch (addr) {
        case CSR7:
            /* W1C (Write 1 to Clear) */
            s->csr[index] &= ~val;
            pcibase_update_irq(s);
            break;
        case CSR8:
            s->csr[index] = val;
            pcibase_update_irq(s);
            break;
        case TXCSR0:
            s->csr[index] = val;
            if (val & TXCSR0_KICK_TX) {
                s->csr[CSR7 / 4] |= CSR7_TXDONE_TXRING;
                s->csr[index] &= ~TXCSR0_KICK_TX;
            }
            if (val & TXCSR0_KICK_ATIM) {
                s->csr[CSR7 / 4] |= CSR7_TXDONE_ATIMRING;
                s->csr[index] &= ~TXCSR0_KICK_ATIM;
            }
            if (val & TXCSR0_KICK_PRIO) {
                s->csr[CSR7 / 4] |= CSR7_TXDONE_PRIORING;
                s->csr[index] &= ~TXCSR0_KICK_PRIO;
            }
            pcibase_update_irq(s);
            pcibase_do_dma(s, true);
            break;
        case BBPCSR:
            s->csr[index] = val & ~BBPCSR_BUSY;
            break;
        case RFCSR:
            s->csr[index] = val & ~RFCSR_BUSY;
            break;
        case PWRCSR1:
            s->csr[index] = val & ~PWRCSR1_SET_STATE;
            s->csr[index] &= ~(PWRCSR1_BBP_CURR_STATE | PWRCSR1_RF_CURR_STATE);
            s->csr[index] |= ((val & PWRCSR1_BBP_DESIRE_STATE) << 4);
            s->csr[index] |= ((val & PWRCSR1_RF_DESIRE_STATE) << 4);
            break;
        default:
            s->csr[index] = val;
            break;
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
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

    memset(s->csr, 0, sizeof(s->csr));
    memset(s->eeprom, 0, sizeof(s->eeprom));
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  RT2400PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  RT2400PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, RT2400PCI_CLASS_ID );
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
    s->bar_info[0].size = CSR_REG_SIZE;
    s->bar_info[0].name = "rt2400pci-mmio";  
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
    .name = "rt2400pci_pci",
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
