// Common include for the XC32 compiler
#include <xc.h>
#include <stdbool.h>
#include <string.h>

#include "../platform.h"

/*
 * @brief This is the function to initialize USART as has been requested by the documentation at Module 05.
 */
void platform_usart_init(void) {
    /*  
     * 1. Enable the APB clock/s for the appropriate SERCOM instance/s. For this module, use the SERCOM instance
     * that is connected to the debugger on-board the Curiosity Nano.
     * 
     * 2. Configure and enable the corresponding GCLK peripheral channel/s.
     * 
     * 3. Trigger a reset of the peripheral by setting the CTRLA.SWRST bit to one.
     * 
     * 4. Configure the peripheral for the following settings:
     * Internally Clocked
     * 2 Stop Bits
     * 8 Bit Character Size
     * None Parity Mode
     * None Flow Control
     * [0]/[1] TX/RX Pad
     * LSB First Data Order
     * FIFO Disabled
     * Arithmetic Baud Mode
     * 38400 Baud Rate
     * 16 Samples / Bit
     * 
     * 5. Program the baud rate by writing the value to SERCOM_BAUD corresponding to the target baud rate.
     * 
     * 6. Enable both the transmitter and receiver by setting the CTRLB.RXEN and CTRLB.TXEN bits
     * 
     * 7. Enable the peripheral itself by setting CTRLA.ENABLE to 1.
     */
    
    // 34.7.1
    SERCOM0_USART_REGS -> SERCOM0_USART_CTRLA |= (0 << 1); // Set Enable to 0 to write.
    // 34.7.1
    SERCOM0_USART_REGS -> SERCOM0_USART_CTRLA |= (1 << 0); // Reset all registers by setting SWRST to 1.
    // 34.7.1
    SERCOM0_USART_REGS -> SERCOM0_USART_CTRLA |= (0x1 << 2); // Internally clocked
    
    

}

void platform_usart_tick_handler(const platform_timespec_t *tick) {
    // Do nothing for now.
}