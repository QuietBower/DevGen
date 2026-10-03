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

#define TYPE_PCIBASE_DEVICE "net2280_pci"
typedef struct PCIBaseState PCIBaseState;
OBJECT_DECLARE_SIMPLE_TYPE(PCIBaseState, PCIBASE_DEVICE)

/* Vendor, Device, Class, and Register Offsets */
#define PCI_VENDOR_ID_NET2280 0x17cc
#define PCI_DEVICE_ID_NET2280 0x2280
#define PCI_CLASS_NET2280     0x0c03fe
#define BAR0_SIZE             0x1000

/* net2280_regs offsets */
#define REG_DEVINIT        0x0000
#define REG_CHIPREV        0x0003
#define REG_EECTL          0x0004
#define REG_EECLKFREQ      0x0008
#define REG_SCRATCH        0x000B
#define REG_UNUSED0_0      0x000C
#define REG_PCIIRQENB0     0x0010
#define REG_PCIIRQENB1     0x0014
#define REG_CPU_IRQENB0    0x0018
#define REG_CPU_IRQENB1    0x001C
#define REG_UNUSED1_0      0x0020
#define REG_USBIRQENB1     0x0024
#define REG_IRQSTAT0       0x0028
#define REG_IRQSTAT1       0x002C
#define REG_IDXADDR        0x0030
#define REG_IDXDATA        0x0034
#define REG_FIFOCTL        0x0038
#define REG_UNUSED2_0      0x003C
#define REG_MEMADDR        0x0040
#define REG_MEMDATA0       0x0044
#define REG_MEMDATA1       0x0048
#define REG_UNUSED3_0      0x004C
#define REG_GPIOCTL        0x0050
#define REG_GPIOSTAT       0x0054

/* devinit bits */
#define DEVINIT_LOCAL_CLOCK_FREQUENCY   8
#define DEVINIT_FORCE_PCI_RESET         7
#define DEVINIT_PCI_ID                  6
#define DEVINIT_PCI_ENABLE              5
#define DEVINIT_FIFO_SOFT_RESET         4
#define DEVINIT_CFG_SOFT_RESET          3
#define DEVINIT_PCI_SOFT_RESET          2
#define DEVINIT_USB_SOFT_RESET          1
#define DEVINIT_M8051_RESET             0

/* eectl bits */
#define EECTL_EEPROM_ADDRESS_WIDTH              23
#define EECTL_EEPROM_CHIP_SELECT_ACTIVE         22
#define EECTL_EEPROM_PRESENT                    21
#define EECTL_EEPROM_VALID                      20
#define EECTL_EEPROM_BUSY                       19
#define EECTL_EEPROM_CHIP_SELECT_ENABLE         18
#define EECTL_EEPROM_BYTE_READ_START            17
#define EECTL_EEPROM_BYTE_WRITE_START           16
#define EECTL_EEPROM_READ_DATA                   8
#define EECTL_EEPROM_WRITE_DATA                  0

/* pciirqenb0/1 bits */
#define PCIIRQENB0_SETUP_PACKET_INTERRUPT_ENABLE   7
#define PCIIRQENB0_ENDPOINT_F_INTERRUPT_ENABLE     6
#define PCIIRQENB0_ENDPOINT_E_INTERRUPT_ENABLE     5
#define PCIIRQENB0_ENDPOINT_D_INTERRUPT_ENABLE     4
#define PCIIRQENB0_ENDPOINT_C_INTERRUPT_ENABLE     3
#define PCIIRQENB0_ENDPOINT_B_INTERRUPT_ENABLE     2
#define PCIIRQENB0_ENDPOINT_A_INTERRUPT_ENABLE     1
#define PCIIRQENB0_ENDPOINT_0_INTERRUPT_ENABLE     0

#define PCIIRQENB1_PCI_INTERRUPT_ENABLE                        31
#define PCIIRQENB1_POWER_STATE_CHANGE_INTERRUPT_ENABLE         27
#define PCIIRQENB1_PCI_ARBITER_TIMEOUT_INTERRUPT_ENABLE        26
#define PCIIRQENB1_PCI_PARITY_ERROR_INTERRUPT_ENABLE           25
#define PCIIRQENB1_PCI_MASTER_ABORT_RECEIVED_INTERRUPT_ENABLE  20
#define PCIIRQENB1_PCI_TARGET_ABORT_RECEIVED_INTERRUPT_ENABLE  19
#define PCIIRQENB1_PCI_TARGET_ABORT_ASSERTED_INTERRUPT_ENABLE  18
#define PCIIRQENB1_PCI_RETRY_ABORT_INTERRUPT_ENABLE            17
#define PCIIRQENB1_PCI_MASTER_CYCLE_DONE_INTERRUPT_ENABLE      16
#define PCIIRQENB1_GPIO_INTERRUPT_ENABLE                       13
#define PCIIRQENB1_DMA_D_INTERRUPT_ENABLE                      12
#define PCIIRQENB1_DMA_C_INTERRUPT_ENABLE                      11
#define PCIIRQENB1_DMA_B_INTERRUPT_ENABLE                      10
#define PCIIRQENB1_DMA_A_INTERRUPT_ENABLE                       9
#define PCIIRQENB1_EEPROM_DONE_INTERRUPT_ENABLE                 8
#define PCIIRQENB1_VBUS_INTERRUPT_ENABLE                        7
#define PCIIRQENB1_CONTROL_STATUS_INTERRUPT_ENABLE              6
#define PCIIRQENB1_ROOT_PORT_RESET_INTERRUPT_ENABLE             4
#define PCIIRQENB1_SUSPEND_REQUEST_INTERRUPT_ENABLE             3
#define PCIIRQENB1_SUSPEND_REQUEST_CHANGE_INTERRUPT_ENABLE      2
#define PCIIRQENB1_RESUME_INTERRUPT_ENABLE                      1
#define PCIIRQENB1_SOF_INTERRUPT_ENABLE                         0

