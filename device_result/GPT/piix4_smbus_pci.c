/*
 * QEMU PCI device model for piix4_smbus_pci
 * Emulates just enough of an Intel PIIX4/AMD SB800-style SMBus host
 * controller to satisfy the Linux i2c-piix4 driver probe and basic
 * transfers.
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
#include "hw/i2c/i2c.h"

#define TYPE_PCIBASE_DEVICE "piix4_smbus_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCIBASE_VENDOR_ID  PCI_VENDOR_ID_INTEL
#define PCIBASE_DEVICE_ID  PCI_DEVICE_ID_INTEL_82371AB_3
#define PCIBASE_CLASS_ID   PCI_CLASS_SERIAL_SMBUS

#define MAX_TIMEOUT                  500
#define SMBBA                        0x090
#define SMBREV                       0x0D6
#define SMBIOSIZE                    9
#define SMBHSTCFG                    0x0D2
#define SMBSLVC                      0x0D3
#define SMBSHDW1                     0x0D4
#define SMBSHDW2                     0x0D5
#define ENABLE_INT9                  0
#define PIIX4_QUICK                  0x00
#define PIIX4_BYTE                   0x04
#define PIIX4_BYTE_DATA              0x08
#define PIIX4_WORD_DATA              0x0C
#define PIIX4_MAX_ADAPTERS           4
#define HUDSON2_MAIN_PORTS           2
#define SB800_PIIX4_SMB_IDX          0xcd6
#define SB800_PIIX4_SMB_MAP_SIZE     2
#define KERNCZ_IMC_IDX               0x3e
#define KERNCZ_IMC_DATA              0x3f
#define SB800_PIIX4_PORT_IDX         0x2c
#define SB800_PIIX4_PORT_IDX_ALT     0x2e
#define SB800_PIIX4_PORT_IDX_SEL     0x2f
#define SB800_PIIX4_PORT_IDX_MASK    0x06
#define SB800_PIIX4_PORT_IDX_SHIFT   1
#define SB800_PIIX4_FCH_PM_SIZE      8
#define PIIX4_BLOCK_DATA             0x14

/* The Linux driver uses abstract I/O port macros (SMBHSTCNT, etc.)
 * which are derived from the decoded SMBus base address (SMBBA).
 * For our virtual device, we map these logical offsets into BAR0 PIO
 * space starting at offset 0.
 */
#define SMBHSTSTS_OFFSET   0x00
#define SMBHSTCNT_OFFSET   0x02
#define SMBHSTCMD_OFFSET   0x03
#define SMBHSTADD_OFFSET   0x04
#define SMBHSTDAT0_OFFSET  0x05
#define SMBHSTDAT1_OFFSET  0x06
#define SMBBLKDAT_OFFSET   0x07
#define SMBSLVCNT_OFFSET   0x08 /* used as SMBSLVCNT in SB800 path */

#define BAR_INDEX_LEGACY_IO          0


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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t smb_base_low;   /* low byte of SMBBA config word */
    uint8_t smb_base_high;  /* high byte of SMBBA config word */
    uint8_t smbhstcfg;      /* SMBHSTCFG config byte */
    uint8_t smbrev;         /* SMBREV config byte */

    /* SMBus I/O register shadows (BAR0 PIO space) */
    uint8_t hststs;         /* SMBHSTSTS */
    uint8_t hstcnt;         /* SMBHSTCNT */
    uint8_t hstcmd;         /* SMBHSTCMD */
    uint8_t hstadd;         /* SMBHSTADD */
    uint8_t hstdat0;        /* SMBHSTDAT0 */
    uint8_t hstdat1;        /* SMBHSTDAT1 */
    uint8_t blkdat;         /* SMBBLKDAT (single-byte buffer) */
    uint8_t slvcnt;         /* SMBSLVCNT (used as semaphore) */

    /* Simple model of SMBus device memory (for data returns) */
    uint8_t device_mem[256];
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The reference driver mostly relies on polling SMBHSTSTS and does
     * not depend on interrupts for basic operation. We thus do not
     * signal interrupts here. Logic can be extended later if needed. */
    (void)pdev;
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The i2c-piix4 driver does not use DMA; nothing to do. */
    (void)pdev;
    (void)is_write;
}

