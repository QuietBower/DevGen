/*
 * QEMU model of DEC FDDI controller (DEFPA) for use with Linux driver
 * (drivers/net/fddi/defxx.c). Based on PCI template and driver analysis.
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
#include "hw/pci/pci_ids.h"

#define TYPE_PCIBASE_DEVICE "defxx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor/Device IDs (from PCI config of DEC FDDI adapter) */
#define VENDOR_ID PCI_VENDOR_ID_DEC
#define PCI_DEVICE_ID_DEC_FDDI 0x000F
#define DEVICE_ID PCI_DEVICE_ID_DEC_FDDI
#define CLASS_ID  PCI_CLASS_NETWORK_FDDI

/* PDQ (Port Data Queue) Register Offsets */
#define PI_PDQ_K_REG_PORT_RESET       0x00000000
#define PI_PDQ_K_REG_HOST_DATA        0x00000004
#define PI_PDQ_K_REG_PORT_CTRL        0x00000008
#define PI_PDQ_K_REG_PORT_DATA_A      0x0000000C
#define PI_PDQ_K_REG_PORT_DATA_B      0x00000010
#define PI_PDQ_K_REG_PORT_STATUS      0x00000014
#define PI_PDQ_K_REG_TYPE_0_STATUS    0x00000018
#define PI_PDQ_K_REG_HOST_INT_ENB     0x0000001C
#define PI_PDQ_K_REG_TYPE_2_PROD      0x00000024
#define PI_PDQ_K_REG_CMD_RSP_PROD     0x00000028
#define PI_PDQ_K_REG_CMD_REQ_PROD     0x0000002C

/* ESIC (EISA) registers - not used by PCI, but defined for completeness */
#define PI_ESIC_K_ESIC_CSR           0xC80
#define PI_ESIC_K_SLOT_CNTRL         0xC84
#define PI_ESIC_K_MEM_ADD_HI_CMP_0   0xC88
#define PI_ESIC_K_MEM_ADD_HI_CMP_1   0xC89
#define PI_ESIC_K_MEM_ADD_HI_CMP_2   0xC8A
#define PI_ESIC_K_MEM_ADD_LO_CMP_0   0xC8E
#define PI_ESIC_K_MEM_ADD_LO_CMP_1   0xC8F
#define PI_ESIC_K_MEM_ADD_LO_CMP_2   0xC90
#define PI_ESIC_K_IO_ADD_CMP_0_0     0xC91
#define PI_ESIC_K_IO_ADD_CMP_0_1     0xC92
#define PI_ESIC_K_IO_ADD_CMP_1_0     0xC93
#define PI_ESIC_K_IO_ADD_CMP_1_1     0xC94
#define PI_ESIC_K_IO_ADD_MASK_0_0    0xC99
#define PI_ESIC_K_IO_ADD_MASK_0_1    0xC9A
#define PI_ESIC_K_IO_ADD_MASK_1_0    0xC9B
#define PI_ESIC_K_IO_ADD_MASK_1_1    0xC9C
#define PI_ESIC_K_IO_CONFIG_STAT_0   0xCA9
#define PI_ESIC_K_FUNCTION_CNTRL     0xCAE

/* PFI (PCI Function Interface) Registers */
#define PFI_K_REG_MODE_CTRL          0x00000040
#define PFI_K_REG_STATUS             0x00000044
#define PFI_MODE_M_DMA_ENB           0x00000001
#define PFI_MODE_M_PDQ_INT_ENB       0x00000004
#define PFI_STATUS_M_PDQ_INT         0x00000001

