#include "IntanShield.h"
#include <Metro.h> // Include the Metro library

Metro serialMetro = Metro(1);  // Instantiate an instance in milliseconds

// NOTE: First channel is 0 and second channel is 15

/* Select which amplifier(s) should be turned on. FirstChannelPwr corresponds to Phone Jack 1, and SecondChannelPwr corresponds to Phone Jack 2. Each channel's power should only be defined once (i.e. true OR false, never true AND false) */
bool FirstChannelPwr = true;
//bool FirstChannelPwr = false;

int loopMax = 65536;    // Maximum 16 bit value
int z = 1;

//bool SecondChannelPwr = true;
bool SecondChannelPwr = true;

String serialout = ""; //string to send through the Serial output

String serialconstants = ",550,-550"; //constants to concatenate with String serialout to control auto-scaling of Serial Plotter

/* Set configuration settings using the DIP switch. 5 settings to configure:
 *  1) audio_enable: enable/disable (Digital Pin 6) - determines if sounds are played through speaker when a pulse has been detected from a channel. When enabled, volume can be changed through rotary potentiomter.
 *  2) low_gain_mode: enable/disable (Digital Pin 7) - determines if channel data is scaled down by a factor of 4. When enabled, signals appear weaker. Can be helpful for viewing particularly strong signals, i.e. EKG, without clipping
 *  3) average_energy_mode: enable/disable (Analog Pin 1, aka Digital Pin 15) - determines if DAC and Serial output display accumulated energy per 20 ms period or raw data per 1 ms period (at amplifier sampling frequency)
 *  4) notch_setting: enable/disable (Analog Pin 2, aka Digital Pin 16) - determines if the software notch filter is enabled or disabled (recommended to reduce noise from power mains)
 *  5) notch_setting: 60 Hz / 50 Hz (Analog Pin 3, aka Digital Pin 17) - determines the frequency of the notch filter (60 Hz or 50 Hz depending on the power mains frequency of the country)
 */

uint8_t data; //variable that holds the 8-bit data to be sent to the DAC

long rawdata; //variable that holds the raw 16-bit data from the RHD2216 chip

int serialdata1; //variable that holds the int data of the first channel to send over Serial

int serialdata2; //variable that holds the int data of the second channel to send over Serial
 

/* Select the lower cutoff of the bandwidth */
enum Bandselect { LowCutoff10Hz, LowCutoff1Hz, LowCutoff100mHz };
Bandselect band_setting; // band_setting has 3 possible values, corresponding to a low cutoff frequency of 10 Hz, 1 Hz, or 0.1 Hz

