#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "qemu/log.h"
#include "qemu/host-utils.h"
#include "qemu/bitops.h"
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

#define TYPE_PCIBASE_DEVICE "aic7xxx_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* BAR types */
#define BAR_TYPE_NONE 0
#define BAR_TYPE_MMIO 1
#define BAR_TYPE_PIO  2
#define BAR_TYPE_RAM  3

typedef struct {
    int index;
    int type;
    hwaddr size;
    const char *name;
} BARInfo;

/* Register Layout and Hardware Identifiers extracted from driver source */
#define VENDOR_ID 0x9004
#define DEVICE_ID 0x5078
#define CLASS_ID  0x0100

/* Register offsets */
#define SCSISEQ          0x00
#define SXFRCTL0         0x01
#define SXFRCTL1         0x02
#define SCSISIGO         0x03
#define SCSIRATE         0x04
#define SCSIID           0x05
#define SCSIDATL         0x06
#define STCNT            0x08
#define OPTIONMODE       0x08
#define TARGCRCCNT       0x0a
#define SSTAT0           0x0b
#define SSTAT1           0x0c
#define SSTAT2           0x0d
#define SSTAT3           0x0e
#define SCSIID_ULTRA2    0x0f
#define SIMODE0          0x10
#define SIMODE1          0x11
#define SCSIBUSL         0x12
#define SHADDR           0x14
#define TARGIDIN         0x18
#define SPIOCAP          0x1b
#define TARGID           0x1b
#define BRDCTL           0x1d
#define SEECTL           0x1e
#define SBLKCTL          0x1f
#define BUSY_TARGETS     0x20
#define TARG_SCSIRATE    0x20
#define ULTRA_ENB        0x30
#define DISC_DSB         0x32
#define CMDSIZE_TABLE    0x30
#define MSG_OUT          0x3a
#define SEQ_FLAGS        0x3c
#define SAVED_SCSIID     0x3d
#define SAVED_LUN        0x3e
#define LASTPHASE        0x3f
#define WAITING_SCBH     0x40
#define DISCONNECTED_SCBH 0x41
#define FREE_SCBH        0x42
#define HSCB_ADDR        0x44
#define SHARED_DATA_ADDR 0x48
#define QINPOS           0x4d
#define QOUTPOS          0x4e
#define KERNEL_QINPOS    0x4c
#define KERNEL_TQINPOS   0x4f
#define TQINPOS          0x50
#define ARG_1            0x51
#define ARG_2            0x52
#define LAST_MSG         0x53
#define SCSISEQ_TEMPLATE 0x54
#define INITIATOR_TAG    0x56
#define SEQ_FLAGS2       0x57
#define SCSICONF         0x5a
#define SEQCTL           0x60
#define SEQRAM           0x61
#define SEQADDR0         0x62
#define SEQADDR1         0x63
#define ACCUM            0x64
#define SINDEX           0x65
#define DINDEX           0x66
#define STACK            0x6f
#define SRAM_BASE        0x70
#define TARG_OFFSET      0x70
#define SXFR             0x70
#define HS_MAILBOX       0x86
#define HCNTRL           0x87
#define HADDR            0x88
#define HCNT             0x8c
#define SCBPTR           0x90
#define INTSTAT          0x91
#define ERROR            0x92
#define CLRINT           0x92
#define DFCNTRL          0x93
#define DFSTATUS         0x94
#define DSPCISTATUS      0x86
#define QINFIFO          0x9b
#define QOUTFIFO         0x9d
#define SCSIPHASE        0x9e
#define SFUNCT           0x9f
#define SCB_BASE         0xa0
#define SCB_RESIDUAL_DATACNT 0xa0
#define SCB_RESIDUAL_SGPTR   0xa4
#define SCB_64_BTT       0xd0
#define QOFF_CTLSTA      0xfa
#define SNSCB_QOFF       0xf6
#define HNSCB_QOFF       0xf4
#define SDSCB_QOFF       0xf8
#define SCBBADDR         0xf0
#define DFF_THRSH        0xfb
#define DSCOMMAND0       0x84
#define DSCOMMAND1       0x85
#define CCSCBCTL         0xee
#define CCSGCTL          0xeb
#define CCSCBCNT         0xef

