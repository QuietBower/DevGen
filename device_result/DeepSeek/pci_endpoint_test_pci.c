/*
 * QEMU model of a PCI endpoint test device (PCI IDs: 0x104c:0xb500)
 * Based on Linux driver pci_endpoint_test.c
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

#define TYPE_PCIBASE_DEVICE "pci_endpoint_test_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor and Device IDs */
#define PCI_VENDOR_ID_TI      0x104c
#define PCI_DEVICE_ID_TI_DRA74x 0xb500
#define VENDOR_ID PCI_VENDOR_ID_TI
#define DEVICE_ID PCI_DEVICE_ID_TI_DRA74x
#define CLASS_ID 0xFF0000 /* Other */

/* Register offsets */
#define PCI_ENDPOINT_TEST_MAGIC          0x0
#define PCI_ENDPOINT_TEST_COMMAND        0x4
#define PCI_ENDPOINT_TEST_STATUS         0x8
#define PCI_ENDPOINT_TEST_LOWER_SRC_ADDR 0x0c
#define PCI_ENDPOINT_TEST_UPPER_SRC_ADDR 0x10
#define PCI_ENDPOINT_TEST_LOWER_DST_ADDR 0x14
#define PCI_ENDPOINT_TEST_UPPER_DST_ADDR 0x18
#define PCI_ENDPOINT_TEST_SIZE           0x1c
#define PCI_ENDPOINT_TEST_CHECKSUM       0x20
#define PCI_ENDPOINT_TEST_IRQ_TYPE       0x24
#define PCI_ENDPOINT_TEST_IRQ_NUMBER     0x28
#define PCI_ENDPOINT_TEST_FLAGS          0x2c
#define PCI_ENDPOINT_TEST_CAPS           0x30
#define PCI_ENDPOINT_TEST_DB_BAR         0x34
#define PCI_ENDPOINT_TEST_DB_OFFSET      0x38
#define PCI_ENDPOINT_TEST_DB_DATA        0x3c

/* Command bits */
#define COMMAND_RAISE_INTX_IRQ           BIT(0)
#define COMMAND_RAISE_MSI_IRQ            BIT(1)
#define COMMAND_RAISE_MSIX_IRQ           BIT(2)
#define COMMAND_READ                     BIT(3)
#define COMMAND_WRITE                    BIT(4)
#define COMMAND_COPY                     BIT(5)
#define COMMAND_ENABLE_DOORBELL          BIT(6)
#define COMMAND_DISABLE_DOORBELL         BIT(7)
#define COMMAND_BAR_SUBRANGE_SETUP       BIT(8)
#define COMMAND_BAR_SUBRANGE_CLEAR       BIT(9)

/* Status bits */
#define STATUS_READ_SUCCESS              BIT(0)
#define STATUS_READ_FAIL                 BIT(1)
#define STATUS_WRITE_SUCCESS             BIT(2)
#define STATUS_WRITE_FAIL                BIT(3)
#define STATUS_COPY_SUCCESS              BIT(4)
#define STATUS_COPY_FAIL                 BIT(5)
#define STATUS_IRQ_RAISED                BIT(6)
#define STATUS_SRC_ADDR_INVALID          BIT(7)
#define STATUS_DST_ADDR_INVALID          BIT(8)
#define STATUS_DOORBELL_SUCCESS          BIT(9)
#define STATUS_DOORBELL_ENABLE_SUCCESS   BIT(10)
#define STATUS_DOORBELL_ENABLE_FAIL      BIT(11)
#define STATUS_DOORBELL_DISABLE_SUCCESS  BIT(12)
#define STATUS_DOORBELL_DISABLE_FAIL     BIT(13)
#define STATUS_BAR_SUBRANGE_SETUP_SUCCESS BIT(14)
#define STATUS_BAR_SUBRANGE_SETUP_FAIL   BIT(15)
#define STATUS_BAR_SUBRANGE_CLEAR_SUCCESS BIT(16)
#define STATUS_BAR_SUBRANGE_CLEAR_FAIL   BIT(17)
#define STATUS_NO_RESOURCE               BIT(18)

/* Flag bits */
#define FLAG_USE_DMA                     BIT(0)

