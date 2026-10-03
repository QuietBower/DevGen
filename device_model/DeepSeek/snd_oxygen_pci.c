/*
 * QEMU model for C-Media CMI8788 (Oxygen HD) audio controller.
 * Emulates minimal register interface for driver probing.
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
#include "qemu/bswap.h"

#define TYPE_PCIBASE_DEVICE "snd_oxygen_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define PCI_VENDOR_ID_CMEDIA 0x13f6
#define OXYGEN_IO_SIZE 0x100

/* Register offsets and masks from driver */
#define OXYGEN_SPI_CONTROL       0x98
#define OXYGEN_SPI_DATA1         0x99
#define OXYGEN_SPI_DATA2         0x9a
#define OXYGEN_SPI_DATA3         0x9b
#define OXYGEN_SPI_CODEC_SHIFT   4
#define OXYGEN_SPI_TRIGGER       0x01
#define OXYGEN_SPI_BUSY          0x01
#define OXYGEN_SPI_DATA_LENGTH_2 0x00
#define OXYGEN_SPI_DATA_LENGTH_3 0x02
#define OXYGEN_SPI_CEN_LATCH_CLOCK_HI 0x80
#define OXYGEN_SPI_CEN_LATCH_CLOCK_LO 0x00

#define OXYGEN_GPIO_CONTROL   0xa8
#define OXYGEN_GPIO_DATA      0xa6

/* Dummy codec register definitions */
#define AK4396_CONTROL_1      0
#define AK4396_CONTROL_2      1
#define AK4396_CONTROL_3      2
#define AK4396_LCH_ATT        3
#define AK4396_RCH_ATT        4
#define AK4396_DFS_MASK       0x18
#define AK4396_DFS_NORMAL     0x00
#define AK4396_DFS_DOUBLE     0x08
#define AK4396_DFS_QUAD       0x10
#define AK4396_DIF_24_MSB     0x04
#define AK4396_RSTN           0x01
#define AK4396_SMUTE          0x01
#define AK4396_DEM_OFF        0x02
#define AK4396_SLOW           0x20

#define WM8785_R0             0
#define WM8785_R2             2
#define WM8785_R7             7

struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion bar_regions[6];
    int num_bars;
    bool has_msi;
    uint8_t regs[OXYGEN_IO_SIZE];
    /* SPI emulation */
    uint8_t ak4396_regs[5];   /* AK4396 register file */
    uint8_t wm8785_regs[8];   /* WM8785 register file */
    uint8_t spi_rx_data[3];   /* Data bytes for read-back */
    uint8_t spi_tx_data[3];   /* Data bytes for write */
};

