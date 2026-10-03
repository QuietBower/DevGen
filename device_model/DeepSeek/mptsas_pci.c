/*
 * QEMU PCI device model for Fusion MPT SAS controller.
 * Based on Linux driver: drivers/message/fusion/mptsas.c
 * Target: QEMU 8.2.10
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

/* MPI Constants from Linux driver (drivers/message/fusion/mptsas.c) */
#define MPI_IOC_STATE_OPERATIONAL           (0x20000000)

#define MPI_SAS_DEVICE_PGAD_FORM_SHIFT              (28)
#define MPI_SAS_DEVICE_PGAD_FORM_BUS_TARGET_ID      (0x00000001)
#define MPI_SAS_DEVICE_PGAD_FORM_HANDLE             (0x00000002)
#define MPI_SAS_DEVICE_PGAD_FORM_GET_NEXT_HANDLE    (0x00000000)

#define MPI_SAS_IOUNIT0_RATE_PHY_DISABLED                   (0x01)
#define MPI_SAS_IOUNIT0_RATE_FAILED_SPEED_NEGOTIATION       (0x02)
#define MPI_SAS_IOUNIT0_RATE_1_5                            (0x08)
#define MPI_SAS_IOUNIT0_RATE_3_0                            (0x09)
#define MPI_SAS_IOUNIT0_RATE_6_0                            (0x0A)
#define MPI_SAS_IOUNIT0_RATE_SATA_OOB_COMPLETE              (0x03)
#define MPI_SAS_IOUNIT0_RATE_UNKNOWN                        (0x00)

#define MPI_SAS_PHY0_PRATE_MAX_RATE_MASK                        (0xF0)
#define MPI_SAS_PHY0_HWRATE_MAX_RATE_1_5                        (0x80)
#define MPI_SAS_PHY0_PRATE_MAX_RATE_3_0                         (0x90)
#define MPI_SAS_PHY0_PRATE_MIN_RATE_MASK                        (0x0F)
#define MPI_SAS_PHY0_HWRATE_MIN_RATE_1_5                        (0x08)
#define MPI_SAS_PHY0_PRATE_MIN_RATE_3_0                         (0x09)
#define MPI_SAS_PHY0_HWRATE_MIN_RATE_MASK                       (0x0F)

#define MPI_SAS_DEVICE_INFO_SSP_TARGET          (0x00000400)
#define MPI_SAS_DEVICE_INFO_STP_TARGET          (0x00000200)
#define MPI_SAS_DEVICE_INFO_SATA_DEVICE         (0x00000080)
#define MPI_SAS_DEVICE_INFO_END_DEVICE          (0x00000001)
#define MPI_SAS_DEVICE_INFO_SSP_INITIATOR       (0x00000040)
#define MPI_SAS_DEVICE_INFO_STP_INITIATOR       (0x00000020)
#define MPI_SAS_DEVICE_INFO_SMP_INITIATOR       (0x00000010)
#define MPI_SAS_DEVICE_INFO_SATA_HOST           (0x00000008)
#define MPI_SAS_DEVICE_INFO_MASK_DEVICE_TYPE    (0x00000007)
#define MPI_SAS_DEVICE_INFO_NO_DEVICE           (0x00000000)
#define MPI_SAS_DEVICE_INFO_EDGE_EXPANDER       (0x00000002)
#define MPI_SAS_DEVICE_INFO_FANOUT_EXPANDER     (0x00000003)
#define MPI_SAS_DEVICE_INFO_SMP_TARGET          (0x00000100)

#define MPI_CONFIG_PAGETYPE_RAID_VOLUME             (0x08)
#define MPI_CONFIG_ACTION_PAGE_HEADER               (0x00)
#define MPI_CONFIG_ACTION_PAGE_READ_CURRENT         (0x01)
#define MPI_CONFIG_PAGETYPE_EXTENDED                (0x0F)
#define MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT          (0x10)
#define MPI_CONFIG_EXTPAGETYPE_SAS_PHY              (0x13)
#define MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE           (0x12)
#define MPI_CONFIG_EXTPAGETYPE_SAS_EXPANDER         (0x11)
#define MPI_CONFIG_EXTPAGETYPE_ENCLOSURE            (0x15)