/* Capability bits */
#define CAP_UNALIGNED_ACCESS             BIT(0)
#define CAP_MSI                          BIT(1)
#define CAP_MSIX                         BIT(2)
#define CAP_INTX                         BIT(3)
#define CAP_SUBRANGE_MAPPING             BIT(4)
#define CAP_DYNAMIC_INBOUND_MAPPING      BIT(5)
#define CAP_BAR0_RESERVED                BIT(6)
#define CAP_BAR1_RESERVED                BIT(7)
#define CAP_BAR2_RESERVED                BIT(8)
#define CAP_BAR3_RESERVED                BIT(9)
#define CAP_BAR4_RESERVED                BIT(10)
#define CAP_BAR5_RESERVED                BIT(11)

#define PCI_ENDPOINT_TEST_BAR_SUBRANGE_NSUB 2

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

    /* Hardware Register Shadows */
    struct {
        uint32_t magic;
        uint32_t command;
        uint32_t status;
        uint32_t lower_src_addr;
        uint32_t upper_src_addr;
        uint32_t lower_dst_addr;
        uint32_t upper_dst_addr;
        uint32_t size;
        uint32_t checksum;
        uint32_t irq_type;
        uint32_t irq_number;
        uint32_t flags;
        uint32_t caps;
        uint32_t db_bar;
        uint32_t db_offset;
        uint32_t db_data;
    } regs;

    /* DMA Context */
    dma_addr_t dma_src;
    dma_addr_t dma_dst;
    dma_addr_t dma_size;
    bool dma_active;
};

/* CRC32 (IEEE 802.3) implementation for DMA tests */
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
    0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
    0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
    0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
    0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
    0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
    0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
    0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
    0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
    0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
    0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
    0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
    0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB30A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
    0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
    0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
    0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
    0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
    0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
};

static uint32_t crc32_le(uint32_t crc, const uint8_t *buf, size_t len)
{
    unsigned int i;
    crc = ~crc;
    for (i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ buf[i]) & 0xFF];
    }
    return ~crc;
}

/* Internal helper for status-triggered signaling. */
static void pcibase_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    if (s->regs.status & STATUS_IRQ_RAISED) {
        pci_set_irq(pdev, 1);
    } else {
        pci_set_irq(pdev, 0);
    }
}

/* DMA: device reads from host memory (COMMAND_READ) */
static void pcibase_dma_read(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint64_t src_addr = ((uint64_t)s->regs.upper_src_addr << 32) | s->regs.lower_src_addr;
    uint32_t size = s->regs.size;
    uint32_t expected_crc = s->regs.checksum;
    uint32_t computed_crc;
    uint8_t *buf;
    bool fail = true;

    if (size == 0 || size > 1024 * 1024) {
        s->regs.status |= STATUS_READ_FAIL;
        goto out;
    }

    buf = g_malloc(size);
    if (!buf) {
        s->regs.status |= STATUS_READ_FAIL;
        goto out;
    }

    if (pci_dma_read(pdev, (dma_addr_t)src_addr, buf, size) != 0) {
        g_free(buf);
        s->regs.status |= STATUS_READ_FAIL;
        goto out;
    }

    computed_crc = crc32_le(0, buf, size);
    g_free(buf);

    if (computed_crc == expected_crc) {
        s->regs.status |= STATUS_READ_SUCCESS;
    } else {
        s->regs.status |= STATUS_READ_FAIL;
    }

    fail = false;
out:
    if (fail) {
        s->regs.status |= STATUS_IRQ_RAISED;
        pcibase_update_irq(s);
        return;
    }
    s->regs.status |= STATUS_IRQ_RAISED;
    pcibase_update_irq(s);
}

/* DMA: device writes to host memory (COMMAND_WRITE) */
static void pcibase_dma_write(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint64_t dst_addr = ((uint64_t)s->regs.upper_dst_addr << 32) | s->regs.lower_dst_addr;
    uint32_t size = s->regs.size;
    uint8_t *buf;
    bool fail = true;

    if (size == 0 || size > 1024 * 1024) {
        s->regs.status |= STATUS_WRITE_FAIL;
        goto out;
    }

    buf = g_malloc(size);
    if (!buf) {
        s->regs.status |= STATUS_WRITE_FAIL;
        goto out;
    }

    /* Fill buffer with a deterministic pattern (all zeros) */
    memset(buf, 0, size);

    if (pci_dma_write(pdev, (dma_addr_t)dst_addr, buf, size) != 0) {
        g_free(buf);
        s->regs.status |= STATUS_WRITE_FAIL;
        goto out;
    }

    s->regs.checksum = crc32_le(0, buf, size);
    g_free(buf);
    s->regs.status |= STATUS_WRITE_SUCCESS;

    fail = false;
out:
    if (fail) {
        s->regs.status |= STATUS_IRQ_RAISED;
        pcibase_update_irq(s);
        return;
    }
    s->regs.status |= STATUS_IRQ_RAISED;
    pcibase_update_irq(s);
}

