/*
 * QEMU PCI device model for Adaptec aic94xx SAS controller (minimal)
 *
 * This model only implements the behavior that is directly visible in
 * drivers/scsi/aic94xx/aic94xx_init.c and related helper code snippets
 * provided by the user, enough for the driver to probe, map BARs,
 * request IRQs and enable/disable basic interrupts.
 */

#include "qemu/osdep.h"
#include <inttypes.h>
#include <string.h>
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/memory.h"
#include "sysemu/dma.h"
#include "sysemu/reset.h"
#include "hw/irq.h"
#include "hw/pci/pci.h"
#include "hw/pci/msi.h"
#include "hw/pci/msix.h"
#include "hw/pci/pcie.h"
#include "qom/object.h"
#include "qapi/visitor.h"
#include "hw/qdev-properties.h"

#define TYPE_PCIBASE_DEVICE "aic94xx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define DRV_NAME "aic94xx"
#define ASD_DRIVER_VERSION "1.0.3"
#define FLASH_CMD_NONE      0x00
#define FLASH_CMD_UPDATE    0x01
#define FLASH_CMD_VERIFY    0x02
#define PCI_IOBAR_OFFSET              4
#define AIC9410_DEV_REV_B0            0x8
#define ASD_MAX_DDBS 128
#define ASD_MAX_PHYS       8
#define FAIL_CHECK_SUM                  0x000300
#define FLASH_IN_PROGRESS               0x001000
#define FAIL_OPEN_BIOS_FILE             0x000100
#define FAIL_CHECK_PCI_ID               0x000200
#define FAIL_FILE_SIZE                  0x000a00
#define FAIL_OUT_MEMORY                 0x000c00
#define FAIL_PARAMETERS                 0x000b00
#define FLASH_OK                        0x000000
#define CONTROL_PHY             0x80
#define ASD_EDB_SIZE (24+1024+4+16)
#define CHIMINT  (REG_BASE_ADDR + 0x18)
#define HARDRST   0x00000200
#define HARDRSTDET  0x00000100
#define PORRSTDET  0x00000200
#define COMBIST  (REG_BASE_ADDR + 0x00)
#define CHIMINTEN (REG_BASE_ADDR + 0x1C)
#define RST_CHIMINTEN   (RST_EN_HOSTERR | RST_EN_INITERR | \
           RST_EN_DEVINT | RST_EN_COMINT | \
           RST_EN_DEVTIMER2 | RST_EN_DEVTIMER1 |\
           RST_EN_DLAVAIL)
#define PCIC_HSTPCIX_CNTRL  0xA0
#define SC_TMR_DIS   0x04000000
#define EN_CSBUFPERR   0x00000008
#define COMSTATEN (REG_BASE_ADDR + 0x08)
#define EN_CSERR   0x00000002
#define DCHSTATUS (REG_BASE_ADDR + 0x81C)
#define EN_CFIFTOERR   0x00020000
#define EN_OVLYERR   0x00000004
#define SET_CHIMINTEN   (SET_EN_HOSTERR | SET_EN_INITERR |\
           SET_EN_DEVINT | SET_EN_COMINT |\
           SET_EN_DLAVAIL)
