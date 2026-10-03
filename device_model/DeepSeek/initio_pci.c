/*
 * QEMU model for Initio INI-9X00U/UW SCSI adapter
 * Based on Linux driver initio.c
 * Emulates minimal hardware to allow driver probing
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

#define TYPE_PCIBASE_DEVICE "initio_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI identification */
#define VENDOR_ID 0x1101   /* PCI_VENDOR_ID_INIT */
#define DEVICE_ID 0x9500
#define CLASS_ID  0x0100   /* PCI_CLASS_STORAGE_SCSI */

/* BAR0 size from driver's request_region (256 bytes) */
#define BAR0_SIZE 0x100

/* Hardware register offsets (from initio.c plus plausible mapping) */
#define TUL_NVRAM       0x5D
#define TUL_GCTRL       0x54
#define TUL_GCTRL1      0x55
#define TUL_PCMD        0x04
#define TUL_SCFG1       0x94

/* SCSI register offsets (assigned) */
#define TUL_SCtrl0      0x00
#define TUL_SCmd        0x01
#define TUL_SBusId      0x02
#define TUL_SIdent      0x03
#define TUL_STimeOut    0x05
#define TUL_SPeriod     0x06
#define TUL_SConfig     0x07
#define TUL_SScsiId     0x08
#define TUL_SSignal     0x09
#define TUL_SStatus0    0x0A
#define TUL_SStatus1    0x0B
#define TUL_SInt        0x0C
#define TUL_Int         0x0D
#define TUL_Mask        0x0E
#define TUL_SCnt0       0x10   /* 32-bit */
#define TUL_SFifo       0x14
#define TUL_SFifoCnt    0x15
#define TUL_XStatus     0x20
#define TUL_XCmd        0x21
#define TUL_XCtrl       0x22
#define TUL_XCntH       0x24
#define TUL_XAddH       0x28
#define TUL_SCtrl1      0x30

/* Control/status bit definitions (from driver constants) */
#define TUL_GCTRL_EEPROM_BIT    0x04

#define TSC_RST_CHIP    0x02
#define TSC_RST_BUS     0x01
#define TSC_HW_RESELECT 0x04
#define TSC_EN_BUS_IN   0x01
#define TSC_EN_SCSI_PAR 0x20
#define TSC_EN_LATCH    0x80
#define TSC_DIS_SCSIRST 0x01
#define TSC_INITIATOR   0x40
#define TSC_ALT_PERIOD  0x02
#define TSC_SET_ACK     0x40
#define TSC_SET_ATN     0x08
#define TSC_WIDE_SCSI   0x80
#define TSC_XF_FIFO_OUT 0x03
#define TSC_XF_FIFO_IN  0x83
#define TSC_XF_DMA_OUT  0x43
#define TSC_XF_DMA_IN   0xC3
#define TSC_CMD_COMP    0x84
#define TSC_MSG_ACCEPT  0x0F
#define TSC_SELATNSTOP  0x1E
#define TSC_SEL_ATN     0x11
#define TSC_SEL_ATN3    0x31

#define TAX_X_CLR_FIFO  0x08
#define TAX_X_ABT       0x04
#define TAX_SG_IN       0xA1
#define TAX_X_IN        0x21
#define TAX_SG_OUT      0x81
#define TAX_X_OUT       0x01

#define TSS_INT_PENDING 0x80
#define TSS_PH_MASK     0x07
#define TSS_RESEL_INT   0x80
#define TSS_FUNC_COMP   0x01
#define TSS_BUS_SERV    0x20
#define TSS_SEL_TIMEOUT 0x40
#define TSS_DISC_INT    0x08
#define TSS_CMD_PH_CMP  0x20
#define TSS_PAR_ERROR   0x08
#define TSS_XFER_CMP    0x04
#define TSS_SCSIRST_INT 0x10

#define HCC_EN_PAR              0x02
#define HCC_ACT_TERM1           0x04
#define HCC_ACT_TERM2           0x08
#define HCC_SCSI_RESET          0x01
#define HCC_AUTO_TERM           0x10

#define TCF_DRV_255_63          0x0400
#define TCF_BUSY                0x0400
#define TCF_SYNC_DONE           0x0200
#define TCF_WDTR_DONE           0x0100
#define TCF_EN_255              0x0040
#define TCF_NO_WDTR             0x0020
#define TCF_NO_SYNC_NEGO        0x0010
#define TCF_DRV_EN_TAG          0x0800
#define TCF_SCSI_RATE           0x0007

#define HCF_EXPECT_DONE_DISC    0x20
#define HCF_EXPECT_DISC         0x01

#define SCB_RENT        0x01
#define SCB_PEND        0x02
#define SCB_BUSY        0x10
#define SCB_DONE        0x20
#define SCB_SELECT      0x08

#define SCF_DONE        0x01
#define SCF_POST        0x02
#define SCF_SG          0x80
#define SCF_SENSE       0x04
#define SCF_DIR         0x18
#define SCF_POLL        0x40
#define SCF_NO_XF       0x18
#define SCF_DOUT        0x10
#define SCF_DIN         0x08
#define SCF_NO_DCHK     0x00

