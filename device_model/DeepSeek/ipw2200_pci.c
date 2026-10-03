/*
 * QEMU model of Intel PRO/Wireless 2200BG (ipw2200) PCI device.
 * Based on ipw2200 driver and hardware specifications.
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

#define TYPE_PCIBASE_DEVICE "ipw2200_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identifiers from driver pci_device_id table (first entry) */
#define VENDOR_ID 0x8086
#define DEVICE_ID 0x1043
#define CLASS_ID  0x0280

/* Direct memory-mapped registers (offsets within BAR0) */
#define IPW_INTA_RW                0x00000008
#define IPW_INTA_MASK_R            0x0000000C
#define IPW_INDIRECT_ADDR          0x00000010
#define IPW_INDIRECT_DATA          0x00000014
#define IPW_AUTOINC_ADDR           0x00000018
#define IPW_AUTOINC_DATA           0x0000001C
#define IPW_RESET_REG              0x00000020
#define IPW_GP_CNTRL_RW            0x00000024

/* Firmware memory registers (offsets from FW_MEM_REG_LOWER_BOUND 0x300000) */
#define FW_MEM_REG_LOWER_BOUND     0x00300000
#define IPW_EVENT_REG              (FW_MEM_REG_LOWER_BOUND + 0x04)
#define IPW_INTERNAL_CMD_EVENT     0x00300004
/* baseband registers; note: accessed via BAR0 but may be unhandled */
#define IPW_BASEBAND_CONTROL_STATUS 0x00200000
#define IPW_BASEBAND_CONTROL_STORE  0x00200010
#define IPW_BASEBAND_RX_FIFO_READ   0x00200004
#define FW_MEM_REG_EEPROM_ACCESS    (FW_MEM_REG_LOWER_BOUND + 0x40)

/* EEPROM registers in shared SRAM region */
#define IPW_SHARED_LOWER_BOUND     0x00000200
#define IPW_EEPROM_LOAD_DISABLE    (IPW_SHARED_LOWER_BOUND + 0x81C)
#define IPW_EEPROM_DATA            (IPW_SHARED_LOWER_BOUND + 0x820)
#define IPW_READ_INT_REGISTER      0x00000FF4

/* Ordinals table pointers in shared SRAM */
#define IPW_ORDINALS_TABLE_LOWER   (IPW_SHARED_LOWER_BOUND + 0x500) /* 0x700 */
#define IPW_ORDINALS_TABLE_1       (IPW_SHARED_LOWER_BOUND + 0x184) /* 0x384 */
#define IPW_ORDINALS_TABLE_2       (IPW_SHARED_LOWER_BOUND + 0x188) /* 0x388 */

/* DMA registers (offsets from 0x300000) */
#define IPW_DMA_I_CB_BASE          0x003000A0
#define IPW_DMA_I_DMA_CONTROL      0x003000A4
#define IPW_DMA_I_CURRENT_CB       0x003000D0
#define IPW_MEM_HALT_AND_RESET     0x003000E0

/* Shared SRAM DMA control area placeholder */
#define IPW_SHARED_SRAM_DMA_CONTROL 0x00300400

/* NIC SRAM bounds */
#define IPW_NIC_SRAM_LOWER_BOUND   0x00000000
#define IPW_NIC_SRAM_UPPER_BOUND   0x00030000

/* Register blocks */
#define IPW_REGISTER_DOMAIN1_END   0x00001000

/* Interrupt bits from driver */
#define IPW_INTA_BIT_RX_TRANSFER            (1 << 0)
#define IPW_INTA_BIT_TX_CMD_QUEUE           (1 << 1)
#define IPW_INTA_BIT_TX_QUEUE_1             (1 << 2)
#define IPW_INTA_BIT_TX_QUEUE_2             (1 << 3)
#define IPW_INTA_BIT_TX_QUEUE_3             (1 << 4)
#define IPW_INTA_BIT_TX_QUEUE_4             (1 << 5)
#define IPW_INTA_BIT_STATUS_CHANGE          (1 << 6)
#define IPW_INTA_BIT_BEACON_PERIOD_EXPIRED  (1 << 7)
#define IPW_INTA_BIT_SLAVE_MODE_HOST_CMD_DONE (1 << 8)
#define IPW_INTA_BIT_FW_INITIALIZATION_DONE (1 << 9)
#define IPW_INTA_BIT_FW_CARD_DISABLE_PHY_OFF_DONE (1 << 10)
#define IPW_INTA_BIT_RF_KILL_DONE           (1 << 11)
#define IPW_INTA_BIT_FATAL_ERROR            (1 << 12)
#define IPW_INTA_BIT_PARITY_ERROR           (1 << 13)
#define IPW_INTA_BIT_PREPARE_FOR_POWER_DOWN 0x00100000
#define IPW_INTA_BIT_POWER_DOWN             0x00200000

