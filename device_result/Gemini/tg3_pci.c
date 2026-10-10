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


#define TYPE_PCIBASE_DEVICE "tg3_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x14e4
#define DEVICE_ID 0x1644
#define CLASS_ID PCI_CLASS_NETWORK_ETHERNET

#define MAILBOX_INTERRUPT_0		0x00000200
#define MAILBOX_RCVRET_CON_IDX_0	0x00000280
#define MAILBOX_SNDHOST_PROD_IDX_0	0x00000300
#define TG3_64BIT_REG_LOW		0x04UL
#define HOSTCC_MODE			0x00003c00
#define WDMAC_MODE			0x00004c00
#define MEMARB_MODE			0x00004000
#define TG3PCI_PCISTATE			0x00000070
#define TG3_RX_TSTAMP_LSB		0x000006b0
#define TG3_RX_TSTAMP_MSB		0x000006b4

#define SD_STATUS_UPDATED		0x00000001
#define PCISTATE_INT_NOT_ACTIVE	 0x00000002
#define PCISTATE_BUS_SPEED_HIGH	 0x00000008
#define HOSTCC_MODE_ENABLE		 0x00000002
#define WDMAC_MODE_ENABLE		 0x00000002
#define MEMARB_MODE_ENABLE		 0x00000002
#define HOSTCC_MODE_COAL_VEC1_NOW	 0x00002000
#define HOSTCC_MODE_NOW		 0x00000008

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

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t regs[0x8000 / 4]; /* TG3_REG_BLK_SIZE is 0x8000 */

    /* DMA Context */
    dma_addr_t rx_std_mapping;
    dma_addr_t rx_jmb_mapping;
    dma_addr_t tx_desc_mapping;
    dma_addr_t status_mapping;
    dma_addr_t stats_mapping;

    uint32_t mac_status;
    uint32_t hw_status;
    uint32_t reset_state;
    uint32_t pm_state;
    
};

struct tg3_rx_buffer_desc {
    uint32_t addr_hi;
    uint32_t addr_lo;
    uint32_t idx_len;
    uint32_t type_flags;
    uint32_t ip_tcp_csum;
    uint32_t err_vlan;
    uint32_t reserved;
    uint32_t opaque;
};

struct tg3_tx_buffer_desc {
    uint32_t addr_hi;
    uint32_t addr_lo;
    uint32_t len_flags;
    uint32_t vlan_tag;
};

