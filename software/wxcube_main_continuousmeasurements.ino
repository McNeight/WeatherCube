//----------------PREAMBLE-----------------
#define SERIAL_ID 17
#define GAIN_INDEX 2  //defines ADC range -- 1 for 3.3V, [2 for 2.048V], 3 for 1.024V
#define DEBUG_MODE 0
#define DO_SETUP_TEST_POST false

int start_second = 0;
int start_minute = 58;
int start_hour = 8;
int start_day_of_week = 3; //Sunday is 0, Saturday 7
int start_day = 11;
int start_month = 4;
int start_year = 18;

bool _reset = false; // whether to reset eeprom; always rerun
//----------------libraries-----------------
#include <Statistic.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <Adafruit_ADS1015.h>
#include "LMP91000.h"
#include "ClosedCube_HDC1080.h"
#include "Adafruit_SHT31.h"
#include <EEPROM.h>
#include "LowPower.h"

//#include "keys.h" // wifi passwords, amazon table details, serial id

//--------------- definitions-----------------
#define SEND_HOUR 13
#define FAN_INTERVAL 30000//30000
//#define READ_INTERVAL 180//180000//60000 // change for debug
//#define SLEEP_INTERVAL 600 //600000
#define RTC_ADDR 0x6F
#define RTC_TS_BITS 7
#define TIME_REG 0x00
#define EEP0 0x50    //Address of 24LC256 eeprom chip
#define EEP1 0x51    //Address of 24LC256 eeprom chip

#define ID_LOCATION 0 // where we save the id
#define CHECK_SETUP_INDEX 2 // where we save whether or not we've run the do_once setup functions, including setting id and setting clock
#define CHECK_SEND_LOCATION 4// where we save if the data successfully send 
#define LOOP_COUNTER_LOCATION 5 // where we log the number of data points not sent, or loop_counter
#define EEP_WRITE_LOCATION_INDEX 6// where we log where the counter is; keep rolling it over to conserve lifetime of eeprom memory
#define START_WRITE_LOCATION 64 // 2nd page 
#define MAX_MESSAGE_LENGTH 25

#define VDIV 5.02   //voltage divider: (1 + 4.02) / 1
#define VREF 1.1    //value of ATMega328 internal reference voltage
#define VREF_EN 4
#define WIFI_EN 8
#define FAN_EN 9
#define VOLT A0

// numeric code to index the array LMP91000
#define CO    0
#define EtOH  1
#define H2S   2
#define SO2   3
#define NO2   4
#define O3    5
#define IAQ   6
#define RESP  7

#define HDC_ADDRESS 0x40

#define DATA_ARRAY_SIZE 18
#define EEPROM_BLOCKSIZE 36
#define TOTAL_MEASUREMENT_TIME 1 // In minutes
#define SLEEP_MINUTES 0//112
#define MAX_POST_TRIES 10

int gain_index = GAIN_INDEX;
float mv_adc_ratio = 0.0;
adsGain_t gain[6] = {GAIN_TWOTHIRDS, GAIN_ONE, GAIN_TWO, GAIN_FOUR, GAIN_EIGHT, GAIN_SIXTEEN};
float mv_ratios[6] = {0.1875, 0.125, 0.0625, 0.03125, 0.015625, 0.0078125};
adsGain_t adc_pga_gain;

Adafruit_SHT31 sht31 = Adafruit_SHT31();
Adafruit_SHT31 sht32 = Adafruit_SHT31();
ClosedCube_HDC1080 hdc1080;
LMP91000 lmp91000;
Adafruit_ADS1115 ads(0x49);
// initialize esp and atmega talking
// software serial mimicking hardware serial
SoftwareSerial mySerial(5, 6); // RX, TX

