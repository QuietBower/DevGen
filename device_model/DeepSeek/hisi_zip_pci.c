/*
 * QEMU HiSilicon ZIP PCI Device Model
 * Based on driver: hisilicon/zip/zip_main.c
 * This emulates the ZIP accelerator's MMIO register interface.
 * 
 * Changes for Phase 4:
 * - Increased MSI-X vectors to 64 to match common HiSilicon QM requirements.
 * - Increased BAR2 (queue memory) size from 32 MiB to 64 MiB.
 * - Minor adjustment to ensure MSI-X fallback works correctly.
 * - Initialize DAE_MEM_DONE and QM_MEM_INIT_DONE bits to 1 after reset to prevent driver timeout waiting for initialization completion.
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

#define TYPE_PCIBASE_DEVICE "hisi_zip_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI IDs */
#define PCI_VENDOR_ID_HUAWEI           0x19e5
#define PCI_DEVICE_ID_HUAWEI_ZIP_PF    0xa250
#define PCI_CLASS_ZIP                  PCI_CLASS_OTHERS

/* BIT and GENMASK helpers */
#ifndef BIT
#define BIT(nr) (1UL << (nr))
#endif
#ifndef GENMASK
#define GENMASK(h, l) (((1UL << ((h)-(l)+1))-1) << (l))
#endif

/* Register offsets from driver source */
#define HZIP_SOFT_CTRL_CNT_CLR_CE      0x301000
#define HZIP_CLOCK_GATE_CTRL           0x301004
#define HZIP_FSM_MAX_CNT               0x301008
#define HZIP_SOFT_CTRL_ZIP_CONTROL     0x30100C
#define HZIP_PORT_ARCA_CHE_0           0x301040
#define HZIP_PORT_ARCA_CHE_1           0x301044
#define HZIP_PORT_AWCA_CHE_0           0x301060
#define HZIP_PORT_AWCA_CHE_1           0x301064
#define HZIP_CORE_INT_SOURCE           0x3010A0
#define HZIP_CORE_INT_MASK_REG         0x3010A4
#define HZIP_CORE_INT_SET              0x3010A8
#define HZIP_CORE_INT_STATUS           0x3010AC
#define HZIP_BD_RUSER_32_63            0x301110
#define HZIP_SGL_RUSER_32_63           0x30111c
#define HZIP_DATA_RUSER_32_63          0x301128
#define HZIP_DATA_WUSER_32_63          0x301134
#define HZIP_BD_WUSER_32_63            0x301140
#define HZIP_SRAM_ECC_ERR_INFO         0x301148
#define HZIP_CORE_INT_RAS_CE_ENB       0x301160
#define HZIP_CORE_INT_RAS_NFE_ENB      0x301164
#define HZIP_CORE_INT_RAS_FE_ENB       0x301168
#define HZIP_PEH_CFG_AUTO_GATE         0x3011A8
#define HZIP_PREFETCH_CFG              0x3011B0
#define HZIP_SVA_TRANS                 0x3011C4
#define HZIP_LIT_LEN_EN_OFFSET         0x301204
#define HZIP_HIGH_PERF_OFFSET          0x301208
#define HZIP_OOO_SHUTDOWN_SEL          0x30120C

#define HZIP_CORE_INT_STATUS_M_ECC     BIT(1)

/* DAE register offsets */
#define DAE_MEM_START_OFFSET           0x331040
#define DAE_MEM_DONE_OFFSET            0x331044
#define DAE_MEM_DONE_MASK              0x1
#define DAE_ERR_SOURCE_OFFSET          0x331C84
#define DAE_ERR_ENABLE_OFFSET          0x331C80
#define DAE_ERR_CE_OFFSET              0x331CA0
#define DAE_ERR_NFE_OFFSET             0x331CA4
#define DAE_ERR_FE_OFFSET              0x331CA8
#define DAE_AXI_CFG_OFFSET             0x331000
#define DAE_AM_CTRL_GLOBAL_OFFSET      0x330000
#define DAE_AM_RETURN_OFFSET           0x330150
#define DAE_AM_RETURN_MASK             0x3

