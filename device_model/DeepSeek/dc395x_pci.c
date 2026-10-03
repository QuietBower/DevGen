/* Complete QEMU C source file for dc395x_pci */

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
#include <stdint.h>

#define TYPE_PCIBASE_DEVICE "dc395x_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Vendor and Device IDs */
#define PCI_VENDOR_ID_TEKRAM          0x1DE1
#define PCI_DEVICE_ID_TEKRAM_TRMS1040 0x0391

/* SCSI controller class code */
#define DC395X_PCI_CLASS 0x0100

/* Base address register 0 size (IO space) */
#ifndef BAR0_SIZE
#define BAR0_SIZE 256
#endif

/* Maximum SCSI IDs (used in NVRAM struct) */
#ifndef DC395x_MAX_SCSI_ID
#define DC395x_MAX_SCSI_ID 16
#endif

/* Maximum scatter/gather list entries */
#ifndef DC395x_MAX_SG_LISTENTRY
#define DC395x_MAX_SG_LISTENTRY 64
#endif

/* TRM-S1040 Register Offsets (IO Space) */
#define TRM_S1040_SCSI_STATUS       0x80
#define TRM_S1040_SCSI_CONTROL      0x80
#define TRM_S1040_SCSI_FIFOSTAT     0x81
#define TRM_S1040_SCSI_SIGNAL       0x83
#define TRM_S1040_SCSI_INTSTATUS    0x84
#define TRM_S1040_SCSI_OFFSET       0x84
#define TRM_S1040_SCSI_INTEN        0x8C
#define TRM_S1040_SCSI_SYNC         0x85
#define TRM_S1040_SCSI_TARGETID      0x86
#define TRM_S1040_SCSI_HOSTID       0x87
#define TRM_S1040_SCSI_COUNTER      0x88
#define TRM_S1040_SCSI_COMMAND      0x90
#define TRM_S1040_SCSI_TIMEOUT      0x91
#define TRM_S1040_SCSI_FIFO         0x98
#define TRM_S1040_SCSI_FIFOCNT      0x82
#define TRM_S1040_SCSI_CONFIG0      0x8D
#define TRM_S1040_SCSI_CONFIG1      0x8E
#define TRM_S1040_SCSI_CONFIG2      0x8F

/* DMA Registers */
#define TRM_S1040_DMA_CONTROL       0xA1
#define TRM_S1040_DMA_FIFOSTAT      0xA2
#define TRM_S1040_DMA_STATUS        0xA3
#define TRM_S1040_DMA_INTEN         0xA4
#define TRM_S1040_DMA_CONFIG        0xA6
#define TRM_S1040_DMA_COMMAND       0xA0
#define TRM_S1040_DMA_XLOWADDR      0xB0
#define TRM_S1040_DMA_XHIGHADDR     0xB4
#define TRM_S1040_DMA_XCNT          0xA8

/* General Registers */
#define TRM_S1040_GEN_CONTROL       0xD4
#define TRM_S1040_GEN_STATUS        0xD5
#define TRM_S1040_GEN_NVRAM         0xD6
#define TRM_S1040_GEN_TIMER         0xDB

/* SCSI Control Bits */
#define DO_SETATN                    0x0200
#define DO_RSTMODULE                 0x0010
#define DO_CLRFIFO                   0x0004
#define DO_HWRESELECT                0x0001
#define DO_DATALATCH                 0x0002
#define DO_RSTSCSI                   0x0008

/* SCSI Interrupt Status/Enable Bits */
#define INT_CMDDONE                  0x01
#define INT_BUSSERVICE               0x02
#define INT_SCSIRESET                0x04
#define INT_RESELECTED               0x08
#define INT_DISCONNECT               0x10
#define INT_SELTIMEOUT               0x20
#define INT_SELECT                   0x40
#define SCSIINTERRUPT                0x0080

/* DMA Control Bits */
#define DMACMD_DIR                   0x01
#define DMACMD_SG                    0x02
#define ABORTXFER                    0x04
#define STOPDMAXFER                  0x08
#define DMARESETMODULE               0x10
#define DMAXFERCOMP                  0x02

