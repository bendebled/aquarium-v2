#define debugSerial SerialUSB
#define NUMBER_OF_LED 8

void setup() {
  debugSerial.begin(9600);
  while (!debugSerial);
  
  debugSerial.println("---");
  
  initPWM();
  debugSerial.println("PWM Initialized");
}


void loop() {
  
}

/*
 * This function inits the pins 2,3,4,5,10,11,12 and 13 for PWM.
 * 
 * By default, the PWM frequency is 185kHz on a Arduino Zero. This is too fast for the LED drivers. 
 * This function will init the PWM to work at a frequency of 100Hz.
 * 
 * For more informations on how this function work, please read:
 *  - Basic informations on SAMD21 timers necessary for PWM: https://forum.arduino.cc/index.php?topic=330735.0
 *  - This forum: https://forum.arduino.cc/index.php?topic=346731.0
 */
void initPWM() {
  REG_GCLK_GENDIV = GCLK_GENDIV_DIV(3) |          // Divide the 48MHz clock source by divisor 3: 48MHz/3=16MHz
                    GCLK_GENDIV_ID(4);            // Select Generic Clock (GCLK) 4
  while (GCLK->STATUS.bit.SYNCBUSY);              // Wait for synchronization


  REG_GCLK_GENCTRL = GCLK_GENCTRL_IDC |           // Set the duty cycle to 50/50 HIGH/LOW
                     GCLK_GENCTRL_GENEN |         // Enable GCLK4
                     GCLK_GENCTRL_SRC_DFLL48M |   // Set the 48MHz clock source
                     GCLK_GENCTRL_ID(4);          // Select GCLK4
  while (GCLK->STATUS.bit.SYNCBUSY);              // Wait for synchronization


  // Enable the port multiplexer for digital pins
  PORT->Group[g_APinDescription[2].ulPort].PINCFG[g_APinDescription[2].ulPin].bit.PMUXEN = 1;
  PORT->Group[g_APinDescription[3].ulPort].PINCFG[g_APinDescription[3].ulPin].bit.PMUXEN = 1;
  PORT->Group[g_APinDescription[4].ulPort].PINCFG[g_APinDescription[4].ulPin].bit.PMUXEN = 1;
  PORT->Group[g_APinDescription[5].ulPort].PINCFG[g_APinDescription[5].ulPin].bit.PMUXEN = 1;
  PORT->Group[g_APinDescription[10].ulPort].PINCFG[g_APinDescription[10].ulPin].bit.PMUXEN = 1;
  PORT->Group[g_APinDescription[11].ulPort].PINCFG[g_APinDescription[11].ulPin].bit.PMUXEN = 1;
  PORT->Group[g_APinDescription[12].ulPort].PINCFG[g_APinDescription[12].ulPin].bit.PMUXEN = 1;  
  PORT->Group[g_APinDescription[13].ulPort].PINCFG[g_APinDescription[13].ulPin].bit.PMUXEN = 1;
 
  
  // Connect the timers to the port output - port pins are paired odd PMUO and even PMUXE
  PORT->Group[g_APinDescription[11].ulPort].PMUX[g_APinDescription[11].ulPin >> 1].reg = PORT_PMUX_PMUXO_E | PORT_PMUX_PMUXE_E;
  PORT->Group[g_APinDescription[10].ulPort].PMUX[g_APinDescription[10].ulPin >> 1].reg = PORT_PMUX_PMUXO_F | PORT_PMUX_PMUXE_F;
  PORT->Group[g_APinDescription[2].ulPort].PMUX[g_APinDescription[2].ulPin >> 1].reg = PORT_PMUX_PMUXO_F | PORT_PMUX_PMUXE_F;
  PORT->Group[g_APinDescription[4].ulPort].PMUX[g_APinDescription[4].ulPin >> 1].reg = PORT_PMUX_PMUXO_F | PORT_PMUX_PMUXE_F;


  // Feed GCLK4 to TCC0 and TCC1
  REG_GCLK_CLKCTRL = GCLK_CLKCTRL_CLKEN |         // Enable GCLK4 to TCC0 and TCC1
                     GCLK_CLKCTRL_GEN_GCLK4 |     // Select GCLK4
                     GCLK_CLKCTRL_ID_TCC0_TCC1;   // Feed GCLK4 to TCC0 and TCC1
  while (GCLK->STATUS.bit.SYNCBUSY);              // Wait for synchronization
  // Feed GCLK4 to TCC0 and TCC1
  REG_GCLK_CLKCTRL = GCLK_CLKCTRL_CLKEN |         // Enable GCLK4 to TCC0 and TCC1
                     GCLK_CLKCTRL_GEN_GCLK4 |     // Select GCLK4
                     GCLK_CLKCTRL_ID_TCC2_TC3;    // Feed GCLK4 to TCC0 and TCC1
  while (GCLK->STATUS.bit.SYNCBUSY);              // Wait for synchronization


  // Dual slope PWM operation: timers countinuously count up to PER register value then down 0
  REG_TCC0_WAVE |= TCC_WAVE_POL(0xF) |            // Reverse the output polarity on all TCC0 outputs
                    TCC_WAVE_WAVEGEN_DSBOTTOM;    // Setup dual slope PWM on TCC0
  while (TCC0->SYNCBUSY.bit.WAVE);                // Wait for synchronization
  REG_TCC1_WAVE |= TCC_WAVE_POL(0xF) |            // Reverse the output polarity on all TCC0 outputs
                    TCC_WAVE_WAVEGEN_DSBOTTOM;    // Setup dual slope PWM on TCC0
  while (TCC1->SYNCBUSY.bit.WAVE);                // Wait for synchronization
  REG_TCC2_WAVE |= TCC_WAVE_POL(0xF) |            // Reverse the output polarity on all TCC0 outputs
                    TCC_WAVE_WAVEGEN_DSBOTTOM;    // Setup dual slope PWM on TCC0
  while (TCC2->SYNCBUSY.bit.WAVE);                // Wait for synchronization
 
  
  // Each timer counts up to a maximum or TOP value set by the PER register,
  // this determines the frequency of the PWM operation:
  // 20000 = 50Hz, 10000 = 100Hz, 2500  = 400Hz
  REG_TCC0_PER = 10000;
  while(TCC0->SYNCBUSY.bit.PER);
  REG_TCC1_PER = 10000;
  while(TCC1->SYNCBUSY.bit.PER);
  REG_TCC2_PER = 10000;
  while(TCC2->SYNCBUSY.bit.PER);
  

  // Divide the 16MHz signal by 8 giving 2MHz (0.5us) TCC0 timer tick and enable the outputs
  REG_TCC0_CTRLA |= TCC_CTRLA_PRESCALER_DIV8 |    // Divide GCLK4 by 8
                    TCC_CTRLA_ENABLE;             // Enable the TCC0 output
  while (TCC0->SYNCBUSY.bit.ENABLE);              // Wait for synchronization
  REG_TCC1_CTRLA |= TCC_CTRLA_PRESCALER_DIV8 |    // Divide GCLK4 by 8
                    TCC_CTRLA_ENABLE;             // Enable the TCC0 output
  while (TCC1->SYNCBUSY.bit.ENABLE);              // Wait for synchronization
  REG_TCC2_CTRLA |= TCC_CTRLA_PRESCALER_DIV8 |    // Divide GCLK4 by 8
                    TCC_CTRLA_ENABLE;             // Enable the TCC0 output
  while (TCC2->SYNCBUSY.bit.ENABLE);              // Wait for synchronization
  

  //Set PWM
  for(int i = 0; i < NUMBER_OF_LED; i++){
    setPWM(i,0);
  }
}

