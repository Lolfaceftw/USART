/**
 * @file platform/usart.c
 * @brief Platform-support routines, USART component
 *
 * @author Alberto de Villa <alberto.de.villa@eee.upd.edu.ph>
 * @date   28 Oct 2024
 */

/*
 * PIC32CM5164LS00048 initial configuration:
 * -- Architecture: ARMv8 Cortex-M23
 * -- GCLK_GEN0: OSC16M @ 4 MHz, no additional prescaler
 * -- Main Clock: No additional prescaling (always uses GCLK_GEN0 as input)
 * -- Mode: Secure, NONSEC disabled
 * 
 * HW configuration for the corresponding Curiosity Nano+ Touch Evaluation
 * Board:
 * -- ???: UART via debugger (TX, SERCOM03, PAD[0]); PB09 PAD[1]
 * -- ???: UART via debugger (RX, SERCOM03, PAD[1]); PB08 PAD[0]
 */

// Common include for the XC32 compiler
#include <xc.h>
#include <stdbool.h>
#include <string.h>

#include "../platform.h"

// Functions "exported" by this file
void platform_usart_init(void);
void platform_usart_tick_handler(const platform_timespec_t *tick);

/////////////////////////////////////////////////////////////////////////////

/**
 * State variables for UART
 * 
 * NOTE: Since these are shared between application code and interrupt handlers
 *       (SysTick and SERCOM), these must be declared volatile.
 */
typedef struct ctx_usart_type {
    /// Pointer to the underlying register set
    sercom_usart_int_registers_t *regs;

    /// State variables for the transmitter

    struct {
        volatile platform_usart_tx_bufdesc_t *desc;
        volatile uint16_t nr_desc;

        // Current descriptor
        volatile const char *buf;
        volatile uint16_t len;
    } tx;

    /// State variables for the receiver

    struct {
        /// Receive descriptor, held by the client
        volatile platform_usart_rx_async_desc_t * volatile desc;

        /// Tick since the last character was received
        volatile platform_timespec_t ts_idle;

        /// Index at which to place an incoming character
        volatile uint16_t idx;

    } rx;

    /// Configuration items

    struct {
        /// Idle timeout (reception only)
        platform_timespec_t ts_idle_timeout;
    } cfg;

} ctx_usart_t;
static ctx_usart_t ctx_uart;

// Configure USART

