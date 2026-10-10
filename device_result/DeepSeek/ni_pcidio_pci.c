/*
 * QEMU model of NI PCI-DIO-32HS
 * based on driver ni_pcidio.c
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

#define TYPE_PCIBASE_DEVICE "ni_pcidio_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_NI 0x1093
#define PCI_DEVICE_ID_NI_PCIDIO_32HS 0x1150
#define PCI_CLASS_ID 0xff00

#define INT_EN (COUNT_EXPIRED | WAITED | PRIMARY_TC | SECONDARY_TC)
#define CHIP_VERSION                            27
#define INTERRUPT_LINE(x)                       ((x) & 3)
#define FIFO_RESET                              BIT(7)
#define START_DELAY                     PROTOCOL_REGISTER_8
#define REQ                                     BIT(2)
#define DMA_RESET                               BIT(6)
#define OP_MODE                         PROTOCOL_REGISTER_1
#define BLOCK_MODE                      PROTOCOL_REGISTER_5
#define MAX_SPEED                               (TIMER_BASE)
#define TIMER_BASE 50
#define USE_DMA
#define WINDOW_ADDRESS                          4
#define INTERRUPT_AND_WINDOW_STATUS             4
#define INT_STATUS_1                            BIT(0)
#define INT_STATUS_2                            BIT(1)
#define WINDOW_ADDRESS_STATUS_MASK              0x7c
#define MASTER_DMA_AND_INTERRUPT_CONTROL 5
#define OPEN_INT                                BIT(2)
#define GROUP_STATUS                            5
#define DATA_LEFT                               BIT(0)
#define STOP_TRIG                               BIT(3)
#define GROUP_1_FLAGS                           6
#define GROUP_2_FLAGS                           7
#define TRANSFER_READY                          BIT(0)
#define COUNT_EXPIRED                           BIT(1)
#define WAITED                                  BIT(5)
#define PRIMARY_TC                              BIT(6)
#define SECONDARY_TC                            BIT(7)
#define GROUP_1_FIRST_CLEAR                     6
#define GROUP_2_FIRST_CLEAR                     7
#define CLEAR_WAITED                            BIT(3)
#define CLEAR_PRIMARY_TC                        BIT(4)
#define CLEAR_SECONDARY_TC                      BIT(5)
#define CLEAR_ALL                               0xf8
#define GROUP_1_FIFO                            8
#define GROUP_2_FIFO                            12
#define TRANSFER_COUNT                          20
#define CHIP_ID_D                               24
#define CHIP_ID_I                               25
#define CHIP_ID_O                               26
#define PORT_IO(x)                              (28 + (x))
#define PORT_PIN_DIRECTIONS(x)                  (32 + (x))
#define PORT_PIN_MASK(x)                        (36 + (x))
#define PORT_PIN_POLARITIES(x)                  (40 + (x))
#define MASTER_CLOCK_ROUTING                    45
#define RTSI_CLOCKING(x)                        (((x) & 3) << 4)
#define GROUP_1_SECOND_CLEAR                    46
#define GROUP_2_SECOND_CLEAR                    47
#define CLEAR_EXPIRED                           BIT(0)
#define PORT_PATTERN(x)                         (48 + (x))
#define DATA_PATH                               64
#define FIFO_ENABLE_A                           BIT(0)
#define FIFO_ENABLE_B                           BIT(1)
#define FIFO_ENABLE_C                           BIT(2)
#define FIFO_ENABLE_D                           BIT(3)
#define FUNNELING(x)                            (((x) & 3) << 4)
#define GROUP_DIRECTION                         BIT(7)
#define PROTOCOL_REGISTER_1                     65
#define RUN_MODE(x)                             ((x) & 7)
#define NUMBERED                                BIT(3)
#define PROTOCOL_REGISTER_2                     66
#define CLOCK_REG                               PROTOCOL_REGISTER_2
#define CLOCK_LINE(x)                           (((x) & 3) << 5)
#define INVERT_STOP_TRIG                        BIT(7)
#define DATA_LATCHING(x)                        (((x) & 3) << 5)
#define PROTOCOL_REGISTER_3                     67
#define SEQUENCE                                PROTOCOL_REGISTER_3
#define PROTOCOL_REGISTER_14                    68
#define CLOCK_SPEED                             PROTOCOL_REGISTER_14
#define PROTOCOL_REGISTER_4                     70
#define REQ_REG                                 PROTOCOL_REGISTER_4
#define REQ_CONDITIONING(x)                     (((x) & 7) << 3)
#define PROTOCOL_REGISTER_5                     71
#define FIFO_Control                            72
#define READY_LEVEL(x)                          ((x) & 7)
#define PROTOCOL_REGISTER_6                     73
#define LINE_POLARITIES                         PROTOCOL_REGISTER_6
#define INVERT_ACK                              BIT(0)
#define INVERT_REQ                              BIT(1)
#define INVERT_CLOCK                            BIT(2)
#define INVERT_SERIAL                           BIT(3)
#define OPEN_ACK                                BIT(4)
#define OPEN_CLOCK                              BIT(5)
#define PROTOCOL_REGISTER_7                     74
#define ACK_SER                                 PROTOCOL_REGISTER_7
#define ACK_LINE(x)                             (((x) & 3) << 2)
#define EXCHANGE_PINS                           BIT(7)
#define INTERRUPT_CONTROL                       75
#define DMA_LINE_CONTROL_GROUP1                 76
#define DMA_LINE_CONTROL_GROUP2                 108
#define TRANSFER_SIZE_CONTROL                   77
#define TRANSFER_WIDTH(x)                       ((x) & 3)
#define TRANSFER_LENGTH(x)                      (((x) & 3) << 3)
#define REQUIRE_R_LEVEL                         BIT(5)
#define PROTOCOL_REGISTER_15                    79
#define DAQ_OPTIONS                             PROTOCOL_REGISTER_15
#define START_SOURCE(x)                         ((x) & 0x3)
#define INVERT_START                            BIT(2)
#define STOP_SOURCE(x)                          (((x) & 0x3) << 3)
#define REQ_START                               BIT(6)
#define PRE_START                               BIT(7)
#define PATTERN_DETECTION                       81
#define DETECTION_METHOD                        BIT(0)
#define INVERT_MATCH                            BIT(1)
#define IE_PATTERN_DETECTION                    BIT(2)
#define PROTOCOL_REGISTER_9                     82
#define REQ_DELAY                               PROTOCOL_REGISTER_9
#define PROTOCOL_REGISTER_10                    83
#define REQ_NOT_DELAY                           PROTOCOL_REGISTER_10
#define PROTOCOL_REGISTER_11                    84
#define ACK_DELAY                               PROTOCOL_REGISTER_11
#define PROTOCOL_REGISTER_12                    85
#define ACK_NOT_DELAY                           PROTOCOL_REGISTER_12
#define PROTOCOL_REGISTER_13                    86
#define DATA_1_DELAY                            PROTOCOL_REGISTER_13
#define PROTOCOL_REGISTER_8                     88

#define BAR0_SIZE 0x1000
#define MIT_BAR_SIZE 0x100

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
    MemoryRegion mite_mmio;  /* MITE registers BAR1 */
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    /* Hardware Register Shadows: byte-accessible via uint32_t array */
    uint32_t regs[0x100];

    /* DMA Context */
    struct {
        uint32_t count;
        uint32_t addr;
        uint32_t next;
        uint32_t dar;
        bool active;
    } dma;

    uint32_t status;
    bool reset_active;
    int board_type;
};