#define GPIOOER  (EXSI_REG_BASE_ADR + 0x40)
#define GPIOCNFGR  (EXSI_REG_BASE_ADR + 0x54)
#define LEDPOL   0x00000100
#define CONTROL  0x48
#define ENABLE_PHY             0x01
#define TF_TMF_TAG_FREE         0x20
#define TF_TMF_NO_CONN_HANDLE   0x22
#define TF_TMF_TASK_DONE        0x21
#define ITNL_TIMEOUT_CONST 0x7D0
#define TF_TMF_NO_TAG           0x1f
#define TF_TMF_NO_CTX           0x1d
#define AIC94XX_SCB_TIMEOUT  (5*HZ)
#define TC_NO_ERROR             0x00
#define TF_NAK_RECV             0x1a
#define SCB_ABORT_TASK          0x03
#define TC_SSP_RESP             0x07
#define NEXUS_ADAPTER  0x00
#define NEXUS_PORT     0x01
#define TC_RESUME               0x13
#define ASD_PCBA_SN_SIZE   12
#define FLASH_MANUF_ID_ST               0x20
#define FLASH_DEV_ID_STM29008           0xEA
#define FLASH_MANUF_ID_UNKNOWN          0xFF
#define FLASH_MANUF_ID_FUJITSU          0x04
#define FLASH_DEV_ID_AM29LV640MT        0x7E
#define FLASH_DEV_ID_MX29LV800BT        0xDA
#define FLASH_DEV_ID_STM29W800DT        0xD7
#define FLASH_DEV_ID_AM29LV800DT        0xDA
#define FLASH_DEV_ID_AM29LV008BT        0x3E
#define FLASH_MANUF_ID_AMD              0x01
#define FLASH_DEV_ID_MBM29LV008TA       0x3E
#define FLASH_DEV_ID_UNKNOWN            0xFF
#define FLASH_MANUF_ID_INTEL            0x89
#define FLASH_DEV_ID_MBM29LV800TE       0xDA
#define FLASH_DEV_ID_STM29LV640         0xDE
#define FLASH_DEV_ID_AM29F800B          0xD6
#define FLASH_MANUF_ID_MACRONIX         0xC2
#define FLASH_DEV_ID_MBM29DL800TA       0x4A
#define FLASH_DEV_ID_I28LV00TAT         0x3E
#define FLASH_STATUS_ERASE_DELAY_COUNT  50
#define FLASH_STATUS_BIT_MASK_DQ6       0x40
#define FLASH_STATUS_BIT_MASK_DQ5       0x20
#define FLASH_STATUS_WRITE_DELAY_COUNT  25
#define FLASH_SECTOR_SIZE               0x010000
#define FLASH_SECTOR_SIZE_MASK          0xffff0000
#define CSMI_TASK           0x40
#define RET_PARTIAL_SGLIST 0x02
#define ATA_Q_TYPE_NCQ      0x08
#define ATA_Q_TYPE_MASK     0x08
#define ATA_Q_TYPE_UNTAGGED 0x00
#define STP_AFFIL_POLICY   0x20
#define SET_AFFIL_POLICY   0x10
#define DATA_XFER_MODE_DMA  0x10
#define DISABLE_PHY_IF_OOB_FAILS       0x02
#define EXECUTE_HARD_RESET     0x81
#define DEV_PRES_TIMER_OVERRIDE_ENABLE 0x01
#define PHY_NO_OP              0x05
#define ENABLE_PHY_NO_SAS_OOB  0x03
#define RELEASE_SPINUP_HOLD    0x02
#define ENABLE_PHY_NO_SATA_OOB 0x04
#define DISABLE_PHY            0x00
#define OVERRIDE_ITNL_TIMER  8
#define NEXUS_T_L      0x07
#define NOTINQ         0x01
#define SEND_Q         0x04
#define NEXUS_T_TAG    0x09
#define SUSPEND_TX     0x80
#define NEXUS_I_T_L    0x03
#define NEXUS_TRANS_CX 0x05
#define NEXUS_SATA_TAG 0x06
#define RESUME_TX      0x40
#define EXEC_Q         0x02
#define NEXUS_TAG      0x04
#define NEXUS_I_T      0x02
#define NEXUS_L        0x08
#define ASD_EDBS_PER_SCB 7
#define ELEMENT_NOT_VALID  0xC0
#define SUSPEND_DATA_TRANS 0x04
#define ENABLE_NOTIFY_SPINUP_INTS 0x02
#define RESET_LINK_ERROR_COUNT    0x01
#define GET_LINK_ERROR_COUNT      0x00
#define DATA_DIR_IN     0x01
#define DATA_DIR_OUT    0x02
#define DATA_DIR_NONE   0x00
#define DATA_DIR_BYRECIPIENT 0x03
#define DL_TOGGLE_MASK     0x01
#define ASD_CRC_DIS  1
#define ASD_SATA_SPINUP_HOLD 2
#define REG_BASE_ADDR                 0xB8000000
#define SCBPRO  (REG_BASE_ADDR + 0x0C)
#define RST_EN_INITERR   0x00200000
#define RST_EN_HOSTERR   0x00400000
#define RST_EN_DEVTIMER2  0x00040000
#define RST_EN_DLAVAIL   0x00010000
#define RST_EN_COMINT   0x00080000
#define RST_EN_DEVINT   0x00100000
#define RST_EN_DEVTIMER1  0x00020000
#define ASD_DEF_DL_TOGGLE  0x01
#define ASD_DL_SIZE_BITS   0x8
#define ASD_DL_SIZE        (1<<(2+ASD_DL_SIZE_BITS))
#define PCI_CONF_MBAR1                0x6C
#define REG_BASE_ADDR_CSEQCIO         0xB8002000
#define PCI_CONF_MBAR0_SWA            0x70
#define PCI_CONF_MBAR0_SWB            0x74
#define OCM_BASE_ADDR                 0xA0000000
#define REG_BASE_ADDR_EXSI            0xB8042800
#define PCI_CONF_MBAR0_SWC            0x78
#define PCI_CONF_MBAR_KEY             0x7C
#define SET_EN_DEVINT   0x00000010
#define SET_EN_HOSTERR   0x00000040
#define SET_EN_INITERR   0x00000020
#define SET_EN_DLAVAIL   0x00000001
#define SET_EN_COMINT   0x00000008
#define EXSI_REG_BASE_ADR  REG_BASE_ADDR_EXSI
#define LSEQ0_HOST_REG_BASE_ADR  0xB8020000
#define FUNCTION_MASK_DEFAULT (SPINUP_HOLD_DIS | SATA_PS_DIS)
#define SPINUP_HOLD_DIS  0x20
#define HOTPLUG_DELAY_TIMEOUT   20
#define HOT_PLUG_DELAY   0x150
#define INT_ENABLE_2   0x15A
#define PHY_CONTROL_1   0x161
#define PHY_CONTROL_2   0x162
#define PHY_CONTROL_3   0x163
#define ASD_COMINIT_TIMEOUT ASD_TEN_MILLISEC_TIMEOUT
#define PHY_CONTROL_0   0x160
#define INITIATE_SSP_TASK       0x00
#define INITIATE_ATA_TASK       0x09
#define CONTROL_ATA_DEV         0x0b
#define INITIATE_ATAPI_TASK     0x0a
#define INITIATE_SMP_TASK       0x0c
#define DDB_TYPE_UNUSED    0xFF
#define QUERY_SSP_TASK          0x08
#define INITIATE_SSP_TMF        0x04
#define CLEAR_NEXUS             0xc4
#define PM_PORT_SET   0x02
#define SUPPORTS_AFFIL     0x40
#define CONCURRENT_CONN_SUPP 0x04
#define SATA_MULTIPORT     0x80
#define OPEN_REQUIRED        0x01
#define STP_AFFIL_POL      0x20
#define DDB_TP_CONN_TYPE 0x81
#define STP_CL_POL_NO_TX    0x00
#define STP_CL_POL_BTW_CMDS 0x01
#define ASD_SG_EL_DS_MASK   0x30
#define ASD_SG_EL_LIST_EOS  0x80
#define ASD_SG_EL_DS_HM     0x00
#define ASD_SG_EL_DS_OCM    0x10
#define ASD_SG_EL_LIST_MASK 0xC0
#define ASD_SG_EL_LIST_EOL  0x40
#define CURRENT_LOSS_OF_SIGNAL  0x40
#define CURRENT_OOB_ERROR  0x01
#define CURRENT_SPINUP_HOLD  0x20
#define CURRENT_GTO_TIMEOUT  0x08
#define CURRENT_OOB_DONE  0x80
#define CTXDOMAIN (REG_BASE_ADDR + 0x810)
#define ASD_SCB_SIZE sizeof(struct scb)
#define CMDCTXBASE (REG_BASE_ADDR + 0x800)
#define ASD_DDB_SIZE sizeof(struct asd_ddb_ssp_smp_target_port)
#define DEVCTXBASE (REG_BASE_ADDR + 0x808)
#define EMPTY_SCB               0xc0
#define PCI_CONF_FLSH_BAR             0xB8
#define FLASHW   0x00000010
#define EXSICNFGR (EXSI_REG_BASE_ADR + 0x00)
#define PCIC_INTRPT_STAT 0xD4
#define OCMINITIALIZED   0x80000000
#define SAS_RAZOR_SEQUENCER_FW_FILE "aic94xx-seq.fw"
#define SATA_PS_DIS   0x08
#define SATA_SPEED_30_DIS  0x10
#define SAS_SPEED_15_DIS  0x01
#define SAS_SPEED_30_DIS  0x02
#define SAS_SPEED_60_DIS  0x04
#define SATA_SPEED_15_DIS  0x08
#define CURRENT_DEVICE_PRESENT  0x02
#define CURRENT_HOT_PLUG_CNCT  0x10
#define CURRENT_ERR_MASK (CURRENT_LOSS_OF_SIGNAL | \
           CURRENT_GTO_TIMEOUT | \
           CURRENT_OOB_TIMEOUT | \
           CURRENT_OOB_ERROR )