#define SENSE_SIZE              14
#define i91u_MAXQUEUE           2
#define MAX_OFFSET              15
#define TOTAL_SG_ENTRY          32
#define MAX_TARGETS             16

#define INITIO_SIGNATURE       0xC925

/* EEPROM bit-banging constants */
#define SE2CS   0x01
#define SE2CLK  0x02
#define SE2DO   0x04
#define SE2DI   0x08

/* EEPROM states */
#define EEP_IDLE            0
#define EEP_INSTRUCTION     1
#define EEP_READ_DATA       2
#define EEP_WRITE_DATA      3
#define EEP_WRITE_COMPLETE  4

/* SCSI command timer delay (milliseconds) */
#define CMD_TIMEOUT_MS      10

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
    uint8_t irq_status;
    uint8_t irq_mask;

    /* Hardware Register Shadows */
    uint8_t regs[BAR0_SIZE];

    /* DMA Context (unused) */
    struct {
        dma_addr_t addr;
        uint32_t len;
        uint32_t direction;
        bool active;
        uint32_t cmd;
    } dma;

    /* Operational status flags */
    uint8_t status;
    uint8_t reset_state;

    /* Timers */
    QEMUTimer *cmd_timer;
    QEMUTimer *dma_timer;

    /* EEPROM emulation state */
    uint16_t eeprom_data[32];
    uint8_t eeprom_state;
    uint8_t eeprom_bitcount;
    uint8_t eeprom_instr;
    uint8_t eeprom_addr;
    uint16_t eeprom_buffer;
    uint16_t eeprom_outshift;
    bool eeprom_di_bit;
    bool eeprom_last_cs;
    bool eeprom_last_clk;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* The driver inverts the mask? Actually, mask bits are 1 to disable. */
    uint8_t pending = s->irq_status & ~s->irq_mask;
    if (pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static void cmd_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    s->regs[TUL_SInt] |= TSS_SEL_TIMEOUT;
    s->irq_status = s->regs[TUL_SInt];
    pcibase_update_irq(s);
}