#define MPI_SASENCLOSURE0_PAGEVERSION       (0x01)
#define MPI_SASIOUNITPAGE0_PAGEVERSION      (0x04)
#define MPI_SASIOUNITPAGE1_PAGEVERSION      (0x07)
#define MPI_SASPHY0_PAGEVERSION             (0x01)
#define MPI_SASPHY1_PAGEVERSION             (0x00)
#define MPI_SASDEVICE0_PAGEVERSION          (0x05)
#define MPI_SASEXPANDER0_PAGEVERSION        (0x03)
#define MPI_SASEXPANDER1_PAGEVERSION        (0x01)

#define MPI_EVENT_SAS_DEVICE_STATUS_CHANGE      (0x0000000F)
#define MPI_EVENT_SAS_EXPANDER_STATUS_CHANGE    (0x0000001B)
#define MPI_EVENT_SAS_BROADCAST_PRIMITIVE       (0x00000017)
#define MPI_EVENT_INTEGRATED_RAID               (0x0000000B)
#define MPI_EVENT_IR2                           (0x00000015)
#define MPI_EVENT_PERSISTENT_TABLE_FULL         (0x00000011)
#define MPI_EVENT_SAS_PHY_LINK_STATUS           (0x00000012)
#define MPI_EVENT_QUEUE_FULL                    (0x0000000E)
#define MPI_EVENT_SAS_DISCOVERY                 (0x00000016)

#define MPI_FUNCTION_SCSI_TASK_MGMT                 (0x01)
#define MPI_FUNCTION_SAS_IO_UNIT_CONTROL            (0x1B)
#define MPI_FUNCTION_SMP_PASSTHROUGH                (0x1A)

#define MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET          (0x03)
#define MPI_SCSITASKMGMT_TASKTYPE_QUERY_TASK            (0x07)
#define MPI_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET         (0x02)
#define MPI_SCSITASKMGMT_MSGFLAGS_LIPRESET_RESET_OPTION (0x04)

#define MPI_IOCSTATUS_SUCCESS                   (0x0000)
#define MPI_IOCSTATUS_CONFIG_INVALID_PAGE       (0x0022)

#define MPI_SAS_OP_PHY_HARD_RESET               (0x07)
#define MPI_SAS_OP_PHY_LINK_RESET               (0x06)
#define MPI_SAS_OP_CLEAR_NOT_PRESENT            (0x01)
#define MPI_SAS_OP_CLEAR_ALL_PERSISTENT         (0x02)

#define MPI_EVENT_SAS_DEV_STAT_RC_ADDED                     (0x03)
#define MPI_EVENT_SAS_DEV_STAT_RC_NOT_RESPONDING            (0x04)
#define MPI_EVENT_SAS_DEV_STAT_RC_NO_PERSIST_ADDED          (0x06)
#define MPI_EVENT_SAS_DEV_STAT_RC_SMART_DATA                (0x05)
#define MPI_EVENT_SAS_DEV_STAT_RC_INTERNAL_DEVICE_RESET     (0x08)
#define MPI_EVENT_SAS_EXP_RC_ADDED                      (0x00)
#define MPI_EVENT_SAS_EXP_RC_NOT_RESPONDING             (0x01)
#define MPI_EVENT_RAID_RC_PHYSDISK_DELETED              (0x06)
#define MPI_EVENT_RAID_RC_PHYSDISK_CREATED              (0x05)
#define MPI_EVENT_RAID_RC_PHYSDISK_STATUS_CHANGED       (0x08)
#define MPI_EVENT_RAID_RC_VOLUME_DELETED                (0x01)
#define MPI_EVENT_RAID_RC_VOLUME_CREATED                (0x00)
#define MPI_EVENT_RAID_RC_VOLUME_STATUS_CHANGED         (0x03)
#define MPI_EVENT_IR2_RC_FOREIGN_CFG_DETECTED       (0x06)
#define MPI_EVENT_IR2_RC_DUAL_PORT_REMOVED          (0x09)
#define MPI_EVENT_IR2_RC_DUAL_PORT_ADDED            (0x08)