#define ASD_BUSADDR_HI(__dma_handle) (((sizeof(dma_addr_t))==8)     \
                                    ? ((u32)((__dma_handle) >> 32)) \
                                    : ((u32)0))
#define ASD_BUSADDR_LO(__dma_handle) ((u32)(__dma_handle))
#define ASD_TEN_MILLISEC_TIMEOUT  0x2710
#define TU_PHY_DOWN             0x0d
#define TF_SMPRSP_TO            0x15
#define TI_PHY_DOWN             0x08
#define TF_PHY_DOWN             0x09
#define TI_NAK                  0x10
#define TC_ATA_R_ERR_RECV       0x1c
#define TF_OPEN_TO              0x03
#define TF_IU_SHORT             0x28
#define TC_UNDERRUN             0x01
#define TA_I_T_NEXUS_LOSS       0x1b
#define TA_ON_REQ               0x1e
#define TI_BREAK                0x05
#define TF_INV_CONN_HANDLE      0x2a
#define TF_SMP_XMIT_RCV_ERR     0x16
#define TF_NO_SMP_CONN          0x27
#define TU_BREAK                0x0e
#define TU_ACK_NAK_TO           0x18
#define TC_PARTIAL_SG_LIST      0x17
#define TF_REQUESTED_N_PENDING  0x2b
#define TC_ATA_RESP             0x0c
#define TF_DATA_OFFS_ERR        0x29
#define TC_CONTROL_PHY          0x11
#define TC_LINK_ADM_RESP        0x0a
#define TI_PROTO_ERR            0x06
#define TC_TASK_CLEARED         0x23
#define TC_OVERRUN              0x02
#define TI_ACK_NAK_TO           0x14
#define TF_OPEN_REJECT          0x04
#define TF_IRTT_TO              0x26
#define TF_BREAK                0x12
#define DDB_TYPE_TARGET    0xFE
#define DDB_TYPE_PM_PORT   0xFC
#define DDB_TYPE_INITIATOR 0xFD
#define PHY_SPEED_60   0x04
#define PHY_SPEED_15   0x01
#define SAS_MODE   0x80
#define PHY_SPEED_30   0x02
#define SATA_MODE   0x40
#define CTXMEMSIZE   0x80000000
#define OCM_MAX_SIZE                  0x20000
#define SAS_ALIGN_DEFAULT   0xFF
#define ASD_SATA_INTERLOCK_TIMEOUT 0
#define STP_ALIGN_DEFAULT   0x1F
#define OVLYHALTERR   0x00000040
#define STARTOVLYDMA   0x00000004
#define RESETOVLYDMA   0x00000008
#define OVLYDMACTL (REG_BASE_ADDR + 0x20)
#define COMSTAT  (REG_BASE_ADDR + 0x04)
#define OVLYDMACNT (REG_BASE_ADDR + 0x24)
#define COMSTAT_MASK   (REQMBXREAD | RSPMBXAVAIL | \
           CSBUFPERR | OVLYERR | CSERR |\
           OVLYDMADONE)
