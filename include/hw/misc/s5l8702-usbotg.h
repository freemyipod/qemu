#ifndef HW_MISC_S5L8702_USBOTG_H
#define HW_MISC_S5L8702_USBOTG_H

#include "qom/object.h"
#include "hw/sysbus.h"

#define TYPE_S5L8702_USBOTG    "s5l8702-usbotg"
OBJECT_DECLARE_SIMPLE_TYPE(S5L8702UsbOtgState, S5L8702_USBOTG)

#define S5L8702_USBOTG_BASE    0x38400000
#define S5L8702_USBOTG_SIZE    0x00001000

/* Register offsets (Synopsys DesignWare USB OTG Core) */
#define GOTGCTL         0x0
#define GOTGINT         0x4
#define GAHBCFG         0x8
#define GUSBCFG         0xC
#define GRSTCTL         0x10
#define GINTSTS         0x14
#define GINTMSK         0x18
#define GRXSTSR         0x1C
#define GRXSTSP         0x20
#define GRXFSIZ         0x24
#define GNPTXFSIZ       0x28
#define GNPTXFSTS       0x2C
#define GHWCFG1         0x44
#define GHWCFG2         0x48
#define GHWCFG3         0x4C
#define GHWCFG4         0x50
#define DIEPTXF(x)      (0x100 + (4*(x)))
#define PCGCCTL         0xE00

/* Device registers */
#define DCFG            0x800
#define DCTL            0x804
#define DSTS            0x808
#define DIEPMSK         0x810
#define DOEPMSK         0x814
#define DAINTSTS        0x818
#define DAINTMSK        0x81C
#define USB_INREGS      0x900
#define USB_OUTREGS     0xB00
#define USB_EPREGS_SIZE 0x200
#define USB_FIFO_START  0x1000
#define USB_FIFO_SIZE   (0x100 * 17)  /* 16 FIFOs + 1 */
#define USB_FIFO_END    (USB_FIFO_START + USB_FIFO_SIZE)

/* Register bit definitions */
#define USB_NUM_ENDPOINTS 8
#define USB_NUM_FIFOS 16

/* GOTGCTL bits */
#define GOTGCTL_BSESSIONVALID (1 << 19)
#define GOTGCTL_SESSIONREQUEST (1 << 1)

/* GAHBCFG bits */
#define GAHBCFG_DMAEN (1 << 5)
#define GAHBCFG_BSTLEN_SHIFT 1
#define GAHBCFG_MASKINT 0x1

/* GUSBCFG bits */
#define GUSBCFG_TURNAROUND_MASK 0xF
#define GUSBCFG_TURNAROUND_SHIFT 10
#define GUSBCFG_HNPENABLE (1 << 9)
#define GUSBCFG_SRPENABLE (1 << 8)
#define GUSBCFG_PHYIF16BIT (1 << 3)

/* GRSTCTL bits */
#define GRSTCTL_AHBIDLE (1 << 31)
#define GRSTCTL_TXFFLUSH (1 << 5)
#define GRSTCTL_TXFFNUM_SHIFT 6
#define GRSTCTL_TXFFNUM_MASK 0x1f
#define GRSTCTL_CORESOFTRESET 0x1
#define GRSTCTL_TKNFLUSH 3

/* GINTSTS/GINTMSK bits */
#define GINTMSK_NONE        0x0
#define GINTMSK_OTG         (1 << 2)
#define GINTMSK_SOF         (1 << 3)
#define GINTMSK_GINNAKEFF   (1 << 6)
#define GINTMSK_GOUTNAKEFF  (1 << 7)
#define GINTMSK_SUSPEND     (1 << 11)
#define GINTMSK_RESET       (1 << 12)
#define GINTMSK_ENUMDONE    (1 << 13)
#define GINTMSK_EPMIS       (1 << 17)
#define GINTMSK_INEP        (1 << 18)
#define GINTMSK_OEP         (1 << 19)
#define GINTMSK_DISCONNECT  (1 << 29)
#define GINTMSK_RESUME      (1 << 31)

/* DCTL bits */
#define DCTL_SFTDISCONNECT   0x2
#define DCTL_PROGRAMDONE     (1 << 11)
#define DCTL_CGOUTNAK        (1 << 10)
#define DCTL_SGOUTNAK        (1 << 9)
#define DCTL_CGNPINNAK       (1 << 8)
#define DCTL_SGNPINNAK       (1 << 7)

/* DCFG bits */
#define DCFG_NZSTSOUTHSHK           (1 << 2)
#define DCFG_EPMSCNT                (1 << 18)
#define DCFG_HISPEED                0x0
#define DCFG_FULLSPEED              0x1
#define DCFG_DEVICEADDR_UNSHIFTED_MASK 0x7F
#define DCFG_DEVICEADDR_SHIFT 4
#define DCFG_DEVICEADDRMSK (DCFG_DEVICEADDR_UNSHIFTED_MASK << DCFG_DEVICEADDR_SHIFT)
#define DCFG_ACTIVE_EP_COUNT_MASK   0x1f
#define DCFG_ACTIVE_EP_COUNT_SHIFT  18

/* DAINT bits */
#define DAINT_ALL        0xFFFFFFFF
#define DAINT_NONE       0
#define DAINT_OUT_SHIFT  16
#define DAINT_IN_SHIFT   0

