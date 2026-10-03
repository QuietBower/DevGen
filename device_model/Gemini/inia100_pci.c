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

#define TYPE_PCIBASE_DEVICE "inia100_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define PCI_VENDOR_ID_INIT		0x1101
#define HOSTSTOP		0x02
#define ORC_HCTRL	0xA5
#define ORC_HSTUS	0xA6
#define RREADY		0x01
#define SCSIRST		0x80
#define HDO			0x40
#define HDI			0x02
#define ORC_CMD_VERSION		0x01
#define ORC_HDATA	0xA4
#define ORC_CMD_SET_NVM		0x03
#define ORC_CMD_GET_NVM		0x04
#define ORCSCB_POST	0x01
#define ORC_PQUEUE	0xA8
#define ORC_FWBASEADR	0xAC
#define ORC_EBIOSADR0 0xB0
#define EEPRG		0x01
#define ORC_EBIOSDATA 0xB3
#define ORC_RISCCTL	0xE0
#define ORC_EBIOSADR2 0xB2
#define DOWNLOAD		0x001
#define ORC_RISCRAM	0xEC
#define ORC_GCFG	0xA2
#define PRGMRST		0x002
#define ORC_SCBSIZE	0xB7
#define ORC_SCBBASE1	0xBC
#define ORC_MAXQUEUE		245
#define ORC_SCBBASE0	0xB8
#define MAX_CHANNELS       2
#define HCF_SCSI_RESET	0x01
#define ORC_MAXTAGS		64
#define MAX_TARGETS		16
#define DEVRST		0x01
#define NCC_BUSRESET    0x01
#define ORC_GIMSK	0xA1
#define ORC_BUSDEVRST	0x01
#define ORC_CMD_ABORT_SCB	0x06
#define ORC_RQUEUE	0xAA
#define ORC_RQUEUECNT	0xAB
#define TOTAL_SG_ENTRY		32
#define IMAX_CDB			15
#define SENSE_SIZE		14
#define DISC_ALLOW              0xC0
#define SCF_NO_DCHK	0x00
#define ORC_EXECSCSI	0x00

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
    uint8_t regs[256];

    /* DMA Context */
    uint32_t scb_base0;
    uint32_t scb_base1;

    uint8_t hstus;
    uint8_t hctrl;

    uint8_t hdata_reg;
    uint8_t hdata_cmd;
    uint8_t hdata_state;
    uint8_t hdata_addr;
    uint8_t hdata_val;

    uint32_t ebios_addr;
    uint32_t riscctl;
    uint32_t riscram[1024];
    uint16_t riscram_idx;
    
    uint8_t gcfg;
    uint8_t gimsk;
    uint8_t scbsize;

    uint8_t nvram[64];

    uint8_t rqueue[256];
    uint8_t rqueue_cnt;
    uint8_t rqueue_head;
    uint8_t rqueue_tail;
};

struct orc_sgent {
    uint32_t base;
    uint32_t length;
};

struct orc_scb {
    uint8_t opcode;
    uint8_t flags;
    uint8_t target;
    uint8_t lun;
    uint32_t reserved0;
    uint32_t xferlen;
    uint32_t reserved1;
    uint32_t sg_len;
    uint32_t sg_addr;
    uint32_t sg_addrhigh;
    uint8_t hastat;
    uint8_t tastat;
    uint8_t status;
    uint8_t link;
    uint8_t sense_len;
    uint8_t cdb_len;
    uint8_t ident;
    uint8_t tag_msg;
    uint8_t cdb[IMAX_CDB];
    uint8_t scbidx;
    uint32_t sense_addr;
};

struct orc_extended_scb {
    struct orc_sgent sglist[TOTAL_SG_ENTRY];
    uint64_t srb;
};

