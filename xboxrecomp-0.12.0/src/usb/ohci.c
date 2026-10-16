/*
 * ohci.c -- OHCI 1.0a host controller registers for the MCPX.
 *
 * See ohci.h for what this is and is not. The short version: enough for a
 * title's own USB driver to find a controller and a populated root hub port,
 * plus a trace of every register access, because what the driver does after
 * that decides how the rest gets built.
 */
#include "ohci.h"
#include "../platform/mmio_decode.h"
#include "../kernel/xbox_memory_layout.h"
#include "../kernel/kernel.h"
#include "usb_gamepad.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

/* The runtime maps guest memory at a fixed host offset. */
extern ptrdiff_t xbox_GetMemoryOffset(void);

/* Calling the title's interrupt service routine.
 *
 * A recompiled function reads its arguments off the guest stack and keeps its
 * registers in thread-local storage, so it can only be called from a thread
 * that has both. xbox_worker_stack_alloc hands out a guest stack slice for
 * exactly this -- a host thread calling recompiled code -- and recomp_lookup
 * turns a guest address into something callable.
 */
typedef void (*recomp_func_t)(void);
extern recomp_func_t recomp_lookup(uint32_t xbox_va);
extern int  xbox_worker_stack_alloc(void);
extern void xbox_worker_stack_free(int slot);
extern uint32_t xbox_GetConnectedInterrupt(uint32_t vector);

/* Declared in the generated runtime; thread-local, so the values below are
 * this thread's and not the guest thread's. */
#if defined(_MSC_VER)
#  define OHCI_TLS __declspec(thread)
#else
#  define OHCI_TLS __thread
#endif
extern OHCI_TLS uint32_t g_eax, g_ecx, g_edx, g_esp;
extern OHCI_TLS uint32_t g_ebx, g_esi, g_edi;
extern OHCI_TLS uint32_t g_fs_base;
extern uint32_t xbox_AllocThreadTib(void);

/* The vector XPP takes for USB0. HalGetInterruptVector(1) returns 1 here, and
 * the bus interrupt level is what the XDK passes. */
#define OHCI_VECTOR  1

/* ---- OHCI 1.0a operational registers, by byte offset ------------------- */
#define HcRevision              0x00
#define HcControl               0x04
#define HcCommandStatus         0x08
#define HcInterruptStatus       0x0C
#define HcInterruptEnable       0x10
#define HcInterruptDisable      0x14
#define HcHCCA                  0x18
#define HcPeriodCurrentED       0x1C
#define HcControlHeadED         0x20
#define HcControlCurrentED      0x24
#define HcBulkHeadED            0x28
#define HcBulkCurrentED         0x2C
#define HcDoneHead              0x30
#define HcFmInterval            0x34
#define HcFmRemaining           0x38
#define HcFmNumber              0x3C
#define HcPeriodicStart         0x40
#define HcLSThreshold           0x44
#define HcRhDescriptorA         0x48
#define HcRhDescriptorB         0x4C
#define HcRhStatus              0x50
#define HcRhPortStatus1         0x54
/* Through port 4. The root hub here reports two downstream ports, and the
 * driver reads four status registers regardless -- so the file has to cover
 * them or the last one falls outside the array and is answered by the
 * not-a-register path instead of by an empty port. Same value either way,
 * reached honestly. */
#define OHCI_REG_MAX            0x64

/* HcCommandStatus */
#define CS_HCR                  0x00000001u   /* host controller reset       */

/* HcInterruptStatus / Enable */
#define INTR_SO                 0x00000001u   /* scheduling overrun          */
#define INTR_WDH                0x00000002u   /* writeback done head         */
#define INTR_SF                 0x00000004u   /* start of frame              */
#define INTR_RD                 0x00000008u   /* resume detected             */
#define INTR_UE                 0x00000010u   /* unrecoverable error         */
#define INTR_FNO                0x00000020u   /* frame number overflow       */
#define INTR_RHSC               0x00000040u   /* root hub status change      */

/* HccaFrameNumber is at +0x80 and the done head at +0x84. Declared up here
 * because the register write handler retires the done list on a WDH ack. */
#define HCCA_DONE_HEAD 0x84
static void wr32(uint32_t va, uint32_t v);
#define INTR_MIE                0x80000000u   /* master interrupt enable     */

/* HcRhPortStatus */
#define PORT_CCS                0x00000001u   /* current connect status      */
#define PORT_PES                0x00000002u   /* port enable status          */
#define PORT_PSS                0x00000004u   /* port suspend status         */
#define PORT_PPS                0x00000100u   /* port power status           */
#define PORT_LSDA               0x00000200u   /* low speed device attached   */
#define PORT_CSC                0x00010000u   /* connect status change       */
#define PORT_PESC               0x00020000u   /* enable status change        */
#define PORT_PRSC               0x00100000u   /* reset status change         */

/* Writes to HcRhPortStatus set/clear by bit position rather than by value. */
#define PORT_W_CCS_CLEAR_ENABLE 0x00000001u   /* ClearPortEnable             */
#define PORT_W_PES_SET_ENABLE   0x00000002u   /* SetPortEnable               */
#define PORT_W_PRS_SET_RESET    0x00000010u   /* SetPortReset                */
#define PORT_W_PPS_SET_POWER    0x00000100u   /* SetPortPower                */
#define PORT_W_CLEAR_POWER      0x00000200u   /* ClearPortPower              */

/* The MCPX gives each controller a small root hub. Two ports apiece covers
 * the console's four front sockets, which is what a title enumerates over. */
#define OHCI_PORTS              2

/* Passes this thread will keep an interrupt back while the guest is at
 * DISPATCH_LEVEL or above. One pass is the OHCI_TICK_MS (4 ms) loop tick, so
 * this is two seconds.
 *
 * It was 80 ms, on the reasoning that no real critical section lasts longer.
 * True of the hardware and false here: recompiled code under an emulated
 * kernel is slower than the console by a wide and variable margin, and the
 * section XAPI holds while it opens the gamepad's interrupt pipe overran it.
 * The interrupt then landed in the middle of that setup and the title faulted
 * on a half-built structure. Two seconds is still not forever -- a guest that
 * genuinely never lowers is still not allowed to switch the device off -- but
 * it is long enough that a slow critical section is not mistaken for one. */
#define OHCI_IRQ_HOLDOFF        500

/* Milliseconds per pass of the controller thread, which is also how many USB
 * frames a pass is worth.
 *
 * It was 20. A control transfer needs a pass per stage and enumeration needs
 * several transfers, so twenty milliseconds put the whole sequence in the
 * hundreds of milliseconds and left it racing the driver's own timeouts: the
 * same build would enumerate fully on one run and stop half way on the next,
 * with nothing to tell the two apart but how many register reads happened.
 * Four is close enough to a real frame to stop losing that race and still
 * cheap -- this thread does nothing but read a few guest dwords per pass. */
#define OHCI_TICK_MS            4u

typedef struct {
    uint32_t base;                      /* Xbox VA of the register block   */
    uint32_t reg[OHCI_REG_MAX / 4];
    unsigned reads, writes, decode_fail;
    int      index;
    int      periodic_seen;
    int      ple_seen;
    int      hcca_written;   /* HCCA may legitimately be physical 0 */
    /* Bumped every time the driver writes HcInterruptStatus. The interrupt
     * thread watches it to tell a source nobody is servicing from one that
     * is simply busy -- see the delivery loop. */
    volatile unsigned ack_seq;
    volatile int done_unseen;  /* a done list published that the ISR has not been called for yet */
    volatile unsigned wdh_kept; /* WDH clears from elsewhere ignored meanwhile (log) */
} OhciController;

static OhciController s_hc[2];
/* set while this thread is inside the guest's ISR (ohci_call_isr) */
static XBOX_THREAD_LOCAL int s_in_isr;
/* Which controller carries the device, and so which one this thread services.
 * XAPI numbers its four gamepad slots across both controllers, and which end
 * it starts from decides whether a pad shows up as player 1 or player 3 --
 * a mapping worth finding by measurement, not by assertion. */
static int s_device_hc;
/* Downstream ports the root hub reports in HcRhDescriptorA. Two is what the
 * MCPX's own hubs have; RECOMP_USB_NDP exists because which slot XAPI gives a
 * pad is decided somewhere in here and the mapping is worth measuring. */
static unsigned s_ndp = OHCI_PORTS;
static int s_enabled;
static int s_trace;
volatile unsigned g_usb_reports;   /* input reports handed to the title */

