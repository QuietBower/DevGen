/*
 * This template provides a robust skeleton for hardware emulation.
 * Integrated QEMU PCI device template (QEMU 8.2.10).
 * Replace #PLACEHOLDER# blocks with driver-specific definitions.
 * Designed for register-level modeling and PCIe driver probing.
 * NOTE: Do not directly fill this file with the Linux kernel source code.
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


#define TYPE_PCIBASE_DEVICE "yenta_cardbus_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_TI		0x104c
#define PCI_DEVICE_ID_TI_1031		0xac13
#define PCI_CLASS_BRIDGE_CARDBUS	0x0607

#define VENDOR_ID PCI_VENDOR_ID_TI
#define DEVICE_ID PCI_DEVICE_ID_TI_1031
#define CLASS_ID PCI_CLASS_BRIDGE_CARDBUS

#define CB_SOCKET_EVENT 0x00
#define CB_SOCKET_MASK 0x04
#define CB_SOCKET_STATE 0x08
#define CB_SOCKET_FORCE 0x0C
#define CB_SOCKET_CONTROL 0x10
#define CB_BRIDGE_CONTROL 0x3e
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
#define CB_SC_VPP_3V 0x00000003
#define I365_PC_RESET 0x40
#define I365_CSC 0x04
#define I365_RING_ENA 0x80
#define I365_CSC_STSCHG 0x01
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
#define I365_W_START 0
#define I365_W_STOP 2
#define I365_IOCTL 0x07
#define I365_ADDRWIN 0x06
#define I365_MEM_REG 0x4000
#define I365_MEM_WRPROT 0x8000
#define I365_MEM_WS1 0x8000
#define I365_MEM_0WS 0x4000
#define I365_MEM_WS0 0x4000
#define I365_MEM_16BIT 0x8000
#define I365_W_OFF 4
#define CB_CD1EVENT 0x00000002
#define CB_CD2EVENT 0x00000004
#define CB_NOTACARD 0x00000080
#define CB_BADVCCREQ 0x00000200
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
#define ENE_TEST_C9 0xc9
#define TI113X_DEVICE_CONTROL 0x0092
#define TI122X_MFUNC 0x008c
#define TI113X_CCR_PCI_CSC 0x08
#define TI113X_CCR_PCI_IREQ 0x10
#define TI113X_CCR_PCI_IRQ_ENA 0x20
#define RL5C4XX_16BIT_CTL 0x0084
#define RL5C4XX_CONFIG 0x80
#define RL5C4XX_MISC 0x0082
#define RL5C4XX_16BIT_MEM_0 0x008a
#define RL5C4XX_16BIT_IO_0 0x0088
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
    uint32_t cb_irq;
    uint32_t io_irq;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint8_t mmio_data[0x1000];

    uint32_t cb_socket_event;
    uint32_t cb_socket_mask;
    uint32_t cb_socket_state;
    uint32_t cb_socket_force;
    uint32_t cb_socket_control;
    uint8_t exca[0x100];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool level = false;

    if (s->cb_socket_event & s->cb_socket_mask) {
        level = true;
    }
    if (s->exca[I365_CSC] & (s->exca[I365_CSCINT] & 0x0F)) {
        level = true;
    }

    pci_set_irq(pdev, level);
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < 0x800) {
        switch (addr) {
            case CB_SOCKET_EVENT: val = s->cb_socket_event; break;
            case CB_SOCKET_MASK: val = s->cb_socket_mask; break;
            case CB_SOCKET_STATE: val = s->cb_socket_state; break;
            case CB_SOCKET_FORCE: val = s->cb_socket_force; break;
            case CB_SOCKET_CONTROL: val = s->cb_socket_control; break;
            default: val = 0; break;
        }
    } else if (addr >= 0x800 && addr < 0x900) {
        uint32_t idx = addr - 0x800;
        val = s->exca[idx];
        if (idx == I365_CSC) {
            s->exca[I365_CSC] = 0; /* Clear on read */
            pcibase_update_irq(s);
        }
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x800) {
        switch (addr) {
            case CB_SOCKET_EVENT:
                s->cb_socket_event &= ~val; /* W1C */
                pcibase_update_irq(s);
                break;
            case CB_SOCKET_MASK:
                s->cb_socket_mask = val;
                pcibase_update_irq(s);
                break;
            case CB_SOCKET_FORCE:
                s->cb_socket_force = val;
                if (val & CB_FCARDSTS) {
                    s->cb_socket_event |= CB_CD1EVENT | CB_CD2EVENT;
                    s->exca[I365_CSC] |= I365_CSC_DETECT | I365_CSC_READY;
                }
                if (val & CB_CVSTEST) {
                    s->cb_socket_event |= CB_CD1EVENT | CB_CD2EVENT;
                }
                pcibase_update_irq(s);
                break;
            case CB_SOCKET_CONTROL:
                s->cb_socket_control = val;
                break;
        }
    } else if (addr >= 0x800 && addr < 0x900) {
        uint32_t idx = addr - 0x800;
        s->exca[idx] = val;
        if (idx == I365_CSCINT) {
            pcibase_update_irq(s);
        }
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* PIO not utilized by driver */
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* PIO not utilized by driver */
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

    s->cb_socket_event = 0;
    s->cb_socket_mask = 0;
    /* Simulate a CardBus card inserted and ready */
    s->cb_socket_state = CB_CBCARD | CB_3VCARD;
    s->cb_socket_force = 0;
    s->cb_socket_control = 0;
    memset(s->exca, 0, sizeof(s->exca));
    
    /* ExCA default status: card detect, ready, power on */
    s->exca[I365_STATUS] = I365_CS_DETECT | I365_CS_READY | I365_CS_POWERON;
    
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* Set Header Type to CardBus Bridge */
    pci_conf[PCI_HEADER_TYPE] = PCI_HEADER_TYPE_CARDBUS;

    /* Make CardBus bridge bus numbers writable */
    pdev->wmask[0x18] = 0xff; /* Primary Bus */
    pdev->wmask[0x19] = 0xff; /* Secondary Bus */
    pdev->wmask[0x1a] = 0xff; /* Subordinate Bus */
    pdev->wmask[0x1b] = 0xff; /* Secondary Latency Timer */

    /* Make CardBus memory/IO windows writable */
    pci_set_long(pdev->wmask + 0x1c, 0xffffffff);
    pci_set_long(pdev->wmask + 0x20, 0xffffffff);
    pci_set_long(pdev->wmask + 0x24, 0xffffffff);
    pci_set_long(pdev->wmask + 0x28, 0xffffffff);
    pci_set_long(pdev->wmask + 0x2c, 0xffffffff);
    pci_set_long(pdev->wmask + 0x30, 0xffffffff);
    pci_set_long(pdev->wmask + 0x34, 0xffffffff);
    pci_set_long(pdev->wmask + 0x38, 0xffffffff);
    
    /* Bridge control */
    pci_set_word(pdev->wmask + 0x3e, 0xffff);

    /* Legacy Mode Base Address */
    pci_set_long(pdev->wmask + 0x44, 0xffffffff);

    /* Subsystem IDs for CardBus (offset 0x40) */
    pci_set_word(pci_conf + 0x40, VENDOR_ID);
    pci_set_word(pci_conf + 0x42, DEVICE_ID);

    /* TI specific registers (0x80 - 0x93) writable */
    pci_set_long(pdev->wmask + 0x80, 0xffffffff);
    pci_set_long(pdev->wmask + 0x84, 0xffffffff);
    pci_set_long(pdev->wmask + 0x88, 0xffffffff);
    pci_set_long(pdev->wmask + 0x8c, 0xffffffff);
    pci_set_long(pdev->wmask + 0x90, 0xffffffff);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    /* Move PCIe cap to 0xA0 to avoid conflict with TI registers at 0x80 */
    pcie_endpoint_cap_init(pdev, 0xA0);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){0, BAR_TYPE_MMIO, 0x1000, "yenta_regs"};
  
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