#define OVLYDMADONE   0x00000001
#define CHIMINT_MASK   (HOSTERR | INITERR | DEVINT | COMINT |\
           DEVTIMER2 | DEVTIMER1 | DLAVAIL)
#define OVLYERR   0x00000004
#define OVLYDMAADR (REG_BASE_ADDR + 0x28)
#define OVLYCSEQ   0x00000080
#define DEVEXCEPT_MASK   (HOSTERR | INITERR | DEVINT | COMINT)
#define OVLYDMAACT   0x00000001
#define MBAR0_SWC_SIZE                0x8
#define ALL_BASE_ADDR                 OCM_BASE_ADDR
#define MBAR0_SWA_SIZE                0x58
#define ARP2CTL  0x00
#define CSEQ_HOST_REG_BASE_ADR  0xB8001000
#define SCRATCHPAGE 0x24
#define ARP2INTEN 0x08
#define ARP2INTCTL 0x05
#define MODECTL 0x40
#define ASD_NOTIFY_TIMEOUT        2500
#define LSEQ_MODE5_PAGE0_OFFSET  0x60
#define LSEQ_PAGE_SIZE   0x20
#define LSEQ_MODE_SCRATCH_SIZE  0x80
#define ASD_DEV_PRESENT_TIMEOUT   0x2710
#define ASD_NOTIFY_ENABLE_SPINUP  0x10
#define ASD_STP_SHUTDOWN_TIMEOUT  0x0
#define ASD_SRST_ASSERT_TIMEOUT   0x05
#define ASD_SMP_RCV_TIMEOUT       0x000F4240
#define ASD_NOTIFY_DOWN_COUNT     0
#define ASD_ONE_MILLISEC_TIMEOUT  0x03e8
#define ASD_RCV_FIS_TIMEOUT       0x01D905C0
#define REQMBXREAD   0x00000040
#define CSBUFPERR   0x00000008
#define CSERR   0x00000002
#define RSPMBXAVAIL   0x00000020
#define COMINT   0x00000008
#define DEVTIMER1   0x00000002
#define DLAVAIL   0x00000001
#define DEVTIMER2   0x00000004
#define INITERR   0x00000020
#define DEVINT   0x00000010
#define HOSTERR   0x00000040
#define ATOMICERR   0x04
#define ATOMICSTATCTL (REG_BASE_ADDR + 0x824)
#define ATOMICWIN   0x02
#define ANEWDATA (REG_BASE_ADDR + 0x830)
#define ATOMICDONE   0x01
#define AOLDDATA (REG_BASE_ADDR + 0x834)
#define CSEQ_MODE_PAGE_SIZE 0x200
#define CSEQ_RAM_REG_BASE_ADR  0xB8004000
#define LSEQRAM  0x1000
#define BISTCTL1 0x50
#define PCI_VENDOR_ID_ADAPTEC2        0x9005
#define NUMPRIO            (MAXPRIO + 1)
#define MAXPRIO            7
#define AMPDU_MAX_SCB_TID  NUMPRIO
#define HZ __USER_HZ
#define CURRENT_OOB_TIMEOUT 0x04
#define u32 unsigned int
#define u8  unsigned char

