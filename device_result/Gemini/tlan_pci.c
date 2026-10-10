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

#ifndef HZ
#define HZ 1000
#endif

/* Additional include files retrieved from driver context */

#ifndef PCI_VENDOR_ID_COMPAQ
#define PCI_VENDOR_ID_COMPAQ		0x0e11
#endif
#ifndef PCI_DEVICE_ID_COMPAQ_NETEL10
#define PCI_DEVICE_ID_COMPAQ_NETEL10	0xae34
#endif

#define TYPE_PCIBASE_DEVICE "tlan_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define TLAN_HOST_CMD          0x00
#define TLAN_CH_PARM           0x04
#define TLAN_DIO_ADR           0x08
#define TLAN_HOST_INT          0x0A
#define TLAN_DIO_DATA          0x0C
#define TLAN_AREG_0            0x10
#define TLAN_NET_CMD           0x00
#define TLAN_NET_STS           0x02
#define TLAN_NET_CONFIG        0x04
#define TLAN_NET_SIO           0x01
#define TLAN_LED_REG           0x44
#define TLAN_HASH_1            0x28
#define TLAN_HASH_2            0x2C
#define TLAN_GOOD_TX_FRMS      0x30
#define TLAN_GOOD_RX_FRMS      0x34
#define TLAN_DEFERRED_TX       0x38
#define TLAN_MULTICOL_FRMS     0x3C
#define TLAN_EXCESSCOL_FRMS    0x40
#define TLAN_ACOMMIT           0x43
#define TLAN_INT_DIS           0x48
#define TLAN_TLPHY_CTL         0x11
#define TLAN_TLPHY_STS         0x12
#define TLAN_TLPHY_PAR         0x19

#define TLAN_BUFFERS_PER_LIST  10

#define TLAN_HC_GO		0x80000000
#define TLAN_HC_AD_RST		0x00008000
#define TLAN_HC_INT_OFF		0x00000800
#define TLAN_HC_INT_ON		0x00000400
#define TLAN_HC_ACK		0x20000000
#define TLAN_HC_RT		0x00080000
#define TLAN_HC_LD_TMR		0x00004000
#define TLAN_HC_LD_THR		0x00002000
#define TLAN_HC_REQ_INT		0x00001000
#define TLAN_HI_IT_MASK		0x001C
#define TLAN_HI_IV_MASK		0x1FE0
#define TLAN_DEF_REVISION		0x0C
#define TLAN_NET_CMD_CAF	0x10
#define TLAN_NET_CMD_NRESET	0x80
#define TLAN_NET_CMD_NWRAP	0x40
#define TLAN_NET_CMD_DUPLEX	0x04
#define TLAN_NET_MASK			0x03
#define TLAN_NET_MASK_MASK4	0x10
#define TLAN_NET_MASK_MASK5	0x20
#define TLAN_NET_MASK_MASK7	0x80
#define TLAN_MAX_RX			0x46
#define TLAN_NET_CFG_1FRAG	0x0400
#define TLAN_NET_CFG_1CHAN	0x0200
#define TLAN_NET_CFG_PHY_EN	0x0080
#define TLAN_NET_CFG_BIT	0x2000
#define TLAN_NET_SIO_NMRST	0x08
#define TLAN_NET_SIO_MINTEN	0x80
#define TLAN_NET_SIO_MTXEN	0x02
#define TLAN_NET_SIO_MCLK	0x04
#define TLAN_NET_SIO_MDATA	0x01
#define TLAN_NET_SIO_ECLOK	0x40
#define TLAN_NET_SIO_EDATA	0x10
#define TLAN_NET_SIO_ETXEN	0x20
#define TLAN_ID_TX_EOC		0x04
#define TLAN_ID_RX_EOC		0x01
#define TLAN_LED_LINK		0x01
#define TLAN_LED_ACT		0x10
#define TLAN_CSTAT_UNUSED	0x8000
#define TLAN_CSTAT_FRM_CMP	0x4000
#define TLAN_CSTAT_READY	0x3000
#define TLAN_CSTAT_EOC		0x0800
#define TLAN_LAST_BUFFER	0x80000000
#define TLAN_MIN_FRAME_SIZE	64
#define TLAN_MAX_FRAME_SIZE	1600
#define TLAN_NUM_RX_LISTS	32
#define TLAN_NUM_TX_LISTS	64
#define TLAN_EEPROM_ACK		0
#define TLAN_EEPROM_STOP	1
#define TLAN_EEPROM_SIZE	256
#define TLAN_TS_POLOK		0x2000
#define TLAN_TC_SWAPOL		0x4000
#define TLAN_TC_INTEN		0x0002
#define TLAN_TC_AUISEL		0x2000
#define TLAN_PHY_MAX_ADDR	0x1F
#define TLAN_PHY_NONE		0x20
#define TLAN_PHY_AN_EN_STAT     0x0400
#define TLAN_PHY_SPEED_100	0x0040
#define TLAN_PHY_DUPLEX_FULL	0x0080
#define MII_GEN_ID_HI			0x02
#define MII_GEN_ID_LO			0x03
#define MII_GEN_STS			0x01
#define MII_GEN_CTL			0x00
#define MII_AN_ADV			0x04
#define MII_AN_LPA			0x05
#define MII_GS_LINK		0x0004
#define MII_GS_AUTONEG		0x0008
#define MII_GS_AUTOCMPLT	0x0020
#define MII_GC_PDOWN		0x0800
#define MII_GC_LOOPBK		0x4000
#define MII_GC_ISOLATE		0x0400
#define MII_GC_RESET		0x8000
#define MII_GC_DUPLEX		0x0100
#define MII_GC_SPEEDSEL		0x2000
#define MII_GC_AUTOENB		0x1000
#define NAT_SEM_ID1			0x2000
#define NAT_SEM_ID2			0x5C01
#define TLAN_TIMER_ACT_DELAY		(HZ/10)
#define TLAN_TIMER_ACTIVITY		2
#define TLAN_TIMER_PHY_PDOWN		3
#define TLAN_TIMER_PHY_PUP		4
#define TLAN_TIMER_PHY_RESET		5
#define TLAN_TIMER_PHY_START_LINK	6
#define TLAN_TIMER_PHY_FINISH_AN	7
#define TLAN_TIMER_FINISH_RESET		8
#define TLAN_IGNORE		0
#define TLAN_RECORD		1
#define TLAN_SPEED_10		10
#define TLAN_SPEED_100		100
#define TLAN_DUPLEX_HALF	1
#define TLAN_DUPLEX_FULL	2
#define TLAN_ADAPTER_ACTIVITY_LED	0x00000008
#define TLAN_ADAPTER_BIT_RATE_PHY	0x00000002
#define TLAN_ADAPTER_USE_INTERN_10	0x00000004
#define TLAN_ADAPTER_UNMANAGED_PHY	0x00000001
#define TX_TIMEOUT		(10*HZ)
#define EISA_ID      0xc80
#define EISA_ID2     0xc82
#define EISA_CR      0xc84

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
    uint16_t host_int;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t host_cmd;
    uint32_t ch_parm;
    uint16_t dio_adr;
    uint32_t dio_data;

    /* DMA Context */
    dma_addr_t rx_list_dma;
    dma_addr_t tx_list_dma;

    uint8_t dio_regs[256];
};