void platform_usart_init(void) {
    /*
     * For ease of typing, #define a macro corresponding to the SERCOM
     * peripheral and its internally-clocked USART view.
     * 
     * To avoid namespace pollution, this macro is #undef'd at the end of
     * this function.
     */
#define UART_REGS (&(SERCOM3_REGS->USART_INT))

    /*
     * Enable the APB clock for this peripheral
     * 
     * NOTE: The chip resets with it enabled; hence, commented-out.
     * 
     * WARNING: Incorrect MCLK settings can cause system lockup that can
     *          only be rectified via a hardware reset/power-cycle.
     */
    //18.6
    MCLK_REGS->MCLK_APBCMASK |= (1 << 4);

    /*
     * Enable the GCLK generator for this peripheral
     * 
     * NOTE: GEN2 (4 MHz) is used, as GEN0 (24 MHz) is too fast for our
     *       use case.
     */
    // 17.7.5
    GCLK_REGS->GCLK_PCHCTRL[20] = 0x00000042;
    while ((GCLK_REGS->GCLK_PCHCTRL[20] & 0x00000040) == 0);

    // Initialize the peripheral's context structure
    memset(&ctx_uart, 0, sizeof (ctx_uart));
    ctx_uart.regs = UART_REGS;

    /*
     * This is the classic "SWRST" (software-triggered reset).
     * 
     * NOTE: Like the TC peripheral, SERCOM has differing views depending
     *       on operating mode (USART_INT for UART mode). CTRLA is shared
     *       across all modes, so set it first after reset.
     */
    // 34.7.1: Reset
    UART_REGS->SERCOM_CTRLA = (1 << 0);
    while ((UART_REGS -> SERCOM_SYNCBUSY & (1 << 0)) != 0);
    UART_REGS->SERCOM_CTRLA = (0x1 << 2); // Internally clocked
    /*
     * Select further settings compatible with the 16550 UART:
     * 
     * - x16-bit oversampling, arithmetic mode (for noise immunity)
     * - xLSB first
     * - xNo parity
     * - xTwo stop bits
     * - x8-bit character size
     * - xNo break detection
     * 
     * - xUse PAD[0] for data transmission
     * - xUse PAD[1] for data reception
     * 
     * NOTE: If a control register is not used, comment it out.
     */
    // For sake of clarity, I still added configurations where their bit position is zero.
    // 34.7.1
    UART_REGS->SERCOM_CTRLA |= (1 << 30); // LSB First
    UART_REGS->SERCOM_CTRLA |= (0x1 << 20); // PAD[1] Rx
    UART_REGS->SERCOM_CTRLB |= (0 << 6); // 1 Stop Bit
    
    UART_REGS->SERCOM_CTRLA |= (0x1 << 24); // USART frame w/ parity
    UART_REGS->SERCOM_CTRLB |= (0 << 13); // Even Parity
    UART_REGS->SERCOM_CTRLA |= (0x0 << 13); // 16 bit oversampling, arithmetic
    UART_REGS->SERCOM_CTRLA |= (0x0 << 16); // PAD[0] Tx
    UART_REGS->SERCOM_CTRLB |= (0 << 8); // No collision detection
    UART_REGS->SERCOM_CTRLB |= (0x0 << 0); // 8 bits
    UART_REGS->SERCOM_CTRLC |= (0 << 27); // FIFO disabled


    /*
     * This value is determined from f_{GCLK} and f_{baud}, the latter
     * being the actual target baudrate (here, 57600 bps).
     */
    // SERCOM_BAUD = 65536 (1 - (16*57600)/4e6)
    UART_REGS->SERCOM_BAUD = 0xC505; // 50437
    /*
     * Configure the IDLE timeout, which should be the length of 3
     * USART characters.
     * 
     * NOTE: Each character is composed of 12 bits (must include parity
     *       and stop bits); add one bit for margin purposes. In addition,
     *       for UART one baud period corresponds to one bit.
     */
    // start bit - 1; stop bits - 1; 8 bits/char; 1 bit margin; 1 parity bit; 12 bits total

    ctx_uart.cfg.ts_idle_timeout.nr_sec = 0;
    ctx_uart.cfg.ts_idle_timeout.nr_nsec = 0xE4E1C;
    // 12*3 = 36 / baud rate = 937500 nano seconds
    /*
     * Third-to-the-last setup:
     * 
     * - Enable receiver and transmitter
     * - Clear the FIFOs (even though they're disabled)
     */
    
    UART_REGS->SERCOM_CTRLB |= (1 << 16) | (0x3 << 22) | (1 << 17);

    while ((UART_REGS->SERCOM_SYNCBUSY & (1 << 2)) != 0);

    /*
     * Second-to-last: Configure the physical pins.
     * 
     * NOTE: Consult both the chip and board datasheets to determine the
     *       correct port pins to use.
     */
    // D Peripheral for SERCOM
    // PB 08 rx; 
    PORT_SEC_REGS -> GROUP[1].PORT_PINCFG[8] |= (0x3 << 0);
    PORT_SEC_REGS -> GROUP[1].PORT_PMUX[4] |= (0x3 << 0);
    // PB 09 tx; 
    PORT_SEC_REGS -> GROUP[1].PORT_PINCFG[9] |= (0x3 << 0);
    PORT_SEC_REGS -> GROUP[1].PORT_PMUX[4] |= (0x3 << 4);

    // Last: enable the peripheral, after resetting the state machine
    UART_REGS->SERCOM_CTRLA |= (1 << 1);
    while ((UART_REGS -> SERCOM_SYNCBUSY & (1 << 1)) != 0);
    return;

#undef UART_REGS
}

// Helper abort routine for USART reception

static void usart_rx_abort_helper(ctx_usart_t *ctx) {
    if (ctx->rx.desc != NULL) {
        ctx->rx.desc->compl_type = PLATFORM_USART_RX_COMPL_DATA;
        ctx->rx.desc->compl_info.data_len = ctx->rx.idx;
        ctx->rx.desc = NULL;
    }
    ctx->rx.ts_idle.nr_sec = 0;
    ctx->rx.ts_idle.nr_nsec = 0;
    ctx->rx.idx = 0;
    return;
}

// Tick handler for the USART