//From D-ULPSM Rev 0.3 LMP91000 Settings //need to set TIACN register, REFCN register, MODECN register
// this is a 2-d array because the settings need to be split between different bytes
int LMP91000_settings[8][2] = {
  {LMP91000_TIA_GAIN_EXT | LMP91000_RLOAD_50OHM, LMP91000_REF_SOURCE_EXT | LMP91000_INT_Z_20PCT | LMP91000_BIAS_SIGN_POS | LMP91000_BIAS_1PCT},  //CO
  {LMP91000_TIA_GAIN_EXT | LMP91000_RLOAD_50OHM, LMP91000_REF_SOURCE_EXT | LMP91000_INT_Z_20PCT | LMP91000_BIAS_SIGN_POS | LMP91000_BIAS_4PCT}, //EtOH
  {LMP91000_TIA_GAIN_EXT | LMP91000_RLOAD_50OHM, LMP91000_REF_SOURCE_EXT | LMP91000_INT_Z_20PCT | LMP91000_BIAS_SIGN_POS | LMP91000_BIAS_0PCT},  //H2S
  {LMP91000_TIA_GAIN_EXT | LMP91000_RLOAD_50OHM, LMP91000_REF_SOURCE_EXT | LMP91000_INT_Z_50PCT | LMP91000_BIAS_SIGN_POS | LMP91000_BIAS_10PCT}, //SO2
  {LMP91000_TIA_GAIN_EXT | LMP91000_RLOAD_50OHM, LMP91000_REF_SOURCE_EXT | LMP91000_INT_Z_50PCT | LMP91000_BIAS_SIGN_NEG | LMP91000_BIAS_10PCT}, //NO2  //INT_Z 20
  {LMP91000_TIA_GAIN_EXT | LMP91000_RLOAD_50OHM, LMP91000_REF_SOURCE_EXT | LMP91000_INT_Z_50PCT | LMP91000_BIAS_SIGN_NEG | LMP91000_BIAS_1PCT},  //O3   //INT_Z 20
  {LMP91000_TIA_GAIN_EXT | LMP91000_RLOAD_50OHM, LMP91000_REF_SOURCE_EXT | LMP91000_INT_Z_20PCT | LMP91000_BIAS_SIGN_POS | LMP91000_BIAS_8PCT},  //IAQ
  {LMP91000_TIA_GAIN_EXT | LMP91000_RLOAD_50OHM, LMP91000_REF_SOURCE_EXT | LMP91000_INT_Z_50PCT | LMP91000_BIAS_SIGN_NEG | LMP91000_BIAS_10PCT}  //RESP
};
int MODECN_DEFAULT = LMP91000_FET_SHORT_DISABLED | LMP91000_OP_MODE_AMPEROMETRIC;

// declaring instances of objects in the statistic class
Statistic stat0;
Statistic stat1;
Statistic stat2;
Statistic stat3;
Statistic stat4;
Statistic stat5;
Statistic stat6;
Statistic stat7;
Statistic stat8;
Statistic stat9;

char inbyte;
//byte cbuf[MAX_MESSAGE_LENGTH];
char* _test;

long post_index = 0;
bool running_fan = true;
bool debug_set_rtc = false; // run the clock debug mode, sets clock and prints it
bool fan_on;
long read_interval, fan_interval;
bool immediate_run = false;
bool debug_run = false;
//int reading_location = 10;
int data_array[DATA_ARRAY_SIZE];
int loop_counter = 0;
//int loop_minimum = 96;
int eeprom_write_location = 64;
int device;
byte integer_time[RTC_TS_BITS]; // what we'll store time array in
char types[] = {"oanbschdtrefgivHmN"};