/* Helper to (re)initialize SMBus host-controller state */
static void pcibase_smbus_reset(PCIBaseState *s)
{
    s->hststs  = 0x00;
    s->hstcnt  = 0x00;
    s->hstcmd  = 0x00;
    s->hstadd  = 0x00;
    s->hstdat0 = 0x00;
    s->hstdat1 = 0x00;
    s->blkdat  = 0x00;
    s->slvcnt  = 0x00;

    /* Initialize device memory with a simple pattern so that reads
     * return deterministic values. Index 0 used as block length.
     */
    for (int i = 0; i < 256; i++) {
        s->device_mem[i] = (uint8_t)i;
    }
    s->device_mem[0] = 16; /* default block length for block transfers */
}

/* Simulate completion of a SMBus transaction driven by SMBHSTCNT bit 6. */
static void pcibase_complete_transaction(PCIBaseState *s)
{
    /* piix4_transaction() expects:
     * - Initially SMBHSTSTS == 0x00 (driver can clear non-zero).
     * - After setting bit 6 in SMBHSTCNT and waiting, the busy bit
     *   (0x01) must eventually clear, and error bits (0x10,0x08,0x04)
     *   should be 0 for success.
     * - At the end, driver writes back SMBHSTSTS and expects it to
     *   become 0x00 again.
     */

    /* Clear busy bit and all error bits; leave others unchanged. */
    s->hststs &= ~(0x1F);

    /* For read transactions (non-quick and non-write), place data
     * into host data registers from our synthetic device memory.
     * Driver sets address and command before starting the transaction.
     */
    uint8_t rw = s->hstadd & 0x01; /* 0 = write, 1 = read */

    if (rw) {
        /* Determine operation from (hstcnt & 0x1C), as in driver: */
        uint8_t op = s->hstcnt & 0x1C;
        uint8_t command = s->hstcmd;
        switch (op) {
        case PIIX4_BYTE:
            /* For BYTE read, device returns a single data byte in DAT0.
             * Use device_mem[command]. */
            s->hstdat0 = s->device_mem[command];
            break;
        case PIIX4_BYTE_DATA:
            /* BYTE DATA read: data byte in DAT0. */
            s->hstdat0 = s->device_mem[command];
            break;
        case PIIX4_WORD_DATA:
        {
            uint16_t val = (uint16_t)s->device_mem[command] |
                           ((uint16_t)s->device_mem[(uint8_t)(command + 1)] << 8);
            s->hstdat0 = val & 0xFF;
            s->hstdat1 = (val >> 8) & 0xFF;
            break;
        }
        case PIIX4_BLOCK_DATA:
        {
            /* BLOCK DATA read: length in DAT0, bytes from SMBBLKDAT.
             * Use device_mem[] starting at index 'command'. */
            uint8_t len = s->device_mem[command];
            if (len == 0 || len > 32) {
                /* Keep within reasonable bounds */
                len = 16;
            }
            s->hstdat0 = len;
            /* The driver calls inb_p(SMBHSTCNT) to reset SMBBLKDAT
             * pointer then reads SMBBLKDAT 'len' times. We implement
             * SMBBLKDAT as a single-byte register that returns
             * sequential bytes from device_mem on each read, tracked
             * implicitly via a static index stored in blkdat. We store
             * current index in blkdat, starting at 'command + 1'. */
            s->blkdat = (uint8_t)(command + 1);
            break;
        }
        default:
            /* QUICK or unsupported op: nothing to return. */
            break;
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* No MMIO region is defined/used by the current driver for the
     * Intel PIIX4 device; return 0 for all accesses. */
    (void)s;
    (void)addr;
    (void)size;
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0xFF;

    /* Only byte-sized I/O is used by the driver (inb_p/outb_p). */
    if (size != 1) {
        return 0xFF;
    }

    switch (addr) {
    case SMBHSTSTS_OFFSET:
        val = s->hststs;
        break;
    case SMBHSTCNT_OFFSET:
        /* Reading SMBHSTCNT is used by the driver in block mode to
         * reset SMBBLKDAT pointer (hardware side effect). For our
         * model we simply leave state unchanged and return current
         * control value. */
        val = s->hstcnt;
        break;
    case SMBHSTCMD_OFFSET:
        val = s->hstcmd;
        break;
    case SMBHSTADD_OFFSET:
        val = s->hstadd;
        break;
    case SMBHSTDAT0_OFFSET:
        val = s->hstdat0;
        break;
    case SMBHSTDAT1_OFFSET:
        val = s->hstdat1;
        break;
    case SMBBLKDAT_OFFSET:
    {
        /* SMBBLKDAT read returns the next byte in a block transaction.
         * We use blkdat as the current index into device_mem. */
        uint8_t index = s->blkdat;
        val = s->device_mem[index];
        s->blkdat = index + 1; /* advance for next read */
        break;
    }
    case SMBSLVCNT_OFFSET:
        val = s->slvcnt;
        break;
    default:
        /* Unimplemented offsets return 0xFF */
        val = 0xFF;
        break;
    }

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 1) {
        return;
    }

    switch (addr) {
    case SMBHSTSTS_OFFSET:
        /* W1C behavior is used by driver: outb_p(temp, SMBHSTSTS);
         * We clear bits that are set in the written value. */
        s->hststs &= ~(uint8_t)val;
        break;
    case SMBHSTCNT_OFFSET:
    {
        uint8_t newcnt = (uint8_t)val;
        /* Detect start of transaction when bit 6 (0x40) is set. */
        uint8_t oldcnt = s->hstcnt;
        s->hstcnt = newcnt;

        if ((newcnt & 0x40) && !(oldcnt & 0x40)) {
            /* Start transaction: set busy bit (0x01) then immediately
             * complete it for simplicity. */
            s->hststs |= 0x01; /* busy */
            pcibase_complete_transaction(s);
        }
        break;
    }
    case SMBHSTCMD_OFFSET:
        s->hstcmd = (uint8_t)val;
        break;
    case SMBHSTADD_OFFSET:
        s->hstadd = (uint8_t)val;
        break;
    case SMBHSTDAT0_OFFSET:
        s->hstdat0 = (uint8_t)val;
        break;
    case SMBHSTDAT1_OFFSET:
        s->hstdat1 = (uint8_t)val;
        break;
    case SMBBLKDAT_OFFSET:
        /* For write block transactions, the driver writes each byte
         * into SMBBLKDAT. We store bytes sequentially in device_mem
         * using blkdat as an index (initialized by the driver via
         * HSTDAT0 and CNT access pattern). */
        s->device_mem[s->blkdat] = (uint8_t)val;
        s->blkdat++;
        break;
    case SMBSLVCNT_OFFSET:
        s->slvcnt = (uint8_t)val;
        break;
    default:
        /* Ignore unknown offsets */
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

    pcibase_smbus_reset(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCIBASE_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCIBASE_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCIBASE_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* Initialize PCI config-space SMBus-related registers:
     * SMBBA: use a non-zero I/O base aligned to 16 bytes. The driver
     * masks with 0xfff0 and checks for non-zero.
     */
    uint16_t smb_base = 0x0C00; /* arbitrary non-zero, 16-byte aligned */
    s->smb_base_low  = smb_base & 0xFF;
    s->smb_base_high = (smb_base >> 8) & 0xFF;
    pci_set_word(pci_conf + SMBBA, smb_base);

    /* SMBHSTCFG: set bit0 to indicate SMBus host controller enabled. */
    s->smbhstcfg = 0x01;
    pci_set_byte(pci_conf + SMBHSTCFG, s->smbhstcfg);

    /* SMBREV: arbitrary non-zero revision. */
    s->smbrev = 0x10;
    pci_set_byte(pci_conf + SMBREV, s->smbrev);

    /* BAR Initialization: BAR0 as I/O space used by SMBus registers. */
    s->num_bars = 1;
    s->bar_info[0].index = BAR_INDEX_LEGACY_IO;
    s->bar_info[0].type  = BAR_TYPE_PIO;
    s->bar_info[0].size  = 32;
    s->bar_info[0].name  = "piix4-smbus-io";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = NULL;
    }

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize SMBus host controller register set */
    pcibase_smbus_reset(s);
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

    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "piix4_smbus_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(smb_base_low, PCIBaseState),
        VMSTATE_UINT8(smb_base_high, PCIBaseState),
        VMSTATE_UINT8(smbhstcfg, PCIBaseState),
        VMSTATE_UINT8(smbrev, PCIBaseState),
        VMSTATE_UINT8(hststs, PCIBaseState),
        VMSTATE_UINT8(hstcnt, PCIBaseState),
        VMSTATE_UINT8(hstcmd, PCIBaseState),
        VMSTATE_UINT8(hstadd, PCIBaseState),
        VMSTATE_UINT8(hstdat0, PCIBaseState),
        VMSTATE_UINT8(hstdat1, PCIBaseState),
        VMSTATE_UINT8(blkdat, PCIBaseState),
        VMSTATE_UINT8(slvcnt, PCIBaseState),
        VMSTATE_UINT8_ARRAY(device_mem, PCIBaseState, 256),
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