/* cpu_irqenb0 bits (same as pciirqenb0) */
#define CPU_IRQENB0_SETUP_PACKET_INTERRUPT_ENABLE   7
#define CPU_IRQENB0_ENDPOINT_F_INTERRUPT_ENABLE     6
#define CPU_IRQENB0_ENDPOINT_E_INTERRUPT_ENABLE     5
#define CPU_IRQENB0_ENDPOINT_D_INTERRUPT_ENABLE     4
#define CPU_IRQENB0_ENDPOINT_C_INTERRUPT_ENABLE     3
#define CPU_IRQENB0_ENDPOINT_B_INTERRUPT_ENABLE     2
#define CPU_IRQENB0_ENDPOINT_A_INTERRUPT_ENABLE     1
#define CPU_IRQENB0_ENDPOINT_0_INTERRUPT_ENABLE     0

/* cpu_irqenb1 bits */
#define CPU_IRQENB1_CPU_INTERRUPT_ENABLE                    31
#define CPU_IRQENB1_POWER_STATE_CHANGE_INTERRUPT_ENABLE     27
#define CPU_IRQENB1_PCI_ARBITER_TIMEOUT_INTERRUPT_ENABLE    26
#define CPU_IRQENB1_PCI_PARITY_ERROR_INTERRUPT_ENABLE       25
#define CPU_IRQENB1_PCI_INTA_INTERRUPT_ENABLE               24
#define CPU_IRQENB1_PCI_PME_INTERRUPT_ENABLE                23
#define CPU_IRQENB1_PCI_SERR_INTERRUPT_ENABLE               22
#define CPU_IRQENB1_PCI_PERR_INTERRUPT_ENABLE               21
#define CPU_IRQENB1_PCI_MASTER_ABORT_RECEIVED_INTERRUPT_ENABLE  20
#define CPU_IRQENB1_PCI_TARGET_ABORT_RECEIVED_INTERRUPT_ENABLE  19
#define CPU_IRQENB1_PCI_RETRY_ABORT_INTERRUPT_ENABLE        17
#define CPU_IRQENB1_PCI_MASTER_CYCLE_DONE_INTERRUPT_ENABLE  16
#define CPU_IRQENB1_GPIO_INTERRUPT_ENABLE                   13
#define CPU_IRQENB1_DMA_D_INTERRUPT_ENABLE                  12
#define CPU_IRQENB1_DMA_C_INTERRUPT_ENABLE                  11
#define CPU_IRQENB1_DMA_B_INTERRUPT_ENABLE                  10
#define CPU_IRQENB1_DMA_A_INTERRUPT_ENABLE                   9
#define CPU_IRQENB1_EEPROM_DONE_INTERRUPT_ENABLE             8
#define CPU_IRQENB1_VBUS_INTERRUPT_ENABLE                    7
#define CPU_IRQENB1_CONTROL_STATUS_INTERRUPT_ENABLE          6
#define CPU_IRQENB1_ROOT_PORT_RESET_INTERRUPT_ENABLE         4
#define CPU_IRQENB1_SUSPEND_REQUEST_INTERRUPT_ENABLE         3
#define CPU_IRQENB1_SUSPEND_REQUEST_CHANGE_INTERRUPT_ENABLE  2
#define CPU_IRQENB1_RESUME_INTERRUPT_ENABLE                  1
#define CPU_IRQENB1_SOF_INTERRUPT_ENABLE                     0

/* usbirqenb1 bits (same as cpu_irqenb1) */
#define USBIRQENB1_USB_INTERRUPT_ENABLE                    31
#define USBIRQENB1_POWER_STATE_CHANGE_INTERRUPT_ENABLE     27
#define USBIRQENB1_PCI_ARBITER_TIMEOUT_INTERRUPT_ENABLE    26
#define USBIRQENB1_PCI_PARITY_ERROR_INTERRUPT_ENABLE       25
#define USBIRQENB1_PCI_INTA_INTERRUPT_ENABLE               24
#define USBIRQENB1_PCI_PME_INTERRUPT_ENABLE                23
#define USBIRQENB1_PCI_SERR_INTERRUPT_ENABLE               22
#define USBIRQENB1_PCI_PERR_INTERRUPT_ENABLE               21
#define USBIRQENB1_PCI_MASTER_ABORT_RECEIVED_INTERRUPT_ENABLE  20
#define USBIRQENB1_PCI_TARGET_ABORT_RECEIVED_INTERRUPT_ENABLE  19
#define USBIRQENB1_PCI_RETRY_ABORT_INTERRUPT_ENABLE        17
#define USBIRQENB1_PCI_MASTER_CYCLE_DONE_INTERRUPT_ENABLE  16
#define USBIRQENB1_GPIO_INTERRUPT_ENABLE                   13
#define USBIRQENB1_DMA_D_INTERRUPT_ENABLE                  12
#define USBIRQENB1_DMA_C_INTERRUPT_ENABLE                  11
#define USBIRQENB1_DMA_B_INTERRUPT_ENABLE                  10
#define USBIRQENB1_DMA_A_INTERRUPT_ENABLE                   9
#define USBIRQENB1_EEPROM_DONE_INTERRUPT_ENABLE             8
#define USBIRQENB1_VBUS_INTERRUPT_ENABLE                    7
#define USBIRQENB1_CONTROL_STATUS_INTERRUPT_ENABLE          6
#define USBIRQENB1_ROOT_PORT_RESET_INTERRUPT_ENABLE         4
#define USBIRQENB1_SUSPEND_REQUEST_INTERRUPT_ENABLE         3
#define USBIRQENB1_SUSPEND_REQUEST_CHANGE_INTERRUPT_ENABLE  2
#define USBIRQENB1_RESUME_INTERRUPT_ENABLE                  1
#define USBIRQENB1_SOF_INTERRUPT_ENABLE                     0

