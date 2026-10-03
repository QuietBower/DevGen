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

#define TYPE_PCIBASE_DEVICE "b2c2_flexcop_pci_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x13d0
#define DEVICE_ID 0x2103

/* Register offsets from flexcop_ibi_register */
enum {
    dma1_000            = 0x000,
    dma1_004            = 0x004,
    dma1_008            = 0x008,
    dma1_00c            = 0x00c,
    dma2_010            = 0x010,
    dma2_014            = 0x014,
    dma2_018            = 0x018,
    dma2_01c            = 0x01c,

    tw_sm_c_100         = 0x100,
    tw_sm_c_104         = 0x104,
    tw_sm_c_108         = 0x108,
    tw_sm_c_10c         = 0x10c,
    tw_sm_c_110         = 0x110,

    lnb_switch_freq_200 = 0x200,
    misc_204            = 0x204,
    ctrl_208            = 0x208,
    irq_20c             = 0x20c,
    sw_reset_210        = 0x210,
    misc_214            = 0x214,
    mbox_v8_to_host_218 = 0x218,
    mbox_host_to_v8_21c = 0x21c,

    pid_filter_300      = 0x300,
    pid_filter_304      = 0x304,
    pid_filter_308      = 0x308,
    pid_filter_30c      = 0x30c,
    index_reg_310       = 0x310,
    pid_n_reg_314       = 0x314,
    mac_low_reg_318     = 0x318,
    mac_high_reg_31c    = 0x31c,

    data_tag_400        = 0x400,
    card_id_408         = 0x408,
    card_id_40c         = 0x40c,
    mac_address_418     = 0x418,
    mac_address_41c     = 0x41c,

    ci_600              = 0x600,
    pi_604              = 0x604,
    pi_608              = 0x608,
    dvb_reg_60c         = 0x60c,

    sram_ctrl_reg_700   = 0x700,
    net_buf_reg_704     = 0x704,
    cai_buf_reg_708     = 0x708,
    cao_buf_reg_70c     = 0x70c,
    media_buf_reg_710   = 0x710,
    sram_dest_reg_714   = 0x714,
    net_buf_reg_718     = 0x718,
    wan_ctrl_reg_71c    = 0x71c
};