struct orc_nvram {
    uint8_t SubVendorID0;
    uint8_t SubVendorID1;
    uint8_t SubSysID0;
    uint8_t SubSysID1;
    uint8_t SubClass;
    uint8_t VendorID0;
    uint8_t VendorID1;
    uint8_t DeviceID0;
    uint8_t DeviceID1;
    uint8_t Reserved0[2];
    uint8_t revision;
    uint8_t NumOfCh;
    uint8_t BIOSConfig1;
    uint8_t BIOSConfig2;
    uint8_t BIOSConfig3;
    uint8_t scsi_id;
    uint8_t SCSI0Config;
    uint8_t SCSI0MaxTags;
    uint8_t SCSI0ResetTime;
    uint8_t ReservedforChannel0[2];
    uint8_t Target00Config;
    uint8_t Target01Config;
    uint8_t Target02Config;
    uint8_t Target03Config;
    uint8_t Target04Config;
    uint8_t Target05Config;
    uint8_t Target06Config;
    uint8_t Target07Config;
    uint8_t Target08Config;
    uint8_t Target09Config;
    uint8_t Target0AConfig;
    uint8_t Target0BConfig;
    uint8_t Target0CConfig;
    uint8_t Target0DConfig;
    uint8_t Target0EConfig;
    uint8_t Target0FConfig;
    uint8_t SCSI1Id;
    uint8_t SCSI1Config;
    uint8_t SCSI1MaxTags;
    uint8_t SCSI1ResetTime;
    uint8_t ReservedforChannel1[2];
    uint8_t Target10Config;
    uint8_t Target11Config;
    uint8_t Target12Config;
    uint8_t Target13Config;
    uint8_t Target14Config;
    uint8_t Target15Config;
    uint8_t Target16Config;
    uint8_t Target17Config;
    uint8_t Target18Config;
    uint8_t Target19Config;
    uint8_t Target1AConfig;
    uint8_t Target1BConfig;
    uint8_t Target1CConfig;
    uint8_t Target1DConfig;
    uint8_t Target1EConfig;
    uint8_t Target1FConfig;
    uint8_t reserved[3];
    uint8_t CheckSum;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool enabled = !(s->gimsk & 0x04);
    if (s->rqueue_cnt > 0 && enabled) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case 0x50:
        if (size == 2) val = 0x0000;
        break;
    case ORC_HCTRL:
        val = s->hctrl;
        break;
    case ORC_HSTUS:
        val = s->hstus;
        break;
    case ORC_HDATA:
        val = s->hdata_val;
        break;
    case ORC_RQUEUE:
        if (s->rqueue_cnt > 0) {
            val = s->rqueue[s->rqueue_head++];
            s->rqueue_cnt--;
            pcibase_update_irq(s);
        }
        break;
    case ORC_RQUEUECNT:
        val = s->rqueue_cnt;
        break;
    case ORC_EBIOSDATA:
        if (s->ebios_addr == 0x0000) val = 0x55;
        else if (s->ebios_addr == 0x0001) val = 0xAA;
        else val = 0x00;
        break;
    case ORC_GCFG:
        val = s->gcfg;
        break;
    case ORC_RISCRAM:
        if (size == 4 && s->riscram_idx < 1024) {
            val = s->riscram[s->riscram_idx++];
        }
        break;
    default:
        val = 0;
        break;
    }
    return val;
}

