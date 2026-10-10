/*
 * QEMU PCI device model for DEFXX FDDI adapter
 * Phase 2: Functional behavior (updated with basic DMA/consumer block emulation)
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
#include "hw/pci/pci_ids.h"

#define PCI_DEVICE_ID_DEC_FDDI 0x000f

#define TYPE_PCIBASE_DEVICE "defxx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define DEFXX_PCI_VENDOR_ID   PCI_VENDOR_ID_DEC
#define DEFXX_PCI_DEVICE_ID   PCI_DEVICE_ID_DEC_FDDI
#define DEFXX_PCI_CLASS_ID    PCI_CLASS_NETWORK_FDDI

/* Selected register offsets and constants explicitly defined in driver */
#define PI_RCV_DATA_K_SIZE_MAX            4608
#define PI_ESIC_K_MEM_ADD_HI_CMP_0       0xC88
#define PI_ESIC_K_MEM_ADD_HI_CMP_1       0xC89
#define PI_ESIC_K_ESIC_CSR               0xC80
#define PI_ESIC_K_MEM_ADD_LO_CMP_2       0xC90
#define PI_MEM_ADD_MASK_M                0x3ff
#define PI_TC_K_CSR_OFFSET               0x100000
#define PI_ESIC_K_CSR_IO_LEN             0x40
#define PI_ESIC_K_ESIC_CSR_LEN           0x40
#define PI_DEFEA_K_BURST_HOLDOFF         0x040
#define PI_ESIC_K_MEM_ADD_HI_CMP_2       0xC8A
#define PI_ESIC_K_MEM_ADD_LO_CMP_0       0xC8E
#define PI_ESIC_K_MEM_ADD_LO_CMP_1       0xC8F
#define PI_ESIC_K_BURST_HOLDOFF_LEN      0x04
#define PI_TC_K_CSR_LEN                  0x40
#define PI_ALIGN_K_DESC_BLK              8192
#define DFX_K_SUCCESS                    0
#define PI_CMD_REQ_K_SIZE_MAX            512
#define PI_CMD_RSP_K_SIZE_MAX            512
#define PI_CONFIG_STAT_0_M_IRQ           0x03
#define PI_IO_CMP_M_SLOT                 0xf0
#define PFI_K_LAT_TIMER_DEF              0x88
#define PFI_MODE_M_PDQ_INT_ENB           0x00000004
#define PI_ESIC_K_IO_ADD_MASK_0_0        0xC99
#define PI_ESIC_K_IO_ADD_MASK_0_1        0xC9A
#define PI_ESIC_K_IO_CONFIG_STAT_0       0xCA9
#define PI_FUNCTION_CNTRL_M_IOCS1        0x02
#define PI_CONFIG_STAT_0_IRQ_K_9         0
#define PI_FUNCTION_CNTRL_M_MEMCS1       0x20
#define PI_ESIC_K_IO_ADD_MASK_1_1        0xC9C
#define PI_FUNCTION_CNTRL_M_IOCS0        0x01
#define PI_CONFIG_STAT_0_IRQ_K_11        2
#define PI_DEFEA_K_CSR_IO                0x000
#define PI_CONFIG_STAT_0_M_INT_ENB       0x08
#define PI_ESIC_K_IO_ADD_CMP_0_0         0xC91
#define PI_ESIC_K_IO_ADD_MASK_1_0        0xC9B
#define PI_CONFIG_STAT_0_IRQ_K_15        3
#define PI_ESIC_K_IO_ADD_CMP_0_1         0xC92
#define PI_ESIC_K_FUNCTION_CNTRL         0xCAE
#define PFI_K_REG_MODE_CTRL              0x00000040
#define PI_ESIC_K_SLOT_CNTRL             0xC84
#define PFI_MODE_M_DMA_ENB               0x00000001
#define PFI_K_LAT_TIMER_MIN              0x20
#define PI_ESIC_K_IO_ADD_CMP_1_1         0xC94
#define PI_CONFIG_STAT_0_IRQ_K_10        1
#define PI_ESIC_K_IO_ADD_CMP_1_0         0xC93
#define PI_BURST_HOLDOFF_M_MEM_MAP       0x01
#define PI_SLOT_CNTRL_M_ENB              0x01
#define PI_CONFIG_STAT_0_V_IRQ           0
#define PI_PDATA_B_DMA_BURST_SIZE_32     3
#define PI_PDATA_B_DMA_BURST_SIZE_16     2
#define PI_SNMP_K_FALSE                  2
#define PI_PDATA_B_DMA_BURST_SIZE_8      1
#define PI_SUB_CMD_K_PDQ_REV_GET         0x0004
#define PI_PCTRL_M_SUB_CMD               0x0001
#define DEFEA_PROD_ID_2                  0x0230A310
#define PI_HOST_INT_K_DISABLE_ALL_INTS   0x00000000
#define PI_PDATA_A_RESET_M_SKIP_ST       0x00000004
#define PI_PDATA_B_DMA_BURST_SIZE_DEF    PI_PDATA_B_DMA_BURST_SIZE_16
#define PI_PDATA_A_MLA_K_LO              0
#define PI_PDATA_A_MLA_K_HI              1
#define PI_PDQ_K_REG_HOST_INT_ENB        0x0000001C
#define PI_PCTRL_M_MLA                   0x0008
#define RCV_BUFS_DEF                     8
#define DFX_K_FAILURE                    1
#define PI_HOST_INT_K_ENABLE_DEF_INTS    0xC000001F
#define PI_ITEM_K_FDX_ENB_DIS            0x2C
#define PI_ITEM_K_MAC_T_REQ              0x29
#define PI_PDQ_K_REG_TYPE_0_STATUS       0x00000018
#define PI_CMD_K_CHARS_SET               0x03
#define PI_CMD_K_START                   0x00
#define PI_CMD_K_SNMP_SET                0x0E
#define PI_ITEM_K_EOL                    0x00
#define PI_PCTRL_M_CONS_BLOCK            0x0040
#define PI_HOST_INT_K_ACK_ALL_TYPE_0     0x000000FF
#define PI_ITEM_K_FLUSH_TIME             0x20
#define PI_SUB_CMD_K_BURST_SIZE_SET      0x0002
#define PI_PCTRL_M_INIT                  0x0100
#define PI_PDATA_A_INIT_M_BSWAP_INIT     0x000000002
#define PI_FSTATE_K_BLOCK                0
#define PI_PSTATUS_M_HALT_ID             0x000000FF
#define PI_HALT_ID_K_HW_FAULT            4
#define PI_HALT_ID_K_DMA_ERROR           6
#define PI_PDQ_K_REG_PORT_STATUS         0x00000014
#define PI_HALT_ID_K_HOST_DIR_HALT       2
#define PI_HALT_ID_K_BUS_EXCEPTION       8
#define PI_PSTATUS_V_HALT_ID             0
#define PI_HALT_ID_K_SW_FAULT            3
#define PI_HALT_ID_K_PC_TRACE            5
#define PI_HALT_ID_K_IMAGE_CRC_ERROR     7
#define PI_HALT_ID_K_SELFTEST_TIMEOUT    0
#define PI_HALT_ID_K_PARITY_ERROR        1
#define PI_TYPE_0_STAT_M_BUS_PAR_ERR     0x00000001
#define PI_TYPE_0_STAT_M_STATE_CHANGE    0x00000010
#define PI_STATE_K_LINK_AVAIL            4
#define PI_PCTRL_M_XMT_DATA_FLUSH_DONE   0x0200
#define PI_TYPE_0_STAT_M_PM_PAR_ERR      0x00000002
#define PI_TYPE_0_STAT_M_NXM             0x00000004
#define PI_K_TRUE                        1
#define PI_STATE_K_HALTED                6
#define PI_TYPE_0_STAT_M_XMT_FLUSH       0x00000008
#define PI_K_FALSE                       0
#define PI_PDQ_K_REG_TYPE_2_PROD         0x00000024
#define PI_PSTATUS_M_TYPE_0_PENDING      0x02000000
#define PI_PSTATUS_M_CMD_REQ_PENDING     0x04000000
#define PI_PSTATUS_M_UNSOL_PENDING       0x10000000
#define PFI_K_REG_STATUS                 0x00000044
#define PI_PSTATUS_M_SMT_HOST_PENDING    0x20000000
#define PI_CONFIG_STAT_0_M_PEND          0x80
#define PI_PSTATUS_M_XMT_DATA_PENDING    0x40000000
#define PFI_STATUS_M_PDQ_INT             0x00000010
#define PI_PSTATUS_M_CMD_RSP_PENDING     0x08000000
#define PI_CONFIG_STAT_0_M_PEND          0x80
#define PI_PSTATUS_M_RCV_DATA_PENDING    0x80000000
#define PI_CMD_K_CNTRS_GET               0x05
#define PI_CMD_K_SMT_MIB_GET             0x10
#define PI_FSTATE_K_PASS                 1
#define PI_CMD_ADDR_FILTER_K_SIZE        62
#define PI_CMD_K_ADDR_FILTER_SET         0x07
#define PI_ITEM_K_IND_GROUP_PROM         0x07
#define PI_ITEM_K_GROUP_PROM             0x08
#define PI_CMD_K_FILTERS_SET             0x01
#define PI_ITEM_K_BROADCAST              0x09
#define PI_STATE_K_RESET                 0
#define PI_XMT_DESCR_M_EOP               0x40000000
#define DFX_K_HW_TIMEOUT                 3
#define PI_XMT_DESCR_M_SOP               0x80000000
#define PI_CMD_RSP_K_NUM_ENTRIES         16
#define DFX_K_OUTSTATE                   2
#define PI_STATE_K_DMA_UNAVAIL           2
#define PI_RCV_DESCR_M_SOP               0x80000000
#define PI_PDQ_K_REG_CMD_REQ_PROD        0x0000002C
#define PI_ALIGN_K_CMD_RSP_BUFF          128
#define PI_XMT_DESCR_V_SEG_LEN           16
#define PI_RCV_DESCR_V_SEG_LEN           23
#define PI_STATE_K_UPGRADE               1
#define PI_CMD_REQ_K_NUM_ENTRIES         16
#define PI_PDQ_K_REG_CMD_RSP_PROD        0x00000028
#define PI_PDQ_K_REG_PORT_DATA_A         0x0000000C
#define PI_PDQ_K_REG_PORT_DATA_B         0x00000010
#define PI_PCTRL_M_CMD_ERROR             0x8000
#define PI_PDQ_K_REG_PORT_CTRL           0x00000008
#define PI_PCTRL_M_BLAST_FLASH           0x4000
#define PI_PDQ_K_REG_HOST_DATA           0x00000004
#define PI_RESET_M_ASSERT_RESET           1
#define PI_PDQ_K_REG_PORT_RESET          0x00000000
#define PI_PSTATUS_M_STATE               0x00000700
#define PI_PSTATUS_V_STATE               8
#define PI_RCV_DATA_K_NUM_ENTRIES        256
#define PI_ALIGN_K_RCV_DATA_BUFF         128
#define RCV_BUFF_K_PADDING               4
#define PI_FMC_DESCR_V_LEN               0
#define PI_FMC_DESCR_M_RCC_FLUSH         0x00200000
#define PI_FMC_DESCR_M_RCC_CRC           0x00100000
#define RCV_BUFF_K_DA                    8
#define RCV_BUFF_K_DESCR                 0
#define PI_FMC_DESCR_M_LEN               0x00001FFF
#define DFX_PRH0_BYTE                    0x20
#define DFX_PRH2_BYTE                    0x00
#define DFX_PRH1_BYTE                    0x38
#define PI_CONS_V_XMT_INDEX              16
#define PI_CONS_M_XMT_INDEX              0x00FF0000

