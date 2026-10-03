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

#define TYPE_PCIBASE_DEVICE "thunder_bgx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define BCAST_ACCEPT      (1UL << 0)
#define CAM_ACCEPT        (1UL << 3)
#define MCAST_MODE_MASK   0x3
#define BGX_MCAST_MODE(x) ((x) << 1)
#define PCI_SUBSYS_DEVID_88XX_BGX 0xA126
#define PCI_SUBSYS_DEVID_83XX_BGX 0xA326
#define PCI_SUBSYS_DEVID_81XX_RGX 0xA254
#define MAX_BGX_PER_CN81XX 3
#define PCI_SUBSYS_DEVID_81XX_BGX 0xA226
#define MAX_BGX_PER_CN88XX 2
#define MAX_BGX_PER_CN83XX 4
#define BGX_CMR_RX_DMACX_CAM 0x200
#define RX_DMACX_CAM_LMACID(x) (((uint64_t)(x)) << 49)
#define RX_DMACX_CAM_EN (1ULL << 48)
#define LMAC_ID_MASK 0x3
#define BGX_CMRX_RX_DMAC_CTL 0x0E8
#define BGX_XCAST_MCAST_ACCEPT (1UL << 1)
#define BGX_XCAST_MCAST_FILTER (1UL << 2)
#define BGX_XCAST_BCAST_ACCEPT (1UL << 0)
#define BGX_CMRX_CFG 0x00
#define BGX_GMP_GMI_TXX_INT_ENA_W1C 0x38510
#define CMR_PKT_RX_EN (1ULL << 14)
#define CMR_PKT_TX_EN (1ULL << 13)
#define GMI_TXX_INT_UNDFLW (1ULL << 0)
#define BGX_GMP_GMI_TXX_INT_ENA_W1S 0x38518
#define BGX_PKT_RX_PTP_EN (1ULL << 12)
#define BGX_SMUX_RX_FRM_CTL 0x20020
#define BGX_GMP_GMI_RXX_FRM_CTL 0x38028
#define TX_EN (1ULL << 1)
#define RX_EN (1ULL << 0)
#define BGX_SMUX_CBFC_CTL 0x20218
#define GMI_PORT_CFG_SPEED_MSB (1ULL << 8)
#define BGX_GMP_GMI_TXX_BURST 0x38228
#define PCS_MISC_CTL_SAMP_PT_MASK 0x7Full
#define BGX_GMP_PCS_MISCX_CTL 0x30078
#define GMI_PORT_CFG_RX_IDLE (1ULL << 12)
#define BGX_GMP_GMI_PRTX_CFG 0x38020
#define GMI_PORT_CFG_DUPLEX (1ULL << 2)
#define PCS_MISC_CTL_GMX_ENO (1ULL << 11)
#define GMI_PORT_CFG_TX_IDLE (1ULL << 13)
#define GMI_PORT_CFG_SLOT_TIME (1ULL << 3)
#define GMI_PORT_CFG_SPEED (1ULL << 1)
#define BGX_GMP_GMI_TXX_SLOT 0x38220
#define BGX_CMRX_RX_STAT0 0x70
#define BGX_CMRX_TX_STAT0 0x600
#define SPU_CTL_LOOPBACK (1ULL << 14)
#define BGX_SPUX_CONTROL1 0x10000
#define PCS_MRX_CTL_LOOPBACK1 (1ULL << 14)
#define BGX_GMP_PCS_MRX_CTL 0x30000
#define PCS_MRX_CTL_AN_EN (1ULL << 12)
#define BGX_GMP_PCS_MRX_STATUS 0x30008
#define PCS_MRX_CTL_PWR_DN (1ULL << 11)
#define BGX_GMP_GMI_TXX_APPEND 0x38218
#define PCS_MRX_CTL_RST_AN (1ULL << 9)
#define PCS_MRX_STATUS_AN_CPT (1ULL << 5)
#define BGX_GMP_GMI_TXX_SGMII_CTL 0x38300
#define BGX_GMP_GMI_RXX_JABBER 0x38038
#define PCS_MRX_CTL_RESET (1ULL << 15)
#define MAX_FRAME_SIZE 9216
#define BGX_GMP_GMI_TXX_THRESH 0x38210
#define CMR_EN (1ULL << 15)
#define PCS_MISC_CTL_DISP_EN (1ULL << 13)
#define BGX_SPUX_FEC_CONTROL 0x100A0
#define BGX_SMUX_RX_JABBER 0x20030
#define BGX_SMUX_TX_INT 0x20140
#define BGX_SPUX_AN_CONTROL 0x100C8
#define SPU_DBG_CTL_AN_ARB_LINK_CHK_EN (1ULL << 18)
#define BGX_SMUX_TX_PAUSE_ZERO 0x20138
#define BGX_SMUX_RX_INT 0x20000
#define BCK_EN (1ULL << 2)
#define DEFAULT_PAUSE_TIME 0xFFFF
#define BGX_SPUX_BR_PMD_CRTL 0x10068
#define BGX_SPUX_MISC_CONTROL 0x10218
#define SPU_MISC_CTL_INTLV_RDISP (1ULL << 10)
#define SPU_FEC_CTL_FEC_EN (1ULL << 0)
#define SMU_TX_CTL_DIC_EN (1ULL << 0)
#define BGX_SPUX_AN_ADV 0x100D8
#define BGX_SMUX_TX_APPEND 0x20100
#define DRP_EN (1ULL << 3)
#define BGX_SMUX_TX_PAUSE_PKT_INTERVAL 0x20120
#define BGX_SMUX_TX_THRESH 0x20180
#define SPU_AN_CTL_XNP_EN (1ULL << 13)
#define SPU_CTL_LOW_POWER (1ULL << 11)
#define SPU_AN_CTL_AN_EN (1ULL << 12)
#define SMU_TX_APPEND_FCS_D (1ULL << 2)
#define BGX_SPUX_INT 0x10220
#define SPU_CTL_RESET (1ULL << 15)
#define SPU_PMD_CRTL_TRAIN_EN (1ULL << 1)
#define BGX_SMUX_TX_CTL 0x20178
#define SPU_MISC_CTL_RX_DIS (1ULL << 12)
#define BGX_SMUX_TX_PAUSE_PKT_TIME 0x20110
#define SMU_TX_CTL_UNI_EN (1ULL << 1)
#define BGX_SPUX_BR_PMD_LD_CUP 0x10088
#define BGX_SPUX_BR_PMD_LD_REP 0x10090
#define BGX_SPU_DBG_CONTROL 0x10300
#define BGX_SPUX_BR_PMD_LP_CUP 0x10078
#define BGX_SPUX_BX_STATUS 0x10028
#define BGX_SMUX_RX_CTL 0x20048
#define SMU_CTL_TX_IDLE (1ULL << 1)
#define BGX_SMUX_CTL 0x20200
#define BGX_SPUX_BR_STATUS1 0x10030
#define SPU_BX_STATUS_RX_ALIGN (1ULL << 12)
#define SMU_RX_CTL_STATUS (3ULL << 0)
#define SMU_CTL_RX_IDLE (1ULL << 0)
#define SPU_BR_STATUS_BLK_LOCK (1ULL << 0)
#define BGX_SPUX_STATUS2 0x10020
#define SPU_STATUS2_RCVFLT (1ULL << 10)
#define BGX_GMP_PCS_ANX_AN_RESULTS 0x30020
#define PCS_MRX_STATUS_LINK (1ULL << 2)
#define SPU_STATUS1_RCV_LNK (1ULL << 2)
#define BGX_SPUX_STATUS1 0x10008
#define BGX_GMP_GMI_TXX_MIN_PKT 0x38240
#define BGX_GMP_PCS_LINKX_TIMER 0x30040
#define BGX_SMUX_TX_MIN_PKT 0x20118
#define PCS_LINKX_TIMER_COUNT 0x1E84
#define RX_DMAC_COUNT 32
#define BGX_CMRX_TX_FIFO_LEN 0x518
#define BGX_CMRX_RX_FIFO_LEN 0x108
#define BGX_CMR_RX_STEERING 0x300
#define MAX_BGX_CHANS_PER_LMAC 16
#define BGX_CMR_TX_LMACS 0x1000
#define BGX_CMR_RX_LMACS 0x468
#define BGX_CMR_BIST_STATUS 0x460
#define BGX_CMR_GLOBAL_CFG 0x08
#define CMR_GLOBAL_CFG_FCS_STRIP (1ULL << 6)
#define RX_TRAFFIC_STEER_RULE_COUNT 8
#define BGX_CMR_CHAN_MSK_AND 0x450
#define BGX_GMP_GMI_TXX_INT 0x38500
#define BGX_LMAC_VEC_OFFSET 7
#define GMPX_GMI_TX_INT 6
#define BGX_ID_MASK 0x3
#define MAX_LMAC_PER_BGX 4
#define PCI_DEVICE_ID_THUNDER_RGX 0xA054
#define PCI_CFG_REG_BAR_NUM 0
#define MAX_BGX_THUNDER 8
#define PCI_DEVICE_ID_THUNDER_BGX 0xA026
#define NIC_NODE_ID_MASK 0x03
#define NIC_NODE_ID_SHIFT 44

