/*
 * QEMU PCI device model for Areca arcmsr RAID controller (minimal emulation)
 * Target QEMU: 8.2.10
 *
 * This model is intentionally minimal: it only implements the register
 * interfaces and behaviors that are directly exercised by the provided
 * Linux driver (drivers/scsi/arcmsr/arcmsr_hba.c). It is sufficient for
 * the driver to probe, request regions, ioremap, and perform the basic
 * firmware configuration handshake for adapter type A using BAR0 MMIO.
 *
 * Other adapter types, DMA engines, SCSI data paths, and full interrupt
 * semantics are not implemented here because their precise hardware
 * behavior and register layouts are not fully specified by the driver
 * alone. They can be incrementally added in later phases based on
 * concrete runtime feedback.
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

/* ------------------------------------------------------------------ */
/* Macros and constants copied from Stage-1 / driver                   */
/* ------------------------------------------------------------------ */

#define TYPE_PCIBASE_DEVICE "arcmsr_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

#define ARCMSR_SLEEPTIME    10
#define ARCMSR_RETRYCOUNT   12
#define ARCMSR_MAX_CMD_PERLUN        128
#define ACB_ADAPTER_TYPE_E      0x00000004
#define ACB_ADAPTER_TYPE_B      0x00000001
#define ACB_ADAPTER_TYPE_D      0x00000003
#define ACB_ADAPTER_TYPE_F      0x00000005
#define ARCMSR_HBCMU_IOP2DRV_MESSAGE_CMD_DONE_DOORBELL_CLEAR  0x00000008
#define ARCMSR_HBFMU_DOORBELL_SYNC      0x100
#define ACB_ADAPTER_TYPE_C      0x00000002
#define ARCMSR_HBEMU_DOORBELL_SYNC      0x100
#define ACB_ADAPTER_TYPE_A      0x00000000
#define ARCMSR_HBCMU_IOP2DRV_MESSAGE_CMD_DONE         0x00000008
#define ARCMSR_MU_OUTBOUND_MESSAGE0_INT       0x01
#define ARCMSR_MESSAGE_INT_CLEAR_PATTERN           0xFF00FFF7
#define ARCMSR_DRV2IOP_END_OF_INTERRUPT            0x00000010
#define ARCMSR_IOP2DRV_MESSAGE_CMD_DONE            0x00000008
#define ARCMSR_ARC1214_IOP2DRV_MESSAGE_CMD_DONE    0x02000000
#define ARCMSR_HBEMU_IOP2DRV_MESSAGE_CMD_DONE  0x00000008
#define ARCMSR_INBOUND_MESG0_FLUSH_CACHE   0x00000005
#define ARCMSR_MESSAGE_FLUSH_CACHE            0x00050008
#define ARCMSR_HBCMU_DRV2IOP_MESSAGE_CMD_DONE         0x00000008
#define ARCMSR_HBEMU_DRV2IOP_MESSAGE_CMD_DONE 0x00000008
#define ARCMSR_MESSAGE_RWBUFFER           0x0000fa00
#define ARCMSR_DRV2IOP_DOORBELL_MASK      0x00020404
#define ARCMSR_DRV2IOP_DOORBELL_MASK_1203     0x0002187C
#define ARCMSR_DRV2IOP_DOORBELL        0x00020400
#define ARCMSR_IOP2DRV_DOORBELL        0x00020408
#define PCI_DEVICE_ID_ARECA_1203   0x1203
#define ARCMSR_DRV2IOP_DOORBELL_1203      0x00021878
#define ARCMSR_IOP2DRV_DOORBELL_1203      0x00021870
#define ARCMSR_IOP2DRV_DOORBELL_MASK_1203     0x00021874
#define ARCMSR_MESSAGE_WBUFFER           0x0000fe00
#define ARCMSR_IOP2DRV_DOORBELL_MASK     0x0002040C
#define ARCMSR_MESSAGE_RBUFFER           0x0000ff00
#define ARCMSR_ARC1214_RESET_REQUEST         0x00108
#define ARCMSR_ARC1214_MESSAGE_RWBUFFER          0x02200
#define ARCMSR_ARC1214_OUTBOUND_MESSAGE1         0x00424
#define ARCMSR_ARC1214_MESSAGE_WBUFFER           0x02000
#define ARCMSR_ARC1214_INBOUND_LIST_BASE_HIGH        0x01004
#define ARCMSR_ARC1214_OUTBOUND_LIST_COPY_POINTER 0x0106C
#define ARCMSR_ARC1214_MESSAGE_RBUFFER           0x02100
#define ARCMSR_ARC1214_OUTBOUND_INTERRUPT_CAUSE     0x01088
#define ARCMSR_ARC1214_CHIP_ID                0x00004
#define ARCMSR_ARC1214_OUTBOUND_LIST_BASE_HIGH        0x01064
#define ARCMSR_ARC1214_OUTBOUND_DOORBELL          0x00480
#define ARCMSR_ARC1214_OUTBOUND_MESSAGE0         0x00420
#define ARCMSR_ARC1214_INBOUND_MESSAGE0          0x00400
#define ARCMSR_ARC1214_INBOUND_LIST_BASE_LOW     0x01000
#define ARCMSR_ARC1214_OUTBOUND_LIST_BASE_LOW        0x01060
#define ARCMSR_ARC1214_OUTBOUND_INTERRUPT_ENABLE     0x0108C
#define ARCMSR_ARC1214_OUTBOUND_LIST_READ_POINTER     0x01070
#define ARCMSR_ARC1214_CPU_MEMORY_CONFIGURATION       0x00008
#define ARCMSR_ARC1214_INBOUND_LIST_WRITE_POINTER     0x01018
#define ARCMSR_ARC1214_PCIE_F0_INTERRUPT_ENABLE       0x0020C
#define ARCMSR_ARC1214_INBOUND_MESSAGE1           0x00404
#define ARCMSR_ARC1214_OUTBOUND_DOORBELL_ENABLE      0x00484
#define ARCMSR_ARC1214_I2_HOST_INTERRUPT_MASK        0x00034
#define ARCMSR_ARC1214_INBOUND_DOORBELL          0x00460
#define ARCMSR_ARC1214_MAIN_INTERRUPT_STATUS     0x00200
#define ARCMSR_ARC1214_SAMPLE_RESET          0x00100
#define ARCMSR_HBFMU_DOORBELL_SYNC1      0x300
#define MESG_RW_BUFFER_SIZE  (256 * 3)
#define ARCMSR_MAX_HBE_DONEQUEUE 512
#define ARCMSR_XOR_SEG_SIZE   (1024 * 1024)
#define ARCMSR_CDB_SG_PAGE_LENGTH  256
#define ARCMSR_MAX_TARGETID      17
#define ARCMSR_MAX_XFER_LEN      0x26000
#define ARECA_RAID_GONE          0x55
#define ARCMSR_DEFAULT_SG_ENTRIES    38
#define ARCMSR_MAX_TARGETLUN     8
#define ARCMSR_SIGNATURE_GET_CONFIG         0x87974060
#define ACB_F_MSG_GET_CONFIG        0x1000
#define ARCMST_NUM_MSIX_VECTORS     4
#define FW_NORMAL           0x0000
#define IS_DMA64    (sizeof(dma_addr_t) == 8)
#define ARCMSR_DEFAULT_OUTSTANDING_CMD  128
#define ARCMSR_MAX_OUTSTANDING_CMD  1024
#define ARCMSR_DEFAULT_CMD_PERLUN   32
#define ARCMSR_MIN_CMD_PERLUN       1
#define ACB_F_MESSAGE_RQBUFFER_CLEARED  0x0020
#define ACB_F_MESSAGE_WQBUFFER_READED   0x0040
#define ACB_F_SCSISTOPADAPTER           0x0001
#define ACB_F_MESSAGE_WQBUFFER_CLEARED  0x0010
#define ARCMSR_SCSI_INITIATOR_ID    255
#define ARCMSR_MIN_OUTSTANDING_CMD  32
#define ARCMSR_MAX_HBB_POSTQUEUE    264
#define ARCMSR_INBOUND_MESG0_ABORT_CMD      0x00000003
#define ARCMSR_MESSAGE_ABORT_CMD            0x00030008
#define ARCMSR_CCB_DONE     0x0000
#define SCSI_SENSE_CURRENT_ERRORS        0x70
#define ARCMSR_HBEMU_OUTBOUND_POSTQUEUE_ISR 0x00000008
#define ARCMSR_HBEMU_OUTBOUND_DOORBELL_ISR  0x00000001
#define ARCMSR_ARC1214_ALL_INT_DISABLE         0x00000000
#define ARCMSR_MU_OUTBOUND_ALL_INTMASKENABLE       0x1F
#define ARCMSR_HBCMU_ALL_INTMASKENABLE      0x0000000D
#define ARCMSR_DEV_ABORTED          0xF1
#define ARCMSR_DEV_INIT_FAIL        0xF2
#define ARCMSR_DEV_CHECK_CONDITION      0x02
#define ARCMSR_DEV_SELECT_TIMEOUT       0xF0
#define ARECA_RAID_GOOD          0xaa
#define ARCMSR_CCB_ABORTED   0xAA55
#define ARCMSR_CCB_START 0x55AA
#define ARCMSR_DOORBELL_INT_CLEAR_PATTERN            0xFF00FFF0
#define ARCMSR_CCBREPLY_FLAG_ERROR_MODE1  0x00000001
#define ARCMSR_HBCMU_OUTBOUND_POSTQUEUE_ISR  0x00000008
#define ARCMSR_MAX_ARC1214_DONEQUEUE  257
#define ARCMSR_CCBREPLY_FLAG_ERROR_MODE0  0x10000000
#define ACB_F_IOP_INITED                0x0100
#define ACB_F_ADAPTER_REMOVED       0x0800
#define ARCMSR_HBCMU_OUTBOUND_DOORBELL_ISR_MASK  0x00000004
#define ARCMSR_MU_OUTBOUND_POSTQUEUE_INTMASKENABLE   0x08
#define ARCMSR_MU_OUTBOUND_MESSAGE0_INTMASKENABLE    0x01
#define ARCMSR_ARC1214_ALL_INT_ENABLE         0x00001010
#define ARCMSR_IOP2DRV_DATA_READ_OK          0x00000002
#define ARCMSR_MU_OUTBOUND_DOORBELL_INTMASKENABLE    0x04
#define ARCMSR_HBCMU_UTILITY_A_ISR_MASK      0x00000001
#define ARCMSR_IOP2DRV_CDB_DONE          0x00000004
#define ARCMSR_HBCMU_OUTBOUND_POSTQUEUE_ISR_MASK 0x00000008
#define ARCMSR_IOP2DRV_DATA_WRITE_OK         0x00000001
#define ARCMSR_CDB_FLAG_SGL_BSIZE          0x01
#define IS_SG64_ADDR    0x01000000
#define ARCMSR_CDB_FLAG_WRITE              0x04
#define ARCMSR_DRV2IOP_CDB_POSTED          0x00000004
#define ARCMSR_CCBPOST_FLAG_SGL_BSIZE      0x80000000
#define ARCMSR_MAX_ARC1214_POSTQUEUE   256
#define ARCMSR_INBOUND_MESG0_STOP_BGRB      0x00000004
#define ACB_F_MSG_START_BGRB           0x0004
#define ARCMSR_MESSAGE_STOP_BGRB            0x00040008
#define ARCMSR_HBEMU_DRV2IOP_DATA_READ_OK   0x00000004
#define ARCMSR_DRV2IOP_DATA_READ_OK         0x00000002
#define ARCMSR_ARC1214_DRV2IOP_DATA_OUT_READ    0x00000002
#define ARCMSR_INBOUND_DRIVER_DATA_READ_OK  0x00000002
#define ARCMSR_HBCMU_DRV2IOP_DATA_READ_OK           0x00000004
#define ARCMSR_HBEMU_DRV2IOP_DATA_WRITE_OK  0x00000002
#define ARCMSR_HBCMU_DRV2IOP_DATA_WRITE_OK          0x00000002
#define ARCMSR_ARC1214_DRV2IOP_DATA_IN_READY    0x00000001
#define ARCMSR_DRV2IOP_DATA_WRITE_OK         0x00000001
#define ARCMSR_INBOUND_DRIVER_DATA_WRITE_OK  0x00000001
#define ARCMSR_MAX_QBUFFER          4096
#define ACB_F_IOPDATA_OVERFLOW          0x0008
#define ARCMSR_OUTBOUND_IOP331_DATA_READ_OK  0x00000002
#define ARCMSR_OUTBOUND_IOP331_DATA_WRITE_OK 0x00000001
#define ARCMSR_HBCMU_IOP2DRV_DATA_WRITE_OK          0x00000002
#define ARCMSR_HBCMU_IOP2DRV_DATA_READ_OK           0x00000004
#define ARCMSR_ARC1214_IOP2DRV_DATA_READ_OK     0x00000002
#define ARCMSR_ARC1214_IOP2DRV_DATA_WRITE_OK        0x00000001
#define ARCMSR_HBEMU_IOP2DRV_DATA_WRITE_OK  0x00000002
#define ARCMSR_HBEMU_IOP2DRV_DATA_READ_OK   0x00000004
#define ARCMSR_HBC_ISR_THROTTLING_LEVEL     12
#define ARCMSR_HBCMU_DRV2IOP_POSTQUEUE_THROTTLING       0x00000010
#define ARCMSR_ARC1214_OUTBOUND_LIST_INTERRUPT_CLEAR    0x00000001
#define ARCMSR_MU_OUTBOUND_POSTQUEUE_INT    0x08
#define ARCMSR_MU_OUTBOUND_MESSAGE1_INT     0x02
#define ARCMSR_MU_OUTBOUND_PCI_INT      0x10
#define ARCMSR_HBCMU_OUTBOUND_DOORBELL_ISR  0x00000004
#define ARCMSR_ARC1214_OUTBOUND_DOORBELL_ISR        0x00001000
#define ARCMSR_ARC1214_OUTBOUND_POSTQUEUE_ISR        0x00000010
#define ARCMSR_MESSAGE_FAIL           0x0001
#define ARCMSR_API_DATA_BUFLEN    1032
#define FW_DEADLOCK           0x0010
#define ARCMSR_MESSAGE_RETURNCODE_OK         0x00000001
#define ARCMSR_MESSAGE_RETURNCODE_BUS_HANG_ON    0x00000088
#define ARCMSR_MESSAGE_RETURNCODE_3F         0x0000003F
#define ARCMSR_HBEMU_ALL_INTMASKENABLE      0x00000009
#define ARCMSR_MAX_FREECCB_NUM      1024
#define ARCMSR_MESSAGE_SYNC_TIMER            0x00080008
#define ARCMSR_MINUTES          (1000 * 60 * 60)
#define ARCMSR_HOURS            (1000 * 60 * 60 * 4)
#define ARCMSR_INBOUND_MESG0_SYNC_TIMER     0x00000008
#define ARCMSR_MESSAGE_SET_POST_WINDOW            0x000F0008
#define ARCMSR_INBOUND_MESG0_SET_CONFIG       0x00000002
#define ARCMSR_SIGNATURE_1884         0x188417D3
#define ARCMSR_SIGNATURE_SET_CONFIG       0x87974063
#define ARCMSR_SIGNATURE_1886         0x188617D3
#define ARCMSR_MESSAGE_SET_CONFIG            0x00020008
#define ARCMSR_HBEMU_MESSAGE_FIRMWARE_OK 0x80000000
#define ARCMSR_ARC1214_MESSAGE_FIRMWARE_OK      0x80000000
#define ARCMSR_HBCMU_MESSAGE_FIRMWARE_OK            0x80000000
#define ARCMSR_MESSAGE_FIRMWARE_OK            0x80000000
#define ARCMSR_OUTBOUND_MESG1_FIRMWARE_OK 0x80000000
#define ARCMSR_HBFMU_MESSAGE_NO_VOLUME_CHANGE 0x20000000
#define ARCMSR_HBFMU_MESSAGE_FIRMWARE_OK  0x80000000
#define ACB_F_BUS_RESET                0x0080
#define ACB_F_ABORT            0x0200
#define ARCMSR_INBOUND_MESG0_START_BGRB     0x00000006
#define ARCMSR_MESSAGE_START_BGRB           0x00060008
#define ARCMSR_MESSAGE_ACTIVE_EOI_MODE          0x00100008
#define ARCMSR_ARC1680_BUS_RESET        0x00000003
#define ARCMSR_ARC1880_RESET_ADAPTER        0x00000024
#define ARCMSR_ARC188X_RESET_ADAPTER        0x00000004
#define PCI_DEVICE_ID_ARECA_1880    0x1880
#define ARCMSR_DRIVER_VERSION       "v1.51.00.14-20230915"
#define PCI_DEVICE_ID_ARECA_1883    0x1883
#define PCI_DEVICE_ID_ARECA_1886    0x188A
#define PCI_DEVICE_ID_ARECA_1884    0x1884
#define PCI_DEVICE_ID_ARECA_1886_0  0x1886
#define PCI_DEVICE_ID_ARECA_1214    0x1214
#define ARCMSR_NAME         "arcmsr"
#define ARCMSR_MAX_XFER_SECTORS_C   304
#define ACB_F_FIRMWARE_TRAP            0x0400
#define ACB_F_MSG_STOP_BGRB        0x0002
#define FW_BOG              0x0001
#define CCB_FLAG_WRITE      0x0001
#define ARCMSR_CCB_ILLEGAL  0xFFFF
#define CCB_FLAG_FLUSHCACHE 0x0004
#define CCB_FLAG_MASTER_ABORTED  0x0008
#define CCB_FLAG_ERROR      0x0002
#define CCB_FLAG_READ       0x0000
#define SCSI_SENSE_DEFERRED_ERRORS        0x71
#define ARCMSR_CDB_FLAG_ORDEREDQ           0x10
#define ARCMSR_CDB_FLAG_BIOS               0x02
#define ARCMSR_CDB_FLAG_HEADQ              0x08
#define ARCMSR_CDB_FLAG_SIMPLEQ            0x00
#define ARECA_SATA_RAID         0x90000000
#define FUNCTION_CLEAR_RQBUFFER     0x0803
#define FUNCTION_RETURN_CODE_3F     0x0806
#define FUNCTION_CLEAR_ALLQBUFFER       0x0805
#define FUNCTION_CLEAR_WQBUFFER     0x0804
#define FUNCTION_SAY_GOODBYE           0x0808
#define FUNCTION_WRITE_WQBUFFER        0x0802
#define FUNCTION_READ_RQBUFFER     0x0801
#define FUNCTION_FLUSH_ADAPTER_CACHE      0x0809
#define FUNCTION_SAY_HELLO           0x0807