#define IPW_INTA_MASK_ALL                    \
        (IPW_INTA_BIT_TX_QUEUE_1            | \
         IPW_INTA_BIT_TX_QUEUE_2            | \
         IPW_INTA_BIT_TX_QUEUE_3            | \
         IPW_INTA_BIT_TX_QUEUE_4            | \
         IPW_INTA_BIT_TX_CMD_QUEUE          | \
         IPW_INTA_BIT_RX_TRANSFER           | \
         IPW_INTA_BIT_FATAL_ERROR           | \
         IPW_INTA_BIT_PARITY_ERROR          | \
         IPW_INTA_BIT_STATUS_CHANGE         | \
         IPW_INTA_BIT_FW_INITIALIZATION_DONE | \
         IPW_INTA_BIT_BEACON_PERIOD_EXPIRED | \
         IPW_INTA_BIT_SLAVE_MODE_HOST_CMD_DONE | \
         IPW_INTA_BIT_PREPARE_FOR_POWER_DOWN | \
         IPW_INTA_BIT_POWER_DOWN            | \
         IPW_INTA_BIT_RF_KILL_DONE)

/* Reset register bits */
#define IPW_RESET_REG_SW_RESET         (1 << 7)
#define IPW_RESET_REG_MASTER_DISABLED  (1 << 8)
#define IPW_RESET_REG_STOP_MASTER      (1 << 9)
#define CBD_RESET_REG_PRINCETON_RESET  (1 << 0)

/* GP control bits */
#define IPW_GP_CNTRL_BIT_INIT_DONE       0x00000004
#define IPW_GP_CNTRL_BIT_CLOCK_READY     0x00000001
#define IPW_GP_CNTRL_BIT_HOST_ALLOWS_STANDBY 0x00000002

/* EEPROM bit-bang lines */
#define EEPROM_BIT_CS   (1 << 0)
#define EEPROM_BIT_SK   (1 << 1)
#define EEPROM_BIT_DI   (1 << 2)
#define EEPROM_BIT_DO   (1 << 3)

/* DMA control bits */
#define DMA_CB_START               (1 << 0)
#define DMA_CB_STOP_AND_ABORT      (1 << 1)
#define DMA_CONTROL_SMALL_CB_CONST_VALUE 0x00540000

/* EEPROM contents: minimal valid image */
static const uint8_t default_eeprom[256] = {
    /* EEPROM version at offset EEPROM_VERSION (0x00) */
    0x01, 0x00, /* version = 1 */
    /* MAC address at offset EEPROM_MAC_ADDRESS (0x02) */
    0x00, 0x16, 0x6f, 0xaa, 0xbb, 0xcc, /* example MAC */
    /* ... remaining bytes zeroed */
    [0 ... 255] = 0
};