#define MPI_RAIDVOL0_STATUS_FLAG_ENABLED                (0x01)
#define MPI_RAIDVOL0_STATUS_FLAG_VOLUME_INACTIVE        (0x08)
#define MPI_RAIDVOL0_STATUS_STATE_FAILED                (0x02)
#define MPI_RAIDVOL0_STATUS_STATE_MISSING               (0x03)
#define MPI_RAIDVOL0_STATUS_STATE_OPTIMAL               (0x00)
#define MPI_RAIDVOL0_STATUS_STATE_DEGRADED              (0x01)

#define MPI_PD_STATE_ONLINE                         (0x00)
#define MPI_PD_STATE_NOT_COMPATIBLE                 (0x02)
#define MPI_PD_STATE_FAILED                         (0x03)
#define MPI_PD_STATE_MISSING                        (0x01)
#define MPI_PD_STATE_OFFLINE_AT_HOST_REQUEST        (0x05)
#define MPI_PD_STATE_FAILED_AT_HOST_REQUEST         (0x06)
#define MPI_PD_STATE_OFFLINE_FOR_ANOTHER_REASON     (0xFF)

#define MPI_SAS_DEVICE0_FLAGS_DEVICE_PRESENT                (0x0001)
#define MPI_SAS_DEVICE0_FLAGS_DEVICE_MAPPED                 (0x0002)

#define MPI_SAS_ENCLOS_PGAD_FORM_HANDLE             (0x00000001)
#define MPI_SAS_ENCLOS_PGAD_FORM_SHIFT              (28)
#define MPI_SAS_PHY_PGAD_FORM_PHY_NUMBER            (0x0)
#define MPI_SAS_PHY_PGAD_FORM_SHIFT                 (28)
#define MPI_SAS_EXPAND_PGAD_FORM_HANDLE_PHY_NUM   (0x00000001)
#define MPI_SAS_EXPAND_PGAD_FORM_SHIFT            (28)
#define MPI_SAS_EXPAND_PGAD_FORM_GET_NEXT_HANDLE  (0x00000000)
#define MPI_SAS_EXPAND_PGAD_FORM_HANDLE           (0x00000002)

#define MPI_PORTFACTS_PROTOCOL_INITIATOR        (0x08)
#define MPI_SGE_FLAGS_SIMPLE_ELEMENT            (0x10)
#define MPI_SGE_FLAGS_END_OF_BUFFER             (0x40)
#define MPI_SGE_FLAGS_DIRECTION                 (0x04)
#define MPI_SGE_FLAGS_SYSTEM_ADDRESS            (0x00)
#define MPI_SGE_FLAGS_HOST_TO_IOC               (0x04)
#define MPI_SGE_FLAGS_IOC_TO_HOST               (0x00)
#define MPI_SGE_FLAGS_SHIFT                     (24)

#define MPI_SCSITASKMGMT_RSP_TM_SUCCEEDED               (0x08)
#define MPI_SCSITASKMGMT_RSP_IO_QUEUED_ON_IOC           (0x80)
#define MPI_EVENT_PRIMITIVE_ASYNCHRONOUS_EVENT  (0x04)
#define MPI_SAS_IOUNIT1_REPORT_MISSING_UNIT_16              (0x80)
#define MPI_SAS_IOUNIT1_REPORT_MISSING_TIMEOUT_MASK         (0x7F)