/*
 * Set a PWM value to an LED pin.
 * The value should be between 0 and 10000.
 */
void setPWM(byte led, int value){
  switch (led) {
    case 0:
      REG_TCC2_CCB0 = value;
      while(TCC2->SYNCBUSY.bit.CCB0);
      break;
    case 1:
      REG_TCC2_CCB1 = value;
      while(TCC2->SYNCBUSY.bit.CCB1);
      break;
    case 2:
      REG_TCC0_CCB3 = value;
      while(TCC0->SYNCBUSY.bit.CCB3);
      break;
    case 3:
      REG_TCC0_CCB2 = value;
      while(TCC0->SYNCBUSY.bit.CCB2);
      break;
    case 4:
      REG_TCC0_CCB0 = value;
      while(TCC0->SYNCBUSY.bit.CCB0);
      break;
    case 5:
      REG_TCC0_CCB1 = value;
      while(TCC0->SYNCBUSY.bit.CCB1);
      break;
    case 6:
      REG_TCC1_CCB0 = value;
      while(TCC1->SYNCBUSY.bit.CCB0);
      break;
    case 7:
      REG_TCC1_CCB1 = value;
      while(TCC1->SYNCBUSY.bit.CCB1);
      break;
    default: 
      debugSerial.println("Error: \"led\" parameter passed to the function \"setPWM\" is not correct");
      break;
  }
}

