/*
 * This QEMU device model emulates Initio INI-A100U2W SCSI controller.
 * Generated from Linux driver a100u2w.c, Phase 4: Debug & Update.
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

#define TYPE_PCIBASE_DEVICE "inia100_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identification */
#define PCI_VENDOR_ID_INIT       0x1101
#define PCI_DEVICE_ID_INIA100    0x1060
#define PCI_CLASS_STORAGE_SCSI   0x0100

/* Register offset macros (from driver source) */
#define ORC_CMD_VERSION     0x01
#define ORC_HCTRL           0xA5
#define ORC_HSTUS           0xA6
#define ORC_HDATA           0xA4
#define ORC_PQUEUE          0xA8
#define ORC_RQUEUE          0xAA
#define ORC_RQUEUECNT       0xAB
#define ORC_FWBASEADR       0xAC
#define ORC_EBIOSADR0       0xB0
#define ORC_EBIOSADR2       0xB2
#define ORC_EBIOSDATA       0xB3
#define ORC_RISCCTL         0xE0
#define ORC_RISCRAM         0xEC
#define ORC_GCFG            0xA2
#define ORC_SCBSIZE         0xB7
#define ORC_SCBBASE0        0xB8
#define ORC_SCBBASE1        0xBC
#define ORC_GIMSK           0xA1

/* Additional bit definitions from driver */
#define HOSTSTOP    0x02
#define RREADY      0x01
#define SCSIRST     0x80
#define HDO         0x40
#define HDI         0x02
#define DEVRST      0x01

/* RISCCTL bits (arbitrary values as not defined in snippets) */
#define PRGMRST     0x02
#define DOWNLOAD    0x01

/* GCFG bit */
#define EEPRG       0x40

#define ORC_CMD_SET_NVM  0x03
#define ORC_CMD_GET_NVM  0x04
#define ORC_CMD_ABORT_SCB 0x06

/* Command protocol phases */
#define CMD_PHASE_IDLE       0
#define CMD_PHASE_NVM_ADDR   1
#define CMD_PHASE_NVM_VALUE  2
#define CMD_PHASE_RESP       3
#define CMD_PHASE_ABORT_IDX  4

/* Interrupt mask bits */
#define GIMSK_RP_FIFO  0x04

/* SCB layout (64 bytes for both 32/64-bit guests) */
#define SCB_SIZE  64

/* Default NVRAM content from driver (64 bytes) with corrected checksum */
static const uint8_t default_nvram[64] = {
    0x01, 0x11, 0x60, 0x10, 0x00, 0x01, 0x11, 0x60, 0x10, 0x00, 0x00, 0x01,
    0x01, 0x01, 0x00, 0x00, 0x07, 0x83, 0x20, 0x0A, 0x00, 0x00,
    0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8,
    0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8,
    0x07, 0x83, 0x20, 0x0A, 0x00, 0x00,
    0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8,
    0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8,
    0x91
};

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    int num_bars;

    /* No MSI/MSI-X used */

    /* Hardware Register Shadows: all registers as byte array */
    uint8_t regs[256];

    /* NVRAM content (64 bytes) */
    uint8_t nvram[64];

    /* Command protocol state */
    int cmd_phase;
    uint8_t cmd_cmd;
    uint8_t cmd_nvm_addr;
    int cmd_resp_left;
    bool cmd_resp_hi;

    /* RQUEUE FIFO */
    uint8_t rqueue_fifo[256];
    int rqueue_head, rqueue_tail, rqueue_count;

    /* BIOS ROM (64K) */
    uint8_t bios_rom[0x10000];

    /* EBIOS address (24-bit) */
    uint32_t ebios_addr;

    /* RISC RAM (4K) and current pointer */
    uint8_t riscram[0x1000];
    uint32_t riscram_addr;

    /* SCB base addresses */
    uint32_t scb_base0;
    uint32_t scb_base1;
};

/* Internal helper for status-triggered signaling */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int irq_level = 0;
    if (s->rqueue_count > 0) {
        if (!(s->regs[ORC_GIMSK] & GIMSK_RP_FIFO)) {
            irq_level = 1;
        }
    }
    pci_set_irq(pdev, irq_level);
}

/* SCB processing */
static void process_scb_post(PCIBaseState *s, uint8_t scb_index)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t buf[SCB_SIZE];
    dma_addr_t scb_addr = s->scb_base0 + scb_index * SCB_SIZE;

    pci_dma_read(pdev, scb_addr, buf, SCB_SIZE);
    /* Complete with success: host adapter status 0, target status 0 */
    buf[28] = 0x00; /* hastat */
    buf[29] = 0x00; /* tastat */
    buf[30] = 0x00; /* status */
    pci_dma_write(pdev, scb_addr, buf, SCB_SIZE);

    /* Push to RQUEUE */
    if (s->rqueue_count < 256) {
        s->rqueue_fifo[s->rqueue_tail] = scb_index;
        s->rqueue_tail = (s->rqueue_tail + 1) % 256;
        s->rqueue_count++;
        pcibase_update_irq(s);
    }
}