struct tg3_hw_status {
    uint32_t status;
    uint32_t status_tag;
    uint16_t rx_jumbo_consumer;
    uint16_t rx_consumer;
    uint16_t rx_mini_consumer;
    uint16_t reserved;
    struct {
        uint16_t rx_producer;
        uint16_t tx_consumer;
    } idx[16];
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (s->intr_status) {
        s->regs[0x70 / 4] &= ~0x00000002;
        if (s->has_msix && msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else if (s->has_msi && msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        s->regs[0x70 / 4] |= 0x00000002;
        if (!s->has_msix || !msix_enabled(pdev)) {
            if (!s->has_msi || !msi_enabled(pdev)) {
                pci_set_irq(pdev, 0);
            }
        }
    }
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    if (is_write) {
        if (s->status_mapping) {
            struct tg3_hw_status hw_status = {0};
            pci_dma_read(pdev, s->status_mapping, &hw_status, sizeof(hw_status));
            
            uint32_t tx_prod = s->regs[0x304 / 4];
            uint32_t tx_cons = hw_status.idx[0].tx_consumer;
            
            if (tx_prod != tx_cons && s->tx_desc_mapping && s->rx_std_mapping) {
                struct tg3_tx_buffer_desc tx_desc;
                uint32_t tx_idx = tx_cons & 511;
                pci_dma_read(pdev, s->tx_desc_mapping + tx_idx * sizeof(tx_desc), &tx_desc, sizeof(tx_desc));
                
                uint64_t tx_addr = ((uint64_t)tx_desc.addr_hi << 32) | tx_desc.addr_lo;
                uint32_t tx_len = (tx_desc.len_flags >> 16) & 0xffff;
                
                if (tx_len > 0 && tx_len <= 10000) {
                    uint8_t *buf = g_malloc(tx_len);
                    pci_dma_read(pdev, tx_addr, buf, tx_len);
                    
                    uint32_t rx_cons = hw_status.idx[0].rx_producer;
                    uint32_t rx_prod = s->regs[0x26c / 4];
                    if (rx_cons != rx_prod) {
                        struct tg3_rx_buffer_desc rx_desc;
                        uint32_t std_idx = rx_cons & 511;
                        pci_dma_read(pdev, s->rx_std_mapping + std_idx * sizeof(rx_desc), &rx_desc, sizeof(rx_desc));
                        
                        uint64_t rx_addr = ((uint64_t)rx_desc.addr_hi << 32) | rx_desc.addr_lo;
                        pci_dma_write(pdev, rx_addr, buf, tx_len);
                        
                        uint64_t rx_ret_mapping = ((uint64_t)s->regs[0x4100 / 4] << 32) | s->regs[0x4104 / 4];
                        if (rx_ret_mapping) {
                            struct tg3_rx_buffer_desc ret_desc = {0};
                            ret_desc.idx_len = (tx_len + 4) << 16;
                            ret_desc.opaque = std_idx;
                            pci_dma_write(pdev, rx_ret_mapping + std_idx * sizeof(ret_desc), &ret_desc, sizeof(ret_desc));
                        }
                        hw_status.idx[0].rx_producer += 1;
                    }
                    g_free(buf);
                }
                
                hw_status.idx[0].tx_consumer = tx_prod;
            }
            
            hw_status.status_tag += 1;
            hw_status.status |= SD_STATUS_UPDATED;
            pci_dma_write(pdev, s->status_mapping, &hw_status, sizeof(hw_status));
            s->intr_status = 1;
            pcibase_update_irq(s);
        }
    }
}

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    if (addr < 0x8000) {
        val = s->regs[addr / 4];
    }

    if (addr >= 0x5c00 && addr <= 0x5cff) {
        return s->regs[0x7ffc / 4];
    }

    switch (addr) {
        case 0x84:
            if (s->regs[0x80 / 4] < 0x8000) {
                val = s->regs[s->regs[0x80 / 4] / 4];
            } else {
                val = s->regs[0x84 / 4];
            }
            break;
        case 0x404:
            val |= 0x03800107;
            break;
        case 0x44c:
            val &= ~0x20000000;
            {
                uint32_t reg = (val >> 16) & 0x1f;
                val &= ~0xffff;
                if (reg == 2) val |= 0x0143;
                else if (reg == 3) val |= 0xbc70;
                else if (reg == 1) val |= 0x782d;
                else if (reg == 0) val |= 0x1000;
            }
            break;
        case 0x70:
            if (s->intr_status) val &= ~0x00000002;
            else val |= 0x00000002;
            break;
        case 0x7000:
        case 0x7020:
        case 0x6838:
        case 0x5100:
            val = 0xffffffff;
            break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr < 0x8000) {
        s->regs[addr / 4] = val;
    }

    if (addr >= 0x5c00 && addr <= 0x5cff) {
        s->regs[0x7ffc / 4] = val;
    }

    switch (addr) {
        case 0x84:
            if (s->regs[0x80 / 4] < 0x8000) {
                if (val == 0x4B657654) {
                    s->regs[s->regs[0x80 / 4] / 4] = ~0x4B657654;
                } else {
                    s->regs[s->regs[0x80 / 4] / 4] = val;
                }
            }
            if (s->regs[0x80 / 4] == 0x4300) {
                s->tx_desc_mapping = ((uint64_t)val << 32) | (s->tx_desc_mapping & 0xffffffff);
            } else if (s->regs[0x80 / 4] == 0x4304) {
                s->tx_desc_mapping = (s->tx_desc_mapping & 0xffffffff00000000ULL) | val;
            }
            break;
        case 0x200:
        case 0x204:
            if (val == 1) {
                s->intr_status = 0;
                pcibase_update_irq(s);
            }
            break;
        case 0x3c38:
            s->status_mapping = ((uint64_t)val << 32) | (s->status_mapping & 0xffffffff);
            break;
        case 0x3c3c:
            s->status_mapping = (s->status_mapping & 0xffffffff00000000ULL) | val;
            break;
        case 0x2450:
            s->rx_std_mapping = ((uint64_t)val << 32) | (s->rx_std_mapping & 0xffffffff);
            break;
        case 0x2454:
            s->rx_std_mapping = (s->rx_std_mapping & 0xffffffff00000000ULL) | val;
            break;
        case 0x2460:
            s->rx_jmb_mapping = ((uint64_t)val << 32) | (s->rx_jmb_mapping & 0xffffffff);
            break;
        case 0x2464:
            s->rx_jmb_mapping = (s->rx_jmb_mapping & 0xffffffff00000000ULL) | val;
            break;
        case 0x6800:
            if (val & 0x80000000) {
                s->intr_status = 1;
                pcibase_update_irq(s);
            }
            break;
        case 0x300:
        case 0x304:
            pcibase_do_dma(s, true);
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

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->regs, 0, sizeof(s->regs));
    s->regs[0x68 / 4] = 0x40000000; /* TG3PCI_MISC_HOST_CTRL */
    s->regs[0x70 / 4] = 0x00000002; /* TG3PCI_PCISTATE: PCISTATE_INT_NOT_ACTIVE */
    s->intr_status = 0;
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID );
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
    s->bar_info[0].size = 0x8000;
    s->bar_info[0].name = "tg3-bar0";  
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    s->has_msi = true;
    msi_init(pdev, 0, 1, true, false, errp);  
      
      
      
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
    .name = "tg3_pci",
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