/* Bit definitions for various registers */
#define ENSELO           0x40
#define SELDO            0x40
#define SELINGO          0x10
#define SELBUSB          0x08
#define SELWIDE          0x02
#define ENRSELI          0x10
#define ENSELI           0x20
#define ENSTIMER         0x04
#define ENSELTIMO        0x80
#define ENBUSFREE        0x08
#define ENSCSIRST        0x20
#define ENSCSIPERR       0x04
#define ENIOERR          0x08
#define ENAB20           0x04
#define ENAB40           0x08
#define ENSPCHK          0x20
#define EXP_ACTIVE       0x10
#define ACTNEGEN         0x02
#define ENAUTOATNP       0x02
#define ENAUTOATNO       0x08
#define CLRSCSIRSTI      0x20
#define CLRSELINGO       0x10
#define CLRSELDO         0x40
#define CLRSELDI         0x20
#define CLRSELTIMEO      0x80
#define CLRBUSFREE       0x08
#define CLRPHASECHG      0x02
#define CLRREQINIT       0x01
#define CLRSCSIPERR      0x04
#define CLRIOERR         0x08
#define CLRSEQINT        0x01
#define CLRSCSIINT       0x04
#define CLRCMDINT        0x02
#define CLRBRKADRINT     0x08
#define CLRCHN           0x02
#define CLRSTCNT         0x10
#define CLRPARERR        0x10
#define CHIPRST          0x01
#define CHIPRSTACK       0x01
#define RESET_SCSI       0x40
#define PAUSE            0x04
#define SINGLE_EDGE      0x10
#define BUSFREE          0x08
#define SELTO            0x80
#define REQINIT          0x01
#define PHASE_MASK       0xe0
#define P_DATAOUT        0x00
#define P_DATAIN         0x40
#define P_COMMAND        0x80
#define P_MESGOUT        0xa0
#define P_MESGIN         0xe0
#define P_STATUS         0xc0
#define P_DATAOUT_DT     0x20
#define P_DATAIN_DT      0x60
#define BSYO             0x04
#define ATNO             0x10
#define TAG_ENB          0x20
#define DISCENB          0x40
#define TARGET           0x80
#define TID              0xf0
#define OID              0x0f
#define LID              0x3f
#define INT_PEND         0x0f
#define SCSIINT          0x04
#define SEQINT           0x01
#define BRKADRINT        0x08
#define MSGI             0x20
#define IOERR            0x08
#define SCSIPERR         0x04
#define SCSIRSTI         0x20
#define CRCVALERR        0x08
#define CRCENDERR        0x04
#define CRCREQERR        0x02
#define DUAL_EDGE_ERR    0x01
#define CIOPARERR        0x80
#define DPARERR          0x10
#define MPARERR          0x20
#define SQPARERR         0x08
#define ILLHADDR         0x01
#define ILLSADDR         0x02
#define ILLOPCODE        0x04
#define PCIERRSTAT       0x40
#define PERR_DETECTED    0x81
#define DATA_OVERRUN     0x91
#define CMDCMPLT         0x02
#define STPWEN           0x01
#define INTEN            0x02
#define DIAGLEDEN        0x80
#define DIAGLEDON        0x40
#define FAILDIS          0x20
#define ENSPCHK          0x20
#define ALT_MODE         0x80
#define AUTOACKEN        0x40
#define EXPPHASEDIS      0x08
#define CACHETHEN        0x80
#define PERRORDIS        0x80
#define BUSFREEREV       0x10
#define RESET_SCSI       0x40
#define DFON             0x80
#define RAMPS            0x04
#define LOADRAM          0x01
#define INTSCBRAMSEL     0x08
#define USCBSIZE32       0x02
#define MPARCKEN         0x20
#define DPARCKEN         0x40
#define CRCENDCHKEN      0x20
#define CRCVALCHKEN      0x40
#define CRCREQCHKEN      0x10
#define TARGCRCENDEN     0x08
#define ENABLE_CRC       0x40
#define FAST20           0x20
#define WIDEXFER         0x80
#define SOFS             0x0f
#define RD_DFTHRSH_MAX   0x07
#define WR_DFTHRSH_MAX   0x70
#define DFTHRSH_100      0xc0
#define OPTIONMODE_DEFAULTS 0x03
#define SCB_QSIZE_256    0x06
#define MSG_IDENTIFYFLAG 0x80
#define MSG_IDENTIFY_DISCFLAG 0x40
#define SCB_LIST_NULL    0xff
#define SG_LIST_NULL     0x01
#define SG_FULL_RESID    0x02
#define SG_RESID_VALID   0x04
#define SCB_XFERLEN_ODD  0x80
#define NO_CDB_SENT      0x40
#define STATUS_RCVD      0x80
#define MK_MESSAGE       0x10
#define SEND_REJECT      0x11
#define HOST_MSG         0xff
#define NO_MATCH         0x31
#define PROTO_VIOLATION  0x21
#define HOST_MSG_LOOP    0x61
#define MKMSG_FAILED     0xa1
#define SCB_MISMATCH     0xc1
#define NO_FREE_SCB      0xd1
#define BAD_PHASE        0x01
#define BAD_STATUS       0x71
#define RETURN_1         0x51
#define PDATA_REINIT     0x51
#define MISSED_BUSFREE   0xb1
#define IGN_WIDE_RES     0x41
#define OUT_OF_RANGE     0xe1
#define CONT_MSG_LOOP    0x04
#define EXIT_MSG_LOOP    0x08
#define PHASEMIS         0x10
#define TARG_CMD_PENDING 0x10
#define CMDPHASE_PENDING 0x08
#define NO_DISCONNECT    0x01
#define DPHASE           0x20
#define CDI              0x80
#define BITBUCKET        0x80
#define NOT_IDENTIFIED   0x80
#define TARGET_MSG_PENDING 0x02
#define SCB_TAG_TYPE     0x03
#define SEND_SENSE       0x40
#define ATNI             0x10
#define CLRATNO          0x40
#define SCB_DMA          0x01
#define MSG_EXT_WDTR_LEN 0x02
#define MSG_EXT_SDTR_LEN 0x03
#define MSG_EXT_PPR_LEN  0x06
#define MSG_EXT_WDTR_BUS_8_BIT  0x00
#define MSG_EXT_WDTR_BUS_16_BIT 0x01
#define MSG_EXT_PPR_QAS_REQ 0x04
#define MSG_EXT_PPR_DT_REQ  0x02
#define MSG_EXT_PPR_IU_REQ  0x01
#define BUS_DMASYNC_PREREAD  0x01
#define BUS_DMASYNC_POSTREAD 0x02
#define BUS_DMASYNC_PREWRITE 0x04
#define BUS_DMASYNC_POSTWRITE 0x08
#define BUS_DMA_NOWAIT       0x1
#define AHC_NUM_TARGETS 16
#define AHC_NUM_LUNS 64
#define AHC_MAX_QUEUE 253
#define AHC_NSEG 128
#define AHC_SCB_MAX 255
#define AHC_SCB_MAX_ALLOC (AHC_MAX_QUEUE+1)
#define AHC_SYNCRATE_DT  0
#define AHC_SYNCRATE_ULTRA 3
#define AHC_SYNCRATE_FAST 6
#define AHC_SYNCRATE_ULTRA2 1
#define MAX_OFFSET 0x7f
#define MAX_OFFSET_ULTRA2 0x7f
#define MAX_OFFSET_8BIT 0x0f
#define MAX_OFFSET_16BIT 0x08
#define AHC_ASYNC_XFER_PERIOD 0x45
#define AHC_TRANS_CUR   0x01
#define AHC_TRANS_GOAL  0x04
#define AHC_TRANS_USER  0x08
#define AHC_TRANS_ACTIVE 0x03
#define AHC_PERIOD_UNKNOWN 0xFF
#define AHC_OFFSET_UNKNOWN 0xFF
#define AHC_WIDTH_UNKNOWN  0xFF
#define SG_PTR_MASK 0xFFFFFFF8
#define SG_PREFETCH_ADDR_MASK 0x06
#define SG_PREFETCH_CNT 0x04
#define CACHESIZE_MASK 0x02
#define INVERTED_CACHESIZE_MASK 0x03
#define SG_PREFETCH_ALIGN_MASK 0x05
#define AHC_DMA_LAST_SEG 0x80000000
#define AHC_SG_HIGH_ADDR_MASK 0x7F000000
#define AHC_SG_LEN_MASK 0x00FFFFFF
#define AHC_LINUX_NOIRQ ((uint32_t)~0)
#define AHC_SHOW_MISC 0x0001
#define AHC_SHOW_SENSE 0x0002
#define AHC_SHOW_SELTO 0x0080
#define AHC_SHOW_MESSAGES 0x0020
#define AHC_SHOW_MASKED_ERRORS 0x1000
#define AHC_DEBUG_SEQUENCER 0x2000
#define AHC_DEBUG 1
#define AHC_TAG_SUCCESS_INTERVAL 50
#define AHC_LOCK_TAGS_COUNT 50
#define AHC_OTAG_THRESH 500
#define AHC_BUSRESET_DELAY 25
#define AHC_PCI_CONFIG 1
#define AHC_PCI_TARGET_PERR_THRESH 10
#define EVENT_TYPE_BUS_RESET 0xFF
#define BUS_SPACE_MAXADDR_32BIT 0xFFFFFFFF
#define BUS_SPACE_MAXSIZE_32BIT 0xFFFFFFFF
#define BUS_SPACE_MAXADDR 0xFFFFFFFF

