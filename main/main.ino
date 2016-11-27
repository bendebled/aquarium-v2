#include <Time.h>
#include <TimeLib.h>
#include "WiFiEsp.h"
#include "WiFiEspUdp.h"
#include <Timezone.h>    //https://github.com/JChristensen/Timezone
#include <Wire.h>

//Debug
#define debugSerial SerialUSB

//Wifi
#define espSerial Serial1
char ssid[] = "SSID";
char pass[] = "PASSWD";
int status = WL_IDLE_STATUS;
  //Server
  WiFiEspServer server(80);
  WifiEspRingBuffer buf(8);
  //NTP
  unsigned int localPort = 2390;      // local port to listen for UDP packets
  IPAddress timeServer(129, 6, 15, 28); // time.nist.gov NTP server 
  const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
  byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
  WiFiEspUDP Udp;

//NTP
unsigned int lastNtpTry = 0;
#define NTP_TRIES 5
TimeChangeRule tcrCET = {"CET", First, Sun, Nov, 2, 60};   //UTC+1
TimeChangeRule tcrCEST = {"CEST", First, Sun, Nov, 2, 60};   //UTC+2
Timezone myTZ(tcrCET, tcrCEST);
TimeChangeRule *tcr; 

//EEPROM
#define AT24C32_ADDRESS 0x50

//SCHEDULE
#define MAX_NMB_OF_SCHEDULE 20
#define NMB_OF_ELEMENTS_PER_SCHEDULE 6
byte schedule[MAX_NMB_OF_SCHEDULE][NMB_OF_ELEMENTS_PER_SCHEDULE];

//LED
#define NUMBER_OF_LED 8

void setup() {
  //Debug
  debugSerial.begin(9600);
  while (!debugSerial);
  debugSerial.println("---");
  
  //Wifi
  espSerial.begin(115200);
  WiFi.init(&espSerial);
  if (WiFi.status() == WL_NO_SHIELD) 
    debugSerial.println("WiFi shield not present");
  while ( status != WL_CONNECTED) {
    debugSerial.print("Attempting to connect to WPA SSID: ");
    debugSerial.println(ssid);
    status = WiFi.begin(ssid, pass);
  }
  debugSerial.println("You're connected to the network");
  printWifiStatus();
    //Server
    server.begin();
    //NTP
    Udp.begin(localPort);

  //LED
  initPWM();
  debugSerial.println("PWM Initialized");

  //NTP
  debugSerial.println("First try to get NTP Time");
  int ntpTry = 1;
  while(!getNtpTime() && ntpTry <= NTP_TRIES){
    debugSerial.println("Failed to get NTP Time. Waiting 3 seconds");
    delay(3000);
    debugSerial.println("Try :"+ntpTry); 
    ntpTry++;
  }
  ntpTry = 1;

  //SCHEDULE
  //Clear the schedule array
  for(int i=0;i<MAX_NMB_OF_SCHEDULE;i++){
    for(int j=0;j<NMB_OF_ELEMENTS_PER_SCHEDULE;j++){
      schedule[i][j] = 0;
    }
  }

  //EEPROM
  EEPROMInit();
  fetchSchedule();
}


