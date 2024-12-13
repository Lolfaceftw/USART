/**
 * @file main.c
 * @brief Module 5 Sample: "Keystroke Hexdump"
 *
 * @author Alberto de Villa <alberto.de.villa@eee.upd.edu.ph>
 * @date 28 Oct 2024
 */

// Common include for the XC32 compiler
#include <xc.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "platform.h"

/////////////////////////////////////////////////////////////////////////////

/*
 * Copyright message printed upon reset
 * 
 * Displaying author information is optional; but as always, must be present
 * as comments at the top of the source file for copyright purposes.
 * 
 * FIXME: Modify this prompt message to account for additional instructions.
 */
#define HOME_KEY 0x1B    // ASCII for Home key
#define CTRL_E 0x05     // ASCII for CTRL+E
static char result[] = "test";
static const char banner_msg[] =
        "\033[0m\033[2J\033[1;1H"
        "+--------------------------------------------------------------------+\r\n"
        "| EEE 158: Electrical and Electronics Engineering Laboratory V       |\r\n"
        "|          Academic Year 2024-2025, Semester 1                       |\r\n"
        "|                                                                    |\r\n"
        "| Solution: Graded Exercise                                          |\r\n"
        "|                                                                    |\r\n"
        "| Author:  EEE 158 Handlers (Almario, de Villa, Nierva, Sison, Tuso) |\r\n"
        "| Date:    21 Oct 2024                                               |\r\n"
        "+--------------------------------------------------------------------+\r\n"
        "\r\n"
        "On-board button: [Released]\r\n"
        "Blink Setting: [   OFF  ]\r\n";
//"Blink Setting: [   ON   ]\r\n"
//"Blink Setting: [  SLOW  ]\r\n"
//"Blink Setting: [ MEDIUM ]\r\n"
//"Blink Setting: [  FAST  ]\r\n";
// \033[ Control Sequence Introducer
// 0m (SGR) Resets all text attributes
// 2J (ED)Clear entire screen
// 1;1H (CUP) Moves cursor to row 1 column 1

static const char ESC_SEQ_KEYP_LINE[] = "\033[11;19H\033[0K";
// \033[ Control Sequence 
// 11;39H (CUP) Moves cursor to row 11 column 39
// 0K (EL) Clear from cursor to end of line \033[0K

static const char ESC_SEQ_IDLE_INF[] = "\033[12;17H";
// \033[ Control Sequence
// 12;1H (CUP) Moves cursor to row 12 column 1

//////////////////////////////////////////////////////////////////////////////

// Program state machine

typedef struct prog_state_type {
    // Flags for this program
#define PROG_FLAG_BANNER_PENDING	0x0001	// Waiting to transmit the banner
#define PROG_FLAG_UPDATE_PENDING	0x0002	// Waiting to transmit updates
#define PROG_FLAG_GEN_COMPLETE		0x8000	// Message generation has been done, but transmission has not occurred; 32768; 2**15

    uint16_t flags;

    // Transmit stuff
    /*
     * Declares a four element array with the buffer and length of the message.
     */
    platform_usart_tx_bufdesc_t tx_desc[4];

    char tx_buf[64];
    uint16_t tx_blen; // [0, 65535]

    // Receiver stuff
    platform_usart_rx_async_desc_t rx_desc; // Buffer, length, type of completion; if applicable, completion info
    uint16_t rx_desc_blen;
    char rx_desc_buf[16];
} prog_state_t;

/*
 * Initialize the main program state
 * 
 * This style might be familiar to those accustomed to he programming
 * conventions employed by the Arduino platform.
 */
static void prog_setup(prog_state_t *ps) {
    memset(ps, 0, sizeof (*ps));

    platform_init();

    ps->rx_desc.buf = ps->rx_desc_buf;
    ps->rx_desc.max_len = sizeof (ps->rx_desc_buf);

    platform_usart_cdc_rx_async(&ps->rx_desc);
    return;
}

// Blink Settings