/* irqstat0 bits */
#define IRQSTAT0_INTA_ASSERTED               12
#define IRQSTAT0_SETUP_PACKET_INTERRUPT       7
#define IRQSTAT0_ENDPOINT_F_INTERRUPT         6
#define IRQSTAT0_ENDPOINT_E_INTERRUPT         5
#define IRQSTAT0_ENDPOINT_D_INTERRUPT         4
#define IRQSTAT0_ENDPOINT_C_INTERRUPT         3
#define IRQSTAT0_ENDPOINT_B_INTERRUPT         2
#define IRQSTAT0_ENDPOINT_A_INTERRUPT         1
#define IRQSTAT0_ENDPOINT_0_INTERRUPT         0

/* irqstat1 bits */
#define IRQSTAT1_POWER_STATE_CHANGE_INTERRUPT                 27
#define IRQSTAT1_PCI_ARBITER_TIMEOUT_INTERRUPT                26
#define IRQSTAT1_PCI_PARITY_ERROR_INTERRUPT                   25
#define IRQSTAT1_PCI_INTA_INTERRUPT                           24
#define IRQSTAT1_PCI_PME_INTERRUPT                            23
#define IRQSTAT1_PCI_SERR_INTERRUPT                           22
#define IRQSTAT1_PCI_PERR_INTERRUPT                           21
#define IRQSTAT1_PCI_MASTER_ABORT_RECEIVED_INTERRUPT          20
#define IRQSTAT1_PCI_TARGET_ABORT_RECEIVED_INTERRUPT          19
#define IRQSTAT1_PCI_RETRY_ABORT_INTERRUPT                    17
#define IRQSTAT1_PCI_MASTER_CYCLE_DONE_INTERRUPT              16
#define IRQSTAT1_SOF_DOWN_INTERRUPT                           14
#define IRQSTAT1_GPIO_INTERRUPT                               13
#define IRQSTAT1_DMA_D_INTERRUPT                              12
#define IRQSTAT1_DMA_C_INTERRUPT                              11
#define IRQSTAT1_DMA_B_INTERRUPT                              10
#define IRQSTAT1_DMA_A_INTERRUPT                               9
#define IRQSTAT1_EEPROM_DONE_INTERRUPT                         8
#define IRQSTAT1_VBUS_INTERRUPT                                7
#define IRQSTAT1_CONTROL_STATUS_INTERRUPT                      6
#define IRQSTAT1_ROOT_PORT_RESET_INTERRUPT                     4
#define IRQSTAT1_SUSPEND_REQUEST_INTERRUPT                     3
#define IRQSTAT1_SUSPEND_REQUEST_CHANGE_INTERRUPT              2
#define IRQSTAT1_RESUME_INTERRUPT                              1
#define IRQSTAT1_SOF_INTERRUPT                                 0

/* fifoctl bits */
#define FIFOCTL_PCI_BASE2_RANGE             16
#define FIFOCTL_IGNORE_FIFO_AVAILABILITY     3
#define FIFOCTL_PCI_BASE2_SELECT             2
#define FIFOCTL_FIFO_CONFIGURATION_SELECT    0

/* memaddr bits */
#define MEMADDR_START                   28
#define MEMADDR_DIRECTION               27
#define MEMADDR_FIFO_DIAGNOSTIC_SELECT  24
#define MEMADDR_MEMORY_ADDRESS           0

/* gpioctl bits */
#define GPIOCTL_GPIO3_LED_SELECT       12
#define GPIOCTL_GPIO3_INTERRUPT_ENABLE 11
#define GPIOCTL_GPIO2_INTERRUPT_ENABLE 10
#define GPIOCTL_GPIO1_INTERRUPT_ENABLE  9
#define GPIOCTL_GPIO0_INTERRUPT_ENABLE  8
#define GPIOCTL_GPIO3_OUTPUT_ENABLE     7
#define GPIOCTL_GPIO2_OUTPUT_ENABLE     6
#define GPIOCTL_GPIO1_OUTPUT_ENABLE     5
#define GPIOCTL_GPIO0_OUTPUT_ENABLE     4
#define GPIOCTL_GPIO3_DATA              3
#define GPIOCTL_GPIO2_DATA              2
#define GPIOCTL_GPIO1_DATA              1
#define GPIOCTL_GPIO0_DATA              0

/* gpiostat bits */
#define GPIOSTAT_GPIO3_INTERRUPT  3
#define GPIOSTAT_GPIO2_INTERRUPT  2
#define GPIOSTAT_GPIO1_INTERRUPT  1
#define GPIOSTAT_GPIO0_INTERRUPT  0