static uint32_t *reg_of(OhciController *hc, uint32_t off)
{
    return (off < OHCI_REG_MAX) ? &hc->reg[off / 4] : NULL;
}

/* ---- register semantics ------------------------------------------------ */

static uint64_t ohci_read(void *dev, uint32_t off, int size)
{
    OhciController *hc = (OhciController *)dev;
    uint32_t aligned = off & ~3u;
    uint32_t *r = reg_of(hc, aligned);
    uint32_t v = r ? *r : 0;

    hc->reads++;

    /* HcFmRemaining and HcFmNumber advance on their own. A driver that waits
     * for the frame counter to move is waiting for the controller to be
     * running, and a counter that never changes is a controller that is not.
     * Derived from the read count rather than a timer: it only has to be
     * monotonic, and a timer here would need a thread to be worth having. */
    if (aligned == HcFmNumber)
        v = (hc->reads >> 3) & 0xFFFFu;
    else if (aligned == HcFmRemaining)
        v = (hc->reads * 977u) & 0x3FFFu;

    /* Sub-dword reads take their slice of the containing register. */
    if (size < 4) {
        unsigned shift = (off & 3u) * 8u;
        v >>= shift;
        if (size == 1) v &= 0xFFu;
        else if (size == 2) v &= 0xFFFFu;
    }

    if (s_trace) {
        static unsigned n;
        if (n++ < 600)
            fprintf(stderr, "  [OHCI%d] read  +0x%02X = %08X\n",
                    hc->index, off, (uint32_t)v);
    }
    return v;
}

static void otr_add(char what, uint32_t ed, uint32_t hin, uint32_t tail, uint32_t td,
                    uint32_t tdinfo, uint32_t next, uint32_t hout, uint32_t hnow);
static void ohci_write(void *dev, uint32_t off, uint64_t val, int size)
{
    OhciController *hc = (OhciController *)dev;
    uint32_t aligned = off & ~3u;
    uint32_t *r = reg_of(hc, aligned);
    uint32_t v = (uint32_t)val;

    hc->writes++;
    if (!r)
        return;

    if (s_trace) {
        static unsigned n;
        if (n++ < 600)
            fprintf(stderr, "  [OHCI%d] write +0x%02X = %08X\n",
                    hc->index, off, v);
    }

    if (aligned == HcHCCA)
        hc->hcca_written = 1;
    /* for the stall trace: every register write but the routine WDH acks */
    if (aligned != HcInterruptStatus || (v & ~INTR_WDH))
        otr_add('W', aligned, v, *r, GetCurrentThreadId(), 0, 0, 0, 0);

    switch (aligned) {
    case HcRevision:                    /* read-only */
        return;

    case HcCommandStatus:
        /* HCR is self-clearing: the controller resets and drops the bit, and
         * a driver polls for exactly that. Leaving it set is a hang, and it
         * is the first thing a driver does, so it would be the only thing
         * anyone ever saw of this file. */
        *r |= v;
        if (*r & CS_HCR) {
            *r &= ~CS_HCR;
            hc->reg[HcControl / 4] &= ~0xC0u;    /* back to UsbReset state  */
            hc->reg[HcInterruptStatus / 4] = 0;
            hc->reg[HcInterruptEnable / 4] = 0;
        }
        return;

    case HcInterruptStatus:
        /* Acknowledging WritebackDoneHead retires the done list.
         *
         * The controller publishes the queue in HccaDoneHead and raises WDH;
         * the driver reads the list and clears WDH to say it is finished, and
         * the head must then read as zero. Leaving the old value there is a
         * stale pointer to descriptors that have already been consumed, and a
         * driver that checks the head before trusting the interrupt walks
         * them a second time. That is a plausible way to enumerate a device
         * perfectly and then stop, which is what happened here. */
        /* A WDH clear from another thread for a done list the ISR has not
         * yet been called for.
         *
         * On the console the ISR runs the moment the controller raises the
         * line, so the driver only ever clears WDH for a list its ISR has
         * read. Here the ISR is called from this controller's thread a moment
         * after the list is published, while the driver's DPC runs on the
         * timer thread -- and the DPC writes HcInterruptStatus too (clearing
         * what it handled). A clear landing in that gap retired a list nobody
         * had read: the pad's input transfer on it never completed, the
         * driver never queued the next, and the game kept its last report for
         * good -- the hero walking on, an axe aimed for ever. That WDH is now
         * kept for the ISR, which reads the list and clears it itself. */
        if ((v & INTR_WDH) && (*r & INTR_WDH) && hc->done_unseen && !s_in_isr) {
            v &= ~INTR_WDH;
            if (hc->wdh_kept++ < 8) {
                fprintf(stderr, "  [OHCI%d] WDH clear from another thread before the ISR read the done "
                        "list: kept for the ISR (%u)\n", hc->index, hc->wdh_kept);
                fflush(stderr);
            }
        }
        if ((v & INTR_WDH) && (*r & INTR_WDH)) {
            uint32_t hcca = hc->reg[HcHCCA / 4];
            if (hcca || hc->hcca_written)
                wr32(hcca + HCCA_DONE_HEAD, 0);
            hc->reg[HcDoneHead / 4] = 0;
        }
        *r &= ~v;                       /* write 1 to clear                 */
        hc->ack_seq++;
        return;

    case HcInterruptEnable:
        hc->reg[HcInterruptEnable / 4] |= v;
        return;

    case HcInterruptDisable:
        hc->reg[HcInterruptEnable / 4] &= ~v;
        return;

    case HcFmNumber:
    case HcFmRemaining:
        return;                         /* driven by the controller         */

    case HcRhDescriptorA:
        /* NumberDownstreamPorts is ours; the driver may set the power and
         * over-current policy bits above it. */
        *r = (*r & 0x000000FFu) | (v & ~0x000000FFu);
        return;

    case HcRhPortStatus1:
    case HcRhPortStatus1 + 4:
    case HcRhPortStatus1 + 8:
    case HcRhPortStatus1 + 12: {
        /* Writes here are set/clear requests by bit position, not a value to
         * store. Getting that wrong looks like a port that will not enable.
         *
         * All four, not the two this root hub usually advertises. The read
         * path already answers four, because a driver reads every port
         * register whatever the descriptor says; the write path did not, so a
         * device placed on port 3 was seen, reset, and never reset-completed
         * -- the request fell through to the plain store and PRSC was never
         * raised. The driver reset it forever. A hub that reports N ports has
         * to give all N the same semantics. */
        unsigned port = (aligned - HcRhPortStatus1) / 4;
        uint32_t *ps = &hc->reg[(HcRhPortStatus1 + port * 4) / 4];

        /* Write-1-to-clear first, because the change bits a write carries
         * refer to the state BEFORE it.
         *
         * This ordering is the whole bug it once had. A driver starting a
         * fresh reset writes SetPortReset and ClearPortResetStatusChange in
         * the same word -- "acknowledge the last reset, begin another" --
         * which is both legal and what the Xbox USB stack does: DDS9 writes
         * 0x01100010. Clearing afterwards wiped the PRSC this very write had
         * just raised, so the reset never reported complete, the driver
         * reset the port again, and enumeration looped forever one step from
         * finishing. The port looked healthy the whole time: connected,
         * enabled, powered. */
        *ps &= ~(v & 0xFFFF0000u);

        if (v & PORT_W_CCS_CLEAR_ENABLE) *ps &= ~PORT_PES;
        if (v & PORT_W_PES_SET_ENABLE)   *ps |= (*ps & PORT_CCS) ? PORT_PES : 0;
        if (v & PORT_W_PPS_SET_POWER)    *ps |= PORT_PPS;
        if (v & PORT_W_CLEAR_POWER)      *ps &= ~PORT_PPS;
        if (v & PORT_W_PRS_SET_RESET) {
            /* Reset completes immediately -- there is no wire to settle -- so
             * PRS is never observed set. What matters is what the driver
             * checks afterwards: a present device comes back enabled, and the
             * reset-change bit says the reset finished.
             *
             * Only the status bit is set here. Delivering the interrupt is the
             * controller thread's job, because this runs on the guest's own
             * thread inside a fault handler, and pointing g_esp at a worker
             * stack from here would overwrite the stack pointer of the thread
             * being interrupted. */
            if (*ps & PORT_CCS)
                *ps |= PORT_PES;
            *ps |= PORT_PRSC;
            hc->reg[HcInterruptStatus / 4] |= INTR_RHSC;
        }
        return;
    }

    default:
        break;
    }

    if (size == 4) {
        *r = v;
    } else {
        unsigned shift = (off & 3u) * 8u;
        uint32_t mask = ((size == 1) ? 0xFFu : 0xFFFFu) << shift;
        *r = (*r & ~mask) | ((v << shift) & mask);
    }
}