/* Additional constants from driver source */
#define PI_PSTATUS_M_CMD_REQ_PENDING 0x04000000
#define PI_PSTATUS_M_CMD_RSP_PENDING 0x08000000
#define PI_PSTATUS_M_SMT_HOST_PENDING 0x20000000
#define PI_PSTATUS_M_UNSOL_PENDING   0x10000000
#define PI_PSTATUS_M_XMT_DATA_PENDING 0x40000000
#define PI_PSTATUS_M_RCV_DATA_PENDING 0x80000000
#define PI_PSTATUS_M_TYPE_0_PENDING  0x02000000
#define PI_PSTATUS_M_STATE           0x00000700
#define PI_PSTATUS_V_STATE           8
#define PI_PSTATUS_V_HALT_ID         0
#define PI_PSTATUS_M_HALT_ID         0x000000FF

#define PI_TYPE_0_STAT_M_XMT_FLUSH   0x00000008
#define PI_TYPE_0_STAT_M_STATE_CHANGE 0x00000010
#define PI_TYPE_0_STAT_M_NXM         0x00000004
#define PI_TYPE_0_STAT_M_BUS_PAR_ERR 0x00000001
#define PI_TYPE_0_STAT_M_PM_PAR_ERR  0x00000002

#define PI_HOST_INT_K_DISABLE_ALL_INTS 0x00000000
#define PI_HOST_INT_K_ENABLE_DEF_INTS 0xC000001F
#define PI_HOST_INT_K_ACK_ALL_TYPE_0  0xFFFFFFFF

#define PI_PCTRL_M_SUB_CMD           0x0001
#define PI_PCTRL_M_MLA               0x0008
#define PI_PCTRL_M_CONS_BLOCK        0x0040
#define PI_PCTRL_M_INIT              0x0100
#define PI_PCTRL_M_XMT_DATA_FLUSH_DONE 0x0200
#define PI_PCTRL_M_BLAST_FLASH       0x4000
#define PI_PCTRL_M_CMD_ERROR         0x8000

#define PI_RESET_M_ASSERT_RESET      1

#define PI_ALIGN_K_DESC_BLK          8192
#define PI_ALIGN_K_CMD_RSP_BUFF      128
#define PI_ALIGN_K_RCV_DATA_BUFF     128
#define PI_CMD_REQ_K_SIZE_MAX        512
#define PI_CMD_RSP_K_SIZE_MAX        512
#define PI_CMD_REQ_K_NUM_ENTRIES     16
#define PI_CMD_RSP_K_NUM_ENTRIES     16
#define PI_RCV_DATA_K_NUM_ENTRIES    256
#define PI_XMT_DATA_K_NUM_ENTRIES    256
#define PI_SMT_HOST_K_NUM_ENTRIES    64
#define PI_UNSOL_K_NUM_ENTRIES       16

#define PI_ITEM_K_EOL                0x00
#define PI_ITEM_K_MAC_T_REQ          0x29
#define PI_ITEM_K_FDX_ENB_DIS        0x2C
#define PI_ITEM_K_FLUSH_TIME         0x20
#define PI_ITEM_K_GROUP_PROM         0x08
#define PI_ITEM_K_IND_GROUP_PROM     0x07
#define PI_ITEM_K_BROADCAST          0x09

#define PI_CMD_K_START               0x00
#define PI_CMD_K_CHARS_SET           0x03
#define PI_CMD_K_SNMP_SET            0x0E
#define PI_CMD_K_CNTRS_GET           0x05
#define PI_CMD_K_SMT_MIB_GET         0x10
#define PI_CMD_K_ADDR_FILTER_SET     0x07
#define PI_CMD_K_FILTERS_SET         0x01
#define PI_SUB_CMD_K_PDQ_REV_GET     0x0004
#define PI_SUB_CMD_K_BURST_SIZE_SET  0x0002

#define PI_PDATA_A_INIT_M_BSWAP_INIT 0x00030000
#define PI_PDATA_A_MLA_K_LO          0
#define PI_PDATA_A_MLA_K_HI          1

#define PI_STATE_K_RESET             1
#define PI_STATE_K_DMA_UNAVAIL       2
#define PI_STATE_K_DMA_AVAILABLE     3
#define PI_STATE_K_LINK_AVAIL        4
#define PI_STATE_K_HALTED            0
#define PI_STATE_K_UPGRADE           5