/* net2280_usb_regs offsets (base 0x0080) */
#define REG_STDRSP         0x0080
#define REG_PRODVENDID     0x0084
#define REG_RELNUM         0x0088
#define REG_USBCTL         0x008C
#define REG_USBSTAT        0x0090
#define REG_XCVRDIAG       0x0094
#define REG_SETUP0123      0x0098
#define REG_SETUP4567      0x009C
#define REG_UNUSED_USB0    0x00A0
#define REG_OURADDR        0x00A4
#define REG_OURCONFIG      0x00A8

/* stdrsp bits */
#define STDRSP_STALL_UNSUPPORTED_REQUESTS          31
#define STDRSP_SET_TEST_MODE                       16
#define STDRSP_GET_OTHER_SPEED_CONFIGURATION       15
#define STDRSP_GET_DEVICE_QUALIFIER                14
#define STDRSP_SET_ADDRESS                         13
#define STDRSP_ENDPOINT_SET_CLEAR_HALT             12
#define STDRSP_DEVICE_SET_CLEAR_DEVICE_REMOTE_WAKEUP 11
#define STDRSP_GET_STRING_DESCRIPTOR_2             10
#define STDRSP_GET_STRING_DESCRIPTOR_1              9
#define STDRSP_GET_STRING_DESCRIPTOR_0              8
#define STDRSP_GET_SET_INTERFACE                    6
#define STDRSP_GET_SET_CONFIGURATION                5
#define STDRSP_GET_CONFIGURATION_DESCRIPTOR         4
#define STDRSP_GET_DEVICE_DESCRIPTOR                3
#define STDRSP_GET_ENDPOINT_STATUS                  2
#define STDRSP_GET_INTERFACE_STATUS                 1
#define STDRSP_GET_DEVICE_STATUS                    0

/* prodvendid bits */
#define PRODVENDID_PRODUCT_ID  16
#define PRODVENDID_VENDOR_ID    0

/* usbctl bits */
#define USBCTL_SERIAL_NUMBER_INDEX              16
#define USBCTL_PRODUCT_ID_STRING_ENABLE         13
#define USBCTL_VENDOR_ID_STRING_ENABLE          12
#define USBCTL_USB_ROOT_PORT_WAKEUP_ENABLE      11
#define USBCTL_VBUS_PIN                         10
#define USBCTL_TIMED_DISCONNECT                  9
#define USBCTL_SUSPEND_IMMEDIATELY               7
#define USBCTL_SELF_POWERED_USB_DEVICE           6
#define USBCTL_REMOTE_WAKEUP_SUPPORT             5
#define USBCTL_PME_POLARITY                      4
#define USBCTL_USB_DETECT_ENABLE                 3
#define USBCTL_PME_WAKEUP_ENABLE                 2
#define USBCTL_DEVICE_REMOTE_WAKEUP_ENABLE       1
#define USBCTL_SELF_POWERED_STATUS               0

/* usbstat bits */
#define USBSTAT_HIGH_SPEED                      7
#define USBSTAT_FULL_SPEED                      6
#define USBSTAT_GENERATE_RESUME                 5
#define USBSTAT_GENERATE_DEVICE_REMOTE_WAKEUP   4

/* xcvrdiag bits */
#define XCVRDIAG_FORCE_HIGH_SPEED_MODE      31
#define XCVRDIAG_FORCE_FULL_SPEED_MODE      30
#define XCVRDIAG_USB_TEST_MODE              24
#define XCVRDIAG_LINE_STATE                 16
#define XCVRDIAG_TRANSCEIVER_OPERATION_MODE  2
#define XCVRDIAG_TRANSCEIVER_SELECT          1
#define XCVRDIAG_TERMINATION_SELECT          0

/* ouraddr bits */
#define OURADDR_FORCE_IMMEDIATE   7
#define OURADDR_OUR_USB_ADDRESS   0

/* usb338x_usb_ext_regs (base 0x00B4) */
#define REG_USBCLASS       0x00B4
#define REG_SS_SEL         0x00B8
#define REG_SS_DEL         0x00BC
#define REG_USB2LPM        0x00C0
#define REG_USB3BELT       0x00C4
#define REG_USBCTL2        0x00C8
#define REG_IN_TIMEOUT     0x00CC
#define REG_ISODELAY       0x00D0

/* usbclass bits */
#define USBCLASS_DEVICE_PROTOCOL  16
#define USBCLASS_DEVICE_SUB_CLASS  8
#define USBCLASS_DEVICE_CLASS      0

/* ss_sel bits */
#define SS_SEL_U2_SYSTEM_EXIT_LATENCY  8
#define SS_SEL_U1_SYSTEM_EXIT_LATENCY  0

/* ss_del bits */
#define SS_DEL_U2_DEVICE_EXIT_LATENCY  8
#define SS_DEL_U1_DEVICE_EXIT_LATENCY  0

/* usb2lpm bits */
#define USB2LPM_USB_L1_LPM_HIRD         2
#define USB2LPM_USB_L1_LPM_REMOTE_WAKE  1
#define USB2LPM_USB_L1_LPM_SUPPORT      0

/* usb3belt bits */
#define USB3BELT_BELT_MULTIPLIER            10
#define USB3BELT_BEST_EFFORT_LATENCY_TOLERANCE 0

/* usbctl2 bits */
#define USBCTL2_LTM_ENABLE              7
#define USBCTL2_U2_ENABLE               6
#define USBCTL2_U1_ENABLE               5
#define USBCTL2_FUNCTION_SUSPEND        4
#define USBCTL2_USB3_CORE_ENABLE         3
#define USBCTL2_USB2_CORE_ENABLE         2
#define USBCTL2_SERIAL_NUMBER_STRING_ENABLE 0

