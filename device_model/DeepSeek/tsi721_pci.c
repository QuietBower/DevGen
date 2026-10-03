/*
 * QEMU Tsi721 PCIe RapidIO Controller Emulation
 * Based on driver: /home/eely/linux-7.1/drivers/rapidio/devices/tsi721.c
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

#define TYPE_PCIBASE_DEVICE "tsi721_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#ifndef PCI_VENDOR_ID_IDT
#define PCI_VENDOR_ID_IDT 0x111d
#endif
#ifndef PCI_CLASS_SYSTEM_RAPIDIO
#define PCI_CLASS_SYSTEM_RAPIDIO 0x0c80
#endif

#define PCI_DEVICE_ID_TSI721 0x80ab

/* BAR indices */
#define BAR_0 0
#define BAR_1 1
#define BAR_2 2
#define BAR_4 4

/* BAR sizes */
#define TSI721_REG_SPACE_SIZE (512 * 1024)  /* BAR0 */
#define TSI721_DB_WIN_SIZE    (16 * 1024 * 1024) /* 16MB */

/* MSI-X offsets */
#define TSI721_MSIXTBL_OFFSET 0x2c000
#define TSI721_MSIXPBA_OFFSET 0x2a000

/* Channel counts */
#define TSI721_DMA_MAXCH  8
#define TSI721_IMSG_CHNUM 8
#define TSI721_OMSG_CHNUM 4
#define TSI721_SRIO_MAXCH 16

/* DMA descriptor fields */
#define TSI721_DMAD_DEVID  0x0000ffff
#define TSI721_DMAD_CRF    0x00010000
#define TSI721_DMAD_PRIO   0x00060000
#define TSI721_DMAD_RTYPE  0x00780000
#define TSI721_DMAD_IOF    0x08000000
#define TSI721_DMAD_DTYPE  0xe0000000
#define TSI721_DMAD_BCOUNT1 0x03ffffff
#define TSI721_DMAD_BCOUNT2 0x0000000f
#define TSI721_DMAD_TT     0x0c000000
#define TSI721_DMAD_RADDR0  0xc0000000
#define TSI721_DMAD_CFGOFF  0x00ffffff
#define TSI721_DMAD_HOPCNT  0xff000000

/* OMSG descriptor fields */
#define TSI721_OMD_DEVID  0x0000ffff
#define TSI721_OMD_CRF    0x00010000
#define TSI721_OMD_PRIO   0x00060000
#define TSI721_OMD_IOF    0x08000000
#define TSI721_OMD_DTYPE  0xe0000000
#define TSI721_OMD_RSRVD  0x17f80000
#define TSI721_OMD_BCOUNT 0x00000ff8
#define TSI721_OMD_SSIZE  0x0000f000
#define TSI721_OMD_LETER  0x00030000
#define TSI721_OMD_XMBOX  0x003c0000
#define TSI721_OMD_MBOX   0x00c00000
#define TSI721_OMD_TT     0x0c000000

/* IMSG descriptor fields */
#define TSI721_IMD_DEVID  0x0000ffff
#define TSI721_IMD_CRF    0x00010000
#define TSI721_IMD_PRIO   0x00060000
#define TSI721_IMD_TT     0x00180000
#define TSI721_IMD_DTYPE  0xe0000000
#define TSI721_IMD_BCOUNT 0x00000ff8
#define TSI721_IMD_SSIZE  0x0000f000
#define TSI721_IMD_LETER  0x00030000
#define TSI721_IMD_XMBOX  0x003c0000
#define TSI721_IMD_MBOX   0x00c00000
#define TSI721_IMD_CS     0x78000000
#define TSI721_IMD_HO     0x80000000

/* Register offsets */
#define TSI721_DEVCTL               0x48004
#define TSI721_DEV_INT              0x29844
#define TSI721_DEV_INTE             0x29840
#define TSI721_DEV_CHAN_INT         0x29850
#define TSI721_DEV_CHAN_INTE        0x2984c
#define TSI721_DEV_INT_SRIO         0x00000020
#define TSI721_DEV_INT_BDMA_CH      0x00002000
#define TSI721_DEV_INT_SMSG_CH      0x00000800
#define TSI721_DEV_INT_SR2PC_CH     0x00000200