/* SEEPROM Config Bits */
#define CFXFER           0x0007
#define CFWIDEB          0x0020
#define CF284XFIFO       0x000C
#define CFBOOTCHAN       0x0300
#define CFWSTERM         0x0008
#define CFDISC           0x0010
#define CFSM2DRV         0x0010
#define CFTERM_MENU      0x0040
#define CFSEAUTOTERM     0x0400
#define CFMSG_VERBOSE    0x0000
#define CFWBCACHEENB     0x4000
#define CFSPARITY        0x0010
#define CFMSG_DIAG       0x0400
#define CFMSG_LEVEL      0x0600
#define CFSIGNATURE      0x250
#define CFSIGNATURE2     0x300
#define CFBIOS_BUSSCAN   0x0008
#define CFSELOWTERM      0x0800
#define CFRNFOUND        0x0400
#define CFMSG_SILENT     0x0200
#define CFMULTILUNDEV    0x0800
#define CFSCAMEN         0x0100
#define CFBOOTCHANSHIFT  8
#define CFBIOSEN         0x0004
#define CFSYNCHISULTRA   0x0040
#define CFSUPREMB        0x0002
#define CFMULTILUN       0x0020
#define CFMAXTARG        0x00ff
#define CFBOOTID         0xf000
#define CFSYNCH          0x0008
#define CFSTPWLEVEL      0x0010
#define CFCTRL_A         0x0020
#define CFCLUSTERENB     0x0080
#define CFSEHIGHTERM     0x1000
#define CFULTRAEN        0x0002
#define CFSTART          0x0100
#define CFBOOTCD         0x0800
#define CF284XEXTEND     0x0020
#define CFWBCACHENOP     0xc000
#define CFENABLEDV       0x4000
#define CFEXTEND         0x0080
#define CFBOOTLUN        0x0f00
#define CFSTERM          0x0004
#define CFAUTOTERM       0x0001
#define CF284XSELTO      0x0003
#define CFINCBIOS        0x0200
#define CF284XSTERM      0x0020
#define CFSUPREM         0x0001
#define CFRESETB         0x0040
#define CFSYNCSINGLE     0x0080
#define CFSCSIID         0x000f
#define CFBRTIME         0xff00