/* QM register offsets */
#define QM_MEM_START_INIT              0x100040
#define QM_MEM_INIT_DONE               0x100044
#define QM_DB_TIMEOUT_CFG              0x100074

/* SVA bit definitions */
#define HZIP_SVA_PREFETCH_DISABLE      BIT(26)
#define HZIP_SVA_DISABLE_READY         (BIT(26) | BIT(30))
#define HZIP_SVA_PREFETCH_NUM          GENMASK(18, 16)
#define HZIP_SVA_STALL_NUM             GENMASK(15, 0)

/* MMIO region size (covers all ZIP registers up to 0x33FFFF) */
#define HISI_ZIP_MMIO_SIZE             (4 * MiB)
/* Queue memory region size (typical for hisi_zip) */
#define HISI_ZIP_QM_MEM_SIZE           (64 * MiB)

/* MSI-X configuration */
#define HISI_ZIP_MSIX_VECTORS          64

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    int num_bars;

    /* Generic MMIO storage for all registers */
    uint8_t *mmio_data;

    /* Interrupt registers */
    uint32_t intr_status;      /* HZIP_CORE_INT_STATUS */
    uint32_t intr_mask;        /* HZIP_CORE_INT_MASK_REG */

    bool has_msi;
    bool has_msix;
};

/* Internal helper for status-triggered signaling */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->intr_status & ~s->intr_mask;
    if (pending) {
        if (s->has_msix && msix_enabled(pdev)) {
            msix_notify(pdev, 0);
        } else if (s->has_msi && msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!s->has_msix && !s->has_msi) {
            pci_set_irq(pdev, 0);
        }
    }
}

/* MMIO read handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* Intercept special registers with side effects */
    if (addr == HZIP_CORE_INT_STATUS) {
        return s->intr_status;
    }
    /* All other reads: fetch from the mmio_data buffer */
    if (addr + size > HISI_ZIP_MMIO_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: read out of bounds: addr=0x%" HWADDR_PRIx " size=%u\n",
                      __func__, addr, size);
        return ~0ULL;
    }
    memcpy(&val, &s->mmio_data[addr], size);
    /* The device is little-endian, so we can just return as-is for same endian host */
    return val;
}