#define TSI721_DMAC_BASE(x)        (0x51000 + (x) * 0x1000)
#define TSI721_DMAC_CTL            0x008
#define TSI721_DMAC_STS            0x014
#define TSI721_DMAC_INT            0x00c
#define TSI721_DMAC_INTE           0x018
#define TSI721_DMAC_CTL_INIT       0x00000001
#define TSI721_DMAC_STS_RUN        0x00200000
#define TSI721_DMAC_STS_ABORT      0x00400000
#define TSI721_DMAC_INT_ALL        0x0000001f

#define TSI721_DMAC_DPTRL    0x01c
#define TSI721_DMAC_DPTRH    0x020
#define TSI721_DMAC_DSBL     0x024
#define TSI721_DMAC_DSBH     0x028
#define TSI721_DMAC_DSSZ     0x02c
#define TSI721_DMAC_DRDCNT   0x030
#define TSI721_DMAC_DWRCNT   0x034
#define TSI721_DMAC_DSWP     0x038
#define TSI721_DMAC_DSRP     0x03c
#define TSI721_DMAC_DPTRL_MASK   0xffffffff
#define TSI721_DMAC_DSBL_MASK    0xffffffff
#define TSI721_DMAC_DSSZ_SIZE(x) (x)

#define TSI721_IDQ_CTL(x)          (0x20000 + (x) * 0x1000)
#define TSI721_IDQ_RP(x)           (0x2000c + (x) * 0x1000)
#define TSI721_IDQ_WP(x)           (0x20010 + (x) * 0x1000)
#define TSI721_IDQ_SIZE(x)         (0x2001c + (x) * 0x1000)
#define TSI721_IDQ_BASEL(x)        (0x20014 + (x) * 0x1000)
#define TSI721_IDQ_BASEU(x)        (0x20018 + (x) * 0x1000)
#define TSI721_IDQ_MASK(x)         (0x20008 + (x) * 0x1000)
#define TSI721_IDQ_INIT            0x00000001
#define TSI721_IDQ_SIZE_VAL(x)     (x)

#define IDB_QUEUE  0
#define TSI721_IDB_ENTRY_SIZE 8
#define IDB_QSIZE  256

#define TSI721_SR_CHINT(x)         (0x20040 + (x) * 0x1000)
#define TSI721_SR_CHINTE(x)        (0x20044 + (x) * 0x1000)
#define TSI721_SR_CHINT_IDBQRCV    0x00000001
#define TSI721_SR_CHINT_ALL        0xffffffff

#define TSI721_IBDMAC_CTL(x)       (0x61240 + (x) * 0x1000)
#define TSI721_IBDMAC_STS(x)       (0x61244 + (x) * 0x1000)
#define TSI721_IBDMAC_INT(x)       (0x61248 + (x) * 0x1000)
#define TSI721_IBDMAC_INTE(x)      (0x61250 + (x) * 0x1000)
#define TSI721_IBDMAC_INT_ALL      0x0000001f
#define TSI721_IBDMAC_INT_MASK     0xffffffff
#define TSI721_IBDMAC_CTL_INIT     0x00000001

#define TSI721_IBDMAC_DQBH(x)  (0x61260 + (x) * 0x1000)
#define TSI721_IBDMAC_DQBL(x)  (0x61264 + (x) * 0x1000)
#define TSI721_IBDMAC_DQSZ(x)  (0x61268 + (x) * 0x1000)
#define TSI721_IBDMAC_FQBH(x)  (0x6126c + (x) * 0x1000)
#define TSI721_IBDMAC_FQBL(x)  (0x61270 + (x) * 0x1000)
#define TSI721_IBDMAC_FQSZ(x)  (0x61274 + (x) * 0x1000)
#define TSI721_IBDMAC_FQWP(x)  (0x61278 + (x) * 0x1000)
#define TSI721_IBDMAC_DQRP(x)  (0x6127c + (x) * 0x1000)
#define TSI721_IBDMAC_FQBL_MASK 0xffffffff
#define TSI721_IBDMAC_DQBL_MASK 0xffffffff

#define TSI721_OBDMAC_CTL(x)       (0x61008 + (x) * 0x1000)
#define TSI721_OBDMAC_STS(x)       (0x61014 + (x) * 0x1000)
#define TSI721_OBDMAC_INT(x)       (0x6100c + (x) * 0x1000)
#define TSI721_OBDMAC_INTE(x)      (0x61018 + (x) * 0x1000)
#define TSI721_OBDMAC_INT_ALL      0x0000001f

