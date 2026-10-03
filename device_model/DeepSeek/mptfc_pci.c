/*
 * This QEMU PCI device model emulates an LSI Logic FC909 controller
 * for the mptfc driver. It supports MMIO register access with
 * generic sub-word handling to ensure driver compatibility.
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

/* Basic type definitions matching the MPI driver environment */
typedef uint8_t   U8;
typedef uint16_t  U16;
typedef uint32_t  U32;
typedef uint64_t  U64;
typedef int8_t    S8;
typedef int16_t   S16;
typedef int32_t   S32;
typedef int64_t   S64;

#define TYPE_PCIBASE_DEVICE "mptfc_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor and Device IDs from mptfc_pci_table[0] */
#define VENDOR_ID 0x1000 /* PCI_VENDOR_ID_LSI_LOGIC */
#define DEVICE_ID 0x0621 /* MPI_MANUFACTPAGE_DEVICEID_FC909 */
#define CLASS_ID 0x0100 /* PCI_CLASS_STORAGE_SCSI */

/* MPI Register Offsets and Bit Definitions from mptfc.c */
#define MPI_DOORBELL_ACTIVE                 (0x08000000)
#define MPI_DOORBELL_DATA_MASK              (0x0000FFFF)
#define MPI_DOORBELL_ADD_DWORDS_SHIFT       (16)
#define MPI_DOORBELL_FUNCTION_SHIFT         (24)
#define MPI_DOORBELL_WHO_INIT_MASK          (0x07000000)
#define MPI_DOORBELL_WHO_INIT_SHIFT         (24)
#define MPI_HIS_DOORBELL_INTERRUPT          (0x00000001)
#define MPI_HIS_IOP_DOORBELL_STATUS         (0x80000000)
#define MPI_IOC_STATE_OPERATIONAL           (0x20000000)
#define MPI_IOC_STATE_MASK                  (0xF0000000)
#define MPI_IOC_STATE_SHIFT                 (28)
#define MPI_IOC_STATE_FAULT                 (0x40000000)
#define MPI_IOC_STATE_RESET                 (0x00000000)
#define MPI_IOC_STATE_READY                 (0x10000000)
#define MPI_FUNCTION_CONFIG                         (0x04)
#define MPI_FUNCTION_SCSI_IO_REQUEST                (0x00)
#define MPI_FUNCTION_SCSI_TASK_MGMT                 (0x01)
#define MPI_FUNCTION_IOC_FACTS                      (0x03)
#define MPI_FUNCTION_PORT_FACTS                     (0x05)
#define MPI_FUNCTION_EVENT_NOTIFICATION             (0x07)
#define MPI_FUNCTION_HANDSHAKE                      (0x42)
#define MPI_FUNCTION_IOC_INIT                       (0x02)
#define MPI_FUNCTION_IOC_MESSAGE_UNIT_RESET         (0x40)
#define MPI_FUNCTION_IO_UNIT_RESET                  (0x41)
#define MPI_FUNCTION_HOST_PAGEBUF_ACCESS_CONTROL    (0x44)
#define MPI_IOCSTATUS_MASK                      (0x7FFF)
#define MPI_IOCSTATUS_SUCCESS                   (0x0000)
#define MPI_IOCSTATUS_SCSI_IOC_TERMINATED       (0x004B)
#define MPI_IOCSTATUS_SCSI_TASK_TERMINATED      (0x0048)
#define MPI_IOCFACTS_CAPABILITY_HIGH_PRI_Q              (0x00000001)
#define MPI_IOCFACTS_FLAGS_FW_DOWNLOAD_BOOT             (0x01)
#define MPI_HIM_DIM                         (0x00000001)
#define MPI_VERSION_01_00                   (0x0100)
#define MPI_VERSION_01_02                   (0x0102)
#define MPI_VERSION_01_05                   (0x0105)
#define MPI_PORTFACTS_PROTOCOL_INITIATOR        (0x08)
#define MPI_PORTFACTS_PROTOCOL_TARGET           (0x04)
#define MPI_PORTFACTS_PROTOCOL_LAN              (0x02)
#define MPI_WHOINIT_HOST_DRIVER                         (0x04)
#define MPI_WHOINIT_PCI_PEER                            (0x03)
#define MPI_DB_HPBAC_FREE_BUFFER            (0x03)
#define MPI_DIAG_FLASH_BAD_SIG              (0x00000040)
#define MPI_DIAG_DISABLE_ARM                (0x00000002)
#define MPI_DIAG_RESET_ADAPTER              (0x00000004)
#define MPI_DIAG_DRWE                       (0x00000080)
#define MPI_DIAG_RESET_HISTORY              (0x00000020)
#define MPI_DIAG_CLEAR_FLASH_BAD_SIG        (0x00000400)
#define MPI_DIAG_RW_ENABLE                  (0x00000010)
#define MPI_DIAG_PREVENT_IOC_BOOT           (0x00000200)
#define MPI_WRSEQ_1ST_KEY_VALUE             (0x04)
#define MPI_WRSEQ_2ND_KEY_VALUE             (0x0B)
#define MPI_WRSEQ_3RD_KEY_VALUE             (0x02)
#define MPI_WRSEQ_4TH_KEY_VALUE             (0x07)
#define MPI_WRSEQ_5TH_KEY_VALUE             (0x0D)
#define MPI_CONFIG_PAGETYPE_FC_DEVICE               (0x06)
#define MPI_CONFIG_PAGETYPE_FC_PORT                 (0x05)
#define MPI_CONFIG_PAGETYPE_MASK                    (0x0F)
#define MPI_CONFIG_PAGETYPE_EXTENDED                (0x0F)
#define MPI_CONFIG_ACTION_PAGE_HEADER               (0x00)
#define MPI_CONFIG_ACTION_PAGE_READ_CURRENT         (0x01)
#define MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT        (0x02)
#define MPI_CONFIG_ACTION_PAGE_WRITE_NVRAM          (0x03)
#define MPI_FCPORTPAGE0_PORTSTATE_UNKNOWN               (0x01)
#define MPI_FCPORTPAGE0_PORTSTATE_ONLINE                (0x02)
#define MPI_FCPORTPAGE0_PORTSTATE_OFFLINE               (0x03)
#define MPI_FCPORTPAGE0_FLAGS_ATTACH_NO_INIT            (0x00000000)
#define MPI_FCPORTPAGE0_FLAGS_ATTACH_TYPE_MASK          (0x00000F00)
#define MPI_FCPORTPAGE0_FLAGS_ATTACH_POINT_TO_POINT     (0x00000100)
#define MPI_FCPORTPAGE0_FLAGS_ATTACH_PRIVATE_LOOP       (0x00000200)
#define MPI_FCPORTPAGE0_FLAGS_ATTACH_FABRIC_DIRECT      (0x00000400)
#define MPI_FCPORTPAGE0_FLAGS_ATTACH_PUBLIC_LOOP        (0x00000800)
#define MPI_FCPORTPAGE0_FLAGS_FABRIC_WWN_VALID          (0x00000040)
#define MPI_FCPORTPAGE0_SUPPORT_CLASS_1                 (0x00000001)
#define MPI_FCPORTPAGE0_SUPPORT_CLASS_2                 (0x00000002)
#define MPI_FCPORTPAGE0_SUPPORT_CLASS_3                 (0x00000004)
#define MPI_FCPORTPAGE0_SUPPORT_1GBIT_SPEED             (0x00000001)
#define MPI_FCPORTPAGE0_SUPPORT_2GBIT_SPEED             (0x00000002)
#define MPI_FCPORTPAGE0_SUPPORT_4GBIT_SPEED             (0x00000008)
#define MPI_FCPORTPAGE0_SUPPORT_10GBIT_SPEED            (0x00000004)
#define MPI_FCPORTPAGE0_SUPPORT_SPEED_UNKNOWN           (0x00000000)
#define MPI_FCPORTPAGE0_CURRENT_SPEED_1GBIT             MPI_FCPORTPAGE0_SUPPORT_1GBIT_SPEED
#define MPI_FCPORTPAGE0_CURRENT_SPEED_2GBIT             MPI_FCPORTPAGE0_SUPPORT_2GBIT_SPEED
#define MPI_FCPORTPAGE0_CURRENT_SPEED_4GBIT             MPI_FCPORTPAGE0_SUPPORT_4GBIT_SPEED
#define MPI_FCPORTPAGE0_CURRENT_SPEED_10GBIT            MPI_FCPORTPAGE0_SUPPORT_10GBIT_SPEED
#define MPI_FCPORTPAGE0_CURRENT_SPEED_UNKNOWN           MPI_FCPORTPAGE0_SUPPORT_SPEED_UNKNOWN
#define MPI_FCPORTPAGE1_FLAGS_IMMEDIATE_ERROR_REPLY     (0x04000000)
#define MPI_FCPORTPAGE1_FLAGS_VERBOSE_RESCAN_EVENTS     (0x01000000)
#define MPI_FC_DEVICE_PAGE0_PROT_FCP_TARGET             (0x02)
#define MPI_FC_DEVICE_PAGE0_PROT_FCP_INITIATOR          (0x04)
#define MPI_FC_DEVICE_PAGE0_FLAGS_TARGETID_BUS_VALID    (0x01)
#define MPI_FC_DEVICE_PAGE0_FLAGS_PRLI_INVALID          (0x04)
#define MPI_FC_DEVICE_PAGE0_FLAGS_PLOGI_INVALID         (0x02)
#define MPI_EVENT_LINK_STATUS_CHANGE            (0x00000007)
#define MPI_EVENT_RESCAN                        (0x00000006)
#define MPI_EVENT_IOC_BUS_RESET                 (0x00000004)
#define MPI_EVENT_EXT_BUS_RESET                 (0x00000005)
#define MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK            (0x01)
#define MPI_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET    (0x05)
#define MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS             (0x04)
#define MPI_SCSITASKMGMT_RSP_TM_NOT_SUPPORTED           (0x04)
#define MPI_SCSITASKMGMT_RSP_INVALID_FRAME              (0x02)
#define MPI_SCSITASKMGMT_RSP_TM_INVALID_LUN             (0x09)
#define MPI_SCSITASKMGMT_RSP_TM_FAILED                  (0x05)
#define MPI_SCSITASKMGMT_RSP_IO_QUEUED_ON_IOC           (0x80)
#define MPI_SCSITASKMGMT_RSP_TM_SUCCEEDED               (0x08)
#define MPI_SCSITASKMGMT_RSP_TM_COMPLETE                (0x00)
#define MPI_SCSITASKMGMT_MSGFLAGS_LIPRESET_RESET_OPTION (0x04)
#define MPI_SCSIIO_CONTROL_NODATATRANSFER       (0x00000000)
#define MPI_SCSIIO_CONTROL_WRITE                (0x01000000)
#define MPI_SCSIIO_CONTROL_READ                 (0x02000000)
#define MPI_SCSIIO_CONTROL_DATADIRECTION_MASK   (0x03000000)
#define MPI_SCSIIO_CONTROL_UNTAGGED             (0x00000500)
#define MPI_SCSIIO_CONTROL_SIMPLEQ              (0x00000000)
#define MPI_SCSIIO_MSGFLGS_SENSE_WIDTH_32           (0x00)
#define MPI_SCSIIO_MSGFLGS_SENSE_WIDTH_64           (0x01)
#define MPI_SGE_FLAGS_SHIFT                     (24)
#define MPI_SGE_LENGTH_MASK                     (0x00FFFFFF)
#define MPT_SGE_FLAGS_LAST_ELEMENT              (0x80000000)
#define MPT_SGE_FLAGS_END_OF_BUFFER             (0x40000000)
#define MPT_SGE_FLAGS_END_OF_LIST               (0x01000000)
#define MPT_SGE_FLAGS_SIMPLE_ELEMENT            (0x10000000)
#define MPT_TRANSFER_IOC_TO_HOST                (0x00000000)
#define MPT_TRANSFER_HOST_TO_IOC                (0x04000000)
#define MPI_SGE_FLAGS_64_BIT_ADDRESSING             (0x02)
#define MPT_SGE_FLAGS_64_BIT_ADDRESSING \
	(MPI_SGE_FLAGS_64_BIT_ADDRESSING << MPI_SGE_FLAGS_SHIFT)
