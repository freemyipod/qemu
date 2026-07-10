#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "hw/misc/s5l8702-usbotg.h"
#include "exec/address-spaces.h"
#include "exec/cpu-common.h"
#include "trace.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

/* Forward declarations */
static void s5l8702_usbotg_update_irq(S5L8702UsbOtgState *s);
static void s5l8702_usbotg_update_ep(S5L8702UsbOtgState *s, S5L8702UsbEpState *ep);
static void usbip_client_readable(void *opaque);

/* ============================================================
 * USB/IP Server Implementation
 * TCP port 3240, big-endian wire format per the USB/IP protocol.
 * ============================================================ */

#define USBIP_VERSION          0x0111
#define USBIP_PORT             3240
#define USBIP_OP_REQ_DEVLIST   0x8005
#define USBIP_OP_REP_DEVLIST   0x0005
#define USBIP_OP_REQ_IMPORT    0x8003
#define USBIP_OP_REP_IMPORT    0x0003
#define USBIP_CMD_SUBMIT       0x00000001
#define USBIP_CMD_UNLINK       0x00000002
#define USBIP_RET_SUBMIT       0x00000003
#define USBIP_RET_UNLINK       0x00000004

/* USB/IP wire structs - all fields big-endian */
typedef struct {
    uint16_t version;
    uint16_t command;
    uint32_t status;
} QEMU_PACKED usbip_op_req_t;

typedef struct {
    uint16_t version;
    uint16_t reply_code;
    uint32_t status;
} QEMU_PACKED usbip_op_rep_t;

typedef struct {
    char     path[256];
    char     bus_id[32];
    uint32_t busnum;
    uint32_t devnum;
    uint32_t speed;
} QEMU_PACKED usbip_device_id_t;

typedef struct {
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bConfigurationValue;
    uint8_t  bNumConfigurations;
    uint8_t  bNumInterfaces;
} QEMU_PACKED usbip_device_desc_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} QEMU_PACKED usbip_iface_desc_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bEndpointAddress;  /* bit 7: direction (1=IN, 0=OUT) */
    uint8_t bmAttributes;       /* 0x02=bulk, 0x03=interrupt */
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
} QEMU_PACKED usbip_ep_desc_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t bMaxPower;
} QEMU_PACKED usbip_config_desc_t;

typedef struct {
    uint32_t command;
    uint32_t seqnum;
    uint32_t devid;
    uint32_t direction;
    uint32_t ep;
} QEMU_PACKED usbip_header_t;

typedef struct {
    uint32_t transfer_flags;
    uint32_t transfer_buffer_length;
    uint32_t start_frame;
    uint32_t number_of_packets;
    uint32_t interval;
    uint8_t  setup[8];
} QEMU_PACKED usbip_cmd_submit_t;

typedef struct {
    uint32_t status;
    uint32_t actual_length;
    uint32_t start_frame;
    uint32_t number_of_packets;
    uint32_t error_count;
    uint64_t padding;
} QEMU_PACKED usbip_ret_submit_t;

typedef struct {
    uint32_t unlink_seqnum;
    uint8_t  padding[24];
} QEMU_PACKED usbip_cmd_unlink_t;

typedef struct {
    uint32_t status;
    uint8_t  padding[24];
} QEMU_PACKED usbip_ret_unlink_t;

/* Blocking read of exactly len bytes - safe to call inside fd handler
 * since the handler only fires when data is available. */
static bool usbip_recv(int fd, void *buf, size_t len)
{
    uint8_t *p = buf;
    size_t orig_len = len;
    while (len > 0) {
        ssize_t n = recv(fd, p, len, MSG_WAITALL);
        if (n <= 0) {
            trace_s5l8702_usbotg_recv_failed(orig_len, n, errno);
            return false;
        }
        p += n;
        len -= n;
    }
    return true;
}

/* Blocking send of exactly len bytes. */
static bool usbip_send(int fd, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    size_t orig_len = len;
    while (len > 0) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n <= 0) {
            trace_s5l8702_usbotg_send_failed(orig_len, n, errno);
            return false;
        }
        p += n;
        len -= n;
    }
    return true;
}

static void usbip_disconnect_impl(S5L8702UsbOtgState *s, const char *reason)
{
    trace_s5l8702_usbotg_disconnect(reason, s->enumeration_phase,
                                    s->usbip_ep0_pending, s->usbip_ep0_d2h,
                                    s->usbip_ep0_status_pending, s->usbip_inep_active);
    if (s->usbip_client_fd >= 0) {
        qemu_set_fd_handler(s->usbip_client_fd, NULL, NULL, NULL);
        close(s->usbip_client_fd);
        s->usbip_client_fd = -1;
    }
    s->usbip_device_imported = false;
    s->usbip_ep0_pending = false;
    s->usbip_ep0_status_pending = false;
    s->usbip_inep_active = false;
    s->usbip_ep0_txsize_armed = false;
    for (int i = 0; i < USB_NUM_ENDPOINTS; i++) {
        g_free(s->usbip_in_pending[i].acc_data);
    }
    memset(s->usbip_in_pending, 0, sizeof(s->usbip_in_pending));
    memset(s->usbip_in_ep_armed, 0, sizeof(s->usbip_in_ep_armed));
    memset(s->usbip_in_ep_xfercompl_pending, 0, sizeof(s->usbip_in_ep_xfercompl_pending));
    memset(s->usbip_in_dma_fresh, 0, sizeof(s->usbip_in_dma_fresh));
    memset(s->usbip_in_tsiz_fresh, 0, sizeof(s->usbip_in_tsiz_fresh));
    for (int i = 0; i < USB_NUM_ENDPOINTS; i++) {
        g_free(s->usbip_out_pending[i].data);
    }
    memset(s->usbip_out_pending, 0, sizeof(s->usbip_out_pending));
    memset(s->usbip_out_ep_armed, 0, sizeof(s->usbip_out_ep_armed));
    memset(s->usbip_out_dma_fresh, 0, sizeof(s->usbip_out_dma_fresh));
    memset(s->usbip_out_tsiz_fresh, 0, sizeof(s->usbip_out_tsiz_fresh));
    memset(s->usbip_in_ep_halted, 0, sizeof(s->usbip_in_ep_halted));
    memset(s->usbip_out_ep_halted, 0, sizeof(s->usbip_out_ep_halted));
}
#define usbip_disconnect(s) usbip_disconnect_impl(s, __func__)

/* Send RET_SUBMIT response for a CMD_SUBMIT.
 * seqnum is echoed verbatim (already network byte order).
 * status is 0 for success or a negative errno (e.g. -EPIPE for a stalled
 * endpoint), matching Linux URB status semantics. */
static bool usbip_send_ret_submit_status(S5L8702UsbOtgState *s,
                                          uint32_t seqnum,
                                          const uint8_t *data,
                                          uint32_t data_len,
                                          int32_t status)
{
    int fd = s->usbip_client_fd;
    if (fd < 0) return false;

    usbip_header_t ret_hdr = {0};
    ret_hdr.command = __builtin_bswap32(USBIP_RET_SUBMIT);
    ret_hdr.seqnum  = seqnum;   /* verbatim */

    usbip_ret_submit_t ret_body = {0};
    ret_body.status            = __builtin_bswap32((uint32_t)status);
    ret_body.actual_length     = __builtin_bswap32(data_len);
    ret_body.number_of_packets = __builtin_bswap32(0);  /* 0 for non-isochronous transfers */

    if (!usbip_send(fd, &ret_hdr, sizeof(ret_hdr))) return false;
    if (!usbip_send(fd, &ret_body, sizeof(ret_body))) return false;
    if (data_len > 0 && data) {
        if (!usbip_send(fd, data, data_len)) return false;
    }
    return true;
}

static bool usbip_send_ret_submit(S5L8702UsbOtgState *s,
                                   uint32_t seqnum,
                                   const uint8_t *data,
                                   uint32_t data_len)
{
    return usbip_send_ret_submit_status(s, seqnum, data, data_len, 0);
}

/* Drop a pending IN URB's state, freeing any accumulated data. */
static void usbip_in_clear_pending(S5L8702UsbOtgState *s, uint32_t ep)
{
    g_free(s->usbip_in_pending[ep].acc_data);
    s->usbip_in_pending[ep].acc_data = NULL;
    s->usbip_in_pending[ep].acc_len = 0;
    s->usbip_in_pending[ep].pending = false;
}

/* Consume one firmware-armed IN transfer into the pending host URB.
 *
 * On real USB the host controller keeps issuing IN tokens until either
 * its URB buffer is full or the device sends a short (< max packet) or
 * zero-length packet.  A full max-packet chunk therefore does NOT
 * complete the URB.  Mirror that: append the armed data to the URB's
 * accumulation buffer, drain DIEPTSIZ/DIEPDMA and fire XferCompl so the
 * firmware arms its next chunk, and only RET_SUBMIT once the USB-level
 * completion condition holds.
 *
 * Returns false on a socket error (caller must usbip_disconnect). */