void loop() {
  webServer();
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

void printWifiStatus() {
  // print the SSID of the network you're attached to
  debugSerial.print("SSID: ");
  debugSerial.println(WiFi.SSID());

  // print your WiFi shield's IP address
  IPAddress ip = WiFi.localIP();
  debugSerial.print("IP Address: ");
  debugSerial.println(ip);
  
  // print where to go in the browser
  debugSerial.println();
  debugSerial.print("To see this page in action, open a browser to http://");
  debugSerial.println(ip);
  debugSerial.println();
}

void webServer(){
WiFiEspClient client = server.available();  // listen for incoming clients

  if (client) {                               // if you get a client,
    debugSerial.println("New client");             // print a message out the serial port
    buf.init();                               // initialize the circular buffer
    while (client.connected()) {              // loop while the client's connected
      if (client.available()) {               // if there's bytes to read from the client,
        char c = client.read();               // read a byte, then
        buf.push(c);                          // push it to the ring buffer
        
        // you got two newline characters in a row
        // that's the end of the HTTP request, so send a response
        if (buf.endsWith("\r\n\r\n")) {
          sendIndexHttpResponse(client);
          break;
        }

        if (buf.endsWith("edit")) {
          sendScheduleHttpResponse(client);
          break;
        }
        
        // Check to see if the client request
        else if (buf.endsWith("schedule")) {
          debugSerial.println("Received a schedule. Parsing...");
          parseHTTPSchedule(client);
          pushSchedule();
          break;
        }
      }
    }
    
    // close the connection
    client.stop();
    debugSerial.println("Client disconnected");
  }
}

void sendIndexHttpResponse(WiFiEspClient client){
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println();

  client.println("<html><head><title>Aquarium</title></head><body>");
  client.println("<span style=\"font-weight:bold;\">Time: </span>"+String(weekdayEU())+" " +String(day())+"/"+String(month())+"/"+String(year())+" "+String(hour())+":"+String(minute())+"<br /><br />");
  client.println("<span style=\"white-space: pre-wrap; font-weight:bold;\">LED:\t1\t2\t3\t4\t5\t6\t7\t8</span><br />");
  client.println("<span style=\"white-space: pre-wrap;\">\t\t100\t50\t30\t0\t0\t0\t0\t0</span><br /><br />");
  client.println("<span style=\"font-weight:bold;\">Mode: </span><a href=\"/H\">Manual</a> - <a href=\"/H\">Automatic</a> - <a href=\"/H\">Off</a><br /><br />");
  client.println("<a href=\"/edit\">View and edit schedules</a><br /><br />");
  client.println("</body></html>");
  
  // The HTTP response ends with another blank line:
  client.println();
}

void sendScheduleHttpResponse(WiFiEspClient client){
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println();

  client.println("<html><head><title>Aquarium</title></head><body>");
  client.println("<form action=\"\" method=\"get\"><textarea name=\"schedule\" rows=\"10\" cols=\"80\">"+printScheduleTable()+"</textarea><br /><input type=\"submit\"></form><br /><br />");
  client.println("<span style=\"font-weight:bold;\">Template: </span>dayOfWeek - hours - minutes - mode - brightness - speed<br /><br />");
  client.println("<span style=\"font-weight:bold;\">day of week : </span><br /><span style=\"white-space: pre-wrap;\">Monday &rarr; 1&nbsp&nbsp&nbsp&nbspTuesday &rarr; 2&nbsp&nbsp&nbsp&nbspWednesday &rarr; 4&nbsp&nbsp&nbsp&nbspThursday &rarr; 8&nbsp&nbsp&nbsp&nbspFriday &rarr; 16&nbsp&nbsp&nbsp&nbsp&nbspSaturday &rarr; 32&nbsp&nbsp&nbsp&nbspSunday &rarr; 64<br /><span style=\"white-space: pre-wrap;\">Example :\tdays of week: 31<br />\t\t\tweek-ends: 96<br />\t\t\tall days: 127</span><br />");
  client.println("<span style=\"font-weight:bold;\">hour: </span>0-23<br />");
  client.println("<span style=\"font-weight:bold;\">minute: </span>0, 15, 30, 45<br />");
  client.println("<span style=\"font-weight:bold;\">mode : </span><span style=\"white-space: pre-wrap;\">\t0: OFF</span><br />");
  client.print("<span style=\"font-weight:bold;\">brightness : </span><br /><span style=\"white-space: pre-wrap;\">\t0 &rarr; 0%\t\t1 &rarr; 5%\t\t2 &rarr; 10%\t\t3 &rarr; 15%\t\t4 &rarr; 20%\t\t5 &rarr; 25%\t\t6 &rarr; 30%\t\t7 &rarr; 35%\t\t8 &rarr; 40%\t\t9 &rarr; 45%\t\t10 &rarr; 50%<br />");
  client.println("\t11 &rarr; 55%\t12 &rarr; 60%\t13 &rarr; 65%\t14 &rarr; 70%\t15 &rarr; 75%\t16 &rarr; 80%\t17 &rarr; 85%\t18 &rarr; 90%\t19 &rarr; 95%\t20 &rarr; 100%</span><br />");
  client.println("<span style=\"font-weight:bold;\">speed : </span>0-7<br />");  
  client.println("</body></html>");
  
  // The HTTP response ends with another blank line:
  client.println();
}

void parseHTTPSchedule(WiFiEspClient client){
  client.read(); // Read the "="
  String schedule = String();
  bool isNewLine = true;
  char c;
  int i = 0;
  while(isNewLine){
    c = client.read();
    if (c != '%'){
      schedule += c;
    }
    else{
      buf.push(c);
      while(!buf.endsWith("%0D%0A") && !buf.endsWith("%3B")){
        c = client.read();
        buf.push(c);
      }

      debugSerial.print("--> ");
      debugSerial.println(schedule);
      parseScheduleToArray(schedule, i);
      
      if(buf.endsWith("0D%0A")){
        isNewLine = true;
        schedule = String();
        i++;
      }
      else{
        isNewLine = false;
      }
    }
  }
}

void parseScheduleToArray(String str, int i){
  debugSerial.println("parsing schedule to array");
  byte dayofweek, hours, minutes, modes, brightness, modespeed;
  char strchar[30];
  str.toCharArray(strchar, 30);

  char *p = strchar;
  char *stra;
  int j = 0;
  while ((stra = strtok_r(p, "-", &p)) != NULL){ // delimiter is the semicolon
    String s = String(stra);
    schedule[i][j] = (byte)s.toInt();
    j++;
  }
  
  /*if (sscanf(strchar, "%d-%d-%d-%d-%d-%d", &dayofweek, &hours, &minutes, &modes, &brightness, &modespeed) == 6) {
    schedule[i][0] = dayofweek;
    schedule[i][1] = hours;
    schedule[i][2] = minutes;
    schedule[i][3] = modes;
    schedule[i][4] = brightness;
    schedule[i][5] = modespeed;
  }*/
  debugSerial.println("Array filled in.");
  debugSerial.println("New array: ");
  //debugSerial.println(printScheduleTable());
}

String printScheduleTable(){
  String str = "";
  for(int i=0; i < MAX_NMB_OF_SCHEDULE; i++){
    //If this schedule is empty, then, do not write it to the EEPROM.
    bool isFinalSchedule = true;
    for(int j=0; j<NMB_OF_ELEMENTS_PER_SCHEDULE;j++){
      if(schedule[i][j] != 0)
        isFinalSchedule = false;
    }
    
    if(isFinalSchedule){
      break;
    }
    else{
      for(int j=0; j<NMB_OF_ELEMENTS_PER_SCHEDULE-1;j++){
        str += schedule[i][j];
        str += '-';
      }
      str += schedule[i][5];
      str += "\n";
    }
  }
  return str;
}

unsigned long sendNTPpacket(IPAddress& address)
{
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

bool getNtpTime(){
  debugSerial.println("Getting NTP Time...");
  sendNTPpacket(timeServer); // send an NTP packet to a time server
  delay(1000); // wait to see if a reply is available
  debugSerial.print("Result of udp.parsePacket: ");
  debugSerial.println(Udp.parsePacket());
  if (Udp.parsePacket()) {
    debugSerial.println("Packet received");
    Udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:
    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    unsigned long epoch = secsSince1900 - seventyYears;
    //setTime(epoch);
    setTime(myTZ.toLocal(epoch, &tcr));
    debugSerial.print("Unix time = ");
    debugSerial.println(epoch);
    debugSerial.print(hour());
    debugSerial.print(":");
    debugSerial.println(minute());
    return true;
  }
  else{
    return false;
  }
}

byte weekdayEU(){
  return (weekday()+6)%7;
}

/********************
 * EEPROM functions *
 * Note: Those lines of code comes from https://github.com/cyberp/AT24Cx
 * Unfortunately, this library does not seem to work on an arduino zero
 * Despite, the tentatives of fixing this problem, I couldn't make it work for an arduino zero.
 * There are no errors at compilation, but the arduino zero is bricked when the code is ran
 * I've stripped down the library to have only the constructors and the Init method.
 * The only way that does not brick the arduino, is to remove the line Wire.begin() in the init method.
 * Of course, doing so break the library.
 */

void EEPROMInit() {
  Wire.begin();
}

void EEPROMWrite(unsigned int address, byte data) {
  Wire.beginTransmission(AT24C32_ADDRESS);
  if(Wire.endTransmission()==0) {
    Wire.beginTransmission(AT24C32_ADDRESS);
    Wire.write(address >> 8);
    Wire.write(address & 0xFF);
    Wire.write(data);
    Wire.endTransmission();
    delay(20);
  }
}

byte EEPROMRead(unsigned int address) {
  byte b = 0;
  int r = 0;
  Wire.beginTransmission(AT24C32_ADDRESS);
  if (Wire.endTransmission()==0) {
    Wire.beginTransmission(AT24C32_ADDRESS);
    Wire.write(address >> 8);
    Wire.write(address & 0xFF);
    if (Wire.endTransmission()==0) {
      Wire.requestFrom(AT24C32_ADDRESS, 1);
      while (Wire.available() > 0 && r<1) {
        b = (byte)Wire.read();
        r++;
      }
    }
  }
  return b;
}

void fetchSchedule(){
  int i;
  for(i=0; i < EEPROMRead(0) && i < MAX_NMB_OF_SCHEDULE; i++){
    byte firstByte = 10+i*4;
    schedule[i][0] = EEPROMRead(firstByte); // Days of Week (1 = Monday)
    byte hourminute = EEPROMRead(firstByte+1);
    schedule[i][1] = (hourminute & B11111000 ) >> 3; // Hours
    schedule[i][2] = (hourminute & B00000111 )*15; // Minutes
    schedule[i][3] = EEPROMRead(firstByte+2); // Mode
    byte brightnessspeed = EEPROMRead(firstByte+3);
    schedule[i][4] = ((brightnessspeed & B11111000) >> 3)*5; // Brightness
    schedule[i][5] = (brightnessspeed & B00000111); // Speed
    
    if(schedule[i][4] > 100){
      debugSerial.println("ERROR in fetchSchedule: one of the brithness is too big.");
    }
  }
  debugSerial.print(i);
  debugSerial.println(" rules retrived from EEPROM");
}

void pushSchedule(){
  int i;
  for(i=0; i < MAX_NMB_OF_SCHEDULE; i++){
    //If this schedule is empty, then, do not write it to the EEPROM.
    bool isFinalSchedule = true;
    for(int j=0; j<NMB_OF_ELEMENTS_PER_SCHEDULE;j++){
      if(schedule[i][j] != 0)
        isFinalSchedule = false;
    }
    if(isFinalSchedule)
      break;

    //Write this schedule to the EEPROM
    byte firstByte = 10+i*4;
    EEPROMWrite(firstByte, schedule[i][0]); // Days of week
    byte hourminute = (schedule[i][1] << 3) + (schedule[i][2]/15); 
    EEPROMWrite(firstByte+1, hourminute);
    EEPROMWrite(firstByte+2, schedule[i][3]);
    byte brightnessspeed = ((schedule[i][4] << 3)/5) + schedule[i][5];
    EEPROMWrite(firstByte+3, brightnessspeed);
  }
  EEPROMWrite(0,i);
  debugSerial.print(i);
  debugSerial.println(" rules writen in EEPROM");
}
