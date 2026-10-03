/*
 * QEMU 8.2.10 virtual PCI device for nfp_netvf driver
 * Implemented from nfp_netvf_main.c behavior
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"

#define TYPE_PCIBASE_DEVICE "nfp_netvf_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor and Device IDs (first entry in pci_device_id table) */
#define PCI_VENDOR_ID_NETRONOME 0x19ee
#define PCI_DEVICE_ID_NFP3800_VF 0x3803
#define PCI_CLASS_NETWORK_ETHERNET 0x0200

/* Control BAR (BAR0) size and subregion sizing */
#define NFP_NET_CFG_BAR_SZ (32 * 1024)
#define NFP_NET_CTRL_REGION_SIZE 0x2000   /* Up to offset 0x1FFF for direct ctrl registers */
#define NFP_NET_MSIX_TABLE_OFFSET 0x2000
#define NFP_NET_MSIX_PBA_OFFSET 0x3000

/* Register offsets (derived from provided macros in driver) */
#define NFP_NET_CFG_CTRL          0x0000
#define NFP_NET_CFG_UPDATE        0x0004
#define NFP_NET_CFG_STS           0x0034
#define NFP_NET_CFG_VERSION       0x0030
#define NFP_NET_CFG_CAP           0x0038
#define NFP_NET_CFG_MAX_TXRINGS   0x003c
#define NFP_NET_CFG_MAX_RXRINGS   0x0040
#define NFP_NET_CFG_MAX_MTU       0x0044
#define NFP_NET_CFG_START_TXQ     0x0048
#define NFP_NET_CFG_START_RXQ     0x004c
#define NFP_NET_CFG_RX_OFFSET     0x0050
#define NFP_NET_CFG_RSS_CAP       0x0054
#define NFP_NET_CFG_TLV_BASE      0x0058
#define NFP_NET_CFG_CTRL_WORD1    0x0098
#define NFP_NET_CFG_CAP_WORD1     0x00a4
#define NFP_NET_CFG_RSS_BASE      0x0100
#define NFP_NET_CFG_ICR_BASE      0x0c00
#define NFP_NET_CFG_STATS_BASE    0x0d00
#define NFP_NET_CFG_MACADDR       0x0024

/* Interrupt constants */
#define NFP_NET_NON_Q_VECTORS     2
#define NFP_NET_MAX_TX_RINGS      64
#define NFP_NET_MAX_RX_RINGS      64
#define NFP_NET_MAX_R_VECS        ((NFP_NET_MAX_TX_RINGS > NFP_NET_MAX_RX_RINGS) ? NFP_NET_MAX_TX_RINGS : NFP_NET_MAX_RX_RINGS)
#define NFP_NET_MAX_IRQS          (NFP_NET_NON_Q_VECTORS + NFP_NET_MAX_R_VECS)

/* BAR indices */
#define NFP_NET_CTRL_BAR          0
#define NFP_NET_Q0_BAR            2
#define NFP_NET_Q1_BAR            4

/* Device state structure */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar0;              /* Container for ctrl and MSI-X */
    MemoryRegion ctrl_mem;          /* MMIO region for ctrl registers */
    MemoryRegion bar2;              /* TX queues BAR */
    MemoryRegion bar4;              /* RX queues BAR */

    /* Hardware Register Shadow for BAR0 ctrl area (bytes) */
    uint8_t ctrl_bar[NFP_NET_CTRL_REGION_SIZE];

    /* Interrupt status mask */
    uint32_t intr_status;
    uint32_t intr_mask;
};

/* MMIO read/write for ctrl registers (BAR0, offset 0x0000-0x1FFF) */
static uint64_t pcibase_ctrl_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size > NFP_NET_CTRL_REGION_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read out of bounds addr=0x%" HWADDR_PRIx " size=%u\n",
                      __func__, addr, size);
        return 0;
    }

    /* Generic read from shadow byte array (little-endian) */
    memcpy(&val, &s->ctrl_bar[addr], size);

    /* For registers that need side effects, handle here (none known yet) */
    return val;
}

static void pcibase_ctrl_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > NFP_NET_CTRL_REGION_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write out of bounds addr=0x%" HWADDR_PRIx " size=%u\n",
                      __func__, addr, size);
        return;
    }

    /* Generic write to shadow byte array */
    memcpy(&s->ctrl_bar[addr], &val, size);

    /* Side effects: if write touches NFP_NET_CFG_UPDATE, signal completion in STS */
    if (addr <= NFP_NET_CFG_UPDATE && (addr + size) > NFP_NET_CFG_UPDATE) {
        uint32_t done = 0xFFFFFFFF;  /* Indicate all updates completed */
        memcpy(&s->ctrl_bar[NFP_NET_CFG_STS], &done, sizeof(done));
    }
}

