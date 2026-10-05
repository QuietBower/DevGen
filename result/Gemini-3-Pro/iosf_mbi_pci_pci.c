/*
 * QEMU Intel IOSF Sideband Mailbox Interface (MBI) Device Model
 *
 * Based on Linux driver: arch/x86/platform/intel/iosf_mbi.c
 * Target QEMU: 8.2.10
 */

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/range.h"
#include "qom/object.h"

/* Driver defined constants */
#define MBI_REG_READ        0x10
#define MBI_REG_WRITE       0x11
#define BT_MBI_UNIT_PMC     0x04
#define MBI_ENABLE          0xF0
#define MBI_MASK_LO         0x000000FF
#define MBI_MASK_HI         0xFFFFFF00
#define MBI_RD_MASK         0xFEFFFFFF
#define MBI_WR_MASK         0x01000000

/* 
 * Corrected device name to match the test harness invocation 
 * which expects "iosf_mbi_pci_pci".
 */
#define TYPE_IOSF_MBI_DEVICE "iosf_mbi_pci_pci"
typedef struct IosfMbiState IosfMbiState;
OBJECT_DECLARE_SIMPLE_TYPE(IosfMbiState, IOSF_MBI_DEVICE)

/* PCI IDs */
#ifndef PCI_VENDOR_ID_INTEL
#define PCI_VENDOR_ID_INTEL             0x8086
#endif
#define PCI_DEVICE_ID_INTEL_BAYTRAIL    0x0F00

/* Register Offsets (PCI Config Space) */
#define MBI_MCRX_OFFSET     0xD8
#define MBI_MCR_OFFSET      0xD0
#define MBI_MDR_OFFSET      0xD4

/* Semaphore Constants */
#define PUNIT_SEMAPHORE_BYT     0x7
#define PUNIT_SEMAPHORE_BIT     0x1
#define PUNIT_SEMAPHORE_ACQUIRE 0x2

struct IosfMbiState {
    PCIDevice parent_obj;

    /* Simulation State */
    uint32_t sem_val; /* P-Unit Semaphore Value */
};

static void iosf_mbi_reset(DeviceState *dev)
{
    IosfMbiState *s = IOSF_MBI_DEVICE(dev);
    
    s->sem_val = 0;
}

static void iosf_mbi_handle_request(IosfMbiState *s, uint32_t mcr, uint32_t mcrx)
{
    /* Extract fields from MCR */
    /* Format: (op << 24) | (port << 16) | (offset << 8) | MBI_ENABLE */
    uint8_t op = (mcr >> 24) & 0xFF;
    uint8_t port = (mcr >> 16) & 0xFF;
    uint8_t offset = (mcr >> 8) & 0xFF;
    
    /* 
     * Handle P-Unit Semaphore Access 
     * Driver uses port BT_MBI_UNIT_PMC and offset PUNIT_SEMAPHORE_BYT (0x7) for Baytrail.
     */
    if (port == BT_MBI_UNIT_PMC && offset == PUNIT_SEMAPHORE_BYT) {
        if (op == MBI_REG_WRITE) {
            /* Write Logic */
            uint32_t val = pci_get_long(s->parent_obj.config + MBI_MDR_OFFSET);
            
            /* 
             * Driver logic: 
             * - Write PUNIT_SEMAPHORE_ACQUIRE (0x2) to request.
             * - Write 0 to release.
             */
            if (val & PUNIT_SEMAPHORE_ACQUIRE) {
                /* Grant semaphore immediately */
                s->sem_val = val | PUNIT_SEMAPHORE_BIT;
            } else {
                /* Release */
                s->sem_val = val & ~PUNIT_SEMAPHORE_BIT;
            }
            
            qemu_log_mask(LOG_GUEST_ERROR, "IOSF: Semaphore Write val=0x%x new=0x%x\n", val, s->sem_val);
            
        } else if (op == MBI_REG_READ) {
            /* Read Logic */
            /* Update MDR with current semaphore value */
            pci_set_long(s->parent_obj.config + MBI_MDR_OFFSET, s->sem_val);
            
            qemu_log_mask(LOG_GUEST_ERROR, "IOSF: Semaphore Read val=0x%x\n", s->sem_val);
        }
    } else {
        qemu_log_mask(LOG_UNIMP, "IOSF: Unimplemented MBI Access Port=0x%x Op=0x%x Offset=0x%x\n", 
                      port, op, offset);
    }
}

static void iosf_mbi_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    IosfMbiState *s = IOSF_MBI_DEVICE(pdev);

    pci_default_write_config(pdev, addr, val, len);

    /* Check if MCR (0xD0) was written to trigger a transaction */
    if (ranges_overlap(addr, len, MBI_MCR_OFFSET, 4)) {
        uint32_t mcr = pci_get_long(pdev->config + MBI_MCR_OFFSET);
        uint32_t mcrx = pci_get_long(pdev->config + MBI_MCRX_OFFSET);
        
        iosf_mbi_handle_request(s, mcr, mcrx);
    }
}

static void iosf_mbi_realize(PCIDevice *pdev, Error **errp)
{
    uint8_t *pci_conf = pdev->config;

    /* Set PCI IDs for Baytrail as default */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_INTEL);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_INTEL_BAYTRAIL);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_OTHERS);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    
    /* No BARs are used by this driver; it relies on Config Space MBI registers */
}

static void iosf_mbi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = iosf_mbi_realize;
    k->config_write = iosf_mbi_config_write;
    dc->reset = iosf_mbi_reset;
    dc->desc = "Intel IOSF Sideband Mailbox Interface";
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
}

static const TypeInfo iosf_mbi_info = {
    .name = TYPE_IOSF_MBI_DEVICE,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(IosfMbiState),
    .class_init = iosf_mbi_class_init,
    .interfaces = (InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void iosf_mbi_register_types(void)
{
    type_register_static(&iosf_mbi_info);
}

type_init(iosf_mbi_register_types)