void setup()
{ 
  Serial.begin(57600);
  Serial.println("Starting setup...");

  //----------------pin stuff-----------------
  // define which way pins work and turn on/off
  analogReference(INTERNAL);
  pinMode(VOLT, INPUT);

  pinMode(VREF_EN, OUTPUT);
  pinMode(13, OUTPUT);
  pinMode(12, OUTPUT);
  pinMode(11, OUTPUT);
  pinMode(10, OUTPUT);
  pinMode(WIFI_EN, OUTPUT);
  pinMode(FAN_EN, OUTPUT);

  digitalWrite(VREF_EN, LOW);
  digitalWrite(FAN_EN, LOW);
  digitalWrite(WIFI_EN, HIGH);
  delay(100);
  mySerial.begin(9600);
  Wire.begin();

  
  //rtc_write_date(start_second, start_minute, start_hour, start_day_of_week, start_day, start_month, start_year); //uncomment to manually update RTC timestamp

  // to reset...
  if (_reset == true) {
    writeEEPROM(EEP0, CHECK_SETUP_INDEX, 0);
  }
  //
  Serial.println(F("finished pin setup"));
  Serial.println(readEEPROM(EEP0, CHECK_SETUP_INDEX));
  int check_variable = readEEPROM(EEP0, CHECK_SETUP_INDEX);

  if (check_variable != 57) {
    Serial.println(F("Doing initial setup..."));
    do_once();
    writeEEPROM(EEP0, CHECK_SETUP_INDEX, 57);
    writeEEPROM(EEP0, EEP_WRITE_LOCATION_INDEX, 64);
    writeEEPROM(EEP0, LOOP_COUNTER_LOCATION, 0);
  }
  else {
    Serial.println(F("Initial setup already completed"));
    loop_counter = readEEPROM(EEP0, LOOP_COUNTER_LOCATION);
    eeprom_write_location = readEEPROM(EEP0, EEP_WRITE_LOCATION_INDEX);
  }
  Serial.println(F("check variable completed"));

  //----------------set adc/gain stuff------------------
  set_adc_gain(gain_index);
  ads.begin();

  //----------------set afe stuff-----------------------
  set_afe(10, O3, 1); // (pin number, sensor type, index#)
  set_afe(11, H2S, 2);
  set_afe(12, SO2, 3);
  set_afe(13, NO2, 4);

  //----------------set clock---------------------------
  delay(50);
  
  rtc_read_timestamp(1);
  Serial.print(F("Timestamp: "));
  Serial.print(integer_time[2]);Serial.print(':');
  Serial.print(integer_time[1]);Serial.print(':');
  Serial.print(integer_time[0]);Serial.print(", ");
  Serial.print(integer_time[5]);Serial.print('/');
  Serial.print(integer_time[4]);Serial.print('/');
  Serial.print(integer_time[6]);Serial.println();
  
  /*for (int i = 0; i < RTC_TS_BITS; i++) {
    int out = integer_time[i];
    if(out < 10) Serial.print('0');
    Serial.print(integer_time[i]);
  }
  Serial.println();*/
  
  // turn on thermometers
  sht31.begin(0x44);
  sht32.begin(0x45);
  hdc1080.begin(0x40);

  if (DO_SETUP_TEST_POST) test_post();
  delay(50);
  Serial.println(F("setup completed"));
  digitalWrite(WIFI_EN, LOW);

  delay(3000);

}

void loop() // run over and over
{
  long toc = millis();
  acquire_data();
  process_data();
  Serial.println(F("Increasing loop_counter to : "));
  Serial.println(++loop_counter);
  delay(500);
}

void acquire_data() {
  digitalWrite(VREF_EN, HIGH);
  //----------------fan on-----------------
  Serial.println("fan on");
  digitalWrite(FAN_EN, HIGH); // delay(FAN_INTERVAL);
  // turn fan off and wait for anything trapped in inductive coils to discharge
  //digitalWrite(FAN_EN, LOW);  delay(2000);
  ////-----------take readings------------------

  Serial.println(F("Taking data..."));
  read_data(); // note: updates data_array
  // sensor 1, sensor 2, sensor 3, sensor4, temp, rh, temp, rh temp, rh,
  digitalWrite(FAN_EN, LOW);
}