/* in_timeout bits */
#define IN_TIMEOUT_GPEP3_TIMEOUT         19
#define IN_TIMEOUT_GPEP2_TIMEOUT         18
#define IN_TIMEOUT_GPEP1_TIMEOUT         17
#define IN_TIMEOUT_GPEP0_TIMEOUT         16
#define IN_TIMEOUT_GPEP3_TIMEOUT_VALUE   13
#define IN_TIMEOUT_GPEP3_TIMEOUT_ENABLE  12
#define IN_TIMEOUT_GPEP2_TIMEOUT_VALUE    9
#define IN_TIMEOUT_GPEP2_TIMEOUT_ENABLE   8
#define IN_TIMEOUT_GPEP1_TIMEOUT_VALUE    5
#define IN_TIMEOUT_GPEP1_TIMEOUT_ENABLE   4
#define IN_TIMEOUT_GPEP0_TIMEOUT_VALUE    1
#define IN_TIMEOUT_GPEP0_TIMEOUT_ENABLE   0

/* isodelay bits */
#define ISODELAY_ISOCHRONOUS_DELAY  0

/* net2280_pci_regs offsets (base 0x0100) */
#define REG_PCIMSTCTL      0x0100
#define REG_PCIMSTADDR     0x0104
#define REG_PCIMSTDATA     0x0108
#define REG_PCIMSTSTAT     0x010C

/* pcimstctl bits */
#define PCIMSTCTL_PCI_ARBITER_PARK_SELECT                  13
#define PCIMSTCTL_PCI_MULTI_LEVEL_ARBITER                  12
#define PCIMSTCTL_PCI_RETRY_ABORT_ENABLE                   11
#define PCIMSTCTL_DMA_MEMORY_WRITE_AND_INVALIDATE_ENABLE   10
#define PCIMSTCTL_DMA_READ_MULTIPLE_ENABLE                  9
#define PCIMSTCTL_DMA_READ_LINE_ENABLE                      8
#define PCIMSTCTL_PCI_MASTER_COMMAND_SELECT                 6
#define PCIMSTCTL_PCI_MASTER_START                          5
#define PCIMSTCTL_PCI_MASTER_READ_WRITE                     4
#define PCIMSTCTL_PCI_MASTER_BYTE_WRITE_ENABLES             0

/* pcimststat bits */
#define PCIMSTSTAT_PCI_ARBITER_CLEAR   2
#define PCIMSTSTAT_PCI_EXTERNAL_ARBITER 1
#define PCIMSTSTAT_PCI_HOST_MODE        0

/* net2280_dma_regs offsets (per channel, base 0x0180, 0x01A0, 0x01C0, 0x01E0) */
#define DMA_CHANNEL_OFFSET  0x20
#define DMA_CHANNEL_BASE    0x0180
#define REG_DMACTL(base)    (base + 0x00)
#define REG_DMASTAT(base)   (base + 0x04)
#define REG_DMAUNUSED0(base) (base + 0x08)
#define REG_DMAUNUSED1(base) (base + 0x0C)
#define REG_DMACOUNT(base)  (base + 0x10)
#define REG_DMAADDR(base)   (base + 0x14)
#define REG_DMADESC(base)   (base + 0x18)
#define REG_DMAUNUSED2(base) (base + 0x1C)

/* dmactl bits */
#define DMACTL_DMA_SCATTER_GATHER_DONE_INTERRUPT_ENABLE  25
#define DMACTL_DMA_CLEAR_COUNT_ENABLE                    21
#define DMACTL_DESCRIPTOR_POLLING_RATE                   19
#define DMACTL_DMA_VALID_BIT_POLLING_ENABLE              18
#define DMACTL_DMA_VALID_BIT_ENABLE                      17
#define DMACTL_DMA_SCATTER_GATHER_ENABLE                 16
#define DMACTL_DMA_OUT_AUTO_START_ENABLE                 4
#define DMACTL_DMA_PREEMPT_ENABLE                        3
#define DMACTL_DMA_FIFO_VALIDATE                         2
#define DMACTL_DMA_ENABLE                                1
#define DMACTL_DMA_ADDRESS_HOLD                          0

/* dmastat bits */
#define DMASTAT_DMA_ABORT_DONE_INTERRUPT           27
#define DMASTAT_DMA_SCATTER_GATHER_DONE_INTERRUPT  25
#define DMASTAT_DMA_TRANSACTION_DONE_INTERRUPT     24
#define DMASTAT_DMA_ABORT                           1
#define DMASTAT_DMA_START                           0

/* dmacount bits */
#define DMACOUNT_VALID_BIT                     31
#define DMACOUNT_DMA_DIRECTION                 30
#define DMACOUNT_DMA_DONE_INTERRUPT_ENABLE     29
#define DMACOUNT_END_OF_CHAIN                  28
#define DMACOUNT_DMA_BYTE_COUNT_MASK           ((1<<24)-1)
#define DMACOUNT_DMA_BYTE_COUNT                0

/* net2280_dep_regs (per endpoint, base 0x0200, 0x0210, 0x0220, 0x0230, 0x0240, 0x0250) */
#define DEP_CHANNEL_OFFSET  0x10
#define DEP_BASE            0x0200
#define REG_DEP_CFG(base)   (base)
#define REG_DEP_RSP(base)   (base + 4)