/* DMA Status Bits */
#define XFERPENDING                  0x80
#define SCSIXFERDONE                 0x0800
#define SCSIXFERCNT_2_ZERO           0x0100

/* Other Configuration Bits */
#define EN_SCSIINTR                  0x01
#define EN_DMAXFERERROR              0x08
#define BLOCKRST                     0x01
#define CFG2_WIDEFIFO                0x02
#define HCC_WIDE_CARD                0x20
#define HCC_PARITY                   0x08
#define HCC_SCSI_RESET               0x10
#define HCC_AUTOTERM                 0x04
#define WIDESCSI                     0x02

#ifndef BIT0
#define BIT0 (1 << 0)
#endif
#ifndef BIT2
#define BIT2 (1 << 2)
#endif
#ifndef BIT5
#define BIT5 (1 << 5)
#endif

#define SYNC_NEGO_ENABLE             BIT0
#define WIDE_NEGO_ENABLE             BIT2
#define EN_TAG_QUEUEING              BIT5
#define NAC_POWERON_SCSI_RESET       0x04
#define NAC_GREATER_1G               0x02
#define NAC_GT2DRIVES                0x01
#define NAC_SCANLUN                  0x20

/* Phase definitions */
#define PH_DATA_OUT                  0x00
#define PH_DATA_IN                   0x01
#define PH_BUS_FREE                  0x05

/* Inferred NVRAM and Timer constants */
#define GTIMEOUT                     0x01
#define NVR_BITIN                    0x80
#define NVR_BITOUT                   0x01
#define NVR_CLOCK                    0x02
#define NVR_SELECT                   0x04
#define EN_EEPROM                    0x80

#define NVR_CMD_READ                 0x06
#define NVR_CMD_WRITE                0x05
#define NVR_CMD_WREN                 0x04
#define NVR_CMD_WRDI                 0x04 /* with addr=0x00 */

/* Scatter/Gather Entry for TRM-S1040 DMA */
struct SGentry {
    uint32_t address;
    uint32_t length;
};

/* EEPROM layout (NVRAM) */
struct NvRamType {
    uint8_t sub_vendor_id[2];
    uint8_t sub_sys_id[2];
    uint8_t sub_class;
    uint8_t vendor_id[2];
    uint8_t device_id[2];
    uint8_t reserved;
    struct NVRamTarget {
        uint8_t cfg0;
        uint8_t period;
        uint8_t cfg2;
        uint8_t cfg3;
    } target[DC395x_MAX_SCSI_ID];
    uint8_t scsi_id;
    uint8_t channel_cfg;
    uint8_t delay_time;
    uint8_t max_tag;
    uint8_t reserved0;
    uint8_t boot_target;
    uint8_t boot_lun;
    uint8_t reserved1;
    uint16_t reserved2[22];
    uint16_t cksum;
};

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

typedef enum {
    NVR_STATE_IDLE = 0,
    NVR_STATE_COMMAND,
    NVR_STATE_ADDRESS,
    NVR_STATE_READING,
    NVR_STATE_WRITING
} NVRState;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint16_t intr_status;
    uint16_t intr_mask;

    uint8_t ioreg[256];

    struct SGentry sg_list[DC395x_MAX_SG_LISTENTRY];
    uint32_t dma_xlowaddr;
    uint32_t dma_xhighaddr;
    uint32_t dma_xcnt;
    uint8_t dma_command;
    uint8_t dma_status;
    uint8_t dma_control;
    uint8_t dma_fifostat;
    uint8_t dma_inten;
    uint8_t dma_config;
    uint8_t scsi_inten;

    uint16_t scsi_control;
    uint16_t scsi_status;

    /* NVRAM state */
    uint8_t nvram_cs;
    uint8_t nvram_clk;
    uint8_t nvram_bitin;
    NVRState nvram_state;
    uint8_t nvram_bitcnt;
    uint8_t nvram_cmd;
    uint8_t nvram_addr;
    uint8_t nvram_data_out_byte;
    uint8_t nvram_data_bitcnt;
    bool    nvram_write_enable;
};

