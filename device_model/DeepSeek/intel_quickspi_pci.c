/*
 * QEMU Intel QuickSPI PCI device model (Phase 2 Implementation)
 * Based on driver: drivers/hid/intel-thc-hid/intel-quickspi/pci-quickspi.c
 * Register offsets and definitions from THC hardware header (thc_hw.h) snippets.
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

/* Register offsets from driver sources */
#define THC_M_CMN_LTR_CTRL_OFFSET          0x14
#define THC_M_PRT_CONTROL_OFFSET           0x1008
#define THC_M_PRT_SPI_CFG_OFFSET           0x1010
#define THC_M_PRT_SPI_ICRRD_OPCODE_OFFSET  0x1014
#define THC_M_PRT_SPI_DMARD_OPCODE_OFFSET  0x1018
#define THC_M_PRT_SPI_WR_OPCODE_OFFSET     0x101C
#define THC_M_PRT_INT_EN_OFFSET            0x1020
#define THC_M_PRT_INT_STATUS_OFFSET        0x1024
#define THC_M_PRT_ERR_CAUSE_OFFSET         0x1028
#define THC_M_PRT_SW_SEQ_CNTRL_OFFSET      0x1040
#define THC_M_PRT_SW_SEQ_STS_OFFSET        0x1044
#define THC_M_PRT_WRITE_DMA_CNTRL_OFFSET   0x1098
#define THC_M_PRT_WRITE_INT_STS_OFFSET     0x109C
#define THC_M_PRT_WR_BULK_ADDR_OFFSET      0x10B4
#define THC_M_PRT_DEV_INT_CAUSE_ADDR_OFFSET 0x10B8
#define THC_M_PRT_DEVINT_CFG_1_OFFSET      0x10EC
#define THC_M_PRT_DEVINT_CFG_2_OFFSET      0x10F0
#define THC_M_PRT_READ_DMA_CNTRL_1_OFFSET  0x110C
#define THC_M_PRT_READ_DMA_INT_STS_1_OFFSET 0x1110
#define THC_M_PRT_TSEQ_CNTRL_1_OFFSET      0x1128
#define THC_M_PRT_RD_BULK_ADDR_1_OFFSET    0x1170
#define THC_M_PRT_READ_DMA_CNTRL_2_OFFSET  0x120C
#define THC_M_PRT_READ_DMA_INT_STS_2_OFFSET 0x1210
#define THC_M_PRT_RD_BULK_ADDR_2_OFFSET    0x1270
#define THC_M_PRT_READ_DMA_INT_STS_SW_OFFSET 0x12D0
#define THC_M_PRT_SPARE_REG_OFFSET         0x12BC
#define THC_M_PRT_SPI_DUTYC_CFG_OFFSET     0x1300

#define THC_MMIO_SIZE 0x2000

/* Interrupt bit definitions from driver */
#define THC_M_PRT_INT_STATUS_DEV_RAW_INT_STS  BIT(15)
#define THC_M_PRT_INT_EN_GBL_INT_EN           BIT(31)

#define THC_M_PRT_SW_SEQ_STS_TSSDONE           BIT(0)
#define THC_M_PRT_SW_SEQ_CNTRL_THC_SS_CD_IE    BIT(1)

#define THC_M_PRT_READ_DMA_CNTRL_START         BIT(0)
#define THC_M_PRT_READ_DMA_INT_STS_EOF_INT_STS BIT(5)
#define THC_M_PRT_WRITE_INT_STS_THC_WRDMA_IOC_STS BIT(2)