static bool usbip_in_try_fulfill(S5L8702UsbOtgState *s, uint32_t ep)
{
    uint32_t cur_txsize = s->in_eps[ep].tx_size;
    uint32_t dev_len = cur_txsize & 0x7ffff;
    uint32_t mps = s->in_eps[ep].control & USB_EPCON_MPS_MASK;
    uint32_t buf_len = s->usbip_in_pending[ep].buf_len;
    uint32_t acc_len = s->usbip_in_pending[ep].acc_len;
    uint32_t remaining = buf_len - acc_len;
    uint32_t len = (dev_len < remaining) ? dev_len : remaining;

    if (len > 0) {
        if (!s->usbip_in_pending[ep].acc_data) {
            s->usbip_in_pending[ep].acc_data = g_malloc(buf_len);
        }
        cpu_physical_memory_read(s->in_eps[ep].dma_address,
                                 s->usbip_in_pending[ep].acc_data + acc_len,
                                 len);
        acc_len += len;
        s->usbip_in_pending[ep].acc_len = acc_len;
    }

    /* Drain tx_size (XferSize bits 18:0, PktCnt bits 28:19) and dma_address
     * the way hardware would */
    uint32_t xfer_size = dev_len;
    uint32_t pkt_cnt = (cur_txsize >> 19) & 0x3ff;
    if (len >= xfer_size) {
        xfer_size = 0;
    } else {
        xfer_size -= len;
    }
    uint32_t pkts = mps ? DIV_ROUND_UP(len, mps) : 1;
    if (pkts == 0) pkts = 1;  /* ZLP still consumes one packet */
    pkt_cnt = (pkts >= pkt_cnt) ? 0 : pkt_cnt - pkts;
    s->in_eps[ep].tx_size = (cur_txsize & ~0x1fffffff) | xfer_size | (pkt_cnt << 19);
    s->in_eps[ep].dma_address += len;

    /* The arm is consumed either way: fire XferCompl so the firmware's
     * state machine advances (and arms the next chunk if it has one). */
    s->usbip_in_ep_armed[ep] = false;
    s->usbip_in_ep_xfercompl_pending[ep] = true;
    s->in_eps[ep].interrupt_status |= USB_EPINT_XferCompl;
    s->in_eps[ep].control &= ~USB_EPCON_ENABLE;
    s5l8702_usbotg_update_irq(s);

    bool complete = (acc_len >= buf_len) ||   /* host buffer full */
                    (len == 0) ||             /* ZLP terminates */
                    (mps == 0) ||
                    (len % mps) != 0;         /* short packet terminates */

    if (!complete) {
        trace_s5l8702_usbotg_in_chunk(ep, len, acc_len, buf_len);
        return true;
    }

    trace_s5l8702_usbotg_in_complete(ep, acc_len, buf_len);
    bool ok = usbip_send_ret_submit(s, s->usbip_in_pending[ep].seqnum,
                                    s->usbip_in_pending[ep].acc_data, acc_len);
    usbip_in_clear_pending(s, ep);
    return ok;
}

/* Firmware stalled an endpoint that has an outstanding host request.
 * Complete the URB with -EPIPE so the host sees the stall immediately
 * (instead of waiting out its 30s URB timeout) and can issue
 * CLEAR_FEATURE(HALT) / error recovery per the USB spec. */
static void usbip_reject_stalled(S5L8702UsbOtgState *s, uint32_t ep, bool is_in)
{
    if (ep == 0) {
        if (!s->usbip_ep0_pending) return;
        trace_s5l8702_usbotg_ep0_stall_reject();
        usbip_send_ret_submit_status(s, s->usbip_ep0_seqnum, NULL, 0, -EPIPE);
        s->usbip_ep0_pending = false;
        s->usbip_ep0_txsize_armed = false;
        if (s->usbip_client_fd >= 0) {
            qemu_set_fd_handler(s->usbip_client_fd, usbip_client_readable, NULL, s);
        }
    } else if (is_in) {
        s->usbip_in_ep_armed[ep] = false;
        if (!s->usbip_in_pending[ep].pending) return;
        trace_s5l8702_usbotg_in_stall_reject(ep);
        usbip_send_ret_submit_status(s, s->usbip_in_pending[ep].seqnum, NULL, 0, -EPIPE);
        usbip_in_clear_pending(s, ep);
    } else {
        s->usbip_out_ep_armed[ep] = false;
        if (!s->usbip_out_pending[ep].pending) return;
        trace_s5l8702_usbotg_out_stall_reject(ep);
        usbip_send_ret_submit_status(s, s->usbip_out_pending[ep].seqnum, NULL, 0, -EPIPE);
        g_free(s->usbip_out_pending[ep].data);
        s->usbip_out_pending[ep].data = NULL;
        s->usbip_out_pending[ep].len = 0;
        s->usbip_out_pending[ep].offset = 0;
        s->usbip_out_pending[ep].pending = false;
    }
}

/* iPod Nano 3G / Apple USB MSC device identity. */
static bool usbip_send_devlist(S5L8702UsbOtgState *s)
{
    int fd = s->usbip_client_fd;

    usbip_op_rep_t rep = {0};
    rep.version    = __builtin_bswap16(USBIP_VERSION);
    rep.reply_code = __builtin_bswap16(USBIP_OP_REP_DEVLIST);
    if (!usbip_send(fd, &rep, sizeof(rep))) return false;

    uint32_t num_dev = __builtin_bswap32(1);
    if (!usbip_send(fd, &num_dev, sizeof(num_dev))) return false;

    usbip_device_id_t dev_id = {0};
    strncpy(dev_id.path,   "/sys/devices/platform/s5l8702_usbotg/usb1/1-1", sizeof(dev_id.path) - 1);
    strncpy(dev_id.bus_id, "1-1", sizeof(dev_id.bus_id) - 1);
    dev_id.busnum = __builtin_bswap32(1);
    dev_id.devnum = __builtin_bswap32(2);
    dev_id.speed  = __builtin_bswap32(3); /* USB_SPEED_HIGH - real device is high-speed 480 Mbps */
    if (!usbip_send(fd, &dev_id, sizeof(dev_id))) return false;

    /* Device descriptor with Apple vendor ID.
     * Real endpoint/config info comes from firmware GET_DESCRIPTOR responses. */
    usbip_device_desc_t desc = {0};
    desc.idVendor            = __builtin_bswap16(0x05AC);  /* Apple Inc. */
    desc.idProduct           = __builtin_bswap16(0x1223);  /* DFU/MSC device */
    desc.bcdDevice           = __builtin_bswap16(0x0100);  /* version 1.0 */
    desc.bDeviceClass        = 0x00;        /* Defined at interface level */
    desc.bDeviceSubClass     = 0x00;
    desc.bDeviceProtocol     = 0x00;
    desc.bNumConfigurations  = 1;
    desc.bNumInterfaces      = 1;
    if (!usbip_send(fd, &desc, sizeof(desc))) return false;

    /* Interface descriptor: placeholder for bInterfaceClass/SubClass/Protocol.
     * Real values come from firmware's GET_DESCRIPTOR(CONFIG) response.
     * This is just informational for the USBIP server. */
    uint8_t iface[4] = {0x08, 0x06, 0x50, 0x00};  /* class, subclass, protocol, padding */
    if (!usbip_send(fd, iface, sizeof(iface))) return false;

    return true;
}

static bool usbip_send_import_reply(S5L8702UsbOtgState *s)
{
    int fd = s->usbip_client_fd;

    usbip_op_rep_t rep = {0};
    rep.version    = __builtin_bswap16(USBIP_VERSION);
    rep.reply_code = __builtin_bswap16(USBIP_OP_REP_IMPORT);
    if (!usbip_send(fd, &rep, sizeof(rep))) return false;

    usbip_device_id_t dev_id = {0};
    strncpy(dev_id.path,   "/sys/devices/platform/s5l8702_usbotg/usb1/1-1", sizeof(dev_id.path) - 1);
    strncpy(dev_id.bus_id, "1-1", sizeof(dev_id.bus_id) - 1);
    dev_id.busnum = __builtin_bswap32(1);
    dev_id.devnum = __builtin_bswap32(2);
    dev_id.speed  = __builtin_bswap32(3); /* USB_SPEED_HIGH - real device is high-speed 480 Mbps */
    if (!usbip_send(fd, &dev_id, sizeof(dev_id))) return false;

    /* Device descriptor with Apple vendor ID. Real descriptor from firmware. */
    usbip_device_desc_t desc = {0};
    desc.idVendor            = __builtin_bswap16(0x05AC);  /* Apple Inc. */
    desc.idProduct           = __builtin_bswap16(0x9999);  /* Test device (avoids apple-mfi-fastcharge) */
    desc.bcdDevice           = __builtin_bswap16(0x0100);  /* version 1.0 */
    desc.bNumConfigurations  = 1;
    desc.bNumInterfaces      = 1;
    if (!usbip_send(fd, &desc, sizeof(desc))) return false;

    return true;
}
/* Conditionally arm the USB/IP client fd handler when firmware is ready.
 * Firmware is ready when BOTH conditions hold:
 * 1. enumeration_phase >= 3 (ENUMDONE was cleared)
 * 2. out_eps[0].dma_address != 0 (firmware wrote DOEPDMA[0])
 *
 * This handles both forward and reverse ordering of ENUMDONE clear vs DOEPDMA write. */
static void usbip_maybe_arm_fd(S5L8702UsbOtgState *s)
{
    if (!s->usbip_device_imported || s->usbip_client_fd < 0) return;
    if (s->enumeration_phase < 3) return;                     /* ENUMDONE not cleared yet */
    if (s->out_eps[0].dma_address == 0) return;               /* DMA not set yet */
    if (s->usbip_ep0_pending || s->usbip_inep_active) return; /* transfer in flight */

    qemu_set_fd_handler(s->usbip_client_fd, usbip_client_readable, NULL, s);
}

/* Handle CMD_SUBMIT for EP0 (control transfer).
 * Injects the SETUP packet into firmware DMA and defers the response
 * until firmware processes it (detected via DIEPCTL0 ENABLE in in_ep_write). */
static void usbip_handle_ep0_submit(S5L8702UsbOtgState *s,
                                     const usbip_header_t *hdr,
                                     const usbip_cmd_submit_t *cmd)
{
    int fd = s->usbip_client_fd;
    uint32_t direction = __builtin_bswap32(hdr->direction);
    uint32_t buf_len   = __builtin_bswap32(cmd->transfer_buffer_length);
    bool d2h = (direction == 1);

    /* Ensure firmware has armed OUT EP0 with a valid DMA buffer */
    if (s->out_eps[0].dma_address == 0) {
        trace_s5l8702_usbotg_ep0_not_armed(s->out_eps[0].dma_address);
        usbip_disconnect(s);
        return;
    }

    /* For h2d transfers with a data stage, read and write data after SETUP */
    if (!d2h && buf_len > 0) {
        if (buf_len > 4096) {
            trace_s5l8702_usbotg_ep0_out_too_large(buf_len);
            usbip_disconnect(s);
            return;
        }
        uint8_t tmp[4096];
        if (!usbip_recv(fd, tmp, buf_len)) { usbip_disconnect(s); return; }
        cpu_physical_memory_write(s->out_eps[0].dma_address + 8, tmp, buf_len);
    }

    /* Write SETUP bytes to firmware DMA */
    cpu_physical_memory_write(s->out_eps[0].dma_address, cmd->setup, 8);

    /* Simulate 8 bytes consumed: XFERSIZE = 64-8 = 56 */
    s->out_eps[0].tx_size = (s->out_eps[0].tx_size & ~0x7f) | 0x38;

    /* Fire DOEPINT[0] STUP - firmware ISR will see this */
    s->out_eps[0].interrupt_status |= USB_EPINT_SetUp;
    s->pcgcctl = 0;

    /* Store pending state */
    s->usbip_ep0_pending = true;
    s->usbip_ep0_d2h    = d2h;
    s->usbip_ep0_seqnum = hdr->seqnum;  /* verbatim */
    s->usbip_ep0_txsize_armed = false;   /* wait for firmware to write DIEPTSIZ */

    /* Pause reading until we can respond */
    qemu_set_fd_handler(fd, NULL, NULL, NULL);

    trace_s5l8702_usbotg_ep0_setup(d2h ? "d2h" : "h2d",
        cmd->setup[0], cmd->setup[1], cmd->setup[2], cmd->setup[3],
        cmd->setup[4], cmd->setup[5], cmd->setup[6], cmd->setup[7]);

    s5l8702_usbotg_update_irq(s);
}