/* MPI Function Codes from supplementary source */
#define MPI_FUNCTION_IOC_FACTS                      (0x03)
#define MPI_FUNCTION_PORT_FACTS                     (0x05)
#define MPI_FUNCTION_CONFIG                         (0x04)

/* QEMU Device Types */
#define TYPE_PCIBASE_DEVICE "mptsas_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x1000
#define DEVICE_ID 0x0050
#define CLASS_ID  0x0107

/* Register Offsets from SYSIF_REGS */
#define REG_DOORBELL          0x00
#define REG_WRITE_SEQUENCE    0x04
#define REG_DIAGNOSTIC        0x08
#define REG_TEST_BASE         0x0C
#define REG_DIAG_RW_DATA      0x10
#define REG_DIAG_RW_ADDRESS   0x14
#define REG_INT_STATUS        0x30
#define REG_INT_MASK          0x34
#define REG_REQUEST_FIFO      0x40
#define REG_REPLY_FIFO        0x44
#define REG_REQUEST_HI_PRI_FIFO 0x48
#define REG_HOST_INDEX        0x50
#define REG_FUBAR             0x90
#define REG_RESET_1078        0x10FC
/* BAR0 size covers all registers up to 0x1100 */
#define BAR0_SIZE             0x1100

/*
 * MPI Message Structures from new driver source.
 */
typedef uint8_t   U8;
typedef uint16_t  U16;
typedef uint32_t  U32;
typedef uint64_t  U64;

/* SGE types (simplified) */
typedef struct {
    U32 FlagsLength;
    U32 Address;
} SGE_SIMPLE_UNION;

typedef struct {
    U32 FlagsLength;
    U32 Address;
    U32 NextChainOffset;
    U16 Length;
    U8  NextChainOffsetHigh;
    U8  Flags;
} SGE_IO_UNION;

/* Message Header (from supplementary source: MSG_REQUEST_HEADER) */
typedef struct {
    U8  Reserved[2];      /* function specific */
    U8  ChainOffset;
    U8  Function;
    U8  Reserved1[3];     /* function specific */
    U8  MsgFlags;
    U32 MsgContext;
} MPIHeader_t;

/* IOCFactsReply message structure */
typedef struct {
    U16 MsgVersion;                 /* 00h */
    U8  MsgLength;                  /* 02h */
    U8  Function;                   /* 03h */
    U16 HeaderVersion;              /* 04h */
    U8  IOCNumber;                  /* 06h */
    U8  MsgFlags;                   /* 07h */
    U32 MsgContext;                 /* 08h */
    U16 IOCExceptions;              /* 0Ch */
    U16 IOCStatus;                  /* 0Eh */
    U32 IOCLogInfo;                 /* 10h */
    U8  MaxChainDepth;              /* 14h */
    U8  WhoInit;                    /* 15h */
    U8  BlockSize;                  /* 16h */
    U8  Flags;                      /* 17h */
    U16 ReplyQueueDepth;            /* 18h */
    U16 RequestFrameSize;           /* 1Ah */
    U16 Reserved_0101_FWVersion;    /* 1Ch */
    U16 ProductID;                  /* 1Eh */
    U32 CurrentHostMfaHighAddr;     /* 20h */
    U16 GlobalCredits;              /* 24h */
    U8  NumberOfPorts;              /* 26h */
    U8  EventState;                 /* 27h */
    U32 CurrentSenseBufferHighAddr; /* 28h */
    U16 CurReplyFrameSize;          /* 2Ch */
    U8  MaxDevices;                 /* 2Eh */
    U8  MaxBuses;                   /* 2Fh */
    U32 FWImageSize;                /* 30h */
    U32 IOCCapabilities;            /* 34h */
    /* MPI_FW_VERSION is 4 bytes, but we represent as U32 */
    U32 FWVersion;                  /* 38h */
    U16 HighPriorityQueueDepth;     /* 3Ch */
    U16 Reserved2;                  /* 3Eh */
    SGE_SIMPLE_UNION HostPageBufferSGE; /* 40h */
    U32 ReplyFifoHostSignalingAddr; /* 4Ch */
} IOCFactsReply_t;

