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

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define TYPE_PCIBASE_DEVICE "csiostor_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define CSIO_VENDOR_ID 0x1425
#define CSIO_DEVICE_ID 0x5600
#define CSIO_CLASS_ID  0x0100

#define CSIO_HW_CHIP_MASK 0xF000

#define FW_HDR_FW_VER_MAJOR_S	24
#define FW_HDR_FW_VER_MAJOR_M	0xff

#define PF_REG(idx, reg) (PF_BASE(idx) + (reg))
#define CIM_PF_MAILBOX_CTRL_A 0x280
#define CIM_PF_MAILBOX_DATA_A 0x240
#define MBOWNER_G(x) (((x) >> MBOWNER_S) & MBOWNER_M)
#define MBMSGVALID_F    MBMSGVALID_V(1U)
#define MBINTREQ_F    MBINTREQ_V(1U)
#define MBOWNER_V(x) ((x) << MBOWNER_S)
#define FW_CMD_OP_G(x)          (((x) >> FW_CMD_OP_S) & FW_CMD_OP_M)
#define PL_WHOAMI_A 0x19400
#define SOURCEPF_G(x) (((x) >> SOURCEPF_S) & SOURCEPF_M)
#define T6_SOURCEPF_G(x) (((x) >> T6_SOURCEPF_S) & T6_SOURCEPF_M)
#define MYPF_REG(reg_addr) (MYPF_BASE + (reg_addr))
#define PL_PF_INT_ENABLE_A 0x3c4
#define PL_INT_MAP0_A 0x19414

struct fw_cmd_hdr {
	__be32 hi;
	__be32 lo;
};

struct fw_port_cmd {
	__be32 op_to_portid;
	__be32 action_to_len16;
	union fw_port {
		struct fw_port_l1cfg {
			__be32 rcap;
			__be32 r;
		} l1cfg;
		struct fw_port_l2cfg {
			__u8   ctlbf;
			__u8   ovlan3_to_ivlan0;
			__be16 ivlantype;
			__be16 txipg_force_pinfo;
			__be16 mtu;
			__be16 ovlan0mask;
			__be16 ovlan0type;
			__be16 ovlan1mask;
			__be16 ovlan1type;
			__be16 ovlan2mask;
			__be16 ovlan2type;
			__be16 ovlan3mask;
			__be16 ovlan3type;
		} l2cfg;
		struct fw_port_info {
			__be32 lstatus_to_modtype;
			__be16 pcap;
			__be16 acap;
			__be16 mtu;
			__u8   cbllen;
			__u8   auxlinfo;
			__u8   dcbxdis_pkd;
			__u8   r8_lo;
			__be16 lpacap;
			__be64 r9;
		} info;
		struct fw_port_diags {
			__u8   diagop;
			__u8   r[3];
			__be32 diagval;
		} diags;
		union fw_port_dcb {
			struct fw_port_dcb_pgid {
				__u8   type;
				__u8   apply_pkd;
				__u8   r10_lo[2];
				__be32 pgid;
				__be64 r11;
			} pgid;
			struct fw_port_dcb_pgrate {
				__u8   type;
				__u8   apply_pkd;
				__u8   r10_lo[5];
				__u8   num_tcs_supported;
				__u8   pgrate[8];
				__u8   tsa[8];
			} pgrate;
			struct fw_port_dcb_priorate {
				__u8   type;
				__u8   apply_pkd;
				__u8   r10_lo[6];
				__u8   strict_priorate[8];
			} priorate;
			struct fw_port_dcb_pfc {
				__u8   type;
				__u8   pfcen;
				__u8   r10[5];
				__u8   max_pfc_tcs;
				__be64 r11;
			} pfc;
			struct fw_port_app_priority {
				__u8   type;
				__u8   r10[2];
				__u8   idx;
				__u8   user_prio_map;
				__u8   sel_field;
				__be16 protocolid;
				__be64 r12;
			} app_priority;
			struct fw_port_dcb_control {
				__u8   type;
				__u8   all_syncd_pkd;
				__be16 dcb_version_to_app_state;
				__be32 r11;
				__be64 r12;
			} control;
		} dcb;
		struct fw_port_l1cfg32 {
			__be32 rcap32;
			__be32 r;
		} l1cfg32;
		struct fw_port_info32 {
			__be32 lstatus32_to_cbllen32;
			__be32 auxlinfo32_mtu32;
			__be32 linkattr32;
			__be32 pcaps32;
			__be32 acaps32;
			__be32 lpacaps32;
		} info32;
	} u;
};