static void pcibase_reset(DeviceState *dev);

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);
    
    switch (addr) {
    case ORC_HCTRL:
        if (val & DEVRST) {
            pcibase_reset(DEVICE(s));
            val |= HOSTSTOP;
            val &= ~DEVRST;
        }
        if (val & HDO) {
            if (s->hdata_state == 0) {
                s->hdata_cmd = s->hdata_reg;
                if (s->hdata_cmd == ORC_CMD_VERSION) {
                    s->hdata_val = 0x10;
                    s->hstus |= HDI;
                    s->hdata_state = 3;
                } else if (s->hdata_cmd == ORC_CMD_GET_NVM || s->hdata_cmd == ORC_CMD_SET_NVM || s->hdata_cmd == ORC_CMD_ABORT_SCB) {
                    s->hstus |= HDI;
                    s->hdata_state = 1;
                }
            } else if (s->hdata_state == 1) {
                s->hdata_addr = s->hdata_reg;
                if (s->hdata_cmd == ORC_CMD_GET_NVM) {
                    s->hdata_val = s->nvram[s->hdata_addr % 64];
                    s->hstus |= HDI;
                    s->hdata_state = 0;
                } else if (s->hdata_cmd == ORC_CMD_SET_NVM) {
                    s->hstus |= HDI;
                    s->hdata_state = 2;
                } else if (s->hdata_cmd == ORC_CMD_ABORT_SCB) {
                    s->hdata_val = 0;
                    s->hstus |= HDI;
                    s->hdata_state = 0;
                }
            } else if (s->hdata_state == 2) {
                if (s->hdata_cmd == ORC_CMD_SET_NVM) {
                    s->nvram[s->hdata_addr % 64] = s->hdata_reg;
                    s->hstus |= HDI;
                    s->hdata_state = 0;
                }
            }
            val &= ~HDO;
        }
        if (val & SCSIRST) {
            val &= ~SCSIRST;
        }
        s->hctrl = val;
        break;
    case ORC_HSTUS:
        if (val & HDI) {
            s->hstus &= ~HDI;
            if (s->hdata_cmd == ORC_CMD_VERSION && s->hdata_state == 3) {
                s->hdata_val = 0x00;
                s->hstus |= HDI;
                s->hdata_state = 0;
            }
        }
        break;
    case ORC_HDATA:
        s->hdata_reg = val;
        break;
    case ORC_PQUEUE:
        if (size == 1) {
            uint8_t scbidx = val;
            struct orc_scb scb;
            hwaddr scb_addr = s->scb_base0 + scbidx * sizeof(struct orc_scb);
            pci_dma_read(pdev, scb_addr, &scb, sizeof(scb));
            
            scb.hastat = 0x11; /* Selection timeout */
            scb.status = 0x0;
            
            pci_dma_write(pdev, scb_addr, &scb, sizeof(scb));
            
            if (s->rqueue_cnt < 255) {
                s->rqueue[s->rqueue_tail++] = scbidx;
                s->rqueue_cnt++;
            }
            pcibase_update_irq(s);
        }
        break;
    case ORC_EBIOSADR0:
        if (size == 2) s->ebios_addr = (s->ebios_addr & 0xFF0000) | val;
        break;
    case ORC_EBIOSADR2:
        if (size == 1) s->ebios_addr = (s->ebios_addr & 0x00FFFF) | (val << 16);
        break;
    case ORC_RISCCTL:
        s->riscctl = val;
        if (val & PRGMRST) {
            s->riscram_idx = 0;
        }
        break;
    case ORC_RISCRAM:
        if (size == 4 && s->riscram_idx < 1024) {
            s->riscram[s->riscram_idx++] = val;
        }
        break;
    case ORC_GCFG:
        s->gcfg = val;
        break;
    case ORC_SCBSIZE:
        s->scbsize = val;
        break;
    case ORC_SCBBASE0:
        if (size == 4) s->scb_base0 = val;
        break;
    case ORC_SCBBASE1:
        if (size == 4) s->scb_base1 = val;
        break;
    case ORC_GIMSK:
        s->gimsk = val;
        pcibase_update_irq(s);
        break;
    case ORC_FWBASEADR:
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

    s->hctrl = 0;
    s->hstus = RREADY;
    s->hdata_reg = 0;
    s->hdata_cmd = 0;
    s->hdata_state = 0;
    s->hdata_addr = 0;
    s->hdata_val = 0;
    s->ebios_addr = 0;
    s->riscctl = 0;
    s->riscram_idx = 0;
    memset(s->riscram, 0, sizeof(s->riscram));
    s->gcfg = 0;
    s->gimsk = 0xFF;
    s->scbsize = 0;
    s->scb_base0 = 0;
    s->scb_base1 = 0;
    s->rqueue_cnt = 0;
    s->rqueue_head = 0;
    s->rqueue_tail = 0;

    memset(s->nvram, 0, sizeof(s->nvram));
    s->nvram[11] = 1; /* revision */
    s->nvram[16] = 7; /* scsi_id */
    uint8_t chksum = 0;
    for (int i = 0; i < 63; i++) {
        chksum += s->nvram[i];
    }
    s->nvram[63] = chksum;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_INIT );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x1060 );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_SCSI );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 256;
    s->bar_info[0].name = "inia100-io";
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
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

static const VMStateDescription vmstate_pcibase = {
    .name = "inia100_pci",
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