/* ---- the control list -------------------------------------------------- */
/*
 * A host controller is a bus master: the driver builds endpoint and transfer
 * descriptors in RAM, points HcControlHeadED at them and sets ControlListFilled,
 * and the controller walks that list itself. Nothing arrives through MMIO, so
 * none of this is visible to the register trace -- which is why the driver
 * looked idle after the port came up.
 *
 * OHCI 1.0a, section 4. An endpoint descriptor is four dwords:
 *
 *   +0  FA | EN<<7 | D<<11 | S<<13 | K<<14 | F<<15 | MPS<<16
 *   +4  TailP        queue tail, 16-byte aligned
 *   +8  HeadP        queue head, with Halted in bit 0 and toggleCarry in bit 1
 *   +C  NextED
 *
 * and a general transfer descriptor is four more:
 *
 *   +0  ... DP<<19 | DI<<21 | T<<24 | EC<<26 | CC<<28
 *   +4  CBP          current buffer pointer, 0 when the transfer moved nothing
 *   +8  NextTD
 *   +C  BE           last byte of the buffer, inclusive
 *
 * A transfer is done when HeadP reaches TailP. Completed descriptors go on the
 * done queue, newest first, and the controller publishes it in the HCCA and
 * raises WritebackDoneHead.
 */
#define ED_SKIP        (1u << 14)
#define ED_HEAD_HALT   (1u << 0)
#define ED_HEAD_TOGGLE (1u << 1)
#define ED_PTR_MASK    0xFFFFFFF0u

#define TD_DP_SETUP    0u
#define TD_DP_OUT      1u
#define TD_DP_IN       2u
#define TD_CC_NOERROR  0u
#define TD_CC_STALL    4u


static uint32_t g_setup_pending;      /* wLength of the last SETUP seen */
/* Set by the first SETUP packet: the driver has started talking to the
 * device (its arrival was seen). */
static volatile int g_setup_seen;
static UsbSetup g_setup;

/* The control transfer's data stage, across however many descriptors it takes.
 *
 * bMaxPacketSize0 is 8, so a driver asking for a 32-byte configuration
 * descriptor queues four 8-byte IN descriptors and expects successive chunks.
 * Answering each one from the top of the buffer hands it the first 8 bytes
 * four times: the descriptor it assembles is nonsense, it gives up and starts
 * again, which is exactly the loop DDS9 sat in -- GET_DESCRIPTOR, SET_ADDRESS,
 * GET_DESCRIPTOR, forever. The answer is computed once per setup packet and
 * then consumed. */
static uint8_t g_ctrl_buf[64];
static int     g_ctrl_len = -1;   /* -1 = not answered yet */
static int     g_ctrl_sent;

/* Every address below came out of guest memory, so none of them are trusted.
 *
 * This is not theoretical. The driver writes HcControlHeadED twice during
 * bring-up, and the second write is 0xCCCCCCCC -- MSVC's uninitialised-memory
 * fill, left there by a local that was never assigned. Following it lands far
 * outside the mapping and takes the runtime down with an access violation,
 * which is a crash the title itself would never have had. A device model
 * following a pointer a title left lying around has to check it first.
 */
/* The contiguous (physical-mirror) window. Mapped separately from main RAM
 * and far above it, so a size check against the mapped RAM size rejects it. */
#define OHCI_CONTIG_BASE 0x80000000u
#define OHCI_CONTIG_SIZE (64u * 1024u * 1024u)

/* What a controller is handed is a PHYSICAL address (the driver gets it from
 * MmGetPhysicalAddress), and physical RAM is seen through the contiguous
 * window, not low virtual memory -- which here holds the XBE image instead.
 * The Two Towers' USB pool is the first contiguous allocation, physical 0,
 * so its HCCA is 0 and its EDs sit at 0x5xx: read as virtual addresses
 * those were zeros inside the XBE header. Addresses that already carry the
 * window's bit 31 pass through. */
extern int xbox_PhysIsIdentity(uint32_t pa);   /* kernel_memory.c */

static uint32_t ohci_phys_to_va(uint32_t pa)
{
    /* A buffer in ordinary memory (a report buffer in .data, say) got its
     * VA back from MmGetPhysicalAddress; the kernel remembers which. */
    if (pa < OHCI_CONTIG_SIZE && !xbox_PhysIsIdentity(pa))
        return OHCI_CONTIG_BASE + pa;
    return pa;
}

static int range_ok(uint32_t va, uint32_t bytes);

/* Descriptor pointers: 0 terminates a list, so it is never a descriptor. */
static int guest_ok(uint32_t va, uint32_t bytes)
{
    return va != 0 && range_ok(va, bytes);
}

/* Plain accesses, which may hit physical 0 (an HCCA placed there). */
static int range_ok(uint32_t va, uint32_t bytes)
{
    size_t mapped = xbox_GetMappedSize();

    va = ohci_phys_to_va(va);
    if (mapped == 0)
        return 0;
    /* Descriptors live where a driver puts DMA memory, and on Xbox that is
     * MmAllocateContiguousMemory -- the contiguous window, not low RAM. The
     * bound below reads as "is this in RAM", which is not the question: the
     * window is mapped too, at the same offset, and is exactly where the Xbox
     * USB stack builds its endpoint and transfer descriptors. Rejecting it
     * made every rd32 return 0 and every wr32 a no-op, so the walker looked at
     * an empty list and the driver waited forever for a done queue that could
     * not be written. DDS9's first control transfer sits at 0x80408940. */
    if (va >= OHCI_CONTIG_BASE
            && (uint64_t)va + bytes <= (uint64_t)OHCI_CONTIG_BASE
                                     + OHCI_CONTIG_SIZE)
        return 1;
    return (uint64_t)va + bytes <= (uint64_t)mapped;
}

static uint32_t rd32(uint32_t va)
{
    if (!range_ok(va, 4))
        return 0;
    return *(uint32_t *)((uint8_t *)xbox_GetMemoryOffset() + ohci_phys_to_va(va));
}
static void wr32(uint32_t va, uint32_t v)
{
    if (!range_ok(va, 4))
        return;
    *(uint32_t *)((uint8_t *)xbox_GetMemoryOffset() + ohci_phys_to_va(va)) = v;
}
static uint8_t *guest_ptr(uint32_t va, uint32_t bytes)
{
    return range_ok(va, bytes)
         ? (uint8_t *)xbox_GetMemoryOffset() + ohci_phys_to_va(va) : NULL;
}

