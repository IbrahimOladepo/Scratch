#include "IntanShield.h"

/* Global variables */

volatile int i = 0; //Keep track of which channel's turn it is to be sampled. The ISR runs every 0.5 ms, and we sample 1 channel per ISR
                    //Since we sample 2 channels total, each channel is sampled every 1 ms, giving us our sample rate of 1 kHz

volatile int16_t channel_data[2] = {0, 0}; //Store the 16-bit channel data from two channels of the RHD
volatile int16_t final_channel_data[2] = {0, 0}; //Store the final 8-bit version of the channel data
volatile bool firstchannelpower; //Adapt FirstChannelPwr from the .ino file (which is easier for the user to change), into a global variable that can be used in the ISR
volatile bool secondchannelpower; //Adapt SecondChannelPwr from the .ino file (which is easier for the user to change), into a global variable that can be used in the ISR


/* ISR is the Interrupt Service Routine, a function triggered by a hardware interrupt every 1/2000th of a second (2 kHz).
 *  This 500 microsecond value is controlled by the register OCR1A, which is set in the setup() function and can be altered to give different frequencies
 *  
 *  The ISR has two main purposes:
 *    a) Update the PWM output that's being sent to the speaker to reflect the sound sample for that 1/2000th of a second
 *    b) Update the channel_data array with a new sample for that 1/2000th of a second. Since we have 2 channels and can only update 1 per iteration, each individual channel is only updated once every 1/1000th of a second, giving an effective sampling rate of 1 kHz
 *    
 *                          Warning: It is not recommended to set the ISR to run faster than 2 kHz
 *  It takes less than 100 microseconds to complete one iteration with no serial data being sent.
 *  However, it takes ~240 microseconds to complete one iteration with an 8-bit number being sent to the computer through the serial port.
 *  Much of this program's code assumes the default sampling rate of 1 kHz - while it is possible to accommodate a different sampling rate,
 *  everything from audio data length to DAC I2C speed to inclusion of Serial communications should be adjusted.
 *  
 *  Above all, if the ISR takes longer than the interrupt period (by default 500 microseconds), then the timer is attempting to trigger an interrupt before the previous ISR is finished.
 *  This causes unreliable execution of the ISR as well as unreliable timer period, compromising the integrity of the read channel data.
 *  If you change the interrupt period, you must ensure the ISR has time to fully execute before the next ISR begins.
 *  It is recommended to monitor the duty cycle and frequency on Digital Pin 2 for this purpose (Header pin B3 on the shield, illustrated in Figure 3 of the User Manual) - this pin is HIGH while an ISR is executing and LOW otherwise
 * 
 *  If a faster channel sampling rate is needed, you can:
 *    a) Remove a channel from the ISR. For example, you can collect data from channel 0 and ignore channel 16.
 *        Change the maximum value of the variable i to 1 instead of 2, and change the SendConvertCommand to only read from channel 0.
 *        The ISR will still execute every 500 microseconds, but only has 1 channel to cycle through, so the channel will be updated every 500 microseconds instead of 1 milliseconds, giving an effective sampling rate of 2 kHz.
 *        You should also change the loop() function to send to one DAC twice per loop, rather than each of the two DACs once per loop since the number of DACs should reflect the number of channels and twice as much data is coming in.
 *        
 *    b) Delete or comment out all Serial.println statements. These are helpful for monitoring the data from a single channel on the computer, but are slow and can be removed if not needed.
 *        Even at the max Baud rate of 250 kHz, these statements take up the majority of the ISR's time when they are active.
 *        Removing these would allow you to manually shorten the timer before the ISR repeats.
 *        This period is controlled by the register OCR1A, which is set in the setup() function.
 *        
 *    b) Remove the processing-time intensive DSP notch filter by disabling the notchfilter through the DIP switch.
 *        50 or 60 Hz noise from power lines will no longer be filtered.
 *        This reduces the amount of time the ISR needs to complete, allowing you to manually shorten the timer before the ISR repeats.
 *        This period is controlled by the register OCR1A, which is set in the setup() function.
 *        
 *   NOTE:
 *        Make sure not to add any more processor-intensive code to the ISR or the ISR may take more time to execute than it is allotted (500 ms by default).   
 */
 
