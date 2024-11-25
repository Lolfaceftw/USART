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
static const char banner_msg[] =
"\033[0m\033[2J\033[1;1H"
"+--------------------------------------------------------------------+\r\n"
"| EEE 158: Electrical and Electronics Engineering Laboratory V       |\r\n"
"|          Academic Year 2024-2025, Semester 1                       |\r\n"
"|                                                                    |\r\n"
"| Sample: \"Hexdump...\"                                               |\r\n"
"|                                                                    |\r\n"
"| Author:  EEE 158 Handlers (Almario, de Villa, Nierva, Sison, Tuso) |\r\n"
"| Date:    21 Oct 2024                                               |\r\n"
"+--------------------------------------------------------------------+\r\n"
"\r\n"
"Last-pressed key sequence (hex dump): <None> \r\n";

static const char ESC_SEQ_KEYP_LINE[] = "\033[11;39H\033[0K";
static const char ESC_SEQ_IDLE_INF[]  = "\033[12;1H";

//////////////////////////////////////////////////////////////////////////////

// Program state machine
typedef struct prog_state_type
{
	// Flags for this program
#define PROG_FLAG_BANNER_PENDING	0x0001	// Waiting to transmit the banner
#define PROG_FLAG_UPDATE_PENDING	0x0002	// Waiting to transmit updates
#define PROG_FLAG_GEN_COMPLETE		0x8000	// Message generation has been done, but transmission has not occurred
	uint16_t flags;
	
	// Transmit stuff
	platform_usart_tx_bufdesc_t tx_desc[4];
	char tx_buf[64];
	uint16_t tx_blen;
	
	// Receiver stuff
	platform_usart_rx_async_desc_t rx_desc;
	uint16_t rx_desc_blen;
	char rx_desc_buf[16];
} prog_state_t;

/*
 * Initialize the main program state
 * 
 * This style might be familiar to those accustomed to he programming
 * conventions employed by the Arduino platform.
 */
static void prog_setup(prog_state_t *ps)
{
	memset(ps, 0, sizeof(*ps));
	
	platform_init();
	
	ps->rx_desc.buf     = ps->rx_desc_buf;
	ps->rx_desc.max_len = sizeof(ps->rx_desc_buf);
	
	platform_usart_cdc_rx_async(&ps->rx_desc);
	return;
}

/*
 * Do a single loop of the main program
 * 
 * This style might be familiar to those accustomed to he programming
 * conventions employed by the Arduino platform.
 */
static void prog_loop_one(prog_state_t *ps)
{
	uint16_t a = 0, b = 0, c = 0;
	
	// Do one iteration of the platform event loop first.
	platform_do_loop_one();
	
	// Something happened to the pushbutton?
	if ((a = platform_pb_get_event()) != 0) {
		if ((a & PLATFORM_PB_ONBOARD_PRESS) != 0) {
			// Print out the banner
			ps->flags |= PROG_FLAG_BANNER_PENDING;
		}
		a = 0;
	}
	
	// Something from the UART?
	if (ps->rx_desc.compl_type == PLATFORM_USART_RX_COMPL_DATA) {
		/*
		 * There's something.
		 * 
		 * The completion-info payload contains the number of bytes
		 * read into the receive buffer.
		 */ 
		ps->flags |= PROG_FLAG_UPDATE_PENDING;
		ps->rx_desc_blen = ps->rx_desc.compl_info.data_len;
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
			ps->tx_desc[0].len = sizeof(banner_msg)-1;
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
			ps->tx_desc[0].len = sizeof(ESC_SEQ_KEYP_LINE)-1;
			ps->tx_desc[2].buf = ESC_SEQ_IDLE_INF;
			ps->tx_desc[2].len = sizeof(ESC_SEQ_IDLE_INF)-1;

			// Echo back the received packet as a hex dump.
			memset(ps->tx_buf, 0, sizeof(ps->tx_buf));
			if (ps->rx_desc_blen > 0) {
				ps->tx_desc[1].len = 0;
				ps->tx_desc[1].buf = ps->tx_buf;
				for (a = 0, c = 0; a < ps->rx_desc_blen && c < sizeof(ps->tx_buf)-1; ++a) {
					b = snprintf(ps->tx_buf + c,
						     sizeof(ps->tx_buf) - c - 1,
						"%02X ", (char)(ps->rx_desc_buf[a] & 0x00FF)
						);
					c += b;
				}
				ps->tx_desc[1].len = c;
			} else {
				ps->tx_desc[1].len = 7;
				ps->tx_desc[1].buf = "<None> ";
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
int main(void)
{
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