/* Register value union for bitfield access (from flexcop_ibi_value) */
typedef union {
    uint32_t raw;

    struct {
        uint32_t dma_0start                     : 1;
        uint32_t dma_0No_update                 : 1;
        uint32_t dma_address0                   :30;
    } dma_0x0;

    struct {
        uint32_t DMA_maxpackets                 : 8;
        uint32_t dma_addr_size                  :24;
    } dma_0x4_remap;

    struct {
        uint32_t dma1timer                      : 7;
        uint32_t unused                         : 1;
        uint32_t dma_addr_size                  :24;
    } dma_0x4_read;

    struct {
        uint32_t unused                         : 1;
        uint32_t dmatimer                       : 7;
        uint32_t dma_addr_size                  :24;
    } dma_0x4_write;

    struct {
        uint32_t unused                         : 2;
        uint32_t dma_cur_addr                   :30;
    } dma_0x8;

    struct {
        uint32_t dma_1start                     : 1;
        uint32_t remap_enable                   : 1;
        uint32_t dma_address1                   :30;
    } dma_0xc;

    struct {
        uint32_t chipaddr                       : 7;
        uint32_t reserved1                      : 1;
        uint32_t baseaddr                       : 8;
        uint32_t data1_reg                      : 8;
        uint32_t working_start                  : 1;
        uint32_t twoWS_rw                       : 1;
        uint32_t total_bytes                    : 2;
        uint32_t twoWS_port_reg                 : 2;
        uint32_t no_base_addr_ack_error         : 1;
        uint32_t st_done                        : 1;
    } tw_sm_c_100;

    struct {
        uint32_t data2_reg                      : 8;
        uint32_t data3_reg                      : 8;
        uint32_t data4_reg                      : 8;
        uint32_t exlicit_stops                  : 1;
        uint32_t force_stop                     : 1;
        uint32_t unused                         : 6;
    } tw_sm_c_104;

    struct {
        uint32_t thi1                           : 6;
        uint32_t reserved1                      : 2;
        uint32_t tlo1                           : 5;
        uint32_t reserved2                      :19;
    } tw_sm_c_108;

    struct {
        uint32_t thi1                           : 6;
        uint32_t reserved1                      : 2;
        uint32_t tlo1                           : 5;
        uint32_t reserved2                      :19;
    } tw_sm_c_10c;

    struct {
        uint32_t thi1                           : 6;
        uint32_t reserved1                      : 2;
        uint32_t tlo1                           : 5;
        uint32_t reserved2                      :19;
    } tw_sm_c_110;

    struct {
        uint32_t LNB_CTLHighCount_sig           :15;
        uint32_t LNB_CTLLowCount_sig            :15;
        uint32_t LNB_CTLPrescaler_sig           : 2;
    } lnb_switch_freq_200;

    struct {
        uint32_t ACPI1_sig                      : 1;
        uint32_t ACPI3_sig                      : 1;
        uint32_t LNB_L_H_sig                    : 1;
        uint32_t Per_reset_sig                  : 1;
        uint32_t reserved                       :20;
        uint32_t Rev_N_sig_revision_hi          : 4;
        uint32_t Rev_N_sig_reserved1            : 2;
        uint32_t Rev_N_sig_caps                 : 1;
        uint32_t Rev_N_sig_reserved2            : 1;
    } misc_204;

    struct {
        uint32_t Stream1_filter_sig             : 1;
        uint32_t Stream2_filter_sig             : 1;
        uint32_t PCR_filter_sig                 : 1;
        uint32_t PMT_filter_sig                 : 1;
        uint32_t EMM_filter_sig                 : 1;
        uint32_t ECM_filter_sig                 : 1;
        uint32_t Null_filter_sig                : 1;
        uint32_t Mask_filter_sig                : 1;
        uint32_t WAN_Enable_sig                 : 1;
        uint32_t WAN_CA_Enable_sig              : 1;
        uint32_t CA_Enable_sig                  : 1;
        uint32_t SMC_Enable_sig                 : 1;
        uint32_t Per_CA_Enable_sig              : 1;
        uint32_t Multi2_Enable_sig              : 1;
        uint32_t MAC_filter_Mode_sig            : 1;
        uint32_t Rcv_Data_sig                   : 1;
        uint32_t DMA1_IRQ_Enable_sig            : 1;
        uint32_t DMA1_Timer_Enable_sig          : 1;
        uint32_t DMA2_IRQ_Enable_sig            : 1;
        uint32_t DMA2_Timer_Enable_sig          : 1;
        uint32_t DMA1_Size_IRQ_Enable_sig       : 1;
        uint32_t DMA2_Size_IRQ_Enable_sig       : 1;
        uint32_t Mailbox_from_V8_Enable_sig     : 1;
        uint32_t unused                         : 9;
    } ctrl_208;

    struct {
        uint32_t DMA1_IRQ_Status                : 1;
        uint32_t DMA1_Timer_Status              : 1;
        uint32_t DMA2_IRQ_Status                : 1;
        uint32_t DMA2_Timer_Status              : 1;
        uint32_t DMA1_Size_IRQ_Status           : 1;
        uint32_t DMA2_Size_IRQ_Status           : 1;
        uint32_t Mailbox_from_V8_Status_sig     : 1;
        uint32_t Data_receiver_error            : 1;
        uint32_t Continuity_error_flag          : 1;
        uint32_t LLC_SNAP_FLAG_set              : 1;
        uint32_t Transport_Error                : 1;
        uint32_t reserved                       :21;
    } irq_20c;

    struct {
        uint32_t reset_block_000                : 1;
        uint32_t reset_block_100                : 1;
        uint32_t reset_block_200                : 1;
        uint32_t reset_block_300                : 1;
        uint32_t reset_block_400                : 1;
        uint32_t reset_block_500                : 1;
        uint32_t reset_block_600                : 1;
        uint32_t reset_block_700                : 1;
        uint32_t Block_reset_enable             : 8;
        uint32_t Special_controls               :16;
    } sw_reset_210;

    struct {
        uint32_t vuart_oe_sig                   : 1;
        uint32_t v2WS_oe_sig                    : 1;
        uint32_t halt_V8_sig                    : 1;
        uint32_t section_pkg_enable_sig         : 1;
        uint32_t s2p_sel_sig                    : 1;
        uint32_t unused1                        : 3;
        uint32_t polarity_PS_CLK_sig            : 1;
        uint32_t polarity_PS_VALID_sig          : 1;
        uint32_t polarity_PS_SYNC_sig           : 1;
        uint32_t polarity_PS_ERR_sig            : 1;
        uint32_t unused2                        :20;
    } misc_214;

    struct {
        uint32_t Mailbox_from_V8                :32;
    } mbox_v8_to_host_218;

    struct {
        uint32_t sysramaccess_data              : 8;
        uint32_t sysramaccess_addr              :15;
        uint32_t unused                         : 7;
        uint32_t sysramaccess_write             : 1;
        uint32_t sysramaccess_busmuster         : 1;
    } mbox_host_to_v8_21c;

    struct {
        uint32_t Stream1_PID                    :13;
        uint32_t Stream1_trans                  : 1;
        uint32_t MAC_Multicast_filter           : 1;
        uint32_t debug_flag_pid_saved           : 1;
        uint32_t Stream2_PID                    :13;
        uint32_t Stream2_trans                  : 1;
        uint32_t debug_flag_write_status00      : 1;
        uint32_t debug_fifo_problem             : 1;
    } pid_filter_300;

    struct {
        uint32_t PCR_PID                        :13;
        uint32_t PCR_trans                      : 1;
        uint32_t debug_overrun3                 : 1;
        uint32_t debug_overrun2                 : 1;
        uint32_t PMT_PID                        :13;
        uint32_t PMT_trans                      : 1;
        uint32_t reserved                       : 2;
    } pid_filter_304;

    struct {
        uint32_t EMM_PID                        :13;
        uint32_t EMM_trans                      : 1;
        uint32_t EMM_filter_4                   : 1;
        uint32_t EMM_filter_6                   : 1;
        uint32_t ECM_PID                        :13;
        uint32_t ECM_trans                      : 1;
        uint32_t reserved                       : 2;
    } pid_filter_308;

    struct {
        uint32_t Group_PID                      :13;
        uint32_t Group_trans                    : 1;
        uint32_t unused1                        : 2;
        uint32_t Group_mask                     :13;
        uint32_t unused2                        : 3;
    } pid_filter_30c_ext_ind_0_7;

    struct {
        uint32_t net_master_read                :17;
        uint32_t unused                         :15;
    } pid_filter_30c_ext_ind_1;

    struct {
        uint32_t net_master_write               :17;
        uint32_t unused                         :15;
    } pid_filter_30c_ext_ind_2;

    struct {
        uint32_t next_net_master_write          :17;
        uint32_t unused                         :15;
    } pid_filter_30c_ext_ind_3;

    struct {
        uint32_t unused1                        : 1;
        uint32_t state_write                    :10;
        uint32_t reserved1                      : 6;
        uint32_t stack_read                     :10;
        uint32_t reserved2                      : 5;
    } pid_filter_30c_ext_ind_4;

    struct {
        uint32_t stack_cnt                      :10;
        uint32_t unused                         :22;
    } pid_filter_30c_ext_ind_5;

    struct {
        uint32_t pid_fsm_save_reg0              : 2;
        uint32_t pid_fsm_save_reg1              : 2;
        uint32_t pid_fsm_save_reg2              : 2;
        uint32_t pid_fsm_save_reg3              : 2;
        uint32_t pid_fsm_save_reg4              : 2;
        uint32_t pid_fsm_save_reg300            : 2;
        uint32_t write_status1                  : 2;
        uint32_t write_status4                  : 2;
        uint32_t data_size_reg                  :12;
        uint32_t unused                         : 4;
    } pid_filter_30c_ext_ind_6;

    struct {
        uint32_t index_reg                      : 5;
        uint32_t extra_index_reg                : 3;
        uint32_t AB_select                      : 1;
        uint32_t pass_alltables                 : 1;
        uint32_t unused                         :22;
    } index_reg_310;

    struct {
        uint32_t PID                            :13;
        uint32_t PID_trans                      : 1;
        uint32_t PID_enable_bit                 : 1;
        uint32_t reserved                       :17;
    } pid_n_reg_314;

    struct {
        uint32_t A4_byte                        : 8;
        uint32_t A5_byte                        : 8;
        uint32_t A6_byte                        : 8;
        uint32_t Enable_bit                     : 1;
        uint32_t HighAB_bit                     : 1;
        uint32_t reserved                       : 6;
    } mac_low_reg_318;

    struct {
        uint32_t A1_byte                        : 8;
        uint32_t A2_byte                        : 8;
        uint32_t A3_byte                        : 8;
        uint32_t reserved                       : 8;
    } mac_high_reg_31c;

    struct {
        uint32_t reserved                       :16;
        uint32_t data_Tag_ID                    :16;
    } data_tag_400;

    struct {
        uint32_t Card_IDbyte6                   : 8;
        uint32_t Card_IDbyte5                   : 8;
        uint32_t Card_IDbyte4                   : 8;
        uint32_t Card_IDbyte3                   : 8;
    } card_id_408;

    struct {
        uint32_t Card_IDbyte2                   : 8;
        uint32_t Card_IDbyte1                   : 8;
    } card_id_40c;

    struct {
        uint32_t MAC1                           : 8;
        uint32_t MAC2                           : 8;
        uint32_t MAC3                           : 8;
        uint32_t MAC6                           : 8;
    } mac_address_418;

    struct {
        uint32_t MAC7                           : 8;
        uint32_t MAC8                           : 8;
        uint32_t reserved                       :16;
    } mac_address_41c;

    struct {
        uint32_t transmitter_data_byte          : 8;
        uint32_t ReceiveDataReady               : 1;
        uint32_t ReceiveByteFrameError          : 1;
        uint32_t txbuffempty                    : 1;
        uint32_t reserved                       :21;
    } ci_600;

    struct {
        uint32_t pi_d                           : 8;
        uint32_t pi_ha                          :20;
        uint32_t pi_rw                          : 1;
        uint32_t pi_component_reg               : 3;
    } pi_604;

    struct {
        uint32_t serialReset                    : 1;
        uint32_t oncecycle_read                 : 1;
        uint32_t Timer_Read_req                 : 1;
        uint32_t Timer_Load_req                 : 1;
        uint32_t timer_data                     : 7;
        uint32_t unused                         : 1;
        uint32_t Timer_addr                     : 5;
        uint32_t reserved                       : 3;
        uint32_t pcmcia_a_mod_pwr_n             : 1;
        uint32_t pcmcia_b_mod_pwr_n             : 1;
        uint32_t config_Done_stat               : 1;
        uint32_t config_Init_stat               : 1;
        uint32_t config_Prog_n                  : 1;
        uint32_t config_wr_n                    : 1;
        uint32_t config_cs_n                    : 1;
        uint32_t config_cclk                    : 1;
        uint32_t pi_CiMax_IRQ_n                 : 1;
        uint32_t pi_timeout_status              : 1;
        uint32_t pi_wait_n                      : 1;
        uint32_t pi_busy_n                      : 1;
    } pi_608;

    struct {
        uint32_t PID                            :13;
        uint32_t key_enable                     : 1;
        uint32_t key_code                       : 2;
        uint32_t key_array_col                  : 3;
        uint32_t key_array_row                  : 5;
        uint32_t dvb_en                         : 1;
        uint32_t rw_flag                        : 1;
        uint32_t reserved                       : 6;
    } dvb_reg_60c;

    struct {
        uint32_t sram_addr                      :15;
        uint32_t sram_rw                        : 1;
        uint32_t sram_data                      : 8;
        uint32_t sc_xfer_bit                    : 1;
        uint32_t reserved1                      : 3;
        uint32_t oe_pin_reg                     : 1;
        uint32_t ce_pin_reg                     : 1;
        uint32_t reserved2                      : 1;
        uint32_t start_sram_ibi                 : 1;
    } sram_ctrl_reg_700;

    struct {
        uint32_t net_addr_read                  :16;
        uint32_t net_addr_write                 :16;
    } net_buf_reg_704;

    struct {
        uint32_t cai_read                       :11;
        uint32_t reserved1                      : 5;
        uint32_t cai_write                      :11;
        uint32_t reserved2                      : 6;
        uint32_t cai_cnt                        : 4;
    } cai_buf_reg_708;

    struct {
        uint32_t cao_read                       :11;
        uint32_t reserved1                      : 5;
        uint32_t cap_write                      :11;
        uint32_t reserved2                      : 6;
        uint32_t cao_cnt                        : 4;
    } cao_buf_reg_70c;

    struct {
        uint32_t media_read                     :11;
        uint32_t reserved1                      : 5;
        uint32_t media_write                    :11;
        uint32_t reserved2                      : 6;
        uint32_t media_cnt                      : 4;
    } media_buf_reg_710;

    struct {
        uint32_t NET_Dest                       : 2;
        uint32_t CAI_Dest                       : 2;
        uint32_t CAO_Dest                       : 2;
        uint32_t MEDIA_Dest                     : 2;
        uint32_t net_ovflow_error               : 1;
        uint32_t media_ovflow_error             : 1;
        uint32_t cai_ovflow_error               : 1;
        uint32_t cao_ovflow_error               : 1;
        uint32_t ctrl_usb_wan                   : 1;
        uint32_t ctrl_sramdma                   : 1;
        uint32_t ctrl_maximumfill               : 1;
        uint32_t reserved                       :17;
    } sram_dest_reg_714;

    struct {
        uint32_t net_cnt                        :12;
        uint32_t reserved1                      : 4;
        uint32_t net_addr_read                  : 1;
        uint32_t reserved2                      : 3;
        uint32_t net_addr_write                 : 1;
        uint32_t reserved3                      :11;
    } net_buf_reg_718;

    struct {
        uint32_t wan_speed_sig                  : 2;
        uint32_t reserved1                      : 6;
        uint32_t wan_wait_state                 : 8;
        uint32_t sram_chip                      : 2;
        uint32_t sram_memmap                    : 2;
        uint32_t reserved2                      : 4;
        uint32_t wan_pkt_frame                  : 4;
        uint32_t reserved3                      : 4;
    } wan_ctrl_reg_71c;
} flexcop_ibi_value;

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
    uint32_t irq_status;
    uint32_t irq_mask;

    /* Hardware Register Shadows (The 'Identity' of the device) */
    uint32_t regs[0x200]; /* covers 0x000-0x7FF, 0x800 bytes */

    /* DMA Context */
    struct {
        bool running[2];
        QEMUTimer *dma_timer;
        dma_addr_t dma_addr[2];
        uint32_t dma_count[2];
        uint32_t dma_cmd[2];
    } dma;

    uint32_t status;      /* Operational status flags */
    uint32_t reset_state; /* State used to handle reset sequences */
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t status_reg = s->regs[irq_20c >> 2];
    uint32_t enable_reg = s->regs[ctrl_208 >> 2];
    /* Bits 0-6: DMA1_IRQ_S, DMA1_Timer_S, DMA2_IRQ_S, DMA2_Timer_S,
     * DMA1_Size_IRQ_S, DMA2_Size_IRQ_S, Mailbox_V8_S. */
    uint32_t active = status_reg & enable_reg & 0x7F;
    pci_set_irq(pdev, active ? 1 : 0);
}