#define PCI_VENDOR_ID_ARECA        0x17d3
#define PCI_DEVICE_ID_ARECA_1110   0x1110
#define PCI_CLASS_STORAGE_RAID     0x0104

#define ARCMSR_INBOUND_MESG0_GET_CONFIG        0x00000001

/* BAR layout: from driver we know adapter type A uses BAR0 MMIO. Size is not
 * explicitly stated; we conservatively expose 4 KiB which is enough to cover
 * all offsets used for type A.
 */

/* ------------------------------------------------------------------ */
/* BAR metadata definition                                             */
/* ------------------------------------------------------------------ */
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

/* ------------------------------------------------------------------ */
/* Simplified adapter type and MU-A register offsets (emulated)        */
/* ------------------------------------------------------------------ */

/* For this minimal model we emulate only adapter type A and its
 * MessageUnit_A registers that the driver actually touches.
 * Offsets are based on struct MessageUnit_A in the driver:
 * inbound_msgaddr0 at 0x10, outbound_msgaddr1 at 0x1C, outbound_intstatus
 * at 0x30, outbound_intmask at 0x34, outbound_doorbell at 0x2C,
 * inbound_doorbell at 0x20, outbound_queueport at 0x44,
 * message_rwbuffer at 0x0A00.
 */

#define ARCMSR_OFF_INBOUND_MSGADDR0      0x0010
#define ARCMSR_OFF_INBOUND_MSGADDR1      0x0014
#define ARCMSR_OFF_OUTBOUND_MSGADDR0     0x0018
#define ARCMSR_OFF_OUTBOUND_MSGADDR1     0x001C
#define ARCMSR_OFF_INBOUND_DOORBELL      0x0020
#define ARCMSR_OFF_INBOUND_INTSTATUS     0x0024
#define ARCMSR_OFF_INBOUND_INTMASK       0x0028
#define ARCMSR_OFF_OUTBOUND_DOORBELL     0x002C
#define ARCMSR_OFF_OUTBOUND_INTSTATUS    0x0030
#define ARCMSR_OFF_OUTBOUND_INTMASK      0x0034
#define ARCMSR_OFF_INBOUND_QUEUEPORT     0x0040
#define ARCMSR_OFF_OUTBOUND_QUEUEPORT    0x0044
#define ARCMSR_OFF_MESSAGE_RWBUFFER      0x0A00