#define PCI_VENDOR_ID_CAVIUM 0x177d

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
    uint64_t regs[0x400000 / 8];
};

enum MCAST_MODE {
    MCAST_MODE_REJECT = 0x0,
    MCAST_MODE_ACCEPT = 0x1,
    MCAST_MODE_CAM_FILTER = 0x2,
    RSVD = 0x3
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool raise = false;
    
    for (int lmac = 0; lmac < 4; lmac++) {
        uint64_t status = s->regs[((lmac << 20) + BGX_GMP_GMI_TXX_INT) / 8];
        uint64_t ena = s->regs[((lmac << 20) + BGX_GMP_GMI_TXX_INT_ENA_W1S) / 8];
        if (status & ena) {
            raise = true;
            break;
        }
    }
    
    if (raise) {
        if (s->has_msi) {
            msi_notify(pdev, GMPX_GMI_TX_INT);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!s->has_msi) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr < sizeof(s->regs)) {
        val = s->regs[addr / 8];
        
        uint64_t offset = addr & 0xFFFFF;
        
        switch (offset) {
            case BGX_GMP_PCS_MRX_CTL:
                val &= ~PCS_MRX_CTL_RESET;
                break;
            case BGX_GMP_PCS_MRX_STATUS:
                val |= PCS_MRX_STATUS_AN_CPT | PCS_MRX_STATUS_LINK;
                break;
            case BGX_SPUX_CONTROL1:
                val &= ~SPU_CTL_RESET;
                break;
            case BGX_SPUX_BR_STATUS1:
                val |= SPU_BR_STATUS_BLK_LOCK;
                break;
            case BGX_SPUX_BX_STATUS:
                val |= SPU_BX_STATUS_RX_ALIGN;
                break;
            case BGX_SMUX_CTL:
                val |= SMU_CTL_RX_IDLE | SMU_CTL_TX_IDLE;
                break;
            case BGX_GMP_GMI_PRTX_CFG:
                val |= GMI_PORT_CFG_RX_IDLE | GMI_PORT_CFG_TX_IDLE;
                break;
            case BGX_CMRX_RX_FIFO_LEN:
            case BGX_CMRX_TX_FIFO_LEN:
                val = 0;
                break;
            case BGX_SPUX_STATUS1:
                val |= SPU_STATUS1_RCV_LNK;
                break;
            case BGX_SMUX_RX_CTL:
                val &= ~SMU_RX_CTL_STATUS;
                break;
            case BGX_GMP_PCS_ANX_AN_RESULTS:
                val = (2 << 3) | (1 << 1);
                break;
            case BGX_CMR_BIST_STATUS:
                val = 0;
                break;
            case BGX_GMP_GMI_TXX_INT_ENA_W1C:
                val = 0;
                break;
        }
    }
    
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < sizeof(s->regs)) {
        uint32_t lmac = addr >> 20;
        uint64_t offset = addr & 0xFFFFF;
        
        if (offset == BGX_GMP_GMI_TXX_INT_ENA_W1C) {
            s->regs[((lmac << 20) + BGX_GMP_GMI_TXX_INT_ENA_W1S) / 8] &= ~val;
            pcibase_update_irq(s);
            return;
        }
        if (offset == BGX_GMP_GMI_TXX_INT_ENA_W1S) {
            s->regs[addr / 8] |= val;
            pcibase_update_irq(s);
            return;
        }
        if (offset == BGX_GMP_GMI_TXX_INT ||
            offset == BGX_SMUX_RX_INT ||
            offset == BGX_SMUX_TX_INT ||
            offset == BGX_SPUX_INT ||
            offset == BGX_SPUX_STATUS2) {
            s->regs[addr / 8] &= ~val;
            if (offset == BGX_GMP_GMI_TXX_INT) {
                pcibase_update_irq(s);
            }
            return;
        }
        
        s->regs[addr / 8] = val;
    }
}

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

    memset(s->regs, 0, sizeof(s->regs));
    
    s->regs[BGX_CMR_RX_LMACS / 8] = 4;
    
    for (int lmac = 0; lmac < 4; lmac++) {
        s->regs[((lmac << 20) + BGX_CMRX_CFG) / 8] = (0 << 8) | lmac;
    }
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_CAVIUM );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  PCI_DEVICE_ID_THUNDER_BGX );
    pci_set_word(pci_conf + PCI_SUBSYSTEM_ID, PCI_SUBSYS_DEVID_88XX_BGX);
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
    s->bar_info[0].index = PCI_CFG_REG_BAR_NUM;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x400000;
    s->bar_info[0].name = "bgx-regs";  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msi_init(pdev, 0, 8, true, false, errp) == 0) {
        s->has_msi = true;
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
    .name = "thunder_bgx_pci",
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