void read_data()
{
  int number_reads = 0;
  stat0.clear();
  stat1.clear();
  stat2.clear();
  stat3.clear();
  rtc_read_timestamp(1);
  long toc = millis();
  float a = 0;
  float scaler = 1;
  Serial.println(F("air quality reads..."));
  while (millis() - toc < TOTAL_MEASUREMENT_TIME * 60000) {
    long toc2 = millis();
    //a = convert_to_mv(ads.readADC_SingleEnded(0)); delay(10);
    a = ads.readADC_SingleEnded(0); delay(10);
    stat0.add(scaler * a);
    Serial.print(scaler * a); Serial.print('\t');

    //a = convert_to_mv(ads.readADC_SingleEnded(1)); delay(10);
    a = ads.readADC_SingleEnded(1); delay(10);
    stat1.add(scaler * a);
    Serial.print(scaler * a); Serial.print('\t');

    //a = convert_to_mv(ads.readADC_SingleEnded(2)); delay(10);
    a = ads.readADC_SingleEnded(2); delay(10);
    stat2.add(scaler * a);
    Serial.print(scaler * a); Serial.print('\t');

    //a = convert_to_mv(ads.readADC_SingleEnded(3)); delay(10);
    a = ads.readADC_SingleEnded(3); delay(10);
    stat3.add(scaler * a);
    Serial.print(scaler * a); Serial.print('\t');
    Serial.println();
    while (millis() - toc2 < 1000) {
      delay(50);  // make sure we're waiting 1 sec between reads
    }
    ++number_reads;
  }

  scaler = 100;
  //read temp/hum sensors
  Serial.println(F("hum/temp reads..."));
  for (int temp_counter = 0; temp_counter < 10; temp_counter++) {
    a = hdc1080.readTemperature();
    Serial.print(scaler * a); Serial.print('\t');
    stat4.add(scaler * a);

    a = hdc1080.readHumidity();
    Serial.print(scaler * a); Serial.print('\t');
    stat5.add(scaler * a);

    a = sht31.readTemperature();
    Serial.print(scaler * a); Serial.print('\t');
    stat6.add(scaler * a);

    a = sht31.readHumidity();
    Serial.print(scaler * a); Serial.print('\t');
    stat7.add(scaler * a);

    a = sht32.readTemperature();
    Serial.print(scaler * a); Serial.print('\t');
    stat8.add(scaler * a);

    a = sht32.readHumidity();
    Serial.print(scaler * a);//Serial.print('\t');
    stat9.add(scaler * a);
    Serial.println();
  }

  rtc_read_timestamp(1); // updates integer_time
  data_array[0] = stat0.average();
  data_array[1] = stat0.unbiased_stdev();
  data_array[2] = stat1.average();
  data_array[3] = stat1.unbiased_stdev();
  data_array[4] = stat2.average();
  data_array[5] = stat2.unbiased_stdev();
  data_array[6] = stat3.average();
  data_array[7] = stat3.unbiased_stdev();
  data_array[8] = stat4.average();
  data_array[9] = stat5.average();
  data_array[10] = stat6.average();
  data_array[11] = stat7.average();
  data_array[12] = stat8.average();
  data_array[13] = stat9.average();
  data_array[14] = scaler * analogRead(VOLT) / 1023.0 * VREF * VDIV;
  data_array[15] = 100 * (integer_time[2]) + integer_time[1]; //hour minute
  data_array[16] = integer_time[5] * 100 + integer_time[4]; //month day
  data_array[17] = number_reads;
}