/* Move one transfer descriptor. Returns the condition code to report. */
static uint32_t ohci_do_td(OhciController *hc, uint32_t ed0, uint32_t td)
{
    uint32_t info = rd32(td);
    uint32_t cbp  = rd32(td + 4);
    uint32_t be   = rd32(td + 12);
    uint32_t dp   = (info >> 19) & 3u;
    int      len  = (cbp && be >= cbp) ? (int)(be - cbp + 1) : 0;
    uint32_t endpoint = (ed0 >> 7) & 0xFu;
    int      moved = 0;

    if (s_trace) {
        static unsigned n;
        if (n++ < 80) {
            fprintf(stderr, "  [OHCI%d] TD %08X dp=%u ep=%u cbp=%08X be=%08X "
                            "len=%d\n", hc->index, td, dp, endpoint, cbp, be,
                    len);
            fflush(stderr);
        }
    }
    if (dp == TD_DP_SETUP) {
        /* Eight bytes of setup, kept for the data stage that follows. */
        if (len >= 8) {
            const uint8_t *p = guest_ptr(cbp, 8);
            if (!p) return TD_CC_NOERROR;
            g_setup.bmRequestType = p[0];
            g_setup.bRequest      = p[1];
            g_setup.wValue        = (uint16_t)(p[2] | (p[3] << 8));
            g_setup.wIndex        = (uint16_t)(p[4] | (p[5] << 8));
            g_setup.wLength       = (uint16_t)(p[6] | (p[7] << 8));
            g_setup_pending = 1;
            g_setup_seen = 1;
            g_ctrl_len = -1;   /* answered lazily on the first IN */
            g_ctrl_sent = 0;
            if (s_trace) {
                fprintf(stderr, "  [OHCI%d] SETUP %02X %02X value %04X "
                                "index %04X len %u\n",
                        hc->index, g_setup.bmRequestType, g_setup.bRequest,
                        g_setup.wValue, g_setup.wIndex, g_setup.wLength);
                fflush(stderr);
            }
            moved = 8;
        }
    } else if (dp == TD_DP_IN) {
        if (endpoint == 0) {
            /* Data stage of a control transfer, or its status stage when the
             * driver asks for nothing. */
            int n;

            if (!g_setup_pending)
                return TD_CC_NOERROR;
            if (g_ctrl_len < 0) {
                g_ctrl_len = usb_gamepad_control(&g_setup, g_ctrl_buf,
                                                 (int)sizeof g_ctrl_buf);
                if (g_ctrl_len < 0) {
                    /* Worth saying out loud. A stall here halts the
                     * endpoint until the driver clears it, and a driver
                     * that sees one usually stops using the device -- so
                     * an unhandled request is not a gap that degrades
                     * gracefully, it is one that ends input. */
                    fprintf(stderr, "  [OHCI%d] STALL: unhandled control "
                            "request %02X %02X value %04X index %04X len %u\n",
                            hc->index, g_setup.bmRequestType, g_setup.bRequest,
                            g_setup.wValue, g_setup.wIndex, g_setup.wLength);
                    fflush(stderr);
                    g_setup_pending = 0;
                    return TD_CC_STALL;
                }
                g_ctrl_sent = 0;
            }
            /* Whatever is left, capped by this descriptor's buffer. A short
             * packet is how the device says "that is all", so running out is
             * the normal end of the stage rather than an error. */
            n = g_ctrl_len - g_ctrl_sent;
            if (n > len) n = len;
            if (n < 0) n = 0;
            if (n > 0 && guest_ptr(cbp, (uint32_t)n))
                memcpy(guest_ptr(cbp, (uint32_t)n),
                       g_ctrl_buf + g_ctrl_sent, (size_t)n);
            g_ctrl_sent += n;
            moved = n;
            if (s_trace && n > 0) {
                fprintf(stderr, "  [OHCI%d] IN ep0 %d bytes (%d/%d)\n",
                        hc->index, n, g_ctrl_sent, g_ctrl_len);
                fflush(stderr);
            }
        } else {
            /* The pad's report, on its interrupt endpoint. */
            uint8_t rep[32];
            int n = usb_gamepad_report(rep, (int)sizeof rep);
            g_usb_reports++;
            if (g_usb_reports <= 4)
                fprintf(stderr, "  [OHCI%d] report #%u on ep%u: TD %08X info %08X cbp %08X be %08X len %d (report %d bytes) ed %08X\n",
                        hc->index, g_usb_reports, endpoint, td, info, cbp, be, len, n, ed0);
            if (getenv("RECOMP_USB_DUMP") && g_usb_reports < 10) {
                fprintf(stderr, "  [USBREP] TD %08X info %08X cbp %08X be %08X len %d next %08X\n",
                        td, info, cbp, be, len, rd32(td + 8));
                fflush(stderr);
            }
            if (n > len) n = len;
            if (n > 0 && guest_ptr(cbp, (uint32_t)n))
                memcpy(guest_ptr(cbp, (uint32_t)n), rep, (size_t)n);
            moved = n;
        }
    } else {
        /* OUT: the status stage of an IN control transfer (nothing to do),
         * or a rumble report on the interrupt endpoint (passed on). */
        if (getenv("RECOMP_USB_OUTLOG")) {   /* testing: what the host sends the pad */
            static int n;
            const uint8_t *p = len > 0 ? guest_ptr(cbp, (uint32_t)len) : NULL;
            if (n++ < 40) {
                fprintf(stderr, "  [OHCI%d] OUT ep%u len %d:", hc->index, endpoint, len);
                if (p) { int k; for (k = 0; k < len && k < 8; k++) fprintf(stderr, " %02X", p[k]); }
                fprintf(stderr, "\n");
                fflush(stderr);
            }
        }
        if (endpoint != 0 && len > 0 && guest_ptr(cbp, (uint32_t)len))
            usb_gamepad_output(guest_ptr(cbp, (uint32_t)len), len);
        /* The data stage of a HID SET_REPORT (class, interface, host to
         * device): how XInputSetState sends rumble to a pad with no OUT pipe
         * open -- which is this one. */
        if (endpoint == 0 && g_setup_pending && g_setup.bmRequestType == 0x21
                && g_setup.bRequest == 0x09 && len > 0 && guest_ptr(cbp, (uint32_t)len))
            usb_gamepad_output(guest_ptr(cbp, (uint32_t)len), len);
        moved = len;
        g_setup_pending = 0;
    }

    /* CBP is zero when everything asked for moved, and otherwise points past
     * what did. A driver computes the transferred length from it. */
    wr32(td + 4, (moved >= len) ? 0u : cbp + (uint32_t)moved);
    return TD_CC_NOERROR;
}

/* Walk the control list once. Returns 1 if anything completed. */
/* Walk one list of endpoint descriptors, moving whatever is queued on each.
 *
 * Shared, because the control list is not the only one that matters: a gamepad
 * delivers its reports on an interrupt endpoint, and those EDs hang off the
 * HCCA interrupt table rather than HcControlHeadED. Walking only the control
 * list enumerates a device perfectly and then never hears from it again. */
/* A short history of what the walker did on endpoints with work queued,
 * printed by the stall watchdog: the last thing that happened to the pad's
 * input endpoint before its reports stopped is the one worth seeing. */
typedef struct {
    DWORD t;
    uint32_t ed, head_in, tail, td, tdinfo, next, head_out, head_now;
    char what;          /* 'C' completed, 'N' bad NextTD, 'R' driver raced the writeback */
} OhciTrace;
#define OTR_N 512
static OhciTrace s_otr[OTR_N];
static volatile LONG s_otr_n;
static void otr_add(char what, uint32_t ed, uint32_t hin, uint32_t tail, uint32_t td,
                    uint32_t tdinfo, uint32_t next, uint32_t hout, uint32_t hnow)
{
    OhciTrace *e;
    if (what == 'N') {      /* the same refusal every tick is one event */
        const OhciTrace *l = &s_otr[(s_otr_n - 1) & (OTR_N - 1)];
        if (s_otr_n && l->what == 'N' && l->ed == ed && l->head_in == hin && l->td == td && l->next == next && l->tail == tail)
            return;
    }
    e = &s_otr[(InterlockedIncrement(&s_otr_n) - 1) & (OTR_N - 1)];
    e->t = GetTickCount(); e->what = what; e->ed = ed; e->head_in = hin; e->tail = tail;
    e->td = td; e->tdinfo = tdinfo; e->next = next; e->head_out = hout; e->head_now = hnow;
    if (what == 'N' || what == 'R') {      /* the driver changed an endpoint under the walker: say so, a few times */
        static volatile LONG said;
        if (InterlockedIncrement(&said) <= 20) {
            fprintf(stderr, "  [OHCI] %s on ED %08X (head %08X, TD %08X next %08X, ED now %08X): left to the driver\n",
                    what == 'R' ? "driver moved HeadP during a transfer" : "TD with no valid NextTD",
                    ed, hin, td, next, hnow);
            fflush(stderr);
        }
    }
}
static void otr_dump(int index)
{
    LONG n = s_otr_n, i = n > OTR_N ? n - OTR_N : 0;
    DWORD now = GetTickCount();
    for (; i < n; i++) {
        const OhciTrace *e = &s_otr[i & (OTR_N - 1)];
        if (e->what == 'W') {
            fprintf(stderr, "  [OHCI%d]   -%5lu ms W reg %02X = %08X (was %08X) thread %u\n", index,
                    (unsigned long)(now - e->t), e->ed, e->head_in, e->tail, e->td);
            continue;
        }
        fprintf(stderr, "  [OHCI%d]   -%5lu ms %c ED %08X head %08X tail %08X TD %08X info %08X next %08X -> head %08X (ED had %08X)\n",
                index, (unsigned long)(now - e->t), e->what, e->ed, e->head_in, e->tail,
                e->td, e->tdinfo, e->next, e->head_out, e->head_now);
    }
}

/* Store a new HeadP only if the driver has not changed it since it was read.
 *
 * The driver runs on other threads while this one walks, and it does change
 * HeadP itself: requeueing after an error, cancelling, re-pointing an idle
 * endpoint. A plain store of what was computed from a stale read puts back a
 * pointer the driver has moved on from -- or one built from a descriptor it
 * already reused -- and the endpoint is wedged for good (head 0xFFFFFFF0, or
 * a head that never meets the tail again). An atomic compare-and-swap leaves
 * the driver's value in place when it got there first. */
