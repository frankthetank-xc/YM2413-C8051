# YM2413-C8051
Program to control a YM2413 via midi &amp; keyboard using a C8051F120 MCU

Compiled with SDCC using Silabs targetting C8051F120.


# EXAMPLE USAGE
Audio renders can be found at https://soundcloud.com/mps-student

These songs are rendered through the following process:
- Reaper is used to play back a MIDI file. Channels for each instrument are set as to correlate with the typedef for "instruments" included in the ym2413 driver.
- Loopmidi is used to generate a virtual MIDI device, and hairless-midiserial is used to send this MIDI instrument to the C8051's UART connector running at 115200 baud
- The output of the audio amplifier is connected to the computer's audio input and recorded with Audacity

The volume needed to record into a computer is MUCH lower than that needed to drive a speaker or headphones. The potentiometer must be adjusted properly to avoid distortion.