/* Deliver buffered host→device data to an armed OUT endpoint:
 * DMA the payload, update DOEPTSIZ, fire XferCompl, and complete the
 * host's URB with RET_SUBMIT.  No-op unless the EP is both armed by
 * firmware and has pending host data. */
static void usbip_out_try_deliver(S5L8702UsbOtgState *s, uint32_t ep)
{
    if (!s->usbip_out_ep_armed[ep] || !s->usbip_out_pending[ep].pending) {
        return;
    }

    uint32_t total = s->usbip_out_pending[ep].len;
    uint32_t off = s->usbip_out_pending[ep].offset;
    uint32_t remaining = total - off;
    uint32_t seqnum = s->usbip_out_pending[ep].seqnum;
    uint32_t xfer_size = s->out_eps[ep].tx_size & 0x7ffff;
    uint32_t pkt_cnt = (s->out_eps[ep].tx_size >> 19) & 0x3ff;
    uint32_t mps = s->out_eps[ep].control & USB_EPCON_MPS_MASK;
    uint32_t len = remaining;

    /* Deliver at most what the firmware armed; the rest waits for re-arm */
    if (len > xfer_size) {
        len = xfer_size;
    }
    if (len > 0) {
        cpu_physical_memory_write(s->out_eps[ep].dma_address,
                                  s->usbip_out_pending[ep].data + off, len);
    } else if (remaining > 0) {
        /* Armed with a zero-size buffer while data is waiting - deliver
         * nothing but don't consume the arm, or we'd spin forever. */
        trace_s5l8702_usbotg_out_zero_arm(ep, remaining);
        return;
    }

    /* Update tx_size (XferSize bits 18:0, PktCnt bits 28:19) and dma_address
     * the way hardware would: firmware computes received = programmed - remaining */
    xfer_size -= len;
    uint32_t pkts = mps ? DIV_ROUND_UP(len, mps) : 1;
    if (pkts == 0) pkts = 1;  /* ZLP still consumes one packet */
    pkt_cnt = (pkts >= pkt_cnt) ? 0 : pkt_cnt - pkts;
    s->out_eps[ep].tx_size = (s->out_eps[ep].tx_size & ~0x1fffffff) | xfer_size | (pkt_cnt << 19);
    s->out_eps[ep].dma_address += len;

    s->usbip_out_pending[ep].offset = off + len;
    s->usbip_out_ep_armed[ep] = false;

    /* Fire DOEPINT[ep] XferCompl; transfer consumed the ENABLE */
    s->out_eps[ep].interrupt_status |= USB_EPINT_XferCompl;
    s->out_eps[ep].control &= ~USB_EPCON_ENABLE;
    s->pcgcctl = 0;

    if (s->usbip_out_pending[ep].offset >= total) {
        /* All host data consumed - complete the URB */
        g_free(s->usbip_out_pending[ep].data);
        s->usbip_out_pending[ep].data = NULL;
        s->usbip_out_pending[ep].pending = false;

        trace_s5l8702_usbotg_out_delivered(ep, total);

        if (!usbip_send_ret_submit(s, seqnum, NULL, total)) {
            usbip_disconnect(s);
            return;
        }
    } else {
        trace_s5l8702_usbotg_out_chunk(ep, s->usbip_out_pending[ep].offset, total);
    }

    s5l8702_usbotg_update_irq(s);
}

/* Handle CMD_SUBMIT for OUT bulk/interrupt endpoint (host→device).
 * The payload is always consumed from the socket (to keep protocol framing)
 * but only delivered to guest memory once the firmware has armed the EP
 * with fresh DOEPDMA/DOEPTSIZ + ENABLE.  RET_SUBMIT is deferred until
 * delivery so the host's URB doesn't complete prematurely. */
static void usbip_handle_out_submit(S5L8702UsbOtgState *s,
                                     const usbip_header_t *hdr,
                                     const usbip_cmd_submit_t *cmd,
                                     uint32_t ep)
{
    int fd = s->usbip_client_fd;
    uint32_t buf_len = __builtin_bswap32(cmd->transfer_buffer_length);
    uint8_t *buf = NULL;

    if (ep >= USB_NUM_ENDPOINTS) {
        trace_s5l8702_usbotg_out_invalid_ep(ep);
        usbip_disconnect(s);
        return;
    }

    if (buf_len > 0) {
        if (buf_len > 1024 * 1024) {
            /* Same bound as the IN path.  usb-storage batches writes into
             * URBs of up to 120 KiB, so a 64 KiB cap here tears down the
             * whole connection on the first big write burst. */
            trace_s5l8702_usbotg_out_too_large(ep, buf_len);
            usbip_disconnect(s);
            return;
        }
        buf = g_malloc(buf_len);
        if (!usbip_recv(fd, buf, buf_len)) {
            g_free(buf);
            usbip_disconnect(s);
            return;
        }
    }

    if (s->usbip_out_pending[ep].pending) {
        trace_s5l8702_usbotg_out_double_pending(ep);
        g_free(buf);
        usbip_disconnect(s);
        return;
    }
    if (s->usbip_out_ep_halted[ep]) {
        trace_s5l8702_usbotg_out_halted_urb(ep);
        g_free(buf);
        usbip_send_ret_submit_status(s, hdr->seqnum, NULL, 0, -EPIPE);
        return;
    }

    s->usbip_out_pending[ep].pending = true;
    s->usbip_out_pending[ep].seqnum = hdr->seqnum;  /* verbatim */
    s->usbip_out_pending[ep].len = buf_len;
    s->usbip_out_pending[ep].offset = 0;
    s->usbip_out_pending[ep].data = buf;

    /* Level-triggered arming: if the EP's ENABLE bit is currently set, the
     * firmware is ready to receive into DOEPDMA right now - even if the
     * enabling write happened before we started tracking (firmware enables
     * its bulk OUT EP once and leaves it waiting; real hardware just NAKs
     * until data arrives). */
    if (!s->usbip_out_ep_armed[ep] &&
        (s->out_eps[ep].control & USB_EPCON_ENABLE)) {
        s->usbip_out_ep_armed[ep] = true;
        s->usbip_out_dma_fresh[ep] = false;
        s->usbip_out_tsiz_fresh[ep] = false;
        trace_s5l8702_usbotg_out_level_armed(ep, s->out_eps[ep].tx_size & 0x7ffff);
    }

    if (s->usbip_out_ep_armed[ep]) {
        usbip_out_try_deliver(s, ep);
    } else {
        trace_s5l8702_usbotg_out_queued(ep, buf_len);
    }
}

/* Handle CMD_SUBMIT for IN bulk/interrupt endpoint (device→host).
 * If firmware already armed the EP, fulfill immediately.
 * Otherwise defer until firmware arms (detected in in_ep_write). */
static void usbip_handle_in_submit(S5L8702UsbOtgState *s,
                                    const usbip_header_t *hdr,
                                    uint32_t ep,
                                    uint32_t buf_len)
{
    if (ep >= USB_NUM_ENDPOINTS) {
        trace_s5l8702_usbotg_in_invalid_ep(ep);
        usbip_disconnect(s);
        return;
    }
    if (s->usbip_in_pending[ep].pending) {
        trace_s5l8702_usbotg_in_double_pending(ep);
        usbip_disconnect(s);
        return;
    }
    if (s->usbip_in_ep_halted[ep]) {
        /* Endpoint is halted (firmware STALL, e.g. BOT short-data-phase
         * termination) - fail the URB now so the host runs clear-halt
         * recovery instead of waiting out its timeout. */
        trace_s5l8702_usbotg_in_halted_urb(ep);
        usbip_send_ret_submit_status(s, hdr->seqnum, NULL, 0, -EPIPE);
        return;
    }

    if (buf_len > 1024 * 1024) {
        /* Bound the accumulation buffer; larger URBs than this never
         * occur for a BOT device (usb-storage caps at 120 KiB). */
        trace_s5l8702_usbotg_in_too_large(ep, buf_len);
        usbip_disconnect(s);
        return;
    }

    s->usbip_in_pending[ep].pending = true;
    s->usbip_in_pending[ep].seqnum = hdr->seqnum;  /* verbatim */
    s->usbip_in_pending[ep].buf_len = buf_len;
    s->usbip_in_pending[ep].acc_len = 0;
    s->usbip_in_pending[ep].acc_data = NULL;

    if (s->usbip_in_ep_armed[ep]) {
        /* Firmware already armed the endpoint with fresh DMA data.
         * usbip_in_ep_armed is only set when DIEPDMA was freshly written,
         * so the buffer at dma_address is current. Consume it now. */
        trace_s5l8702_usbotg_in_immediate(ep, s->in_eps[ep].tx_size & 0x7ffff);
        if (!usbip_in_try_fulfill(s, ep)) {
            usbip_disconnect(s);
        }
        return;
    }

    /* The host is polling this endpoint with IN tokens.  On real hardware
     * an IN token to a not-yet-enabled endpoint fires DIEPINT.INTknTXFEmp;
     * some firmware waits for exactly that event before arming the EP
     * (e.g. for the CSW of a mass-storage transfer).  Inject it. */
    s->in_eps[ep].interrupt_status |= USB_EPINT_INTknTXFEmp;
    s->pcgcctl = 0;
    s5l8702_usbotg_update_irq(s);

    trace_s5l8702_usbotg_in_pending(ep, buf_len);
}