static int ed_set_head(uint32_t ed, uint32_t expect, uint32_t head)
{
    uint8_t *p = guest_ptr(ed + 8, 4);
    if (!p)
        return 0;
    return (uint32_t)InterlockedCompareExchange((volatile LONG *)p, (LONG)head, (LONG)expect) == expect;
}

static int ohci_walk_eds(OhciController *hc, uint32_t ed, uint32_t *done_head)
{
    int completed = 0, guard = 0;

    while (guest_ok(ed, 16) && ++guard < 64) {
        uint32_t ed0  = rd32(ed);
        uint32_t tail = rd32(ed + 4) & ED_PTR_MASK;
        uint32_t head = rd32(ed + 8);
        int tguard = 0;

        if (s_trace) {
            static unsigned n;
            if (n++ < 60) {
                fprintf(stderr, "  [OHCI%d] ED %08X info=%08X head=%08X "
                                "tail=%08X next=%08X\n", hc->index, ed, ed0,
                        head, tail, rd32(ed + 12));
                fflush(stderr);
            }
        }
        {     /* note each change of an endpoint's queue */
            static uint32_t sed[8], shead[8], stail[8], sinfo[8];
            int k;
            for (k = 0; k < 8 && sed[k] && sed[k] != ed; k++) {}
            if (k < 8 && (sed[k] != ed || shead[k] != head || stail[k] != tail || sinfo[k] != ed0)) {
                sed[k] = ed; shead[k] = head; stail[k] = tail; sinfo[k] = ed0;
                otr_add('S', ed, head, tail, head & ED_PTR_MASK, rd32(head & ED_PTR_MASK), rd32((head & ED_PTR_MASK) + 8), ed0, 0);
            }
        }
        if (ed0 & ED_SKIP) { ed = rd32(ed + 12) & ED_PTR_MASK; continue; }
        if (head & ED_HEAD_HALT) { ed = rd32(ed + 12) & ED_PTR_MASK; continue; }

        while ((head & ED_PTR_MASK) != tail
            && guest_ok(head & ED_PTR_MASK, 16) && ++tguard < 64) {
            uint32_t td = head & ED_PTR_MASK;
            uint32_t rawnext = rd32(td + 8);
            uint32_t next = rawnext & ED_PTR_MASK;
            uint32_t was = head, now, cc, tdinfo = rd32(td);

            /* A queued descriptor always links on, to the next one or to the
             * tail's dummy. One that does not (0xFFFFFFFF is what the driver
             * leaves in a descriptor it has taken back) is not ours to move:
             * the driver is changing this endpoint under us. Leave it as it
             * is and look again next tick. */
            if (next != tail && !guest_ok(next, 16)) {
                otr_add('N', ed, head, tail, td, tdinfo, rawnext, head, rd32(ed + 8));
                break;
            }
            /* Claim the descriptor before touching it: move HeadP past it,
             * atomically, and only if the driver has not moved it meanwhile.
             * Doing the transfer first and storing HeadP afterwards left a
             * window in which a requeue by the driver was undone. */
            head = next | (was & (ED_HEAD_TOGGLE | 0x0Cu));
            if (!ed_set_head(ed, was, head)) {
                now = rd32(ed + 8);
                otr_add('R', ed, was, tail, td, tdinfo, rawnext, head, now);
                head = now;
                break;
            }
            /* sKip set by the driver since this endpoint was read: it is
             * taking the endpoint back, and wants it left alone. */
            if (rd32(ed) & ED_SKIP) {
                if (ed_set_head(ed, head, was)) { head = was; break; }
            }

            cc = ohci_do_td(hc, ed0, td);

            /* Report the outcome where the driver reads it, then put the
             * descriptor on the done queue, newest first. */
            wr32(td, (rd32(td) & 0x0FFFFFFFu) | (cc << 28));
            wr32(td + 8, *done_head);
            *done_head = td;
            completed++;
            if (((ed0 >> 7) & 0xFu) != 0)      /* not the control endpoint */
                otr_add('C', ed, was, tail, td, tdinfo, rawnext, head, 0);

            if (cc != TD_CC_NOERROR) {
                /* a stall halts the endpoint */
                if (ed_set_head(ed, head, head | ED_HEAD_HALT))
                    head |= ED_HEAD_HALT;
                break;
            }
        }
        ed = rd32(ed + 12) & ED_PTR_MASK;
    }

    return completed;
}

/* Publish the done queue where the driver reads it and say so. */
static void ohci_publish_done(OhciController *hc, uint32_t done_head)
{
    uint32_t hcca = hc->reg[HcHCCA / 4];

    if (hcca || hc->hcca_written)
        wr32(hcca + HCCA_DONE_HEAD, done_head);
    hc->reg[HcDoneHead / 4] = done_head;
    hc->done_unseen = 1;
    hc->reg[HcInterruptStatus / 4] |= INTR_WDH;
}

/* The periodic list for the current frame.
 *
 * The HCCA opens with 32 ED pointers, one per frame slot, and the controller
 * services the one the frame number selects. An interrupt endpoint polled
 * every 4 ms appears in several slots, so cycling through them the way the
 * frame counter does is what makes its transfers happen at all. */
static int ohci_run_periodic_list(OhciController *hc, uint32_t *done_head)
{
    uint32_t hcca = hc->reg[HcHCCA / 4];
    uint32_t slot, ed;
    int completed;

    if (!hcca && !hc->hcca_written)
        return 0;
    /* Every slot, not the one the frame counter happens to land on.
     *
     * A tick covers twenty frames, and stepping the counter by twenty means
     * only slots that are multiples of four are ever selected -- gcd(20,32)
     * is 4, so twenty-four of the thirty-two are never looked at. An
     * interrupt endpoint that lands in one of those is polled by the driver
     * and serviced by nobody. Sweeping all of them costs a guest read per
     * slot and cannot miss. */
    completed = 0;
    if (getenv("RECOMP_USB_DUMP")) {
        static DWORD last;
        if (GetTickCount() - last > 10000) {
            const uint8_t *m = (const uint8_t *)xbox_GetMemoryOffset() + 0x80000000u;
            uint32_t a;
            last = GetTickCount();
            fprintf(stderr, "  [USBDUMP] %u reports so far; status %08X enable %08X control %08X; HCCA slots:", g_usb_reports, hc->reg[HcInterruptStatus / 4], hc->reg[HcInterruptEnable / 4], hc->reg[HcControl / 4]);
            for (a = 0; a < 32; a++) fprintf(stderr, " %X", *(const uint32_t *)(m + hcca + a * 4));
            fprintf(stderr, "\n  [USBDUMP] nonzero words 0x000-0x2000:");
            for (a = 0x80; a < 0x2000; a += 4) { uint32_t w = *(const uint32_t *)(m + a); if (w) fprintf(stderr, " %X=%X", a, w); }
            fprintf(stderr, "\n");
        }
    }
    for (slot = 0; slot < 32u; slot++) {
        ed = rd32(hcca + slot * 4u) & ED_PTR_MASK;
        if (!ed)
            continue;
        /* The first interrupt endpoint the driver publishes is the one the
         * input reports ride on, so say so once: an enumerated pad whose
         * table stays empty is a pad nobody opened, which is a different
         * problem from a pad nobody serviced. */
        if (!hc->periodic_seen) {
            hc->periodic_seen = 1;
            {
                const uint8_t *m = (const uint8_t *)xbox_GetMemoryOffset();
                fprintf(stderr, "  [OHCI%d] HCCA %08X identity=%d -> va %08X; words at contig: %08X %08X, at va: %08X %08X\n",
                        hc->index, hcca, xbox_PhysIsIdentity(hcca), ohci_phys_to_va(hcca),
                        *(const uint32_t *)(m + 0x80000000u + hcca), *(const uint32_t *)(m + 0x80000000u + hcca + 4),
                        hcca < 0x1000000u ? *(const uint32_t *)(m + hcca) : 0, hcca < 0x1000000u ? *(const uint32_t *)(m + hcca + 4) : 0);
            }
            fprintf(stderr, "  [OHCI%d] periodic slot %u -> ED %08X "
                    "info=%08X\n", hc->index, slot, ed, rd32(ed));
            fflush(stderr);
        }
        completed += ohci_walk_eds(hc, ed, done_head);
    }
    return completed;
}

/* ---- raising an interrupt --------------------------------------------- */