/* Size of MMIO region for adapter A: covers up to end of message_rbuffer */
#define ARCMSR_MMIO_SIZE_A       0x1000

/* ------------------------------------------------------------------ */
/* Device State                                                        */
/* ------------------------------------------------------------------ */
struct PCIBaseState {
    PCIDevice parent_obj;

    /* BAR memory regions */
    MemoryRegion bar_regions[6];

    /* optional linear backing (not used) */
    uint8_t *mmio_backing;
    size_t mmio_backing_size;

    /* BAR table */
    BARInfo bar_info[6];
    int num_bars;

    /* interrupt state */
    bool has_msi;
    bool has_msix;

    /* Minimal emulated registers for adapter type A */
    uint32_t adapter_type;   /* fixed to ACB_ADAPTER_TYPE_A */

    uint32_t inbound_msgaddr0;
    uint32_t inbound_msgaddr1;
    uint32_t outbound_msgaddr0;
    uint32_t outbound_msgaddr1;
    uint32_t inbound_doorbell;
    uint32_t inbound_intstatus;
    uint32_t inbound_intmask;
    uint32_t outbound_doorbell;
    uint32_t outbound_intstatus;
    uint32_t outbound_intmask;
    uint32_t inbound_queueport;
    uint32_t outbound_queueport;