static const MemoryRegionOps pcibase_ctrl_mmio_ops = {
    .read = pcibase_ctrl_mmio_read,
    .write = pcibase_ctrl_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* MMIO read/write for queue BARs (BAR2, BAR4) - dummy implementation */
static uint64_t pcibase_queue_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_queue_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No-op: queue operations not modeled */
}

static const MemoryRegionOps pcibase_queue_mmio_ops = {
    .read = pcibase_queue_mmio_read,
    .write = pcibase_queue_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

/* Device reset */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Clear ctrl shadow registers */
    memset(s->ctrl_bar, 0, sizeof(s->ctrl_bar));

    /* Set initial values for registers that the driver expects */
    /* NFP_NET_CFG_VERSION (0x30): firmware version 4.0.0.0, NFD3 data path */
    s->ctrl_bar[0x30] = 0x00;  /* minor */
    s->ctrl_bar[0x31] = 0x04;  /* major */
    s->ctrl_bar[0x32] = 0x00;  /* class */
    s->ctrl_bar[0x33] = 0x00;  /* extend (NFD3) */

    /* NFP_NET_CFG_MAX_TXRINGS (0x3C): set to 1 */
    uint32_t val = 1;
    memcpy(&s->ctrl_bar[0x3C], &val, sizeof(val));
    /* NFP_NET_CFG_MAX_RXRINGS (0x40): set to 1 */
    memcpy(&s->ctrl_bar[0x40], &val, sizeof(val));
    /* NFP_NET_CFG_START_TXQ (0x48): set to 0 */
    val = 0;
    memcpy(&s->ctrl_bar[0x48], &val, sizeof(val));
    /* NFP_NET_CFG_START_RXQ (0x4C): set to 0 */
    memcpy(&s->ctrl_bar[0x4C], &val, sizeof(val));

    /* NFP_NET_CFG_MACADDR (0x24): assign a default local MAC */
    s->ctrl_bar[0x24] = 0x02;
    s->ctrl_bar[0x25] = 0x00;
    s->ctrl_bar[0x26] = 0x00;
    s->ctrl_bar[0x27] = 0x00;
    s->ctrl_bar[0x28] = 0x00;
    s->ctrl_bar[0x29] = 0x01;

    /* NFP_NET_CFG_CTRL (0x00): set to 0 (device disabled) */
    /* Others remain 0 */

    s->intr_status = 0;
    s->intr_mask = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_NETRONOME);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_NFP3800_VF);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NETWORK_ETHERNET);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* PCI Express capability */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR0: Control BAR with MSI-X subregions */
    memory_region_init(&s->bar0, OBJECT(s), "nfp_netvf_bar0", NFP_NET_CFG_BAR_SZ);
    memory_region_init_io(&s->ctrl_mem, OBJECT(s), &pcibase_ctrl_mmio_ops, s,
                          "nfp_netvf_ctrl", NFP_NET_CTRL_REGION_SIZE);
    memory_region_add_subregion(&s->bar0, 0, &s->ctrl_mem);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);

    /* BAR2: TX Queues (Q0) - size depends on NFP_QCP_QUEUE_ADDR_SZ, stride, max_tx_rings.
     * For now, set a large enough size (e.g., 0x10000). */
    memory_region_init_io(&s->bar2, OBJECT(s), &pcibase_queue_mmio_ops, s,
                          "nfp_netvf_q0", 0x10000);
    pci_register_bar(pdev, NFP_NET_Q0_BAR, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar2);

    /* BAR4: RX Queues (Q1) - similar */
    memory_region_init_io(&s->bar4, OBJECT(s), &pcibase_queue_mmio_ops, s,
                          "nfp_netvf_q1", 0x10000);
    pci_register_bar(pdev, NFP_NET_Q1_BAR, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar4);

    /* MSI-X initialization using BAR0 container region */
    if (msix_init(pdev, NFP_NET_MAX_IRQS, &s->bar0, 0, NFP_NET_MSIX_TABLE_OFFSET,
                  &s->bar0, 0, NFP_NET_MSIX_PBA_OFFSET, 0, errp)) {
        error_setg(errp, "Failed to initialize MSI-X");
        return;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, &s->bar0, &s->bar0);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = TYPE_PCIBASE_DEVICE,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT8_ARRAY(ctrl_bar, PCIBaseState, NFP_NET_CTRL_REGION_SIZE),
        VMSTATE_UINT32(intr_status, PCIBaseState),
        VMSTATE_UINT32(intr_mask, PCIBaseState),
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