#define MPI_SGE_FLAGS_LOCAL_ADDRESS             (0x08)
#define MPI_SGE_FLAGS_CHAIN_ELEMENT             (0x30)
#define MPT_DEFAULT_FRAME_SIZE                    128
#define MPT_REPLY_FRAME_SIZE                      0x50
#define MPT_SENSE_BUFFER_ALLOC                    64
#define MPT_SENSE_BUFFER_SIZE                     MPT_SENSE_BUFFER_ALLOC
#define MPT_FC_CAN_QUEUE                           1024
#define MPT_SCSI_SG_DEPTH                         CONFIG_FUSION_MAX_SGE
#define MPT_DEFAULT_REPLY_DEPTH                    128
#define MPT_MAX_REQ_DEPTH                          1023
#define MPT_SCSI_CMD_PER_DEV_HIGH                  64
#define MPT_SCSI_CMD_PER_DEV_LOW                   32
#define MPT_SCANDV_BUSY                            (0x00000040)
#define MPT_ICFLAG_BUF_CAP                         0x01
#define MPT_ICFLAG_ECHO                            0x02
#define MPT_HOSTEVENT_IOC_BRINGUP                  0x91
#define MPT_HOSTEVENT_IOC_RECOVER                  0x92
#define C0_1030                                    0x08
#define XL_929                                     0x01
#define MPT_ULTRA160                               0x09
#define MPT_TARGET_FLAGS_Q_YES                     0x08
#define MPT_TARGET_FLAGS_RAID_COMPONENT            0x40
#define MPT_RPORT_INFO_FLAGS_REGISTERED            0x01
#define MPT_RPORT_INFO_FLAGS_MISSING               0x02
#define MPT_MGMT_STATUS_PENDING                    0x04
#define MPT_MGMT_STATUS_DID_IOCRESET               0x08
#define MPT_MGMT_STATUS_COMMAND_GOOD               0x02
#define MPT_MGMT_STATUS_RF_VALID                   0x01
#define MPT_POLLING_INTERVAL                       1000
#define MPT_FW_REV_MAGIC_ID_STRING                 "FwRev="
#define MPT_LINUX_VERSION_COMMON                   "3.04.20"
#define MPT_PROCFS_MPTBASEDIR                      "mpt"
#define MPT_HOST_NO_CHAIN                          (0xFFFFFFFF)
#define MPT_MAX_PROTOCOL_DRIVERS                   16
#define MPT_MAX_CALLBACKNAME_LEN                   49
#define MPT_FC_DEV_LOSS_TMO                        (60)
#define MPT_FC_MAX_LUN                             (16895)
#define MPT_FC_FW_DEVICE_TIMEOUT                   (1)
#define MPT_FC_FW_IO_PEND_TIMEOUT                  (1)
#define ON_FLAGS  (MPI_FCPORTPAGE1_FLAGS_IMMEDIATE_ERROR_REPLY)
#define OFF_FLAGS (MPI_FCPORTPAGE1_FLAGS_VERBOSE_RESCAN_EVENTS)