#define TYPE_PCIBASE_DEVICE "intel_quickspi_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* PCI Identifiers from driver placeholder - need to be confirmed */
#define VENDOR_ID 0x8086  /* Intel */
#define DEVICE_ID 0x0001  /* Placeholder - need THC_MTL_DEVICE_ID_SPI_PORT1 */
#define CLASS_ID  0x0C0330 /* USB subclass (placeholder - need actual THC class) */

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    int num_bars;

    bool has_msi;

    /* Actual hardware registers */
    uint32_t cmn_ltr_ctrl;
    uint32_t prt_control;
    uint32_t prt_spi_cfg;
    uint32_t prt_spi_icrrd_opcode;
    uint32_t prt_spi_dmard_opcode;
    uint32_t prt_spi_wr_opcode;
    uint32_t prt_int_en;
    uint32_t prt_int_status;
    uint32_t prt_err_cause;
    uint32_t prt_sw_seq_cntrl;
    uint32_t prt_sw_seq_sts;
    uint32_t prt_write_dma_cntrl;
    uint32_t prt_write_int_sts;
    uint32_t prt_wr_bulk_addr_lo;
    uint32_t prt_dev_int_cause_addr;
    uint32_t prt_devint_cfg_1;
    uint32_t prt_devint_cfg_2;
    uint32_t prt_read_dma_cntrl_1;
    uint32_t prt_read_dma_int_sts_1;
    uint32_t prt_tseq_cntrl_1;
    uint32_t prt_rd_bulk_addr_1_lo;
    uint32_t prt_rd_bulk_addr_1_hi;
    uint32_t prt_read_dma_cntrl_2;
    uint32_t prt_read_dma_int_sts_2;
    uint32_t prt_rd_bulk_addr_2_lo;
    uint32_t prt_rd_bulk_addr_2_hi;
    uint32_t prt_read_dma_int_sts_sw;
    uint32_t prt_spare_reg;
    uint32_t prt_spi_dutyc_cfg;

    QEMUTimer dma_timer;
    uint8_t dma_active; /* bitmask: bit0 = rd_dma1, bit1 = rd_dma2, bit2 = wr_dma */
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool pending = (s->prt_int_status & s->prt_int_en & THC_M_PRT_INT_EN_GBL_INT_EN) != 0;
    if (pending) {
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

static void pcibase_dma_timer(void *opaque)
{
    PCIBaseState *s = opaque;
    if (s->dma_active & 0x1) {
        s->prt_read_dma_int_sts_1 |= THC_M_PRT_READ_DMA_INT_STS_EOF_INT_STS;
        s->prt_int_status |= THC_M_PRT_INT_STATUS_DEV_RAW_INT_STS;
        s->dma_active &= ~0x1;
    }
    if (s->dma_active & 0x2) {
        s->prt_read_dma_int_sts_2 |= THC_M_PRT_READ_DMA_INT_STS_EOF_INT_STS;
        s->prt_int_status |= THC_M_PRT_INT_STATUS_DEV_RAW_INT_STS;
        s->dma_active &= ~0x2;
    }
    if (s->dma_active & 0x4) {
        s->prt_write_int_sts |= THC_M_PRT_WRITE_INT_STS_THC_WRDMA_IOC_STS;
        s->prt_int_status |= THC_M_PRT_INT_STATUS_DEV_RAW_INT_STS;
        s->dma_active &= ~0x4;
    }
    pcibase_update_irq(s);
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    switch (addr) {
    case THC_M_CMN_LTR_CTRL_OFFSET:
        val = s->cmn_ltr_ctrl;
        break;
    case THC_M_PRT_CONTROL_OFFSET:
        val = s->prt_control;
        break;
    case THC_M_PRT_SPI_CFG_OFFSET:
        val = s->prt_spi_cfg;
        break;
    case THC_M_PRT_SPI_ICRRD_OPCODE_OFFSET:
        val = s->prt_spi_icrrd_opcode;
        break;
    case THC_M_PRT_SPI_DMARD_OPCODE_OFFSET:
        val = s->prt_spi_dmard_opcode;
        break;
    case THC_M_PRT_SPI_WR_OPCODE_OFFSET:
        val = s->prt_spi_wr_opcode;
        break;
    case THC_M_PRT_INT_EN_OFFSET:
        val = s->prt_int_en;
        break;
    case THC_M_PRT_INT_STATUS_OFFSET:
        val = s->prt_int_status;
        break;
    case THC_M_PRT_ERR_CAUSE_OFFSET:
        val = s->prt_err_cause;
        break;
    case THC_M_PRT_SW_SEQ_CNTRL_OFFSET:
        val = s->prt_sw_seq_cntrl;
        break;
    case THC_M_PRT_SW_SEQ_STS_OFFSET:
        val = s->prt_sw_seq_sts;
        break;
    case THC_M_PRT_WRITE_DMA_CNTRL_OFFSET:
        val = s->prt_write_dma_cntrl;
        break;
    case THC_M_PRT_WRITE_INT_STS_OFFSET:
        val = s->prt_write_int_sts;
        break;
    case THC_M_PRT_WR_BULK_ADDR_OFFSET:
        val = s->prt_wr_bulk_addr_lo;
        break;
    case THC_M_PRT_DEV_INT_CAUSE_ADDR_OFFSET:
        val = s->prt_dev_int_cause_addr;
        break;
    case THC_M_PRT_DEVINT_CFG_1_OFFSET:
        val = s->prt_devint_cfg_1;
        break;
    case THC_M_PRT_DEVINT_CFG_2_OFFSET:
        val = s->prt_devint_cfg_2;
        break;
    case THC_M_PRT_READ_DMA_CNTRL_1_OFFSET:
        val = s->prt_read_dma_cntrl_1;
        break;
    case THC_M_PRT_READ_DMA_INT_STS_1_OFFSET:
        val = s->prt_read_dma_int_sts_1;
        break;
    case THC_M_PRT_TSEQ_CNTRL_1_OFFSET:
        val = s->prt_tseq_cntrl_1;
        break;
    case THC_M_PRT_RD_BULK_ADDR_1_OFFSET:
        val = s->prt_rd_bulk_addr_1_lo;
        break;
    case THC_M_PRT_RD_BULK_ADDR_1_OFFSET + 4:
        val = s->prt_rd_bulk_addr_1_hi;
        break;
    case THC_M_PRT_READ_DMA_CNTRL_2_OFFSET:
        val = s->prt_read_dma_cntrl_2;
        break;
    case THC_M_PRT_READ_DMA_INT_STS_2_OFFSET:
        val = s->prt_read_dma_int_sts_2;
        break;
    case THC_M_PRT_RD_BULK_ADDR_2_OFFSET:
        val = s->prt_rd_bulk_addr_2_lo;
        break;
    case THC_M_PRT_RD_BULK_ADDR_2_OFFSET + 4:
        val = s->prt_rd_bulk_addr_2_hi;
        break;
    case THC_M_PRT_READ_DMA_INT_STS_SW_OFFSET:
        val = s->prt_read_dma_int_sts_sw;
        break;
    case THC_M_PRT_SPARE_REG_OFFSET:
        val = s->prt_spare_reg;
        break;
    case THC_M_PRT_SPI_DUTYC_CFG_OFFSET:
        val = s->prt_spi_dutyc_cfg;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented MMIO read at 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        val = 0;
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case THC_M_CMN_LTR_CTRL_OFFSET:
        s->cmn_ltr_ctrl = val;
        break;
    case THC_M_PRT_CONTROL_OFFSET:
        s->prt_control = val;
        if (val & BIT(1)) { /* QUIESCE_EN */
            s->prt_control |= BIT(2); /* HW_STS */
            /* Optionally raise interrupt if enabled */
            s->prt_int_status |= THC_M_PRT_INT_STATUS_DEV_RAW_INT_STS;
            pcibase_update_irq(s);
        }
        break;
    case THC_M_PRT_SPI_CFG_OFFSET:
        s->prt_spi_cfg = val;
        break;
    case THC_M_PRT_SPI_ICRRD_OPCODE_OFFSET:
        s->prt_spi_icrrd_opcode = val;
        break;
    case THC_M_PRT_SPI_DMARD_OPCODE_OFFSET:
        s->prt_spi_dmard_opcode = val;
        break;
    case THC_M_PRT_SPI_WR_OPCODE_OFFSET:
        s->prt_spi_wr_opcode = val;
        break;
    case THC_M_PRT_INT_EN_OFFSET:
        s->prt_int_en = val;
        pcibase_update_irq(s);
        break;
    case THC_M_PRT_INT_STATUS_OFFSET:
        s->prt_int_status &= ~val; /* W1C */
        pcibase_update_irq(s);
        break;
    case THC_M_PRT_ERR_CAUSE_OFFSET:
        s->prt_err_cause = val; /* no side effects for now */
        break;
    case THC_M_PRT_SW_SEQ_CNTRL_OFFSET:
        s->prt_sw_seq_cntrl = val;
        s->prt_sw_seq_sts |= THC_M_PRT_SW_SEQ_STS_TSSDONE;
        if (val & THC_M_PRT_SW_SEQ_CNTRL_THC_SS_CD_IE) {
            s->prt_int_status |= THC_M_PRT_INT_STATUS_DEV_RAW_INT_STS;
            pcibase_update_irq(s);
        }
        break;
    case THC_M_PRT_SW_SEQ_STS_OFFSET:
        /* Possibly W1C? Let's assume reads only, ignore write */
        break;
    case THC_M_PRT_WRITE_DMA_CNTRL_OFFSET:
        s->prt_write_dma_cntrl = val;
        if (val & BIT(0)) { /* START */
            s->dma_active |= 0x4;
            timer_mod(&s->dma_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
        }
        break;
    case THC_M_PRT_WRITE_INT_STS_OFFSET:
        s->prt_write_int_sts &= ~val; /* W1C */
        break;
    case THC_M_PRT_WR_BULK_ADDR_OFFSET:
        s->prt_wr_bulk_addr_lo = val;
        break;
    case THC_M_PRT_DEV_INT_CAUSE_ADDR_OFFSET:
        s->prt_dev_int_cause_addr = val;
        break;
    case THC_M_PRT_DEVINT_CFG_1_OFFSET:
        s->prt_devint_cfg_1 = val;
        break;
    case THC_M_PRT_DEVINT_CFG_2_OFFSET:
        s->prt_devint_cfg_2 = val;
        break;
    case THC_M_PRT_READ_DMA_CNTRL_1_OFFSET:
        s->prt_read_dma_cntrl_1 = val;
        if (val & THC_M_PRT_READ_DMA_CNTRL_START) {
            s->dma_active |= 0x1;
            timer_mod(&s->dma_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
        }
        break;
    case THC_M_PRT_READ_DMA_INT_STS_1_OFFSET:
        s->prt_read_dma_int_sts_1 &= ~val; /* W1C */
        break;
    case THC_M_PRT_TSEQ_CNTRL_1_OFFSET:
        s->prt_tseq_cntrl_1 = val;
        break;
    case THC_M_PRT_RD_BULK_ADDR_1_OFFSET:
        s->prt_rd_bulk_addr_1_lo = val;
        break;
    case THC_M_PRT_RD_BULK_ADDR_1_OFFSET + 4:
        s->prt_rd_bulk_addr_1_hi = val;
        break;
    case THC_M_PRT_READ_DMA_CNTRL_2_OFFSET:
        s->prt_read_dma_cntrl_2 = val;
        if (val & THC_M_PRT_READ_DMA_CNTRL_START) {
            s->dma_active |= 0x2;
            timer_mod(&s->dma_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000000);
        }
        break;
    case THC_M_PRT_READ_DMA_INT_STS_2_OFFSET:
        s->prt_read_dma_int_sts_2 &= ~val; /* W1C */
        break;
    case THC_M_PRT_RD_BULK_ADDR_2_OFFSET:
        s->prt_rd_bulk_addr_2_lo = val;
        break;
    case THC_M_PRT_RD_BULK_ADDR_2_OFFSET + 4:
        s->prt_rd_bulk_addr_2_hi = val;
        break;
    case THC_M_PRT_READ_DMA_INT_STS_SW_OFFSET:
        s->prt_read_dma_int_sts_sw &= ~val; /* W1C */
        break;
    case THC_M_PRT_SPARE_REG_OFFSET:
        s->prt_spare_reg = val;
        break;
    case THC_M_PRT_SPI_DUTYC_CFG_OFFSET:
        s->prt_spi_dutyc_cfg = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unimplemented MMIO write to 0x%" HWADDR_PRIx " val=0x%" PRIx64 "\n",
                      __func__, addr, val);
        break;
    }
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 4, .max_access_size = 8 },
    .impl  = { .min_access_size = 4, .max_access_size = 8 },
};

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));
    /* Reset register shadows to power-on defaults */
    s->cmn_ltr_ctrl = 0;
    s->prt_control = 0;
    s->prt_spi_cfg = 0;
    s->prt_spi_icrrd_opcode = 0;
    s->prt_spi_dmard_opcode = 0;
    s->prt_spi_wr_opcode = 0;
    s->prt_int_en = 0;
    s->prt_int_status = 0;
    s->prt_err_cause = 0;
    s->prt_sw_seq_cntrl = 0;
    s->prt_sw_seq_sts = 0;
    s->prt_write_dma_cntrl = 0;
    s->prt_write_int_sts = 0;
    s->prt_wr_bulk_addr_lo = 0;
    s->prt_dev_int_cause_addr = 0;
    s->prt_devint_cfg_1 = 0;
    s->prt_devint_cfg_2 = 0;
    s->prt_read_dma_cntrl_1 = 0;
    s->prt_read_dma_int_sts_1 = 0;
    s->prt_tseq_cntrl_1 = 0;
    s->prt_rd_bulk_addr_1_lo = 0;
    s->prt_rd_bulk_addr_1_hi = 0;
    s->prt_read_dma_cntrl_2 = 0;
    s->prt_read_dma_int_sts_2 = 0;
    s->prt_rd_bulk_addr_2_lo = 0;
    s->prt_rd_bulk_addr_2_hi = 0;
    s->prt_read_dma_int_sts_sw = 0;
    s->prt_spare_reg = 0;
    s->prt_spi_dutyc_cfg = 0;
    s->dma_active = 0;
    timer_del(&s->dma_timer);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID, DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR0: MMIO for THC registers */
    s->num_bars = 1;
    MemoryRegion *mr = &s->bar_regions[0];
    memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, "quickspi-mmio", THC_MMIO_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);

    /* Enable MSI to support PCI_IRQ_ALL_TYPES from driver */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }
    s->has_msi = true;

    timer_init_ns(&s->dma_timer, QEMU_CLOCK_VIRTUAL, pcibase_dma_timer, s);
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
    timer_del(&s->dma_timer);
}

static const VMStateDescription vmstate_pcibase = {
    .name = "intel_quickspi_pci",
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