/* net2280_ep_regs (per endpoint, base 0x0300, 0x0320, ... 0x04C0) */
#define EP_CHANNEL_OFFSET   0x20
#define EP_BASE             0x0300
#define REG_EP_CFG(base)    (base + 0x00)
#define REG_EP_RSP(base)    (base + 0x04)
#define REG_EP_IRQENB(base) (base + 0x08)
#define REG_EP_STAT(base)   (base + 0x0C)
#define REG_EP_AVAIL(base)  (base + 0x10)
#define REG_EP_DATA(base)   (base + 0x14)

/* ep_cfg bits */
#define EP_CFG_ENDPOINT_BYTE_COUNT  16
#define EP_CFG_ENDPOINT_ENABLE      10
#define EP_CFG_ENDPOINT_TYPE         8
#define EP_CFG_ENDPOINT_DIRECTION    7
#define EP_CFG_ENDPOINT_NUMBER       0

/* ep_rsp bits */
#define EP_RSP_SET_NAK_OUT_PACKETS              15
#define EP_RSP_SET_EP_HIDE_STATUS_PHASE         14
#define EP_RSP_SET_EP_FORCE_CRC_ERROR           13
#define EP_RSP_SET_INTERRUPT_MODE               12
#define EP_RSP_SET_CONTROL_STATUS_PHASE_HANDSHAKE 11
#define EP_RSP_SET_NAK_OUT_PACKETS_MODE         10
#define EP_RSP_SET_ENDPOINT_TOGGLE               9
#define EP_RSP_SET_ENDPOINT_HALT                 8
#define EP_RSP_CLEAR_NAK_OUT_PACKETS             7
#define EP_RSP_CLEAR_EP_HIDE_STATUS_PHASE        6
#define EP_RSP_CLEAR_EP_FORCE_CRC_ERROR          5
#define EP_RSP_CLEAR_INTERRUPT_MODE              4
#define EP_RSP_CLEAR_CONTROL_STATUS_PHASE_HANDSHAKE 3
#define EP_RSP_CLEAR_NAK_OUT_PACKETS_MODE        2
#define EP_RSP_CLEAR_ENDPOINT_TOGGLE             1
#define EP_RSP_CLEAR_ENDPOINT_HALT               0

/* ep_irqenb bits */
#define EP_IRQENB_SHORT_PACKET_OUT_DONE_INTERRUPT_ENABLE          6
#define EP_IRQENB_SHORT_PACKET_TRANSFERRED_INTERRUPT_ENABLE       5
#define EP_IRQENB_DATA_PACKET_RECEIVED_INTERRUPT_ENABLE           3
#define EP_IRQENB_DATA_PACKET_TRANSMITTED_INTERRUPT_ENABLE        2
#define EP_IRQENB_DATA_OUT_PING_TOKEN_INTERRUPT_ENABLE            1
#define EP_IRQENB_DATA_IN_TOKEN_INTERRUPT_ENABLE                  0

/* ep_stat bits */
#define EP_STAT_FIFO_VALID_COUNT                    24
#define EP_STAT_HIGH_BANDWIDTH_OUT_TRANSACTION_PID   22
#define EP_STAT_TIMEOUT                              21
#define EP_STAT_USB_STALL_SENT                       20
#define EP_STAT_USB_IN_NAK_SENT                      19
#define EP_STAT_USB_IN_ACK_RCVD                      18
#define EP_STAT_USB_OUT_PING_NAK_SENT                17
#define EP_STAT_USB_OUT_ACK_SENT                     16
#define EP_STAT_FIFO_OVERFLOW                        13
#define EP_STAT_FIFO_UNDERFLOW                       12
#define EP_STAT_FIFO_FULL                            11
#define EP_STAT_FIFO_EMPTY                           10
#define EP_STAT_FIFO_FLUSH                            9
#define EP_STAT_SHORT_PACKET_OUT_DONE_INTERRUPT       6
#define EP_STAT_SHORT_PACKET_TRANSFERRED_INTERRUPT    5
#define EP_STAT_NAK_OUT_PACKETS                       4
#define EP_STAT_DATA_PACKET_RECEIVED_INTERRUPT        3
#define EP_STAT_DATA_PACKET_TRANSMITTED_INTERRUPT     2
#define EP_STAT_DATA_OUT_PING_TOKEN_INTERRUPT         1
#define EP_STAT_DATA_IN_TOKEN_INTERRUPT               0

/* usb338x_ll_regs offsets (base 0x0700) */
#define REG_LL_LTSSM_CTRL1    0x0700
#define REG_LL_LTSSM_CTRL2    0x0704
#define REG_LL_LTSSM_CTRL3    0x0708
#define REG_LL_UNUSED1        0x070C
#define REG_LL_UNUSED2        0x0710
#define REG_LL_GENERAL_CTRL0  0x0714
#define REG_LL_GENERAL_CTRL1  0x0718
#define REG_LL_GENERAL_CTRL2  0x071C
#define REG_LL_GENERAL_CTRL3  0x0720
#define REG_LL_GENERAL_CTRL4  0x0724
#define REG_LL_ERROR_GEN      0x0728
#define REG_LL_LFPS_5         0x0748
#define REG_LL_LFPS_6         0x074C
#define REG_LL_TSN_COUNTERS_2 0x077C
#define REG_LL_TSN_COUNTERS_3 0x0780
#define REG_LL_LFPS_TIMERS_2  0x0794
#define REG_LL_TSN_CHICKEN_BIT 0x079C