/* DMA: device copy from src to dst (COMMAND_COPY) */
static void pcibase_dma_copy(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint64_t src_addr = ((uint64_t)s->regs.upper_src_addr << 32) | s->regs.lower_src_addr;
    uint64_t dst_addr = ((uint64_t)s->regs.upper_dst_addr << 32) | s->regs.lower_dst_addr;
    uint32_t size = s->regs.size;
    uint8_t *buf;
    bool fail = true;

    if (size == 0 || size > 1024 * 1024) {
        s->regs.status |= STATUS_COPY_FAIL;
        goto out;
    }

    buf = g_malloc(size);
    if (!buf) {
        s->regs.status |= STATUS_COPY_FAIL;
        goto out;
    }

    if (pci_dma_read(pdev, (dma_addr_t)src_addr, buf, size) != 0) {
        g_free(buf);
        s->regs.status |= STATUS_COPY_FAIL;
        goto out;
    }

    if (pci_dma_write(pdev, (dma_addr_t)dst_addr, buf, size) != 0) {
        g_free(buf);
        s->regs.status |= STATUS_COPY_FAIL;
        goto out;
    }

    g_free(buf);
    s->regs.status |= STATUS_COPY_SUCCESS;

    fail = false;
out:
    if (fail) {
        s->regs.status |= STATUS_IRQ_RAISED;
        pcibase_update_irq(s);
        return;
    }
    s->regs.status |= STATUS_IRQ_RAISED;
    pcibase_update_irq(s);
}

/* Process a written command */
static void pcibase_process_command(PCIBaseState *s)
{
    uint32_t cmd = s->regs.command;

    if (cmd & COMMAND_RAISE_INTX_IRQ) {
        s->regs.status |= STATUS_IRQ_RAISED;
        pcibase_update_irq(s);
    } else if (cmd & COMMAND_RAISE_MSI_IRQ) {
        /* MSI not supported */
    } else if (cmd & COMMAND_RAISE_MSIX_IRQ) {
        /* MSI-X not supported */
    } else if (cmd & COMMAND_READ) {
        pcibase_dma_read(s);
    } else if (cmd & COMMAND_WRITE) {
        pcibase_dma_write(s);
    } else if (cmd & COMMAND_COPY) {
        pcibase_dma_copy(s);
    } else if (cmd & COMMAND_ENABLE_DOORBELL) {
        s->regs.status |= STATUS_DOORBELL_ENABLE_SUCCESS | STATUS_IRQ_RAISED;
        pcibase_update_irq(s);
    } else if (cmd & COMMAND_DISABLE_DOORBELL) {
        s->regs.status |= STATUS_DOORBELL_DISABLE_SUCCESS | STATUS_IRQ_RAISED;
        pcibase_update_irq(s);
    } else if (cmd & COMMAND_BAR_SUBRANGE_SETUP) {
        s->regs.status |= STATUS_BAR_SUBRANGE_SETUP_SUCCESS | STATUS_IRQ_RAISED;
        pcibase_update_irq(s);
    } else if (cmd & COMMAND_BAR_SUBRANGE_CLEAR) {
        s->regs.status |= STATUS_BAR_SUBRANGE_CLEAR_SUCCESS | STATUS_IRQ_RAISED;
        pcibase_update_irq(s);
    }
}