// ISR(TIMER1_COMPA_vect) {

//     //Every time Timer1 matches the value set in OCR1A (every 500 microseconds, or at a rate of 2 kHz)

    
// 	digitalWrite(2, HIGH); //Optional but helpful - monitor how much time each interrupt cycle takes compared to the amount it is allotted by monitoring the duty cycle from pin 2

// 	//Read the channel data from whichever sample in the pipeline corresponds to this 1/2000th of a second
//     if (i == 0) {
// 		channel_data[0] = SendConvertCommand(FIRSTCHANNEL);
// 		if (!firstchannelpower)
// 			channel_data[0] = 0;
// 	}

// 	if (i == 1) {
// 		channel_data[1] = SendConvertCommand(SECONDCHANNEL);
// 		if (!secondchannelpower)
// 			channel_data[1] = 0;
// 	}
	
// 	final_channel_data[i] = channel_data[i]; //Save channel data into a variable that is not directly manipulated by the NotchFilter functions, so that it can be output at any time

// 	if (i == 1)
// 		//If we just read the data from the SECONDCHANNEL, read FIRSTCHANNEL on the next iteration
// 		i = 0;
// 	else
// 		//If we just read the data from the FIRSTCHANNEL, read SECONDCHANNEL on the next iteration
// 		i++;
   
// 	//Optional but helpful - monitor how much time each interrupt cycle takes compared to the amount it is allotted by monitoring the duty cycle from pin 2
// 	digitalWrite(2, LOW);
// }


uint16_t SendReadCommand(uint8_t regnum) {
	//Sends a READ command to the RHD chip through the SPI interface
	//The regnum is the register number that is desired to be read. Registers 0-17 are writeable RAM registers, Registers 40-44 & 60-63 are read-only ROM registers
	uint16_t mask = regnum << 8;
	mask = 0b1100000000000000 | mask;
	digitalWriteFast(chipSelectPin, LOW);
	uint16_t out = SPI.transfer16(mask);
	digitalWriteFast(chipSelectPin, HIGH);
	return out;
}

uint16_t SendConvertCommand(uint8_t channelnum) {
	//Sends a CONVERT command to the RHD chip through the SPI interface
	//The channelnum is the channel number that is desired to be converted. When a channel is specified, the amplifier and on-chip ADC send the digital data back through the SPI interface
	uint16_t mask = channelnum << 8;
	mask = 0b0000000000000000 | mask;
	digitalWriteFast(chipSelectPin, LOW);
	uint16_t out = SPI.transfer16(mask);
	digitalWriteFast(chipSelectPin, HIGH);
	return out;
}

uint16_t SendConvertCommandH(uint8_t channelnum) {
	//Sends a CONVERT command to the RHD chip through the SPI interface, with the LSB set to 1
	//If DSP offset removel is enabled, then the output of the digital high-pass filter associated with the given channel number is reset to zero
	//Useful for initializing amplifiers before begin to record, or to rapidly recover from a large transient and settle to baseline
	uint16_t mask = channelnum << 8;
	mask = 0b0000000000000001 | mask;
	digitalWriteFast(chipSelectPin, LOW);
	uint16_t out = SPI.transfer16(mask);
	digitalWriteFast(chipSelectPin, HIGH);
	return out;
}

uint16_t SendWriteCommand(uint8_t regnum, uint8_t data) {
	//Sends a WRITE command to the RHD chip through the SPI interface
	//The regnum is the register number that is desired to be written to
	//The data are the 8 bits of information that are written into the chosen register
	//NOTE: For different registers, 'data' can signify many different things, from bandwidth settings to absolute value mode. Consult the datasheet for what the data in each register represents
	uint16_t mask = regnum << 8;
	mask = 0b1000000000000000 | mask | data;
	digitalWriteFast(chipSelectPin, LOW);
	uint16_t out = SPI.transfer16(mask);
	digitalWriteFast(chipSelectPin, HIGH);
	return out;
}