#define TSI721_OBDMAC_DPTRH(x) (0x61020 + (x) * 0x1000)
#define TSI721_OBDMAC_DPTRL(x) (0x6101c + (x) * 0x1000)
#define TSI721_OBDMAC_DPTRL_MASK 0xffffffff
#define TSI721_OBDMAC_DSBH(x)  (0x61024 + (x) * 0x1000)
#define TSI721_OBDMAC_DSBL(x)  (0x61028 + (x) * 0x1000)
#define TSI721_OBDMAC_DSBL_MASK 0xffffffff
#define TSI721_OBDMAC_DSSZ(x)  (0x6102c + (x) * 0x1000)
#define TSI721_OBDMAC_DRDCNT(x) (0x61030 + (x) * 0x1000)
#define TSI721_OBDMAC_DWRCNT(x) (0x61034 + (x) * 0x1000)
#define TSI721_OBDMAC_DSRP(x)  (0x6103c + (x) * 0x1000)

#define TSI721_IB_DEVID            0x60020
#define TSI721_IMSGID_SET          0x00000001

#define TSI721_SMSG_ECC_LOG            0x29890
#define TSI721_SMSG_ECC_COR_LOG(x)    (0x29894 + (x) * 4)
#define TSI721_SMSG_ECC_NCOR(x)       (0x298a4 + (x) * 4)
#define TSI721_SMSG_ECC_COR_LOG_MASK  0xffffffff
#define TSI721_SMSG_ECC_NCOR_MASK     0xffffffff
#define TSI721_RETRY_GEN_CNT          0x298b0
#define TSI721_RETRY_RX_CNT           0x298b4
#define TSI721_RQRPTO                 0x298b8
#define TSI721_RQRPTO_VAL             0x00000000
#define TSI721_SMSG_INTE              0x298c0

#define TSI721_BDMA_INTE              0x29900
#define TSI721_SR2PC_GEN_INTE         0x29910
#define TSI721_PC2SR_INTE             0x29920
#define TSI721_I2C_INT_ENABLE         0x29930

#define TSI721_RIO_EM_INT_ENABLE      0x29880
#define TSI721_RIO_EM_INT_STAT        0x29884
#define TSI721_RIO_EM_DEV_INT_EN      0x29888

#define TSI721_RIO_PW_CTL             0x29800
#define TSI721_RIO_PW_RX_STAT         0x29804
#define TSI721_RIO_PW_RX_CAPT(x)     (0x29810 + (x) * 4)

#define TSI721_OBWINLB(x)      (0x40000 + (x) * 0x20)
#define TSI721_OBWINUB(x)      (0x40004 + (x) * 0x20)
#define TSI721_OBWINSZ(x)      (0x40008 + (x) * 0x20)
#define TSI721_LUT_DATA0        0x41000
#define TSI721_LUT_DATA1        0x41004
#define TSI721_LUT_DATA2        0x41008
#define TSI721_ZONE_SEL         0x4100c
#define TSI721_ZONE_SEL_GO      0x80000000

#define TSI721_IBWIN_LB(x)     (0x48000 + (x) * 0x20)
#define TSI721_IBWIN_SZ(x)     (0x48008 + (x) * 0x20)
#define TSI721_IBWIN_TUA(x)    (0x4800c + (x) * 0x20)
#define TSI721_IBWIN_TLA(x)    (0x48010 + (x) * 0x20)
#define TSI721_IBWIN_UB(x)     (0x48004 + (x) * 0x20)
#define TSI721_IBWIN_LB_BA     0xffff0000
#define TSI721_IBWIN_LB_WEN    0x00000001
#define TSI721_IBWIN_TLA_ADD   0xffffffff
#define TSI721_OBWINLB_BA      0xffff0000
#define TSI721_OBWINLB_WEN     0x00000001

#define TSI721_DEVCTL_SRBOOT_CMPL 0x00000001