/* Hardware register map from SYSIF_REGS */
typedef struct _SYSIF_REGS {
    U32 Doorbell;            /* 0x00 */
    U32 WriteSequence;       /* 0x04 */
    U32 Diagnostic;          /* 0x08 */
    U32 TestBase;            /* 0x0C */
    U32 DiagRwData;          /* 0x10 */
    U32 DiagRwAddress;       /* 0x14 */
    U32 Reserved1[6];        /* 0x18-0x2C */
    U32 IntStatus;           /* 0x30 */
    U32 IntMask;             /* 0x34 */
    U32 Reserved2[2];        /* 0x38-0x3C */
    U32 RequestFifo;         /* 0x40 */
    U32 ReplyFifo;           /* 0x44 */
    U32 RequestHiPriFifo;    /* 0x48 */
    U32 Reserved3;           /* 0x4C */
    U32 HostIndex;           /* 0x50 */
    U32 Reserved4[15];       /* 0x54-0x8C */
    U32 Fubar;               /* 0x90 */
    U32 Reserved5[1050];     /* 0x94-0x10F8 */
    U32 Reset_1078;          /* 0x10FC */
} SYSIF_REGS;

/* MPI message structures used by the driver */
typedef struct _CONFIG_PAGE_HEADER {
    U8 PageVersion;
    U8 PageLength;
    U8 PageNumber;
    U8 PageType;
} CONFIG_PAGE_HEADER;

