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
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-core.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "dmfe_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1282
#define DEVICE_ID 0x9132
#define CLASS_ID  PCI_CLASS_NETWORK_ETHERNET

/* Register offsets */
enum dmfe_offsets {
    DCR0 = 0x00, DCR1 = 0x08, DCR2 = 0x10, DCR3 = 0x18, DCR4 = 0x20,
    DCR5 = 0x28, DCR6 = 0x30, DCR7 = 0x38, DCR8 = 0x40, DCR9 = 0x48,
    DCR10 = 0x50, DCR11 = 0x58, DCR12 = 0x60, DCR13 = 0x68, DCR14 = 0x70,
    DCR15 = 0x78
};

/* CR6 bit definitions */
enum dmfe_CR6_bits {
    CR6_RXSC = 0x2, CR6_PBF = 0x8, CR6_PM = 0x40, CR6_PAM = 0x80,
    CR6_FDM = 0x200, CR6_TXSC = 0x2000, CR6_STI = 0x100000,
    CR6_SFT = 0x200000, CR6_RXA = 0x40000000, CR6_NO_PURGE = 0x20000000
};

/* Additional driver constants */
#define DM910X_RESET    1
#define TX_BUF_ALLOC    0x600

/* SROM/PHY bit definitions (from kernel header) */
#define CR9_SROM_READ   0x4800
#define CR9_SRCS        0x1
#define CR9_SRCLK       0x2
#define SROM_DATA_1     0x4
#define CR9_CRDOUT      0x8

#define PHY_DATA_1      0x20000
#define PHY_DATA_0      0x00000
#define MDCLKH          0x10000

#define CR0_DEFAULT 0x00E00000
#define CR6_DEFAULT 0x00080000
#define CR7_DEFAULT 0x180c1
#define CR15_DEFAULT 0x06

#define TX_DESC_CNT 0x10
#define RX_DESC_CNT 0x20

struct dmfe_tx_desc {
    uint32_t tdes0, tdes1, tdes2, tdes3;
} __attribute__((packed));
struct dmfe_rx_desc {
    uint32_t rdes0, rdes1, rdes2, rdes3;
} __attribute__((packed));

typedef struct {
    int     index;
    int     type;
    hwaddr  size;
    const char *name;
} BARInfo;

#define BAR_TYPE_NONE 0
#define BAR_TYPE_MMIO 1
#define BAR_TYPE_PIO  2
#define BAR_TYPE_RAM  3

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    uint8_t io_bytes[0x100];
    uint32_t dcr[16];

    /* SROM bit-bang state */
    bool srom_cs;
    bool srom_clk;
    int srom_bitcnt;
    uint16_t srom_shift_in;
    uint16_t srom_data_out;
    uint8_t srom_out_bit;
    uint8_t srom_addr;

    uint8_t srom[128];
};

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x100) {
        return UINT64_MAX;
    }

    /* Handle DCR9 special case for SROM bit-banging */
    if (addr >= 0x48 && addr < 0x50) {
        uint32_t dcr9_val = s->dcr[9];
        if (s->srom_cs) {
            dcr9_val = (dcr9_val & ~CR9_CRDOUT) | (s->srom_out_bit ? CR9_CRDOUT : 0);
        }
        /* Extract bytes for the requested access */
        for (unsigned i = 0; i < size; i++) {
            val |= (uint64_t)((dcr9_val >> ((addr - 0x48 + i) * 8)) & 0xFF) << (i * 8);
        }
    } else {
        memcpy(&val, &s->io_bytes[addr], size);
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x100) {
        return;
    }

    /* Write bytes to shadow */
    for (unsigned i = 0; i < size; i++) {
        s->io_bytes[addr + i] = (val >> (i * 8)) & 0xFF;
    }

    /* Special handling for DCR registers */
    if (addr < 0x80) {
        int reg = addr / 8;
        uint32_t *dcr = &s->dcr[reg];
        /* Only handle full 32-bit accesses; simple model */
        if (size == 4 && (addr % 8 == 0)) {
            *dcr = val;
            /* For DCR9, update SROM state machine */
            if (reg == 9) {
                bool cs = (val & CR9_SRCS) != 0;
                bool clk = (val & CR9_SRCLK) != 0;

                if (cs && !s->srom_cs) {
                    /* CS rising edge: start new SROM transaction */
                    s->srom_cs = true;
                    s->srom_bitcnt = 0;
                    s->srom_shift_in = 0;
                    s->srom_out_bit = 0;
                } else if (!cs) {
                    s->srom_cs = false;
                    s->srom_bitcnt = 0;
                }

                if (s->srom_cs && clk && !s->srom_clk) {
                    /* Rising clock edge: sample input and shift output */
                    /* Capture data bit */
                    s->srom_shift_in = (s->srom_shift_in << 1) | ((val & SROM_DATA_1) ? 1 : 0);
                    s->srom_bitcnt++;

                    if (s->srom_bitcnt == 9) {
                        /* Command+address received; extract address from bits [5:0] */
                        s->srom_addr = s->srom_shift_in & 0x3F;
                        /* Load output word from srom[], assume little-endian 16-bit words */
                        uint16_t word = s->srom[s->srom_addr * 2] |
                                        ((uint16_t)s->srom[s->srom_addr * 2 + 1] << 8);
                        s->srom_data_out = word;
                        s->srom_out_bit = (s->srom_data_out >> 15) & 1;
                        s->srom_data_out <<= 1;
                    } else if (s->srom_bitcnt > 9) {
                        s->srom_out_bit = (s->srom_data_out >> 15) & 1;
                        s->srom_data_out <<= 1;
                    }
                }
                s->srom_clk = clk;
            }
        }
    } else if (addr >= 0x80) {
        /* PHY registers for DM9132, not needed for probe */
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

    memset(s->io_bytes, 0, sizeof(s->io_bytes));
    s->dcr[0] = CR0_DEFAULT;
    s->dcr[6] = CR6_DEFAULT;
    s->dcr[7] = CR7_DEFAULT;
    s->dcr[15] = CR15_DEFAULT;
    memcpy(&s->io_bytes[0x00], &s->dcr[0], 4);
    memcpy(&s->io_bytes[0x30], &s->dcr[6], 4);
    memcpy(&s->io_bytes[0x38], &s->dcr[7], 4);
    memcpy(&s->io_bytes[0x78], &s->dcr[15], 4);

    s->srom_cs = false;
    s->srom_clk = false;
    s->srom_bitcnt = 0;
    s->srom_shift_in = 0;
    s->srom_out_bit = 0;
    s->srom_addr = 0;

    /* Default MAC 52:54:00:12:34:56 */
    s->srom[20] = 0x52;
    s->srom[21] = 0x54;
    s->srom[22] = 0x00;
    s->srom[23] = 0x12;
    s->srom[24] = 0x34;
    s->srom[25] = 0x56;
    /* SROM version and capabilities to satisfy dmfe_parse_srom */
    s->srom[18] = 0x52; /* SROM_V41_CODE */
    s->srom[34] = 0x0f; /* NIC_capability (lower byte) */
    s->srom[35] = 0x00;
    s->srom[36] = 0x00; /* reserved/mode */
    s->srom[37] = 0x00;
    s->srom[38] = 0x00;
    s->srom[39] = 0x00;
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x100;
    s->bar_info[0].name = "dmfe-io";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize defaults via reset */
    pcibase_reset(DEVICE(s));
}

static void pcibase_uninit(PCIDevice *pdev)
{
    /* Nothing to free */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "dmfe_pci",
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