/* Internal helper for status-triggered signaling */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    /* Implement interrupt logic based on driver ISR if needed */
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* Implement DMA if required */
}

static uint32_t regs_read_byte(PCIBaseState *s, hwaddr addr)
{
    if (addr >= sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read byte out of range: 0x%"PRIx64"\n",
                      __func__, addr);
        return 0;
    }
    uint32_t index = addr >> 2;
    uint32_t shift = (addr & 3) * 8;
    return (s->regs[index] >> shift) & 0xff;
}

static uint32_t regs_read_word(PCIBaseState *s, hwaddr addr)
{
    if (addr + 1 >= sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read word out of range: 0x%"PRIx64"\n",
                      __func__, addr);
        return 0;
    }
    /* assume little-endian, aligned */
    uint32_t b0 = regs_read_byte(s, addr);
    uint32_t b1 = regs_read_byte(s, addr + 1);
    return b0 | (b1 << 8);
}

static uint32_t regs_read_dword(PCIBaseState *s, hwaddr addr)
{
    if (addr + 3 >= sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read dword out of range: 0x%"PRIx64"\n",
                      __func__, addr);
        return 0;
    }
    if (addr & 3) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned dword read: 0x%"PRIx64"\n",
                      __func__, addr);
        return 0;
    }
    return s->regs[addr >> 2];
}