/* IRQ Masks */
#define IRQMS            0x08

/* SEEPROM related */
#define SEECS            0x08
#define SEEDI            0x01
#define SEEDO            0x02
#define SEEMS            0x20
#define SEERDY           0x10
#define SEECK            0x04
#define SSPIOCPS         0x01
#define SEEPROM          0x08
#define EEPROM           0x04

/* BRDCTL bits */
#define BRDDAT7          0x80
#define BRDDAT6          0x40
#define BRDDAT5          0x20
#define BRDDAT4          0x10
#define BRDDAT3          0x08
#define BRDCS            0x08
#define BRDSTB           0x10
#define BRDSTB_ULTRA2    0x01
#define BRDRW            0x04
#define BRDRW_ULTRA2     0x02
#define SOFTCMDEN        0x20
#define EXT_BRDCTL       0x10

/* SCSICONF bits */
#define CLRCHN           0x02

/* SCB control */
#define SCB_CONTROL      0xb8
#define SCB_SCSIID       0xb9
#define SCB_LUN          0xba
#define SCB_TAG          0xbb
#define SCB_SCSIRATE     0xbd
#define SCB_SCSIOFFSET   0xbe
#define SCB_NEXT         0xbf

/* SEQCTL */
#define FASTMODE         0x10

/* HCNTRL bits */
#define POWRDN           0x40
#define CHIPRST          0x01
#define PAUSE            0x04

/* DSCOMMAND0 */
#define DIAGLEDEN        0x80
#define DIAGLEDON        0x40

/* PCIM CMD */
#define PCIM_CMD_MEMEN   0x0002
#define PCIM_CMD_PORTEN  0x0001
#define PCIM_CMD_BUSMASTEREN 0x0004
#define PCIM_CMD_SERRESPEN  0x0100

/* SCSI sense key definitions */
#define SSD_KEY_NO_SENSE 0x00
#define SSD_KEY_RECOVERED_ERROR 0x01
#define SSD_KEY_NOT_READY 0x02
#define SSD_KEY_MEDIUM_ERROR 0x03
#define SSD_KEY_HARDWARE_ERROR 0x04
#define SSD_KEY_ILLEGAL_REQUEST 0x05
#define SSD_KEY_UNIT_ATTENTION 0x06
#define SSD_KEY_DATA_PROTECT 0x07
#define SSD_KEY_BLANK_CHECK 0x08
#define SSD_KEY_COPY_ABORTED 0x0a
#define SSD_KEY_ABORTED_COMMAND 0x0b
#define SSD_KEY_EQUAL 0x0c
#define SSD_KEY_VOLUME_OVERFLOW 0x0d
#define SSD_KEY_MISCOMPARE 0x0e
#define SSD_KEY_RESERVED 0x0f
#define SSD_ERRCODE_VALID 0x80
#define SSD_ERRCODE 0x7F
#define SSD_KEY 0x0F
#define SSD_ILI 0x20
#define SSD_EOM 0x40
#define SSD_FILEMARK 0x80
#define SSD_FIELDPTR_CMD 0x40
#define SSD_BITPTR_VALID 0x08
#define SSD_BITPTR_VALUE 0x07
#define SSD_MIN_SIZE 18
#define SSD_FULL_SIZE sizeof(struct scsi_sense_data)

/* Build TCL macro */
#define BUILD_TCL(scsiid, lun) ((lun) | (((scsiid) & TID) << 4))
#define TCL_TARGET_OFFSET(tcl) ((((tcl) >> 4) & TID) >> 4)
#define TCL_LUN(tcl) (tcl & (AHC_NUM_LUNS - 1))
#define TID_SHIFT 0x04
#define TWIN_CHNLB 0x80
#define TWIN_TID 0x70

/* SCSIID macros */
#define SCSIID_TARGET(ahc, scsiid) \
    (((scsiid) & ((((ahc)->features & AHC_TWIN) != 0) ? TWIN_TID : TID)) \
    >> TID_SHIFT)
#define SCSIID_OUR_ID(scsiid) ((scsiid) & OID)
#define SCSIID_CHANNEL(ahc, scsiid) \
    ((((ahc)->features & AHC_TWIN) != 0) \
        ? ((((scsiid) & TWIN_CHNLB) != 0) ? 'B' : 'A') \
       : 'A')

/* SCB access macros */
#define SCB_GET_LUN(scb) ((scb)->hscb->lun & LID)
#define SCB_GET_TARGET(ahc, scb) SCSIID_TARGET((ahc), (scb)->hscb->scsiid)
#define SCB_GET_CHANNEL(ahc, scb) SCSIID_CHANNEL(ahc, (scb)->hscb->scsiid)
#define SCB_IS_SILENT(scb) \
    ((ahc_debug & AHC_SHOW_MASKED_ERRORS) == 0 && (((scb)->flags & SCB_SILENT) != 0))