void process_data()
{
  // note: have 4 more bytes, maybe can add timestamp?
  Serial.println(F("Data taken is..."));
  for (int i = 0; i < DATA_ARRAY_SIZE; i++) {
    Serial.println(data_array[i]);
  }

  ////-----------save readings------------------
  Serial.println(F("Writing data"));
  // note: should have that eeprom_write_location = loop_counter*32
  // pick which eeprom memory to go to
  // first case: eeprom write location less than size of 1st eepro
  if (eeprom_write_location < (1024.0 * 128.0 - 64.0)) {
    device = EEP0;
  }
  // second case: eeprom write location needs to move to eeprom2
  else if (eeprom_write_location < (1024.0 * 128.0)) {
    device = EEP1;
    eeprom_write_location = 0;
  }
  // third case: eeprom write location needs to move back to eeprom1
  else {
    device = EEP0;
    eeprom_write_location = 64;
  }
  Serial.println(F("from :"));
  Serial.println(eeprom_write_location + 0 * 2); //+ loop_counter * EEPROM_BLOCKSIZE );
  Serial.println(F("to:"));
  Serial.println(eeprom_write_location + DATA_ARRAY_SIZE * 2);  //+ loop_counter * EEPROM_BLOCKSIZE );

  for (int i = 0; i < DATA_ARRAY_SIZE; i += 1) {
    int save_location = eeprom_write_location + i * 2;// + loop_counter * EEPROM_BLOCKSIZE ;
    //Serial.println(save_location);
    writeEEPROMdouble(device, save_location , (data_array[i] + 32768)); // note: save as signed integer
  }

  eeprom_write_location = eeprom_write_location + EEPROM_BLOCKSIZE;
  ///// send data

  //sendData();
  // deep sleep
  // note: millis() won't count up while in 'deep sleep' mode or idle mode
  //    rtc_read_timestamp(1);
  //    int minute_0 = integer_time[1];
  //    int minute = integer_time[1];
  //    int hour;
  //    while ( abs((minute - minute_0)) % 60 < SLEEP_MINUTES) {
  //      Serial.println("entering deep sleep mode");
  //      delay(500);
  //      for (int i = 0; i < 7; i++) {
  //        LowPower.idle(SLEEP_8S, ADC_OFF, TIMER2_OFF, TIMER1_OFF, TIMER0_OFF,
  //                      SPI_OFF, USART0_OFF, TWI_OFF);
  //      }

  rtc_read_timestamp(1);
  int _minute = integer_time[1];
  int _hour = integer_time[2];

  /*
  if(abs((minute - SERIAL_ID)) % 60 < 2) { // && hour == SEND_HOUR) {
  {
    //Serial.println("time to send data!");
    //Serial.println("Sending data..");
    // digitalWrite(FAN_EN, LOW);

    //Serial.println("sendData() DISABLED!");
    sendData();
    //Serial.println("Going back to sleep...") ;
    delay(500);
    // go back to sleep for 4 minutes so we don't double send
    //        for (int i = 0; i < 7 * 3; i++) {
    //          LowPower.idle(SLEEP_8S, ADC_OFF, TIMER2_OFF, TIMER1_OFF, TIMER0_OFF,
    //                        SPI_OFF, USART0_OFF, TWI_OFF);
    //        }

    writeEEPROM(EEP1, EEP_WRITE_LOCATION_INDEX, eeprom_write_location);
    rtc_read_timestamp(1);
    _minute = integer_time[1];
  }
  */
  //      Serial.println("time difference is...") ;
  //      Serial.println(abs((minute - minute_0) % 60)) ;
  //      delay(50);
  //      // deep sleep
  //    }
  //    Serial.println("Waking up...");

  //Serial.println("Sending data..");
  //sendData();
  //    while ( mySerial.available()) {
  //      inbyte = mySerial.read();
  //      if (inbyte == '#') {
  //        inbyte = mySerial.read();
  //        if (inbyte == 'S') {
  //          loop_counter = 0;
  //        }
  //      }
  //      delay(10);
  //    }
}


//----------------FUNCTIONS----------------------

void do_once() { // do at least once, but not all the time
  //----------------set id----------------
  Serial.println(F("Doing do_once()"));
  // set id (& save it and EEprom)
  writeEEPROM(EEP0, ID_LOCATION, SERIAL_ID);
  delay(10);
  //----------------set clock-----------------
  rtc_write_date(start_second, start_minute, start_hour, start_day_of_week, start_day, start_month, start_year); // note: rename function to in order of the time registers in the memory of the rtc
  // second, minute, hour, day of the week, day, month, year
  delay(10);

  // set clock & save to rtc
  if (DEBUG_MODE == 1) {
    Serial.println(F("setup:"));
    Serial.println(readEEPROM(EEP0, ID_LOCATION), DEC);
    delay(10);
    rtc_read_timestamp(0);
    delay(10);
  }
  else {
  }
  // set gain & save to EEprom
}