    /* RW buffer used for firmware config (u32 array) */
    uint32_t message_rwbuffer[256];

    /* interrupt line state */
    bool irq_level;
};

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size);
static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
static uint32_t pcibase_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void pcibase_config_write(PCIDevice *pdev, uint32_t addr, uint32_t val, int len);
static void pcibase_reset(DeviceState *dev);
static void pcibase_realize(PCIDevice *pdev, Error **errp);
static void pcibase_uninit(PCIDevice *pdev);

/* ------------------------------------------------------------------ */
/* MemoryRegionOps                                                      */
/* ------------------------------------------------------------------ */
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

/* ------------------------------------------------------------------ */
/* Helper: raise/lower legacy INTx                                     */
/* ------------------------------------------------------------------ */
static void arcmsr_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    pci_set_irq(pdev, s->irq_level ? 1 : 0);
}

static void arcmsr_raise_irq(PCIBaseState *s)
{
    if (!s->irq_level) {
        s->irq_level = true;
        arcmsr_update_irq(s);
    }
}

static void arcmsr_lower_irq(PCIBaseState *s)
{
    if (s->irq_level) {
        s->irq_level = false;
        arcmsr_update_irq(s);
    }
}

/* ------------------------------------------------------------------ */
/* Helper: register a BAR (MMIO or PIO)                               */
/* ------------------------------------------------------------------ */
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