static void usbip_handle_cmd_submit(S5L8702UsbOtgState *s, const usbip_header_t *hdr)
{
    int fd = s->usbip_client_fd;

    usbip_cmd_submit_t cmd;
    if (!usbip_recv(fd, &cmd, sizeof(cmd))) { usbip_disconnect(s); return; }

    uint32_t ep        = __builtin_bswap32(hdr->ep);
    uint32_t direction = __builtin_bswap32(hdr->direction);
    uint32_t buf_len   = __builtin_bswap32(cmd.transfer_buffer_length);

    if (ep == 0) {
        usbip_handle_ep0_submit(s, hdr, &cmd);
    } else if (direction == 0) {
        usbip_handle_out_submit(s, hdr, &cmd, ep);
    } else {
        usbip_handle_in_submit(s, hdr, ep, buf_len);
    }
}

static void usbip_handle_cmd_unlink(S5L8702UsbOtgState *s, const usbip_header_t *hdr)
{
    int fd = s->usbip_client_fd;

    usbip_cmd_unlink_t cmd;
    if (!usbip_recv(fd, &cmd, sizeof(cmd))) { usbip_disconnect(s); return; }

    uint32_t unlink_seqnum = cmd.unlink_seqnum; /* verbatim, big-endian */
    trace_s5l8702_usbotg_unlink(__builtin_bswap32(hdr->seqnum),
                                __builtin_bswap32(unlink_seqnum));

    /* Clear any pending IN/OUT EP request that matches the unlinked seqnum */
    for (int i = 0; i < USB_NUM_ENDPOINTS; i++) {
        if (s->usbip_in_pending[i].pending &&
            s->usbip_in_pending[i].seqnum == unlink_seqnum) {
            usbip_in_clear_pending(s, i);
        }
        if (s->usbip_out_pending[i].pending &&
            s->usbip_out_pending[i].seqnum == unlink_seqnum) {
            g_free(s->usbip_out_pending[i].data);
            s->usbip_out_pending[i].data = NULL;
            s->usbip_out_pending[i].len = 0;
            s->usbip_out_pending[i].offset = 0;
            s->usbip_out_pending[i].pending = false;
        }
    }

    usbip_header_t ret_hdr = {0};
    ret_hdr.command = __builtin_bswap32(USBIP_RET_UNLINK);
    ret_hdr.seqnum  = hdr->seqnum;   /* echo CMD_UNLINK's own seqnum */

    usbip_ret_unlink_t ret_body = {0};
    ret_body.status = __builtin_bswap32((uint32_t)-ECONNRESET); /* -104: URB was cancelled */

    usbip_send(fd, &ret_hdr, sizeof(ret_hdr));
    usbip_send(fd, &ret_body, sizeof(ret_body));
}

/* fd handler: called when the USB/IP client socket has data to read. */
static void usbip_client_readable(void *opaque)
{
    S5L8702UsbOtgState *s = opaque;
    int fd = s->usbip_client_fd;

    if (!s->usbip_device_imported) {
        /* Pre-import: expect OP_REQ_DEVLIST or OP_REQ_IMPORT */
        usbip_op_req_t req;
        if (!usbip_recv(fd, &req, sizeof(req))) { usbip_disconnect(s); return; }

        uint16_t version = __builtin_bswap16(req.version);
        uint16_t command = __builtin_bswap16(req.command);

        if (version != USBIP_VERSION) {
            trace_s5l8702_usbotg_bad_version(version);
            usbip_disconnect(s);
            return;
        }

        if (command == USBIP_OP_REQ_DEVLIST) {
            if (!usbip_send_devlist(s)) usbip_disconnect(s);
        } else if (command == USBIP_OP_REQ_IMPORT) {
            char bus_id[32] = {0};
            if (!usbip_recv(fd, bus_id, sizeof(bus_id))) { usbip_disconnect(s); return; }
            if (!usbip_send_import_reply(s)) { usbip_disconnect(s); return; }
            s->usbip_device_imported = true;
            trace_s5l8702_usbotg_imported(s->enumeration_phase);
            if (s->enumeration_phase == 1) {
                s->gintsts |= (1 << 13);  /* ENUMDONE */
                s->enumeration_phase = 2;
                s5l8702_usbotg_update_irq(s);
            } else if (s->enumeration_phase >= 3) {
                usbip_maybe_arm_fd(s);
            }
        } else {
            trace_s5l8702_usbotg_unknown_preimport_cmd(command);
            usbip_disconnect(s);
        }
    } else {
        /* Post-import: expect CMD_SUBMIT or CMD_UNLINK */
        usbip_header_t hdr;
        if (!usbip_recv(fd, &hdr, sizeof(hdr))) { usbip_disconnect(s); return; }

        uint32_t command = __builtin_bswap32(hdr.command);
        if (command == USBIP_CMD_SUBMIT) {
            usbip_handle_cmd_submit(s, &hdr);
        } else if (command == USBIP_CMD_UNLINK) {
            usbip_handle_cmd_unlink(s, &hdr);
        } else {
            trace_s5l8702_usbotg_unknown_cmd(command);
            usbip_disconnect(s);
        }
    }
}

/* fd handler: called when the listen socket has an incoming connection. */
static void usbip_listen_readable(void *opaque)
{
    S5L8702UsbOtgState *s = opaque;

    int client_fd = accept(s->usbip_listen_fd, NULL, NULL);
    if (client_fd < 0) return;

    if (s->usbip_client_fd >= 0) {
        /* Already have a client - reject */
        close(client_fd);
        trace_s5l8702_usbotg_second_client();
        return;
    }

    /* Make client socket blocking (for usbip_recv/usbip_send) */
    fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL) & ~O_NONBLOCK);

    s->usbip_client_fd = client_fd;
    qemu_set_fd_handler(client_fd, usbip_client_readable, NULL, s);
    trace_s5l8702_usbotg_client_connected(client_fd);
}

/* Start the USB/IP TCP server on port 3240. */
static void usbip_server_start(S5L8702UsbOtgState *s)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        trace_s5l8702_usbotg_server_error("socket", errno);
        return;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    fcntl(fd, F_SETFL, O_NONBLOCK);

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(USBIP_PORT),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        trace_s5l8702_usbotg_server_error("bind", errno);
        close(fd);
        return;
    }
    if (listen(fd, 1) < 0) {
        trace_s5l8702_usbotg_server_error("listen", errno);
        close(fd);
        return;
    }

    s->usbip_listen_fd = fd;
    qemu_set_fd_handler(fd, usbip_listen_readable, NULL, s);
    trace_s5l8702_usbotg_listening(USBIP_PORT);
}

/* ============================================================ */

/* Debug logging helpers */
static const char *offset_name(hwaddr offset)
{
    switch (offset) {
        case PCGCCTL: return "PCGCCTL";
        case GOTGCTL: return "GOTGCTL";
        case GOTGINT: return "GOTGINT";
        case GAHBCFG: return "GAHBCFG";
        case GUSBCFG: return "GUSBCFG";
        case GRSTCTL: return "GRSTCTL";
        case GINTSTS: return "GINTSTS";
        case GINTMSK: return "GINTMSK";
        case GRXSTSR: return "GRXSTSR";
        case GRXSTSP: return "GRXSTSP";
        case GRXFSIZ: return "GRXFSIZ";
        case GNPTXFSIZ: return "GNPTXFSIZ";
        case GNPTXFSTS: return "GNPTXFSTS";
        case GHWCFG1: return "GHWCFG1";
        case GHWCFG2: return "GHWCFG2";
        case GHWCFG3: return "GHWCFG3";
        case GHWCFG4: return "GHWCFG4";
        case DCFG: return "DCFG";
        case DCTL: return "DCTL";
        case DSTS: return "DSTS";
        case DIEPMSK: return "DIEPMSK";
        case DOEPMSK: return "DOEPMSK";
        case DAINTSTS: return "DAINTSTS";
        case DAINTMSK: return "DAINTMSK";
        case 0x440: return "HPRT0";
        default:
            if (offset >= DIEPTXF(1) && offset <= DIEPTXF(USB_NUM_FIFOS + 1)) {
                static char buf[32];
                snprintf(buf, sizeof(buf), "DIEPTXF(%d)", (int)((offset - DIEPTXF(1)) >> 2) + 1);
                return buf;
            }
            if (offset >= USB_INREGS && offset < USB_INREGS + USB_EPREGS_SIZE) {
                static char buf[32];
                hwaddr ep_off = offset - USB_INREGS;
                snprintf(buf, sizeof(buf), "IN_EP[%d]+0x%x", (int)(ep_off >> 5), (unsigned)(ep_off & 0x1f));
                return buf;
            }
            if (offset >= USB_OUTREGS && offset < USB_OUTREGS + USB_EPREGS_SIZE) {
                static char buf[32];
                hwaddr ep_off = offset - USB_OUTREGS;
                snprintf(buf, sizeof(buf), "OUT_EP[%d]+0x%x", (int)(ep_off >> 5), (unsigned)(ep_off & 0x1f));
                return buf;
            }
            if (offset >= USB_FIFO_START && offset < USB_FIFO_END) {
                static char buf[32];
                snprintf(buf, sizeof(buf), "FIFO[0x%x]", (unsigned)(offset - USB_FIFO_START));
                return buf;
            }
            return "UNKNOWN";
    }
}

static inline size_t usb_tx_fifo_start(S5L8702UsbOtgState *s, uint32_t fifo)
{
    if (fifo == 0)
        return s->gnptxfsiz >> 16;
    else
        return s->dptxfsiz[fifo - 1] >> 16;
}

static inline size_t usb_tx_fifo_size(S5L8702UsbOtgState *s, uint32_t fifo)
{
    if (fifo == 0)
        return s->gnptxfsiz & 0xFFFF;
    else
        return s->dptxfsiz[fifo - 1] & 0xFFFF;
}