#define PI_SMT_HOST_K_NUM_ENTRIES            64
#define PI_UNSOL_K_NUM_ENTRIES               16
#define PI_XMT_DATA_K_NUM_ENTRIES            256

#define FDDI 1

/* PFI register bit we fabricate for IRQ cause inside our model */
#define PFI_STATUS_INT_BIT  PFI_STATUS_M_PDQ_INT

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

/* Minimal internal representations of PDQ visible DMA layout used by driver */
typedef struct PI_RCV_DESCR_hw {
    uint32_t long_0;
    uint32_t long_1;
} PI_RCV_DESCR_hw;

typedef struct PI_XMT_DESCR_hw {
    uint32_t long_0;
    uint32_t long_1;
} PI_XMT_DESCR_hw;

typedef struct PI_CONSUMER_BLOCK_hw {
    uint32_t xmt_rcv_data;
    uint32_t reserved_1;
    uint32_t smt_host;
    uint32_t reserved_2;
    uint32_t unsol;
    uint32_t reserved_3;
    uint32_t cmd_rsp;
    uint32_t reserved_4;
    uint32_t cmd_req;
    uint32_t reserved_5;
} PI_CONSUMER_BLOCK_hw;

/* Type 1 and Type 2 producer register views from driver */
typedef union PI_TYPE_1_PROD_REG_hw {
    uint32_t lword;
    struct {
        uint8_t prod;
        uint8_t comp;
        uint8_t mbz_1;
        uint8_t mbz_2;
    } index;
} PI_TYPE_1_PROD_REG_hw;