void test_post()
{
  digitalWrite(WIFI_EN, HIGH);
  delay(2000);
  long one_reading_array[EEPROM_BLOCKSIZE];
  memset(one_reading_array, 0, sizeof(one_reading_array));
  //// check serial messages
  if (mySerial.available()) {
    while (mySerial.available()) {
      Serial.write(mySerial.read());
    }
  }
  bool post_success =  false;
  int counter = 0;
  while (post_success == false && counter < MAX_POST_TRIES ) {
    //// send data to AWS
    char cbuf[25];
    for (int i = 0; i < 14; i++) { //DATA_ARRAY_SIZE; i++) {
      String s = "";
      s += types[i];
      if ( one_reading_array[i] == -32768) {
        s += "NaN"; //"NaN";
      }
      else {
        s += String(one_reading_array[i]);
      }
      s += "x";
      s.toCharArray(cbuf, MAX_MESSAGE_LENGTH);
      Serial.println(F("data to be posted is..."));
      Serial.println(s);
      mySerial.write(cbuf);
      mySerial.flush();
      delay(800);
    }
    Serial.println(F("posting next set of data..."));

    // hitting limit of 17 fields for dynamodb... adding rest to last field
    String s = "";
    s += types[14];
    for (int i = 14; i < DATA_ARRAY_SIZE; i++) { //DATA_ARRAY_SIZE; i++) {
      if ( one_reading_array[i] == -32768) {
        s += "NaN"; //"NaN";
      }
      else {
        s += String(one_reading_array[i]);
      }
      s += ",";
    }
    s += "x";
    s.toCharArray(cbuf, MAX_MESSAGE_LENGTH);
    Serial.println(F("rest of data to be posted is..."));
    Serial.println(s);

    mySerial.write(cbuf);
    mySerial.flush();
    delay(800);
    mySerial.write("px");
    mySerial.flush();
    Serial.println(F("Attempted post..."));
    delay(5000);
    long toc = millis();
    //while (millis() - toc < 50000) {
    //int counter = 0;
    // while (counter < 25){
    if (mySerial.available()) {
      while (mySerial.available()) { // & counter < 50) {
        inbyte = mySerial.read();
        //Serial.write(mySerial.read());
        Serial.write(inbyte);
        delay(30);
        if (inbyte == '#') {
          inbyte = mySerial.read();
          if (inbyte == 'S') {
            post_success = true;
            Serial.println(F("post success!"));
          }
        }
      }
    }
    counter = counter + 1;
    Serial.println(F("counter increasing to..."));
    Serial.println(counter);
  }


  delay(5000);
  long toc = millis();
  while (millis() - toc < 15000) {
    int counter = 0;
    if (mySerial.available()) {
      while (mySerial.available()) { // & counter < 50) {
        Serial.write(mySerial.read());
        delay(500);
        counter++ ;
      }
    }
  }
  digitalWrite(WIFI_EN, LOW);
}

void sendData() {
  digitalWrite(WIFI_EN, HIGH);
  delay(2000);
  //long one_reading_array[EEPROM_BLOCKSIZE];
  bool post_success;
  for (int reading_counter = loop_counter; reading_counter > 1; reading_counter--) {
    post_success = false;
    Serial.print(F("Sending reading number "));
    Serial.println(reading_counter);
    delay(50);

    //// first pull from EEPROM
    Serial.println(F("Reading from EEPROM at ..."));
    Serial.println(eeprom_write_location - reading_counter * EEPROM_BLOCKSIZE);
    Serial.println(F("to..."));
    Serial.println(eeprom_write_location - reading_counter * EEPROM_BLOCKSIZE + DATA_ARRAY_SIZE * 2 + 1);

    long one_reading_array[EEPROM_BLOCKSIZE];
    for (int i = 0; i < DATA_ARRAY_SIZE * 2; i += 2) {
      byte two_e = readEEPROM(EEP0, eeprom_write_location - reading_counter * EEPROM_BLOCKSIZE + i);
      byte one_e = readEEPROM(EEP0, eeprom_write_location - reading_counter * EEPROM_BLOCKSIZE + i + 1);
      one_reading_array[i / 2] = ((two_e << 0) & 0xFF) + ((one_e << 8) & 0xFFFF) - 32768; //readEEPROMdouble(EEP0,  64 + i + loop_counter * 32);
      delay(50);
    }

    Serial.println(F("Data from EEPROM is..."));
    for (int i = 0; i < DATA_ARRAY_SIZE; i++) {
      Serial.println(one_reading_array[i]);
    }
    //memset(one_reading_array, 0, sizeof(one_reading_array));
    //// check serial messages
    if (mySerial.available()) {
      while (mySerial.available()) {
        Serial.write(mySerial.read());
        delay(10);
      }
    }
    char cbuf[25];
    for (int i = 0; i < 14; i++) { //DATA_ARRAY_SIZE; i++) {
      String s = "";
      s += types[i];
      if ( one_reading_array[i] == -32768) {
        s += "NaN"; //"NaN";
      }
      else {
        s += String(one_reading_array[i]);
      }
      s += "x";
      s.toCharArray(cbuf, MAX_MESSAGE_LENGTH);
      Serial.println(F("data to be posted is..."));
      Serial.println(s);
      mySerial.write(cbuf);
      mySerial.flush();
      delay(800);
    }
    Serial.println(F("posting next set of data..."));

    // hitting limit of 17 fields for dynamodb... adding rest to last field
    String s = "";
    s += types[14];
    for (int i = 14; i < DATA_ARRAY_SIZE; i++) { //DATA_ARRAY_SIZE; i++) {
      if ( one_reading_array[i] == -32768) {
        s += "NaN"; //"NaN";
      }
      else {
        s += String(one_reading_array[i]);
      }
      s += ",";
    }
    s += "x";
    s.toCharArray(cbuf, MAX_MESSAGE_LENGTH);
    Serial.println(F("rest of data to be posted is..."));
    Serial.println(s);

    mySerial.write(cbuf);
    mySerial.flush();
    delay(800);

    //bool post_success =  false;
    int number_tries = 0;
    while (post_success == false && number_tries < MAX_POST_TRIES) {
      mySerial.write("px");
      mySerial.flush();
      Serial.println(F("Attempted post..."));
      delay(5000);
      long toc = millis();
      //while (millis() - toc < 50000) {
      int counter = 0;
      // while (counter < 25){
      if (mySerial.available()) {
        while (mySerial.available()) { // & counter < 50) {
          inbyte = mySerial.read();
          //Serial.write(mySerial.read());
          Serial.write(inbyte);
          delay(30);
          if (inbyte == '#') {
            inbyte = mySerial.read();
            if (inbyte == 'S') {
              post_success = true;
              Serial.println(F("post success!"));
            }
          }
        }
      }
      number_tries = number_tries + 1;
      delay(50);
    }
  }
  if (post_success == true) {
    loop_counter = 0 ;
    Serial.println(F("Writing loop_counter to eep1"));
    writeEEPROM(EEP0, LOOP_COUNTER_LOCATION, loop_counter);
  }
  else {
    Serial.println(F("Writing loop_counter to eep1"));
    Serial.println(loop_counter);
    writeEEPROM(EEP0, LOOP_COUNTER_LOCATION, loop_counter);
    // save loop_counter to memory
  }
  digitalWrite(WIFI_EN, LOW);
}