struct fw_fcoe_link_cmd {
	__be32 op_to_portid;
	__be32 retval_len16;
	__be32 sub_opcode_fcfi;
	u8     r3;
	u8     lstatus;
	__be16 flags;
	u8     r4;
	u8     set_vlan;
	__be16 vlan_id;
	__be32 vnpi_pkd;
	__be16 r6;
	u8     phy_mac[6];
	u8     vnport_wwnn[8];
	u8     vnport_wwpn[8];
};

struct fw_wr_hdr {
	__be32 hi;
	__be32 lo;
};

struct fw_rdev_wr {
	__be32 op_to_immdlen;
	__be32 alloc_to_len16;
	__be64 cookie;
	u8     protocol;
	u8     event_cause;
	u8     cur_state;
	u8     prev_state;
	__be32 flags_to_assoc_flowid;
	union rdev_entry {
		struct fcoe_rdev_entry {
			__be32 flowid;
			u8     protocol;
			u8     event_cause;
			u8     flags;
			u8     rjt_reason;
			u8     cur_login_st;
			u8     prev_login_st;
			__be16 rcv_fr_sz;
			u8     rd_xfer_rdy_to_rport_type;
			u8     vft_to_qos;
			u8     org_proc_assoc_to_acc_rsp_code;
			u8     enh_disc_to_tgt;
			u8     wwnn[8];
			u8     wwpn[8];
			__be16 iqid;
			u8     fc_oui[3];
			u8     r_id[3];
		} fcoe_rdev;
		struct iscsi_rdev_entry {
			__be32 flowid;
			u8     protocol;
			u8     event_cause;
			u8     flags;
			u8     r3;
			__be16 iscsi_opts;
			__be16 tcp_opts;
			__be16 ip_opts;
			__be16 max_rcv_len;
			__be16 max_snd_len;
			__be16 first_brst_len;
			__be16 max_brst_len;
			__be16 r4;
			__be16 def_time2wait;
			__be16 def_time2ret;
			__be16 nop_out_intrvl;
			__be16 non_scsi_to;
			__be16 isid;
			__be16 tsid;
			__be16 port;
			__be16 tpgt;
			u8     r5[6];
			__be16 iqid;
		} iscsi_rdev;
	} u;
};

struct fw_fcoe_els_ct_wr {
	__be32 op_immdlen;
	__be32 flowid_len16;
	u64    cookie;
	__be16 iqid;
	u8     tmo_val;
	u8     els_ct_type;
	u8     ctl_pri;
	u8     cp_en_class;
	__be16 xfer_cnt;
	u8     fl_to_sp;
	u8     l_id[3];
	u8     r5;
	u8     r_id[3];
	__be64 rsp_dmaaddr;
	__be32 rsp_dmalen;
	__be32 r6;
};

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
    uint64_t mb[8]; /* CSIO_MB_MAX_REGS */

    /* DMA Context */
    dma_addr_t pstart;

    uint32_t flags;
};

/* MMIO/PIO Handlers generated during Behavioral Modeling */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    uint64_t val = 0;

    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    uint64_t val = 0;

    return val;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
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
    (void)s;
    pci_device_reset(PCI_DEVICE(dev));
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  CSIO_VENDOR_ID );
    pci_set_word(pci_conf + PCI_DEVICE_ID,  CSIO_DEVICE_ID );
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CSIO_CLASS_ID );
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
    s->bar_info[0].size = 0x1000; /* Placeholder size */
    s->bar_info[0].name = "csio-bar0";
      
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "csiostor_pci",
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
