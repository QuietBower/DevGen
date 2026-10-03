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
#include "qemu/error-report.h"

#define TYPE_PCIBASE_DEVICE "mt76x0e_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_MEDIATEK 0x14c3
#define PCI_CLASS_NETWORK_OTHER 0x0280

#define MT_ASIC_VERSION 0x0000
#define MT_CMB_CTRL 0x0020
#define MT_EFUSE_CTRL 0x0024
#define MT_EFUSE_DATA_BASE 0x0028
#define MT_COEXCFG0 0x0040
#define MT_COEXCFG3 0x004c
#define MT_WLAN_FUN_CTRL 0x0080
#define MT_XO_CTRL7 0x011c
#define MT_INT_MASK_CSR 0x0204
#define MT_WPDMA_GLO_CFG 0x0208
#define MT_WPDMA_RST_IDX 0x020c
#define MT_WMM_AIFSN 0x0214
#define MT_WMM_CWMIN 0x0218
#define MT_WMM_CWMAX 0x021c
#define MT_WMM_TXOP_BASE 0x0220
#define MT_WMM_CTRL 0x0230
#define MT_TX_RING_BASE 0x0300
#define MT_RX_RING_BASE 0x03c0
#define MT_BCN_OFFSET_BASE 0x041c
#define MT_RXQ_STA 0x0430
#define MT_RF_CSR_CFG 0x0500
#define MT_RF_BYPASS_0 0x0504
#define MT_RF_SETTING_0 0x050c
#define MT_RF_MISC 0x0518
#define MT_MCU_RESET_CTL 0x070C
#define MT_MCU_INT_LEVEL 0x0718
#define MT_MCU_COM_REG0 0x0730
#define MT_MCU_PCIE_REMAP_BASE4 0x074C
#define MT_LED_S0_BASE 0x077C
#define MT_LED_S1_BASE 0x0780
#define MT_LED_CTRL 0x0770
#define MT_MCU_SEMAPHORE_00 0x07B0
#define MT_FCE_L2_STUFF 0x080c
#define MT_BCN_BYPASS_MASK 0x108c
#define MT_MAC_APC_BSSID_BASE 0x1090
#define MT_MAC_SYS_CTRL 0x1004
#define MT_MAC_ADDR_DW0 0x1008
#define MT_MAC_ADDR_DW1 0x100c
#define MT_MAC_BSSID_DW0 0x1010
#define MT_MAC_BSSID_DW1 0x1014
#define MT_MAX_LEN_CFG 0x1018
#define MT_WCID_DROP_BASE 0x106c
#define MT_XIFS_TIME_CFG 0x1100
#define MT_BKOFF_SLOT_CFG 0x1104
#define MT_CH_TIME_CFG 0x110c
#define MT_CH_IDLE 0x1130
#define MT_CH_BUSY 0x1134
#define MT_ED_CCA_TIMER 0x1140
#define MT_BEACON_TIME_CFG 0x1114
#define MT_INT_TIMER_CFG 0x1128
#define MT_INT_TIMER_EN 0x112c
#define MT_MAC_STATUS 0x1200
#define MT_EDCA_CFG_BASE 0x1300
#define MT_TX_PWR_CFG_0 0x1314
#define MT_TX_PWR_CFG_1 0x1318
#define MT_TX_PWR_CFG_2 0x131c
#define MT_TX_PWR_CFG_3 0x1320
#define MT_TX_PWR_CFG_4 0x1324
#define MT_TX_PIN_CFG 0x1328
#define MT_TX_BAND_CFG 0x132c
#define MT_TX_SW_CFG0 0x1330
#define MT_TXOP_CTRL_CFG 0x1340
#define MT_TX_RTS_CFG 0x1344
#define MT_TX_TIMEOUT_CFG 0x1348
#define MT_TX_LINK_CFG 0x1350
#define MT_CCK_PROT_CFG 0x1364
#define MT_OFDM_PROT_CFG 0x1368
#define MT_TX0_RF_GAIN_CORR 0x13a0
#define MT_TX0_RF_GAIN_ATTEN 0x13a8
#define MT_TX_ALC_CFG_0 0x13b0
#define MT_TX_ALC_CFG_1 0x13b4
#define MT_TX_ALC_VGA3 0x13c8
#define MT_TX_PWR_CFG_7 0x13d4
#define MT_TX_PWR_CFG_8 0x13d8
#define MT_TX_PWR_CFG_9 0x13dc
#define MT_TX_PROT_CFG6 0x13e0
#define MT_RX_FILTR_CFG 0x1400
#define MT_AUTO_RSP_CFG 0x1404
#define MT_EXT_CCA_CFG 0x141c
#define MT_TXOP_HLDR_ET 0x1608
#define MT_RX_STAT_0 0x1700
#define MT_RX_STAT_1 0x1704
#define MT_RX_STAT_2 0x1708
#define MT_TX_STA_0 0x170c
#define MT_TX_STA_1 0x1710
#define MT_TX_STA_2 0x1714
#define MT_TX_STAT_FIFO 0x1718
#define MT_TX_STAT_FIFO_EXT 0x1798
#define MT_WCID_ADDR_BASE 0x1800
#define MT_WCID_KEY_BASE 0x8000
#define MT_WCID_IV_BASE 0xa000
#define MT_WCID_ATTR_BASE 0xa800
#define MT_SKEY_MODE_BASE_0 0xb000
#define MT_SKEY_MODE_BASE_1 0xb3f0
#define MT_SKEY_BASE_0 0xac00
#define MT_SKEY_BASE_1 0xb400