/* ------------------------------------------------------------------ */
/* MMIO handlers (minimal emulation for adapter type A)                */
/* ------------------------------------------------------------------ */

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t val32 = 0;

    /* Adapter type is fixed to A; implement the offsets used by
     * the driver during initialization and message handling.
     */

    switch (addr) {
    case ARCMSR_OFF_INBOUND_MSGADDR0:
        val32 = s->inbound_msgaddr0;
        break;
    case ARCMSR_OFF_INBOUND_MSGADDR1:
        val32 = s->inbound_msgaddr1;
        break;
    case ARCMSR_OFF_OUTBOUND_MSGADDR0:
        val32 = s->outbound_msgaddr0;
        break;
    case ARCMSR_OFF_OUTBOUND_MSGADDR1:
        /* Firmware ready bit used by arcmsr_wait_firmware_ready() */
        val32 = s->outbound_msgaddr1;
        break;
    case ARCMSR_OFF_INBOUND_DOORBELL:
        val32 = s->inbound_doorbell;
        break;
    case ARCMSR_OFF_INBOUND_INTSTATUS:
        val32 = s->inbound_intstatus;
        break;
    case ARCMSR_OFF_INBOUND_INTMASK:
        val32 = s->inbound_intmask;
        break;
    case ARCMSR_OFF_OUTBOUND_DOORBELL:
        val32 = s->outbound_doorbell;
        break;
    case ARCMSR_OFF_OUTBOUND_INTSTATUS:
        val32 = s->outbound_intstatus;
        break;
    case ARCMSR_OFF_OUTBOUND_INTMASK:
        val32 = s->outbound_intmask;
        break;
    case ARCMSR_OFF_INBOUND_QUEUEPORT:
        val32 = s->inbound_queueport;
        break;
    case ARCMSR_OFF_OUTBOUND_QUEUEPORT:
        /* When driver polls completion it expects 0xFFFFFFFF when queue is empty. */
        val32 = 0xFFFFFFFF;
        break;
    default:
        if (addr >= ARCMSR_OFF_MESSAGE_RWBUFFER &&
            addr < ARCMSR_OFF_MESSAGE_RWBUFFER + sizeof(s->message_rwbuffer)) {
            unsigned index = (addr - ARCMSR_OFF_MESSAGE_RWBUFFER) >> 2;
            val32 = s->message_rwbuffer[index];
        } else {
            qemu_log_mask(LOG_UNIMP,
                          "[%s] mmio_read addr=%" PRIx64 " size=%u\n",
                          TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
            val32 = 0;
        }
        break;
    }

    return val32;
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    uint32_t v = (uint32_t)val;

    switch (addr) {
    case ARCMSR_OFF_INBOUND_MSGADDR0:
        /* Driver writes message codes here (GET_CONFIG, SYNC_TIMER, etc.). */
        s->inbound_msgaddr0 = v;
        if (v == ARCMSR_INBOUND_MESG0_GET_CONFIG) {
            /* Prepare firmware configuration in message_rwbuffer and
             * raise MESSAGE0 interrupt so arcmsr_get_firmware_spec()
             * can complete for adapter type A.
             */
            s->message_rwbuffer[0] = ARCMSR_SIGNATURE_GET_CONFIG;
            s->message_rwbuffer[1] = 0x20;   /* firm_request_len (arbitrary) */
            s->message_rwbuffer[2] = 128;    /* firm_numbers_queue */
            s->message_rwbuffer[3] = 0x100;  /* firm_sdram_size */
            s->message_rwbuffer[4] = 4;      /* firm_hd_channels */
            /* firm_model at [15..17] and firm_version at [18..22] as chars */
            /* We only need them to be non-zero strings. */
            /* model "QEMU ARC" */
            {
                const char model[] = "QEMU ARC";
                int i;
                for (i = 0; i < (int)sizeof(model); i++) {
                    ((char *)s->message_rwbuffer)[(15 * 4) + i] = model[i];
                }
            }
            /* version "1.0" */
            {
                const char vers[] = "1.0";
                int i;
                for (i = 0; i < (int)sizeof(vers); i++) {
                    ((char *)s->message_rwbuffer)[(18 * 4) + i] = vers[i];
                }
            }
            /* firm_cfg_version at [25] */
            s->message_rwbuffer[25] = 0x00030000;
            /* firm_PicStatus at [30] */
            s->message_rwbuffer[30] = 0;

            /* Signal message interrupt ready:
             * arcmsr_hbaA_wait_msgint_ready() (via generic helpers)
             * waits on outbound_intstatus & ARCMSR_MU_OUTBOUND_MESSAGE0_INT.
             */
            s->outbound_intstatus |= ARCMSR_MU_OUTBOUND_MESSAGE0_INT;
            arcmsr_raise_irq(s);
        } else if (v == ARCMSR_INBOUND_MESG0_SYNC_TIMER) {
            /* For SYNC_TIMER we just acknowledge immediately by
             * raising MESSAGE0 interrupt; no data needs to be returned.
             */
            s->outbound_intstatus |= ARCMSR_MU_OUTBOUND_MESSAGE0_INT;
            arcmsr_raise_irq(s);
        }
        break;

    case ARCMSR_OFF_INBOUND_MSGADDR1:
        s->inbound_msgaddr1 = v;
        break;

    case ARCMSR_OFF_OUTBOUND_MSGADDR0:
        s->outbound_msgaddr0 = v;
        break;

    case ARCMSR_OFF_OUTBOUND_MSGADDR1:
        /* Usually written by firmware; ignore host writes. */
        break;

    case ARCMSR_OFF_INBOUND_DOORBELL:
        s->inbound_doorbell = v;
        break;

    case ARCMSR_OFF_INBOUND_INTSTATUS:
        /* Not used by driver for adapter A; just store/clear. */
        s->inbound_intstatus &= ~v;
        break;

    case ARCMSR_OFF_INBOUND_INTMASK:
        s->inbound_intmask = v;
        break;

    case ARCMSR_OFF_OUTBOUND_DOORBELL:
        /* For adapter A, driver reads/clears this in doorbell helpers.
         * We treat writes as clear of bits set in v.
         */
        s->outbound_doorbell &= ~v;
        break;

    case ARCMSR_OFF_OUTBOUND_INTSTATUS:
        /* Writing back bits clears them. */
        s->outbound_intstatus &= ~v;
        if (s->outbound_intstatus == 0) {
            arcmsr_lower_irq(s);
        }
        break;

    case ARCMSR_OFF_OUTBOUND_INTMASK:
        s->outbound_intmask = v;
        break;

    case ARCMSR_OFF_INBOUND_QUEUEPORT:
        s->inbound_queueport = v;
        break;

    case ARCMSR_OFF_OUTBOUND_QUEUEPORT:
        /* Host does not write this in the driver; ignore safely. */
        break;

    default:
        if (addr >= ARCMSR_OFF_MESSAGE_RWBUFFER &&
            addr < ARCMSR_OFF_MESSAGE_RWBUFFER + sizeof(s->message_rwbuffer)) {
            unsigned index = (addr - ARCMSR_OFF_MESSAGE_RWBUFFER) >> 2;
            s->message_rwbuffer[index] = v;
        } else {
            qemu_log_mask(LOG_UNIMP,
                          "[%s] mmio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n",
                          TYPE_PCIBASE_DEVICE, (uint64_t)addr,
                          (uint64_t)val, size);
        }
        break;
    }
}