typedef struct _CONFIG_PAGE_FC_PORT_0 {
    CONFIG_PAGE_HEADER      Header;
    U32                     Flags;
    U8                      MPIPortNumber;
    U8                      LinkType;
    U8                      PortState;
    U8                      Reserved;
    U32                     PortIdentifier;
    U64                     WWNN;
    U64                     WWPN;
    U32                     SupportedServiceClass;
    U32                     SupportedSpeeds;
    U32                     CurrentSpeed;
    U32                     MaxFrameSize;
    U64                     FabricWWNN;
    U64                     FabricWWPN;
    U32                     DiscoveredPortsCount;
    U32                     MaxInitiators;
    U8                      MaxAliasesSupported;
    U8                      MaxHardAliasesSupported;
    U8                      NumCurrentAliases;
    U8                      Reserved1;
} CONFIG_PAGE_FC_PORT_0;

typedef struct _CONFIG_PAGE_FC_DEVICE_0 {
    CONFIG_PAGE_HEADER      Header;
    U64                     WWNN;
    U64                     WWPN;
    U32                     PortIdentifier;
    U8                      Protocol;
    U8                      Flags;
    U16                     BBCredit;
    U16                     MaxRxFrameSize;
    U8                      ADISCHardALPA;
    U8                      PortNumber;
    U8                      FcPhLowestVersion;
    U8                      FcPhHighestVersion;
    U8                      CurrentTargetID;
    U8                      CurrentBus;
} CONFIG_PAGE_FC_DEVICE_0;

typedef struct _CONFIG_PAGE_FC_PORT_1 {
    CONFIG_PAGE_HEADER      Header;
    U32                     Flags;
    U64                     NoSEEPROMWWNN;
    U64                     NoSEEPROMWWPN;
    U8                      HardALPA;
    U8                      LinkConfig;
    U8                      TopologyConfig;
    U8                      AltConnector;
    U8                      NumRequestedAliases;
    U8                      RR_TOV;
    U8                      InitiatorDeviceTimeout;
    U8                      InitiatorIoPendTimeout;
} CONFIG_PAGE_FC_PORT_1;

typedef struct _MSG_EVENT_NOTIFY_REPLY {
     U16                    EventDataLength;
     U8                     MsgLength;
     U8                     Function;
     U8                     Reserved1[2];
     U8                     AckRequired;
     U8                     MsgFlags;
     U32                    MsgContext;
     U8                     Reserved2[2];
     U16                    IOCStatus;
     U32                    IOCLogInfo;
     U32                    Event;
     U32                    EventContext;
     U32                    Data[];
} MSG_EVENT_NOTIFY_REPLY;

typedef struct _MSG_IOC_FACTS_REPLY {
    U16                     MsgVersion;                 /* 00h */
    U8                      MsgLength;                  /* 02h */
    U8                      Function;                   /* 03h */
    U16                     HeaderVersion;              /* 04h */
    U8                      IOCNumber;                  /* 06h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
    U16                     IOCExceptions;              /* 0Ch */
    U16                     IOCStatus;                  /* 0Eh */
    U32                     IOCLogInfo;                 /* 10h */
    U8                      MaxChainDepth;              /* 14h */
    U8                      WhoInit;                    /* 15h */
    U8                      BlockSize;                  /* 16h */
    U8                      Flags;                      /* 17h */
    U16                     ReplyQueueDepth;            /* 18h */
    U16                     RequestFrameSize;           /* 1Ah */
    U16                     Reserved_0101_FWVersion;    /* 1Ch */ /* obsolete 16-bit FWVersion */
    U16                     ProductID;                  /* 1Eh */
    U32                     CurrentHostMfaHighAddr;     /* 20h */
    U16                     GlobalCredits;              /* 24h */
    U8                      NumberOfPorts;              /* 26h */
    U8                      EventState;                 /* 27h */
    U32                     CurrentSenseBufferHighAddr; /* 28h */
    U16                     CurReplyFrameSize;          /* 2Ch */
    U8                      MaxDevices;                 /* 2Eh */
    U8                      MaxBuses;                   /* 2Fh */
    U32                     FWImageSize;                /* 30h */
    U32                     IOCCapabilities;            /* 34h */
    U32                     FWVersion[2];               /* 38h */  /* MPI_FW_VERSION is two U32 */
    U16                     HighPriorityQueueDepth;     /* 3Ch */
    U16                     Reserved2;                  /* 3Eh */
    /* SGE_SIMPLE_UNION HostPageBufferSGE *//* Not expanded here */
    U32                     HostPageBufferSGE[4];       /* 40h-4Ch, placeholder for union */
    U32                     ReplyFifoHostSignalingAddr; /* 4Ch */
} MSG_IOC_FACTS_REPLY;