/* Descriptor block offsets calculated from PI_DESCR_BLOCK layout */
#define DESC_CMD_REQ_OFFSET \
    (256 * 8 + 256 * 8 + 64 * 8 + 16 * 8 + 16 * 8)  /* 0x1300 */
#define DESC_CMD_RSP_OFFSET \
    (256 * 8 + 256 * 8 + 64 * 8 + 16 * 8)            /* 0x1280 */

/* Consumer block offsets from PI_CONSUMER_BLOCK */
#define CONS_CMD_REQ_OFFSET    (7 * 4)   /* 28 */
#define CONS_CMD_RSP_OFFSET    (6 * 4)   /* 24 */

typedef uint32_t PI_UINT32;
typedef struct { PI_UINT32 ms; PI_UINT32 ls; } PI_CNTR;
typedef struct { PI_UINT32 long_0; PI_UINT32 long_1; } PI_XMT_DESCR;
typedef struct { PI_UINT32 long_0; PI_UINT32 long_1; } PI_RCV_DESCR;
typedef struct { PI_UINT32 lwrd_0; PI_UINT32 lwrd_1; } PI_LAN_ADDR;
typedef struct { PI_UINT32 octet_7_4; PI_UINT32 octet_3_0; } PI_STATION_ID;

typedef struct {
    PI_RCV_DESCR  rcv_data[256];
    PI_XMT_DESCR  xmt_data[256];
    PI_RCV_DESCR  smt_host[64];
    PI_RCV_DESCR  unsol[16];
    PI_RCV_DESCR  cmd_rsp[16];
    PI_XMT_DESCR  cmd_req[16];
} PI_DESCR_BLOCK;

typedef struct {
    volatile PI_UINT32 xmt_rcv_data;
    volatile PI_UINT32 reserved_1;
    volatile PI_UINT32 smt_host;
    volatile PI_UINT32 reserved_2;
    volatile PI_UINT32 unsol;
    volatile PI_UINT32 reserved_3;
    volatile PI_UINT32 cmd_rsp;
    volatile PI_UINT32 reserved_4;
    volatile PI_UINT32 cmd_req;
    volatile PI_UINT32 reserved_5;
} PI_CONSUMER_BLOCK;

/* Device State */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* PCI resources */
    MemoryRegion bar_mmio;
    MemoryRegion bar_pio;

    /* Shadow Registers (PDQ/PFI) */
    uint32_t port_data_a;
    uint32_t port_data_b;
    uint32_t host_data;
    uint32_t type_0_status;
    uint32_t host_int_enb;
    uint32_t cmd_rsp_prod;
    uint32_t cmd_req_prod;
    uint32_t type_2_prod;
    uint32_t pfi_mode_ctrl;
    uint32_t pfi_status;

    /* Internal state */
    uint32_t port_status;       /* cached port status register value */
    hwaddr  cons_block_phys;
    hwaddr  descr_block_phys;
    uint8_t mac_addr[6];
    uint8_t cmd_req_cons;       /* adapter's internal CMD req consumer index */
    uint8_t cmd_rsp_cons;       /* adapter's internal CMD rsp consumer index */
    QEMUTimer *dma_timer;       /* delayed DMA processing */
};

/* Default MAC address (00:00:F8:12:34:56) */
static const uint8_t default_mac[6] = { 0x00, 0x00, 0xF8, 0x12, 0x34, 0x56 };