/* RapidIO standard registers */
#define TSI721_RIO_PORT_N_ERR_STS_CSR   0x158
#define TSI721_RIO_PORT_N_CTL2_CSR      0x15c
#define TSI721_RIO_PORT_N_CTL_CSR       0x160
#define TSI721_RIO_PORT_GEN_CTL_CSR     0x104
#define RIO_PORT_N_ERR_STS_PORT_OK  0x00000001
#define RIO_PORT_N_CTL2_SEL_BAUD    0xf0000000
#define RIO_PORT_N_CTL_IPW          0x08000000

#define TSI721_VECT_MAX 34

#define TSI721_DMACH_MAINT 7
#define TSI721_DMA_MINSTSSZ 2
#define TSI721_MSG_BUFFER_SIZE 0x1000

#define TSI721_INT_IMSG_CHAN_M  0x00ff0000
#define TSI721_INT_OMSG_CHAN_M  0x000000ff
#define TSI721_INT_BDMA_CHAN_M  0xff000000
#define TSI721_INT_IMSG_CHAN(x) (1 << (16 + (x)))
#define TSI721_INT_OMSG_CHAN(x) (1 << (0 + (x)))
#define TSI721_INT_BDMA_CHAN(x) (1 << (24 + (x)))

#define TSI721_OBDMAC_INT_DONE      0x00000001
#define TSI721_OBDMAC_INT_IOF_DONE  0x00000002
#define TSI721_OBDMAC_INT_ST_FULL   0x00000004
#define TSI721_OBDMAC_INT_ERROR     0x00000008
#define TSI721_OBDMAC_INT_MASK      0xffffffff
#define TSI721_IBDMAC_INT_DQ_RCV    0x00000001
#define TSI721_IBDMAC_INT_SRTO      0x00000002
#define TSI721_IBDMAC_INT_PC_ERROR  0x00000004
#define TSI721_IBDMAC_INT_FQ_LOW    0x00000008

#define TSI721_OMSGD_MIN_RING_SIZE 2
#define TSI721_OMSGD_RING_SIZE     256
#define TSI721_IMSGD_MIN_RING_SIZE 2
#define TSI721_IMSGD_RING_SIZE     256

typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int     index;
    BARType type;
    hwaddr  size;
    const char *name;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;
    uint32_t intr_status;
    uint32_t intr_mask;

    uint8_t regs[TSI721_REG_SPACE_SIZE];

    struct {
        struct {
            dma_addr_t bd_phys;
            uint32_t bd_num;
            dma_addr_t sts_phys;
            uint32_t sts_size;
            uint32_t sts_rdptr;
        } bdma[TSI721_DMA_MAXCH];
    } dma;

    uint32_t dev_status;
};

struct tsi721_dma_desc {
    __le32 type_id;
    __le32 bcount;
    union {
        __le32 raddr_lo;
        __le32 next_lo;
    };
    union {
        __le32 raddr_hi;
        __le32 next_hi;
    };
    union {
        struct {
            __le32 bufptr_lo;
            __le32 bufptr_hi;
            __le32 s_dist;
            __le32 s_size;
        } t1;
        __le32 data[4];
        uint32_t    reserved[4];
    };
};

struct tsi721_dma_sts {
    __le64 desc_sts[8];
};

struct tsi721_omsg_desc {
    __le32 type_id;
    __le32 msg_info;
    union {
        __le32 bufptr_lo;
        __le32 next_lo;
    };
    union {
        __le32 bufptr_hi;
        __le32 next_hi;
    };
};

struct tsi721_imsg_desc {
    __le32 type_id;
    __le32 msg_info;
    __le32 bufptr_lo;
    __le32 bufptr_hi;
    uint32_t    reserved[12];
};

