/* keyboard.h
 * 
 * Ken Schmitt and Frank Sinapi
 * MPS at RPI, Fall 2017
 * ------------------------------------------------------------------------------------
 * Header file to integrate the keyboard from the PSS-140 keyboard					
 * 
 * PORT 7 = OUTPUTS to Diodes
 * Port 5 = INPUTS from diodes
 *
 * NOTE: Port 5 assumes wiring is "backwards" and does not utilize P5.7, thus
 *       the initial shift and the shifting left rather than right.
 * 
 * Driver function should create an instance of keyboard_t for keeping track of
 *   the state of the keyboard. Accessing the struct should only be done via
 *   the provided bit[On/Off/State] functions.*/

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <c8051f120.h>
#include <stdint.h>

//------------------------------------------------------------------------------------
// Global Constants
//------------------------------------------------------------------------------------
#define NUM_KEYS 37
#define ROWS	6
#define COLS	7
#define KBD_DELAY 100

//------------------------------------------------------------------------------------
// Typedefs
//------------------------------------------------------------------------------------

typedef struct {
	uint8_t current[(NUM_KEYS / 8) + 1];
	uint8_t last[(NUM_KEYS / 8) + 1];
} keyboard_t;


// The delay_us function is defined in the main driver of the program
extern void delay_us(uint16_t waitTime);


//------------------------------------------------------------------------------------
// Global Functions
//------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------
// bitOff
//------------------------------------------------------------------------------------
// Turn off a specified bit in an array
void bitOff(uint8_t *arr, uint8_t bit)
{
	arr[bit / 8] &= ~(1 << (bit%8));
}

//------------------------------------------------------------------------------------
// bitOn
//------------------------------------------------------------------------------------
// Turn on a specified bit in an array
void bitOn(uint8_t *arr, uint8_t bit)
{
	arr[bit / 8] |= (1 << (bit%8));
}

//------------------------------------------------------------------------------------
// bitState
//------------------------------------------------------------------------------------
// Get status (1 or 0) of a specified bit within an array
char bitState(uint8_t *arr, uint8_t bit)
{
	return (arr[bit / 8] & (1 << (bit%8) ) ) ? 1 : 0;
}

//------------------------------------------------------------------------------------
// initKeyboard
//------------------------------------------------------------------------------------
// Configure ports for interfacing with keyboard, and initialize the keyboard struct
void initKeyboard(keyboard_t *keyboard)
{
    char SFRPAGE_SAVE;
    char i;
    SFRPAGE_SAVE = SFRPAGE;     // Save Current SFR page.
    SFRPAGE = CONFIG_PAGE;

    P5MDOUT = 0x00;				// Port 5 for inputs
    P5 = 0xFF;

    P7MDOUT = 0xFF;				// Port 7 outputs initially off
   	P7 = 0x00;

   	for(i = 0; i < NUM_KEYS; ++i)
   	{
   		bitOff(keyboard->current, i);
   		bitOff(keyboard->last, i);
   	}

    SFRPAGE = SFRPAGE_SAVE;
}

//------------------------------------------------------------------------------------
// updateKeyboard
//------------------------------------------------------------------------------------
// Read the keyboard and update the keyboard status struct
void updateKeyboard(keyboard_t *keyboard)
{
	uint8_t row, col, data, key;

	char SFRPAGE_SAVE = SFRPAGE;
	SFRPAGE = CONFIG_PAGE;

	for(row = 0; row < ROWS; ++row)
	{
		P7 = 1 << row;				// Set one bit of P7  high
		delay_us(KBD_DELAY);		// Give time to settle
		data = (P5 << 1);			// Read in data (shift bc we only use 7 of 8 pins)
		for(col = 0; col < COLS; ++col)
		{
			// Col 0 only exists for row 0
			// if( (col == 0) && (row != 0) ) continue;
			
			// Getting the right key from the col/row is messy
			// Result of the weird layout of the PSS-140 kbd
			key = (col * ROWS) + row - (ROWS - 1);

			// Set the "last" field
			if(bitState(keyboard->current, key) )
				bitOn(keyboard->last, key);
			else
				bitOff(keyboard->last, key);
			if(data & 0x80)	
				bitOn(keyboard->current, key);	// Key is on
			else	
				bitOff(keyboard->current, key);	// Key is off
			
			data = data << 1;
		}
	}

	SFRPAGE = SFRPAGE_SAVE;
}


#endif /*KEYBOARD_H*/