struct tlan_buffer {
    uint32_t count;
    uint32_t address;
};

struct tlan_list {
    uint32_t forward;
    uint16_t c_stat;
    uint16_t frame_size;
    struct tlan_buffer buffer[TLAN_BUFFERS_PER_LIST];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->host_int) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* DMA logic will be implemented once macros are available */
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case TLAN_HOST_CMD:
        val = s->host_cmd;
        break;
    case TLAN_CH_PARM:
        val = s->ch_parm;
        break;
    case TLAN_DIO_ADR:
        val = s->dio_adr;
        break;
    case TLAN_HOST_INT:
        val = s->host_int;
        break;
    case TLAN_DIO_DATA:
    case TLAN_DIO_DATA + 1:
    case TLAN_DIO_DATA + 2:
    case TLAN_DIO_DATA + 3:
        {
            uint16_t reg_addr = (s->dio_adr & ~3) + (addr - TLAN_DIO_DATA);
            if (reg_addr < sizeof(s->dio_regs)) {
                if (size == 1) val = s->dio_regs[reg_addr];
                else if (size == 2) val = lduw_le_p(&s->dio_regs[reg_addr]);
                else if (size == 4) val = ldl_le_p(&s->dio_regs[reg_addr]);
            }
        }
        break;
    }
    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    
    switch (addr) {
    case TLAN_HOST_CMD:
        s->host_cmd = val;
        break;
    case TLAN_CH_PARM:
        s->ch_parm = val;
        break;
    case TLAN_DIO_ADR:
        s->dio_adr = val;
        break;
    case TLAN_HOST_INT:
        s->host_int &= ~val;
        pcibase_update_irq(s);
        break;
    case TLAN_DIO_DATA:
    case TLAN_DIO_DATA + 1:
    case TLAN_DIO_DATA + 2:
    case TLAN_DIO_DATA + 3:
        {
            uint16_t reg_addr = (s->dio_adr & ~3) + (addr - TLAN_DIO_DATA);
            if (reg_addr < sizeof(s->dio_regs)) {
                if (size == 1) s->dio_regs[reg_addr] = val;
                else if (size == 2) stw_le_p(&s->dio_regs[reg_addr], val);
                else if (size == 4) stl_le_p(&s->dio_regs[reg_addr], val);
            }
        }
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

    s->host_cmd = 0;
    s->ch_parm = 0;
    s->dio_adr = 0;
    s->host_int = 0;
    memset(s->dio_regs, 0, sizeof(s->dio_regs));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_COMPAQ );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_COMPAQ_NETEL10 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET );
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
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 0x10;
    s->bar_info[0].name = "tlan-pio";
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

      /* msi_init or msix_init calls */
       /* Set DMA masks or ring buffer limits */
     /* Initialize internal hardware timers if used */
       /* Final state initialization before the device is 'live' */
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
    .name = "tlan_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(host_cmd, PCIBaseState),
        VMSTATE_UINT32(ch_parm, PCIBaseState),
        VMSTATE_UINT16(dio_adr, PCIBaseState),
        VMSTATE_UINT16(host_int, PCIBaseState),
        VMSTATE_UINT8_ARRAY(dio_regs, PCIBaseState, 256),
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