struct scb_ampdu_tid_ini {
    u8 txretry[AMPDU_MAX_SCB_TID];
};

struct scb_ampdu {
    uint8_t max_pdu;
    uint8_t release;
    uint32_t max_rx_ampdu_bytes;
    struct scb_ampdu_tid_ini ini[AMPDU_MAX_SCB_TID];
};

struct scb {
    uint32_t magic;
    uint32_t flags;
    uint16_t seqctl[NUMPRIO];
    uint16_t seqnum[NUMPRIO];
    struct scb_ampdu scb_ampdu;
};

struct asd_ddb_ssp_smp_target_port {
    uint8_t     conn_type;
    uint8_t     conn_rate;
    uint16_t    init_conn_tag;
    uint8_t     dest_sas_addr[8];
    uint16_t    send_queue_head;
    uint8_t     sq_suspended;
    uint8_t     ddb_type;
    uint16_t    _r_a;
    uint16_t    awt_def;
    uint8_t     compat_features;
    uint8_t     pathway_blocked_count;
    uint16_t    arb_wait_time;
    uint32_t    more_compat_features;
    uint8_t     conn_mask;
    uint8_t     flags;
    uint16_t    _r_b;
    uint16_t    exec_queue_tail;
    uint16_t    send_queue_tail;
    uint16_t    sister_ddb;
    uint16_t    _r_c;
    uint8_t     max_concurrent_conn;
    uint8_t     num_concurrent_conn;
    uint8_t     num_contexts;
    uint8_t     _r_d;
    uint16_t    active_task_count;
    uint8_t     _r_e[9];
    uint8_t     itnl_reason;
    uint16_t    _r_f;
    uint16_t    itnl_timeout;
    uint32_t    itnl_timestamp;
};

