/* synth.c
 * 
 * Ken Schmitt and Frank Sinapi
 * Microprocessor Systems Fall 2017 at RPI
 * ------------------------------------------------------------------------------------
 * This is a program to interface the C8051F120 with a YM2413 FM voice chip
 * The program accepts MIDI input from the UART0 line, as well as keyboard input 	 
 * 
 * Compiled with SDCC 3.5.0 targetting C8051F120*/

#include <c8051f120.h>
#include <stdio.h>
#include <stdint.h>
// #include <putget.h>
#include <stdlib.h>
#include "YM2413.h"
#include "keyboard.h"
//------------------------------------------------------------------------------------
// Global Constants
//------------------------------------------------------------------------------------
#define EXTCLK      22118400    // External oscillator frequency in Hz
#define SYSCLK      49766400            // Output of PLL derived from (EXTCLK * 9/4)
#define BAUDRATE    115200              // UART baud rate in bps

#define SYSCLK_D_12	(SYSCLK / 12)		// Sys clock divided by 12

#define TIMER_FREQ  (194400)      // Frequency of timer 2 in Hz
#define TICKS_T2    (SYSCLK / TIMER_FREQ)   // Number of ticks for 0.01 seconds
#define T2_PRELOAD  ((0xFFFF) - TICKS_T2)   // Subtract ticks from T2 overflow level

#define NOTE_ON_OPCODE 0x90
#define NOTE_OFF_OPCODE 0x80

#define NOTE_OFFSET	36
#define KEYBOARD_VOL (0x2F)

//-------------------------------------------------------------------------------------------
// Global Vars
//-------------------------------------------------------------------------------------------
typedef enum {
	WAITING, 
	ONE_BYTE, 
	TWO_BYTES,
	KEYBOARD_MODE
} state_t;

typedef struct {
	uint8_t opcode;
	uint8_t instrument;
	uint8_t note;
	uint8_t vol;
} message_t;

state_t state = WAITING;
message_t message;

keyboard_t keyboard;

uint16_t T2_Overflows;
uint16_t keysPressed;
inst_t kbdInstrument = piano;

__sbit __at (0xC8) MODE_PIN;

//-------------------------------------------------------------------------------------------
// Function PROTOTYPES
//-------------------------------------------------------------------------------------------
void main(void);

void PORT_INIT(void);
void SYSCLK_INIT(void);
void UART0_INIT(void);
void T2_INIT(void);
void delay_us(uint16_t waitTime);
char checkModePin(void);

state_t waiting(char input);
state_t one_byte(char input);
state_t two_bytes(char input);

void putchar(char c);
char getchar(void);

void SW_ISR (void) __interrupt 0;

//-------------------------------------------------------------------------------------------
// MAIN Routine
//-------------------------------------------------------------------------------------------
void main (void)
{
	char input;
	uint8_t i;

    SFRPAGE = CONFIG_PAGE;

    PORT_INIT();                // Configure the Crossbar and GPIO.
    SYSCLK_INIT();              // Initialize the oscillator.
    UART0_INIT();               // Initialize UART0.
    T2_INIT();                  // Initialize Timer2

    synthInit();
    initKeyboard(&keyboard);
    
    SFRPAGE = UART0_PAGE;       // Direct the output to UART0

	//while(1) testSynth();
    while(1)
    {   
    	if(checkModePin())
    	{
    		// MIDI MODE
    		if(state == KEYBOARD_MODE)
    		{
    			killAll();
    			state = WAITING;
    		}
    		// Get a new byte ASAP
	    	input = getchar();
	    	// Handle the input
			
			switch(state)
	    	{
	    		case WAITING:
	    			state = waiting(input);
	    			break;
	    		case ONE_BYTE:
	    			state = one_byte(input);
	    			break;
				case TWO_BYTES:
					state = two_bytes(input);
	    			break;
	    		default:
	    			break;
	    	}
			
	    	//putchar(input);
    	}
    	else
    	{
    		// KEYBOARD MODE
    		updateKeyboard(&keyboard);
    		if(state != KEYBOARD_MODE)
    		{
    			killAll();
    			state = KEYBOARD_MODE;
    		}
    		for(i = 0; i < NUM_KEYS; ++i)
    		{
    			// Only act if state changed
    			if(bitState(keyboard.current, i) ^ bitState(keyboard.last, i))
    			{
    				if(bitState(keyboard.current, i))
					{
    					noteOn(i + NOTE_OFFSET, kbdInstrument, KEYBOARD_VOL);
						printf("Key = %i\r\n", i+NOTE_OFFSET);
					}
    				else
    					noteOff(i + NOTE_OFFSET, kbdInstrument);
    			}
    		}
    	}
    }
}