#define SCB_IS_SCSIBUS_B(ahc, scb) (SCSIID_CHANNEL(ahc, (scb)->hscb->scsiid) == 'B')
#define SCB_GET_TARGET_OFFSET(ahc, scb) \
    (SCB_GET_TARGET(ahc, scb) + (SCB_IS_SCSIBUS_B(ahc, scb) ? 8 : 0))

/* Link macros */
#define LIST_ENTRY(type) \
struct { \
    struct type *le_next; \
    struct type **le_prev; \
}
#define LIST_HEAD(name, type) \
struct name { \
    struct type *lh_first; \
}
#define LIST_FIRST(head) ((head)->lh_first)
#define LIST_INIT(head) do { \
    LIST_FIRST((head)) = NULL; \
} while (0)
#define LIST_NEXT(elm, field) ((elm)->field.le_next)
#define LIST_REMOVE(elm, field) do { \
    if (LIST_NEXT((elm), field) != NULL) \
        LIST_NEXT((elm), field)->field.le_prev = \
            (elm)->field.le_prev; \
    *(elm)->field.le_prev = LIST_NEXT((elm), field); \
} while (0)
#define LIST_FOREACH(var, head, field) \
    for ((var) = LIST_FIRST((head)); \
        (var); \
        (var) = LIST_NEXT((var), field))

#define SLIST_ENTRY(type) \
struct { \
    struct type *sle_next; \
}
#define SLIST_HEAD(name, type) \
struct name { \
    struct type *slh_first; \
}
#define SLIST_FIRST(head) ((head)->slh_first)
#define SLIST_INIT(head) do { \
    SLIST_FIRST((head)) = NULL; \
} while (0)
#define SLIST_NEXT(elm, field) ((elm)->field.sle_next)
#define SLIST_REMOVE_HEAD(head, field) do { \
    SLIST_FIRST((head)) = SLIST_NEXT(SLIST_FIRST((head)), field); \
} while (0)
#define SLIST_INSERT_HEAD(head, elm, field) do { \
    SLIST_NEXT((elm), field) = SLIST_FIRST((head)); \
    SLIST_FIRST((head)) = (elm); \
} while (0)
#define SLIST_FOREACH(var, head, field) \
    for ((var) = SLIST_FIRST((head)); \
        (var); \
        (var) = SLIST_NEXT((var), field))

#define TAILQ_ENTRY(type) \
struct { \
    struct type *tqe_next; \
    struct type **tqe_prev; \
}
#define TAILQ_HEAD(name, type) \
struct name { \
    struct type *tqh_first; \
    struct type **tqh_last; \
}
#define TAILQ_FIRST(head) ((head)->tqh_first)
#define TAILQ_INIT(head) do { \
    TAILQ_FIRST((head)) = NULL; \
    (head)->tqh_last = &TAILQ_FIRST((head)); \
} while (0)
#define TAILQ_NEXT(elm, field) ((elm)->field.tqe_next)
#define TAILQ_EMPTY(head) ((head)->tqh_first == NULL)
#define TAILQ_REMOVE(head, elm, field) do { \
    if ((TAILQ_NEXT((elm), field)) != NULL) \
        TAILQ_NEXT((elm), field)->field.tqe_prev = \
            (elm)->field.tqe_prev; \
    else \
        (head)->tqh_last = (elm)->field.tqe_prev; \
    *(elm)->field.tqe_prev = TAILQ_NEXT((elm), field); \
} while (0)
#define TAILQ_INSERT_HEAD(head, elm, field) do { \
    if ((TAILQ_NEXT((elm), field) = TAILQ_FIRST((head))) != NULL) \
        TAILQ_FIRST((head))->field.tqe_prev = \
            &TAILQ_NEXT((elm), field); \
    else \
        (head)->tqh_last = &TAILQ_NEXT((elm), field); \
    TAILQ_FIRST((head)) = (elm); \
    (elm)->field.tqe_prev = &TAILQ_FIRST((head)); \
} while (0)
#define TAILQ_FOREACH(var, head, field) \
    for ((var) = TAILQ_FIRST((head)); \
        (var); \
        (var) = TAILQ_NEXT((var), field))

