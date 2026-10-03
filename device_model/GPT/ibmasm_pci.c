/*
 * QEMU IBMASM PCI Device Model
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
/* 'hw/input/input.h' removed: header not available in this QEMU version and not used */

#define TYPE_PCIBASE_DEVICE "ibmasm_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define IBMASM_VENDOR_ID   0x1014
#define IBMASM_DEVICE_ID   0x010F
#define IBMASM_CLASS_ID    PCI_CLASS_OTHERS

#define SCOUT_COM_B_BASE         0x0100
#define SP_INTR_MASK             0x00000008
#define UART_INTR_MASK           0x00000010
#define INTR_CONTROL_REGISTER    0x13A4
#define CONDOR_MOUSE_ISR_CONTROL 0x00
#define CONDOR_MOUSE_DATA        0x000AC000
#define INBOUND_QUEUE_PORT       0x40

#define HEARTBEAT_BUFFER_SIZE        0x400
#define IBMASM_EVENT_MAX_SIZE        2048u
#define IBMASM_CMD_MAX_BUFFER_SIZE   0x8000

#define XLATE_SIZE 256

#define MAILBOX_FULL(x)  ((x) & 0x00000001)
#define GET_MFA_ADDR(x)  ((x) & 0xFFFFFF00)

/* Keyboard and symbol key code definitions from driver source */
#define KEY_RESERVED        0
#define KEY_SPACE           57
#define KEY_GRAVE           41
#define KEY_1               2
#define KEY_2               3
#define KEY_3               4
#define KEY_4               5
#define KEY_5               6
#define KEY_6               7
#define KEY_7               8
#define KEY_8               9
#define KEY_9               10
#define KEY_0               11
#define KEY_MINUS           12
#define KEY_EQUAL           13
#define KEY_LEFTBRACE       26
#define KEY_RIGHTBRACE      27
#define KEY_BACKSLASH       43
#define KEY_APOSTROPHE      40
#define KEY_SEMICOLON       39
#define KEY_COMMA           51
#define KEY_DOT             52
#define KEY_SLASH           53
#define KEY_A               30
#define KEY_B               48
#define KEY_C               46
#define KEY_D               32
#define KEY_E               18
#define KEY_F               33
#define KEY_G               34
#define KEY_H               35
#define KEY_I               23
#define KEY_J               36
#define KEY_K               37
#define KEY_L               38
#define KEY_M               50
#define KEY_N               49
#define KEY_O               24
#define KEY_P               25
#define KEY_Q               16
#define KEY_R               19
#define KEY_S               31
#define KEY_T               20
#define KEY_U               22
#define KEY_V               47
#define KEY_W               17
#define KEY_X               45
#define KEY_Y               21
#define KEY_Z               44
#define KEY_ENTER           28
#define KEY_KPSLASH         98
#define KEY_KPASTERISK      55
#define KEY_KPMINUS         74
#define KEY_KPDOT           83
#define KEY_KPPLUS          78
#define KEY_KP0             82
#define KEY_KP1             79
#define KEY_KP2             80
#define KEY_KP3             81
#define KEY_KP4             75
#define KEY_KP5             76
#define KEY_KP6             77
#define KEY_KP7             71
#define KEY_KP8             72
#define KEY_KP9             73
#define KEY_BACKSPACE       14
#define KEY_TAB             15
#define KEY_LEFTCTRL        29
#define KEY_LEFTALT         56
#define KEY_INSERT          110
#define KEY_DELETE          111
#define KEY_LEFTSHIFT       42
#define KEY_UP              103
#define KEY_DOWN            108
#define KEY_LEFT            105
#define KEY_RIGHT           106
#define KEY_ESC             1
#define KEY_PAGEUP          104
#define KEY_PAGEDOWN        109
#define KEY_HOME            102
#define KEY_END             107
#define KEY_F1              59
#define KEY_F2              60
#define KEY_F3              61
#define KEY_F4              62
#define KEY_F5              63
#define KEY_F6              64
#define KEY_F7              65
#define KEY_F8              66
#define KEY_F9              67
#define KEY_F10             68
#define KEY_F11             87
#define KEY_F12             88
#define KEY_CAPSLOCK        58
#define KEY_NUMLOCK         69
#define KEY_SCROLLLOCK      70
#define KEY_SYM_a           0x61
#define KEY_SYM_b           0x62
#define KEY_SYM_c           0x63
#define KEY_SYM_d           0x64
#define KEY_SYM_e           0x65
#define KEY_SYM_f           0x66
#define KEY_SYM_g           0x67
#define KEY_SYM_h           0x68
#define KEY_SYM_i           0x69
#define KEY_SYM_j           0x6A
#define KEY_SYM_k           0x6B
#define KEY_SYM_l           0x6C
#define KEY_SYM_m           0x6D
#define KEY_SYM_n           0x6E
#define KEY_SYM_o           0x6F
#define KEY_SYM_p           0x70
#define KEY_SYM_q           0x71
#define KEY_SYM_r           0x72
#define KEY_SYM_s           0x73
#define KEY_SYM_t           0x74
#define KEY_SYM_u           0x75
#define KEY_SYM_v           0x76
#define KEY_SYM_w           0x77
#define KEY_SYM_x           0x78
#define KEY_SYM_y           0x79
#define KEY_SYM_z           0x7A


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
    uint32_t intr_ctrl;             /* INTR_CONTROL_REGISTER shadow */
    uint32_t inbound_queue_value;   /* INBOUND_QUEUE_PORT shadow */

    /* DMA Context */
    dma_addr_t dma_buffer_addr;     /* Backing buffer for I2O messages */
    size_t dma_buffer_size;
    uint8_t *dma_buffer;
};