typedef union PI_TYPE_2_PROD_REG_hw {
    uint32_t lword;
    struct {
        uint8_t rcv_prod;
        uint8_t xmt_prod;
        uint8_t rcv_comp;
        uint8_t xmt_comp;
    } index;
} PI_TYPE_2_PROD_REG_hw;

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
    struct {
        uint32_t port_reset;      /* PI_PDQ_K_REG_PORT_RESET   0x00 */
        uint32_t host_data;       /* PI_PDQ_K_REG_HOST_DATA    0x04 */
        uint32_t port_ctrl;       /* PI_PDQ_K_REG_PORT_CTRL    0x08 */
        uint32_t port_data_a;     /* PI_PDQ_K_REG_PORT_DATA_A  0x0C */
        uint32_t port_data_b;     /* PI_PDQ_K_REG_PORT_DATA_B  0x10 */
        uint32_t port_status;     /* PI_PDQ_K_REG_PORT_STATUS  0x14 */
        uint32_t type_0_status;   /* PI_PDQ_K_REG_TYPE_0_STATUS 0x18 */
        uint32_t host_int_enb;    /* PI_PDQ_K_REG_HOST_INT_ENB 0x1C */
        uint32_t type_2_prod;     /* PI_PDQ_K_REG_TYPE_2_PROD  0x24 */
        uint32_t cmd_rsp_prod;    /* PI_PDQ_K_REG_CMD_RSP_PROD 0x28 */
        uint32_t cmd_req_prod;    /* PI_PDQ_K_REG_CMD_REQ_PROD 0x2C */

        /* PFI PCI-side registers used by driver */
        uint32_t pfi_mode_ctrl;   /* PFI_K_REG_MODE_CTRL 0x40 */
        uint32_t pfi_status;      /* PFI_K_REG_STATUS    0x44 */
    } regs;

    /* Minimal state for PDQ side emulation */
    bool pdq_cmd_error_busy;  /* port_ctrl CMD_ERROR bit busy */
    uint32_t pdq_state;       /* PI_STATE_K_* */

    /* Basic DMA-visible addresses captured from INIT/CONS_BLOCK operations */
    dma_addr_t descr_block_phys;
    dma_addr_t cons_block_phys;

    /* Shadow of Type 1 prod registers used by driver logic */
    uint32_t cmd_req_prod_reg;
    uint32_t cmd_rsp_prod_reg;
    uint32_t type2_prod_reg;
};

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);

    /* IRQ is signaled to host when PFI_STATUS_M_PDQ_INT is set and
     * PFI_MODE_M_PDQ_INT_ENB is enabled. Driver only checks status bit,
     * but will not see interrupt if line is not raised. */
    bool enabled = (s->regs.pfi_mode_ctrl & PFI_MODE_M_PDQ_INT_ENB) != 0;
    bool pending = (s->regs.pfi_status & PFI_STATUS_INT_BIT) != 0;

    if (enabled && pending) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0x00000000;

    if (size != 4) {
        /* Driver only uses 32-bit accesses */
        return 0xFFFFFFFFu;
    }

    switch (addr) {
    case PI_PDQ_K_REG_PORT_RESET: /* 0x00 */
        val = s->regs.port_reset;
        break;
    case PI_PDQ_K_REG_HOST_DATA: /* 0x04 */
        val = s->regs.host_data;
        break;
    case PI_PDQ_K_REG_PORT_CTRL: /* 0x08 */
        val = s->regs.port_ctrl;
        break;
    case PI_PDQ_K_REG_PORT_DATA_A: /* 0x0C */
        val = s->regs.port_data_a;
        break;
    case PI_PDQ_K_REG_PORT_DATA_B: /* 0x10 */
        val = s->regs.port_data_b;
        break;
    case PI_PDQ_K_REG_PORT_STATUS: /* 0x14 */
        val = s->regs.port_status;
        break;
    case PI_PDQ_K_REG_TYPE_0_STATUS: /* 0x18 */
        val = s->regs.type_0_status;
        break;
    case PI_PDQ_K_REG_HOST_INT_ENB: /* 0x1C */
        val = s->regs.host_int_enb;
        break;
    case PI_PDQ_K_REG_TYPE_2_PROD: /* 0x24 */
        val = s->regs.type_2_prod;
        break;
    case PI_PDQ_K_REG_CMD_RSP_PROD: /* 0x28 */
        val = s->regs.cmd_rsp_prod;
        break;
    case PI_PDQ_K_REG_CMD_REQ_PROD: /* 0x2C */
        val = s->regs.cmd_req_prod;
        break;
    case PFI_K_REG_MODE_CTRL: /* 0x40 */
        val = s->regs.pfi_mode_ctrl;
        break;
    case PFI_K_REG_STATUS: /* 0x44 */
        val = s->regs.pfi_status;
        break;
    default:
        /* Unimplemented offsets return 0 */
        val = 0;
        break;
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v32 = (uint32_t)val;

    if (size != 4) {
        return;
    }

    switch (addr) {
    case PI_PDQ_K_REG_PORT_RESET: /* 0x00 */
        s->regs.port_reset = v32;
        if (v32 & PI_RESET_M_ASSERT_RESET) {
            /* Reset asserted: move PDQ to RESET state */
            s->pdq_state = PI_STATE_K_RESET;
            /* While reset, clear port status and type0 status */
            s->regs.port_status = 0;
            s->regs.type_0_status = 0;
        } else {
            /* Deassert reset: PDQ goes to DMA_UNAVAIL, as expected by
             * dfx_hw_dma_uninit() which waits for this state. */
            s->pdq_state = PI_STATE_K_DMA_UNAVAIL;
            s->regs.port_status &= ~PI_PSTATUS_M_STATE;
            s->regs.port_status |= (PI_STATE_K_DMA_UNAVAIL << PI_PSTATUS_V_STATE);
        }
        break;

    case PI_PDQ_K_REG_HOST_DATA: /* 0x04 */
        s->regs.host_data = v32;
        break;

    case PI_PDQ_K_REG_PORT_CTRL: /* 0x08 */
        s->regs.port_ctrl = v32;
        /* Emulate port control commands that driver uses via
         * dfx_hw_port_ctrl_req(). CMD_ERROR bit indicates busy when set. */
        if (v32 & PI_PCTRL_M_CMD_ERROR) {
            /* Begin command; mark busy and then immediately complete
             * by clearing CMD_ERROR and possibly providing host_data. */
            s->pdq_cmd_error_busy = true;

            /* For commands, we only implement what the driver uses.
             * Command code is in low bits masked by PI_PCTRL_M_CMD_ERROR. */
            uint32_t cmd = v32 & ~PI_PCTRL_M_CMD_ERROR;
            switch (cmd) {
            case PI_PCTRL_M_SUB_CMD:
                /* Sub-commands distinguished by port_data_a */
                if (s->regs.port_data_a == PI_SUB_CMD_K_PDQ_REV_GET) {
                    /* Return a non-D revision (e.g. 3) so that
                     * dfx_bus_config_check() does not trigger workaround. */
                    s->regs.host_data = 3;
                }
                break;
            case PI_PCTRL_M_MLA:
                /* Access factory MAC address. Driver calls twice with
                 * data_a = PI_PDATA_A_MLA_K_LO / HI and expects MAC
                 * returned via HOST_DATA on completion. Provide a
                 * stable but arbitrary MAC address. */
                if (s->regs.port_data_a == PI_PDATA_A_MLA_K_LO) {
                    /* Low 4 bytes of MAC address */
                    s->regs.host_data = 0x00112233U;
                } else if (s->regs.port_data_a == PI_PDATA_A_MLA_K_HI) {
                    /* High 2 bytes in low halfword of HOST_DATA */
                    s->regs.host_data = 0x00004455U & 0x0000FFFFU;
                }
                break;
            case PI_PCTRL_M_CONS_BLOCK:
                /* Set consumer block base address: we just remember it. */
                s->cons_block_phys = (dma_addr_t)s->regs.port_data_b;
                break;
            case PI_PCTRL_M_INIT:
                /* Descriptor block base address is in PORT_DATA_B. */
                s->descr_block_phys = (dma_addr_t)s->regs.port_data_b;
                /* On INIT completion, adapter transitions to DMA_UNAVAIL
                 * or further states via other commands; we keep DMA_UNAVAIL
                 * here as seen by dfx_hw_dma_uninit(). */
                s->pdq_state = PI_STATE_K_DMA_UNAVAIL;
                s->regs.port_status &= ~PI_PSTATUS_M_STATE;
                s->regs.port_status |= (PI_STATE_K_DMA_UNAVAIL << PI_PSTATUS_V_STATE);
                break;
            case PI_PCTRL_M_XMT_DATA_FLUSH_DONE:
                /* Acknowledge transmit flush, nothing to do. */
                break;
            default:
                break;
            }

            /* Immediately complete: clear CMD_ERROR (busy) bit so that
             * dfx_hw_port_ctrl_req() polling loop sees completion. */
            s->regs.port_ctrl &= ~PI_PCTRL_M_CMD_ERROR;
            s->pdq_cmd_error_busy = false;
        }
        break;

    case PI_PDQ_K_REG_PORT_DATA_A: /* 0x0C */
        s->regs.port_data_a = v32;
        break;

    case PI_PDQ_K_REG_PORT_DATA_B: /* 0x10 */
        s->regs.port_data_b = v32;
        break;

    case PI_PDQ_K_REG_TYPE_0_STATUS: /* 0x18 */
        /* Type 0 status is write-1-to-clear according to driver usage: it
         * reads then writes back same value to clear bits. */
        s->regs.type_0_status &= ~v32;
        break;

    case PI_PDQ_K_REG_HOST_INT_ENB: /* 0x1C */
        s->regs.host_int_enb = v32;
        break;

    case PI_PDQ_K_REG_TYPE_2_PROD: /* 0x24 */
        /* Driver writes combined RX/TX indices; we just shadow it. */
        s->regs.type_2_prod = v32;
        s->type2_prod_reg = v32;
        /* When producer indices are updated and interrupts enabled,
         * we can synthesize PDQ interrupt cause at PFI. */
        if (s->regs.host_int_enb != PI_HOST_INT_K_DISABLE_ALL_INTS) {
            s->regs.pfi_status |= PFI_STATUS_INT_BIT;
            pcibase_update_irq(s);
        }
        break;

    case PI_PDQ_K_REG_CMD_RSP_PROD: /* 0x28 */
        /* Shadow Type 1 producer register, used by dfx_hw_dma_cmd_req(). */
        s->regs.cmd_rsp_prod = v32;
        s->cmd_rsp_prod_reg = v32;
        break;

    case PI_PDQ_K_REG_CMD_REQ_PROD: /* 0x2C */
        /* Shadow Type 1 producer register, used by dfx_hw_dma_cmd_req(). */
        s->regs.cmd_req_prod = v32;
        s->cmd_req_prod_reg = v32;
        break;

    case PFI_K_REG_MODE_CTRL: /* 0x40 */
        /* This register enables/disables PDQ->host interrupts and DMA.
         * Interrupt enable bit is PFI_MODE_M_PDQ_INT_ENB; DMA not modeled. */
        s->regs.pfi_mode_ctrl = v32;
        pcibase_update_irq(s);
        break;

    case PFI_K_REG_STATUS: /* 0x44 */
        /* Driver writes PFI_STATUS_M_PDQ_INT to clear PDQ INT bit. */
        s->regs.pfi_status &= ~v32;
        pcibase_update_irq(s);
        break;

    default:
        /* ignore */
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;

    /* In PCI mode, driver uses MMIO via dfx_use_mmio; PIO path is for
     * EISA/TC, which we do not model here. Return 0xFF/FFFF/FFFFFFFF. */
    switch (size) {
    case 1:
        return 0xFFu;
    case 2:
        return 0xFFFFu;
    default:
        return 0xFFFFFFFFu;
    }
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
    /* Not used for PCI variant */
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

    /* Revert registers to power-on defaults */
    s->regs.port_reset = 0;
    s->regs.host_data = 0;
    s->regs.port_ctrl = 0;
    s->regs.port_data_a = 0;
    s->regs.port_data_b = 0;
    s->regs.port_status = 0;
    s->regs.type_0_status = 0;
    s->regs.host_int_enb = PI_HOST_INT_K_DISABLE_ALL_INTS;
    s->regs.type_2_prod = 0;
    s->regs.cmd_rsp_prod = 0;
    s->regs.cmd_req_prod = 0;
    s->regs.pfi_mode_ctrl = 0;
    s->regs.pfi_status = 0;

    s->pdq_cmd_error_busy = false;
    s->pdq_state = PI_STATE_K_RESET;

    s->descr_block_phys = 0;
    s->cons_block_phys = 0;
    s->cmd_req_prod_reg = 0;
    s->cmd_rsp_prod_reg = 0;
    s->type2_prod_reg = 0;

    pcibase_update_irq(s);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  DEFXX_PCI_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEFXX_PCI_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, DEFXX_PCI_CLASS_ID );
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization: driver uses BAR0 for MMIO */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "defxx-mmio";
    for (int i = 1; i < 6; i++) {
        s->bar_info[i].index = i;
        s->bar_info[i].type = BAR_TYPE_NONE;
        s->bar_info[i].size = 0;
        s->bar_info[i].name = NULL;
    }
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X used by driver in provided snippet */
    s->has_msi = false;
    s->has_msix = false;

    /* Final state initialization before the device is 'live' */
    s->regs.port_reset = 0;
    s->regs.host_data = 0;
    s->regs.port_ctrl = 0;
    s->regs.port_data_a = 0;
    s->regs.port_data_b = 0;
    s->regs.port_status = 0;
    s->regs.type_0_status = 0;
    s->regs.host_int_enb = PI_HOST_INT_K_DISABLE_ALL_INTS;
    s->regs.type_2_prod = 0;
    s->regs.cmd_rsp_prod = 0;
    s->regs.cmd_req_prod = 0;
    s->regs.pfi_mode_ctrl = 0;
    s->regs.pfi_status = 0;

    s->pdq_cmd_error_busy = false;
    s->pdq_state = PI_STATE_K_RESET;

    s->descr_block_phys = 0;
    s->cons_block_phys = 0;
    s->cmd_req_prod_reg = 0;
    s->cmd_rsp_prod_reg = 0;
    s->type2_prod_reg = 0;

    pcibase_update_irq(s);
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

    /* No dynamic resources to free in this model */
    (void)s;
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "defxx_pci",
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

