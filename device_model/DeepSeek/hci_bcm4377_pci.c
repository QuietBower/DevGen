/*
 * Integrated QEMU PCI device model for Broadcom BCM4377 Bluetooth
 * Based on Linux driver hci_bcm4377.c
 * Generated for Phase 2: Functional Behavior Implementation
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
#include "qemu/bitops.h"

#define TYPE_PCIBASE_DEVICE "hci_bcm4377_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Hardware identifiers extracted from Linux driver */
#define VENDOR_ID 0x14E4
#define DEVICE_ID 0x5FA0
#define CLASS_ID 0x028000

#define BCM4377_DEVICE_ID 0x5fa0
#define BCM4378_DEVICE_ID 0x5f69
#define BCM4387_DEVICE_ID 0x5f71
#define BCM4388_DEVICE_ID 0x5f72
#define BCM4377_TIMEOUT 1000
#define BCM4377_BOOT_TIMEOUT 5000
#define BCM4377_DMA_MASK 0xfffffe00
#define BCM4377_PCIECFG_BAR0_WINDOW1	   0x80
#define BCM4377_PCIECFG_BAR0_WINDOW2	   0x70
#define BCM4377_PCIECFG_BAR0_CORE2_WINDOW1 0x74
#define BCM4377_PCIECFG_BAR0_CORE2_WINDOW2 0x78
#define BCM4377_PCIECFG_BAR2_WINDOW	   0x84
#define BCM4377_PCIECFG_BAR0_CORE2_WINDOW1_DEFAULT 0x18011000
#define BCM4377_PCIECFG_BAR2_WINDOW_DEFAULT	   0x19000000
#define BCM4377_PCIECFG_SUBSYSTEM_CTRL 0x88
#define BCM4377_BAR0_FW_DOORBELL 0x140
#define BCM4377_BAR0_RTI_CONTROL 0x144
#define BCM4377_BAR0_SLEEP_CONTROL	      0x150
#define BCM4377_BAR0_SLEEP_CONTROL_UNQUIESCE  0
#define BCM4377_BAR0_SLEEP_CONTROL_AWAKE      2
#define BCM4377_BAR0_SLEEP_CONTROL_QUIESCE    3
#define BCM4377_BAR0_DOORBELL	    0x174
#define BCM4377_BAR0_DOORBELL_VALUE GENMASK(31, 16)
#define BCM4377_BAR0_DOORBELL_IDX   GENMASK(15, 8)
#define BCM4377_BAR0_DOORBELL_RING  BIT(5)
#define BCM4377_BAR0_HOST_WINDOW_LO   0x590
#define BCM4377_BAR0_HOST_WINDOW_HI   0x594
#define BCM4377_BAR0_HOST_WINDOW_SIZE 0x598
#define BCM4377_BAR2_BOOTSTAGE 0x200454
#define BCM4377_BAR2_FW_LO   0x200478
#define BCM4377_BAR2_FW_HI   0x20047c
#define BCM4377_BAR2_FW_SIZE 0x200480
#define BCM4377_BAR2_CONTEXT_ADDR_LO 0x20048c
#define BCM4377_BAR2_CONTEXT_ADDR_HI 0x200450
#define BCM4377_BAR2_RTI_STATUS	     0x20045c
#define BCM4377_BAR2_RTI_WINDOW_LO   0x200494
#define BCM4377_BAR2_RTI_WINDOW_HI   0x200498
#define BCM4377_BAR2_RTI_WINDOW_SIZE 0x20049c
#define BCM4377_OTP_SIZE	  0xe0
#define BCM4377_OTP_SYS_VENDOR	  0x15
#define BCM4377_OTP_CIS		  0x80
#define BCM4377_OTP_VENDOR_HDR	  0x00000008
#define BCM4377_OTP_MAX_PARAM_LEN 16
#define BCM4377_N_TRANSFER_RINGS   9
#define BCM4377_N_COMPLETION_RINGS 6
#define BCM4377_MAX_RING_SIZE 256
#define BCM4377_MSGID_GENERATION GENMASK(15, 8)
#define BCM4377_MSGID_ID	 GENMASK(7, 0)
#define BCM4377_RING_N_ENTRIES 128
#define BCM4377_CONTROL_MSG_SIZE		   0x34
#define BCM4377_XFER_RING_MAX_INPLACE_PAYLOAD_SIZE (4 * 0xff)
#define MAX_ACL_PAYLOAD_SIZE   (1024 + 4)
#define MAX_SCO_PAYLOAD_SIZE   (255 + 3)
#define MAX_EVENT_PAYLOAD_SIZE (255 + 2)
#define BCM4377_XFER_RING_FLAG_PAYLOAD_MAPPED	 BIT(0)
#define BCM4377_XFER_RING_FLAG_PAYLOAD_IN_FOOTER BIT(1)
#define BCM4377_XFER_RING_FLAG_VIRTUAL BIT(7)
#define BCM4377_XFER_RING_FLAG_SYNC    BIT(8)