/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* Simple level-triggered INTx based on inverse of mask bits. */
    if (!(s->intr_ctrl & (SP_INTR_MASK | UART_INTR_MASK))) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* The real hardware performs DMA to/from host memory for I2O messages.
     * The Linux driver never inspects device memory, only uses MMIO for queue
     * control. We therefore keep this empty for now to avoid invented behavior.
     */
    (void)pdev;
    (void)is_write;
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Only 32-bit accesses are used by the driver (readl/writel). */
    if (size != 4) {
        return 0;
    }

    switch (addr) {
    case INTR_CONTROL_REGISTER:
        /* Interrupt control register shadow */
        val = s->intr_ctrl;
        break;
    case INBOUND_QUEUE_PORT:
        /* get_mfa_inbound() reads this mailbox */
        val = s->inbound_queue_value;
        break;
    default:
        /* For all other offsets, just return 0.
         * This keeps the device quiet and is sufficient for probe paths
         * which only touch INTR_CONTROL_REGISTER and INBOUND_QUEUE_PORT
         * explicitly in provided code.
         */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case INTR_CONTROL_REGISTER:
        /* ibmasm_enable/disable_interrupts read-modify-write this register
         * using SP_INTR_MASK and UART_INTR_MASK bits. We simply store it.
         */
        s->intr_ctrl = (uint32_t)val;
        pcibase_update_irq(s);
        break;
    case INBOUND_QUEUE_PORT:
        /* set_mfa_inbound() writes here when a message is ready. The driver
         * does not re-read this value in the provided paths after writing,
         * so we only store it for completeness.
         */
        s->inbound_queue_value = (uint32_t)val;
        break;
    default:
        /* Ignore other writes */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    (void)s;
    (void)addr;
    (void)size;
     /* Logic for Port I/O (Legacy support) */
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
     /* Logic for Port I/O (Legacy support) */
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
    s->intr_ctrl = 0x00000000;
    s->inbound_queue_value = 0x00000000;
    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  IBMASM_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  IBMASM_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, IBMASM_CLASS_ID );
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
    s->bar_info[0].index = 0;
    s->bar_info[0].type  = BAR_TYPE_MMIO;
    /* The driver only touches small offsets, but BAR must be power-of-two; use 4KB. */
    s->bar_info[0].size  = 0x1000;
    s->bar_info[0].name  = "ibmasm-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type  = BAR_TYPE_NONE;
        s->bar_info[i].size  = 0;
        s->bar_info[i].name  = "";
    }
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

      /* msi_init or msix_init calls */
      /* The driver uses legacy INTx via request_irq on pdev->irq, so MSI/MSI-X are not required. */
      s->has_msi = false;
      s->has_msix = false;

      /* Set DMA masks or ring buffer limits */
      /* Not required: driver never programs DMA masks via PCI config. */

      /* Initialize internal hardware state */
      s->intr_ctrl = 0x00000000;
      s->inbound_queue_value = 0x00000000;
      s->dma_buffer = NULL;
      s->dma_buffer_addr = 0;
      s->dma_buffer_size = 0;
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

     /* Free buffers, stop timers, etc. */
    if (s->dma_buffer) {
        g_free(s->dma_buffer);
        s->dma_buffer = NULL;
        s->dma_buffer_addr = 0;
        s->dma_buffer_size = 0;
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ibmasm_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(intr_ctrl, PCIBaseState),
        VMSTATE_UINT32(inbound_queue_value, PCIBaseState),
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