/* ll_general_ctrl1 bits */
#define LL_GENERAL_CTRL1_PM_U3_AUTO_EXIT           29
#define LL_GENERAL_CTRL1_PM_U2_AUTO_EXIT           28
#define LL_GENERAL_CTRL1_PM_U1_AUTO_EXIT           27
#define LL_GENERAL_CTRL1_PM_FORCE_U2_ENTRY         26
#define LL_GENERAL_CTRL1_PM_FORCE_U1_ENTRY         25
#define LL_GENERAL_CTRL1_PM_LGO_COLLISION_SEND_LAU 24
#define LL_GENERAL_CTRL1_PM_DIR_LINK_REJECT        23
#define LL_GENERAL_CTRL1_PM_FORCE_LINK_ACCEPT      22
#define LL_GENERAL_CTRL1_PM_DIR_ENTRY_U3           20
#define LL_GENERAL_CTRL1_PM_DIR_ENTRY_U2           19
#define LL_GENERAL_CTRL1_PM_DIR_ENTRY_U1           18
#define LL_GENERAL_CTRL1_PM_U2_ENABLE              17
#define LL_GENERAL_CTRL1_PM_U1_ENABLE              16
#define LL_GENERAL_CTRL1_SKP_THRESHOLD_ADJUST_FMW   8
#define LL_GENERAL_CTRL1_RESEND_DPP_ON_LRTY_FMW     7
#define LL_GENERAL_CTRL1_DL_BIT_VALUE_FMW           6
#define LL_GENERAL_CTRL1_FORCE_DL_BIT               5

/* ll_general_ctrl2 bits */
#define LL_GENERAL_CTRL2_SELECT_INVERT_LANE_POLARITY  7
#define LL_GENERAL_CTRL2_FORCE_INVERT_LANE_POLARITY   6

/* ll_lfps_5 bits */
#define LL_LFPS_5_TIMER_LFPS_6US  16

/* ll_lfps_6 bits */
#define LL_LFPS_6_TIMER_LFPS_80US  0

/* ll_tsn_counters_2 bits */
#define LL_TSN_COUNTERS_2_HOT_TX_NORESET_TS2  24

/* ll_tsn_counters_3 bits */
#define LL_TSN_COUNTERS_3_HOT_RX_RESET_TS2     0

/* ll_lfps_timers_2 value */
#define LFPS_TIMERS_2_WORKAROUND_VALUE  0x084d

/* ll_tsn_chicken_bit bits */
#define LL_TSN_CHICKEN_BIT_RECOVERY_IDLE_TO_RECOVER_FMW  3

/* usb338x_pl_regs offsets (base 0x0800) */
#define REG_PL_REG_1          0x0800
#define REG_PL_REG_2          0x0804
#define REG_PL_REG_3          0x0808
#define REG_PL_REG_4          0x080C
#define REG_PL_EP_CTRL        0x0810
#define REG_PL_REG_6          0x0814
#define REG_PL_REG_7          0x0818
#define REG_PL_REG_8          0x081C
#define REG_PL_EP_STATUS_1    0x0820
#define REG_PL_EP_STATUS_2    0x0824
#define REG_PL_EP_STATUS_3    0x0828
#define REG_PL_EP_STATUS_4    0x082C
#define REG_PL_EP_CFG_4       0x0830

/* pl_ep_ctrl bits */
#define PL_EP_CTRL_ENDPOINT_SELECT          0
#define PL_EP_CTRL_EP_INITIALIZED          16
#define PL_EP_CTRL_SEQUENCE_NUMBER_RESET   17
#define PL_EP_CTRL_CLEAR_ACK_ERROR_CODE    20

/* pl_ep_status_1 bits */
#define PL_EP_STATUS_1_STATE               16
#define PL_EP_STATUS_1_ACK_GOOD_NORMAL     0x11
#define PL_EP_STATUS_1_ACK_GOOD_MORE_ACKS_TO_COME  0x16

/* pl_ep_status_3 bits */
#define PL_EP_STATUS_3_SEQUENCE_NUMBER      0

/* pl_ep_cfg_4 bits */
#define PL_EP_CFG_4_NON_CTRL_IN_TOLERATE_BAD_DIR  6

/* Additional driver constants */
#define DEFECT7374_FSM_FIELD    28
#define DEFECT7374_FSM_NON_SS_CONTROL_READ (2 << DEFECT7374_FSM_FIELD)
#define DEFECT7374_FSM_SS_CONTROL_READ (3 << DEFECT7374_FSM_FIELD)
#define DEFECT_7374_NUMBEROF_MAX_WAIT_LOOPS         200
#define DEFECT_7374_PROCESSOR_WAIT_TIME             10

struct PCIBaseState {
    PCIDevice parent_obj;
    MemoryRegion bar0;
    uint8_t regs[BAR0_SIZE]; /* all registers */
    uint32_t idx_regs[256]; /* indexed registers */
    uint32_t idxaddr;
};