static void oxygen_spi_transaction(PCIBaseState *s)
{
    uint8_t ctrl = s->regs[OXYGEN_SPI_CONTROL];
    uint8_t codec_sel = (ctrl >> OXYGEN_SPI_CODEC_SHIFT) & 0x3;
    uint8_t *regfile = NULL;
    int maxreg = 0;

    /* Determine target codec */
    switch (codec_sel) {
    case 0: /* AK4396 DAC */
        regfile = s->ak4396_regs;
        maxreg = 4;
        break;
    case 1: /* WM8785 ADC */
        regfile = s->wm8785_regs;
        maxreg = 7;
        break;
    default:
        return;
    }

    /* Determine if read or write. Bit 1 of control? We'll use CEN_LATCH_CLOCK_HI for read,
     * CEN_LATCH_CLOCK_LO for write, as a guess */
    bool is_read = (ctrl & OXYGEN_SPI_CEN_LATCH_CLOCK_HI) != 0;
    uint8_t reg = s->spi_tx_data[0] & 0x1f; /* register address in first byte */

    if (!is_read) {
        /* Write data from spi_tx_data to regfile */
        uint8_t val = s->spi_tx_data[1];
        if (reg >= 0 && reg <= maxreg) {
            regfile[reg] = val;
        }
        /* For 16-bit writes (if length 3), second data byte in spi_tx_data[2] */
        if ((s->spi_tx_data[1] & 0x80) == 0) { /* Not a continuation? */
            /* Could be 16-bit, but we ignore for now */
        }
    } else {
        /* Read: place register value into spi_rx_data */
        if (reg >= 0 && reg <= maxreg) {
            s->spi_rx_data[0] = regfile[reg];
            s->spi_rx_data[1] = 0;
            s->spi_rx_data[2] = 0;
        } else {
            s->spi_rx_data[0] = 0xff; /* invalid reg */
            s->spi_rx_data[1] = 0;
            s->spi_rx_data[2] = 0;
        }
    }

    /* Clear busy bit */
    s->regs[OXYGEN_SPI_CONTROL] &= ~OXYGEN_SPI_BUSY;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size > OXYGEN_IO_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read beyond IO range: addr=0x%" HWADDR_PRIx " size=%u\n",
                      __func__, addr, size);
        return 0;
    }

    /* Special handling for SPI data registers */
    if (addr >= OXYGEN_SPI_DATA1 && addr <= OXYGEN_SPI_DATA3) {
        /* Return rx data byte */
        int idx = addr - OXYGEN_SPI_DATA1;
        if (idx >= 0 && idx < 3) {
            val = s->spi_rx_data[idx];
        } else {
            val = 0;
        }
        return val;
    }

    /* Default PIO read */
    switch (size) {
    case 1:
        val = s->regs[addr];
        break;
    case 2:
        if (addr & 1) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned 16-bit read at 0x%" HWADDR_PRIx "\n",
                          __func__, addr);
            return 0;
        }
        val = lduw_le_p(&s->regs[addr]);
        break;
    case 4:
        if (addr & 3) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned 32-bit read at 0x%" HWADDR_PRIx "\n",
                          __func__, addr);
            return 0;
        }
        val = ldl_le_p(&s->regs[addr]);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported read size %u at 0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        return 0;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > OXYGEN_IO_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write beyond IO range: addr=0x%" HWADDR_PRIx " size=%u\n",
                      __func__, addr, size);
        return;
    }

    /* SPI data register writes */
    if (addr >= OXYGEN_SPI_DATA1 && addr <= OXYGEN_SPI_DATA3) {
        int idx = addr - OXYGEN_SPI_DATA1;
        if (idx >= 0 && idx < 3) {
            s->spi_tx_data[idx] = (uint8_t)val;
        }
        return;
    }

    /* Default write */
    switch (size) {
    case 1:
        s->regs[addr] = (uint8_t)val;
        break;
    case 2:
        if (addr & 1) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned 16-bit write at 0x%" HWADDR_PRIx "\n",
                          __func__, addr);
            return;
        }
        stw_le_p(&s->regs[addr], val);
        break;
    case 4:
        if (addr & 3) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned 32-bit write at 0x%" HWADDR_PRIx "\n",
                          __func__, addr);
            return;
        }
        stl_le_p(&s->regs[addr], val);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported write size %u at 0x%" HWADDR_PRIx "\n",
                      __func__, size, addr);
        return;
    }

    /* Trigger SPI transaction if needed */
    if (addr == OXYGEN_SPI_CONTROL && (val & OXYGEN_SPI_TRIGGER)) {
        oxygen_spi_transaction(s);
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

    /* Chip ID as read by driver: 0x8788 little-endian */
    s->regs[0x00] = 0x88;
    s->regs[0x01] = 0x87;

    /* Initialize codec register defaults */
    /* AK4396: reset state */
    s->ak4396_regs[AK4396_CONTROL_1] = AK4396_RSTN | AK4396_DIF_24_MSB; /* DIF=24-bit MSB, reset inactive */
    s->ak4396_regs[AK4396_CONTROL_2] = AK4396_SMUTE | AK4396_DEM_OFF; /* mute, DEM off */
    s->ak4396_regs[AK4396_CONTROL_3] = AK4396_DFS_NORMAL | AK4396_SLOW; /* slow roll-off, normal speed */
    s->ak4396_regs[AK4396_LCH_ATT] = 0x00; /* 0 dB attenuation */
    s->ak4396_regs[AK4396_RCH_ATT] = 0x00;

    /* WM8785: reset state */
    s->wm8785_regs[WM8785_R0] = 0x00;
    s->wm8785_regs[WM8785_R2] = 0x00;
    s->wm8785_regs[WM8785_R7] = 0x00;

    /* Clear SPI buffers */
    memset(s->spi_rx_data, 0, sizeof(s->spi_rx_data));
    memset(s->spi_tx_data, 0, sizeof(s->spi_tx_data));
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_CMEDIA);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x8788);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_MULTIMEDIA_AUDIO);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_VENDOR_ID, PCI_VENDOR_ID_CMEDIA);
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, 0x8788);  /* Generic CMI8788 */

    /* Add power management capability */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* Initialize MSI with one vector */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
    s->has_msi = true;

    /* Mark as PCI Express and add endpoint capability */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0);

    /* BAR0: PIO region */
    s->num_bars = 1;
    hwaddr bar_size = OXYGEN_IO_SIZE;
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_pio_ops, s, "oxygen-pio", bar_size);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_regions[0]);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

static const VMStateDescription vmstate_pcibase = {
    .name = "snd_oxygen_pci",
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
    set_bit(DEVICE_CATEGORY_SOUND, dc->categories);
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