static void defxx_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    bool pdq_int_pending = (s->pfi_status & PFI_STATUS_M_PDQ_INT) &&
                           (s->pfi_mode_ctrl & PFI_MODE_M_PDQ_INT_ENB);
    if (pdq_int_pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static void defxx_dma_timer(void *opaque)
{
    PCIBaseState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);

    while (s->cmd_req_cons < s->cmd_req_prod) {
        /* Read cmd_req descriptor */
        PI_XMT_DESCR req_desc;
        hwaddr req_desc_addr = s->descr_block_phys + DESC_CMD_REQ_OFFSET +
                               (hwaddr)(s->cmd_req_cons) * sizeof(PI_XMT_DESCR);
        pci_dma_read(pdev, req_desc_addr, &req_desc, sizeof(req_desc));

        /* Read cmd_req buffer (512 bytes) */
        uint8_t req_buf[PI_CMD_REQ_K_SIZE_MAX];
        pci_dma_read(pdev, (dma_addr_t)req_desc.long_1, req_buf, sizeof(req_buf));

        /* Read cmd_rsp descriptor to get response buffer address */
        PI_RCV_DESCR rsp_desc;
        hwaddr rsp_desc_addr = s->descr_block_phys + DESC_CMD_RSP_OFFSET +
                               (hwaddr)(s->cmd_rsp_cons) * sizeof(PI_RCV_DESCR);
        pci_dma_read(pdev, rsp_desc_addr, &rsp_desc, sizeof(rsp_desc));

        /* For simplicity, echo the request to the response buffer */
        pci_dma_write(pdev, (dma_addr_t)rsp_desc.long_1, req_buf, sizeof(req_buf));

        /* Advance internal consumer indices */
        s->cmd_req_cons++;
        s->cmd_rsp_cons++;

        /* Update consumer block in guest memory */
        pci_dma_write(pdev,
                      s->cons_block_phys + CONS_CMD_REQ_OFFSET,
                      &s->cmd_req_cons, 1);
        pci_dma_write(pdev,
                      s->cons_block_phys + CONS_CMD_RSP_OFFSET,
                      &s->cmd_rsp_cons, 1);
    }
}

static void defxx_do_port_ctrl_command(PCIBaseState *s, uint32_t cmd)
{
    /* cmd always has PI_PCTRL_M_CMD_ERROR set; we clear it on completion */
    uint32_t command = cmd & ~PI_PCTRL_M_CMD_ERROR;

    switch (command) {
    case PI_PCTRL_M_SUB_CMD:
        if (s->port_data_a == PI_SUB_CMD_K_PDQ_REV_GET) {
            s->host_data = 3;   /* Emulate PDQ rev. 3 */
        } else if (s->port_data_a == PI_SUB_CMD_K_BURST_SIZE_SET) {
            /* Nothing to do */
        }
        break;
    case PI_PCTRL_M_CONS_BLOCK:
        s->cons_block_phys = s->port_data_a;
        break;
    case PI_PCTRL_M_INIT:
        /* Mask off lower 3 bits (aligned) */
        s->descr_block_phys = s->port_data_a & ~0x7;
        s->port_status = (PI_STATE_K_DMA_AVAILABLE << PI_PSTATUS_V_STATE);
        break;
    case PI_PCTRL_M_MLA:
        if (s->port_data_a == PI_PDATA_A_MLA_K_LO) {
            s->host_data = (uint32_t)s->mac_addr[3] << 24 |
                           (uint32_t)s->mac_addr[2] << 16 |
                           (uint32_t)s->mac_addr[1] << 8  |
                           (uint32_t)s->mac_addr[0];
        } else if (s->port_data_a == PI_PDATA_A_MLA_K_HI) {
            s->host_data = ((uint32_t)s->mac_addr[5] << 8) |
                           (uint32_t)s->mac_addr[4];
        }
        break;
    case PI_PCTRL_M_XMT_DATA_FLUSH_DONE:
        /* Acknowledge only */
        break;
    case PI_PCTRL_M_BLAST_FLASH:
        /* Not implemented; just ack */
        break;
    default:
        break;
    }
}