/* BAR metadata */
typedef enum {
    BAR_TYPE_NONE = 0,
    BAR_TYPE_MMIO,
    BAR_TYPE_PIO,
    BAR_TYPE_RAM
} BARType;

typedef struct {
    int index;
    BARType type;
    hwaddr size;
    const char *name;
    bool sparse;
} BARInfo;

struct PCIBaseState {
    PCIDevice parent_obj;

    MemoryRegion bar_regions[6];

    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    BARInfo bar_info[6];
    int num_bars;

    bool has_msi;
    bool has_msix;

    uint32_t chiminten_reg;
    uint32_t comstaten_reg;
    uint32_t dchstatus_reg;
    uint32_t chimint_status;
};

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

static const MemoryRegionOps pcibase_mmio_ops = {
    .read = pcibase_mmio_read,
    .write = pcibase_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static const MemoryRegionOps pcibase_pio_ops = {
    .read = pcibase_pio_read,
    .write = pcibase_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 4 },
    .impl  = { .min_access_size = 1, .max_access_size = 4 },
};

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }

    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_pio_ops, s, bi->name, bi->size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_IO, mr);
    } else if (bi->type == BAR_TYPE_RAM) {
        memory_region_init_ram(mr, OBJECT(s), bi->name, bi->size, errp);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    }
}

