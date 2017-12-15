/* YM213.h
 * 
 * Ken Schmitt and Frank Sinapi
 * MPS at RPI, Fall 2017
 * ------------------------------------------------------------------------------------
 * Header file to control use of the YM2413 chip 	 								*/
#ifndef YM2413_H
#define YM2413_H

#include <c8051f120.h>
#include <stdint.h>

//------------------------------------------------------------------------------------
// Global Constants
//------------------------------------------------------------------------------------
#define MAX_VOICES 	9

#define NOTE_OFF 	0
#define NOTE_ON		1

//------------------------------------------------------------------------------------
// Typedefs
//------------------------------------------------------------------------------------

// Not sure how much this will get used tbh...
typedef enum {
	custom, violin, guitar, piano,
	flute, clarinet, oboe, trumpet,
	organ, horn, synthesizer, harpsichord,
	vibraphone, synthesizer_bass, wood_bass, electric_guitar
} inst_t;

typedef struct {
	uint8_t note;
	uint8_t instrument : 4;
	uint8_t state : 4;
} voice_t;

typedef struct {
	voice_t voices[MAX_VOICES];
} synth_t;

//------------------------------------------------------------------------------------
// Global Variables
//------------------------------------------------------------------------------------

// sbits correspond to control lines for the YM2413
__sbit __at (0xA0) ADDR;	// Switches between address/data write mode
__sbit __at (0xA1) WE;		// Write enable (active low)
__sbit __at (0xA2) CS;		// Chip Select (active low)
__sbit __at (0xA3) IC;		// Chip reset (bringing low will reset the chip)

// Lookup table starts with C at 0, B at 11...
__xdata static const uint16_t fnum_lookup[12] = {	// Relies on using the stock oscillator
	172, 181, 192, 204, 216, 229,					// FNum could be calculated on the fly but it would be expensive
	242, 257, 272, 288, 305, 323
};

// synth keeps track of all the voices available
static synth_t synth;

// voiceItr keeps track of round-robin partitioning of voices
static uint8_t voiceItr = 0;

//------------------------------------------------------------------------------------
// Function Prototypes
//------------------------------------------------------------------------------------
void synthInit(void);
void resetSynth(void);
void testSynth(void);
int8_t noteOn(uint8_t note, uint8_t instr, uint8_t vol);
int8_t noteOff(uint8_t note, uint8_t instr);
void killAll(void);

//------------------------------------------------------------------------------------
// Static Function Prototypes
//------------------------------------------------------------------------------------
static void writeRegister(uint8_t addr, uint8_t data);
static void setNote(uint8_t voice, uint8_t note, uint8_t state);
static void setInstrument(uint8_t voice, uint8_t instrument, uint8_t vol);
static inline uint16_t get_fnum(uint8_t note);
static inline uint8_t get_octave(uint8_t note);

// The delay_us function is defined in the main driver of the program
extern void delay_us(uint16_t waitTime);

//------------------------------------------------------------------------------------
// synthInit
//------------------------------------------------------------------------------------
// Initialize ports, reset synth
void synthInit(void)
{
	char SFRPAGE_SAVE = SFRPAGE;
	SFRPAGE = CONFIG_PAGE;
	P3MDOUT  = 0XFF;		// Data line
	P2MDOUT |= 0X0F;		// Control lines

	resetSynth();
	SFRPAGE = SFRPAGE_SAVE;
}

//------------------------------------------------------------------------------------
// PUBLIC FUNCTION IMPLEMENTATIONS
//------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------
// resetSynth
//------------------------------------------------------------------------------------
// Reset physical chip, send all data from the struct
void resetSynth(void)
{
	char i;

	// Reset the chip using the IC line
	IC = 0;

	// Set chip in high impedance mode
	CS = 1;
	WE = 1;
	ADDR = 1;
	delay_us(50000);
	IC = 1;
	
	// Turn off the rhythm stuff
	writeRegister(0x0E, 0x00);
	
	// Turn off all voices and set them to GITAR
	for(i = 0; i < MAX_VOICES; ++i)
	{
		setNote(i, 0, NOTE_OFF);
		setInstrument(i, guitar, 0xF);
	}
	
	voiceItr = 0;
}

//------------------------------------------------------------------------------------
// testSynth
//------------------------------------------------------------------------------------
// Function to test turning on/off a note. Can be used to verify wiring is correct.
void testSynth()
{
	printf("press a key to turn on a voice\r\n");
	getchar();

	setInstrument(0,synthesizer_bass, 0xF);

	setNote(0,50,NOTE_ON);
	printf("Press a key to turn it off\r\n");
	getchar();
	setNote(0,50,NOTE_OFF);
}