static uint64_t defxx_reg_read(PCIBaseState *s, hwaddr addr, unsigned size)
{
    /* The driver always does 32-bit accesses */
    switch (addr) {
    case PI_PDQ_K_REG_HOST_DATA:
        return s->host_data;
    case PI_PDQ_K_REG_PORT_CTRL:
        return 0;               /* always ready (CMD_ERROR cleared) */
    case PI_PDQ_K_REG_PORT_DATA_A:
        return s->port_data_a;
    case PI_PDQ_K_REG_PORT_DATA_B:
        return s->port_data_b;
    case PI_PDQ_K_REG_PORT_STATUS:
        return s->port_status;
    case PI_PDQ_K_REG_TYPE_0_STATUS:
        return s->type_0_status;
    case PI_PDQ_K_REG_HOST_INT_ENB:
        return s->host_int_enb;
    case PI_PDQ_K_REG_TYPE_2_PROD:
        return s->type_2_prod;
    case PI_PDQ_K_REG_CMD_RSP_PROD:
        return s->cmd_rsp_prod;
    case PI_PDQ_K_REG_CMD_REQ_PROD:
        return s->cmd_req_prod;
    case PFI_K_REG_MODE_CTRL:
        return s->pfi_mode_ctrl;
    case PFI_K_REG_STATUS:
        return s->pfi_status;
    default:
        return 0;
    }
}

static void defxx_reg_write(PCIBaseState *s, hwaddr addr, uint64_t val, unsigned size)
{
    switch (addr) {
    case PI_PDQ_K_REG_PORT_RESET:
        if (val & PI_RESET_M_ASSERT_RESET) {
            /* Assert reset: set state to DMA_UNAVAILABLE, clear pending */
            s->port_status = (PI_STATE_K_DMA_UNAVAIL << PI_PSTATUS_V_STATE);
            s->type_0_status = 0;
            s->pfi_status &= ~PFI_STATUS_M_PDQ_INT;
            s->cmd_req_cons = 0;
            s->cmd_rsp_cons = 0;
            defxx_update_irq(s);
        }
        /* Deassert: no action; driver will wait for state change separately */
        break;
    case PI_PDQ_K_REG_PORT_DATA_A:
        s->port_data_a = (uint32_t)val;
        break;
    case PI_PDQ_K_REG_PORT_DATA_B:
        s->port_data_b = (uint32_t)val;
        break;
    case PI_PDQ_K_REG_PORT_CTRL:
        defxx_do_port_ctrl_command(s, (uint32_t)val);
        break;
    case PI_PDQ_K_REG_TYPE_0_STATUS:
        s->type_0_status &= ~val;   /* W1C */
        break;
    case PI_PDQ_K_REG_HOST_INT_ENB:
        s->host_int_enb = (uint32_t)val;
        break;
    case PI_PDQ_K_REG_TYPE_2_PROD:
        s->type_2_prod = (uint32_t)val;
        break;
    case PI_PDQ_K_REG_CMD_RSP_PROD:
        s->cmd_rsp_prod = (uint32_t)val;
        break;
    case PI_PDQ_K_REG_CMD_REQ_PROD:
        s->cmd_req_prod = (uint32_t)val;
        /* Schedule DMA processing immediately */
        timer_mod(s->dma_timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL));
        break;
    case PFI_K_REG_MODE_CTRL:
        s->pfi_mode_ctrl = (uint32_t)val;
        defxx_update_irq(s);
        break;
    case PFI_K_REG_STATUS:
        s->pfi_status &= ~(uint32_t)val;   /* W1C */
        defxx_update_irq(s);
        break;
    default:
        break;
    }
}

static uint64_t defxx_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return defxx_reg_read(s, addr, size);
}

static void defxx_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    defxx_reg_write(s, addr, val, size);
}

static uint64_t defxx_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    return defxx_reg_read(s, addr, size);
}

static void defxx_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    defxx_reg_write(s, addr, val, size);
}