/* Endpoint control bits */
#define USB_EPCON_ENABLE        (1 << 31)
#define USB_EPCON_DISABLE       (1 << 30)
#define USB_EPCON_SETD0PID      (1 << 28)
#define USB_EPCON_SETNAK        (1 << 27)
#define USB_EPCON_CLEARNAK      (1 << 26)
#define USB_EPCON_TXFNUM_MASK   0xf
#define USB_EPCON_TXFNUM_SHIFT  22
#define USB_EPCON_STALL         (1 << 21)
#define USB_EPCON_TYPE_MASK     0x3
#define USB_EPCON_TYPE_SHIFT    18
#define USB_EPCON_NAKSTS        (1 << 17)
#define USB_EPCON_ACTIVE        (1 << 15)
#define USB_EPCON_NEXTEP_MASK   0xF
#define USB_EPCON_NEXTEP_SHIFT  11
#define USB_EPCON_MPS_MASK      0x7FF

/* Endpoint interrupt bits */
#define USB_EPINT_INEPNakEff 0x40
#define USB_EPINT_INTknEPMis 0x20
#define USB_EPINT_INTknTXFEmp 0x10
#define USB_EPINT_TimeOUT 0x8
#define USB_EPINT_AHBErr 0x4
#define USB_EPINT_EPDisbld 0x2
#define USB_EPINT_XferCompl 0x1
#define USB_EPINT_Back2BackSetup (1 << 6)
#define USB_EPINT_OUTTknEPDis 0x10
#define USB_EPINT_SetUp 0x8
#define USB_EPINT_EpDisbld 0x1
#define USB_EPINT_NONE 0
#define USB_EPINT_ALL 0xFFFFFFFF

/* Endpoint state */
typedef struct {
    uint32_t control;
    uint32_t interrupt_status;
    uint32_t tx_size;
    uint32_t dma_address;
    uint32_t dma_buffer;
} S5L8702UsbEpState;

struct S5L8702UsbOtgState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    qemu_irq irq;

    /* Global registers */
    uint32_t pcgcctl;
    uint32_t ghwcfg1;
    uint32_t ghwcfg2;
    uint32_t ghwcfg3;
    uint32_t ghwcfg4;
    uint32_t gahbcfg;
    uint32_t gusbcfg;
    uint32_t grxfsiz;
    uint32_t gnptxfsiz;
    uint32_t gotgctl;
    uint32_t gotgint;
    uint32_t grstctl;
    uint32_t gintmsk;
    uint32_t gintsts;
    uint32_t dptxfsiz[USB_NUM_FIFOS];

    /* Device registers */
    uint32_t dctl;
    uint32_t dcfg;
    uint32_t dsts;
    uint32_t daintmsk;
    uint32_t daintsts;
    uint32_t diepmsk;
    uint32_t doepmsk;

    /* Endpoint arrays */
    S5L8702UsbEpState in_eps[USB_NUM_ENDPOINTS];
    S5L8702UsbEpState out_eps[USB_NUM_ENDPOINTS];

    /* FIFOs */
    uint8_t fifos[USB_FIFO_SIZE];

    /* For simulating USB enumeration */
    bool enumeration_started;
    int enumeration_phase;  /* 0=init, 1=reset injected, 2=setup ready */
    int gintsts_poll_count;

    /* USB/IP server state */
    int usbip_listen_fd;       /* TCP listen socket, -1 if not listening */
    int usbip_client_fd;       /* Connected USB/IP client, -1 if none */
    bool usbip_device_imported; /* True after USBIP_OP_REQ_IMPORT accepted */

    /* Pending IN EP requests: host is waiting for device-to-host data */
    struct {
        bool pending;
        uint32_t seqnum;   /* verbatim from CMD_SUBMIT header, echoed in RET_SUBMIT */
        uint32_t buf_len;  /* host's transfer_buffer_length — cap actual data to this */
    } usbip_in_pending[USB_NUM_ENDPOINTS];

    /* Firmware armed IN EP but no CMD_SUBMIT yet — fulfill on arrival */
    bool usbip_in_ep_armed[USB_NUM_ENDPOINTS];

    /* XferCompl fired but maintenance re-arm ENABLE not yet skipped.
     * The ENABLE immediately after XferCompl is a maintenance re-arm
     * (buffer still has previous transfer data).  Skip it; the following
     * ENABLE will have the real payload. */
    bool usbip_in_ep_xfercompl_pending[USB_NUM_ENDPOINTS];

    /* DIEPDMA was written since last ENABLE — the DMA buffer is fresh.
     * Only ENABLEs with fresh DMA are eligible to arm/fulfill; others
     * (e.g. firmware maintaining endpoint during enumeration) are ignored. */
    bool usbip_in_dma_fresh[USB_NUM_ENDPOINTS];

    /* DIEPTSIZ was written since last ENABLE — the transfer parameters are fresh. */
    bool usbip_in_tsiz_fresh[USB_NUM_ENDPOINTS];

    /* Pending EP0 control transfer */
    bool usbip_ep0_pending;
    bool usbip_ep0_d2h;           /* true = d2h (IN), false = h2d (OUT) */
    uint32_t usbip_ep0_seqnum;
    bool usbip_ep0_status_pending; /* waiting for DOEPINT[0] XferCompl before re-arming */
    bool usbip_inep_active;        /* DIEPINT[0] XferCompl fired for d2h, not yet cleared */
    bool usbip_ep0_txsize_armed;   /* firmware wrote DIEPTSIZ[0] since last SETUP injection */
};

#endif /* HW_MISC_S5L8702_USBOTG_H */