//------------------------------------------------------------------------------------
// noteON
//------------------------------------------------------------------------------------
// Turn on a new note
// Uses round robin w/ a status iterator to reduce voice stealing for long releases
int8_t noteOn(uint8_t note, uint8_t instr, uint8_t vol)
{
	char i = 0;
	char voice;
	while(i < MAX_VOICES)
	{
		voice = (i + voiceItr) % MAX_VOICES;
		// If this note is off, we're gonna write it
		if(synth.voices[voice].state == NOTE_OFF)
		{
			setInstrument(voice, instr, vol >> 3);
			setNote(voice, note, NOTE_ON);
			voiceItr = (voiceItr + 1) % MAX_VOICES;
			return 0;
		}
		++i;
	}
	// Move the round robin tracker
	voiceItr = (voiceItr + 1) % MAX_VOICES;
	// If we couldn't allocate a new voice, just quit :(
	return -1;
}

//------------------------------------------------------------------------------------
// noteOff
//------------------------------------------------------------------------------------
// Turn off a note
int8_t noteOff(uint8_t note, uint8_t instr)
{
	char voice = 0;
	while(voice < MAX_VOICES)
	{
		// If this note is ON, has same NOTE, has same INSTR, turn it off
		if(	synth.voices[voice].state 		== NOTE_ON &&
			synth.voices[voice].note 		== note &&
			synth.voices[voice].instrument 	== instr)
		{
			setNote(voice, note, NOTE_OFF);
			return 0;
		}
		++voice;
	}
	// This voice was not currently on
	return -1;
}

//------------------------------------------------------------------------------------
// killAll
//------------------------------------------------------------------------------------
// Turn off all notes
void killAll(void)
{
	char voice;
	for(voice = 0; voice < MAX_VOICES; ++voice)
	{
		setNote(voice, synth.voices[voice].note, NOTE_OFF);
		synth.voices[voice].state = NOTE_OFF;
	}
}

//------------------------------------------------------------------------------------
// STATIC FUNCTION IMPLEMENTATIONS
//------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------
// writeRegister
//------------------------------------------------------------------------------------
// Write 8 bits of "data" to Ym2413 register "addr"
static void writeRegister(uint8_t addr, uint8_t data)
{
	WE = 0;
	ADDR = 0;
	delay_us(2);
	// Write the address
	P3 = addr;
	// Let data lines settle
	delay_us(2);
	CS = 0;
	// YM needs 12 clock cycles to write address
	delay_us(2);

	CS = 1;
	WE = 1;
	ADDR = 1;
	delay_us(2);
	// Write the data
	P3 = data;
	// Let data lines settle
	WE = 0;
	delay_us(2);

	CS = 0;
	// YM needs 84 clock cycles to write data
	delay_us(2);

	// Leave the chip in high impedance mode
	WE = 1;
	CS = 1;
}

//------------------------------------------------------------------------------------
// setNote
//------------------------------------------------------------------------------------
// Set a voice "voice" to note "note" in state "state"
// Can turn notes on or off based on state
static void setNote(uint8_t voice, uint8_t note, uint8_t state)
{
	// Get the frequency number and the octave
	uint16_t fnum = get_fnum(note);
	uint8_t oct = get_octave(note);
	uint8_t data;
	// Update the global synth struct
	synth.voices[voice].note = note;
	synth.voices[voice].state = state & 0xF;
	// Set address 0x10 + [voice] to be:
	// 	 F Num LSB [0~7]
	data = (uint8_t)(fnum & 0xFF);
	writeRegister(0x10 + voice, data);
	delay_us(20);
	// Set address 0x20 + [voice] to be:
	//   F Num MSb [0]
	//   Octave Setting [1~3]
	//   Key ON/OFF [4]
	//   Sustain [5]
	data = (state != NOTE_OFF) ? 0x10 : 0x00;
	data |= (fnum >> 8) & 0x01;
	data |= (oct & 0x07) << 1;
	writeRegister(0x20 + voice, data);
	delay_us(20);
}

//------------------------------------------------------------------------------------
// setInstrument
//------------------------------------------------------------------------------------
// Set the instrument for a voice to "instrument"
static void setInstrument(uint8_t voice, uint8_t instrument, uint8_t vol)
{
	// Instrument occupes upper nibble, vol occupies lower nibble
	uint8_t data = (instrument << 4) & 0xF0;
	data |= (vol & 0xF);
	synth.voices[voice].instrument = instrument & 0xF;
	writeRegister(0x30 + voice, data);
	delay_us(20);
}

//------------------------------------------------------------------------------------
// get_fnum
//------------------------------------------------------------------------------------
// Convert a note to its frequency number
// This assumes 12 notes used in every octave. Gives notes from C-0 up to C-7
static inline uint16_t get_fnum(uint8_t note)
{
	return fnum_lookup[note % 12];
}

//------------------------------------------------------------------------------------
// get_octave
//------------------------------------------------------------------------------------
// Get the octave of a note
// Assumes that note 0 is in octave 0
static inline uint8_t get_octave(uint8_t note)
{
	// Max octave is 7
	if(note > 95)
		return 0x07;
	else
		return (note / 12) & 0x07;
}

#endif /* YM2413_H */