static void s5l8702_usbotg_update_irq(S5L8702UsbOtgState *s)
{
    s->daintsts = 0;
    s->gintsts &= ~((1 << 19) | (1 << 18) | (1 << 2));  /* OEP, INEP, OTG */

    if (s->gotgint)
        s->gintsts |= (1 << 2);  /* OTG interrupt */

    /* Check endpoint interrupts */
    for (int i = 0; i < USB_NUM_ENDPOINTS; i++) {
        if (s->out_eps[i].interrupt_status & s->doepmsk) {
            s->daintsts |= 1 << (i + 16);  /* OUT endpoint */
            if (s->daintmsk & (1 << (i + 16)))
                s->gintsts |= (1 << 19);  /* OEP interrupt */
        }

        if (s->in_eps[i].interrupt_status & s->diepmsk) {
            s->daintsts |= 1 << i;  /* IN endpoint */
            if (s->daintmsk & (1 << i))
                s->gintsts |= (1 << 18);  /* INEP interrupt */
        }
    }

    /* Raise/lower IRQ based on interrupt status and mask */
    if ((s->pcgcctl & 3) == 0 && (s->gintmsk & s->gintsts)) {
        qemu_irq_raise(s->irq);
    } else {
        qemu_irq_lower(s->irq);
    }
}

static void s5l8702_usbotg_update_ep(S5L8702UsbOtgState *s, S5L8702UsbEpState *ep)
{
    /* Handle SET NAK */
    if (ep->control & (1 << 27)) {  /* SETNAK */
        ep->control |= (1 << 17);  /* NAKSTS */
        ep->interrupt_status |= 0x40;  /* INEPNakEff */
        ep->control &= ~(1 << 27);  /* Clear SETNAK */
    }

    /* Handle CLEAR NAK */
    if (ep->control & (1 << 26)) {  /* CNAK */
        ep->control &= ~(1 << 17);  /* Clear NAKSTS */
        ep->control &= ~(1 << 26);  /* Clear CNAK (write-only trigger bit) */
    }

    /* Handle disable */
    if (ep->control & (1 << 30)) {  /* DISABLE */
        ep->interrupt_status |= 0x2;  /* EPDisbld */
        ep->control &= ~((1 << 30) | (1 << 31));  /* Clear DISABLE and ENABLE */
    }
}

static uint32_t s5l8702_usbotg_in_ep_read(S5L8702UsbOtgState *s, uint8_t ep, hwaddr offset)
{
    if (ep >= USB_NUM_ENDPOINTS) {
        qemu_log_mask(LOG_GUEST_ERROR, "USB: Read from disabled IN EP %d\n", ep);
        return 0;
    }

    switch (offset) {
        case 0x00: return s->in_eps[ep].control;
        case 0x08: return s->in_eps[ep].interrupt_status;
        case 0x10: return s->in_eps[ep].tx_size;
        case 0x14: return s->in_eps[ep].dma_address;
        case 0x18:
            /* DTXFSTS: available space in the endpoint's TxFIFO, in 32-bit
             * words.  We move data instantly, so the FIFO is always empty -
             * report maximum free space.  Returning 0 here (the old default)
             * wedges firmware that polls for FIFO room before arming. */
            return 0xFFFF;
        case 0x1C: return s->in_eps[ep].dma_buffer;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "USB: Bad IN EP read offset 0x%x\n", (unsigned)offset);
            return 0;
    }
}

static uint32_t s5l8702_usbotg_out_ep_read(S5L8702UsbOtgState *s, uint8_t ep, hwaddr offset)
{
    if (ep >= USB_NUM_ENDPOINTS) {
        qemu_log_mask(LOG_GUEST_ERROR, "USB: Read from disabled OUT EP %d\n", ep);
        return 0;
    }

    switch (offset) {
        case 0x00: return s->out_eps[ep].control;
        case 0x08: return s->out_eps[ep].interrupt_status;
        case 0x10: return s->out_eps[ep].tx_size;
        case 0x14: return s->out_eps[ep].dma_address;
        case 0x1C: return s->out_eps[ep].dma_buffer;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "USB: Bad OUT EP read offset 0x%x\n", (unsigned)offset);
            return 0;
    }
}

static uint64_t s5l8702_usbotg_read(void *opaque, hwaddr offset, unsigned size)
{
    S5L8702UsbOtgState *s = S5L8702_USBOTG(opaque);
    uint64_t val = 0;

    /*
     * After firmware connects (DCTL=0xd00) and enters its polling loop,
     * inject ENUMDONE to simulate the host completing USB bus enumeration.
     * Gate on phase==1 (set when DCTL=0xd00 is written) and GINTSTS==0
     * (firmware is idle, waiting for an event).
     */
    /*
     * USB host enumeration injection state machine (GINTSTS idle poll trigger).
     *
     * Phase map:
     *  1         - poll for ENUMDONE (20 idle reads)
     *  2         - ENUMDONE pending (cleared in GINTSTS write handler)
     *  3         - SETUP[0] GET_DESCRIPTOR Device(64) active
     *  4         - INEP XferCompl pending (d2h)
     *  5         - STATUS OUT + idle: poll to inject SETUP[1]
     *  6         - SETUP[1] SET_ADDRESS active
     *  7         - ZLP IN pending
     *  8         - idle: poll to inject SETUP[2]
     *  9         - SETUP[2] GET_DESCRIPTOR Device(18) active
     *  10        - INEP XferCompl pending (d2h)
     *  11        - STATUS OUT + idle: poll to inject SETUP[3]
     *  12        - SETUP[3] GET_DESCRIPTOR Config(255) active
     *  13        - INEP XferCompl pending (d2h)
     *  14        - STATUS OUT + idle: poll to inject SETUP[4]
     *  15        - SETUP[4] SET_CONFIGURATION(1) active
     *  16        - ZLP IN pending
     *  17        - MSC active (enumeration complete)
     */
    if (offset == GINTSTS && s->gintsts == 0 && !s->usbip_device_imported) {
        /* Lookup table: {phase, threshold, setup_bytes, description} */
        static const struct {
            int phase;
            int threshold;
            uint8_t setup[8];
            const char *name;
        } inject_table[] = {
            /* phase 5: inject SET_ADDRESS(1) */
            { 5,  5, { 0x00, 0x05, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 }, "SET_ADDRESS(1)" },
            /* phase 8: inject GET_DESCRIPTOR Device (wLength=18) */
            { 8,  5, { 0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x12, 0x00 }, "GET_DESCRIPTOR Device(18)" },
            /* phase 11: inject GET_DESCRIPTOR Config (wLength=255) */
            { 11, 5, { 0x80, 0x06, 0x00, 0x02, 0x00, 0x00, 0xFF, 0x00 }, "GET_DESCRIPTOR Config(255)" },
            /* phase 14: inject SET_CONFIGURATION(1) */
            { 14, 5, { 0x00, 0x09, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 }, "SET_CONFIGURATION(1)" },
        };

        if (s->enumeration_phase == 1) {
            s->gintsts_poll_count++;
            if (s->gintsts_poll_count >= 20) {
                s->gintsts |= (1 << 13);
                s->enumeration_phase = 2;
                trace_s5l8702_usbotg_enumdone_injected(s->gintsts_poll_count);
            }
        } else {
            for (int ti = 0; ti < (int)(sizeof(inject_table)/sizeof(inject_table[0])); ti++) {
                if (s->enumeration_phase == inject_table[ti].phase) {
                    s->gintsts_poll_count++;
                    if (s->gintsts_poll_count >= inject_table[ti].threshold) {
                        uint32_t dma_addr = s->out_eps[0].dma_address;
                        cpu_physical_memory_write(dma_addr, inject_table[ti].setup, 8);
                        s->out_eps[0].tx_size = (s->out_eps[0].tx_size & ~0x7f) | 0x38;
                        s->out_eps[0].interrupt_status |= USB_EPINT_SetUp;
                        s->pcgcctl = 0;
                        s->enumeration_phase++;
                        s->gintsts_poll_count = 0;
                        trace_s5l8702_usbotg_setup_injected(inject_table[ti].name,
                                     inject_table[ti].phase, inject_table[ti].phase + 1);
                    }
                    break;
                }
            }
        }
    }

    switch (offset) {
        case PCGCCTL: val = s->pcgcctl; break;
        case GOTGCTL: val = s->gotgctl; break;
        case GOTGINT: val = s->gotgint; break;
        case GRSTCTL: val = s->grstctl; break;
        case GHWCFG1: val = s->ghwcfg1; break;
        case GHWCFG2: val = s->ghwcfg2; break;
        case GHWCFG3: val = s->ghwcfg3; break;
        case GHWCFG4: val = s->ghwcfg4; break;
        case GAHBCFG: val = s->gahbcfg; break;
        case GUSBCFG: val = s->gusbcfg; break;
        case GINTMSK: val = s->gintmsk; break;
        case GINTSTS:
            val = s->gintsts;
            if (val != 0) {
                /* GINTSTS polled frequently - not logged */
            }
            break;
        case DIEPMSK: val = s->diepmsk; break;
        case DOEPMSK: val = s->doepmsk; break;
        case DAINTMSK: val = s->daintmsk; break;
        case DAINTSTS: val = s->daintsts; break;
        case DCTL: val = s->dctl; break;
        case DCFG: val = s->dcfg; break;
        case DSTS: val = s->dsts; break;
        case GRXSTSR:
        case GRXSTSP: val = 0; break;
        case GNPTXFSTS: val = 0xFFFFFFFF; break;
        case GRXFSIZ: val = s->grxfsiz; break;
        case GNPTXFSIZ: val = s->gnptxfsiz; break;

        case 0x440: {
            /* Host Port Status Register (HPRT0)
             * Bit 0: Port connect status
             * Bits [15:13]: Port speed (0=HS, 1=FS, 2=LS)
             * We simulate an always-connected high-speed port (matches real device)
             */
            val = (1 << 0) |   /* Port connected */
                  (0 << 13);   /* High-speed (bits [15:13] = 0) */
            break;
        }

        /* DIEPTXF registers */
        case DIEPTXF(1) ... DIEPTXF(USB_NUM_FIFOS + 1): {
            uint32_t idx = (offset - DIEPTXF(1)) >> 2;
            val = s->dptxfsiz[idx];
            break;
        }

        /* IN endpoint registers */
        case USB_INREGS ... (USB_INREGS + USB_EPREGS_SIZE - 4): {
            hwaddr ep_offset = offset - USB_INREGS;
            val = s5l8702_usbotg_in_ep_read(s, ep_offset >> 5, ep_offset & 0x1f);
            break;
        }

        /* OUT endpoint registers */
        case USB_OUTREGS ... (USB_OUTREGS + USB_EPREGS_SIZE - 4): {
            hwaddr ep_offset = offset - USB_OUTREGS;
            val = s5l8702_usbotg_out_ep_read(s, ep_offset >> 5, ep_offset & 0x1f);
            break;
        }

        /* FIFOs */
        case USB_FIFO_START ... (USB_FIFO_END - 4): {
            hwaddr fifo_offset = offset - USB_FIFO_START;
            val = *(uint32_t *)(&s->fifos[fifo_offset]);
            break;
        }

        default:
            qemu_log_mask(LOG_GUEST_ERROR, "USB: Unhandled read at offset 0x%x\n", (unsigned)offset);
            val = 0;
    }

    return val;
}