/* PIO Handlers */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case ORC_RQUEUE:
        if (s->rqueue_count > 0) {
            val = s->rqueue_fifo[s->rqueue_head];
            s->rqueue_head = (s->rqueue_head + 1) % 256;
            s->rqueue_count--;
            pcibase_update_irq(s);
        }
        return val;
    case ORC_RQUEUECNT:
        return s->rqueue_count;
    case ORC_HSTUS:
        return s->regs[ORC_HSTUS];
    case ORC_HCTRL:
        return s->regs[ORC_HCTRL];
    case ORC_HDATA:
        return s->regs[ORC_HDATA];
    case ORC_EBIOSDATA:
        return s->bios_rom[s->ebios_addr & 0xFFFF];
    case ORC_RISCRAM:
        if (size == 4) {
            uint32_t *ptr = (uint32_t *) &s->riscram[s->riscram_addr];
            val = *ptr;
            s->riscram_addr = (s->riscram_addr + 4) % sizeof(s->riscram);
            return val;
        }
        return s->regs[addr];
    case ORC_SCBBASE0:
        if (size == 4) return s->scb_base0;
        return s->regs[addr];
    case ORC_SCBBASE1:
        if (size == 4) return s->scb_base1;
        return s->regs[addr];
    default:
        return s->regs[addr];
    }
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case ORC_HCTRL: {
        uint8_t wval = val & 0xFF;
        /* Update register preserving HOSTSTOP except when explicitly cleared */
        s->regs[ORC_HCTRL] = (s->regs[ORC_HCTRL] & 0x02) | (wval & 0xC1);
        if (wval & DEVRST) {
            /* Device reset */
            s->regs[ORC_HCTRL] = HOSTSTOP;
            s->regs[ORC_HSTUS] = RREADY;  /* set ready after reset */
            s->cmd_phase = CMD_PHASE_IDLE;
            s->rqueue_count = 0;
            pcibase_update_irq(s);
            break;
        }
        if (wval & SCSIRST) {
            /* SCSI reset: clear the bit immediately */
            s->regs[ORC_HCTRL] &= ~SCSIRST;
        }
        if (wval & HDO) {
            s->regs[ORC_HCTRL] &= ~HDO; /* hardware clears HDO */
            if (s->cmd_phase == CMD_PHASE_IDLE) {
                s->cmd_cmd = s->regs[ORC_HDATA];
                s->cmd_resp_left = 0;
                switch (s->cmd_cmd) {
                case ORC_CMD_VERSION:
                    s->cmd_phase = CMD_PHASE_RESP;
                    s->cmd_resp_left = 2;
                    s->cmd_resp_hi = false;
                    s->regs[ORC_HDATA] = 0x01; /* low byte */
                    s->regs[ORC_HSTUS] |= HDI;
                    break;
                case ORC_CMD_SET_NVM:
                    s->cmd_phase = CMD_PHASE_NVM_ADDR;
                    break;
                case ORC_CMD_GET_NVM:
                    s->cmd_phase = CMD_PHASE_NVM_ADDR;
                    break;
                case ORC_CMD_ABORT_SCB:
                    s->cmd_phase = CMD_PHASE_ABORT_IDX;
                    break;
                default:
                    s->cmd_phase = CMD_PHASE_IDLE;
                    break;
                }
            } else {
                switch (s->cmd_phase) {
                case CMD_PHASE_NVM_ADDR:
                    s->cmd_nvm_addr = s->regs[ORC_HDATA];
                    if (s->cmd_cmd == ORC_CMD_SET_NVM) {
                        s->cmd_phase = CMD_PHASE_NVM_VALUE;
                    } else if (s->cmd_cmd == ORC_CMD_GET_NVM) {
                        s->regs[ORC_HDATA] = s->nvram[s->cmd_nvm_addr & 0x3F];
                        s->regs[ORC_HSTUS] |= HDI;
                        s->cmd_phase = CMD_PHASE_RESP;
                        s->cmd_resp_left = 1;
                    }
                    break;
                case CMD_PHASE_NVM_VALUE:
                    s->nvram[s->cmd_nvm_addr & 0x3F] = s->regs[ORC_HDATA];
                    s->cmd_phase = CMD_PHASE_IDLE;
                    break;
                case CMD_PHASE_ABORT_IDX:
                    s->regs[ORC_HDATA] = 0x00; /* success */
                    s->regs[ORC_HSTUS] |= HDI;
                    s->cmd_phase = CMD_PHASE_RESP;
                    s->cmd_resp_left = 1;
                    break;
                }
            }
        } else if (wval == 0x00) {
            s->regs[ORC_HCTRL] &= ~HOSTSTOP;
            s->regs[ORC_HSTUS] |= RREADY;
        }
        break;
    }
    case ORC_HSTUS:
        /* Write-1-to-clear HDI */
        if (val & HDI) {
            s->regs[ORC_HSTUS] &= ~HDI;
            if (s->cmd_phase == CMD_PHASE_RESP && s->cmd_resp_left > 0) {
                s->cmd_resp_left--;
                if (s->cmd_resp_left > 0) {
                    if (s->cmd_cmd == ORC_CMD_VERSION) {
                        s->regs[ORC_HDATA] = 0x00; /* high byte */
                        s->regs[ORC_HSTUS] |= HDI;
                    }
                } else {
                    s->cmd_phase = CMD_PHASE_IDLE;
                }
            }
        }
        /* Other bits are read-only, ignore */
        break;
    case ORC_HDATA:
        s->regs[ORC_HDATA] = val & 0xFF;
        break;
    case ORC_SCBBASE0:
        if (size == 4) {
            s->scb_base0 = val & 0xFFFFFFFF;
        } else {
            s->regs[addr] = val;
        }
        break;
    case ORC_SCBBASE1:
        if (size == 4) {
            s->scb_base1 = val & 0xFFFFFFFF;
        } else {
            s->regs[addr] = val;
        }
        break;
    case ORC_FWBASEADR:
        if (size == 4) {
            /* Store but unused */
        }
        s->regs[addr] = val;
        break;
    case ORC_EBIOSADR0:
        if (size == 2) {
            s->ebios_addr = (s->ebios_addr & 0xFF000000) | (val & 0xFFFF);
        } else {
            s->regs[addr] = val;
        }
        break;
    case ORC_EBIOSADR2:
        if (size == 1) {
            s->ebios_addr = (s->ebios_addr & 0x00FFFF) | ((val & 0xFF) << 16);
        } else {
            s->regs[addr] = val;
        }
        break;
    case ORC_RISCCTL:
        s->regs[addr] = val & 0xFF;
        /* Reset RISC RAM pointer on PRGMRST */
        if (val & PRGMRST) {
            s->riscram_addr = 0;
        }
        break;
    case ORC_RISCRAM:
        if (size == 4) {
            uint32_t *ptr = (uint32_t *) &s->riscram[s->riscram_addr];
            *ptr = val;
            s->riscram_addr = (s->riscram_addr + 4) % sizeof(s->riscram);
        } else {
            s->regs[addr] = val;
        }
        break;
    case ORC_GCFG:
        s->regs[ORC_GCFG] = val & 0xFF;
        break;
    case ORC_GIMSK:
        s->regs[ORC_GIMSK] = val & 0xFF;
        pcibase_update_irq(s);
        break;
    case ORC_PQUEUE:
        process_scb_post(s, val & 0xFF);
        break;
    case ORC_SCBSIZE:
        s->regs[addr] = val & 0xFF;
        break;
    default:
        s->regs[addr] = val & 0xFF;
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
    memcpy(s->nvram, default_nvram, sizeof(default_nvram));

    s->regs[ORC_GCFG] = 0x01;
    s->regs[ORC_GIMSK] = 0xFF;  /* all interrupts disabled */
    s->regs[ORC_HCTRL] = HOSTSTOP;  /* chip stopped */
    s->regs[ORC_HSTUS] = RREADY;     /* set ready */

    s->cmd_phase = CMD_PHASE_IDLE;
    s->rqueue_count = 0;
    s->rqueue_head = 0;
    s->rqueue_tail = 0;
    s->ebios_addr = 0;
    s->riscram_addr = 0;
    s->scb_base0 = 0;
    s->scb_base1 = 0;

    memset(s->bios_rom, 0, sizeof(s->bios_rom));
    s->bios_rom[0] = 0x55;
    s->bios_rom[1] = 0xAA;

    memset(s->riscram, 0, sizeof(s->riscram));

    pcibase_update_irq(s);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, int bar_idx, Error **errp)
{
    /* Single BAR 0, PIO, size 0x100 */
    hwaddr aligned_size = pow2ceil(0x100);
    MemoryRegion *mr = &s->bar_regions[bar_idx];
    memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, "inia100-pio", aligned_size);
    pci_register_bar(pdev, bar_idx, PCI_BASE_ADDRESS_SPACE_IO, mr);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INIT);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_INIA100);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_SCSI);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    pcibase_register_bar(pdev, s, 0, errp);

    memcpy(s->nvram, default_nvram, sizeof(default_nvram));
    memset(s->regs, 0, sizeof(s->regs));
    s->regs[ORC_GCFG] = 0x01;
    s->regs[ORC_GIMSK] = 0xFF;
    s->regs[ORC_HCTRL] = HOSTSTOP;
    s->regs[ORC_HSTUS] = RREADY;

    s->cmd_phase = CMD_PHASE_IDLE;
    s->rqueue_count = 0;
    s->rqueue_head = 0;
    s->rqueue_tail = 0;
    s->ebios_addr = 0;
    s->riscram_addr = 0;
    s->scb_base0 = 0;
    s->scb_base1 = 0;

    memset(s->bios_rom, 0, sizeof(s->bios_rom));
    s->bios_rom[0] = 0x55;
    s->bios_rom[1] = 0xAA;

    memset(s->riscram, 0, sizeof(s->riscram));
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
    .name = "inia100_pci",
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