/* Call the title's ISR on this thread, with a guest stack under it.
 *
 * BOOLEAN ServiceRoutine(PKINTERRUPT Interrupt, PVOID ServiceContext), stdcall,
 * so the two arguments go on the stack right to left with a return address on
 * top. The sentinel is what the routine pops on the way out; nothing jumps to
 * it, and a recognisable value beats a real address if it ever shows up in a
 * report.
 *
 * Returns what the routine returned: an ISR that does not claim the interrupt
 * returns FALSE, and that is worth seeing rather than assuming.
 */
static int ohci_call_isr(OhciController *hc)
{
    uint32_t kinterrupt = xbox_GetConnectedInterrupt(OHCI_VECTOR);
    uint32_t routine, context;
    recomp_func_t fn;
    uint8_t *mem;
    int slot;

    if (!kinterrupt)
        return -1;                      /* nothing connected yet            */

    mem     = (uint8_t *)xbox_GetMemoryOffset();
    routine = *(uint32_t *)(mem + kinterrupt + 0);
    context = *(uint32_t *)(mem + kinterrupt + 4);
    if (!routine)
        return -1;

    fn = recomp_lookup(routine);
    if (!fn) {
        fprintf(stderr, "  [OHCI%d] ISR 0x%08X has no translation\n",
                hc->index, routine);
        fflush(stderr);
        return -1;
    }

    /* One slice for the life of the raise. Sixteen exist and this takes one
     * only while the routine runs, so a title using them for its own workers
     * is not starved by a controller that interrupts. */
    slot = xbox_worker_stack_alloc();
    if (slot < 0) {
        fprintf(stderr, "  [OHCI%d] no worker stack for the ISR\n", hc->index);
        fflush(stderr);
        return -1;
    }

    g_esp = XBOX_WORKER_STACK_TOP(slot);
    g_eax = g_ecx = g_edx = g_ebx = g_esi = g_edi = 0;

    g_esp -= 4; *(uint32_t *)(mem + g_esp) = context;      /* arg 2 */
    g_esp -= 4; *(uint32_t *)(mem + g_esp) = kinterrupt;   /* arg 1 */
    g_esp -= 4; *(uint32_t *)(mem + g_esp) = 0xDEADBEEFu;  /* return address */

    s_in_isr = 1;
    hc->done_unseen = 0;            /* the ISR is about to read it */
    fn();
    s_in_isr = 0;

    xbox_worker_stack_free(slot);
    return (int)(g_eax & 1u);
}

/* Set the status bits and, if the driver has unmasked them, call the ISR.
 *
 * MIE is the master enable and HcInterruptEnable is the per-source mask; a
 * controller that interrupts through either of those while they are clear is
 * a controller the driver has every right to be confused by.
 */
static void ohci_raise(OhciController *hc, uint32_t source)
{
    uint32_t enable = hc->reg[HcInterruptEnable / 4];
    int claimed;

    hc->reg[HcInterruptStatus / 4] |= source;

    if (!(enable & INTR_MIE) || !(enable & source))
        return;

    claimed = ohci_call_isr(hc);
    if (s_trace || claimed >= 0) {
        static unsigned n;
        if (n++ < 20) {
            fprintf(stderr, "  [OHCI%d] raised %08X -> ISR %s\n",
                    hc->index, source,
                    claimed < 0 ? "not callable" :
                    claimed ? "claimed it" : "declined it");
            fflush(stderr);
        }
    }
}

/* ---- bring-up ---------------------------------------------------------- */

static void ohci_reset(OhciController *hc, uint32_t base, int index)
{
    memset(hc, 0, sizeof *hc);
    hc->base  = base;
    hc->index = index;

    hc->reg[HcRevision / 4]       = 0x00000010u;   /* OHCI 1.0             */
    hc->reg[HcFmInterval / 4]     = 0x27782EDFu;   /* 11999, FSMPS default */
    hc->reg[HcPeriodicStart / 4]  = 0x00003E67u;   /* 90% of the frame     */
    hc->reg[HcLSThreshold / 4]    = 0x00000628u;

    /* Root hub: OHCI_PORTS downstream, ports always powered, no over-current
     * reporting. NoPowerSwitching keeps a driver from waiting on a power-on
     * sequence that has nothing to switch. */
    hc->reg[HcRhDescriptorA / 4]  = (uint32_t)s_ndp | (1u << 9);
    hc->reg[HcRhDescriptorB / 4]  = 0x00000000u;
    hc->reg[HcRhStatus / 4]       = 0x00000000u;

    /* Ports powered and empty. The gamepad is not here yet, deliberately.
     *
     * Presenting it as already connected does not work, and the reason is
     * worth keeping: the driver scans the root hub itself during bring-up,
     * sees the connect-status-change bit, clears it, and by the time it
     * unmasks the root hub interrupt there is no change left to report. The
     * pending status bit does not survive either, because the driver resets
     * the controller first and a reset clears interrupt status -- both of
     * those are correct behaviour, and between them a device that was always
     * there is a device that never arrives.
     *
     * A console detects the port change after the controller is running, so
     * that is what the controller thread does: it waits for operational with
     * the interrupt unmasked, and only then plugs the device in. */
    /* Every port the descriptor claims, not the first two.
     *
     * A root hub that reports four downstream ports and powers two has two
     * ports a driver will skip: no PortPowerStatus is an unpowered socket,
     * and nothing is expected to arrive on one. That made a device placed on
     * port 3 invisible, which read as "the driver only scans two ports" and
     * is really "we only powered two". */
    {
        unsigned p;
        for (p = 0; p < 4u; p++)
            hc->reg[(HcRhPortStatus1 + p * 4) / 4] =
                (p < s_ndp) ? PORT_PPS : 0u;
    }
}

/* The controller, running on its own thread.
 *
 * It has to be its own thread for a reason that is easy to get wrong: the
 * guest register file is thread-local, so setting g_esp to a worker stack on
 * the guest's thread would overwrite the guest's own stack pointer mid-call.
 * A separate thread has its own copy, and it also happens to be what the
 * hardware does -- an interrupt arrives when the controller decides, not when
 * the driver next reads a register.
 *
 * ponytail: one delivery, of the root hub status change that is already
 * pending from bring-up. There is nothing behind it yet -- no descriptor list
 * walking, so an enumeration attempt has nothing to answer it -- and the point
 * of this delivery is to find out what the driver does when it finally gets
 * the interrupt it has been waiting for. Repeat delivery and the transfer
 * lists come after that answer, not before it.
 */
/* Where the controller thread is, for the input watchdog's report:
 * 1 sleeping, 2 walking the lists, 3 in the guest's interrupt handler. */