#define BAR0_SIZE (1 * MiB)
#define EEPROM_SIZE 512

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t *mmio;

    /* DMA Context */
    dma_addr_t tx_ring_addr;
    dma_addr_t rx_ring_addr;
    uint32_t tx_ring_size;
    uint32_t rx_ring_size;

    uint32_t status;
    bool in_reset;
    uint8_t pm_state;

    /* EEPROM emulation */
    uint8_t eeprom[EEPROM_SIZE];
    uint32_t efuse_ctrl;

    /* MAC reset active flag (for CSR0 polling) */
    bool mac_reset_active;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t status = s->mmio[0x0200/4] & s->mmio[0x0204/4];
    if (status) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size > BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds read at 0x%" HWADDR_PRIx " size %u\n",
                      __func__, addr, size);
        return 0;
    }

    /* Special register handling */
    switch (addr) {
    case MT_EFUSE_DATA_BASE:
        {
            uint32_t addr_in_eeprom = s->efuse_ctrl & 0xFF;
            if (addr_in_eeprom + 4 <= EEPROM_SIZE) {
                val = s->eeprom[addr_in_eeprom] |
                      (s->eeprom[addr_in_eeprom+1] << 8) |
                      (s->eeprom[addr_in_eeprom+2] << 16) |
                      (s->eeprom[addr_in_eeprom+3] << 24);
            } else {
                val = 0;
            }
        }
        return val;
    case 0x0200: /* INT_STATUS */
        return s->mmio[0x0200/4];
    case 0x0204: /* INT_MASK */
        return s->mmio[0x0204/4];
    case 0x1000: /* MAC_CSR0 */
        if (s->mac_reset_active) {
            return 0;
        }
        break;
    default:
        break;
    }

    /* Generic read from shadow array */
    uint8_t *mmio_bytes = (uint8_t *)s->mmio;
    for (int i = 0; i < size; i++) {
        val |= (uint64_t)mmio_bytes[addr + i] << (i * 8);
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > BAR0_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: out-of-bounds write at 0x%" HWADDR_PRIx " size %u\n",
                      __func__, addr, size);
        return;
    }

    /* Handle special registers first */
    switch (addr) {
    case MT_EFUSE_CTRL:
        s->efuse_ctrl = val & 0xFF;
        break;
    case 0x0200: /* INT_STATUS (W1C) */
        {
            uint32_t *p = &s->mmio[0x0200/4];
            for (int i = 0; i < size; i++) {
                uint8_t byte_val = (val >> (i*8)) & 0xFF;
                if (byte_val) {
                    *p &= ~(byte_val << (i*8));
                }
            }
        }
        pcibase_update_irq(s);
        return;
    case 0x0204: /* INT_MASK */
        {
            uint32_t *p = &s->mmio[0x0204/4];
            for (int i = 0; i < size; i++) {
                uint8_t byte_val = (val >> (i*8)) & 0xFF;
                *p = (*p & ~(0xFF << (i*8))) | (byte_val << (i*8));
            }
        }
        pcibase_update_irq(s);
        return;
    case MT_EFUSE_DATA_BASE:
        /* Ignore writes to EFUSE data */
        return;
    case MT_MAC_SYS_CTRL:
        {
            uint32_t *p = &s->mmio[addr/4];
            uint32_t new_val = 0;
            for (int i = 0; i < size; i++) {
                uint8_t byte_val = (val >> (i*8)) & 0xFF;
                new_val = (new_val & ~(0xFF << (i*8))) | (byte_val << (i*8));
            }
            /* merge or replace depending on size? For simplicity, treat as full write */
            *p = new_val;

            /* Bits 0 and 1 correspond to RESET_CSR and RESET_BBP */
            if (new_val & 0x3) {
                s->mac_reset_active = true;
            } else {
                s->mac_reset_active = false;
            }
        }
        return;
    case MT_WLAN_FUN_CTRL:
        {
            uint32_t *p = &s->mmio[addr/4];
            uint32_t new_val = 0;
            for (int i = 0; i < size; i++) {
                uint8_t byte_val = (val >> (i*8)) & 0xFF;
                new_val = (new_val & ~(0xFF << (i*8))) | (byte_val << (i*8));
            }
            *p = new_val;

            /* If WLAN is enabled, set CMB_CTRL ready bits immediately.
             * The driver expects MT_CMB_CTRL_XTAL_RDY and MT_CMB_CTRL_PLL_LD
             * to become set.
             */
            if (new_val & 0x1) { /* MT_WLAN_FUN_CTRL_WLAN_EN is BIT(0) */
                uint32_t cmb = s->mmio[MT_CMB_CTRL/4];
                cmb |= (1 << 22) | (1 << 23); /* BIT(22): XTAL_RDY, BIT(23): PLL_LD */
                s->mmio[MT_CMB_CTRL/4] = cmb;
            }
        }
        return;
    case MT_MCU_RESET_CTL:
        {
            uint32_t *p = &s->mmio[addr/4];
            uint32_t new_val = 0;
            for (int i = 0; i < size; i++) {
                uint8_t byte_val = (val >> (i*8)) & 0xFF;
                new_val = (new_val & ~(0xFF << (i*8))) | (byte_val << (i*8));
            }
            *p = new_val;

            /* For 7610, writing 0x300 triggers firmware start.
             * Set MCU_COM_REG0 bit 0 to indicate success.
             */
            if (new_val == 0x300) {
                s->mmio[MT_MCU_COM_REG0/4] = 0x1;
            }
        }
        return;
    case MT_MCU_INT_LEVEL:
        {
            uint32_t *p = &s->mmio[addr/4];
            uint32_t new_val = 0;
            for (int i = 0; i < size; i++) {
                uint8_t byte_val = (val >> (i*8)) & 0xFF;
                new_val = (new_val & ~(0xFF << (i*8))) | (byte_val << (i*8));
            }
            *p = new_val;

            /* For combo chips, writing 0x3 triggers firmware start. */
            if (new_val == 0x3) {
                s->mmio[MT_MCU_COM_REG0/4] = 0x1;
            }
        }
        return;
    default:
        break;
    }

    /* Generic write to shadow array */
    uint8_t *mmio_bytes = (uint8_t *)s->mmio;
    for (int i = 0; i < size; i++) {
        mmio_bytes[addr + i] = (val >> (i * 8)) & 0xFF;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->mmio, 0, BAR0_SIZE);
    /* Set default register values */
    s->mmio[MT_ASIC_VERSION / 4] = cpu_to_le32(0x76100200);
    s->mmio[0x1000 / 4] = cpu_to_le32(0x0000000a);  /* MAC_CSR0 ready */
    s->efuse_ctrl = 0;
    memset(s->eeprom, 0, EEPROM_SIZE);
    /* Initialize MCU semaphore to 1 (available) */
    s->mmio[MT_MCU_SEMAPHORE_00 / 4] = cpu_to_le32(0x1);
    /* Clear COM_REG0 */
    s->mmio[MT_MCU_COM_REG0 / 4] = 0;
    /* Reset flags */
    s->mac_reset_active = false;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_MEDIATEK);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x7610);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_OTHER);
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
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = BAR0_SIZE;
    s->bar_info[0].name = "mt76x0e-mmio";
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* Allocate MMIO shadow memory */
    s->mmio = g_new0(uint32_t, BAR0_SIZE / 4);

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        error_report("MSI init failed");
        return;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
    g_free(s->mmio);
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "mt76x0e_pci",
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