/* Pre‑computed valid EEPROM image with checksum 0x1234 */
static const uint8_t eeprom_default_data[128] = {
    0xE1, 0x1D, 0x91, 0x03, 0x00, 0xE1, 0x1D, 0x91,
    0x03, 0x00, /* offsets 0-9 */
    /* 16 targets: each 0x00000421 -> bytes 0x21,0x04,0x00,0x00 */
    0x21, 0x04, 0x00, 0x00, 0x21, 0x04, 0x00, 0x00,
    0x21, 0x04, 0x00, 0x00, 0x21, 0x04, 0x00, 0x00,
    0x21, 0x04, 0x00, 0x00, 0x21, 0x04, 0x00, 0x00,
    0x21, 0x04, 0x00, 0x00, 0x21, 0x04, 0x00, 0x00,
    0x21, 0x04, 0x00, 0x00, 0x21, 0x04, 0x00, 0x00,
    0x21, 0x04, 0x00, 0x00, 0x21, 0x04, 0x00, 0x00,
    0x21, 0x04, 0x00, 0x00, 0x21, 0x04, 0x00, 0x00,
    0x21, 0x04, 0x00, 0x00, 0x21, 0x04, 0x00, 0x00,
    /* offsets 74-81 */
    0x07, 0x0F, 0x06, 0x02, 0x15, 0x00, 0x00, 0x00,
    /* reserved2[22] = 44 bytes zeros */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    /* cksum */
    0x70, 0x2B
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = 0;

    /* SCSI interrupts */
    pending |= (s->intr_status & s->scsi_inten);
    /* DMA interrupts */
    pending |= (s->dma_status & s->dma_inten);

    if (pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static void pcibase_nvram_update(PCIBaseState *s, uint8_t val)
{
    uint8_t cs = val & NVR_SELECT;
    uint8_t clk = val & NVR_CLOCK;
    uint8_t bit = val & NVR_BITOUT;

    if (!(s->ioreg[TRM_S1040_GEN_CONTROL] & EN_EEPROM)) {
        s->nvram_cs = cs;
        s->nvram_clk = clk;
        return;
    }

    if (cs && !s->nvram_cs) {
        /* CS raising edge: start new command */
        s->nvram_state = NVR_STATE_COMMAND;
        s->nvram_bitcnt = 0;
        s->nvram_cmd = 0;
        s->nvram_addr = 0;
        s->nvram_bitin = 0;
    } else if (cs && s->nvram_cs) {
        if (clk && !s->nvram_clk) {
            /* rising clock edge */
            switch (s->nvram_state) {
            case NVR_STATE_COMMAND:
                s->nvram_cmd = (s->nvram_cmd << 1) | bit;
                if (++s->nvram_bitcnt == 3) {
                    s->nvram_bitcnt = 0;
                    if (s->nvram_cmd == NVR_CMD_READ || s->nvram_cmd == NVR_CMD_WRITE) {
                        s->nvram_state = NVR_STATE_ADDRESS;
                    } else if (s->nvram_cmd == NVR_CMD_WREN) {
                        s->nvram_state = NVR_STATE_ADDRESS;
                    } else {
                        s->nvram_state = NVR_STATE_IDLE;
                    }
                }
                break;
            case NVR_STATE_ADDRESS:
                s->nvram_addr = (s->nvram_addr << 1) | bit;
                if (++s->nvram_bitcnt == 7) {
                    s->nvram_bitcnt = 0;
                    if (s->nvram_state == NVR_STATE_ADDRESS && s->nvram_cmd == NVR_CMD_READ) {
                        s->nvram_data_out_byte = eeprom_default_data[s->nvram_addr];
                        s->nvram_data_bitcnt = 0;
                        s->nvram_state = NVR_STATE_READING;
                        /* output first bit immediately? data will be read on falling edge later */
                    } else if (s->nvram_state == NVR_STATE_ADDRESS && s->nvram_cmd == NVR_CMD_WRITE) {
                        s->nvram_data_bitcnt = 0;
                        s->nvram_state = NVR_STATE_WRITING;
                    } else if (s->nvram_state == NVR_STATE_ADDRESS && s->nvram_cmd == NVR_CMD_WREN) {
                        if (s->nvram_addr == 0x7F) {
                            s->nvram_write_enable = true;
                        } else if (s->nvram_addr == 0x00) {
                            s->nvram_write_enable = false;
                        }
                        s->nvram_state = NVR_STATE_IDLE;
                    }
                }
                break;
            default:
                break;
            }
        } else if (!clk && s->nvram_clk) {
            /* falling clock edge */
            if (s->nvram_state == NVR_STATE_READING) {
                if (s->nvram_data_bitcnt < 8) {
                    /* MSB first */
                    s->nvram_bitin = (s->nvram_data_out_byte & 0x80) ? 1 : 0;
                    s->nvram_data_out_byte <<= 1;
                    s->nvram_data_bitcnt++;
                } else {
                    s->nvram_bitin = 0;
                }
            }
        }
    } else if (!cs && s->nvram_cs) {
        /* CS falling edge */
        s->nvram_state = NVR_STATE_IDLE;
    }

    s->nvram_cs = cs;
    s->nvram_clk = clk;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= BAR0_SIZE) {
        return ~0ULL;
    }

    switch (addr) {
    case TRM_S1040_GEN_NVRAM:
        /* Return current NVRAM bit in if EN_EEPROM is set */
        if (s->ioreg[TRM_S1040_GEN_CONTROL] & EN_EEPROM) {
            val = s->nvram_bitin ? NVR_BITIN : 0;
        } else {
            val = 0;
        }
        break;
    default:
        if (size == 1) {
            val = s->ioreg[addr];
        } else if (size == 2) {
            val = lduw_le_p(s->ioreg + addr);
        } else if (size == 4) {
            val = ldl_le_p(s->ioreg + addr);
        } else {
            val = ~0ULL;
        }
        break;
    }

    /* side effects after reading */
    if (addr == TRM_S1040_SCSI_INTSTATUS) {
        s->intr_status = 0;
        pcibase_update_irq(s);
    } else if (addr == TRM_S1040_GEN_STATUS) {
        /* reading GEN_STATUS might clear GTIMEOUT? not required */
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= BAR0_SIZE) {
        return;
    }

    /* update shadow register */
    if (size == 1) {
        s->ioreg[addr] = val & 0xFF;
    } else if (size == 2) {
        stw_le_p(s->ioreg + addr, val & 0xFFFF);
    } else if (size == 4) {
        stl_le_p(s->ioreg + addr, val & 0xFFFFFFFF);
    }

    /* side effects */
    switch (addr) {
    case TRM_S1040_SCSI_CONTROL:
        /* SCSI Control writes; handle DO_RSTMODULE, DO_CLRFIFO, etc. */
        if (val & DO_RSTMODULE) {
            /* reset SCSI module - no state to clear currently */
        }
        if (val & DO_CLRFIFO) {
            /* clear fifo */
        }
        if (val & DO_RSTSCSI) {
            /* SCSI reset: generate reset interrupt? Not during probe */
        }
        break;
    case TRM_S1040_GEN_NVRAM:
        /* NVRAM interface update */
        pcibase_nvram_update(s, val & 0xFF);
        break;
    case TRM_S1040_GEN_TIMER:
        /* write to timer: set GTIMEOUT immediately for 30us delay */
        s->ioreg[TRM_S1040_GEN_STATUS] |= GTIMEOUT;
        break;
    case TRM_S1040_SCSI_INTEN:
        s->scsi_inten = val & 0xFF;
        pcibase_update_irq(s);
        break;
    case TRM_S1040_DMA_INTEN:
        s->dma_inten = val & 0xFF;
        pcibase_update_irq(s);
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

    memset(s->ioreg, 0, sizeof(s->ioreg));
    s->intr_status = 0;
    s->scsi_inten = 0;
    s->dma_inten = 0;
    s->nvram_cs = 0;
    s->nvram_clk = 0;
    s->nvram_bitin = 0;
    s->nvram_state = NVR_STATE_IDLE;
    s->nvram_write_enable = false;
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
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x1DE1);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x0391);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0100);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_PIO, .size = BAR0_SIZE, .name = "dc395x-io" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X; legacy interrupts only */
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
    .name = "dc395x_pci",
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