/* PortFactsReply message structure */
typedef struct {
    U16 Reserved;                   /* 00h */
    U8  MsgLength;                  /* 02h */
    U8  Function;                   /* 03h */
    U16 Reserved1;                  /* 04h */
    U8  PortNumber;                 /* 06h */
    U8  MsgFlags;                   /* 07h */
    U32 MsgContext;                 /* 08h */
    U16 Reserved2;                  /* 0Ch */
    U16 IOCStatus;                  /* 0Eh */
    U32 IOCLogInfo;                 /* 10h */
    U8  Reserved3;                  /* 14h */
    U8  PortType;                   /* 15h */
    U16 MaxDevices;                 /* 16h */
    U16 PortSCSIID;                 /* 18h */
    U16 ProtocolFlags;              /* 1Ah */
    U16 MaxPostedCmdBuffers;        /* 1Ch */
    U16 MaxPersistentIDs;           /* 1Eh */
    U16 MaxLanBuckets;              /* 20h */
    U8  MaxInitiators;              /* 22h */
    U8  Reserved4;                  /* 23h */
    U32 Reserved5;                  /* 24h */
} PortFactsReply_t;

/* Config message request */
typedef struct {
    U8  Action;                     /* 00h */
    U8  Reserved;                   /* 01h */
    U8  ChainOffset;                /* 02h */
    U8  Function;                   /* 03h */
    U16 ExtPageLength;              /* 04h */
    U8  ExtPageType;                /* 06h */
    U8  MsgFlags;                   /* 07h */
    U32 MsgContext;                 /* 08h */
    U8  Reserved2[8];               /* 0Ch */
    /* CONFIG_PAGE_HEADER at 14h */
    U8  PageType;                   /* 14h */
    U8  PageNumber;                 /* 15h */
    U8  PageVersion;                /* 16h */
    U8  Reserved3;                  /* 17h */
    U32 PageAddress;                /* 18h */
    SGE_IO_UNION PageBufferSGE;     /* 1Ch */
} ConfigRequest_t;

/* Config message reply */
typedef struct {
    U8  Action;                     /* 00h */
    U8  Reserved;                   /* 01h */
    U8  MsgLength;                  /* 02h */
    U8  Function;                   /* 03h */
    U16 ExtPageLength;              /* 04h */
    U8  ExtPageType;                /* 06h */
    U8  MsgFlags;                   /* 07h */
    U32 MsgContext;                 /* 08h */
    U8  Reserved2[2];               /* 0Ch */
    U16 IOCStatus;                  /* 0Eh */
    U32 IOCLogInfo;                 /* 10h */
    U8  PageType;                   /* 14h */
    U8  PageNumber;                 /* 15h */
    U8  PageVersion;                /* 16h */
    U8  Reserved3;                  /* 17h */
} ConfigReply_t;

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

/* Free reply FIFO depth */
#define MAX_REPLY_FREE_FIFO 32

struct PCIBaseState {
    PCIDevice parent_obj;

    /* Resource Management */
    MemoryRegion bar_regions[6];
    BARInfo bar_info[6];
    int num_bars;

    /* Capability and Interrupt State */
    bool has_msi;
    bool has_msix;

    /* Shadow registers for MMIO */
    uint32_t doorbell;
    uint32_t write_sequence;
    uint32_t diagnostic;
    uint32_t test_base;
    uint32_t diag_rw_data;
    uint32_t diag_rw_address;
    uint32_t int_status;
    uint32_t int_mask;
    uint32_t request_fifo;
    uint32_t request_hi_pri_fifo;
    uint32_t host_index;
    uint32_t fubar;
    uint32_t reset_1078;

