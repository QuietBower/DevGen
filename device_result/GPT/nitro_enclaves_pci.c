/*
 * QEMU PCI device model for Nitro Enclaves PCI interface
 * Generated for Linux driver drivers/virt/nitro_enclaves/ne_pci_dev.c
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
/* Removed driver-specific header that is not available in QEMU build environment */

#define TYPE_PCIBASE_DEVICE "nitro_enclaves_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define NE_DEFAULT_TIMEOUT_MSECS    (120000)
#define NE_COMMAND          (0x0004)
#define NE_SEND_DATA        (0x0010)
#define NE_RECV_DATA        (0x0100)
#define NE_RECV_DATA_SIZE   (240)
#define NE_SEND_DATA_SIZE   (240)
#define NE_VEC_EVENT        (1)
#define NE_VEC_REPLY        (0)
#define NE_ENABLE_ON        (0x01)
#define NE_VERSION          (0x0002)
#define NE_VERSION_MAX      (0x0001)
#define NE_ENABLE           (0x0000)
#define NE_ENABLE_OFF       (0x00)
#define PCI_BAR_NE          (0x03)
#define PCI_DEVICE_ID_NE    (0xe4c1)
/* Define vendor ID locally since kernel PCI_VENDOR_ID_AMAZON is unavailable here */
#define NE_PCI_VENDOR_ID    (0x1d0f)
#define NE_PCI_DEVICE_ID    PCI_DEVICE_ID_NE
#define NE_PCI_CLASS_ID PCI_CLASS_OTHERS


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
    uint8_t reg_enable;      /* NE_ENABLE (8-bit) */
    uint16_t reg_version;    /* NE_VERSION (16-bit) */
    uint32_t reg_command;    /* NE_COMMAND (32-bit) */

    uint8_t send_buf[NE_SEND_DATA_SIZE];
    uint8_t recv_buf[NE_RECV_DATA_SIZE];

    /* Internal state */
    bool device_enabled;     /* reflects NE_ENABLE_ON/OFF */
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /*
     * The Linux driver expects two MSI-X vectors:
     *  - NE_VEC_REPLY: signaled after a command reply is ready
     *  - NE_VEC_EVENT: signaled for asynchronous events
     *
     * The actual triggering policy is implemented in this model when
     * commands are written to NE_COMMAND or when events are synthesized.
     * This helper is currently unused but kept for future extensions.
     */
    (void)pdev;
}

/* Helper to raise a specific MSI-X vector if enabled */
static void pcibase_msix_notify_vector(PCIBaseState *s, unsigned int vector)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (msix_enabled(pdev)) {
        msix_notify(pdev, vector);
    } else if (msi_enabled(pdev)) {
        /* Fallback: single MSI vector maps to any notification */
        msi_notify(pdev, 0);
    } else {
        /* Legacy INTx as a very simple fallback */
        pci_set_irq(pdev, 1);
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    (void)s;
    (void)pdev;
    (void)is_write;
    /*
     * The provided driver never programs DMA engine registers, nor it
     * uses pci_alloc_consistent or similar APIs for device-initiated DMA.
     * All data transfer is done via MMIO memcpy_toio/memcpy_fromio to
     * NE_SEND_DATA/NE_RECV_DATA. Therefore, no DMA emulation is needed.
     */
}

/*
 * Very small command engine used by the model.
 * The Linux driver checks cmd_reply fields including rc and state and
 * other 64-bit values for SLOT_INFO and related commands. The exact
 * semantics of those fields are not defined in the provided source,
 * so we only ensure that the structure layout in recv_buf matches the
 * driver's expectations and we return success (rc = 0) with all other
 * fields zeroed.
 */