//-------------------------------------------------------------------------------------------
// Interrupt Service Routines
//-------------------------------------------------------------------------------------------
// Keep track of T2 overflows of seconds
void T2_ISR (void) __interrupt 5   // Interrupt 5 corresponds to Timer 2 Overflow
{
    TF2     = 0;                // Clear overflow flag
    ++T2_Overflows;             // Increment overflows
}


//-------------------------------------------------------------------------------------------
// PORT_Init
//-------------------------------------------------------------------------------------------
//
// Configure the Crossbar and GPIO ports
//
void PORT_INIT(void)
{
    char SFRPAGE_SAVE;

    SFRPAGE_SAVE = SFRPAGE;     // Save Current SFR page.

    SFRPAGE = CONFIG_PAGE;
    WDTCN   = 0xDE;             // Disable watchdog timer.
    WDTCN   = 0xAD;
    EA      = 1;                // Enable interrupts as selected.

    XBR0    = 0x04;             // Enable UART0.
    XBR1    = 0x04;             // /INT0 routed to port pin (P0.2)
    XBR2    = 0x40;             // Enable Crossbar and weak pull-ups.

    P0MDOUT = 0x01;             // P0.0 (TX0) is configured as Push-Pull for output.
                                // P0.1 (RX0) is configure as Open-Drain input.
                                // P0.2 (SW2 through jumper wire) is configured as Open_Drain
                                //       for input.
    P0      = 0x06;             // Additionally, set P0.0=0, P0.1=1, and P0.2=1.

    P4MDOUT =  0xFE;
    P4 		|= 0x01;

    EX0     = 1; 				// Enable /INT0
    
    SFRPAGE = SFRPAGE_SAVE;     // Restore SFR page.
}

//-------------------------------------------------------------------------------------------
// SYSCLK_Init
//-------------------------------------------------------------------------------------------
//
// Initialize the system clock
//
void SYSCLK_INIT(void)
{
    int i;

    char SFRPAGE_SAVE;

    SFRPAGE_SAVE = SFRPAGE;     // Save Current SFR page.

    SFRPAGE = CONFIG_PAGE;
    OSCXCN  = 0x67;             // Start external oscillator
    for(i=0; i < 256; i++);     // Wait for the oscillator to start up.
    while(!(OSCXCN & 0x80));    // Check to see if the Crystal Oscillator Valid Flag is set.
    CLKSEL  = 0x01;             // SYSCLK derived from the External Oscillator circuit.
    OSCICN  = 0x00;             // Disable the internal oscillator.

    SFRPAGE = CONFIG_PAGE;
    PLL0CN  = 0x04;
    SFRPAGE = LEGACY_PAGE;
    FLSCL   = 0x10;
    SFRPAGE = CONFIG_PAGE;
    PLL0CN |= 0x01;
    PLL0DIV = 0x04;
    PLL0FLT = 0x01;
    PLL0MUL = 0x09;
    for(i=0; i < 256; i++);
    PLL0CN |= 0x02;
    while(!(PLL0CN & 0x10));
    CLKSEL  = 0x02;             // SYSCLK derived from the PLL.

    SFRPAGE = SFRPAGE_SAVE;     // Restore SFR page.
}

//-------------------------------------------------------------------------------------------
// UART0_Init
//-------------------------------------------------------------------------------------------
//
// Configure the UART0 using Timer1, for <baudrate> and 8-N-1.
//
void UART0_INIT(void)
{
    char SFRPAGE_SAVE;

    SFRPAGE_SAVE = SFRPAGE;     // Save Current SFR page.

    SFRPAGE = TIMER01_PAGE;
    TMOD   &= ~0xF0;
    TMOD   |=  0x20;            // Timer1, Mode 2: 8-bit counter/timer with auto-reload.
    TH1     = (unsigned char)-(SYSCLK/BAUDRATE/16); // Set Timer1 reload value for baudrate
    CKCON  |= 0x10;             // Timer1 uses SYSCLK as time base.
    TL1     = TH1;
    TR1     = 1;                // Start Timer1.

    SFRPAGE = UART0_PAGE;
    SCON0   = 0x50;             // Set Mode 1: 8-Bit UART
    SSTA0   = 0x10;             // UART0 baud rate divide-by-two disabled (SMOD0 = 1).
    TI0     = 1;                // Indicate TX0 ready.

    SFRPAGE = SFRPAGE_SAVE;     // Restore SFR page.
}

