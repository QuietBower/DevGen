#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "qom/object.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "qemu/bitops.h"
#include "hw/qdev-core.h"
#include "qemu/module.h"

/* Additional include files retrieved from driver context */
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "yenta_cardbus_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID PCI_VENDOR_ID_TI
#define DEVICE_ID PCI_DEVICE_ID_TI_1031
#define PCI_DEVICE_ID_TI_1031 0x1031
#define CLASS_ID 0x0607

#define I365_STATUS 0x01
#define I365_CS_BVD2 0x02
#define I365_INTCTL 0x03
#define I365_CS_POWERON 0x40
#define CB_YVCARD 0x00002000
#define CB_PWRCYCLE 0x00000008
#define CB_CBCARD 0x00000020
#define CB_CARDSTS 0x00000001
#define CB_CDETECT1 0x00000002
#define CB_XVCARD 0x00001000
#define I365_CS_BVD1 0x01
#define CB_CDETECT2 0x00000004
#define I365_CS_DETECT 0x0C
#define I365_PC_IOCARD 0x20
#define CB_SOCKET_STATE 0x08
#define CB_16BITCARD 0x00000010
#define I365_CS_STSCHG 0x01
#define I365_CS_WRPROT 0x10
#define CB_3VCARD 0x00000800
#define CB_5VCARD 0x00000400
#define I365_CS_READY 0x20
#define I365_VPP2_12V 0x08
#define I365_VPP1_5V 0x01
#define I365_VCC_3V 0x18
#define CB_SC_VCC_3V 0x00000030
#define I365_VCC_MASK 0x18
#define I365_POWER 0x02
#define CB_SC_VPP_5V 0x00000002
#define I365_VCC_5V 0x10
#define I365_VPP1_12V 0x02
#define I365_VPP1_MASK 0x03
#define I365_VPP2_5V 0x04
#define I365_VPP2_MASK 0x0c
#define YENTA_16BIT_POWER_EXCA 0x00000001
#define YENTA_16BIT_POWER_DF 0x00000002
#define CB_SC_VCC_5V 0x00000020
#define CB_SC_VPP_12V 0x00000001
#define CB_SOCKET_CONTROL 0x10
#define CB_SC_VPP_3V 0x00000003
#define I365_PC_RESET 0x40
#define CB_SOCKET_EVENT 0x00
#define I365_CSC 0x04
#define I365_RING_ENA 0x80
#define I365_CSC_STSCHG 0x01
#define CB_BRIDGE_CONTROL 0x3e
#define CB_SOCKET_MASK 0x04
#define I365_CSC_IRQ_MASK 0xF0
#define I365_PWR_NORESET 0x40
#define CB_BRIDGE_CRST 0x00000040
#define I365_CSC_BVD2 0x02
#define CB_CDMASK 0x00000006
#define I365_PWR_AUTO 0x20
#define CB_BRIDGE_INTR 0x00000080
#define I365_CSCINT 0x05
#define I365_INTR_ENA 0x10
#define I365_CSC_BVD1 0x01
#define I365_CSC_DETECT 0x08
#define I365_CSC_READY 0x04
#define I365_PWR_OUT 0x80
#define I365_IOCTL_0WS(map) (0x04 << ((map)<<2))
#define I365_W_START 0
#define I365_W_STOP 2
#define I365_IOCTL 0x07
#define I365_ADDRWIN 0x06
#define I365_ENA_IO(map) (0x40 << (map))
#define I365_IOCTL_MASK(map) (0x0F << ((map)<<2))
#define I365_IO(map) (0x08+((map)<<2))
#define I365_IOCTL_IOCS16(map) (0x02 << ((map)<<2))
#define I365_IOCTL_16BIT(map) (0x01 << ((map)<<2))
#define I365_MEM_REG 0x4000
#define I365_MEM(map) (0x10+((map)<<3))
#define I365_MEM_WRPROT 0x8000
#define I365_MEM_WS1 0x8000
#define I365_MEM_0WS 0x4000
#define I365_MEM_WS0 0x4000
#define CB_MEM_PAGE(map) (0x40 + (map))
#define I365_MEM_16BIT 0x8000
#define I365_W_OFF 4
#define I365_ENA_MEM(map) (0x01 << (map))
#define CB_CD1EVENT 0x00000002
#define CB_CD2EVENT 0x00000004
#define CB_NOTACARD 0x00000080
#define CB_BADVCCREQ 0x00000200
#define CB_SOCKET_FORCE 0x0C
#define CB_CVSTEST 0x00004000
#define I365_GENCTL 0x16
#define I365_GBLCTL 0x1E
#define CB_FCARDSTS 0x00000001
#define CB_CSTSMASK 0x00000001
#define CB_BRIDGE_PREFETCH0 0x00000100
#define CB_BRIDGE_VGAEN 0x00000008
#define CB_BRIDGE_POSTEN 0x00000400
#define CB_BRIDGE_PREFETCH1 0x00000200
#define CB_LEGACY_MODE_BASE 0x44
#define CB_BRIDGE_ISAEN 0x00000004
#define O2_RESERVED1 0x94
#define O2_RESERVED2 0xD4
#define O2_RES_WRITE_BURST 0x08
#define O2_RES_READ_PREFETCH 0x02
#define TI1250_DIAG_PCI_IREQ 0x40
#define TI113X_SYSTEM_CONTROL 0x0080
#define TI1250_DIAG_PCI_CSC 0x20
#define TI1250_DIAGNOSTIC 0x0093
#define TI113X_SCR_KEEPCLK 0x00000002
#define TI122X_SCR_MRBURSTUP 0x00004000
#define TI113X_CARD_CONTROL 0x0091
#define ene_test_c9(socket) ((socket)->private[5])
#define ti_diag(socket) ((socket)->private[3])
#define ENE_TEST_C9 0xc9
#define TI113X_DEVICE_CONTROL 0x0092
#define ti_mfunc(socket) ((socket)->private[4])
#define ti_sysctl(socket) ((socket)->private[0])
#define TI122X_MFUNC 0x008c
#define ti_cardctl(socket) ((socket)->private[1])
#define ti_devctl(socket) ((socket)->private[2])
#define TI113X_CCR_PCI_CSC 0x08
#define TI113X_CCR_PCI_IREQ 0x10
#define TI113X_CCR_PCI_IRQ_ENA 0x20
#define RL5C4XX_16BIT_CTL 0x0084
#define RL5C4XX_CONFIG 0x80
#define rl_mem(socket) ((socket)->private[3])
#define RL5C4XX_MISC 0x0082
#define rl_io(socket) ((socket)->private[2])
#define RL5C4XX_16BIT_MEM_0 0x008a
#define rl_ctl(socket) ((socket)->private[1])
#define RL5C4XX_16BIT_IO_0 0x0088
#define rl_misc(socket) ((socket)->private[0])
#define rl_config(socket) ((socket)->private[4])
#define TOPIC_EXCA_IFC_33V_ENA 0x01
#define TOPIC_PCI_CFG_PPBCN_WBEN 0x0400
#define TOPIC_PCI_CFG_PPBCN 0x3e
#define TOPIC_EXCA_IF_CONTROL 0x3e
#define RL5C46X_16CTL_LEVEL_2 0x0020
#define RL5C4XX_16CTL_IO_TIMING 0x0100
#define RL5C46X_16CTL_LEVEL_1 0x0010
#define RL5C4XX_CONFIG_PREFETCH 0x0001
#define RL5C4XX_16CTL_MEM_TIMING 0x0200
#define TOPIC97_AUDIO_VIDEO_SWITCH 0x003c
#define TOPIC97_AVS_VIDEO_CONTROL 0x01
#define TOPIC97_AVS_AUDIO_CONTROL 0x02
#define TOPIC97_ZOOM_VIDEO_CONTROL 0x009c
#define TOPIC97_ZV_CONTROL_ENABLE 0x01
#define TI113X_DCR_IMODE_MASK 0x06
#define TI122X_SCR_INTRTIE 0x20000000
#define TI122X_MFUNC0_MASK 0x0000000f
#define TI122X_MFUNC1_MASK 0x000000f0
#define TI12XX_DCR_IMODE_ALL_SERIAL 0x06
#define TI125X_MFUNC0_INTB 0x00000001
#define TI122X_MFUNC1_INTB 0x00000020
#define TI122X_MFUNC3_IRQSER 0x00001000
#define TI122X_MFUNC0_INTA 0x00000002
#define TI113X_DCR_IMODE_SERIAL 0x04
#define TI122X_MFUNC3_MASK 0x0000f000
#define TI1250_GPIO3_CONTROL 0x008b
#define TI1250_GPIO_MODE_MASK 0xc0
#define I365_IRQ_MASK 0x0F
#define RL5C47X_MISC3_CB_CLKRUN_DIS BIT(1)
#define RL5C4XX_MISC3 0x00A2
#define ENE_TEST_C9_TLTENABLE 0x02
#define TI1250_MULTIMEDIA_CTL 0x0084
#define TI1250_MMC_ZVOUTEN 0x80
#define TI113X_CCR_ZVENABLE 0x40
#define TIXX21_SCR_TIEALL 0x10000000
#define RL5C4XX_ZV_ENABLE 0x08
#define RL5C4XX_MISC_CONTROL 0x2F

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
    uint32_t intr_status;
    uint32_t intr_mask;

    /* CardBus Socket Registers */
    uint32_t socket_event;      /* CB_SOCKET_EVENT offset */
    uint32_t socket_mask;       /* CB_SOCKET_MASK offset */
    uint32_t socket_state;      /* CB_SOCKET_STATE offset */
    uint32_t socket_force;      /* CB_SOCKET_FORCE offset */
    uint32_t socket_control;    /* CB_SOCKET_CONTROL offset */

    /* ExCA Registers */
    uint8_t exca_regs[0x100];   /* 0x800..0x8ff */
};