typedef enum {
    OFF,
    ON,
    SLOW,
    MEDIUM,
    FAST,
    NUM_SETTINGS
} BlinkSetting;

static const char *blinkSettingStrings[NUM_SETTINGS] = {
    "[   OFF  ]\r\n",
    "[   ON   ]\r\n",
    "[  SLOW  ]\r\n",
    "[ MEDIUM ]\r\n",
    "[  FAST  ]\r\n"
};

static BlinkSetting currentSetting = OFF;
static const char CHANGE_MODE[] = "\033[12;16H";
static void updateBlinkSetting(prog_state_t *ps, bool increase) {
    if (increase) {
        if (currentSetting < FAST) {
            currentSetting++;
        }
    } else {
        if (currentSetting > OFF) {
            currentSetting--;
        }
    }

    ps->tx_desc[0].buf = CHANGE_MODE; // Move cursor to blink setting line
    ps->tx_desc[0].len = sizeof (CHANGE_MODE) - 1;
    ps->tx_desc[1].buf = blinkSettingStrings[currentSetting];
    ps->tx_desc[1].len = strlen(blinkSettingStrings[currentSetting]);
    platform_usart_cdc_tx_async(ps->tx_desc, 2);
}

/*
 * Do a single loop of the main program
 * 
 * This style might be familiar to those accustomed to he programming
 * conventions employed by the Arduino platform.
 */
int init = 0;
// Add these escape sequences at the top with other static constants
static const char ESC_SEQ_BUTTON_POS[] = "\033[11;19H"; // Position cursor at button state
static const char BUTTON_PRESSED[] = "Pressed] ";
static const char BUTTON_RELEASED[] = "Released]";