/* Patch USB descriptors for high-speed compatibility.
 * The bootrom firmware advertises 64-byte max packet size, which is invalid for
 * high-speed devices (must be 512). We intercept and fix it transparently. */
static void patch_descriptor_for_highspeed(uint8_t *data, uint32_t len)
{
    /* Config descriptor structure (32 bytes):
     * 0-8:   Config descriptor (9 bytes)
     * 9-17:  Interface descriptor (9 bytes)
     * 18-24: Endpoint descriptor 1 (7 bytes)
     *   22-23: wMaxPacketSize
     * 25-31: Endpoint descriptor 2 (7 bytes)
     *   29-30: wMaxPacketSize
     */

    if (len != 32 || data[1] != 0x02) {
        return;  /* Not a full config descriptor, skip */
    }

    /* Verify this is the structure we expect */
    if (data[9] != 0x09 || data[9 + 1] != 0x04 ||  /* Interface descriptor */
        data[18] != 0x07 || data[18 + 1] != 0x05 ||  /* Endpoint 1 */
        data[25] != 0x07 || data[25 + 1] != 0x05) {  /* Endpoint 2 */
        return;  /* Unexpected structure, don't patch */
    }

    /* Patch endpoint 1 wMaxPacketSize: 64 bytes → 512 bytes (little-endian) */
    data[22] = 0x00;
    data[23] = 0x02;

    /* Patch endpoint 2 wMaxPacketSize: 64 bytes → 512 bytes (little-endian) */
    data[29] = 0x00;
    data[30] = 0x02;

    trace_s5l8702_usbotg_descriptor_patched();
}

static void s5l8702_usbotg_in_ep_write(S5L8702UsbOtgState *s, uint8_t ep, hwaddr offset, uint32_t val)
{
    if (ep >= USB_NUM_ENDPOINTS) {
        qemu_log_mask(LOG_GUEST_ERROR, "USB: Write to disabled IN EP %d\n", ep);
        return;
    }

    switch (offset) {
        case 0x00:
            s->in_eps[ep].control = val;
            s5l8702_usbotg_update_ep(s, &s->in_eps[ep]);

            if (val & USB_EPCON_STALL) {
                if (!s->usbip_in_ep_halted[ep]) {
                    trace_s5l8702_usbotg_in_halted(ep);
                }
                s->usbip_in_ep_halted[ep] = true;
                if (s->usbip_device_imported) {
                    usbip_reject_stalled(s, ep, true);
                }
                break;
            } else if (s->usbip_in_ep_halted[ep]) {
                trace_s5l8702_usbotg_in_halt_cleared(ep);
                s->usbip_in_ep_halted[ep] = false;
            }

            if (val & USB_EPCON_ENABLE) {
                if (!s->usbip_device_imported) {
                    /* === Standalone enumeration injection ===
                     * Phases 3,6,9,12,15 = DIEPCTL ENABLE detected.
                     * Immediately fire XferCompl to simulate host ACK. */
                    if (ep == 0 &&
                        s->enumeration_phase >= 3 && s->enumeration_phase <= 15 &&
                        (s->enumeration_phase % 3) == 0) {
                        s->in_eps[0].interrupt_status |= USB_EPINT_XferCompl;
                        s->enumeration_phase++;
                        trace_s5l8702_usbotg_inep_xfercompl_injected(
                            s->enumeration_phase - 1, s->enumeration_phase);
                        s5l8702_usbotg_update_irq(s);
                    }
                } else {
                    /* === USB/IP mode: fulfill pending IN requests ===  */
                    if (ep == 0 && s->usbip_ep0_pending && s->usbip_ep0_d2h
                        && s->usbip_ep0_txsize_armed) {
                        /* d2h control transfer: read data from DIEPDMA[0], send RET_SUBMIT */
                        uint32_t len = s->in_eps[0].tx_size & 0x7f;  /* EP0: 7-bit XFERSIZE */
                        if (len > 64) len = 64;
                        uint8_t data[64];
                        cpu_physical_memory_read(s->in_eps[0].dma_address, data, len);

                        /* Patch descriptor for high-speed compatibility */
                        patch_descriptor_for_highspeed(data, len);

                        trace_s5l8702_usbotg_ep0_data_in(len);
                        if (!usbip_send_ret_submit(s, s->usbip_ep0_seqnum, data, len)) {
                            usbip_disconnect(s);
                            return;
                        }

                        /* Update tx_size (XferSize bits 6:0, PktCnt bits 20:19) and dma_address */
                        uint32_t pkt_cnt = (s->in_eps[0].tx_size >> 19) & 0x3;
                        if (pkt_cnt > 0) pkt_cnt--;
                        s->in_eps[0].tx_size = (s->in_eps[0].tx_size & ~0x18007F) | (pkt_cnt << 19);
                        s->in_eps[0].dma_address += len;

                        s->usbip_ep0_pending = false;
                        /* Fire INEP XferCompl so firmware clears it.
                         * Track that it's outstanding - the fd handler must not
                         * re-arm until BOTH this AND the STATUS OUT XferCompl have
                         * been cleared, otherwise a SET_ADDRESS SETUP injected
                         * between the two clears trips the h2d case 0x08 path. */
                        s->usbip_inep_active = true;
                        s->in_eps[0].interrupt_status  |= USB_EPINT_XferCompl;
                        /* Clear ENABLE bit - transfer is done */
                        s->in_eps[0].control &= ~USB_EPCON_ENABLE;
                        /* Fire STATUS OUT XferCompl so firmware completes status stage. */
                        s->out_eps[0].interrupt_status |= USB_EPINT_XferCompl;
                        s->usbip_ep0_status_pending = true;
                        s5l8702_usbotg_update_irq(s);
                        } else if (ep == 0 && s->usbip_ep0_pending && !s->usbip_ep0_d2h) {
                        /* h2d control: firmware enabling IN EP0 for ZLP status.
                         * Fire XferCompl; RET_SUBMIT sent when firmware clears it. */
                        s->in_eps[0].interrupt_status |= USB_EPINT_XferCompl;
                        /* Clear ENABLE bit - transfer is done */
                        s->in_eps[0].control &= ~USB_EPCON_ENABLE;
                        s5l8702_usbotg_update_irq(s);
                        }
 else if (ep != 0) {
                        /* Bulk/interrupt IN endpoint enabled by firmware.
                         *
                         * On real DWC2 hardware, ENABLE starts a DMA transfer.
                         * XferCompl fires only AFTER the host polls with IN tokens
                         * and actually receives the data.  We mirror this:
                         *
                         *  - If the host already sent CMD_SUBMIT (pending): the host
                         *    is waiting for data, so fulfill now and fire XferCompl.
                         *  - If no CMD_SUBMIT yet: mark armed.  When the host's
                         *    CMD_SUBMIT arrives later, fulfill then.
                         *
                         * We never fire XferCompl without actually delivering data
                         * to the host - doing so would desync the firmware's state
                         * machine (it would think data was sent when it wasn't).
                         */
                        uint32_t cur_txsize = s->in_eps[ep].tx_size;
                        bool is_fresh = s->usbip_in_dma_fresh[ep] || s->usbip_in_tsiz_fresh[ep];

                        if (is_fresh) {
                            /* Real ENABLE with a freshly-written DIEPDMA or DIEPTSIZ - 
                             * the buffer or size is current. Either fulfill an 
                             * already-pending CMD_SUBMIT or arm for the next one. */
                            s->usbip_in_dma_fresh[ep] = false;
                            s->usbip_in_tsiz_fresh[ep] = false;
                            s->usbip_in_ep_xfercompl_pending[ep] = false;

                            if (s->usbip_in_pending[ep].pending) {
                                /* CMD_SUBMIT already waiting - consume this
                                 * arm into it (completes the URB only on a
                                 * short packet or a full host buffer). */
                                trace_s5l8702_usbotg_in_deferred(ep, cur_txsize & 0x7ffff);
                                if (!usbip_in_try_fulfill(s, ep)) {
                                    usbip_disconnect(s);
                                    return;
                                }
                            } else {
                                /* No CMD_SUBMIT yet - arm.  When CMD_SUBMIT arrives
                                 * it can fulfill immediately. */
                                s->usbip_in_ep_armed[ep] = true;
                                trace_s5l8702_usbotg_in_armed(ep, cur_txsize & 0x7ffff);
                            }
                        } else if (s->usbip_in_ep_xfercompl_pending[ep]) {
                            /* Maintenance re-arm: firmware re-enables the endpoint
                             * inside its XferCompl interrupt handler before updating
                             * the DMA buffer.  The firmware requires another XferCompl
                             * here to advance its state machine. Fire XferCompl 
                             * but do NOT fulfill any CMD_SUBMIT. */
                            s->usbip_in_ep_xfercompl_pending[ep] = false;
                            s->in_eps[ep].interrupt_status |= USB_EPINT_XferCompl;
                            /* Clear ENABLE bit - maintenance is done */
                            s->in_eps[ep].control &= ~USB_EPCON_ENABLE;
                            s5l8702_usbotg_update_irq(s);
                            trace_s5l8702_usbotg_in_maintenance(ep);
                        } else {
                            /* ENABLE without a fresh DIEPDMA/DIEPTSIZ write - ignore. */
                            trace_s5l8702_usbotg_in_enable_ignored(ep,
                                s->usbip_in_pending[ep].pending);
                        }
                    }
                }
            }
            break;

        case 0x08:
            s->in_eps[ep].interrupt_status &= ~val;

            if (!s->usbip_device_imported) {
                /* === Standalone enumeration injection ===
                 * When firmware clears INEP XferCompl on EP0, determine if a
                 * STATUS OUT ZLP is needed (d2h: phases 4,10,13) or not (h2d: 7,16). */
                if (ep == 0 && (val & USB_EPINT_XferCompl)) {
                    if (s->enumeration_phase == 4 ||
                        s->enumeration_phase == 10 ||
                        s->enumeration_phase == 13) {
                        s->out_eps[0].interrupt_status |= USB_EPINT_XferCompl;
                        s->enumeration_phase++;
                        s->gintsts_poll_count = 0;
                        trace_s5l8702_usbotg_status_out_injected(
                            s->enumeration_phase - 1, s->enumeration_phase);
                    } else if (s->enumeration_phase == 7 || s->enumeration_phase == 16) {
                        s->enumeration_phase++;
                        s->gintsts_poll_count = 0;
                        trace_s5l8702_usbotg_zlp_cleared(
                            s->enumeration_phase - 1, s->enumeration_phase);
                        if (s->enumeration_phase == 17) {
                            trace_s5l8702_usbotg_enum_complete();
                        }
                    }
                }
            } else {
                /* === USB/IP mode === */
                if (ep == 0 && (val & USB_EPINT_XferCompl)) {
                    if (s->usbip_inep_active) {
                        /* d2h post-transfer: firmware clearing the INEP XferCompl we
                         * injected for the STATUS phase.  Re-arm the fd handler only
                         * if the STATUS OUT XferCompl has already been cleared; otherwise
                         * leave it for out_ep_write to do when that happens. */
                        s->usbip_inep_active = false;
                        if (!s->usbip_ep0_status_pending && s->usbip_client_fd >= 0) {
                            qemu_set_fd_handler(s->usbip_client_fd,
                                                usbip_client_readable, NULL, s);
                        }
                    } else if (s->usbip_ep0_pending && !s->usbip_ep0_d2h) {
                        /* h2d control transfer: firmware cleared ZLP IN XferCompl */
                        if (!usbip_send_ret_submit(s, s->usbip_ep0_seqnum, NULL, 0)) {
                            usbip_disconnect(s);
                            return;
                        }
                        s->usbip_ep0_pending = false;
                        if (s->usbip_client_fd >= 0) {
                            qemu_set_fd_handler(s->usbip_client_fd,
                                                usbip_client_readable, NULL, s);
                        }
                    }
                }
            }
            s5l8702_usbotg_update_irq(s);
            break;
        case 0x10:
            s->in_eps[ep].tx_size = val;
            if (ep != 0) {
                s->usbip_in_tsiz_fresh[ep] = true;
                trace_s5l8702_usbotg_in_tsiz_fresh(ep, val);
            }
            /* Track that firmware wrote DIEPTSIZ[0] - real transfer setup */
            if (ep == 0 && s->usbip_ep0_pending && s->usbip_ep0_d2h) {
                s->usbip_ep0_txsize_armed = true;
            }
            break;
        case 0x14:
            s->in_eps[ep].dma_address = val;
            if (ep != 0) {
                s->usbip_in_dma_fresh[ep] = true;
                trace_s5l8702_usbotg_in_dma_fresh(ep, val);
            }
            break;
        case 0x1C:
            s->in_eps[ep].dma_buffer = val;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "USB: Bad IN EP write offset 0x%x\n", (unsigned)offset);
    }
}