/* Internal helper for status-triggered signaling */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    int level = (s->socket_event & s->socket_mask) ? 1 : 0;
    pci_set_irq(pdev, level);
}

/* Device-initiated DMA logic based on driver access patterns - not used */

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x800) {
        /* ExCA registers */
        unsigned reg = addr - 0x800;
        if (reg < sizeof(s->exca_regs)) {
            val = s->exca_regs[reg];
        }
    } else {
        /* CardBus registers */
        switch (addr) {
        case CB_SOCKET_EVENT:
            val = s->socket_event;
            break;
        case CB_SOCKET_MASK:
            val = s->socket_mask;
            break;
        case CB_SOCKET_STATE:
            val = s->socket_state;
            break;
        case CB_SOCKET_FORCE:
            val = s->socket_force;
            break;
        case CB_SOCKET_CONTROL:
            val = s->socket_control;
            break;
        default:
            val = 0;
            break;
        }
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x800) {
        /* ExCA registers */
        unsigned reg = addr - 0x800;
        if (reg < sizeof(s->exca_regs)) {
            s->exca_regs[reg] = val & 0xFF;
            /* If power control register I365_POWER is written, reflect in socket state? Not needed */
        }
    } else {
        switch (addr) {
        case CB_SOCKET_EVENT:
            s->socket_event &= ~val; /* write-1-to-clear */
            pcibase_update_irq(s);
            break;
        case CB_SOCKET_MASK:
            s->socket_mask = val;
            pcibase_update_irq(s);
            break;
        case CB_SOCKET_STATE:
            /* Read-only? Driver only reads, ignore writes */
            break;
        case CB_SOCKET_FORCE:
            s->socket_force = val;
            /* Force events: set corresponding bits in event register */
            s->socket_event |= val;
            pcibase_update_irq(s);
            break;
        case CB_SOCKET_CONTROL:
            s->socket_control = val;
            break;
        default:
            break;
        }
    }
}

/* PIO handlers not used, but keep for completeness */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    return val;
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
    memset(s->exca_regs, 0, sizeof(s->exca_regs));
    s->socket_event = 0;
    s->socket_mask = 0;
    s->socket_state = 0;
    s->socket_force = 0;
    s->socket_control = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    
    /* Set header type to CardBus bridge (type 2) so that the guest enumerates it as a bridge */
    pci_set_byte(pci_conf + PCI_HEADER_TYPE, PCI_HEADER_TYPE_CARDBUS);

    /* Enable bus mastering so the kernel's PCI subsystem will scan behind this bridge */
    pci_set_word(pci_conf + PCI_COMMAND, PCI_COMMAND_MASTER);

    pci_config_set_interrupt_pin(pci_conf, 1);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "yenta-mmio";
    pcibase_register_bar(pdev, s, &s->bar_info[0], errp);
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
    .name = "yenta_cardbus_pci",
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