/* Hardware registers state */
struct hw_regs {
    uint8_t scsiseq;          /* 0x00 */
    uint8_t sxfrctl0;         /* 0x01 */
    uint8_t sxfrctl1;         /* 0x02 */
    uint8_t scsisigo;         /* 0x03 */
    uint8_t scsirate;         /* 0x04 */
    uint8_t scsiid;           /* 0x05 */
    uint8_t scsidatl;         /* 0x06 */
    uint8_t stcnt;            /* 0x08, also OPTIONMODE */
    uint8_t targcrccnt;       /* 0x0a */
    uint8_t sstat0;           /* 0x0b */
    uint8_t sstat1;           /* 0x0c */
    uint8_t sstat2;           /* 0x0d */
    uint8_t sstat3;           /* 0x0e */
    uint8_t scsiid_ultra2;    /* 0x0f */
    uint8_t simode0;          /* 0x10 */
    uint8_t simode1;          /* 0x11 */
    uint8_t scsibusl;         /* 0x12 */
    uint8_t shaddr;           /* 0x14 */
    uint8_t targidin;         /* 0x18 */
    uint8_t spiocap;          /* 0x1b, also TARGID */
    uint8_t brdctl;           /* 0x1d */
    uint8_t seectl;           /* 0x1e */
    uint8_t sblkctl;          /* 0x1f */
    uint8_t busy_targets;     /* 0x20, also TARG_SCSIRATE */
    uint8_t ultra_enb;        /* 0x30, also CMDSIZE_TABLE */
    uint8_t disc_dsb;         /* 0x32 */
    uint8_t msg_out;          /* 0x3a */
    uint8_t seq_flags;        /* 0x3c */
    uint8_t saved_scsiid;     /* 0x3d */
    uint8_t saved_lun;        /* 0x3e */
    uint8_t lastphase;        /* 0x3f */
    uint8_t waiting_scbh;     /* 0x40 */
    uint8_t disconnected_scbh;/* 0x41 */
    uint8_t free_scbh;        /* 0x42 */
    uint32_t hscb_addr;       /* 0x44 */
    uint32_t shared_data_addr;/* 0x48 */
    uint8_t kernel_qinpos;    /* 0x4c */
    uint8_t qinpos;           /* 0x4d */
    uint8_t qoutpos;          /* 0x4e */
    uint8_t kernel_tqinpos;   /* 0x4f */
    uint8_t tqinpos;          /* 0x50 */
    uint8_t arg_1;            /* 0x51 */
    uint8_t arg_2;            /* 0x52 */
    uint8_t last_msg;         /* 0x53 */
    uint8_t scsiseq_template; /* 0x54 */
    uint8_t initiator_tag;    /* 0x56 */
    uint8_t seq_flags2;       /* 0x57 */
    uint8_t scsiconf;         /* 0x5a */
    uint8_t seqctl;           /* 0x60 */
    uint8_t seqram;           /* 0x61 */
    uint8_t seqaddr0;         /* 0x62 */
    uint8_t seqaddr1;         /* 0x63 */
    uint8_t accum;            /* 0x64 */
    uint8_t sindex;           /* 0x65 */
    uint8_t dindex;           /* 0x66 */
    uint8_t stack;            /* 0x6f */
    uint8_t sram_base;        /* 0x70, also TARG_OFFSET and SXFR */
    uint8_t dscommand0;       /* 0x84 */
    uint8_t dscommand1;       /* 0x85 */
    uint8_t hs_mailbox;       /* 0x86, also DSPCISTATUS */
    uint8_t hcntrl;           /* 0x87 */
    uint32_t haddr;           /* 0x88 */
    uint32_t hcnt;            /* 0x8c */
    uint8_t scbptr;           /* 0x90 */
    uint8_t intstat;          /* 0x91 */
    uint8_t error;            /* 0x92, also CLRINT */
    uint8_t dfcntrl;          /* 0x93 */
    uint8_t dfstatus;         /* 0x94 */
    uint8_t qinfifo;          /* 0x9b */
    uint8_t qoutfifo;         /* 0x9d */
    uint8_t scsiphase;        /* 0x9e */
    uint8_t sfunct;           /* 0x9f */
    uint32_t scb_base;        /* 0xa0, also SCB_RESIDUAL_DATACNT */
    uint32_t scb_residual_sgptr; /* 0xa4 */
    uint64_t scb_64_btt;      /* 0xd0 */
    uint8_t ccsgctl;          /* 0xeb */
    uint8_t ccscbctl;         /* 0xee */
    uint8_t ccscbcnt;         /* 0xef */
    uint8_t scbbaddr;         /* 0xf0 */
    uint8_t hnscb_qoff;       /* 0xf4 */
    uint8_t snscb_qoff;       /* 0xf6 */
    uint8_t sdscb_qoff;       /* 0xf8 */
    uint8_t qoff_ctlsta;      /* 0xfa */
    uint8_t dff_thrsh;        /* 0xfb */
};

/* Main device state structure */
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
    struct hw_regs regs;

    /* DMA Context */
    dma_addr_t hscb_base;
    dma_addr_t sense_base;
    dma_addr_t sg_base;
    uint32_t scb_count;

    uint32_t status;            /* Operational status flags */
    uint8_t reset_state;        /* Reset sequence state */
    uint8_t power_state;        /* D0-D3 */
};

