#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include "RTClib.h"
#include <OneWire.h>

//
// A simple data logger for OneWire temperature sensors.
// A remix of some examples and previous work.
// AS 06-2022
//

// Time settings
const unsigned long TICK = 100;               // system tick in ms
const unsigned long READ_INTERVAL = 2000;     // Time between sensor readings in ms
const unsigned long LOG_INTERVAL = 60000;     // ms between entries (reduce to take more/faster data)
const unsigned long SYNC_INTERVAL = 10 * LOG_INTERVAL;  // ms between calls to flush() to write data
const unsigned long LED_TIME = 2000;          // Warning before flush

#define ECHO_TO_SERIAL                        // echo data to serial port

// the digital pin that connect to the LED
const int redLEDpin = 9; 
const int greenLEDpin = 7;

// define the Real Time Clock object
RTC_DS1307 RTC; 

// for the data logging shield, we use digital pin 10 for the SD cs line
const int chipSelect = 10;

// the logging file
File logfile;

// OneWire temperature sensors
OneWire  ds(8);  // on pin 8 (a 4.7k pull-up resistor is necessary)
const int NUMBER_OF_SENSORS = 5;

static float sensor_output[NUMBER_OF_SENSORS][2] {}; // Accumulated sensor outputs and availability. global.
const byte ow_addresses[NUMBER_OF_SENSORS][8] {      // Sensor oneWire addresses, to be foud using the OneWire example sketch. 
  {0x28, 0xFF, 0xD6, 0x37, 0x22, 0x17, 0x04, 0x7C }, // 28 FF D6 37 22 17 04 7C "brown".
  {0x28, 0xFF, 0x71, 0x0E, 0x85, 0x16, 0x05, 0xD3 }, // 28 FF 71 0E 85 16 05 D3 "red".
  {0x28, 0xFF, 0x3D, 0xE5, 0x84, 0x16, 0x05, 0x6D }, // 28 FF 3D E5 84 16 05 6D "yellow".
  {0x28, 0xFF, 0x50, 0xCD, 0x84, 0x16, 0x05, 0xFC }, // 28 FF 50 CD 84 16 05 FC "green".
  {0x28, 0xFF, 0xDB, 0x38, 0x22, 0x17, 0x04, 0x0A }, // 28 FF DB 38 22 17 04 0A "blue".

};

// debug sensors
// #define debug_temp               // DS18S20 routine debug messages

void error(char *str)
{
  Serial.print("error: ");
  Serial.println(str);

  // red LED indicates error
  digitalWrite(redLEDpin, HIGH);

  while (1);
}

void setup(void)
{
  Serial.begin(115200);
  Serial.println();

  // use debugging LEDs
  pinMode(redLEDpin, OUTPUT);
  pinMode(greenLEDpin, OUTPUT); 

  // initialize the SD card
  Serial.print("Initializing SD card...");
  // make sure that the default chip select pin is set to
  // output, even if you don't use it:
  pinMode(10, OUTPUT);

  // see if the card is present and can be initialized:
  if (!SD.begin(chipSelect)) {
    error("Card failed, or not present");
  }
  Serial.println("card initialized.");

  // create a new file
  char filename[] = "LOGGER00.CSV";
  for (uint8_t i = 0; i < 100; i++) {
    filename[6] = i / 10 + '0';
    filename[7] = i % 10 + '0';
    if (! SD.exists(filename)) {
      // only open a new file if it doesn't exist
      logfile = SD.open(filename, FILE_WRITE);
      break;  // leave the loop!
    }
  }

  if (! logfile) {
    error("couldnt create file");
  }

  Serial.print("Logging to: ");
  Serial.println(filename);

  Serial.print("Read interval s:  "); 
  Serial.println(READ_INTERVAL / 1000);
  
  Serial.print("Log interval s:   "); 
  Serial.println(LOG_INTERVAL / 1000);
  
  Serial.print("Flush interval s: "); 
  Serial.println(SYNC_INTERVAL / 1000);

  // connect to RTC
  Wire.begin();
  if (!RTC.begin()) {
    logfile.println("RTC failed");
#ifdef ECHO_TO_SERIAL
    Serial.println("RTC failed");
#endif  //ECHO_TO_SERIAL
  }


  logfile.println("datetime, brown, red, yellow, green, blue");
#ifdef ECHO_TO_SERIAL
  Serial.println("datetime, brown, red, yellow, green, blue");
#endif //ECHO_TO_SERIAL

}

void loop(void)
{

  read_DS_temperature();

  log_data();

  wait_tick();

}

void log_data(void) {

  static unsigned long lastLog = 0;
  static unsigned long lastFlush = 0;

  // switch red led on ahead of flushing, even if not logging. so LED_TIME can be < SYNC_INTERVAL
  if ((millis() - lastFlush) > (SYNC_INTERVAL - LED_TIME)) digitalWrite(redLEDpin, HIGH);

  if (millis() - lastLog < LOG_INTERVAL) return;
  lastLog = millis();

  DateTime now;

  // fetch the time
  now = RTC.now();
  // log time

  logfile.print(now.year(), DEC);
  logfile.print("/");
  logfile.print(now.month(), DEC);
  logfile.print("/");
  logfile.print(now.day(), DEC);
  logfile.print(" ");
  logfile.print(now.hour(), DEC);
  logfile.print(":");
  logfile.print(now.minute(), DEC);
  logfile.print(":");
  logfile.print(now.second(), DEC);
  logfile.print(",");

#ifdef ECHO_TO_SERIAL
  Serial.print(now.year(), DEC);
  Serial.print("/");
  Serial.print(now.month(), DEC);
  Serial.print("/");
  Serial.print(now.day(), DEC);
  Serial.print(" ");
  Serial.print(now.hour(), DEC);
  Serial.print(":");
  Serial.print(now.minute(), DEC);
  Serial.print(":");
  Serial.print(now.second(), DEC);
  Serial.print(",");
#endif //ECHO_TO_SERIAL

  for (int i = 0; i < (sizeof(ow_addresses) / (sizeof(byte) * 8)); i++) {
    float temperature = (float)sensor_output[i][0] / (float)sensor_output[i][1];
    sensor_output[i][0] = 0;
    sensor_output[i][1] = 0;

    logfile.print(temperature);
    logfile.print(",");
#ifdef ECHO_TO_SERIAL
    Serial.print(temperature);
    Serial.print(",");
#endif
  }

  logfile.println();
#ifdef ECHO_TO_SERIAL
  Serial.println();
#endif // ECHO_TO_SERIAL

  // Now we write data to disk! Don't sync too often - requires 2048 bytes of I/O to SD card
  // which uses a bunch of power and takes time

  if ((millis() - lastFlush) < SYNC_INTERVAL) return;
  lastFlush = millis();

  logfile.flush();

#ifdef ECHO_TO_SERIAL
  Serial.println("Flush!");
#endif

  digitalWrite(redLEDpin, LOW); // Rode led weer uit.

}

void wait_tick(void) {
  // Wait up to TICK ms in this idle & load measurement routine
  static unsigned long start_time;   // ms. Start time of last system tick
  digitalWrite(greenLEDpin, LOW); 
  while (millis() - start_time  < TICK) { // https://arduino.stackexchange.com/questions/12587/how-can-i-handle-the-millis-rollover
    delay(1); // Do nosePicking();
  }
  digitalWrite(greenLEDpin, HIGH);
  start_time = millis();
}