static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->chimint_status & CHIMINT_MASK) {
        if (s->has_msi) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!s->has_msi) {
            pci_set_irq(pdev, 0);
        }
    }
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val = 0;

    switch (addr) {
    case (CHIMINT - REG_BASE_ADDR):
        /* Read CHIMINT status as masked interrupt bits */
        val = s->chimint_status & CHIMINT_MASK;
        break;
    case (CHIMINTEN - REG_BASE_ADDR):
        val = s->chiminten_reg;
        break;
    case (COMSTATEN - REG_BASE_ADDR):
        val = s->comstaten_reg;
        break;
    case (DCHSTATUS - REG_BASE_ADDR):
        val = s->dchstatus_reg;
        break;
    default:
        val = 0;
        break;
    }

    if (size == 1) {
        return (uint8_t)val;
    } else if (size == 2) {
        return (uint16_t)val;
    } else {
        return val;
    }
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v32 = (uint32_t)val;

    switch (addr) {
    case (CHIMINT - REG_BASE_ADDR):
        /*
         * asd_hw_isr() writes the CHIMINT value back to clear
         * active bits. Emulate write-one-to-clear on CHIMINT_MASK.
         */
        s->chimint_status &= ~((uint32_t)val & CHIMINT_MASK);
        pcibase_update_irq(s);
        break;
    case (CHIMINTEN - REG_BASE_ADDR):
        /* asd_enable_ints / asd_disable_ints */
        s->chiminten_reg = v32;
        if (v32 == RST_CHIMINTEN) {
            /* Disable all host interrupts as driver requests */
            s->chimint_status &= ~CHIMINT_MASK;
        }
        pcibase_update_irq(s);
        break;
    case (COMSTATEN - REG_BASE_ADDR):
        /* Driver enables COMSTAT error bits here */
        s->comstaten_reg = v32;
        break;
    case (DCHSTATUS - REG_BASE_ADDR):
        /* Driver enables CFIFTOERR bit here */
        s->dchstatus_reg = v32;
        break;
    default:
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    (void)s;
    (void)addr;
    (void)val;
    (void)size;
}

static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    s->chiminten_reg = 0;
    s->comstaten_reg = 0;
    s->dchstatus_reg = 0;
    s->chimint_status = 0;

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }
}

static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    (void)s;
    (void)errp;
}

static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    uint32_t val = pci_default_read_config(pdev, addr, len);

    switch (len) {
    case 1:
        val &= 0xFF;
        break;
    case 2:
        val &= 0xFFFF;
        break;
    case 4:
    default:
        break;
    }
    return val;
}

static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len)
{
    if (addr >= PCI_BASE_ADDRESS_0 && addr <= PCI_BASE_ADDRESS_5) {
        pci_default_write_config(pdev, addr, val, len);
        return;
    }
    pci_default_write_config(pdev, addr, val, len);
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID,  PCI_VENDOR_ID_ADAPTEC2);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x0410);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_SCSI);
    pci_set_byte(pci_conf + PCI_REVISION_ID, AIC9410_DEV_REV_B0);
    pci_config_set_interrupt_pin(pci_conf, 1);

    s->num_bars = 3;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = 0x1000;
    s->bar_info[0].name = "aic94xx-mmio0";
    s->bar_info[0].sparse = false;

    s->bar_info[1].index = 2;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x1000;
    s->bar_info[1].name = "aic94xx-mmio1";
    s->bar_info[1].sparse = false;

    s->bar_info[2].index = PCI_IOBAR_OFFSET;
    s->bar_info[2].type = BAR_TYPE_PIO;
    s->bar_info[2].size = 0x100;
    s->bar_info[2].name = "aic94xx-io";
    s->bar_info[2].sparse = false;

    s->chiminten_reg = 0;
    s->comstaten_reg = 0;
    s->dchstatus_reg = 0;
    s->chimint_status = 0;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    s->has_msi = false;
    s->has_msix = false;

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    if (s->has_msix) {
        msix_uninit(pdev, NULL, 0);
        s->has_msix = false;
    }
    if (s->has_msi) {
        msi_uninit(pdev);
        s->has_msi = false;
    }

    if (s->mmio_backing) {
        g_free(s->mmio_backing);
        s->mmio_backing = NULL;
    }
}

static void pcibase_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->config_read  = pcibase_config_read;
    k->config_write = pcibase_config_write;
    k->realize = pcibase_realize;
    k->exit    = pcibase_uninit;
    dc->reset  = pcibase_reset;

    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static void pcibase_register_types(void)
{
    static InterfaceInfo interfaces[] = {
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