static void net2280_update_irq(PCIBaseState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint32_t irqstat0 = ldl_le_p(s->regs + REG_IRQSTAT0);
    uint32_t irqstat1 = ldl_le_p(s->regs + REG_IRQSTAT1);
    uint32_t pciirqenb0 = ldl_le_p(s->regs + REG_PCIIRQENB0);
    uint32_t pciirqenb1 = ldl_le_p(s->regs + REG_PCIIRQENB1);
    uint32_t active = (irqstat0 & pciirqenb0) | (irqstat1 & pciirqenb1);

    /* Update INTA_ASSERTED bit in irqstat0 */
    if (active) {
        irqstat0 |= BIT(IRQSTAT0_INTA_ASSERTED);
    } else {
        irqstat0 &= ~BIT(IRQSTAT0_INTA_ASSERTED);
    }
    stl_le_p(s->regs + REG_IRQSTAT0, irqstat0);

    if (active) {
        if (msi_enabled(pdev)) {
            msi_notify(pdev, 0);
        } else {
            pci_set_irq(pdev, 1);
        }
    } else {
        if (!msi_enabled(pdev)) {
            pci_set_irq(pdev, 0);
        }
    }
}

static void reg_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (size) {
    case 1:
        s->regs[addr] = val;
        break;
    case 2:
        stw_le_p(s->regs + addr, val);
        break;
    case 4:
        stl_le_p(s->regs + addr, val);
        break;
    case 8:
        stq_le_p(s->regs + addr, val);
        break;
    }
}

static uint64_t reg_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;
    switch (size) {
    case 1:
        return s->regs[addr];
    case 2:
        return lduw_le_p(s->regs + addr);
    case 4:
        return ldl_le_p(s->regs + addr);
    case 8:
        return ldq_le_p(s->regs + addr);
    }
    return 0;
}

static uint64_t pcibase_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Handle indexed register access */
    if (addr == REG_IDXDATA && size == 4) {
        return s->idx_regs[s->idxaddr & 0xFF];
    }
    if (addr == REG_IDXADDR && size == 4) {
        return s->idxaddr;
    }
    /* irqstat0/1 just read directly */
    return reg_read(s, addr, size);
}

static void pcibase_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    PCIBaseState *s = opaque;

    /* Handle indexed register writes */
    if (addr == REG_IDXADDR && size == 4) {
        s->idxaddr = val;
        return;
    }
    if (addr == REG_IDXDATA && size == 4) {
        s->idx_regs[s->idxaddr & 0xFF] = val;
        return;
    }

    /* Handle irqstat0/1 W1C */
    if (addr == REG_IRQSTAT0 && size == 4) {
        uint32_t cur = ldl_le_p(s->regs + addr);
        cur &= ~((uint32_t)val);
        stl_le_p(s->regs + addr, cur);
        net2280_update_irq(s);
        return;
    }
    if (addr == REG_IRQSTAT1 && size == 4) {
        uint32_t cur = ldl_le_p(s->regs + addr);
        cur &= ~((uint32_t)val);
        stl_le_p(s->regs + addr, cur);
        net2280_update_irq(s);
        return;
    }

    /* pciirqenb0/1: call update_irq after write */
    if ((addr == REG_PCIIRQENB0 || addr == REG_PCIIRQENB1) && size == 4) {
        stl_le_p(s->regs + addr, val);
        net2280_update_irq(s);
        return;
    }

    /* DMA channel dmastat at offsets 0x184, 0x1A4, 0x1C4, 0x1E4: W1C */
    if ((addr == 0x184 || addr == 0x1A4 || addr == 0x1C4 || addr == 0x1E4) && size == 4) {
        uint32_t cur = ldl_le_p(s->regs + addr);
        cur &= ~((uint32_t)val);
        stl_le_p(s->regs + addr, cur);
        return;
    }

    /* EP status registers at each EP base+0x0C: W1C */
    if ((addr >= 0x030C && addr <= 0x04EC) && (addr & 0x1F) == 0x0C && size == 4) {
        uint32_t cur = ldl_le_p(s->regs + addr);
        cur &= ~((uint32_t)val);
        stl_le_p(s->regs + addr, cur);
        return;
    }

    /* Default: normal write */
    reg_write(s, addr, val, size);
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
    memset(s->regs, 0, BAR0_SIZE);
    memset(s->idx_regs, 0, sizeof(s->idx_regs));
    s->idx_regs[REG_CHIPREV] = 0x0100; /* some revision */
    s->idxaddr = 0;
}

static void pcibase_realize(PCIDevice *pdev, Error **errp)
{
    PCIBaseState *s = PCIBASE_DEVICE(pdev);
    uint8_t *pci_conf = pdev->config;

    pci_set_word(pci_conf + PCI_VENDOR_ID, PCI_VENDOR_ID_NET2280);
    pci_set_word(pci_conf + PCI_DEVICE_ID, PCI_DEVICE_ID_NET2280);
    pci_set_word(pci_conf + PCI_CLASS_DEVICE, PCI_CLASS_NET2280);
    pci_set_byte(pci_conf + PCI_REVISION_ID, 0x01);
    pci_config_set_interrupt_pin(pci_conf, 1);

    pdev->cap_present |= QEMU_PCI_CAP_EXPRESS;
    pcie_endpoint_cap_init(pdev, 0x80);
    int pm_pos = pci_add_capability(pdev, PCI_CAP_ID_PM, 0, PCI_PM_SIZEOF, errp);
    if (pm_pos > 0) {
        pci_set_word(pci_conf + pm_pos + PCI_PM_PMC, 0x0003);
    }

    /* MSI support */
    if (msi_init(pdev, 0, 1, true, false, errp) < 0) {
        /* not fatal; driver will fall back to INTx */
    }

    memory_region_init_io(&s->bar0, OBJECT(s), &pcibase_mmio_ops, s, "net2280-mmio", BAR0_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->bar0);

    /* Initialize indexed register: chiprev */
    s->idx_regs[REG_CHIPREV] = 0x0100;
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
    .name = "net2280_pci",
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