/* Helper to get a pointer to the register byte at given offset */
static uint8_t *pcibase_get_reg_ptr(PCIBaseState *s, hwaddr addr)
{
    struct hw_regs *r = &s->regs;
    switch (addr) {
    case SCSISEQ:          return &r->scsiseq;
    case SXFRCTL0:         return &r->sxfrctl0;
    case SXFRCTL1:         return &r->sxfrctl1;
    case SCSISIGO:         return &r->scsisigo;
    case SCSIRATE:         return &r->scsirate;
    case SCSIID:           return &r->scsiid;
    case SCSIDATL:         return &r->scsidatl;
    case STCNT:            return &r->stcnt;
    case TARGCRCCNT:       return &r->targcrccnt;
    case SSTAT0:           return &r->sstat0;
    case SSTAT1:           return &r->sstat1;
    case SSTAT2:           return &r->sstat2;
    case SSTAT3:           return &r->sstat3;
    case SCSIID_ULTRA2:    return &r->scsiid_ultra2;
    case SIMODE0:          return &r->simode0;
    case SIMODE1:          return &r->simode1;
    case SCSIBUSL:         return &r->scsibusl;
    case SHADDR:           return &r->shaddr;
    case TARGIDIN:         return &r->targidin;
    case SPIOCAP:          return &r->spiocap;
    case BRDCTL:           return &r->brdctl;
    case SEECTL:           return &r->seectl;
    case SBLKCTL:          return &r->sblkctl;
    case BUSY_TARGETS:     return &r->busy_targets;
    case ULTRA_ENB:        return &r->ultra_enb;
    case DISC_DSB:         return &r->disc_dsb;
    case MSG_OUT:          return &r->msg_out;
    case SEQ_FLAGS:        return &r->seq_flags;
    case SAVED_SCSIID:     return &r->saved_scsiid;
    case SAVED_LUN:        return &r->saved_lun;
    case LASTPHASE:        return &r->lastphase;
    case WAITING_SCBH:     return &r->waiting_scbh;
    case DISCONNECTED_SCBH: return &r->disconnected_scbh;
    case FREE_SCBH:        return &r->free_scbh;
    case HSCB_ADDR:        return (uint8_t *)&r->hscb_addr;
    case HSCB_ADDR + 1:    return ((uint8_t *)&r->hscb_addr) + 1;
    case HSCB_ADDR + 2:    return ((uint8_t *)&r->hscb_addr) + 2;
    case HSCB_ADDR + 3:    return ((uint8_t *)&r->hscb_addr) + 3;
    case SHARED_DATA_ADDR: return (uint8_t *)&r->shared_data_addr;
    case SHARED_DATA_ADDR + 1: return ((uint8_t *)&r->shared_data_addr) + 1;
    case SHARED_DATA_ADDR + 2: return ((uint8_t *)&r->shared_data_addr) + 2;
    case SHARED_DATA_ADDR + 3: return ((uint8_t *)&r->shared_data_addr) + 3;
    case QINPOS:           return &r->qinpos;
    case QOUTPOS:          return &r->qoutpos;
    case KERNEL_QINPOS:    return &r->kernel_qinpos;
    case KERNEL_TQINPOS:   return &r->kernel_tqinpos;
    case TQINPOS:          return &r->tqinpos;
    case ARG_1:            return &r->arg_1;
    case ARG_2:            return &r->arg_2;
    case LAST_MSG:         return &r->last_msg;
    case SCSISEQ_TEMPLATE: return &r->scsiseq_template;
    case INITIATOR_TAG:    return &r->initiator_tag;
    case SEQ_FLAGS2:       return &r->seq_flags2;
    case SCSICONF:         return &r->scsiconf;
    case SEQCTL:           return &r->seqctl;
    case SEQRAM:           return &r->seqram;
    case SEQADDR0:         return &r->seqaddr0;
    case SEQADDR1:         return &r->seqaddr1;
    case ACCUM:            return &r->accum;
    case SINDEX:           return &r->sindex;
    case DINDEX:           return &r->dindex;
    case STACK:            return &r->stack;
    case SRAM_BASE:        return &r->sram_base;
    case DSCOMMAND0:       return &r->dscommand0;
    case DSCOMMAND1:       return &r->dscommand1;
    case HS_MAILBOX:       return &r->hs_mailbox;
    case HCNTRL:           return &r->hcntrl;
    case HADDR:            return (uint8_t *)&r->haddr;
    case HADDR + 1:        return ((uint8_t *)&r->haddr) + 1;
    case HADDR + 2:        return ((uint8_t *)&r->haddr) + 2;
    case HADDR + 3:        return ((uint8_t *)&r->haddr) + 3;
    case HCNT:             return (uint8_t *)&r->hcnt;
    case HCNT + 1:         return ((uint8_t *)&r->hcnt) + 1;
    case HCNT + 2:         return ((uint8_t *)&r->hcnt) + 2;
    case HCNT + 3:         return ((uint8_t *)&r->hcnt) + 3;
    case SCBPTR:           return &r->scbptr;
    case INTSTAT:          return &r->intstat;
    case ERROR:            return &r->error;
    case DFCNTRL:          return &r->dfcntrl;
    case DFSTATUS:         return &r->dfstatus;
    case QINFIFO:          return &r->qinfifo;
    case QOUTFIFO:         return &r->qoutfifo;
    case SCSIPHASE:        return &r->scsiphase;
    case SFUNCT:           return &r->sfunct;
    case SCB_BASE:         return (uint8_t *)&r->scb_base;
    case SCB_BASE + 1:     return ((uint8_t *)&r->scb_base) + 1;
    case SCB_BASE + 2:     return ((uint8_t *)&r->scb_base) + 2;
    case SCB_BASE + 3:     return ((uint8_t *)&r->scb_base) + 3;
    case SCB_RESIDUAL_SGPTR: return (uint8_t *)&r->scb_residual_sgptr;
    case SCB_RESIDUAL_SGPTR + 1: return ((uint8_t *)&r->scb_residual_sgptr) + 1;
    case SCB_RESIDUAL_SGPTR + 2: return ((uint8_t *)&r->scb_residual_sgptr) + 2;
    case SCB_RESIDUAL_SGPTR + 3: return ((uint8_t *)&r->scb_residual_sgptr) + 3;
    case SCB_64_BTT:        return (uint8_t *)&r->scb_64_btt;
    case QOFF_CTLSTA:      return &r->qoff_ctlsta;
    case SNSCB_QOFF:       return &r->snscb_qoff;
    case HNSCB_QOFF:       return &r->hnscb_qoff;
    case SDSCB_QOFF:       return &r->sdscb_qoff;
    case SCBBADDR:         return &r->scbbaddr;
    case DFF_THRSH:        return &r->dff_thrsh;
    case CCSCBCTL:         return &r->ccscbctl;
    case CCSGCTL:          return &r->ccsgctl;
    case CCSCBCNT:         return &r->ccscbcnt;
    default:
        if (addr < 0x100) {
            /* fallback: treat as zeroed */
            return NULL;
        }
        return NULL;
    }
}