/* Structure for the device state */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR regions */
    MemoryRegion bar_regions[6];
    int num_bars;

    /* MMIO region */
    MemoryRegion mmio;

    /* Interrupt state */
    uint32_t inta_rw;       /* interrupt status (W1C) */
    uint32_t inta_mask;     /* interrupt mask */

    /* Other register shadows */
    uint32_t reset_reg;
    uint32_t gp_cntrl;
    uint32_t indirect_addr;
    uint32_t indirect_data;
    uint32_t autoinc_addr;
    uint32_t autoinc_data;
    uint32_t eeprom_data_reg;   /* direct data reg at IPW_EEPROM_DATA */
    uint32_t internal_cmd_event; /* IPW_INTERNAL_CMD_EVENT register */

    /* DMA state */
    uint32_t dma_cb_base;
    uint32_t dma_control;
    uint32_t current_cb;
    uint32_t dma_active;
    QEMUTimer *dma_timer;

    /* EEPROM state and data */
    uint32_t eeprom_ctrl;
    uint8_t  eeprom_data[256];
    int      eeprom_addr_bits;
    int      eeprom_bit_count;
    uint16_t eeprom_op_addr;
    uint16_t eeprom_out_data;
    bool     eeprom_reading;

    /* Indirect memory for firmware/SRAM: 256KB */
    uint8_t *indirect_mem;
    hwaddr  indirect_mem_size;

    /* Firmware init done flag */
    bool fw_init_done;
};

/* EEPROM operations */
static void eeprom_cs_rise(PCIBaseState *s)
{
    /* On CS rising, start new command */
    s->eeprom_bit_count = 0;
    s->eeprom_op_addr = 0;
    s->eeprom_reading = false;
}