/* MMIO write handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Intercept special registers */
    switch (addr) {
    case HZIP_CORE_INT_SOURCE:
        /* Write-1-to-clear */
        s->intr_status &= ~(uint32_t)val;
        pcibase_update_irq(s);
        return;
    case HZIP_CORE_INT_SET:
        s->intr_status |= (uint32_t)val;
        pcibase_update_irq(s);
        return;
    case HZIP_CORE_INT_MASK_REG:
        s->intr_mask = (uint32_t)val;
        pcibase_update_irq(s);
        return;
    case DAE_MEM_START_OFFSET:
        /* Trigger memory init done immediately */
        memcpy(&s->mmio_data[addr], &val, size);
        {
            uint32_t done;
            memcpy(&done, &s->mmio_data[DAE_MEM_DONE_OFFSET], sizeof(done));
            done |= DAE_MEM_DONE_MASK;
            memcpy(&s->mmio_data[DAE_MEM_DONE_OFFSET], &done, sizeof(done));
        }
        return;
    case QM_MEM_START_INIT:
        /* Trigger memory init done immediately */
        memcpy(&s->mmio_data[addr], &val, size);
        {
            uint32_t done;
            memcpy(&done, &s->mmio_data[QM_MEM_INIT_DONE], sizeof(done));
            done |= 1;  /* bit0 = done */
            memcpy(&s->mmio_data[QM_MEM_INIT_DONE], &done, sizeof(done));
        }
        return;
    case HZIP_SVA_TRANS:
        /* Write the value, then auto-set the disable ready bits */
        memcpy(&s->mmio_data[addr], &val, size);
        {
            uint32_t sva_val;
            memcpy(&sva_val, &s->mmio_data[addr], sizeof(sva_val));
            sva_val |= HZIP_SVA_DISABLE_READY;
            memcpy(&s->mmio_data[addr], &sva_val, sizeof(sva_val));
        }
        return;
    }

    /* Default: store to generic mmio_data */
    if (addr + size > HISI_ZIP_MMIO_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write out of bounds: addr=0x%" HWADDR_PRIx " size=%u val=0x%" PRIx64 "\n",
                      __func__, addr, size, val);
        return;
    }
    memcpy(&s->mmio_data[addr], &val, size);
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

    /* Clear all MMIO storage */
    memset(s->mmio_data, 0, HISI_ZIP_MMIO_SIZE);
    /* Initialize DAE_MEM_DONE to 1 to prevent driver timeout on init */
    uint32_t dae_done = DAE_MEM_DONE_MASK;
    memcpy(&s->mmio_data[DAE_MEM_DONE_OFFSET], &dae_done, sizeof(dae_done));
    /* Initialize QM_MEM_INIT_DONE to 1 */
    uint32_t qm_done = 1;
    memcpy(&s->mmio_data[QM_MEM_INIT_DONE], &qm_done, sizeof(qm_done));
    s->intr_status = 0;
    s->intr_mask = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;
    Error *local_err = NULL;

    /* Allocate MMIO backing store */
    s->mmio_data = g_malloc0(HISI_ZIP_MMIO_SIZE);

    /* Initialize done bits to 1 just as after reset */
    uint32_t dae_done = DAE_MEM_DONE_MASK;
    memcpy(&s->mmio_data[DAE_MEM_DONE_OFFSET], &dae_done, sizeof(dae_done));
    uint32_t qm_done = 1;
    memcpy(&s->mmio_data[QM_MEM_INIT_DONE], &qm_done, sizeof(qm_done));

    /* Static PCI configuration */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_HUAWEI);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_HUAWEI_ZIP_PF);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_ZIP);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    /* PCI Express capability */
    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    /* PM capability */
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, NULL);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR 0: MMIO region */
    memory_region_init_io(&s->bar_regions[0], OBJECT(s), &pcibase_mmio_ops, s,
                          "hisi_zip-mmio", HISI_ZIP_MMIO_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[0]);

    /* BAR 2: Queue memory (DMA buffer area) */
    memory_region_init_ram(&s->bar_regions[2], OBJECT(s), "hisi_zip-qm-mem",
                           HISI_ZIP_QM_MEM_SIZE, &local_err);
    if (local_err) {
        g_free(s->mmio_data);
        s->mmio_data = NULL;
        error_propagate(errp, local_err);
        return;
    }
    pci_register_bar(pdev, 2, PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_PREFETCH | PCI_BASE_ADDRESS_MEM_TYPE_64,
                     &s->bar_regions[2]);

    /* BAR 1: MSI-X */
    memory_region_init_ram(&s->bar_regions[1], OBJECT(s), "hisi_zip-msix",
                           16 * 1024, &local_err);
    if (local_err) {
        g_free(s->mmio_data);
        s->mmio_data = NULL;
        error_propagate(errp, local_err);
        return;
    }
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_regions[1]);

    /* Interrupt setup: try MSI-X first, fallback to MSI */
    s->has_msix = false;
    s->has_msi = false;

    if (!msix_init_exclusive_bar(pdev, HISI_ZIP_MSIX_VECTORS, 1, &local_err)) {
        /* MSI-X enabled successfully */
        s->has_msix = true;
    } else {
        /* MSI-X failed, clear error and try MSI */
        error_free(local_err);
        local_err = NULL;
        if (msi_init(pdev, 0, 1, true, false, &local_err) >= 0) {
            s->has_msi = true;
        } else {
            /* Both failed, propagate the MSI error */
            g_free(s->mmio_data);
            s->mmio_data = NULL;
            error_propagate(errp, local_err);
            return;
        }
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    g_free(s->mmio_data);
    s->mmio_data = NULL;

    if (s->has_msix) {
        msix_uninit_exclusive_bar(pdev);
    }
    if (s->has_msi) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "hisi_zip_pci",
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