#define BCM4378_CALIBRATION_CHUNK_SIZE 0xe6
#define BCM4378_PTB_CHUNK_SIZE 0xcf

/* Static OTP data for device identification (stepping = b1, vendor = Broadcom) */
static const uint8_t default_otp[BCM4377_OTP_SIZE] = {
    [0] = 0x15,
    [1] = 0x10,             /* length = 16 */
    [2] = 's', [3] = '=', [4] = 'b', [5] = '1',
    [6] = 0,                /* null terminator for "s=b1" */
    [7] = 'V', [8] = '=', [9] = 'B', [10] = 'r', [11] = 'o',
    [12] = 'a', [13] = 'd', [14] = 'c', [15] = 'o', [16] = 'm',
    [18] = 0                /* end of list marker */
};

/* State structure for the device */
typedef struct {
    int index;
    int type;
    hwaddr size;
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

    /* Hardware register shadows */
    uint32_t bar0_fw_doorbell;
    uint32_t bar0_rti_control;
    uint32_t bar0_sleep_control;
    uint32_t bar0_doorbell;
    uint32_t bar0_host_window_lo;
    uint32_t bar0_host_window_hi;
    uint32_t bar0_host_window_size;
    uint32_t bar2_bootstage;
    uint32_t bar2_fw_lo;
    uint32_t bar2_fw_hi;
    uint32_t bar2_fw_size;
    uint32_t bar2_context_addr_lo;
    uint32_t bar2_context_addr_hi;
    uint32_t bar2_rti_status;
    uint32_t bar2_rti_window_lo;
    uint32_t bar2_rti_window_hi;
    uint32_t bar2_rti_window_size;

    uint8_t otp[BCM4377_OTP_SIZE];
    uint64_t dma_mask;
    uint8_t power_state;
};