static void usart_tick_handler_common(
        ctx_usart_t *ctx, const platform_timespec_t *tick) {
    uint16_t status = 0x0000;
    uint8_t data = 0x00;
    platform_timespec_t ts_delta;

    // TX handling
    if ((ctx->regs->SERCOM_INTFLAG & (1 << 0)) != 0) {
        if (ctx->tx.len > 0) {
            /*
             * There is still something to transmit in the working
             * copy of the current descriptor.
             */
            ctx->regs->SERCOM_DATA = *(ctx->tx.buf++);
            --ctx->tx.len;
        }
        if (ctx->tx.len == 0) {
            // Load a new descriptor
            ctx->tx.buf = NULL;
            if (ctx->tx.nr_desc > 0) {
                /*
                 * There's at least one descriptor left to
                 * transmit
                 * 
                 * If either ->buf or ->len of the candidate
                 * descriptor refer to an empty buffer, the
                 * next invocation of this routine will cause
                 * the next descriptor to be evaluated.
                 */
                ctx->tx.buf = ctx->tx.desc->buf;
                ctx->tx.len = ctx->tx.desc->len;

                ++ctx->tx.desc;
                --ctx->tx.nr_desc;

                if (ctx->tx.buf == NULL || ctx->tx.len == 0) {
                    ctx->tx.buf = NULL;
                    ctx->tx.len = 0;
                }
            } else {
                /*
                 * No more descriptors available
                 * 
                 * Clean up the corresponding context data so
                 * that we don't trip over them on the next
                 * invocation.
                 */
                ctx->regs->SERCOM_INTENCLR = 0x01;
                ctx->tx.desc = NULL;
                ctx->tx.buf = NULL;
            }
        }
    }

    // RX handling
    if ((ctx->regs->SERCOM_INTFLAG & (1 << 2)) != 0) {
        /*
         * There are unread data
         * 
         * To enable readout of error conditions, STATUS must be read
         * before reading DATA.
         * 
         * NOTE: Piggyback on Bit 15, as it is undefined for this
         *       platform.
         */
        status = ctx->regs->SERCOM_STATUS | 0x8000;
        data = (uint8_t) (ctx->regs->SERCOM_DATA);
    }
    do {
        if (ctx->rx.desc == NULL) {
            // Nowhere to store any read data
            break;
        }

        if ((status & 0x8003) == 0x8000) {
            // No errors detected
            ctx->rx.desc->buf[ctx->rx.idx++] = data;
            ctx->rx.ts_idle = *tick;
        }
        ctx->regs->SERCOM_STATUS |= (status & 0x00F7);

        // Some housekeeping
        if (ctx->rx.idx >= ctx->rx.desc->max_len) {
            // Buffer completely filled
            usart_rx_abort_helper(ctx);
            break;
        } else if (ctx->rx.idx > 0) {
            platform_tick_delta(&ts_delta, tick, &ctx->rx.ts_idle);
            if (platform_timespec_compare(&ts_delta, &ctx->cfg.ts_idle_timeout) >= 0) {
                // IDLE timeout
                usart_rx_abort_helper(ctx);
                break;
            }
        }
    } while (0);

    // Done
    return;
}

void platform_usart_tick_handler(const platform_timespec_t *tick) {
    usart_tick_handler_common(&ctx_uart, tick);
}

/// Maximum number of bytes that may be sent (or received) in one transaction
#define NR_USART_CHARS_MAX (65528)

/// Maximum number of fragments for USART TX
#define NR_USART_TX_FRAG_MAX (32)

// Enqueue a buffer for transmission

static bool usart_tx_busy(ctx_usart_t *ctx) {
    return (ctx->tx.len > 0) || (ctx->tx.nr_desc > 0) ||
            ((ctx->regs->SERCOM_INTFLAG & (1 << 0)) == 0);
}

static bool usart_tx_async(ctx_usart_t *ctx,
        const platform_usart_tx_bufdesc_t *desc,
        unsigned int nr_desc) {
    uint16_t avail = NR_USART_CHARS_MAX;
    unsigned int x, y;

    if (!desc || nr_desc == 0)
        return true;
    else if (nr_desc > NR_USART_TX_FRAG_MAX)
        // Too many descriptors
        return false;

    // Don't clobber an existing buffer
    if (usart_tx_busy(ctx))
        return false;

    for (x = 0, y = 0; x < nr_desc; ++x) {
        if (desc[x].len > avail) {
            // IF the message is too long, don't enqueue.
            return false;
        }

        avail -= desc[x].len;
        ++y;
    }

    // The tick will trigger the transfer
    ctx->tx.desc = desc;
    ctx->tx.nr_desc = nr_desc;
    return true;
}

static void usart_tx_abort(ctx_usart_t *ctx) {
    ctx->tx.nr_desc = 0;
    ctx->tx.desc = NULL;
    ctx->tx.len = 0;
    ctx->tx.buf = NULL;
    return;
}

// API-visible items

bool platform_usart_cdc_tx_async(
        const platform_usart_tx_bufdesc_t *desc,
        unsigned int nr_desc) {
    return usart_tx_async(&ctx_uart, desc, nr_desc);
}

bool platform_usart_cdc_tx_busy(void) {
    return usart_tx_busy(&ctx_uart);
}

void platform_usart_cdc_tx_abort(void) {
    usart_tx_abort(&ctx_uart);
    return;
}

// Begin a receive transaction

static bool usart_rx_busy(ctx_usart_t *ctx) {
    return (ctx->rx.desc) != NULL;
}

static bool usart_rx_async(ctx_usart_t *ctx, platform_usart_rx_async_desc_t *desc) {
    // Check some items first
    if (!desc || !desc->buf || desc->max_len == 0 || desc->max_len > NR_USART_CHARS_MAX)
        // Invalid descriptor
        return false;

    if ((ctx->rx.desc) != NULL)
        // Don't clobber an existing buffer
        return false;

    desc->compl_type = PLATFORM_USART_RX_COMPL_NONE;
    desc->compl_info.data_len = 0;
    ctx->rx.idx = 0;
    platform_tick_hrcount(&ctx->rx.ts_idle);
    ctx->rx.desc = desc;
    return true;
}

// API-visible items

bool platform_usart_cdc_rx_async(platform_usart_rx_async_desc_t *desc) {
    return usart_rx_async(&ctx_uart, desc);
}

bool platform_usart_cdc_rx_busy(void) {
    return usart_rx_busy(&ctx_uart);
}

void platform_usart_cdc_rx_abort(void) {
    usart_rx_abort_helper(&ctx_uart);
}