volatile int g_ohci_phase;
static DWORD WINAPI ohci_thread(LPVOID unused)
{
    /* This thread calls recompiled code, so it needs what any thread running
     * recompiled code needs: its own TIB. The guest register set is already
     * thread-local and the ISR gets a worker stack, but fs:[0] is the SEH
     * chain head and fs:[4] reaches the CRT's per-thread data -- and a guest
     * function with an SEH prologue on a thread whose g_fs_base is zero
     * dereferences null before it executes a line of its own body. That is
     * what killed the process here, two interrupts in, with no fault report
     * because the fault was in the runtime rather than in the title. */
    unsigned waited = 0;
    int plugged = 0, listen_ticks = 0, unseen_ticks = 0, replugs = 0;
    uint32_t last_status = 0;
    unsigned repeats = 0;
    unsigned held_off = 0;
    int      held_off_warned = 0, held_off_forced = 0;
    unsigned last_ack = 0;

    (void)unused;
    {
        uint32_t tib = xbox_AllocThreadTib();
        if (!tib) {
            fprintf(stderr, "  [OHCI0] no TIB for the controller thread; "
                            "not delivering interrupts\n");
            fflush(stderr);
            return 0;
        }
        g_fs_base = tib;
    }

    for (;;) {
        OhciController *hc = &s_hc[s_device_hc];
        uint32_t control, enable, status;

        g_ohci_phase = 1;
        Sleep(OHCI_TICK_MS);
        g_ohci_phase = 2;
        control = hc->reg[HcControl / 4];
        enable  = hc->reg[HcInterruptEnable / 4];

        /* Operational is HCFS == 10b in bits 7:6. Interrupting a controller
         * the driver has not started yet is not a test of anything. */
        if ((control & 0xC0u) != 0x80u) {
            if (++waited > 1500)              /* 30 s and it never started */
                break;
            continue;
        }
        if (!(enable & INTR_MIE))
            continue;

        /* Plug the device in once, after the driver is running and listening.
         * Presenting it earlier does not work: the driver clears the connect
         * change during its own bring-up scan, and a reset clears interrupt
         * status, so a device that was always there is one that never
         * arrives. */
        /* Not straight away: on a fast PC the driver can enable RHSC while
         * its own bring-up scan is still running, and a connect change made
         * then is cleared unseen (1.1.3/1.1.4 on Windows: "operational after
         * 0 ms", then no transfer ever -- no pad, so no keyboard either).
         * Ten ticks (200 ms) after the driver is listening. */
        if (!plugged && (enable & INTR_RHSC) && listen_ticks < 10) {
            listen_ticks++;          /* only the plug waits; the clock runs on */
        } else if (!plugged && (enable & INTR_RHSC)) {
            /* Which port the device appears on decides which controller slot
             * XAPI assigns it, and a title that reads "player 1" looks at slot
             * 0. RECOMP_USB_PORT picks it (0..1 on this controller) so the
             * mapping can be found by measurement rather than assumed. */
            const char *pspec = getenv("RECOMP_USB_PORT");
            unsigned port = pspec ? (unsigned)atoi(pspec) : 0u;
            if (port >= s_ndp) port = s_ndp - 1u;
            hc->reg[(HcRhPortStatus1 + port * 4) / 4] |= PORT_CCS | PORT_CSC;
            hc->reg[HcInterruptStatus / 4] |= INTR_RHSC;
            plugged = 1;
            if (getenv("RECOMP_USB_MISS_FIRST")) {
                /* testing: the arrival is missed, as when the driver's own
                 * bring-up scan clears it */
                hc->reg[(HcRhPortStatus1 + port * 4) / 4] &= ~(PORT_CCS | PORT_CSC);
                hc->reg[HcInterruptStatus / 4] &= ~INTR_RHSC;
            }
            fprintf(stderr, "  [OHCI0] operational after %u ms; device "
                            "arriving on port 1\n", waited * 20);
            fflush(stderr);
        } else if (plugged && !g_setup_seen && ++unseen_ticks >= 50 && replugs < 10) {
            /* A second and the driver has not asked the device anything: its
             * arrival was missed. Present it again, as a pad unplugged and
             * plugged back in. */
            const char *pspec = getenv("RECOMP_USB_PORT");
            unsigned port = pspec ? (unsigned)atoi(pspec) : 0u;
            if (port >= s_ndp) port = s_ndp - 1u;
            unseen_ticks = 0;
            replugs++;
            hc->reg[(HcRhPortStatus1 + port * 4) / 4] |= PORT_CCS | PORT_CSC;
            hc->reg[HcInterruptStatus / 4] |= INTR_RHSC;
            fprintf(stderr, "  [OHCI0] the driver has not enumerated the pad; "
                            "presenting it again (%d)\n", replugs);
            fflush(stderr);
        }

        /* Watchdog: the pad's reports stopped (they come every few ms while
         * the driver keeps its input transfer queued). Says once per stall
         * what the controller and the driver's endpoints look like, so the
         * log shows where it stuck. */
        {
            extern volatile DWORD g_usb_report_ms;
            static DWORD said_for;
            DWORD now = GetTickCount(), at = g_usb_report_ms;
            if (at && g_usb_reports > 200 && (LONG)(now - at) > 3000 && said_for != at) {
                uint32_t hcca = hc->reg[HcHCCA / 4], slot, edp;
                said_for = at;
                fprintf(stderr, "  [OHCI%d] no pad report for %ld ms: status %08X enable %08X control %08X "
                        "cmd %08X done %08X; irq held off %u ticks (IRQL depth %d); acks %u\n",
                        hc->index, (long)(now - at), hc->reg[HcInterruptStatus / 4], hc->reg[HcInterruptEnable / 4],
                        hc->reg[HcControl / 4], hc->reg[HcCommandStatus / 4], hc->reg[HcDoneHead / 4],
                        held_off, xbox_IrqlRaisedCount(), hc->ack_seq);
                for (slot = 0; slot < 32u && (hcca || hc->hcca_written); slot++) {
                    edp = rd32(hcca + slot * 4u) & ED_PTR_MASK;
                    if (edp && guest_ok(edp, 16)) {
                        fprintf(stderr, "  [OHCI%d]   slot %u ED %08X info %08X tail %08X head %08X next %08X\n",
                                hc->index, slot, edp, rd32(edp), rd32(edp + 4), rd32(edp + 8), rd32(edp + 12));
                        break;
                    }
                }
                edp = hc->reg[HcControlHeadED / 4] & ED_PTR_MASK;
                if (edp && guest_ok(edp, 16))
                    fprintf(stderr, "  [OHCI%d]   control ED %08X info %08X tail %08X head %08X next %08X\n",
                            hc->index, edp, rd32(edp), rd32(edp + 4), rd32(edp + 8), rd32(edp + 12));
                {
                    uint32_t a;
                    for (a = 0x1E00; a < 0x1F00; a += 0x20)
                        fprintf(stderr, "  [OHCI%d]   TD %08X: %08X %08X %08X %08X | %08X %08X %08X %08X\n", hc->index, a,
                                rd32(a), rd32(a + 4), rd32(a + 8), rd32(a + 12), rd32(a + 16), rd32(a + 20), rd32(a + 24), rd32(a + 28));
                }
                fprintf(stderr, "  [OHCI%d]   last transfers on the input endpoints:\n", hc->index);
                otr_dump(hc->index);
                fflush(stderr);
            }
        }

        /* The frame clock.
         *
         * INTR_SF was defined and never raised, and HcFmNumber only moved when
         * something read it. That is enough for a driver that polls, and not
         * enough for one that waits: USB enumeration is full of delays counted
         * in frames -- the recovery time after SET_ADDRESS is the first -- and
         * the Xbox stack enables StartOfFrame precisely to count them. Without
         * a frame ever starting, enumeration stops one transfer past
         * SET_ADDRESS with the driver waiting for time to pass.
         *
         * Twenty frames a tick because a frame is 1 ms and this loop sleeps
         * 20. The count is what drivers compare; the rate is what they feel,
         * and both are closer to right this way than a counter that never
         * moves. */
        {
            uint32_t hcca = hc->reg[HcHCCA / 4];

            hc->reg[HcFmNumber / 4] =
                (hc->reg[HcFmNumber / 4] + OHCI_TICK_MS) & 0xFFFFu;
            /* HccaFrameNumber is 16 bits at +0x80 with a pad above it that the
             * controller zeroes, so a dword write is what hardware does. */
            if (hcca || hc->hcca_written)
                wr32(hcca + 0x80, hc->reg[HcFmNumber / 4]);
            hc->reg[HcInterruptStatus / 4] |= INTR_SF;
        }

        /* Be the bus master. ControlListEnable in HcControl says the driver
         * wants the list walked; ControlListFilled says it has put something
         * on it. Walking on the tick rather than only when CLF is written
         * costs a read of a guest dword and means a descriptor queued without
         * rewriting CLF is still moved -- which is legal, and drivers do it. */
        /* Not while the driver still owes us an acknowledgement.
         *
         * HccaDoneHead belongs to the driver from the moment WDH is set
         * until it clears it. Writing a second done list over the first
         * loses every descriptor on it -- the driver walks a chain whose
         * links now point into the new list, and the structures it keeps
         * alongside go with it. At a twenty-millisecond tick that overlap
         * was rare enough to look like bad luck; at four it corrupted the
         * host controller's pending list within a second or two of the
         * gamepad's reports starting. Leaving the descriptors on their
         * endpoints for another pass costs a few milliseconds of latency
         * and is what the controller is specified to do. */
        if (hc->reg[HcInterruptStatus / 4] & INTR_WDH)
            goto deliver;

        /* All three lists in one pass, into one done list, published once.
         *
         * They used to be walked one at a time, each publishing its own done
         * list and the next skipped while that was unacknowledged. The pad's
         * input report completes on the periodic list every tick, so from the
         * moment input started the control list was never reached again: a
         * control request made after enumeration -- XInputSetState's
         * SET_REPORT, the only way this pad takes rumble -- stayed queued for
         * ever, and the game, which sends the next rumble only when the last
         * completes, never sent another. (Walking them in the same tick while
         * still publishing twice was worse: the second list overwrote the
         * first and an input completion was lost.) Chaining every completion
         * into the one done list is what a real controller does. */
        {
            uint32_t done_head = 0;
            int completed = 0;
            if (control & 0x04u) {          /* PeriodicListEnable */
                if (!hc->ple_seen) {
                    hc->ple_seen = 1;
                    fprintf(stderr, "  [OHCI%d] periodic list enabled "
                            "(HcControl=%08X HCCA=%08X)\n",
                            hc->index, control, hc->reg[HcHCCA / 4]);
                    fflush(stderr);
                }
                completed += ohci_run_periodic_list(hc, &done_head);
            }
            /* BulkListEnable, bit 5. Nothing on this device uses bulk, but a
             * driver that puts a transfer there is owed the same service. */
            if ((control & 0x20u)
             && guest_ok(hc->reg[HcBulkHeadED / 4] & ED_PTR_MASK, 16)) {
                int c = ohci_walk_eds(hc, hc->reg[HcBulkHeadED / 4] & ED_PTR_MASK, &done_head);
                if (c) hc->reg[HcCommandStatus / 4] &= ~0x04u;   /* BLF consumed */
                completed += c;
            }
            /* ControlListEnable, bit 4. */
            if ((control & 0x10u)
             && guest_ok(hc->reg[HcControlHeadED / 4] & ED_PTR_MASK, 16)) {
                int c = ohci_walk_eds(hc, hc->reg[HcControlHeadED / 4] & ED_PTR_MASK, &done_head);
                if (c) hc->reg[HcCommandStatus / 4] &= ~0x02u;   /* CLF consumed */
                completed += c;
            }
            if (completed)
                ohci_publish_done(hc, done_head);
        }

        /* Level-triggered, which is what OHCI is: while an enabled source is
         * set, the line is asserted. The handler clears the status bit, so
         * this stops on its own -- and if it ever does not, the cap below says
         * so rather than spinning the ISR forever. */
    deliver:
        status = hc->reg[HcInterruptStatus / 4] & enable & 0x7Fu;
        if (!status)
            continue;

        /* Bounded, because the mask is a model and not the hardware.
         *
         * The depth is a count of raise/lower pairs across every thread, and
         * one unmatched raise anywhere leaves the gate shut for the rest of
         * the run -- which is how this first showed up: no interrupt was ever
         * delivered and enumeration stopped at two empty control EDs. Real
         * hardware is never masked for 80 ms, so past that the line is
         * asserted anyway. The common case still holds the ISR out of the
         * guest's critical section; the pathological case costs a delay
         * instead of the device. */
        if (xbox_IrqlBlocksInterrupts() && ++held_off <= OHCI_IRQ_HOLDOFF) {
            if (!held_off_warned) {
                held_off_warned = 1;
                fprintf(stderr, "  [OHCI%d] irq held off by guest IRQL "
                        "(depth %d, status=0x%02X)\n",
                        hc->index, xbox_IrqlRaisedCount(), status);
                fflush(stderr);
            }
            continue;
        }
        if (held_off > OHCI_IRQ_HOLDOFF && !held_off_forced) {
            held_off_forced = 1;
            fprintf(stderr, "  [OHCI%d] guest IRQL never dropped (depth %d); "
                    "delivering anyway\n", hc->index, xbox_IrqlRaisedCount());
            fflush(stderr);
        }
        held_off = 0;

        /* A stuck source is one the handler never clears. Repeating the
         * same status value is not that.
         *
         * This counted deliveries of an unchanged status and gave up at
         * 200, and a working gamepad trips that in two seconds: the pad
         * reports at 100 Hz, every report completes a transfer descriptor,
         * every completion raises WritebackDoneHead, and every one of them
         * is the same status value. The driver was acknowledging each one
         * -- 141 writes to HcInterruptStatus in the run this was found on
         * -- and the count climbed anyway, because nothing reset it.
         *
         * What distinguishes the two is whether the driver wrote the
         * register at all between one delivery and the next. */
        if (status == last_status && hc->ack_seq == last_ack) {
            if (++repeats > 200) {
                fprintf(stderr, "  [OHCI%d] status %08X delivered 200 times "
                                "with no acknowledgement; stopping\n",
                        hc->index, status);
                fflush(stderr);
                break;
            }
        } else {
            last_status = status;
            last_ack    = hc->ack_seq;
            repeats     = 0;
        }
        g_ohci_phase = 3;
        ohci_raise(hc, status);
    }
    return 0;
}