static void eeprom_clock_rise(PCIBaseState *s)
{
    uint32_t di = (s->eeprom_ctrl & EEPROM_BIT_DI) ? 1 : 0;
    if (s->eeprom_bit_count < 3) {
        /* shift in opcode bits */
        s->eeprom_op_addr = (s->eeprom_op_addr << 1) | di;
    } else if (s->eeprom_bit_count < 3+8) {
        /* shift in address bits */
        s->eeprom_op_addr = (s->eeprom_op_addr << 1) | di;
    } else if (s->eeprom_bit_count == 3+8) {
        /* dummy bit */
    } else if (s->eeprom_bit_count < 3+8+1+16) {
        /* read data: output next bit */
        if (!s->eeprom_reading) {
            /* start read: prepare data from EEPROM array */
            uint8_t addr = (s->eeprom_op_addr >> 8) & 0xFF;
            s->eeprom_out_data = (s->eeprom_data[addr*2] << 8) | s->eeprom_data[addr*2+1];
            s->eeprom_reading = true;
        }
        if (s->eeprom_out_data & 0x8000) {
            s->eeprom_ctrl |= EEPROM_BIT_DO;
        } else {
            s->eeprom_ctrl &= ~EEPROM_BIT_DO;
        }
        s->eeprom_out_data <<= 1;
    }
    if (s->eeprom_bit_count < 3+8+1+16) {
        s->eeprom_bit_count++;
    }
}

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t active = s->inta_rw & s->inta_mask;
    if (active) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint32_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    switch (addr) {
    case IPW_INTA_RW:
        val = s->inta_rw;
        break;
    case IPW_INTA_MASK_R:
        val = s->inta_mask;
        break;
    case IPW_RESET_REG:
        val = s->reset_reg;
        break;
    case IPW_GP_CNTRL_RW:
        val = s->gp_cntrl;
        break;
    case IPW_INDIRECT_ADDR:
        val = s->indirect_addr;
        break;
    case IPW_INDIRECT_DATA:
        /* Indirect read from indirect_mem */
        if (s->indirect_addr < s->indirect_mem_size) {
            val = ldl_le_p(s->indirect_mem + s->indirect_addr);
        }
        break;
    case IPW_AUTOINC_ADDR:
        val = s->autoinc_addr;
        break;
    case IPW_AUTOINC_DATA:
        if (s->autoinc_addr < s->indirect_mem_size) {
            val = ldl_le_p(s->indirect_mem + s->autoinc_addr);
            s->autoinc_addr += 4;
        }
        break;
    case IPW_READ_INT_REGISTER:
        val = s->inta_rw;   /* driver reads interrupt status here */
        break;
    case IPW_DMA_I_CB_BASE:
        val = s->dma_cb_base;
        break;
    case IPW_DMA_I_DMA_CONTROL:
        val = s->dma_control;
        break;
    case IPW_DMA_I_CURRENT_CB:
        val = s->current_cb;
        break;
    case IPW_MEM_HALT_AND_RESET:
        val = 0;
        break;
    case FW_MEM_REG_EEPROM_ACCESS:
        val = s->eeprom_ctrl;
        break;
    case IPW_EEPROM_LOAD_DISABLE:
        val = 0;  /* driver may read, but we return 0 */
        break;
    case IPW_EEPROM_DATA:
        val = s->eeprom_data_reg;
        break;
    case IPW_INTERNAL_CMD_EVENT:
        val = s->internal_cmd_event;
        break;
    case 0x301100: /* specific GPIO register */
        val = 0x00010000;
        break;
    default:
        if (addr >= IPW_NIC_SRAM_LOWER_BOUND && addr < IPW_NIC_SRAM_UPPER_BOUND) {
            if (addr < s->indirect_mem_size) {
                val = ldl_le_p(s->indirect_mem + addr);
            }
        }
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);

    switch (addr) {
    case IPW_INTA_RW:
        /* W1C: clear bits written with 1 */
        s->inta_rw &= ~(val & IPW_INTA_MASK_ALL);
        pcibase_update_irq(s);
        break;
    case IPW_INTA_MASK_R:
        s->inta_mask = val;
        pcibase_update_irq(s);
        break;
    case IPW_RESET_REG:
        s->reset_reg = val;
        if (val & IPW_RESET_REG_STOP_MASTER) {
            s->reset_reg |= IPW_RESET_REG_MASTER_DISABLED;
        }
        if (val & IPW_RESET_REG_SW_RESET) {
            s->gp_cntrl &= ~IPW_GP_CNTRL_BIT_INIT_DONE;
        }
        break;
    case IPW_GP_CNTRL_RW:
        s->gp_cntrl |= val; /* bits set by writing 1 */
        if (val & IPW_GP_CNTRL_BIT_INIT_DONE) {
            s->gp_cntrl |= IPW_GP_CNTRL_BIT_CLOCK_READY;
        }
        break;
    case IPW_INDIRECT_ADDR:
        s->indirect_addr = val & (s->indirect_mem_size - 1);
        break;
    case IPW_INDIRECT_DATA:
        if (s->indirect_addr < s->indirect_mem_size) {
            stl_le_p(s->indirect_mem + s->indirect_addr, val);
        }
        break;
    case IPW_AUTOINC_ADDR:
        s->autoinc_addr = val & (s->indirect_mem_size - 1);
        break;
    case IPW_AUTOINC_DATA:
        if (s->autoinc_addr < s->indirect_mem_size) {
            stl_le_p(s->indirect_mem + s->autoinc_addr, val);
            s->autoinc_addr += 4;
        }
        break;
    case IPW_DMA_I_CB_BASE:
        s->dma_cb_base = val;
        break;
    case IPW_DMA_I_DMA_CONTROL:
        s->dma_control = val;
        if (val & DMA_CB_START) {
            s->dma_active = 1;
            timer_mod(s->dma_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 50);
        }
        if (val & DMA_CB_STOP_AND_ABORT) {
            s->dma_active = 0;
            timer_del(s->dma_timer);
        }
        break;
    case IPW_DMA_I_CURRENT_CB:
        s->current_cb = val;
        break;
    case IPW_MEM_HALT_AND_RESET:
        /* ignore writes */
        break;
    case FW_MEM_REG_EEPROM_ACCESS:
        if ((val & EEPROM_BIT_CS) && !(s->eeprom_ctrl & EEPROM_BIT_CS)) {
            eeprom_cs_rise(s);
        }
        if ((val & EEPROM_BIT_SK) && !(s->eeprom_ctrl & EEPROM_BIT_SK)) {
            eeprom_clock_rise(s);
        }
        s->eeprom_ctrl = val;
        break;
    case IPW_EEPROM_LOAD_DISABLE:
        /* ignore write; driver may disable EEPROM load */
        break;
    case IPW_EEPROM_DATA:
        s->eeprom_data_reg = val;
        break;
    case IPW_INTERNAL_CMD_EVENT:
        s->internal_cmd_event = val;
        /* firmware may trigger actions; for now, just store */
        break;
    case 0x301100:
        /* GPIO register; ignore writes */
        break;
    default:
        if (addr >= IPW_NIC_SRAM_LOWER_BOUND && addr < IPW_NIC_SRAM_UPPER_BOUND) {
            if (addr < s->indirect_mem_size) {
                stl_le_p(s->indirect_mem + addr, val);
            }
        }
        break;
    }
}