/* Device-initiated DMA logic based on driver access patterns */
G_GNUC_UNUSED static void pcibase_do_dma(PCIBaseState *s, bool is_write)
{
    /* No DMA transfer required for driver probe; placeholder removed. */
}

/* MMIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    /* All accesses must be 32-bit and within the 0x800 region */
    if (size != 4 || addr >= 0x800) {
        return ~0ULL;
    }

    uint32_t idx = addr >> 2;
    val = s->regs[idx];

    /* IRQ status register 0x20c: read clears all status bits */
    if (addr == irq_20c) {
        s->regs[idx] = 0;
        pcibase_update_irq(s);
    }

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* All accesses must be 32-bit and within the 0x800 region */
    if (size != 4 || addr >= 0x800) {
        return;
    }

    uint32_t idx = addr >> 2;
    s->regs[idx] = (uint32_t)val;

    /* If interrupt control or status registers written, update IRQ state */
    if (addr == ctrl_208 || addr == irq_20c) {
        pcibase_update_irq(s);
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

    /* Reset all registers to zero, then set power-on defaults */
    memset(s->regs, 0, sizeof(s->regs));

    /* Set revision (Rev_N_sig_revision_hi = 1) in misc_204 */
    s->regs[misc_204 >> 2] = (1 << 24);
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
    pci_set_word(pci_conf + PCI_VENDOR_ID, 0x13d0);
    pci_set_word(pci_conf + PCI_DEVICE_ID, 0x2103);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0480); /* multimedia other */
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = 0x800,
        .name = "flexcop-mmio"
    };
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "b2c2_flexcop_pci_pci",
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