void Calibrate() {
	//Sends a CALIBRATE command to the RHD chip through the SPI interface
	//Initiates an ADC self-calibration routine that should be performed after chip power-up and register configuration
	//Takes several clock cycles to execute, and requires 9 clock cycles of dummy commands that will be ignored until the calibration is complete
	digitalWriteFast(chipSelectPin, LOW);
	SPI.transfer16(0b0101010100000000);
	digitalWriteFast(chipSelectPin, HIGH);
	int i = 0;
	for (i = 0; i < 9; i++) {
		//Use the READ command as a dummy command that acts only to set the 9 clock cycles the calibration needs to complete
		SendReadCommand(40);
	}
}


void SetAmpPwr(bool Ch1, bool Ch2) {
	//Function that takes amplifier channel power information from the .ino file (which is easier for the user to change), and sets global variables equal to those values so they can be used in the ISR
	//FirstChannelPwr and SecondChannelPwr are used to determine which channels of the RHD2216 should be powered on during initial setup
	//Their values are also read into global variables "firstchannelpower" and "secondchannelpower" so that if a channel is not powered on, all data read from that channel is set to 0.
	//This is important because turning off a channel's power doesn't guarantee its output to be 0, and if the user chooses not to turn on an amplifier its readings shouldn't affect the sketch in any way
	
	//This function also sends the READ command over the SPI interface to the chip, performs bit operations to the result, and sends the WRITE command to power on individual amplifiers on the chip
	
	//2 8-bit variables to hold the values read from Registers 14 and 15
	uint8_t previousreg14;
	uint8_t previousreg15;
	
	//Use READ commands to assign values from the chip to the 8-bit variables
	SendReadCommand(14);
	SendReadCommand(14);
	previousreg14 = SendReadCommand(14);
	SendReadCommand(15);
	SendReadCommand(15);
	previousreg15 = SendReadCommand(15);
	
	//If FirstChannelPwr has been set true in the main .ino file, then set global variable firstchannelpower as true and send a WRITE command to the corresponding register setting the corresponding bit
	if (Ch1) {
		firstchannelpower = true;
		if (FIRSTCHANNEL < 8)
			//If FIRSTCHANNEL is an amplifier channel between 0 and 7, send the WRITE command to Register 14 (See RHD2216 datasheet for details)
			SendWriteCommand(14, (1<<FIRSTCHANNEL | previousreg14));
		else if (FIRSTCHANNEL >= 8){
			//If FIRSTCHANNEL is an amplifier channel between 8 and 15, send the WRITE command to Register 15 (See RHD2216 datasheet for details)
			SendWriteCommand(15, (1<<abs(FIRSTCHANNEL-8) | previousreg15)); //abs() is not necessary, as the conditional "else if()" ensures FIRSTCHANNEL-8 is positive. However, the compiler gives a warning unless the FIRSTCHANNEL-8 is positive. Hence abs()
		}
	}
	else {
		//If FirstChannelPwr has been set false in the main .ino file, then set global variable firstchannelpower as false
		firstchannelpower = false;
	}
	
	//If SecondChannelPwr has been set true in the main .ino file, then set global variable secondchannelpower as true and send a WRITE command to the corresponding register setting the corresponding bit
	if (Ch2) {
		secondchannelpower = true;
		if (SECONDCHANNEL < 8)
			//If SECONDCHANNEL is an amplifier channel between 0 and 7, send the WRITE command to Register 14 (See RHD2216 datasheet for details)
			SendWriteCommand(14, (1<<SECONDCHANNEL | previousreg14));
		else if (SECONDCHANNEL >= 8)
			//If SECONDCHANNEL is an amplifier channel between 8 and 15, send the WRITE command to Register 15 (See RHD2216 datasheet for details)
			SendWriteCommand(15, (1<<abs(SECONDCHANNEL-8) | previousreg15)); //abs() is not necessary, as the conditional "else if()" ensures SECONDCHANNEL-8 is positive. However, the compiler gives a warning unless the SECONDCHANNEL-8 is positive. Hence abs()
	}
	else {
		//If SecondChannelPwr has been set false in the main .ino file, then set global variable secondchannelpower as false
		secondchannelpower = false;
	}
}