static void dma_complete_timer(void *opaque)
{
    PCIBaseState *s = opaque;
    s->dma_active = 0;
    s->current_cb = s->dma_cb_base + (64 * sizeof(uint32_t) * 4);
    s->inta_rw |= IPW_INTA_BIT_FW_INITIALIZATION_DONE;
    pcibase_update_irq(s);
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

    s->inta_rw = 0;
    s->inta_mask = 0;
    s->reset_reg = 0;
    s->gp_cntrl = 0;
    s->indirect_addr = 0;
    s->indirect_data = 0;
    s->autoinc_addr = 0;
    s->autoinc_data = 0;
    s->eeprom_data_reg = 0;
    s->internal_cmd_event = 0;
    s->dma_cb_base = 0;
    s->dma_control = 0;
    s->current_cb = 0;
    s->dma_active = 0;
    s->fw_init_done = false;
    /* Re-initialize EEPROM with default data */
    memcpy(s->eeprom_data, default_eeprom, sizeof(default_eeprom));
    s->eeprom_ctrl = 0;
    s->eeprom_addr_bits = 0;
    s->eeprom_bit_count = 0;
    s->eeprom_reading = false;
    /* Clear indirect memory */
    memset(s->indirect_mem, 0, s->indirect_mem_size);
    /* Pre-setup initial firmware table pointers */
    /* table0_addr at IPW_ORDINALS_TABLE_LOWER: points to a table of size 1? */
    if (IPW_ORDINALS_TABLE_LOWER < s->indirect_mem_size) {
        stl_le_p(s->indirect_mem + IPW_ORDINALS_TABLE_LOWER, 0x00000001);
    }
    /* table1_addr points to itself? */
    if (IPW_ORDINALS_TABLE_1 < s->indirect_mem_size) {
        stl_le_p(s->indirect_mem + IPW_ORDINALS_TABLE_1, IPW_ORDINALS_TABLE_1);
    }
    if (IPW_ORDINALS_TABLE_2 < s->indirect_mem_size) {
        stl_le_p(s->indirect_mem + IPW_ORDINALS_TABLE_2, IPW_ORDINALS_TABLE_2);
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

    /* No MSI/MSIX, use legacy INTx */
    pdev->cap_present &= ~(QEMU_PCI_CAP_MSI | QEMU_PCI_CAP_MSIX);

    /* Allocate indirect memory (256KB) */
    s->indirect_mem_size = 256 * 1024;
    s->indirect_mem = g_malloc0(s->indirect_mem_size);

    /* Create MMIO BAR0 */
    memory_region_init_io(&s->mmio, OBJECT(s), &pcibase_mmio_ops, s, "ipw2200-mmio", 0x40000);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);

    /* Create DMA completion timer */
    s->dma_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, dma_complete_timer, s);

    /* Load default EEPROM */
    memcpy(s->eeprom_data, default_eeprom, sizeof(default_eeprom));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    timer_del(s->dma_timer);
    timer_free(s->dma_timer);
    g_free(s->indirect_mem);
    msi_uninit(pdev);
}

static const VMStateDescription vmstate_pcibase = {
    .name = "ipw2200_pci",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(inta_rw, PCIBaseState),
        VMSTATE_UINT32(inta_mask, PCIBaseState),
        VMSTATE_UINT32(reset_reg, PCIBaseState),
        VMSTATE_UINT32(gp_cntrl, PCIBaseState),
        VMSTATE_UINT32(indirect_addr, PCIBaseState),
        VMSTATE_UINT32(autoinc_addr, PCIBaseState),
        VMSTATE_UINT32(eeprom_data_reg, PCIBaseState),
        VMSTATE_UINT32(internal_cmd_event, PCIBaseState),
        VMSTATE_UINT32(dma_cb_base, PCIBaseState),
        VMSTATE_UINT32(dma_control, PCIBaseState),
        VMSTATE_UINT32(current_cb, PCIBaseState),
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