/* MMIO read handler for registers */
static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    uint64_t val = 0;

    if (addr >= 0x40) {
        return 0;
    }

    switch (addr) {
    case PCI_ENDPOINT_TEST_MAGIC:        val = s->regs.magic; break;
    case PCI_ENDPOINT_TEST_COMMAND:      val = s->regs.command; break;
    case PCI_ENDPOINT_TEST_STATUS:       val = s->regs.status; break;
    case PCI_ENDPOINT_TEST_LOWER_SRC_ADDR: val = s->regs.lower_src_addr; break;
    case PCI_ENDPOINT_TEST_UPPER_SRC_ADDR: val = s->regs.upper_src_addr; break;
    case PCI_ENDPOINT_TEST_LOWER_DST_ADDR: val = s->regs.lower_dst_addr; break;
    case PCI_ENDPOINT_TEST_UPPER_DST_ADDR: val = s->regs.upper_dst_addr; break;
    case PCI_ENDPOINT_TEST_SIZE:         val = s->regs.size; break;
    case PCI_ENDPOINT_TEST_CHECKSUM:     val = s->regs.checksum; break;
    case PCI_ENDPOINT_TEST_IRQ_TYPE:     val = s->regs.irq_type; break;
    case PCI_ENDPOINT_TEST_IRQ_NUMBER:   val = s->regs.irq_number; break;
    case PCI_ENDPOINT_TEST_FLAGS:        val = s->regs.flags; break;
    case PCI_ENDPOINT_TEST_CAPS:         val = s->regs.caps; break;
    case PCI_ENDPOINT_TEST_DB_BAR:       val = s->regs.db_bar; break;
    case PCI_ENDPOINT_TEST_DB_OFFSET:    val = s->regs.db_offset; break;
    case PCI_ENDPOINT_TEST_DB_DATA:      val = s->regs.db_data; break;
    default: break;
    }

    return val;
}

/* MMIO write handler for registers */
static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    if (addr >= 0x40) {
        return;
    }

    switch (addr) {
    case PCI_ENDPOINT_TEST_MAGIC:        break; /* read-only */
    case PCI_ENDPOINT_TEST_COMMAND:
        s->regs.command = val;
        pcibase_process_command(s);
        break;
    case PCI_ENDPOINT_TEST_STATUS:
        s->regs.status = val;
        pcibase_update_irq(s);
        break;
    case PCI_ENDPOINT_TEST_LOWER_SRC_ADDR: s->regs.lower_src_addr = val; break;
    case PCI_ENDPOINT_TEST_UPPER_SRC_ADDR: s->regs.upper_src_addr = val; break;
    case PCI_ENDPOINT_TEST_LOWER_DST_ADDR: s->regs.lower_dst_addr = val; break;
    case PCI_ENDPOINT_TEST_UPPER_DST_ADDR: s->regs.upper_dst_addr = val; break;
    case PCI_ENDPOINT_TEST_SIZE:          s->regs.size = val; break;
    case PCI_ENDPOINT_TEST_CHECKSUM:      s->regs.checksum = val; break;
    case PCI_ENDPOINT_TEST_IRQ_TYPE:      s->regs.irq_type = val; break;
    case PCI_ENDPOINT_TEST_IRQ_NUMBER:    s->regs.irq_number = val; break;
    case PCI_ENDPOINT_TEST_FLAGS:         s->regs.flags = val; break;
    case PCI_ENDPOINT_TEST_DB_BAR:        s->regs.db_bar = val; break;
    case PCI_ENDPOINT_TEST_DB_OFFSET:     s->regs.db_offset = val; break;
    case PCI_ENDPOINT_TEST_DB_DATA:       s->regs.db_data = val; break;
    default: break;
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
    s->regs.magic = 0x50434543; /* 'PCEC' */
    s->regs.caps = CAP_INTX; /* Only INTx supported */
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

    /* BAR configuration */
    s->num_bars = 6;
    for (int i = 0; i < 6; i++) {
        s->bar_info[i].index = i;
        if (i == 0) {
            s->bar_info[i].type = BAR_TYPE_MMIO;
            s->bar_info[i].size = 256;
            s->bar_info[i].name = "bar0";
        } else {
            s->bar_info[i].type = BAR_TYPE_RAM;
            s->bar_info[i].size = 64 * 1024;  /* 64KB */
            s->bar_info[i].name = "bar1";    /* dummy, we'll set unique names below */
        }
    }
    /* set names for RAM bars */
    s->bar_info[1].name = "bar1";
    s->bar_info[2].name = "bar2";
    s->bar_info[3].name = "bar3";
    s->bar_info[4].name = "bar4";
    s->bar_info[5].name = "bar5";

    for (int i = 0; i < s->num_bars; i++) {
        pcibase_register_bar(pdev, s, &s->bar_info[i], errp);
    }

    /* MSI/MSI-X not initialized; only INTx supported */
}

static void pcibase_uninit(PCIDevice *pdev)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    /* No MSI/MSI-X to cleanup */
}

static const VMStateDescription vmstate_pcibase = {
    .name = "pci_endpoint_test_pci",
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
