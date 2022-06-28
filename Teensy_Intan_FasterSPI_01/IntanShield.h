#include <SPI.h>
#include <Wire.h>

#if defined(ARDUINO) && ARDUINO >= 100
	#include "Arduino.h"
#else
	#include "WProgram.h"
#endif


uint16_t SendReadCommand(uint8_t regnum);
uint16_t SendConvertCommand(uint8_t channelnum);
uint16_t SendConvertCommandH(uint8_t channelnum);
uint16_t SendWriteCommand(uint8_t regnum, uint8_t data);
void Calibrate();
void SetAmpPwr(bool Ch1, bool Ch2);

const int chipSelectPin = 10;
// const byte PS_128 = (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
// const byte PS_16 = (1 << ADPS2);

#define FIRSTCHANNEL 0
#define SECONDCHANNEL 15
#define DAC_SCALE_COEFFICIENT 12