long writeEEPROMdouble(int deviceaddress, unsigned int eeaddress, int value)
{
  //    int value = input[i];
  //    int address = 64 + 2 * i;
  byte two = (value & 0xFF);
  byte one = ((value >> 8) & 0xFF);
  Wire.beginTransmission(deviceaddress);
  Wire.write((int)(eeaddress >> 8));   // MSB
  Wire.write((int)(eeaddress & 0xFF)); // LSB
  // write 2nd bit
  Wire.write(two);
  // write first byte
  Wire.write(one);
  Wire.endTransmission();
  delay(50);
}

byte readEEPROMdouble(int deviceaddress, unsigned int eeaddress )
{
  long two = readEEPROM(deviceaddress, eeaddress);
  long one = readEEPROM(deviceaddress, eeaddress + 1);
  //    // append bit-shifted data
  int output =  ((two << 0) & 0xFF) + ((one << 8) & 0xFFFF);
  return output;
}

byte readEEPROM(int deviceaddress, unsigned int eeaddress )
{
  byte rdata = 0xFF;

  Wire.beginTransmission(deviceaddress);
  Wire.write((int)(eeaddress >> 8));   // MSB
  Wire.write((int)(eeaddress & 0xFF)); // LSB
  Wire.endTransmission();

  Wire.requestFrom(deviceaddress, 1);

  if (Wire.available()) rdata = Wire.read();

  return rdata;
}

void writeEEPROM(int deviceaddress, unsigned int eeaddress, byte data )
{
  Wire.beginTransmission(deviceaddress);
  Wire.write((int)(eeaddress >> 8));   // MSB
  Wire.write((int)(eeaddress & 0xFF)); // LSB
  Wire.write(data);
  Wire.endTransmission();

  delay(10);
}

void which_afe(int num)
{
  digitalWrite(10, HIGH);
  digitalWrite(11, HIGH);
  digitalWrite(12, HIGH);
  digitalWrite(13, HIGH);
  digitalWrite(num, LOW);
}