static void dma_timer_cb(void *opaque)
{
    PCIBaseState *s = opaque;
    /* DMA not implemented for probe; just complete with zero bytes */
    s->regs[TUL_SInt] |= TSS_FUNC_COMP | TSS_XFER_CMP;
    s->irq_status = s->regs[TUL_SInt];
    pcibase_update_irq(s);
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= BAR0_SIZE) {
        return ~0ULL;
    }

    switch (size) {
    case 1:
        val = s->regs[addr];
        break;
    case 2:
        val = lduw_le_p(&s->regs[addr]);
        break;
    case 4:
        val = ldl_le_p(&s->regs[addr]);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid read size %u at 0x%"HWADDR_PRIx"\n",
                      __func__, size, addr);
        return ~0ULL;
    }

    /* Special handling for certain registers */
    switch (addr) {
    case TUL_SInt:
        /* Reading SInt clears it */
        val = s->regs[TUL_SInt];
        s->regs[TUL_SInt] = 0;
        s->irq_status = 0;
        pcibase_update_irq(s);
        break;
    case TUL_Int:
        /* TUL_Int mirrors SInt */
        val = s->regs[TUL_SInt];
        break;
    case TUL_SStatus0:
        /* Provide phase 0 (bus free) and pending flag */
        val = (s->regs[TUL_SInt] ? TSS_INT_PENDING : 0);
        break;
    case TUL_NVRAM:
        /* EEPROM status bit */
        val = s->eeprom_di_bit ? SE2DI : 0;
        break;
    default:
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= BAR0_SIZE) {
        return;
    }

    /* Special handling for EEPROM bit-banging */
    if (addr == TUL_NVRAM) {
        uint8_t v = val & 0xFF;
        bool cs = v & SE2CS;
        bool clk = v & SE2CLK;
        bool do_bit = v & SE2DO;

        if (cs && !s->eeprom_last_cs) {
            /* CS rising edge: reset state machine */
            s->eeprom_state = EEP_IDLE;
            s->eeprom_bitcount = 0;
            s->eeprom_instr = 0;
            s->eeprom_di_bit = false;
        }
        if (!cs && s->eeprom_last_cs) {
            /* CS falling edge: finalize any write */
            if (s->eeprom_state == EEP_WRITE_DATA && s->eeprom_bitcount == 16) {
                s->eeprom_data[s->eeprom_addr] = s->eeprom_buffer;
            }
            s->eeprom_state = EEP_IDLE;
            s->eeprom_di_bit = false;
        }

        if (cs) {
            if (clk && !s->eeprom_last_clk) {
                /* CLK rising edge */
                switch (s->eeprom_state) {
                case EEP_IDLE:
                    if (do_bit) {
                        /* Start bit detected */
                        s->eeprom_state = EEP_INSTRUCTION;
                        s->eeprom_bitcount = 0;
                        s->eeprom_instr = 0;
                    }
                    break;
                case EEP_INSTRUCTION:
                    s->eeprom_instr = (s->eeprom_instr << 1) | do_bit;
                    s->eeprom_bitcount++;
                    if (s->eeprom_bitcount == 8) {
                        uint8_t op = (s->eeprom_instr >> 6) & 3;
                        s->eeprom_addr = s->eeprom_instr & 0x3F;
                        if (op == 2) { /* READ */
                            s->eeprom_state = EEP_READ_DATA;
                            s->eeprom_outshift = s->eeprom_data[s->eeprom_addr];
                            s->eeprom_bitcount = 0;
                        } else if (op == 1) { /* WRITE */
                            s->eeprom_state = EEP_WRITE_DATA;
                            s->eeprom_buffer = 0;
                            s->eeprom_bitcount = 0;
                        } else if (op == 0 || op == 3) {
                            /* EWEN/EWDS: ignore */
                            s->eeprom_state = EEP_IDLE;
                        }
                    }
                    break;
                case EEP_READ_DATA:
                    /* Do nothing on rising edge; data is output on falling */
                    break;
                case EEP_WRITE_DATA:
                    s->eeprom_buffer = (s->eeprom_buffer << 1) | do_bit;
                    s->eeprom_bitcount++;
                    if (s->eeprom_bitcount == 16) {
                        /* Write goes to EEPROM on CS falling edge, track state */
                        s->eeprom_state = EEP_WRITE_COMPLETE;
                        s->eeprom_di_bit = true; /* ready */
                    }
                    break;
                case EEP_WRITE_COMPLETE:
                    /* CS will be dropped after checking DI=1 */
                    break;
                }
            } else if (!clk && s->eeprom_last_clk) {
                /* CLK falling edge */
                if (s->eeprom_state == EEP_READ_DATA) {
                    s->eeprom_di_bit = (s->eeprom_outshift >> 15) & 1;
                    s->eeprom_outshift <<= 1;
                    s->eeprom_bitcount++;
                    if (s->eeprom_bitcount == 16) {
                        s->eeprom_state = EEP_IDLE;
                    }
                } else if (s->eeprom_state == EEP_WRITE_DATA) {
                    /* Data sampling on rising, no action here */
                }
            }
        }

        s->eeprom_last_cs = cs;
        s->eeprom_last_clk = clk;
        return;
    }

    /* Write to internal register shadow */
    switch (size) {
    case 1:
        s->regs[addr] = val & 0xFF;
        break;
    case 2:
        stw_le_p(&s->regs[addr], val);
        break;
    case 4:
        stl_le_p(&s->regs[addr], val);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid write size %u at 0x%"HWADDR_PRIx"\n",
                      __func__, size, addr);
        return;
    }

    /* Handle side effects */
    switch (addr) {
    case TUL_SCmd:
        if ((val & 0xFF) == TSC_SEL_ATN || (val & 0xFF) == TSC_SEL_ATN3 ||
            (val & 0xFF) == TSC_SELATNSTOP) {
            /* Selection command: start timeout timer */
            timer_mod(s->cmd_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + CMD_TIMEOUT_MS);
        }
        break;
    case TUL_SCtrl0:
        if (val & TSC_RST_BUS) {
            /* SCSI bus reset: set interrupt immediately */
            s->regs[TUL_SInt] |= TSS_SCSIRST_INT;
            s->irq_status = s->regs[TUL_SInt];
            pcibase_update_irq(s);
        }
        break;
    case TUL_Mask:
        s->irq_mask = val & 0xFF;
        pcibase_update_irq(s);
        break;
    case TUL_XCmd:
        /* DMA commands not needed for probe; ignore */
        break;
    default:
        break;
    }
}

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

    memset(s->regs, 0, sizeof(s->regs));
    s->irq_mask = 0;
    s->irq_status = 0;
    s->status = 0;
    s->reset_state = 0;
    s->dma.addr = 0;
    s->dma.len = 0;
    s->dma.direction = 0;
    s->dma.active = false;
    s->dma.cmd = 0;

    /* Pre-fill EEPROM with default data matching INITIO_SIGNATURE */
    memset(s->eeprom_data, 0, sizeof(s->eeprom_data));
    s->eeprom_data[0] = INITIO_SIGNATURE;       /* NVM_Signature */
    s->eeprom_data[31] = INITIO_SIGNATURE;      /* Checksum (sum of first 31 words = 0xC925) */

    s->eeprom_state = EEP_IDLE;
    s->eeprom_bitcount = 0;
    s->eeprom_last_cs = false;
    s->eeprom_last_clk = false;
    s->eeprom_di_bit = false;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) { .index = 0, .type = BAR_TYPE_PIO, .size = BAR0_SIZE, .name = "initio-pio" };
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);

    /* Create timers */
    s->cmd_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, cmd_timer_cb, s);
    s->dma_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, dma_timer_cb, s);

    /* Legacy IRQ only */
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
    timer_del(s->cmd_timer);
    timer_free(s->cmd_timer);
    timer_del(s->dma_timer);
    timer_free(s->dma_timer);
}

static const VMStateDescription vmstate_pcibase = {
    .name = "initio_pci",
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

type_init(pcibase_register_types)