    /* Indirect diagnostic access */
    uint32_t diag_addr;
    uint32_t diag_regs[256];

    /* Reply free FIFO: host pushes free reply frame addresses here */
    uint32_t reply_free_fifo[MAX_REPLY_FREE_FIFO];
    int free_fifo_head;   /* points to oldest entry */
    int free_fifo_tail;   /* points to next write position */
    int free_fifo_count;

    /* Reply done FIFO: device pushes completed reply addresses here */
    uint32_t reply_done_fifo[MAX_REPLY_FREE_FIFO];
    int done_fifo_head;   /* points to oldest completed reply address */
    int done_fifo_tail;   /* points to next write position for device */
    int done_fifo_count;

    /* DMA request processing state */
    QEMUTimer *req_timer;
};

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->int_status & s->int_mask) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* Write a reply frame to a DMA address and push it to done FIFO. */
static void pcibase_post_reply(PCIBaseState *s, uint32_t reply_addr, const void *data, size_t len)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    pci_dma_write(pdev, reply_addr, data, len);
    /* Push the reply address to done FIFO for driver to retrieve */
    s->reply_done_fifo[s->done_fifo_tail] = reply_addr;
    s->done_fifo_tail = (s->done_fifo_tail + 1) % MAX_REPLY_FREE_FIFO;
    s->done_fifo_count++;
    /* Set reply interrupt bit and raise IRQ */
    s->int_status |= 0x00000002;  /* bit 1: reply */
    pcibase_update_irq(s);
}

/*
 * Process a pending request frame.
 * Handles IOCFacts, PortFacts, and Config (partially) based on provided function codes.
 */
static void pcibase_process_request(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t req_addr = s->request_fifo;
    MPIHeader_t hdr;

    if (pci_dma_read(pdev, req_addr, &hdr, sizeof(hdr)) != 0) {
        return;
    }

    uint8_t func = hdr.Function;
    uint32_t msg_context = hdr.MsgContext;

    qemu_log_mask(LOG_GUEST_ERROR, "mptsas: request at 0x%x, Function=0x%02x\n", req_addr, func);

    /* Obtain a free reply frame from the reply free FIFO */
    if (s->free_fifo_count == 0) {
        qemu_log_mask(LOG_GUEST_ERROR, "mptsas: no free reply frame, dropping request\n");
        return;
    }
    uint32_t free_reply = s->reply_free_fifo[s->free_fifo_head];
    s->free_fifo_head = (s->free_fifo_head + 1) % MAX_REPLY_FREE_FIFO;
    s->free_fifo_count--;

    switch (func) {
    case MPI_FUNCTION_IOC_FACTS:
        {
            IOCFactsReply_t reply;
            memset(&reply, 0, sizeof(reply));
            reply.Function = MPI_FUNCTION_IOC_FACTS;
            reply.MsgVersion = 0x0105;
            reply.MsgLength = sizeof(reply) / 4; /* in DWords */
            reply.HeaderVersion = 0x0100;
            reply.IOCNumber = 0;
            reply.MsgFlags = 0;
            reply.MsgContext = msg_context;
            reply.IOCExceptions = 0;
            reply.IOCStatus = MPI_IOCSTATUS_SUCCESS;
            reply.IOCLogInfo = 0;
            reply.MaxChainDepth = 8;
            reply.WhoInit = 1;  /* BIOS */
            reply.BlockSize = 8;
            reply.Flags = 0;
            reply.ReplyQueueDepth = 64;
            reply.RequestFrameSize = 256; /* bytes */
            reply.ProductID = 0x0060;     /* SAS 1068E */
            reply.CurrentHostMfaHighAddr = 0;
            reply.GlobalCredits = 0;
            reply.NumberOfPorts = 1;
            reply.EventState = 0;
            reply.CurrentSenseBufferHighAddr = 0;
            reply.CurReplyFrameSize = 0;  /* not used */
            reply.MaxDevices = 16;
            reply.MaxBuses = 1;
            reply.FWImageSize = 0;
            reply.IOCCapabilities = 0;    /* no RAID etc. */
            reply.FWVersion = 0x01000000;
            reply.HighPriorityQueueDepth = 0;
            reply.HostPageBufferSGE.FlagsLength = 0;
            reply.HostPageBufferSGE.Address = 0;
            reply.ReplyFifoHostSignalingAddr = 0;
            pcibase_post_reply(s, free_reply, &reply, sizeof(reply));
        }
        break;
    case MPI_FUNCTION_PORT_FACTS:
        {
            PortFactsReply_t reply;
            /* Need to read the request to get PortNumber */
            /* PortFacts request layout: after header, U8 PortNumber at offset? */
            /* Minimal: just assume PortNumber=0 for simplicity */
            U8 port = 0;
            memset(&reply, 0, sizeof(reply));
            reply.Function = MPI_FUNCTION_PORT_FACTS;
            reply.MsgLength = sizeof(reply) / 4;
            reply.PortNumber = port;
            reply.MsgFlags = 0;
            reply.MsgContext = msg_context;
            reply.IOCStatus = MPI_IOCSTATUS_SUCCESS;
            reply.IOCLogInfo = 0;
            reply.PortType = 0x01;  /* SAS */
            reply.MaxDevices = 16;
            reply.PortSCSIID = 0;
            reply.ProtocolFlags = MPI_PORTFACTS_PROTOCOL_INITIATOR;
            reply.MaxPostedCmdBuffers = 64;
            reply.MaxPersistentIDs = 16;
            reply.MaxLanBuckets = 0;
            reply.MaxInitiators = 1;
            pcibase_post_reply(s, free_reply, &reply, sizeof(reply));
        }
        break;
    /* Config pages not yet implemented: missing struct definitions */
    default:
        qemu_log_mask(LOG_UNIMP, "mptsas: unsupported function 0x%02x\n", func);
        break;
    }
}