static const MemoryRegionOps defxx_mmio_ops = {
    .read = defxx_mmio_read,
    .write = defxx_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps defxx_pio_ops = {
    .read = defxx_pio_read,
    .write = defxx_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void defxx_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    pci_device_reset(PCI_DEVICE(dev));

    /* Reset registers to power-on defaults */
    s->port_status = (PI_STATE_K_DMA_UNAVAIL << PI_PSTATUS_V_STATE);
    s->host_data = 0;
    s->type_0_status = 0;
    s->host_int_enb = PI_HOST_INT_K_DISABLE_ALL_INTS;
    s->pfi_mode_ctrl = 0;
    s->pfi_status = 0;
    s->cmd_req_prod = 0;
    s->cmd_rsp_prod = 0;
    s->type_2_prod = 0;
    s->cmd_req_cons = 0;
    s->cmd_rsp_cons = 0;
    s->cons_block_phys = 0;
    s->descr_block_phys = 0;
    memcpy(s->mac_addr, default_mac, 6);
}

static void defxx_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Static PCI configuration */
    pci_config_set_interrupt_pin(pci_conf, 1);
    pci_config_set_vendor_id(pci_conf, VENDOR_ID);
    pci_config_set_device_id(pci_conf, DEVICE_ID);
    pci_config_set_class(pci_conf, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);

    /* Enable MSI (optional, but the driver uses INTx; we support INTx) */
    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }

    /* Initialize DMA timer */
    s->dma_timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, defxx_dma_timer, s);

    /* Set up BARs */
    memory_region_init_io(&s->bar_mmio, OBJECT(s), &defxx_mmio_ops, s,
                          "defxx-mmio", 4096);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar_mmio);

    memory_region_init_io(&s->bar_pio, OBJECT(s), &defxx_pio_ops, s,
                          "defxx-pio", 4096);
    pci_register_bar(pdev, 1, PCI_BASE_ADDRESS_SPACE_IO, &s->bar_pio);

    /* Reset state */
    defxx_reset(DEVICE(s));
}

static void defxx_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    timer_del(s->dma_timer);
    timer_free(s->dma_timer);
    msi_uninit(pdev);
}

static const VMStateDescription vmstate_defxx = {
    .name = "defxx_pci",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, PCIBaseState),
        VMSTATE_UINT32(port_data_a, PCIBaseState),
        VMSTATE_UINT32(port_data_b, PCIBaseState),
        VMSTATE_UINT32(host_data, PCIBaseState),
        VMSTATE_UINT32(type_0_status, PCIBaseState),
        VMSTATE_UINT32(host_int_enb, PCIBaseState),
        VMSTATE_UINT32(cmd_rsp_prod, PCIBaseState),
        VMSTATE_UINT32(cmd_req_prod, PCIBaseState),
        VMSTATE_UINT32(type_2_prod, PCIBaseState),
        VMSTATE_UINT32(pfi_mode_ctrl, PCIBaseState),
        VMSTATE_UINT32(pfi_status, PCIBaseState),
        VMSTATE_UINT32(port_status, PCIBaseState),
        VMSTATE_UINT64(cons_block_phys, PCIBaseState),
        VMSTATE_UINT64(descr_block_phys, PCIBaseState),
        VMSTATE_UINT8_ARRAY(mac_addr, PCIBaseState, 6),
        VMSTATE_UINT8(cmd_req_cons, PCIBaseState),
        VMSTATE_UINT8(cmd_rsp_cons, PCIBaseState),
        VMSTATE_END_OF_LIST()
    }
};

static void defxx_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->realize = defxx_realize;
    k->exit = defxx_uninit;
    dc->reset = defxx_reset;
    dc->vmsd = &vmstate_defxx;
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static void defxx_register_types(void)
{
    static InterfaceInfo interfaces[] = {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    };

    static const TypeInfo defxx_info = {
        .name = TYPE_PCIBASE_DEVICE,
        .parent = TYPE_PCI_DEVICE,
        .instance_size = sizeof(PCIBaseState),
        .class_init = defxx_class_init,
        .interfaces = interfaces,
    };

    type_register_static(&defxx_info);
}

type_init(defxx_register_types);