void xbox_OhciInit(void)
{
    static int done;

    if (done)
        return;
    done = 1;

    /* Opt-in. Without a descriptor list walker behind it a driver that finds
     * a port has nothing to enumerate, so this must not change how a title
     * behaves until the rest of it exists. */
    if (!getenv("RECOMP_USB"))
        return;

    s_enabled = 1;
    s_trace   = getenv("RECOMP_USB_TRACE") != NULL;
    {
        const char *hcspec = getenv("RECOMP_USB_HC");
        const char *ndpspec = getenv("RECOMP_USB_NDP");
        s_device_hc = (hcspec && atoi(hcspec) == 1) ? 1 : 0;
        if (ndpspec) {
            int n = atoi(ndpspec);
            if (n >= 1 && n <= 4) s_ndp = (unsigned)n;
        }
    }
    ohci_reset(&s_hc[0], XBOX_OHCI0_BASE, 0);
    ohci_reset(&s_hc[1], XBOX_OHCI1_BASE, 1);

    /* The registers have to fault to be answered. The MCPX aperture is mapped
     * as plain committed memory, so both blocks are made inaccessible here and
     * the title's VEH routes the faults back to xbox_OhciHandleMmio.
     *
     * A failed protect switches the model off rather than leaving it half on:
     * a controller whose registers read as zero out of RAM is exactly the
     * situation this exists to end, and it would look identical. */
    {
        ptrdiff_t off = xbox_GetMemoryOffset();
        int i;

        if (!off) {
            s_enabled = 0;
            fprintf(stderr, "  OHCI: guest memory not mapped yet; disabled\n");
            return;
        }
        for (i = 0; i < 2; i++) {
            DWORD old_protect;
            LPVOID at = (LPVOID)((uintptr_t)off + s_hc[i].base);
            if (!VirtualProtect(at, XBOX_OHCI_SIZE, PAGE_NOACCESS,
                                &old_protect)) {
                s_enabled = 0;
                fprintf(stderr, "  OHCI: cannot trap 0x%08X (error %lu); "
                                "disabled\n", s_hc[i].base, GetLastError());
                return;
            }
        }
    }

    fprintf(stderr, "  OHCI: two controllers at 0x%08X and 0x%08X, "
                    "%d ports each, one device on HC0 port 1\n",
            XBOX_OHCI0_BASE, XBOX_OHCI1_BASE, OHCI_PORTS);
    fflush(stderr);

    {
        HANDLE th = CreateThread(NULL, 0, ohci_thread, NULL, 0, NULL);
        /* It calls the title's ISR: guest code, so it lives on the guest core. */
        if (th && getenv("RECOMP_GUEST_AFFINITY")) SetThreadAffinityMask(th, 1);
        if (th)
            CloseHandle(th);
    }
}

static OhciController *hc_for(uint32_t va)
{
    int i;

    if (!s_enabled)
        return NULL;
    for (i = 0; i < 2; i++)
        if (va >= s_hc[i].base && va < s_hc[i].base + XBOX_OHCI_SIZE)
            return &s_hc[i];
    return NULL;
}

int xbox_OhciOwnsAddress(uint32_t xbox_va)
{
    return hc_for(xbox_va) != NULL;
}

int xbox_OhciHandleMmio(void *ctx, uint32_t xbox_va)
{
    OhciController *hc = hc_for(xbox_va);
    int ok;

    if (!hc)
        return 0;
    ok = mmio_emulate((PCONTEXT)ctx, xbox_va - hc->base, hc,
                      ohci_read, ohci_write);
    if (!ok && hc->decode_fail++ < 20) {
        const uint8_t *ip = (const uint8_t *)((PCONTEXT)ctx)->Rip;
        fprintf(stderr, "  [OHCI%d] undecoded access at +0x%03X: "
                        "%02X %02X %02X %02X %02X %02X\n",
                hc->index, xbox_va - hc->base,
                ip[0], ip[1], ip[2], ip[3], ip[4], ip[5]);
        fflush(stderr);
    }
    return ok;
}

void xbox_OhciReport(void)
{
    int i;

    if (!s_enabled)
        return;
    for (i = 0; i < 2; i++)
        fprintf(stderr, "  [OHCI%d] %u reads, %u writes, %u undecoded; "
                        "HcControl=%08X HcIntStatus=%08X port1=%08X\n",
                i, s_hc[i].reads, s_hc[i].writes, s_hc[i].decode_fail,
                s_hc[i].reg[HcControl / 4],
                s_hc[i].reg[HcInterruptStatus / 4],
                s_hc[i].reg[HcRhPortStatus1 / 4]);
    fflush(stderr);
}