static bool is_w1c_register(hwaddr addr)
{
    int ch;
    for (ch = 0; ch < TSI721_DMA_MAXCH; ch++) {
        if (addr == TSI721_DMAC_BASE(ch) + TSI721_DMAC_INT) return true;
    }
    for (ch = 0; ch < TSI721_IMSG_CHNUM; ch++) {
        if (addr == TSI721_IBDMAC_INT(ch)) return true;
    }
    for (ch = 0; ch < TSI721_OMSG_CHNUM; ch++) {
        if (addr == TSI721_OBDMAC_INT(ch)) return true;
    }
    for (ch = 0; ch < TSI721_SRIO_MAXCH; ch++) {
        if (addr == TSI721_SR_CHINT(ch)) return true;
    }
    if (addr == TSI721_RIO_EM_INT_STAT) return true;
    if (addr == TSI721_RIO_PW_RX_STAT) return true;
    return false;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr + size > TSI721_REG_SPACE_SIZE) {
        return 0;
    }

    switch (size) {
    case 1: val = s->regs[addr]; break;
    case 2: val = lduw_le_p(&s->regs[addr]); break;
    case 4: val = ldl_le_p(&s->regs[addr]); break;
    case 8: val = ldq_le_p(&s->regs[addr]); break;
    default: val = 0;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr + size > TSI721_REG_SPACE_SIZE) {
        return;
    }

    if (size == 4 && is_w1c_register(addr)) {
        uint32_t reg_val = ldl_le_p(&s->regs[addr]);
        reg_val &= ~((uint32_t)val);
        stl_le_p(&s->regs[addr], reg_val);
        return;
    }

    switch (size) {
    case 1: s->regs[addr] = (uint8_t)val; break;
    case 2: stw_le_p(&s->regs[addr], (uint16_t)val); break;
    case 4: stl_le_p(&s->regs[addr], (uint32_t)val); break;
    case 8: stq_le_p(&s->regs[addr], val); break;
    }
}

static uint64_t pcibase_db_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void pcibase_db_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* Doorbell write: ignore for now */
}

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

static const MemoryRegionOps pcibase_db_mmio_ops = {
    .read = pcibase_db_mmio_read,
    .write = pcibase_db_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 8 },
};

G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* Not used during probe */
}

G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    /* Not used during probe */
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    memset(s->regs, 0, sizeof(s->regs));
    memset(&s->dma, 0, sizeof(s->dma));
    s->dev_status = 0;
    s->intr_status = 0;
    s->intr_mask = 0;

    /* Set initial register values required by driver probe */
    stl_le_p(&s->regs[TSI721_DEVCTL], 0);
    stl_le_p(&s->regs[0x100 + TSI721_RIO_PORT_N_ERR_STS_CSR], RIO_PORT_N_ERR_STS_PORT_OK);
    stl_le_p(&s->regs[0x100 + TSI721_RIO_PORT_N_CTL2_CSR], 0x10000000);
    stl_le_p(&s->regs[0x100 + TSI721_RIO_PORT_N_CTL_CSR], RIO_PORT_N_CTL_IPW);
    stl_le_p(&s->regs[0x100 + TSI721_RIO_PORT_GEN_CTL_CSR], 0);
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, const MemoryRegionOps *ops, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), ops, s, bi->name, aligned_size);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_IDT);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_TSI721);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_SYSTEM_RAPIDIO);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR0: registers */
    s->bar_info[0] = (BARInfo){ .index = 0, .type = BAR_TYPE_MMIO, .size = TSI721_REG_SPACE_SIZE, .name = "BAR0" };
    pcibase_register_bar(pdev, s, &s->bar_info[0], &pcibase_mmio_ops, errp);

    /* BAR1: outbound doorbells */
    s->bar_info[1] = (BARInfo){ .index = 1, .type = BAR_TYPE_MMIO, .size = TSI721_DB_WIN_SIZE, .name = "BAR1" };
    pcibase_register_bar(pdev, s, &s->bar_info[1], &pcibase_db_mmio_ops, errp);

    /* BAR2: outbound translation (not used) */
    s->bar_info[2] = (BARInfo){ .index = 2, .type = BAR_TYPE_MMIO, .size = 0, .name = "BAR2" };
    pcibase_register_bar(pdev, s, &s->bar_info[2], &pcibase_mmio_ops, errp);

    /* BAR4: outbound translation + MSI-X */
    s->bar_info[3] = (BARInfo){ .index = 4, .type = BAR_TYPE_MMIO, .size = 0x40000, .name = "BAR4" };
    pcibase_register_bar(pdev, s, &s->bar_info[3], &pcibase_mmio_ops, errp);

    s->num_bars = 4;

    /* MSI-X initialization */
    int ret = msix_init(pdev, TSI721_VECT_MAX,
                        &s->bar_regions[4], 4, TSI721_MSIXTBL_OFFSET,
                        &s->bar_regions[4], 4, TSI721_MSIXPBA_OFFSET,
                        0, errp);
    if (ret < 0) {
        return;
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

static const VMStateDescription vmstate_pcibase = {
    .name = "tsi721_pci",
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