static void regs_write_byte(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    if (addr >= sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write byte out of range: 0x%"PRIx64" = 0x%x\n",
                      __func__, addr, val);
        return;
    }
    uint32_t index = addr >> 2;
    uint32_t shift = (addr & 3) * 8;
    s->regs[index] = (s->regs[index] & ~(0xff << shift)) | ((val & 0xff) << shift);
}

static void regs_write_word(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    if (addr + 1 >= sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write word out of range: 0x%"PRIx64" = 0x%x\n",
                      __func__, addr, val);
        return;
    }
    regs_write_byte(s, addr, val & 0xff);
    regs_write_byte(s, addr + 1, (val >> 8) & 0xff);
}

static void regs_write_dword(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    if (addr + 3 >= sizeof(s->regs)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write dword out of range: 0x%"PRIx64" = 0x%x\n",
                      __func__, addr, val);
        return;
    }
    if (addr & 3) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unaligned dword write: 0x%"PRIx64" = 0x%x\n",
                      __func__, addr, val);
        return;
    }
    s->regs[addr >> 2] = val;
}

/* MMIO Handler for main board registers (BAR0) */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (size) {
    case 1:
        val = regs_read_byte(s, addr);
        break;
    case 2:
        val = regs_read_word(s, addr);
        break;
    case 4:
        val = regs_read_dword(s, addr);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid read size %u at 0x%"PRIx64"\n",
                      __func__, size, addr);
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (size) {
    case 1:
        regs_write_byte(s, addr, val);
        break;
    case 2:
        regs_write_word(s, addr, val);
        break;
    case 4:
        regs_write_dword(s, addr, val);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: invalid write size %u at 0x%"PRIx64" = 0x%"PRIx64"\n",
                      __func__, size, addr, val);
        break;
    }
}

/* MMIO Handler for MITE registers (BAR1). Simplified: log and ignore writes, return 0 on reads. */
static uint64_t pcibase_mite_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: MITE read addr=0x%"PRIx64" size=%u\n", __func__, addr, size);
    return 0;
}

static void pcibase_mite_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "%s: MITE write addr=0x%"PRIx64" size=%u val=0x%"PRIx64"\n", __func__, addr, size, val);
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_mite_mmio_ops = {
    .read = pcibase_mite_mmio_read,
    .write = pcibase_mite_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Initialize board register shadows to 0 */
    memset(s->regs, 0, sizeof(s->regs));

    /* Set CHIP_VERSION register (offset 27) to a plausible value */
    regs_write_byte(s, CHIP_VERSION, 0x01); /* version 1 */

    s->intr_status = 0;
    s->intr_mask = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x1093);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x1150);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR0: main board registers */
    s->num_bars = 2;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "bar0";
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);

    /* BAR1: MITE registers */
    memory_region_init_io(&s->mite_mmio, OBJECT(s), &pcibase_mite_mmio_ops, s, "mite-mmio", MIT_BAR_SIZE);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mite_mmio);
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "ni_pcidio_pci",
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