typedef struct _MSG_PORT_FACTS_REPLY {
     U16                    Reserved;                   /* 00h */
     U8                     MsgLength;                  /* 02h */
     U8                     Function;                   /* 03h */
     U16                    Reserved1;                  /* 04h */
     U8                     PortNumber;                 /* 06h */
     U8                     MsgFlags;                   /* 07h */
     U32                    MsgContext;                 /* 08h */
     U16                    Reserved2;                  /* 0Ch */
     U16                    IOCStatus;                  /* 0Eh */
     U32                    IOCLogInfo;                 /* 10h */
     U8                     Reserved3;                  /* 14h */
     U8                     PortType;                   /* 15h */
     U16                    MaxDevices;                 /* 16h */
     U16                    PortSCSIID;                 /* 18h */
     U16                    ProtocolFlags;              /* 1Ah */
     U16                    MaxPostedCmdBuffers;        /* 1Ch */
     U16                    MaxPersistentIDs;           /* 1Eh */
     U16                    MaxLanBuckets;              /* 20h */
     U8                     MaxInitiators;              /* 22h */
     U8                     Reserved4;                  /* 23h */
     U32                    Reserved5;                  /* 24h */
} MSG_PORT_FACTS_REPLY;

typedef struct _MSG_REQUEST_HEADER {
    U8                      Reserved[2];      /* function specific */
    U8                      ChainOffset;
    U8                      Function;
    U8                      Reserved1[3];     /* function specific */
    U8                      MsgFlags;
    U32                     MsgContext;
} MSG_REQUEST_HEADER;

/* SGE structure for DMA data buffers */
typedef struct _SGE_SIMPLE64 {
    U32 FlagsLength;
    U32 Reserved;
    U64 Address;
} SGE_SIMPLE64;

typedef union _SGE_IO_UNION {
    SGE_SIMPLE64 simple;
} SGE_IO_UNION;

typedef struct _MSG_CONFIG {
    U8                      Action;                     /* 00h */
    U8                      Reserved;                   /* 01h */
    U8                      ChainOffset;                /* 02h */
    U8                      Function;                   /* 03h */
    U16                     ExtPageLength;              /* 04h */
    U8                      ExtPageType;                /* 06h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
    U8                      Reserved2[8];               /* 0Ch */
    CONFIG_PAGE_HEADER      Header;                     /* 14h */
    U32                     PageAddress;                /* 18h */
    SGE_IO_UNION            PageBufferSGE;              /* 1Ch */
} MSG_CONFIG;

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
    SYSIF_REGS regs;

    /* DMA Context */
    /* Placeholder: DMA descriptor and ring info */

    /* Operational status flags */
    uint32_t ioc_state;
    uint32_t diag;

    /* State used to handle reset sequences */
    bool reset_in_progress;

    /* Power management state (D0-D3) */
    uint8_t pm_state;

    /* WriteSequence validation state */
    uint8_t wr_seq_state;

    /* DMA base addresses obtained from IOC_INIT */
    dma_addr_t reply_frame_base;
    uint32_t reply_frame_size;
    uint32_t reply_index;
    uint32_t reply_max_index;

    /* Configuration page copies */
    CONFIG_PAGE_FC_PORT_0 port_page0;
    CONFIG_PAGE_FC_PORT_1 port_page1;
};

/* Internal helper for status-triggered signaling. G_GNUC_UNUSED prevents compiler warnings if unused. */
G_GNUC_UNUSED static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t pending = s->intr_status & s->intr_mask;

    if (pending) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (msi_enabled(pdev)) {
            /* MSI level is edge-triggered, no deassert needed */
        } else {
            pci_set_irq(pdev, 0);
        }
    }
}

/* Helper to post a reply frame and raise interrupt */
static void post_reply(PCIBaseState *s, uint8_t function, uint32_t msg_context, uint16_t ioc_status)
{
    if (!s->reply_frame_base || s->reply_index >= s->reply_max_index) {
        /* Fallback: ignore or wrap */
        if (s->reply_index >= s->reply_max_index) {
            s->reply_index = 0;
        } else {
            return;
        }
    }

    /* Construct a minimal reply header: 12 bytes header + 2 bytes IOCStatus = 14 bytes, but reply frame size is 0x50, we zero rest */
    uint8_t reply[MPT_REPLY_FRAME_SIZE];
    memset(reply, 0, sizeof(reply));
    /* Set Function */
    reply[3] = function;
    /* Set MsgContext */
    *(uint32_t *)(reply + 8) = cpu_to_le32(msg_context);
    /* Set IOCStatus at offset 0x0E? Actually in MPI reply header, IOCStatus is at offset 0x0E */
    *(uint16_t *)(reply + 0x0E) = cpu_to_le16(ioc_status);

    dma_addr_t reply_addr = s->reply_frame_base + (dma_addr_t)s->reply_index * MPT_REPLY_FRAME_SIZE;
    pci_dma_write(PCI_DEVICE(s), reply_addr, reply, sizeof(reply));
    s->reply_index++;
    s->intr_status |= MPI_HIS_DOORBELL_INTERRUPT;
    pcibase_update_irq(s);
}

/* Helper to read a 32-bit register value (aligned address, no side effects) */
static uint32_t pcibase_read32(PCIBaseState *s, hwaddr addr)
{
    switch (addr) {
    case 0x00: return s->ioc_state & MPI_IOC_STATE_MASK;
    case 0x04: return s->regs.WriteSequence;
    case 0x08: return s->regs.Diagnostic;
    case 0x0C: return s->regs.TestBase;
    case 0x10: return s->regs.DiagRwData;
    case 0x14: return s->regs.DiagRwAddress;
    case 0x30: return s->intr_status;
    case 0x34: return s->intr_mask;
    case 0x40: return s->regs.RequestFifo;
    case 0x44: return s->reply_index;
    case 0x48: return s->regs.RequestHiPriFifo;
    case 0x50: return s->regs.HostIndex;
    case 0x90: return s->regs.Fubar;
    case 0x10FC: return s->regs.Reset_1078;
    default: return 0;
    }
}