/* MMIO Handlers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    switch (addr) {
    case REG_DOORBELL:
        val = s->doorbell;
        break;
    case REG_WRITE_SEQUENCE:
        val = s->write_sequence;
        break;
    case REG_DIAGNOSTIC:
        val = s->diagnostic;
        break;
    case REG_TEST_BASE:
        val = s->test_base;
        break;
    case REG_DIAG_RW_DATA:
        /* Return the diagnostic register at the latched address */
        {
            uint32_t index = (s->diag_addr / 4) & 0xFF;
            val = s->diag_regs[index];
        }
        break;
    case REG_DIAG_RW_ADDRESS:
        val = s->diag_rw_address;
        break;
    case REG_INT_STATUS:
        val = s->int_status;
        break;
    case REG_INT_MASK:
        val = s->int_mask;
        break;
    case REG_REQUEST_FIFO:
        val = s->request_fifo;
        break;
    case REG_REPLY_FIFO:
        /* Pop a completed reply address from done FIFO */
        if (s->done_fifo_count > 0) {
            val = s->reply_done_fifo[s->done_fifo_head];
            s->done_fifo_head = (s->done_fifo_head + 1) % MAX_REPLY_FREE_FIFO;
            s->done_fifo_count--;
        } else {
            val = 0xFFFFFFFF; /* no reply */
        }
        break;
    case REG_REQUEST_HI_PRI_FIFO:
        val = s->request_hi_pri_fifo;
        break;
    case REG_HOST_INDEX:
        val = s->host_index;
        break;
    case REG_FUBAR:
        val = s->fubar;
        break;
    case REG_RESET_1078:
        val = s->reset_1078;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unimplemented read 0x%"HWADDR_PRIx"\n", __func__, addr);
        break;
    }
    return val;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    switch (addr) {
    case REG_DOORBELL:
        s->doorbell = val;
        /* Simulate doorbell ring: set bit 0 in int_status and raise IRQ
         * Also, check if a request is pending and process it.
         */
        s->int_status |= 0x00000001;
        pcibase_process_request(s);
        pcibase_update_irq(s);
        break;
    case REG_WRITE_SEQUENCE:
        s->write_sequence = val;
        break;
    case REG_DIAGNOSTIC:
        /* Store the written value directly; no override */
        s->diagnostic = val;
        break;
    case REG_TEST_BASE:
        s->test_base = val;
        break;
    case REG_DIAG_RW_DATA:
        /* Write data to the diagnostic register at the latched address */
        {
            uint32_t index = (s->diag_addr / 4) & 0xFF;
            s->diag_regs[index] = val;
        }
        s->diag_rw_data = val; /* mirror for optional direct read */
        break;
    case REG_DIAG_RW_ADDRESS:
        s->diag_rw_address = val;
        s->diag_addr = val;  /* latch the address for indirect access */
        break;
    case REG_INT_STATUS:
        /* W1C: clear bits that are set in val */
        s->int_status &= ~val;
        pcibase_update_irq(s);
        break;
    case REG_INT_MASK:
        s->int_mask = val;
        pcibase_update_irq(s);
        break;
    case REG_REQUEST_FIFO:
        s->request_fifo = val;
        break;
    case REG_REPLY_FIFO:
        /* Host writes free reply frame address to the reply free FIFO */
        if (s->free_fifo_count < MAX_REPLY_FREE_FIFO) {
            s->reply_free_fifo[s->free_fifo_tail] = val;
            s->free_fifo_tail = (s->free_fifo_tail + 1) % MAX_REPLY_FREE_FIFO;
            s->free_fifo_count++;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR, "mptsas: reply free FIFO overflow\n");
        }
        break;
    case REG_REQUEST_HI_PRI_FIFO:
        s->request_hi_pri_fifo = val;
        break;
    case REG_HOST_INDEX:
        s->host_index = val;
        break;
    case REG_FUBAR:
        /* FUBAR is read-only status; ignore writes to prevent driver from corrupting IOC state */
        break;
    case REG_RESET_1078:
        s->reset_1078 = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unimplemented write 0x%"HWADDR_PRIx" val=0x%"PRIx64"\n", __func__, addr, val);
        break;
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

    /* Reset all shadow registers to power-on defaults */
    s->doorbell = 0;
    s->write_sequence = 0;
    s->diagnostic = 0;
    s->test_base = 0;
    s->diag_rw_data = 0;
    s->diag_rw_address = 0;
    s->int_status = 0;
    s->int_mask = 0;
    s->request_fifo = 0;
    s->request_hi_pri_fifo = 0;
    s->host_index = 0;
    /* Set FUBAR to indicate operational state */
    s->fubar = MPI_IOC_STATE_OPERATIONAL;
    s->reset_1078 = 0;

    /* Clear free reply FIFO */
    s->free_fifo_head = 0;
    s->free_fifo_tail = 0;
    s->free_fifo_count = 0;

    /* Clear done reply FIFO */
    s->done_fifo_head = 0;
    s->done_fifo_tail = 0;
    s->done_fifo_count = 0;

    /* Clear diagnostic indirect access state */
    s->diag_addr = 0;
    memset(s->diag_regs, 0, sizeof(s->diag_regs));

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
        memory_region_init_io(mr, OBJECT(s), NULL, s, bi->name, aligned_size);
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

    /* BAR Initialization */
    s->num_bars = 1;
    s->bar_info[0] = (BARInfo){
        .index = 0,
        .type = BAR_TYPE_MMIO,
        .size = BAR0_SIZE,
        .name = "mpt-mmio",
    };
    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
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

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "mptsas_pci",
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

type_init(pcibase_register_types)