//-------------------------------------------------------------------------------------------
// T2_INIT
//-------------------------------------------------------------------------------------------
//
// Configure Timer 2 into Mode 0, set to overflow every ~0.1 seconds
// Page 323 has good info
//
void T2_INIT(void)
{
    char SFRPAGE_SAVE;
    SFRPAGE_SAVE = SFRPAGE;     // Save Current SFR page.
    SFRPAGE = TMR2_PAGE;        // Page for Timer 2

    TMR2CN  |= 0x00;            // Set Timer 2 to Capture Mode
    TMR2CN  &= ~0x0F;           // Enable auto reload mode
                                // Disable Timer 2 (for now)
                                // Set timer 2 to advance according to T2M1:T2M0
    TMR2CF  &= ~0x1B;           // disable output, disable decrement
    TMR2CF  |=  0x08;           // Set to advance on SYSCLK

    RCAP2L = (unsigned char)(T2_PRELOAD & 0x00FF); // Configure Timer 2 Preload
    RCAP2H = (T2_PRELOAD >> 8);  
    TMR2 = T2_PRELOAD;

    TR2     = 1;                // Enable Timer 2

    SFRPAGE = CONFIG_PAGE;
    ET2     = 1;                // Enable T2 interrupts

    SFRPAGE = SFRPAGE_SAVE;     // Restore SFR page.
}

void delay_us(uint16_t waitTime)
{
	T2_Overflows = 0;
	ET2 = 1;
	if(waitTime > 5)
	{
		waitTime /= 5;
	}
	else
	{
		waitTime = 1;
	}
	while(T2_Overflows < waitTime);
	ET2 = 0;
}

char checkModePin(void)
{
	char SFRPAGE_SAVE;
	char returnVal;
    SFRPAGE_SAVE = SFRPAGE;     // Save Current SFR page.
	SFRPAGE = CONFIG_PAGE;

	returnVal = (MODE_PIN)?1:0;

	SFRPAGE = SFRPAGE_SAVE;
	return returnVal;
}

state_t waiting(char input)
{
	// If this is a status byte
	if(input & 0x80)
	{
		if((input & 0xF0) == 0xF0)
		{
			return state;
		}
		message.instrument = (input & 0x0F);
		if((input & 0xF0) == NOTE_ON_OPCODE)
		{
			message.opcode = NOTE_ON_OPCODE;
			return ONE_BYTE;
		}
		else if((input & 0xF0) == NOTE_OFF_OPCODE)
		{
			message.opcode = NOTE_OFF_OPCODE;
			return ONE_BYTE;
		}
		else
		{
			return WAITING;
		}
	}
	return WAITING;
}

state_t one_byte(char input)
{
	
	if(input & 0x80)
	{
		// If we get a control message instead of data
		return waiting(input);
	}
	else
	{
		// Should get the "note" data
		message.note = input;
		return TWO_BYTES;
	}
	
}

state_t two_bytes(char input)
{
	// If we get a control message instead of data
	if(input & 0x80)
	{
		return waiting(input);
	}
	// If we get any non-control message, set a note
	// Assumes that the only opcodes are NOTE ON and NOTE OFF
	if(message.opcode == NOTE_ON_OPCODE)
	{
		message.vol = input;
		if(message.vol == 0x00)
			noteOff(message.note, message.instrument);
		else
			noteOn(message.note, message.instrument, ~message.vol);
		return ONE_BYTE;
	}
	else
	{
		message.vol = input;
		noteOff(message.note, message.instrument);
		return ONE_BYTE;
	}
}

//------------------------------------------------------------------------------------
// putchar()
//------------------------------------------------------------------------------------
void putchar(char c)
{
    while(!TI0); 
    TI0=0;
    SBUF0 = c;
}

//------------------------------------------------------------------------------------
// getchar()
//------------------------------------------------------------------------------------
char getchar(void)
{
    char c;
    while(!RI0)
    	if(checkModePin) return 0xFF;
    RI0 =0;
    c = SBUF0;
// Echoing the get character back to the terminal is not normally part of getchar()
    //putchar(c);    // echo to terminal
    return SBUF0;
}

//------------------------------------------------------------------------------------
// SW_ISR
//------------------------------------------------------------------------------------
// ISR to increment the instrument
void SW_ISR (void) __interrupt 0
{
	if(state != KEYBOARD_MODE) return;
	kbdInstrument = (kbdInstrument + 1) % 16;
	if(kbdInstrument == 0) ++kbdInstrument;
	state = WAITING;
}