void setup() {
  SPI.begin(); //Initialize SPI bus
  pinMode(chipSelectPin, OUTPUT); //Set the chipSelectPin to be an output on the Arduino  

  /* Set digital pins 4-5 to be outputs corresponding to whether or not channels FIRSTCHANNEL or SECONDCHANNEL are sensing a strong signal */
  pinMode(4, OUTPUT);
  pinMode(5, OUTPUT);
  
  digitalWrite(chipSelectPin, HIGH); //Initialize the chipSelectPin to be high (active low, so default should be high)
  Serial.begin(250000); //Initialize the serial monitor to monitor at 250,000 Baud
  SPI.beginTransaction(SPISettings(18000000, MSBFIRST, SPI_MODE0)); //Set Arduino's SPI settings to match those of the RHD2000 chip - max clock speed of 24 MHz, data transmitted most-significant-bit first, and mode 0: clock polarity = 0, clock edge = 1
  delay(250); //Give SPI time to stabilize before using it to initialize registers
  
  //Initialize registers - write command is 16 bits: "1  0  R[5] R[4] R[3] R[2] R[1] R[0] D[7] D[6] D[5] D[4] D[3] D[2] D[1] D[0]"
  //Set bits R[5] through R[0] by writing the number of the write-enabled register (between 0 and 17) in binary - registers 40 through 63 are read-only
  //Set bits D[7] through D[0] as the data to be written to that register. Examples are given below for register configuration, consult the datasheet for more details

  //R0: ADC Configuration and Amplifier Fast Settle: leave these settings unless a large transient event is expected (i.e. stimulation pulses near the input electrodes) or power supply to the biopotential amplifiers is not desired (amplifiers will not be used for an extended period of time)
  //D[7] - D[6]: ADC reference BW = 3
  //D[5]: amp fast settle = 0
  //D[4]: amp Vref enable = 0
  //D[3] - D[2]: ADC comparator bias = 3
  //D[1] - D[0]: ADC comparator select = 2
  SendWriteCommand(0, 0b11011110);

  //R1: Supply Sensor and ADC Buffer Bias Current: set VDD sense enable to one if the on-chip supply voltage sensor's output is desired (can be sampled by the ADC on channel 48), leave ADC buffer bias at 32 unless sampling above 120 kS/s (not possible with Arduino's clock speed)
  //D[7]: X - set to 0
  //D[6]: VDD sense enable = 0
  //D[5] - D[0]: ADC buffer bias = 32
  SendWriteCommand(1, 0b00100000);

  //R2: MUX Bias Current: leave MUX bias current at 40 unless sampling above 120 kS/s (not possible with Arduino's clock speed)
  //D[7] - D[6]: X - set to 0
  //D[5] - D[0]: MUX bias current = 40
  SendWriteCommand(2, 0b00101000);

  //R3: MUX Load, Temperature Sensor, and Auxiliary Digital Output: always set MUX load to 0, tempS1, tempS2, and tempen to 0 (unless using the on-chip temperature sensor), and digout HiZ and digout to 0 (unless using auxout pin for off-chip circuity)
  //D[7] - D[5]: MUX load = 0
  //D[4]: tempS2 = 0
  //D[3]: tempS1 = 0
  //D[2]: tempen = 0
  //D[1]: digout HiZ = 0
  //D[0]: digout = 0
  SendWriteCommand(3, 0b00000000);

  //R4: ADC Output Format and DSP Offset Removal: set weak MISO to 1 if the chip is the only slave device on the MISO line, otherwise set weak MISO to 0.
  //Set twoscomp to 0 if unsigned binary notation is desired from the ADC, otherwise set to 1 if values below baseline are desired as negative numbers.
  //Set absmode to 1 to pass ADC conversions through an absolute value function - useful in our application of measuring the energy of a muscle contraction regardless of polarity
  //Set DSPen to 1 if DSAP offset removal from amplifier channels is desired. Set DSP cutoff freq to the appropriate value from the datasheet depending on sampling frequency and desired cutoff frequency
  //D[7]: weak MISO = 1
  //D[6]: twoscomp = 1
  //D[5]: absmode = 0
  //D[4]: DSPen = 1
  //D[3] - D[0]: DSP cutoff frequency variable = 8 (details on datasheet, gives a cutoff frequency for DSP high-pass filter of approximately 10 Hz at a sampling frequency of 17 kHz
  SendWriteCommand(4, 0b11011000);

  //If you want absolute value mode activated, uncomment the following line:
  //SendWriteCommand(4, 0b11111000);

  //R5: Impedance Check Control: only set bits in this register if the chip is being used to check the impedance through electrode(s)
  //D[7]: X - set to 0
  //D[6]: Zcheck DAC power = 0
  //D[5]: Zcheck load = 0
  //D[4] - D[3]: Zcheck scale = 0
  //D[2]: Zcheck conn all = 0
  //D[1]: Zcheck sel pol = 0
  //D[0]: Zcheck en = 0
  SendWriteCommand(5, 0b00000000);

  //R6: Impedance Check DAC: only set bits in this register if the chip is being used to check the impedance through electrode(s)
  //D[7] - D[0]: Zcheck DAC = 0
  SendWriteCommand(6, 0b00000000);

  //R7: Impedance Check Amplifier Select: only set bits in this register if the chip is being used to check the impedance through electrode(s)
  //D[7] - D[6]: X - set to 0
  //D[5] - D[0]: Zcheck select = 0
  SendWriteCommand(7, 0b00000000);



  /*Registers 8 through 11: On-Chip Amplifier Bandwidth Select */
  //Choose the frequency range from lowest frequency to highest frequency you want to amplify
  //Upper frequency can range from 100 Hz to 20 kHz (if off-chip resistors are used, can range from  10 Hz to 20 kHz) - to use off-chip resistors, set bits offchip RH1, offchip RH2, and offchip RL
  //Lower frequency can range from 0.1 Hz to 500 Hz (if off-chip resistors are used, can range from 0.02 Hz to 1 kHz) - to use off-chip resistors, set bits offchip RH1, offchip RH2, and offchip RL
  //Consult the datasheet to determine the values of RH1 DAC1, RH1 DAC2, RH2 DAC2, RL DAC1, RL DAC2, and RL DAC3 for your given frequency range, then set the corresponding bits in the appropriate registers
  //For this example, we use frequency range 10 Hz - 500 Hz. From the datasheet, this gives RH1 DAC1 = 30, RH1 DAC2 = 5, RH2 DAC1 = 43, RH2 DAC2 = 6, RL DAC1 = 5, RL DAC2 = 1, RL DAC3 = 0
  //Set bits ADC aux1 en, ADC aux2 en, and ADC aux3 en if auxiliary ADC inputs are desired in channels 32, 33, and 34 respectively
  
  //R8: On-Chip Amplifier Bandwidth High-Frequency Select
  //D[7]: offchip RH1 = 0
  //D[6]: X - set to 0
  //D[5] - D[0]: RH1 DAC1 = 30
  SendWriteCommand(8, 30);

  //R9: On-Chip Amplifier Bandwidth High-Frequency Select
  //D[7]: ADC aux1 en = 0
  //D[6] - D[5]: X - set to 0
  //D[4] - D[0]: RH1 DAC2 = 5
  SendWriteCommand(9, 5);

  //R10: On-Chip Amplifier Bandwidth High-Frequency Select
  //D[7]: offchip RH2 = 0
  //D[6]: X - set to 0
  //D[5] - D[0]: RH2 DAC1 = 43
  SendWriteCommand(10, 43);

  //R11: On-Chip Amplifier Bandwidth High-Frequency Select
  //D[7]: ADC aux2 en = 0
  //D[6] - D[5]: X - set to 0
  //D[4] - D[0]: RH2 DAC2 = 6
  SendWriteCommand(11, 6);

  /* Registers 12 through 13: On-Chip Amplifier Bandwidth Low-Frequency Select
   * Choose band_setting to select the lower cutoff of the on-chip bandpass filter
   * Uncomment one of the following three band_setting assignments to set the on-chip registers to the corresponding values suitable for that lower cutoff frequency
   */

  uint8_t R12, RL, RLDAC1, R13, ADCaux3en, RLDAC3, RLDAC2; //variables holding the values to be written to the bandwidth-controlling on-chip registers
     
  band_setting = LowCutoff10Hz;
  //band_setting = LowCutoff1Hz;
  //band_setting = LowCutoff100mHz;

  switch(band_setting) {
  
    case LowCutoff10Hz:
    
      //R12: On-Chip Amplifier Bandwidth Select
      //D[7]: offchip RL = 0
      //D[6] - D[0]: RL DAC1 = 5
      RL = 0;
      RLDAC1 = 5;
    
      //R13: On-Chip Amplifier Bandwidth Select
      //D[7]: ADC aux3 en = 0
      //D[6]: RL DAC3 = 0
      //D[5] - D[0]: RL DAC2 = 1
      ADCaux3en = 0;
      RLDAC3 = 0;
      RLDAC2 = 1;
    
    break;
    
    case LowCutoff1Hz:
    
      //R12: On-Chip Amplifier Bandwidth Select
      //D[7]: offchip RL = 0
      //D[6] - D[0]: RL DAC1 = 44
      RL = 0;
      RLDAC1 = 44;
    
      //R13: On-Chip Amplifier Bandwidth Select
      //D[7]: ADC aux3 en = 0
      //D[6]: RL DAC3 = 0
      //D[5] - D[0]: RL DAC2 = 6
      ADCaux3en = 0;
      RLDAC3 = 0;
      RLDAC2 = 6;
    
    break;
    
    case LowCutoff100mHz:
  
      //R12: On-Chip Amplifier Bandwidth Select
      //D[7]: offchip RL = 0
      //D[6] - D[0]: RL DAC1 = 16
      RL = 0;
      RLDAC1 = 16;
    
      //R13: On-Chip Amplifier Bandwidth Select
      //D[7]: ADC aux3 en = 0
      //D[6]: RL DAC3 = 1
      //D[5] - D[0]: RL DAC2 = 60
      ADCaux3en = 0;
      RLDAC3 = 1;
      RLDAC2 = 60;
  
    break;
  }

  /* R12 and R13 are the 8-bit values to be sent to the on-chip registers 12 and 13, which are set in the previous switch statement */
  R12 = ((RL << 7) | RLDAC1);
  R13 = (ADCaux3en << 7) | (RLDAC3 << 6) | RLDAC2;

  /* Send the write commands to registers 12 and 13 */
  SendWriteCommand(12, R12);
  SendWriteCommand(13, R13);
  
  /* Registers 14 through 17: Individual Amplifier Power */
  //For each channel you wish to observe, set its corresponding bit apwr[CHANNEL]. For all other channels, set their bits to 0 to conserve power.
  //For clarification, let's turn all amplifiers off to start, and then power on any amplifiers we wish to use through the function SetAmpPwr()
  
  //R14: Individual Amplifier Power: D[7] - D[0] = apwr[7] - apwr[0]
  //D[7] - D[4] = 0, D[3] = 1, D[2] - D[0] = 0
  SendWriteCommand(14, 0b00000000);

  //R15: Individual Amplifier Power: D[7] - D[0] = apwr[15] - apwr[8]
  //D[7] - D[5] = 0, D[4] = 1, D[3] - D[0] = 0
  SendWriteCommand(15, 0b00000000);

  //The following 2 commands have no effect on the RHD2216 chip but we include them in case the code is ever use with an RHD2132 chip
  
  //R16: Individual Amplifier Power: D[7] - D[0] = apwr[23] - apwr[16]
  //D[7] - D[0] = 0
  SendWriteCommand(16, 0);
  
  //R17: Individual Amplifier Power: D[7] - D[0] = apwr[31] - apwr[24]
  //D[7] - D[0] = 0
  SendWriteCommand(17, 0);

  //Turning individual amplifiers that we wish to use on
  SetAmpPwr(FirstChannelPwr, SecondChannelPwr);
  
  //Initiate ADC self-calibration routine that should be performed after chip power-up and register configuration
  Calibrate();

  //Send convert command with LSB set to 1, which resets the output of the digital high-pass filter associated with the channel to zero
  SendConvertCommandH(FIRSTCHANNEL);
  SendConvertCommandH(SECONDCHANNEL);

  //Send convert commands to channels 0 and 15, so that when we enter the loop the results are immediately available
//  SendConvertCommand(FIRSTCHANNEL);
//  SendConvertCommand(SECONDCHANNEL);

  SendConvertCommand(FIRSTCHANNEL);
  SendConvertCommand(FIRSTCHANNEL);

  //Optional - monitor how much of the CPU is being used per interrupt cycle by monitoring the duty cycle from pin 2
  pinMode(2, OUTPUT);
  pinMode(3, OUTPUT);

  //Monitoring CPU usage - set pin 2 low, it will be set high at the beginning of each interrupt cycle
  digitalWriteFast(2, LOW);
  digitalWriteFast(3, LOW);

  //Setting ADC to refer voltages to an external value (3.3 V)
  // analogReference(EXTERNAL);

}

void loop() {
  
  if (serialMetro.check() == 1) { // check if the metro has passed it's interval .
    digitalWriteFast(2, HIGH);
    
    // Output all the analog readings seperated by a space character
    serialdata1 = SendConvertCommand(FIRSTCHANNEL);
  
    // if sign bit is set e.g., 8bit: 128-255
    if (((serialdata1 >> 15) && 1) == 1){ 
      serialdata1 = serialdata1 - loopMax; // compute negative value
    }
  
    serialdata1 = serialdata1 * 0.195;
    serialout = serialout + String(serialdata1) + serialconstants;
    
    Serial.println(serialdata1);
  
    //Clear the serial data string to prepare for the next iteration
    serialout = "";
  
    digitalWriteFast(2, LOW);  
  }
}