static void handle_config_request(PCIBaseState *s, dma_addr_t req_addr);

/* Helper to write a 32-bit register value (aligned address, triggers side effects) */
static void pcibase_write32(PCIBaseState *s, hwaddr addr, uint32_t val)
{
    switch (addr) {
    case 0x00: /* Doorbell */
    {
        uint8_t func = (val >> MPI_DOORBELL_FUNCTION_SHIFT) & 0xFF;
        uint16_t data = val & MPI_DOORBELL_DATA_MASK;

        if (func == MPI_FUNCTION_IOC_INIT) {
            dma_addr_t req_addr = s->regs.RequestFifo;
            if (req_addr) {
                dma_addr_t reply_base;
                pci_dma_read(PCI_DEVICE(s), req_addr + 12, &reply_base, sizeof(reply_base));
                s->reply_frame_base = le64_to_cpu(reply_base);
                s->ioc_state = MPI_IOC_STATE_OPERATIONAL;
                s->reply_index = 0;

                uint32_t msg_context;
                pci_dma_read(PCI_DEVICE(s), req_addr + 8, &msg_context, sizeof(msg_context));
                msg_context = le32_to_cpu(msg_context);
                post_reply(s, MPI_FUNCTION_IOC_INIT, msg_context, MPI_IOCSTATUS_SUCCESS);
            }
        } else if (func == MPI_FUNCTION_IOC_FACTS) {
            dma_addr_t req_addr = s->regs.RequestFifo;
            if (req_addr && s->reply_frame_base) {
                uint32_t msg_context;
                pci_dma_read(PCI_DEVICE(s), req_addr + 8, &msg_context, sizeof(msg_context));
                msg_context = le32_to_cpu(msg_context);

                MSG_IOC_FACTS_REPLY reply = {0};
                reply.MsgVersion = MPI_VERSION_01_05;
                reply.MsgLength = sizeof(MSG_IOC_FACTS_REPLY) / 4;
                reply.Function = MPI_FUNCTION_IOC_FACTS;
                reply.HeaderVersion = 0x0100;
                reply.MsgContext = msg_context;
                reply.IOCStatus = MPI_IOCSTATUS_SUCCESS;
                reply.MaxChainDepth = 1;
                reply.WhoInit = MPI_WHOINIT_HOST_DRIVER;
                reply.BlockSize = 12;
                reply.Flags = MPI_IOCFACTS_FLAGS_FW_DOWNLOAD_BOOT;
                reply.ReplyQueueDepth = 128;
                reply.RequestFrameSize = 128;
                reply.ProductID = 0x0621;
                reply.GlobalCredits = 128;
                reply.NumberOfPorts = 1;
                reply.CurReplyFrameSize = 128;
                reply.MaxDevices = 256;
                reply.MaxBuses = 1;
                reply.IOCCapabilities = MPI_IOCFACTS_CAPABILITY_HIGH_PRI_Q;
                reply.HighPriorityQueueDepth = 0;

                dma_addr_t reply_addr = s->reply_frame_base + s->reply_index * MPT_REPLY_FRAME_SIZE;
                pci_dma_write(PCI_DEVICE(s), reply_addr, &reply, sizeof(reply));
                s->reply_index++;
                s->intr_status |= MPI_HIS_DOORBELL_INTERRUPT;
                pcibase_update_irq(s);
            }
        } else if (func == MPI_FUNCTION_PORT_FACTS) {
            dma_addr_t req_addr = s->regs.RequestFifo;
            if (req_addr && s->reply_frame_base) {
                uint32_t msg_context;
                pci_dma_read(PCI_DEVICE(s), req_addr + 8, &msg_context, sizeof(msg_context));
                msg_context = le32_to_cpu(msg_context);

                MSG_PORT_FACTS_REPLY reply = {0};
                reply.MsgLength = sizeof(MSG_PORT_FACTS_REPLY) / 4;
                reply.Function = MPI_FUNCTION_PORT_FACTS;
                reply.PortNumber = 0;
                reply.MsgContext = msg_context;
                reply.IOCStatus = MPI_IOCSTATUS_SUCCESS;
                reply.PortType = 0; /* FC */
                reply.MaxDevices = 256;
                reply.ProtocolFlags = MPI_PORTFACTS_PROTOCOL_INITIATOR;
                reply.MaxPostedCmdBuffers = 128;
                reply.MaxInitiators = 256;

                dma_addr_t reply_addr = s->reply_frame_base + s->reply_index * MPT_REPLY_FRAME_SIZE;
                pci_dma_write(PCI_DEVICE(s), reply_addr, &reply, sizeof(reply));
                s->reply_index++;
                s->intr_status |= MPI_HIS_DOORBELL_INTERRUPT;
                pcibase_update_irq(s);
            }
        } else if (func == MPI_FUNCTION_CONFIG) {
            dma_addr_t req_addr = s->regs.RequestFifo;
            if (req_addr) {
                handle_config_request(s, req_addr);
            }
        } else if (func == MPI_FUNCTION_EVENT_NOTIFICATION) {
            dma_addr_t req_addr = s->regs.RequestFifo;
            if (req_addr) {
                uint32_t msg_context;
                pci_dma_read(PCI_DEVICE(s), req_addr + 8, &msg_context, sizeof(msg_context));
                msg_context = le32_to_cpu(msg_context);
                post_reply(s, MPI_FUNCTION_EVENT_NOTIFICATION, msg_context, MPI_IOCSTATUS_SUCCESS);
            }
        }
        break;
    }
    case 0x04: /* WriteSequence */
    {
        static const uint32_t wr_seq[] = {
            MPI_WRSEQ_1ST_KEY_VALUE,
            MPI_WRSEQ_2ND_KEY_VALUE,
            MPI_WRSEQ_3RD_KEY_VALUE,
            MPI_WRSEQ_4TH_KEY_VALUE,
            MPI_WRSEQ_5TH_KEY_VALUE
        };
        if (s->wr_seq_state < 5 && val == wr_seq[s->wr_seq_state]) {
            s->wr_seq_state++;
        } else {
            s->wr_seq_state = 0;
        }
        s->regs.WriteSequence = val;
        break;
    }
    case 0x08: /* Diagnostic */
    {
        if (val & MPI_DIAG_DRWE) {
            s->regs.Diagnostic = val;
            s->ioc_state = MPI_IOC_STATE_READY;
            if (val & MPI_DIAG_RESET_ADAPTER) {
                s->reply_index = 0;
                s->intr_status = 0;
                pcibase_update_irq(s);
                s->ioc_state = MPI_IOC_STATE_READY;
            }
            s->wr_seq_state = 0;
        } else if (s->wr_seq_state == 5) {
            s->regs.Diagnostic = val;
            if (val & MPI_DIAG_RESET_ADAPTER) {
                s->ioc_state = MPI_IOC_STATE_RESET;
                s->reply_index = 0;
                s->intr_status = 0;
                pcibase_update_irq(s);
            }
            s->wr_seq_state = 0;
        }
        break;
    }
    case 0x30: /* IntStatus */
        s->intr_status &= ~val;
        pcibase_update_irq(s);
        break;
    case 0x34: /* IntMask */
        s->intr_mask = val;
        pcibase_update_irq(s);
        break;
    case 0x40: /* RequestFifo */
        s->regs.RequestFifo = val;
        break;
    case 0x44: /* ReplyFifo */
        break;
    case 0x48: /* RequestHiPriFifo */
        s->regs.RequestHiPriFifo = val;
        break;
    case 0x50: /* HostIndex */
        s->regs.HostIndex = val;
        break;
    case 0x90: /* Fubar */
        s->regs.Fubar = val;
        break;
    case 0x10FC: /* Reset_1078 */
        s->regs.Reset_1078 = val;
        break;
    default:
        break;
    }
}