static void pcibase_process_command(PCIBaseState *s)
{
    uint32_t cmd = s->reg_command;

    /* Clear previous reply contents */
    memset(s->recv_buf, 0, sizeof(s->recv_buf));

    /*
     * ne_pci_dev_cmd_reply layout from driver:
     *   s32 rc;              // offset 0, 4 bytes
     *   u8  padding0[4];     // offset 4, 4 bytes
     *   u64 slot_uid;        // offset 8
     *   u64 enclave_cid;     // offset 16
     *   u64 slot_count;      // offset 24
     *   u64 mem_regions;     // offset 32
     *   u64 mem_size;        // offset 40
     *   u64 nr_vcpus;        // offset 48
     *   u64 flags;           // offset 56
     *   u16 state;           // offset 64
     *   u8  padding1[6];     // offset 66
     * Total size fits into NE_RECV_DATA_SIZE (240 bytes).
     *
     * The driver treats negative rc as error. Zero means success.
     */

    /* Always return success (rc = 0) for all commands. */
    {
        int32_t rc = 0;
        memcpy(&s->recv_buf[0], &rc, sizeof(rc));
    }

    /* For SLOT_INFO and other commands we could populate more fields,
     * but their precise values are not defined in the provided source.
     * We leave them zero, which should be interpreted as a valid but
     * mostly empty reply by the driver.
     */
    (void)cmd;

    /* Notify the guest that a reply is ready via the reply MSI-X vector. */
    pcibase_msix_notify_vector(s, NE_VEC_REPLY);

    /* No asynchronous events (NE_VEC_EVENT) are generated. */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Handle NE_ENABLE (8-bit) */
    if (addr == NE_ENABLE && size == 1) {
        val = s->reg_enable;
        return val;
    }

    /* Handle NE_VERSION (16-bit) */
    if (addr == NE_VERSION && size == 2) {
        val = s->reg_version;
        return val;
    }

    /* Handle NE_COMMAND (32-bit) - typically not read by the driver */
    if (addr == NE_COMMAND && size == 4) {
        val = s->reg_command;
        return val;
    }

    /* Handle NE_SEND_DATA region (read is not used by the driver but is defined) */
    if (addr >= NE_SEND_DATA && addr < NE_SEND_DATA + NE_SEND_DATA_SIZE) {
        hwaddr offset = addr - NE_SEND_DATA;
        if (offset + size > NE_SEND_DATA_SIZE) {
            /* Out-of-bounds; return 0 */
            return 0;
        }
        switch (size) {
        case 1:
            val = s->send_buf[offset];
            break;
        case 2:
            val = le16_to_cpu(*(uint16_t *)&s->send_buf[offset]);
            break;
        case 4:
            val = le32_to_cpu(*(uint32_t *)&s->send_buf[offset]);
            break;
        case 8:
            val = le64_to_cpu(*(uint64_t *)&s->send_buf[offset]);
            break;
        default:
            break;
        }
        return val;
    }

    /* Handle NE_RECV_DATA region */
    if (addr >= NE_RECV_DATA && addr < NE_RECV_DATA + NE_RECV_DATA_SIZE) {
        hwaddr offset = addr - NE_RECV_DATA;
        if (offset + size > NE_RECV_DATA_SIZE) {
            /* Out-of-bounds; return 0 */
            return 0;
        }
        switch (size) {
        case 1:
            val = s->recv_buf[offset];
            break;
        case 2:
            val = le16_to_cpu(*(uint16_t *)&s->recv_buf[offset]);
            break;
        case 4:
            val = le32_to_cpu(*(uint32_t *)&s->recv_buf[offset]);
            break;
        case 8:
            val = le64_to_cpu(*(uint64_t *)&s->recv_buf[offset]);
            break;
        default:
            break;
        }
        return val;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Handle NE_ENABLE (8-bit) */
    if (addr == NE_ENABLE && size == 1) {
        uint8_t v = (uint8_t)val;
        s->reg_enable = v;

        if (v == NE_ENABLE_ON) {
            s->device_enabled = true;
        } else if (v == NE_ENABLE_OFF) {
            s->device_enabled = false;
        }
        return;
    }

    /* Handle NE_VERSION (16-bit) */
    if (addr == NE_VERSION && size == 2) {
        /* Device echoes the negotiated version back to the guest. */
        uint16_t v = (uint16_t)val;
        s->reg_version = v;
        return;
    }

    /* Handle NE_COMMAND (32-bit) */
    if (addr == NE_COMMAND && size == 4) {
        s->reg_command = (uint32_t)val;

        /* Process the command immediately and generate a reply. */
        pcibase_process_command(s);
        return;
    }

    /* Handle NE_SEND_DATA region */
    if (addr >= NE_SEND_DATA && addr < NE_SEND_DATA + NE_SEND_DATA_SIZE) {
        hwaddr offset = addr - NE_SEND_DATA;
        if (offset + size > NE_SEND_DATA_SIZE) {
            /* Truncate writes that go out of bounds */
            if (offset >= NE_SEND_DATA_SIZE) {
                return;
            }
            size = NE_SEND_DATA_SIZE - offset;
        }
        switch (size) {
        case 1:
            s->send_buf[offset] = (uint8_t)val;
            break;
        case 2:
            *(uint16_t *)&s->send_buf[offset] = cpu_to_le16((uint16_t)val);
            break;
        case 4:
            *(uint32_t *)&s->send_buf[offset] = cpu_to_le32((uint32_t)val);
            break;
        case 8:
            *(uint64_t *)&s->send_buf[offset] = cpu_to_le64((uint64_t)val);
            break;
        default:
            break;
        }
        return;
    }

    /* Writes to NE_RECV_DATA are ignored; the device is producer. */
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
    /* Logic for Port I/O (Legacy support) - not used by this driver */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    (void)s;
    (void)addr;
    (void)val;
    (void)size;
    /* Logic for Port I/O (Legacy support) - not used by this driver */
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

    /* Revert registers to power-on defaults */
    s->reg_enable = NE_ENABLE_OFF;
    s->reg_version = NE_VERSION_MAX;
    s->reg_command = 0;
    s->device_enabled = false;
    memset(s->send_buf, 0, sizeof(s->send_buf));
    memset(s->recv_buf, 0, sizeof(s->recv_buf));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  NE_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  NE_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, NE_PCI_CLASS_ID );
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
    s->bar_info[0].index = PCI_BAR_NE;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000; /* Size large enough to cover used offsets */
    s->bar_info[0].name = "ne-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = "";
    }
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Initialize internal register state to defaults */
    s->reg_enable = NE_ENABLE_OFF;
    s->reg_version = NE_VERSION_MAX;
    s->reg_command = 0;
    s->device_enabled = false;
    memset(s->send_buf, 0, sizeof(s->send_buf));
    memset(s->recv_buf, 0, sizeof(s->recv_buf));

    /* Initialize MSI-X with at least 2 vectors (reply and event) */
    Error *local_err = NULL;
    int rc = msix_init_exclusive_bar(pdev, 2, 0, &local_err);
    if (rc < 0) {
        if (local_err) {
            error_propagate(errp, local_err);
        }
        /* Fallback: try MSI with a single vector. */
        Error *msi_err = NULL;
        if (msi_init(pdev, 0, 1, true, false, &msi_err)) {
            if (msi_err) {
                error_propagate(errp, msi_err);
            }
        }
    }

      /* msi_init or msix_init calls */
      /* Set DMA masks or ring buffer limits */
      /* Initialize internal hardware timers if used */
      /* Final state initialization before the device is 'live' */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

     /* Free buffers, stop timers, etc. */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "nitro_enclaves_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8(reg_enable, PCIBaseState),
        VMSTATE_UINT16(reg_version, PCIBaseState),
        VMSTATE_UINT32(reg_command, PCIBaseState),
        VMSTATE_BOOL(device_enabled, PCIBaseState),
        VMSTATE_BUFFER(send_buf, PCIBaseState),
        VMSTATE_BUFFER(recv_buf, PCIBaseState),
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