static void s5l8702_usbotg_out_ep_write(S5L8702UsbOtgState *s, uint8_t ep, hwaddr offset, uint32_t val)
{
    if (ep >= USB_NUM_ENDPOINTS) {
        qemu_log_mask(LOG_GUEST_ERROR, "USB: Write to disabled OUT EP %d\n", ep);
        return;
    }

    switch (offset) {
        case 0x00:
            s->out_eps[ep].control = val;
            s5l8702_usbotg_update_ep(s, &s->out_eps[ep]);

            if (val & USB_EPCON_STALL) {
                if (!s->usbip_out_ep_halted[ep]) {
                    trace_s5l8702_usbotg_out_halted(ep);
                }
                s->usbip_out_ep_halted[ep] = true;
                if (s->usbip_device_imported) {
                    usbip_reject_stalled(s, ep, false);
                }
                break;
            } else if (s->usbip_out_ep_halted[ep]) {
                trace_s5l8702_usbotg_out_halt_cleared(ep);
                s->usbip_out_ep_halted[ep] = false;
            }

            /* Track arming even before a USB/IP client imports the device:
             * firmware typically arms its bulk OUT EP right after
             * SET_CONFIGURATION, which can precede the client attach. */
            if (ep != 0) {
                if (val & USB_EPCON_DISABLE) {
                    s->usbip_out_ep_armed[ep] = false;
                } else if (val & USB_EPCON_ENABLE) {
                    /* For OUT endpoints ENABLE means "buffer at DOEPDMA is
                     * ready to receive" - arm unconditionally (level
                     * semantics; fresh flags kept only for diagnostics). */
                    s->usbip_out_ep_armed[ep] = true;
                    trace_s5l8702_usbotg_out_armed(ep, s->out_eps[ep].tx_size & 0x7ffff,
                                   s->out_eps[ep].dma_address);
                    s->usbip_out_dma_fresh[ep] = false;
                    s->usbip_out_tsiz_fresh[ep] = false;
                    usbip_out_try_deliver(s, ep);
                }
            }
            break;
        case 0x08:
            s->out_eps[ep].interrupt_status &= ~val;
            /* When firmware clears DOEPINT[0] XferCompl (STATUS OUT stage),
             * it has just re-armed OUT EP0 with a fresh DMA buffer.
             * Only re-arm the fd handler once BOTH this AND the DIEPINT[0]
             * XferCompl have been cleared; otherwise leave it for in_ep_write
             * to finish (prevents a race where a new SET_ADDRESS SETUP injected
             * between the two clears trips the h2d case 0x08 handler). */
            if (ep == 0 && (val & USB_EPINT_XferCompl)) {
                if (s->usbip_device_imported && s->usbip_ep0_status_pending) {
                    s->usbip_ep0_status_pending = false;
                    if (!s->usbip_inep_active && s->usbip_client_fd >= 0) {
                        qemu_set_fd_handler(s->usbip_client_fd,
                                            usbip_client_readable, NULL, s);
                    }
                }
            }
            s5l8702_usbotg_update_irq(s);
            break;
        case 0x10:
            s->out_eps[ep].tx_size = val;
            if (ep != 0) {
                s->usbip_out_tsiz_fresh[ep] = true;
            }
            break;
        case 0x14:
            s->out_eps[ep].dma_address = val;
            if (ep != 0) {
                s->usbip_out_dma_fresh[ep] = true;
            }
            /* If OUT EP0 DMA set and USB/IP waiting, try to arm fd handler.
             * This handles the case where DOEPDMA[0] is written after ENUMDONE
             * has already been cleared. */
            if (ep == 0 && val != 0) {
                usbip_maybe_arm_fd(s);
            }
            break;
        case 0x1C:
            s->out_eps[ep].dma_buffer = val;
            break;
        default:
            qemu_log_mask(LOG_GUEST_ERROR, "USB: Bad OUT EP write offset 0x%x\n", (unsigned)offset);
    }
}