static void prog_loop_one(prog_state_t *ps) {
    uint16_t a = 0, b = 0, c = 0;

    // Do one iteration of the platform event loop first.
    platform_do_loop_one();
    // Print out the banner
    if (init == 0) {
        //ps->flags |= PROG_FLAG_BANNER_PENDING;
        ps->tx_desc[0].buf = banner_msg;
        ps->tx_desc[0].len = sizeof (banner_msg) - 1;
        platform_usart_cdc_tx_async(&ps->tx_desc[0], 1);
        init = 1;
    }
    // Something happened to the pushbutton?
    if ((a = platform_pb_get_event()) != 0) {
        if ((a & PLATFORM_PB_ONBOARD_PRESS) != 0) {
            ps->tx_desc[0].buf = ESC_SEQ_BUTTON_POS;
            ps->tx_desc[0].len = sizeof (ESC_SEQ_BUTTON_POS) - 1;
            ps->tx_desc[1].buf = BUTTON_PRESSED;
            ps->tx_desc[1].len = sizeof (BUTTON_PRESSED) - 1;
            platform_usart_cdc_tx_async(&ps->tx_desc[0], 2);
        } else if ((a & PLATFORM_PB_ONBOARD_RELEASE) != 0) {
            ps->tx_desc[0].buf = ESC_SEQ_BUTTON_POS;
            ps->tx_desc[0].len = sizeof (ESC_SEQ_BUTTON_POS) - 1;
            ps->tx_desc[1].buf = BUTTON_RELEASED;
            ps->tx_desc[1].len = sizeof (BUTTON_RELEASED) - 1;
            platform_usart_cdc_tx_async(&ps->tx_desc[0], 2);
        }
    }

    // Something from the UART?
    if (ps->rx_desc.compl_type == PLATFORM_USART_RX_COMPL_DATA) {
        char received_char = ps->rx_desc_buf[0];

        if (received_char == CTRL_E) {
            // Print banner directly
            ps->tx_desc[0].buf = banner_msg;
            ps->tx_desc[0].len = sizeof (banner_msg) - 1;
            platform_usart_cdc_tx_async(&ps->tx_desc[0], 1);
        } else if (received_char == '<' || received_char == 'A' || received_char == 'a') {
            updateBlinkSetting(ps, false);
        } else if (received_char == '>' || received_char == 'D' || received_char == 'd') {
            updateBlinkSetting(ps, true);
        } else {
            // Handle other inputs as before
            ps->flags |= PROG_FLAG_UPDATE_PENDING;
            ps->rx_desc_blen = ps->rx_desc.compl_info.data_len;
        }

        // Reset receive buffer and wait for completion
        while (platform_usart_cdc_tx_busy()) {
            platform_do_loop_one();
        }

        ps->rx_desc.compl_type = PLATFORM_USART_RX_COMPL_NONE;
        platform_usart_cdc_rx_async(&ps->rx_desc);
    }


    ////////////////////////////////////////////////////////////////////

    // Process any pending flags (BANNER)
    do {
        if ((ps->flags & PROG_FLAG_BANNER_PENDING) == 0)
            break;

        if (platform_usart_cdc_tx_busy())
            break;

        if ((ps->flags & PROG_FLAG_GEN_COMPLETE) == 0) {
            // Message has not been generated.
            ps->tx_desc[0].buf = banner_msg;
            ps->tx_desc[0].len = sizeof (banner_msg) - 1;
            ps->flags |= PROG_FLAG_GEN_COMPLETE;
        }

        if (platform_usart_cdc_tx_async(&ps->tx_desc[0], 1)) {
            ps->flags &= ~(PROG_FLAG_BANNER_PENDING | PROG_FLAG_GEN_COMPLETE);
        }
    } while (0);

    // Process any pending flags (UPDATE)
    do {
        if ((ps->flags & PROG_FLAG_UPDATE_PENDING) == 0)
            break;

        if (platform_usart_cdc_tx_busy())
            break;

        if ((ps->flags & PROG_FLAG_GEN_COMPLETE) == 0) {
            // Message has not been generated.
            ps->tx_desc[0].buf = ESC_SEQ_KEYP_LINE;
            ps->tx_desc[0].len = sizeof (ESC_SEQ_KEYP_LINE) - 1;
            ps->tx_desc[2].buf = ESC_SEQ_IDLE_INF;
            ps->tx_desc[2].len = sizeof (ESC_SEQ_IDLE_INF) - 1;

            // Echo back the received packet as a hex dump.
            memset(ps->tx_buf, 0, sizeof (ps->tx_buf));
            if (ps->rx_desc_blen > 0) {
                ps->tx_desc[1].len = 0;
                ps->tx_desc[1].buf = ps->tx_buf;
                for (a = 0, c = 0; a < ps->rx_desc_blen && c < sizeof (ps->tx_buf) - 1; ++a) {
                    char received_char = ps -> rx_desc_buf[a];
                    b = snprintf(ps->tx_buf + c,
                            sizeof (ps->tx_buf) - c - 1,
                            "%02X Pressed", (char) (ps->rx_desc_buf[a] & 0x00FF)
                            );
                    c += b;

                }
                ps->tx_desc[1].len = c;
            } else {
                ps->tx_desc[1].len = 7;
                ps->tx_desc[1].buf = "<None> xd";
            }

            ps->flags |= PROG_FLAG_GEN_COMPLETE;
            ps->rx_desc_blen = 0;
        }

        if (platform_usart_cdc_tx_async(&ps->tx_desc[0], 3)) {
            ps->rx_desc.compl_type = PLATFORM_USART_RX_COMPL_NONE;
            platform_usart_cdc_rx_async(&ps->rx_desc);
            ps->flags &= ~(PROG_FLAG_UPDATE_PENDING | PROG_FLAG_GEN_COMPLETE);
        }


    } while (0);

    // Done
    return;
}

// main() -- the heart of the program

int main(void) {
    prog_state_t ps;

    // Initialization time	
    prog_setup(&ps);

    /*
     * Microcontroller main()'s are supposed to never return (welp, they
     * have none to return to); hence the intentional infinite loop.
     */

    for (;;) {
        prog_loop_one(&ps);
    }

    // This line must never be reached
    return 1;
}