/* Internal helper for status-triggered signaling */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t intstat = s->regs.intstat;
    uint8_t simode0 = s->regs.simode0;
    uint8_t simode1 = s->regs.simode1;
    uint8_t masked = intstat & (simode0 | (simode1 << 8));
    if (masked) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* MMIO Read Handler */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;
    uint8_t *ptr;
    if (addr < 0x100) {
        switch (size) {
        case 1:
            ptr = pcibase_get_reg_ptr(s, addr);
            if (ptr)
                val = *ptr;
            break;
        case 2:
            for (int i = 0; i < 2; i++) {
                ptr = pcibase_get_reg_ptr(s, addr + i);
                val |= (ptr ? *ptr : 0) << (i * 8);
            }
            break;
        case 4:
            for (int i = 0; i < 4; i++) {
                ptr = pcibase_get_reg_ptr(s, addr + i);
                val |= (ptr ? *ptr : 0) << (i * 8);
            }
            break;
        case 8:
            for (int i = 0; i < 8; i++) {
                ptr = pcibase_get_reg_ptr(s, addr + i);
                val |= (uint64_t)(ptr ? *ptr : 0) << (i * 8);
            }
            break;
        default:
            break;
        }
    }
    return val;
}

/* MMIO Write Handler */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint8_t *ptr;
    if (addr < 0x100) {
        switch (size) {
        case 1:
            ptr = pcibase_get_reg_ptr(s, addr);
            if (ptr)
                *ptr = (uint8_t)val;
            break;
        case 2:
            for (int i = 0; i < 2; i++) {
                ptr = pcibase_get_reg_ptr(s, addr + i);
                if (ptr)
                    *ptr = (val >> (i * 8)) & 0xFF;
            }
            break;
        case 4:
            for (int i = 0; i < 4; i++) {
                ptr = pcibase_get_reg_ptr(s, addr + i);
                if (ptr)
                    *ptr = (val >> (i * 8)) & 0xFF;
            }
            break;
        case 8:
            for (int i = 0; i < 8; i++) {
                ptr = pcibase_get_reg_ptr(s, addr + i);
                if (ptr)
                    *ptr = (val >> (i * 8)) & 0xFF;
            }
            break;
        default:
            break;
        }
        /* Handle special write effects: interrupt clear, etc. */
        if (addr == CLRINT) {
            s->regs.intstat &= ~((uint8_t)val);
            pcibase_update_irq(s);
        }
    }
}

/* PIO Read Handler */
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* PIO BAR is 256 bytes, same offset semantics */
    return pcibase_mmio_read(opaque, addr, size);
}

/* PIO Write Handler */
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    pcibase_mmio_write(opaque, addr, val, size);
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

    /* Reset all registers to known defaults */
    memset(&s->regs, 0, sizeof(s->regs));
    /* Set power-on defaults as indicated by driver macros */
    s->regs.stcnt = OPTIONMODE_DEFAULTS;
    /* Additional defaults could be set here if needed */
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
    pci_set_word(pci_conf + PCI_VENDOR_ID,  0x9004);
    pci_set_word(pci_conf + PCI_DEVICE_ID,  0x5078);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, 0x0100);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* BAR Initialization */
    /* Configure BARs based on driver expectations: BAR0 I/O 256B, BAR1 MMIO 4KB */
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_PIO;
    s->bar_info[0].size = 256;
    s->bar_info[0].name = "aic7xxx-pio";
    s->bar_info[1].index = 1;
    s->bar_info[1].type = BAR_TYPE_MMIO;
    s->bar_info[1].size = 0x1000;
    s->bar_info[1].name = "aic7xxx-mmio";
    s->num_bars = 2;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* No MSI/MSI-X; driver does not use them */

    /* No explicit DMA configuration required; driver programs DMA mask via PCI API */

    /* Final state initialization: already handled by reset */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    if (msix_enabled(pdev)) {
        msix_uninit(pdev, NULL, NULL);
    }
    if (msi_enabled(pdev)) {
        msi_uninit(pdev);
    }

    /* No extra cleanup needed */
}

/* Minimal VMState to satisfy QEMU migration subsystems */
static const VMStateDescription vmstate_pcibase = {
    .name = "aic7xxx_pci",
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