static void handle_config_request(PCIBaseState *s, dma_addr_t req_addr)
{
    MSG_CONFIG req;
    pci_dma_read(PCI_DEVICE(s), req_addr, &req, sizeof(req));

    uint8_t action = req.Action;
    uint8_t page_type = req.Header.PageType & MPI_CONFIG_PAGETYPE_MASK;
    uint8_t page_number = req.Header.PageNumber;
    uint32_t msg_context = req.MsgContext;

    bool success = true;

    /* Extract SGE buffer address from PageBufferSGE */
    dma_addr_t buf_addr = le64_to_cpu(req.PageBufferSGE.simple.Address);
    uint32_t buf_len = le32_to_cpu(req.PageBufferSGE.simple.FlagsLength) & MPI_SGE_LENGTH_MASK;

    if (action == MPI_CONFIG_ACTION_PAGE_READ_CURRENT) {
        if (page_type == MPI_CONFIG_PAGETYPE_FC_PORT && page_number == 0) {
            CONFIG_PAGE_FC_PORT_0 page = s->port_page0;
            if (buf_len < sizeof(page)) {
                success = false;
            } else {
                pci_dma_write(PCI_DEVICE(s), buf_addr, &page, sizeof(page));
            }
        } else if (page_type == MPI_CONFIG_PAGETYPE_FC_DEVICE && page_number == 0) {
            CONFIG_PAGE_FC_DEVICE_0 page = {0};
            page.Header.PageVersion = 1;
            page.Header.PageLength = sizeof(CONFIG_PAGE_FC_DEVICE_0) / 4;
            page.Header.PageNumber = 0;
            page.Header.PageType = MPI_CONFIG_PAGETYPE_FC_DEVICE;
            if (buf_len < sizeof(page)) {
                success = false;
            } else {
                pci_dma_write(PCI_DEVICE(s), buf_addr, &page, sizeof(page));
            }
        } else {
            success = false;
        }
    } else if (action == MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT) {
        if (page_type == MPI_CONFIG_PAGETYPE_FC_PORT && page_number == 1) {
            CONFIG_PAGE_FC_PORT_1 page;
            if (buf_len < sizeof(page)) {
                success = false;
            } else {
                pci_dma_read(PCI_DEVICE(s), buf_addr, &page, sizeof(page));
                s->port_page1 = page;
            }
        } else {
            success = false;
        }
    } else {
        success = false;
    }

    uint16_t ioc_status = success ? MPI_IOCSTATUS_SUCCESS : MPI_IOCSTATUS_SCSI_IOC_TERMINATED;
    post_reply(s, MPI_FUNCTION_CONFIG, msg_context, ioc_status);
}