/* Helper to raise MSI or INTx interrupt */
static void pcibase_raise_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (msi_enabled(pdev)) {
        msi_notify(pdev, 0);
    } else {
        pci_set_irq(pdev, 1);
    }
}

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < 0x1000) { /* BAR0 region */
        if (addr < BCM4377_OTP_SIZE) {
            /* OTP reads are byte-oriented */
            if (size == 1) {
                return s->otp[addr];
            }
            return 0;
        }

        switch (addr) {
        case BCM4377_BAR0_FW_DOORBELL:
            val = s->bar0_fw_doorbell;
            break;
        case BCM4377_BAR0_RTI_CONTROL:
            val = s->bar0_rti_control;
            break;
        case BCM4377_BAR0_SLEEP_CONTROL:
            val = s->bar0_sleep_control;
            break;
        case BCM4377_BAR0_DOORBELL:
            val = s->bar0_doorbell;
            break;
        case BCM4377_BAR0_HOST_WINDOW_LO:
            val = s->bar0_host_window_lo;
            break;
        case BCM4377_BAR0_HOST_WINDOW_HI:
            val = s->bar0_host_window_hi;
            break;
        case BCM4377_BAR0_HOST_WINDOW_SIZE:
            val = s->bar0_host_window_size;
            break;
        default:
            val = 0;
        }
    } else if (addr >= 0x200000 && addr < 0x200000 + 0x400000) { /* BAR2 region */
        switch (addr) {
        case BCM4377_BAR2_BOOTSTAGE:
            val = s->bar2_bootstage;
            break;
        case BCM4377_BAR2_FW_LO:
            val = s->bar2_fw_lo;
            break;
        case BCM4377_BAR2_FW_HI:
            val = s->bar2_fw_hi;
            break;
        case BCM4377_BAR2_FW_SIZE:
            val = s->bar2_fw_size;
            break;
        case BCM4377_BAR2_CONTEXT_ADDR_LO:
            val = s->bar2_context_addr_lo;
            break;
        case BCM4377_BAR2_CONTEXT_ADDR_HI:
            val = s->bar2_context_addr_hi;
            break;
        case BCM4377_BAR2_RTI_STATUS:
            val = s->bar2_rti_status;
            break;
        case BCM4377_BAR2_RTI_WINDOW_LO:
            val = s->bar2_rti_window_lo;
            break;
        case BCM4377_BAR2_RTI_WINDOW_HI:
            val = s->bar2_rti_window_hi;
            break;
        case BCM4377_BAR2_RTI_WINDOW_SIZE:
            val = s->bar2_rti_window_size;
            break;
        default:
            val = 0;
        }
    }

    return val;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x1000) { /* BAR0 */
        if (addr < BCM4377_OTP_SIZE) {
            /* OTP is read-only, ignore writes */
            return;
        }

        switch (addr) {
        case BCM4377_BAR0_FW_DOORBELL:
            s->bar0_fw_doorbell = val;
            if (val == 0) {
                /* Boot the firmware: construct DMA address */
                dma_addr_t fw_addr = ((uint64_t)s->bar2_fw_hi << 32) | s->bar2_fw_lo;
                uint32_t fw_size = s->bar2_fw_size;

                if (fw_size) {
                    /* Minimal DMA read to simulate firmware fetching */
                    uint8_t dummy;
                    pci_dma_read(PCI_DEVICE(s), fw_addr, &dummy, 1);
                }

                /* Boot completed */
                s->bar2_bootstage = 2;
                pcibase_raise_irq(s);
            }
            break;

        case BCM4377_BAR0_RTI_CONTROL:
            s->bar0_rti_control = val;
            if (val == 1 && s->bar2_rti_status == 0) {
                s->bar2_rti_status = 1;
                pcibase_raise_irq(s);
            } else if (val == 2 && s->bar2_rti_status == 1) {
                s->bar2_rti_status = 2;
                pcibase_raise_irq(s);
            }
            break;

        case BCM4377_BAR0_SLEEP_CONTROL:
            s->bar0_sleep_control = val;
            break;

        case BCM4377_BAR0_DOORBELL:
            s->bar0_doorbell = val;
            /* Full ring processing not implemented; only needed for data transfer */
            break;

        case BCM4377_BAR0_HOST_WINDOW_LO:
            s->bar0_host_window_lo = val;
            break;

        case BCM4377_BAR0_HOST_WINDOW_HI:
            s->bar0_host_window_hi = val;
            break;

        case BCM4377_BAR0_HOST_WINDOW_SIZE:
            s->bar0_host_window_size = val;
            break;

        default:
            break;
        }
    } else if (addr >= 0x200000 && addr < 0x200000 + 0x400000) { /* BAR2 */
        switch (addr) {
        case BCM4377_BAR2_BOOTSTAGE:
            s->bar2_bootstage = val;
            break;
        case BCM4377_BAR2_FW_LO:
            s->bar2_fw_lo = val;
            break;
        case BCM4377_BAR2_FW_HI:
            s->bar2_fw_hi = val;
            break;
        case BCM4377_BAR2_FW_SIZE:
            s->bar2_fw_size = val;
            break;
        case BCM4377_BAR2_CONTEXT_ADDR_LO:
            s->bar2_context_addr_lo = val;
            break;
        case BCM4377_BAR2_CONTEXT_ADDR_HI:
            s->bar2_context_addr_hi = val;
            break;
        case BCM4377_BAR2_RTI_STATUS:
            s->bar2_rti_status = val;
            break;
        case BCM4377_BAR2_RTI_WINDOW_LO:
            s->bar2_rti_window_lo = val;
            break;
        case BCM4377_BAR2_RTI_WINDOW_HI:
            s->bar2_rti_window_hi = val;
            break;
        case BCM4377_BAR2_RTI_WINDOW_SIZE:
            s->bar2_rti_window_size = val;
            break;
        default:
            break;
        }
    }
}

/* PIO read/write (unused, but must exist) */
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

    /* Reset all hardware registers to default state */
    s->bar0_fw_doorbell = 0;
    s->bar0_rti_control = 0;
    s->bar0_sleep_control = 0;
    s->bar0_doorbell = 0;
    s->bar0_host_window_lo = 0;
    s->bar0_host_window_hi = 0;
    s->bar0_host_window_size = 0;
    s->bar2_bootstage = 0;
    s->bar2_fw_lo = 0;
    s->bar2_fw_hi = 0;
    s->bar2_fw_size = 0;
    s->bar2_context_addr_lo = 0;
    s->bar2_context_addr_hi = 0;
    s->bar2_rti_status = 0;
    s->bar2_rti_window_lo = 0;
    s->bar2_rti_window_hi = 0;
    s->bar2_rti_window_size = 0;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == 0) {
        return;
    }
    
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == 1) { /* MMIO */
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == 2) { /* PIO */
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == 3) { /* RAM */
        memory_region_init_ram(mr, OBJECT(s), bi->name, aligned_size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Add PCIe capability at offset 0xA0 to avoid overlap with vendor-specific config registers */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0xA0);

    /* Initialize MSI */
    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        error_setg(errp, "MSI initialization failed");
        return;
    }

    /* Copy default OTP data */
    memcpy(s->otp, default_otp, BCM4377_OTP_SIZE);

    /* Initialize BARs */
    s->num_bars = 2;
    s->bar_info[0] = (BARInfo){ .index = 0, .type = 1, .size = 0x1000, .name = "bar0" };
    s->bar_info[1] = (BARInfo){ .index = 2, .type = 1, .size = 0x400000, .name = "bar2" };

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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
}

static const VMStateDescription vmstate_pcibase = {
    .name = "hci_bcm4377_pci",
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