static uint64_t pcibase_pio_read(void *opaque, hwaddr addr, unsigned size)
{
    /* No PIO BARs are used by the driver for this device. */
    qemu_log_mask(LOG_UNIMP,
                  "[%s] pio_read addr=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr, size);
    return 0;
}

static void pcibase_pio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    /* No PIO BARs are used. */
    qemu_log_mask(LOG_UNIMP,
                  "[%s] pio_write addr=%" PRIx64 " val=%" PRIx64 " size=%u\n",
                  TYPE_PCIBASE_DEVICE, (uint64_t)addr,
                  (uint64_t)val, size);
}

/* ------------------------------------------------------------------ */
/* Reset                                                              */
/* ------------------------------------------------------------------ */
static void pcibase_reset(DeviceState *dev)
{
    PCIBaseState *s = PCIBASE_DEVICE(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);

    pci_device_reset(pdev);

    s->adapter_type = ACB_ADAPTER_TYPE_A;

    s->inbound_msgaddr0 = 0;
    s->inbound_msgaddr1 = 0;
    s->outbound_msgaddr0 = 0;
    /* Signal firmware ready for type A immediately so that
     * arcmsr_wait_firmware_ready() can succeed. It tests
     * outbound_msgaddr1 & ARCMSR_OUTBOUND_MESG1_FIRMWARE_OK.
     */
    s->outbound_msgaddr1 = ARCMSR_OUTBOUND_MESG1_FIRMWARE_OK;

    s->inbound_doorbell = 0;
    s->inbound_intstatus = 0;
    s->inbound_intmask = 0;
    s->outbound_doorbell = 0;
    s->outbound_intstatus = 0;
    s->outbound_intmask = 0;

    s->inbound_queueport = 0;
    s->outbound_queueport = 0xFFFFFFFF;

    memset(s->message_rwbuffer, 0, sizeof(s->message_rwbuffer));

    s->irq_level = false;
    arcmsr_update_irq(s);

    if (s->mmio_backing && s->mmio_backing_size) {
        memset(s->mmio_backing, 0, s->mmio_backing_size);
    }
}