/* New MMIO/PIO Handlers with generic access size support */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    hwaddr aligned_addr = addr & ~3;
    unsigned offset = addr & 3;
    uint32_t lo, hi;
    uint64_t val;

    if (aligned_addr >= sizeof(SYSIF_REGS)) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "%s: bad read at 0x%" HWADDR_PRIx ", size %u\n",
            __func__, addr, size);
        return ~0ULL >> (64 - size * 8);
    }

    lo = pcibase_read32(s, aligned_addr);
    if (size <= 4) {
        val = lo >> (offset * 8) & ((1ULL << (size * 8)) - 1);
    } else {
        /* size == 8 */
        hi = pcibase_read32(s, aligned_addr + 4);
        uint8_t buf[8];
        memcpy(buf, &lo, 4);
        memcpy(buf + 4, &hi, 4);
        memcpy(&val, buf + offset, size);
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    hwaddr aligned_addr = addr & ~3;
    unsigned offset = addr & 3;

    if (aligned_addr >= sizeof(SYSIF_REGS)) {
        qemu_log_mask(LOG_GUEST_ERROR,
            "%s: bad write at 0x%" HWADDR_PRIx ", size %u, val 0x%" PRIx64 "\n",
            __func__, addr, size, val);
        return;
    }

    if (size == 4) {
        pcibase_write32(s, aligned_addr, (uint32_t)val);
    } else if (size == 8) {
        uint32_t lo = pcibase_read32(s, aligned_addr);
        uint32_t hi = pcibase_read32(s, aligned_addr + 4);
        uint8_t buf[8];
        memcpy(buf, &lo, 4);
        memcpy(buf + 4, &hi, 4);
        memcpy(buf + offset, &val, size);
        memcpy(&lo, buf, 4);
        memcpy(&hi, buf + 4, 4);
        pcibase_write32(s, aligned_addr, lo);
        pcibase_write32(s, aligned_addr + 4, hi);
    } else {
        /* size 1 or 2 */
        uint32_t reg_val = pcibase_read32(s, aligned_addr);
        uint8_t *dst = (uint8_t *)&reg_val;
        memcpy(dst + offset, &val, size);
        pcibase_write32(s, aligned_addr, reg_val);
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

    memset(&s->regs, 0, sizeof(s->regs));
    s->ioc_state = MPI_IOC_STATE_RESET;
    s->diag = 0;
    s->intr_status = 0;
    s->intr_mask = 0;
    s->wr_seq_state = 0;
    s->reply_frame_base = 0;
    s->reply_index = 0;
    s->reply_max_index = MPT_DEFAULT_REPLY_DEPTH;

    memset(&s->port_page0, 0, sizeof(s->port_page0));
    s->port_page0.Header.PageVersion = 1;
    s->port_page0.Header.PageLength = sizeof(CONFIG_PAGE_FC_PORT_0) / 4;
    s->port_page0.Header.PageNumber = 0;
    s->port_page0.Header.PageType = MPI_CONFIG_PAGETYPE_FC_PORT;
    s->port_page0.Flags = MPI_FCPORTPAGE0_FLAGS_ATTACH_POINT_TO_POINT | MPI_FCPORTPAGE0_FLAGS_FABRIC_WWN_VALID;
    s->port_page0.MPIPortNumber = 0;
    s->port_page0.LinkType = 0;
    s->port_page0.PortState = MPI_FCPORTPAGE0_PORTSTATE_ONLINE;
    s->port_page0.WWNN = 0x10000000c0ffee00ULL;
    s->port_page0.WWPN = 0x20000000c0ffee00ULL;
    s->port_page0.SupportedServiceClass = MPI_FCPORTPAGE0_SUPPORT_CLASS_3;
    s->port_page0.SupportedSpeeds = MPI_FCPORTPAGE0_SUPPORT_2GBIT_SPEED | MPI_FCPORTPAGE0_SUPPORT_4GBIT_SPEED;
    s->port_page0.CurrentSpeed = MPI_FCPORTPAGE0_CURRENT_SPEED_4GBIT;
    s->port_page0.MaxFrameSize = 2048;
    s->port_page0.FabricWWNN = 0x10000000deadbeefULL;
    s->port_page0.FabricWWPN = 0x20000000deadbeefULL;
    s->port_page0.MaxInitiators = 256;

    memset(&s->port_page1, 0, sizeof(s->port_page1));
    s->port_page1.Header.PageVersion = 1;
    s->port_page1.Header.PageLength = sizeof(CONFIG_PAGE_FC_PORT_1) / 4;
    s->port_page1.Header.PageNumber = 1;
    s->port_page1.Header.PageType = MPI_CONFIG_PAGETYPE_FC_PORT;
    s->port_page1.Flags = OFF_FLAGS;
}

static void pcibase_register_bar(PCIDevice *pdev, PCIBaseState *s, BARInfo *bi, Error **errp)
{
    if (!bi || bi->type == BAR_TYPE_NONE) {
        return;
    }
    
    hwaddr aligned_size = pow2ceil(bi->size);
    MemoryRegion *mr = &s->bar_regions[bi->index];

    if (bi->type == BAR_TYPE_MMIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
        pci_register_bar(pdev, bi->index, PCI_BASE_ADDRESS_SPACE_MEMORY, mr);
    } else if (bi->type == BAR_TYPE_PIO) {
        memory_region_init_io(mr, OBJECT(s), &pcibase_mmio_ops, s, bi->name, aligned_size);
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

    pci_set_word(pci_conf + PCI_VENDOR_ID,  VENDOR_ID);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  DEVICE_ID);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, CLASS_ID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003); 
    }

    s->num_bars = 1;
    s->bar_info[0] = (BARInfo) { .index = 0, .type = BAR_TYPE_MMIO, .size = 0x2000, .name = "mptfc-mmio" };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    if (msi_init(pdev, 0, 1, true, false, errp)) {
        return;
    }

    s->ioc_state = MPI_IOC_STATE_RESET;
    s->reset_in_progress = false;
    s->wr_seq_state = 0;
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
    .name = "mptfc_pci",
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