void set_afe(int num, int analyte, int index)
{
  delay(500);
  Serial.print(F("Setting AFE"));
  Serial.print(index);
  Serial.print(F("..."));
  which_afe(num);
  delay(100);
  uint8_t res = configure_LMP91000(analyte);
  Serial.println(F("done"));
  Serial.print(F("Config Result: "));    Serial.println(res);
  Serial.print(F("STATUS: "));    Serial.println(lmp91000.read(LMP91000_STATUS_REG), HEX);
  Serial.print(F("TIACN: "));    Serial.println(lmp91000.read(LMP91000_TIACN_REG), HEX);
  Serial.print(F("REFCN: "));    Serial.println(lmp91000.read(LMP91000_REFCN_REG), HEX);
  Serial.print(F("MODECN: "));    Serial.println(lmp91000.read(LMP91000_MODECN_REG), HEX);
  digitalWrite(num, HIGH);
}

float convert_to_mv(float val) {
  if (val > 32767) val -= 65535;
  return val * mv_adc_ratio;
}

//--------AFE and ADC---------------
//-----------------------------------

void set_adc_gain(int g)
{
  gain_index = g;
  ads.setGain(gain[gain_index]);
  mv_adc_ratio = mv_ratios[gain_index];
}

int configure_LMP91000(int gas)
{
  return lmp91000.configure( LMP91000_settings[gas][0], LMP91000_settings[gas][1], MODECN_DEFAULT);
}

//------- CLOCK STUFF ---------------
//-----------------------------------


uint8_t dec2bcd(uint8_t n) {
  return n + 6 * (n / 10);
}

uint8_t bcd2dec(uint8_t n) {
  return n - 6 * (n >> 4);
}

void printBits(byte myByte) {
  for (byte mask = 0x80; mask; mask >>= 1) {
    if (mask  & myByte)
      Serial.print('1');
    else
      Serial.print('0');
  }
  Serial.println();
}

void rtc_write_date(int sec, int mint, int hr24, int dotw, int dy, int mon, int yr)
{
  Wire.beginTransmission(RTC_ADDR);
  Wire.write((uint8_t)TIME_REG);
  Wire.write(dec2bcd(sec) | B10000000);
  Wire.write(dec2bcd(mint));
  Wire.write(dec2bcd(hr24)); //hr
  Wire.write(dec2bcd(dotw) | B00001000); //dotw
  Wire.write(dec2bcd(dy)); //date
  Wire.write(dec2bcd(mon)); //month
  Wire.write(dec2bcd(yr)); //year

  Wire.write((byte) 0);
  //Wire.write((uint8_t)0x00);                     //stops the oscillator (Bit 7, ST == 0)
  /*
     Wire.write(dec2bcd(05));
    Wire.write(dec2bcd(04));                  //sets 24 hour format (Bit 6 == 0)
    Wire.endTransmission();
    Wire.beginTransmission(RTC_ADDR);
    Wire.write((uint8_t)TIME_REG);
    Wire.write(dec2bcd(30) | _BV(ST));    //set the seconds and start the oscillator (Bit 7, ST == 1)
  */
  Wire.endTransmission();
}

void rtc_read_timestamp(int mode)
{
  byte rtc_out[RTC_TS_BITS];
  //int integer_time[RTC_TS_BITS];
  Wire.beginTransmission(RTC_ADDR);
  Wire.write((byte)0x00);
  if (Wire.endTransmission() != 0) {
    Serial.println(F("no luck"));
    //return false;
  }
  else {
    //request 7 bytes (secs, min, hr, dow, date, mth, yr)
    Wire.requestFrom(RTC_ADDR, RTC_TS_BITS);

    // cycle through bytes and remove non-time data (eg, 12 hour or 24 hour bit)
    for (int i = 0; i < RTC_TS_BITS; i++) {
      rtc_out[i] = Wire.read();
      if (mode == 0) printBits(rtc_out[i]);
      else
      {
        //byte b = rtc_out[i];
        if (i == 0) rtc_out[i] &= B01111111;
        else if (i == 3) rtc_out[i] &= B00000111;
        else if (i == 5) rtc_out[i] &= B00011111;
        //int j = bcd2dec(b);
        //Serial.print(j);
        //if(i < how_many - 1) Serial.print(",");
      }
    }
  }

  for (int i = 0; i < RTC_TS_BITS ; i++) {
    int ii = rtc_out[i];
    integer_time[i] = bcd2dec(ii);
  }
}