static void s5l8702_usbotg_write(void *opaque, hwaddr offset, uint64_t val, unsigned size)
{
    S5L8702UsbOtgState *s = S5L8702_USBOTG(opaque);
    uint32_t value = (uint32_t)val;

    trace_s5l8702_usbotg_reg_write(offset_name(offset), value);

    switch (offset) {
        case PCGCCTL:
            s->pcgcctl = value;
            s5l8702_usbotg_update_irq(s);
            break;

        case GOTGCTL:
            s->gotgctl = value;
            break;

        case GOTGINT:
            s->gotgint &= ~value;
            s5l8702_usbotg_update_irq(s);
            break;

        case GRSTCTL:
            if (value & 0x1) {  /* Core soft reset - firmware's own init, not a host bus reset */
                s->grstctl = (1 << 31);  /* AHB idle, reset complete */
                /* Do NOT set GINTSTS USBRST here: this is the firmware's internal
                 * USB core reset, not a USB host bus reset. The ISR (now correctly
                 * wired to VIC0 IRQ19) would fire and cause firmware to loop if we
                 * inject USBRST at this point. ENUMDONE is injected separately
                 * after firmware enters its idle polling loop. */
            } else if (value == 0) {
                s->grstctl = 0;
            }
            break;

        case GINTMSK:
            s->gintmsk = value;
            s5l8702_usbotg_update_irq(s);
            break;

        case GINTSTS:
            if ((value & (1 << 13)) && (s->gintsts & (1 << 13)) &&
                s->enumeration_phase == 2) {
                if (!s->usbip_device_imported) {
                    /*
                     * Standalone mode: firmware clearing ENUMDONE (phase 2).
                     * Inject GET_DESCRIPTOR Device(64) SETUP immediately.
                     */
                    static const uint8_t setup_pkt[8] = {
                        0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x40, 0x00,
                    };
                    uint32_t dma_addr = s->out_eps[0].dma_address;
                    cpu_physical_memory_write(dma_addr, setup_pkt, sizeof(setup_pkt));
                    s->out_eps[0].tx_size = (s->out_eps[0].tx_size & ~0x7f) | 0x38;
                    s->out_eps[0].interrupt_status |= USB_EPINT_SetUp;
                    s->pcgcctl = 0;
                    s->enumeration_phase = 3;
                    trace_s5l8702_usbotg_get_desc_injected(dma_addr);
                } else {
                    /*
                     * USB/IP mode: firmware clearing ENUMDONE signals that it has
                     * (or will imminently) arm OUT EP0 with a valid DMA buffer.
                     * Try to arm the fd handler now; if DOEPDMA[0] isn't set yet,
                     * usbip_maybe_arm_fd will wait for the next DOEPDMA write.
                     */
                    s->enumeration_phase = 3;  /* "USB/IP ready" */
                    usbip_maybe_arm_fd(s);
                }
            }
            s->gintsts &= ~value;
            s5l8702_usbotg_update_irq(s);
            break;

        case DOEPMSK:
            s->doepmsk = value;
            s5l8702_usbotg_update_irq(s);
            break;

        case DIEPMSK:
            s->diepmsk = value;
            s5l8702_usbotg_update_irq(s);
            break;

        case DAINTMSK:
            s->daintmsk = value;
            s5l8702_usbotg_update_irq(s);
            break;

        case DAINTSTS:
            s->daintsts &= ~value;
            s5l8702_usbotg_update_irq(s);
            break;

        case GAHBCFG:
            s->gahbcfg = value;
            break;

        case GUSBCFG:
            s->gusbcfg = value;
            break;

        case 0x440:
            /* Host Port Status Register - most bits are read-only
             * Some bits are write-to-clear (like port connect detected at bit 1)
             */
            break;

        case DCTL: {
            if (((value & (1 << 7)) != (s->dctl & (1 << 7))) && (value & (1 << 7))) {
                s->gintsts |= (1 << 6);  /* GINNAKEFF */
                value &= ~(1 << 7);
            }
            if (((value & (1 << 9)) != (s->dctl & (1 << 9))) && (value & (1 << 9))) {
                s->gintsts |= (1 << 7);  /* GOUTNAKEFF */
                value &= ~(1 << 9);
            }
            s->dctl = value;

            /* DCTL = 0xd00: CGNPInNAK | CGOUTNak | ProgDone, SftDiscon clear.
             * Firmware just finished USB core init and connected (D+ pullup active). */
            if ((value & 0x2) == 0 && (value & 0x800) != 0 && s->enumeration_phase == 0) {
                s->gintsts_poll_count = 0;
                if (s->usbip_device_imported) {
                    /* USB/IP already imported: inject ENUMDONE immediately */
                    s->gintsts |= (1 << 13);
                    s->enumeration_phase = 2;
                    /* Re-arm fd handler after reset - client is waiting for us */
                    if (s->usbip_client_fd >= 0) {
                        qemu_set_fd_handler(s->usbip_client_fd, usbip_client_readable, NULL, s);
                    }
                    trace_s5l8702_usbotg_connected(1);
                    s5l8702_usbotg_update_irq(s);
                } else {
                    s->enumeration_phase = 1;
                    trace_s5l8702_usbotg_connected(0);
                }
            }

            s5l8702_usbotg_update_irq(s);
            break;
        }

        case DCFG:
            s->dcfg = value;
            break;

        case GRXFSIZ:
            s->grxfsiz = value;
            break;

        case GNPTXFSIZ:
            s->gnptxfsiz = value;
            break;

        case DIEPTXF(1) ... DIEPTXF(USB_NUM_FIFOS + 1): {
            uint32_t idx = (offset - DIEPTXF(1)) >> 2;
            s->dptxfsiz[idx] = value;
            break;
        }

        /* IN endpoint registers */
        case USB_INREGS ... (USB_INREGS + USB_EPREGS_SIZE - 4): {
            hwaddr ep_offset = offset - USB_INREGS;
            s5l8702_usbotg_in_ep_write(s, ep_offset >> 5, ep_offset & 0x1f, value);
            break;
        }

        /* OUT endpoint registers */
        case USB_OUTREGS ... (USB_OUTREGS + USB_EPREGS_SIZE - 4): {
            hwaddr ep_offset = offset - USB_OUTREGS;
            s5l8702_usbotg_out_ep_write(s, ep_offset >> 5, ep_offset & 0x1f, value);
            break;
        }

        /* FIFOs */
        case USB_FIFO_START ... (USB_FIFO_END - 4): {
            hwaddr fifo_offset = offset - USB_FIFO_START;
            *(uint32_t *)(&s->fifos[fifo_offset]) = value;
            break;
        }

        default:
            qemu_log_mask(LOG_GUEST_ERROR, "USB: Unhandled write at offset 0x%x = 0x%x\n",
                         (unsigned)offset, value);
    }
}

static const MemoryRegionOps s5l8702_usbotg_ops = {
    .read = s5l8702_usbotg_read,
    .write = s5l8702_usbotg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void s5l8702_usbotg_reset(DeviceState *dev)
{
    S5L8702UsbOtgState *s = S5L8702_USBOTG(dev);

    s->pcgcctl = 3;  /* Default: clocks gated (firmware enables/disables as needed) */
    s->gahbcfg = 0;
    s->gusbcfg = 0;
    s->dctl = 0;
    s->dcfg = 0;
    /* Set DSTS to indicate device is connected and configured
     * Bits 3-1: Enumeration speed (1=full speed)
     * Bit 0: Suspend status (0=not suspended)
     * This helps firmware know device is ready
     */
    s->dsts = (0 << 1) | 0;  /* High speed (0=HS, 1=FS), not suspended */
    s->gotgctl = GOTGCTL_BSESSIONVALID;  /* B-session valid: firmware checks this after SETUP */
    s->gotgint = 0;
    s->gintmsk = 0;
    s->gintsts = 0;
    s->daintmsk = 0;
    s->daintsts = 0;
    s->diepmsk = 0;
    s->doepmsk = 0;

    s->grxfsiz = 0x100;
    s->gnptxfsiz = (0x100 << 16) | 0x100;

    /* Initialize TX FIFO sizes */
    uint32_t fifo_start = 0x200;
    for (int i = 0; i < USB_NUM_FIFOS; i++) {
        s->dptxfsiz[i] = (fifo_start << 16) | 0x100;
        fifo_start += 0x100;
    }

    /* Initialize hardware config registers
     * These describe the USB core capabilities.
     * GHWCFG1: Endpoint info
     * GHWCFG2: Hardware config
     * GHWCFG3: User HW config
     * GHWCFG4: User HW config
     */
    s->ghwcfg1 = 0;  /* All endpoints are bidirectional (0 = bidir) */

    /* GHWCFG2: Mode=Device (bit 0-2), 8 endpoints, 64KB RxFIFO, etc */
    s->ghwcfg2 = (2 << 0) |           /* Mode: Device only (bits 0-2) */
                 (8 << 10) |          /* Num endpoints: 8 (bits 13-10) */
                 (0 << 26);           /* Token queue depth (bits 29-26) */

    /* GHWCFG3: DMA enabled, etc */
    s->ghwcfg3 = 0;

    /* GHWCFG4: DMA enabled */
    s->ghwcfg4 = (1 << 25);  /* DED_FIFO_EN */

    /* Initialize endpoints */
    for (int i = 0; i < USB_NUM_ENDPOINTS; i++) {
        s->in_eps[i].control = 0;
        s->in_eps[i].interrupt_status = 0;
        s->in_eps[i].tx_size = 0;
        s->in_eps[i].dma_address = 0;
        s->in_eps[i].dma_buffer = 0;

        s->out_eps[i].control = 0;
        s->out_eps[i].interrupt_status = 0;
        s->out_eps[i].tx_size = 0;
        s->out_eps[i].dma_address = 0;
        s->out_eps[i].dma_buffer = 0;
    }

    /* Reset enumeration simulation state */
    s->enumeration_started = false;
    s->enumeration_phase = 0;
    s->gintsts_poll_count = 0;

    /* Reset USB/IP control transfer state BUT KEEP CLIENT CONNECTED.
     * In real USB/IP, a bus reset doesn't disconnect the TCP connection;
     * the device re-enumerates on the same link. Closing the fd here causes
     * vhci_hcd to reconnect, creating a reset loop. */
    if (s->usbip_client_fd >= 0) {
        qemu_set_fd_handler(s->usbip_client_fd, NULL, NULL, NULL);
        /* DO NOT close(s->usbip_client_fd) - keep connection alive */
    }
    /* Keep s->usbip_device_imported = true (don't reset it) */
    s->usbip_ep0_pending = false;
    s->usbip_ep0_d2h = false;
    s->usbip_ep0_seqnum = 0;
    s->usbip_ep0_status_pending = false;
    s->usbip_inep_active = false;
    s->usbip_ep0_txsize_armed = false;
    for (int i = 0; i < USB_NUM_ENDPOINTS; i++) {
        g_free(s->usbip_in_pending[i].acc_data);
    }
    memset(s->usbip_in_pending, 0, sizeof(s->usbip_in_pending));
    memset(s->usbip_in_ep_armed, 0, sizeof(s->usbip_in_ep_armed));
    memset(s->usbip_in_ep_xfercompl_pending, 0, sizeof(s->usbip_in_ep_xfercompl_pending));
    memset(s->usbip_in_dma_fresh, 0, sizeof(s->usbip_in_dma_fresh));
    memset(s->usbip_in_tsiz_fresh, 0, sizeof(s->usbip_in_tsiz_fresh));
    for (int i = 0; i < USB_NUM_ENDPOINTS; i++) {
        g_free(s->usbip_out_pending[i].data);
    }
    memset(s->usbip_out_pending, 0, sizeof(s->usbip_out_pending));
    memset(s->usbip_out_ep_armed, 0, sizeof(s->usbip_out_ep_armed));
    memset(s->usbip_out_dma_fresh, 0, sizeof(s->usbip_out_dma_fresh));
    memset(s->usbip_out_tsiz_fresh, 0, sizeof(s->usbip_out_tsiz_fresh));
    memset(s->usbip_in_ep_halted, 0, sizeof(s->usbip_in_ep_halted));
    memset(s->usbip_out_ep_halted, 0, sizeof(s->usbip_out_ep_halted));

    s5l8702_usbotg_update_irq(s);
}

static void s5l8702_usbotg_init(Object *obj)
{
    S5L8702UsbOtgState *s = S5L8702_USBOTG(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &s5l8702_usbotg_ops, s,
                          TYPE_S5L8702_USBOTG, S5L8702_USBOTG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    /* Initialize USB/IP fds to -1 before realize (reset also sets these) */
    s->usbip_listen_fd = -1;
    s->usbip_client_fd = -1;
}

static void s5l8702_usbotg_realize(DeviceState *dev, Error **errp)
{
    S5L8702UsbOtgState *s = S5L8702_USBOTG(dev);
    usbip_server_start(s);
}

static void s5l8702_usbotg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->reset   = s5l8702_usbotg_reset;
    dc->realize = s5l8702_usbotg_realize;
}

static const TypeInfo s5l8702_usbotg_types[] = {
    {
        .name          = TYPE_S5L8702_USBOTG,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(S5L8702UsbOtgState),
        .instance_init = s5l8702_usbotg_init,
        .class_init    = s5l8702_usbotg_class_init,
    },
};

DEFINE_TYPES(s5l8702_usbotg_types);