/* ------------------------------------------------------------------ */
/* DMA initialize (not used in minimal model)                         */
/* ------------------------------------------------------------------ */
static void pcibase_dma_device_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);

    (void)s;
    (void)errp;
}

/* ------------------------------------------------------------------ */
/* PCI config space access                                             */
/* ------------------------------------------------------------------ */
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

/* ------------------------------------------------------------------ */
/* Realize (device init)                                              */
/* ------------------------------------------------------------------ */
static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    /* Identify as Areca 1110 RAID controller */
    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_ARECA);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_ARECA_1110);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_STORAGE_RAID);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1); /* INTA# */

    s->adapter_type = ACB_ADAPTER_TYPE_A;

    /* Define BAR0 as MMIO for MessageUnit_A; size covers MU-A layout. */
    s->num_bars = 1;
    s->bar_info[0].index = 0;
    s->bar_info[0].type = BAR_TYPE_MMIO;
    s->bar_info[0].size = ARCMSR_MMIO_SIZE_A;
    s->bar_info[0].name = "arcmsr-mmio";
    s->bar_info[0].sparse = false;

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
        if (errp && *errp) {
            return;
        }
    }

    /* We rely on legacy INTx; the driver will request_irq() on the
     * assigned IRQ line. MSI/MSI-X negotiation is done in the driver
     * via pci_alloc_irq_vectors(), which will fall back to INTx if MSI
     * is not enabled in hardware. We do not enable MSI/MSI-X here.
     */

    pcibase_dma_device_realize(pdev, errp);
    if (errp && *errp) {
        return;
    }

    qemu_log_mask(LOG_UNIMP, "[%s] device realized (minimal arcmsr model)\n",
                  TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Uninit/cleanup                                                     */
/* ------------------------------------------------------------------ */
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

    qemu_log_mask(LOG_UNIMP, "[%s] device uninit\n", TYPE_PCIBASE_DEVICE);
}

/* ------------------------------------------------------------------ */
/* Class init / type registration                                     */
/* ------------------------------------------------------------------ */
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
