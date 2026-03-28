#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/irq.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/misc/s5l8702-usbotg.h"
#include "exec/address-spaces.h"
#include "exec/cpu-common.h"
#include "trace.h"

/* Forward declarations */
static void s5l8702_usbotg_update_irq(S5L8702UsbOtgState *s);
static void s5l8702_usbotg_update_ep(S5L8702UsbOtgState *s, S5L8702UsbEpState *ep);

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
        qemu_log_mask(LOG_UNIMP, "USB: IRQ_RAISE - pending=0x%08x mask=0x%08x\n",
                     s->gintsts, s->gintmsk);
        qemu_irq_raise(s->irq);
    } else {
        qemu_log_mask(LOG_UNIMP, "USB: IRQ_LOWER - pcgcctl=0x%x pending=0x%08x mask=0x%08x\n",
                     s->pcgcctl, s->gintsts, s->gintmsk);
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
     *  1         — poll for ENUMDONE (20 idle reads)
     *  2         — ENUMDONE pending (cleared in GINTSTS write handler)
     *  3         — SETUP[0] GET_DESCRIPTOR Device(64) active
     *  4         — INEP XferCompl pending (d2h)
     *  5         — STATUS OUT + idle: poll to inject SETUP[1]
     *  6         — SETUP[1] SET_ADDRESS active
     *  7         — ZLP IN pending
     *  8         — idle: poll to inject SETUP[2]
     *  9         — SETUP[2] GET_DESCRIPTOR Device(18) active
     *  10        — INEP XferCompl pending (d2h)
     *  11        — STATUS OUT + idle: poll to inject SETUP[3]
     *  12        — SETUP[3] GET_DESCRIPTOR Config(255) active
     *  13        — INEP XferCompl pending (d2h)
     *  14        — STATUS OUT + idle: poll to inject SETUP[4]
     *  15        — SETUP[4] SET_CONFIGURATION(1) active
     *  16        — ZLP IN pending
     *  17        — MSC active (enumeration complete)
     */
    if (offset == GINTSTS && s->gintsts == 0) {
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
                qemu_log_mask(LOG_UNIMP, "USB: Injected ENUMDONE after %d idle polls\n",
                             s->gintsts_poll_count);
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
                        qemu_log_mask(LOG_UNIMP, "USB: Injected %s SETUP (phase%d->%d)\n",
                                     inject_table[ti].name,
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
                qemu_log_mask(LOG_UNIMP, "USB: GINTSTS read = 0x%08x (RESET=%d ENUMDONE=%d OEP=%d INEP=%d)\n",
                             (unsigned)val,
                             !!(val & (1 << 12)),  /* RESET */
                             !!(val & (1 << 13)),  /* ENUMDONE */
                             !!(val & (1 << 19)),  /* OEP */
                             !!(val & (1 << 18))); /* INEP */
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
             * Bit 1: Port connect detected
             * Bits 17-13: Port speed (bit 13 = full speed, 14 = high speed)
             * We simulate an always-connected full-speed port
             */
            val = (1 << 0) |   /* Port connected */
                  (0 << 13) |  /* Full speed (0=high, 1=full, 2=low) */
                  (0 << 27);   /* Speed: full speed */
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

    qemu_log_mask(LOG_UNIMP, "USB READ [0x%03x] %s = 0x%08x\n", (unsigned)offset, offset_name(offset), (unsigned)val);

    return val;
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
            /* When IN EP0 is enabled during enumeration, simulate the host
             * receiving the transfer by immediately setting XferCompl.
             * Phases 3,9,12 = data IN (d2h descriptors); phases 6,15 = ZLP IN (h2d status). */
            if (ep == 0 && (val & USB_EPCON_ENABLE) &&
                s->enumeration_phase >= 3 && s->enumeration_phase <= 15 &&
                (s->enumeration_phase % 3) == 0) {
                s->in_eps[0].interrupt_status |= USB_EPINT_XferCompl;
                s->enumeration_phase++;
                qemu_log_mask(LOG_UNIMP,
                    "USB: IN EP0 enabled (phase%d->%d), injecting INEP XferCompl\n",
                    s->enumeration_phase - 1, s->enumeration_phase);
                s5l8702_usbotg_update_irq(s);
            }
            break;
        case 0x08:
            s->in_eps[ep].interrupt_status &= ~val;
            /* When firmware clears INEP XferCompl on EP0 during enumeration, determine
             * if a STATUS OUT ZLP is needed (d2h: phases 4,10,13) or not (h2d: 7,16). */
            if (ep == 0 && (val & USB_EPINT_XferCompl)) {
                if (s->enumeration_phase == 4 ||
                    s->enumeration_phase == 10 ||
                    s->enumeration_phase == 13) {
                    /* d2h: descriptor transfer done, inject STATUS OUT ZLP */
                    s->out_eps[0].interrupt_status |= USB_EPINT_XferCompl;
                    s->enumeration_phase++;
                    s->gintsts_poll_count = 0;
                    qemu_log_mask(LOG_UNIMP,
                        "USB: INEP XferCompl cleared (phase%d->%d), injecting STATUS OUT ZLP\n",
                        s->enumeration_phase - 1, s->enumeration_phase);
                } else if (s->enumeration_phase == 7 || s->enumeration_phase == 16) {
                    /* h2d ZLP: SET_ADDRESS/SET_CONFIGURATION status — no STATUS OUT */
                    s->enumeration_phase++;
                    s->gintsts_poll_count = 0;
                    qemu_log_mask(LOG_UNIMP,
                        "USB: ZLP IN cleared (phase%d->%d), no STATUS OUT\n",
                        s->enumeration_phase - 1, s->enumeration_phase);
                    if (s->enumeration_phase == 17) {
                        qemu_log_mask(LOG_UNIMP,
                            "USB: Enumeration complete! MSC stack should be active.\n");
                    }
                }
            }
            s5l8702_usbotg_update_irq(s);
            break;
        case 0x10:
            s->in_eps[ep].tx_size = val;
            break;
        case 0x14:
            s->in_eps[ep].dma_address = val;
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
            break;
        case 0x08:
            s->out_eps[ep].interrupt_status &= ~val;
            s5l8702_usbotg_update_irq(s);
            break;
        case 0x10:
            s->out_eps[ep].tx_size = val;
            break;
        case 0x14:
            s->out_eps[ep].dma_address = val;
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

    qemu_log_mask(LOG_UNIMP, "USB WRITE [0x%03x] %s = 0x%08x\n", (unsigned)offset, offset_name(offset), (unsigned)value);

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
            if (value & 0x1) {  /* Core soft reset — firmware's own init, not a host bus reset */
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
            /* If firmware enables ENUMDONE interrupt, simulate that enumeration
             * is complete. This helps the firmware progress past initialization. */
            if ((value & (1 << 13)) && !(s->gintsts & (1 << 13))) {
                s->gintsts |= (1 << 13);  /* Set ENUMDONE */
            }
            s5l8702_usbotg_update_irq(s);
            break;

        case GINTSTS:
            if ((value & (1 << 13)) && (s->gintsts & (1 << 13)) &&
                s->enumeration_phase == 2) {
                /*
                 * Firmware is clearing ENUMDONE (phase 2). Inject a SETUP packet
                 * so the USB ISR fires on the next instruction.
                 *
                 * Force pcgcctl=0 so update_irq() raises VIC IRQ immediately —
                 * the firmware's polling loop keeps pcgcctl=1 which would otherwise
                 * suppress the interrupt.
                 */
                static const uint8_t setup_pkt[8] = {
                    0x80, 0x06,  /* bmRequestType=Device-to-Host Standard, bRequest=GET_DESCRIPTOR */
                    0x00, 0x01,  /* wValue=0x0100 (Device Descriptor) */
                    0x00, 0x00,  /* wIndex=0 */
                    0x40, 0x00,  /* wLength=64 */
                };
                uint32_t dma_addr = s->out_eps[0].dma_address;
                cpu_physical_memory_write(dma_addr, setup_pkt, sizeof(setup_pkt));

                /* Simulate controller decremented XFERSIZE after 8-byte SETUP receipt */
                s->out_eps[0].tx_size = (s->out_eps[0].tx_size & ~0x7f) | 0x38;

                /* STUP bit in DOEPINT[0] — update_irq() propagates → DAINTSTS → GINTSTS OEP */
                s->out_eps[0].interrupt_status |= 0x8;

                /* Force clocks on so IRQ can be delivered to ARM this instant */
                s->pcgcctl = 0;
                s->enumeration_phase = 3;
                qemu_log_mask(LOG_UNIMP,
                    "USB: Injected GET_DESCRIPTOR SETUP at 0x%08x, forced pcgcctl=0\n",
                    dma_addr);
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
             * Firmware just finished USB core init and connected (D+ pullup active).
             * Enter phase 1 so we inject ENUMDONE after the idle polling loop starts. */
            if ((value & 0x2) == 0 && (value & 0x800) != 0 && s->enumeration_phase == 0) {
                s->enumeration_phase = 1;
                s->gintsts_poll_count = 0;
                qemu_log_mask(LOG_UNIMP, "USB: DCTL=0x%x (connected), phase=1 — will inject ENUMDONE\n",
                             value);
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
    s->dsts = (1 << 1) | 0;  /* Full speed, not suspended */
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

    /* Device starts in basic configured state
     * Firmware will override these values during initialization.
     * We just ensure device appears to be operating.
     */
    s->dsts = (1 << 1);  /* Full speed, not suspended */

    s5l8702_usbotg_update_irq(s);
}

static void s5l8702_usbotg_init(Object *obj)
{
    S5L8702UsbOtgState *s = S5L8702_USBOTG(obj);

    memory_region_init_io(&s->iomem, OBJECT(s), &s5l8702_usbotg_ops, s,
                          TYPE_S5L8702_USBOTG, S5L8702_USBOTG_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static void s5l8702_usbotg_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->reset = s5l8702_usbotg_reset;
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
