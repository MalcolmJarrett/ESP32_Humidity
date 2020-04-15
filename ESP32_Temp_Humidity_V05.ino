/*
   Audit Trail

   V04    mrj Add FTP Option





*/
//WiFi time and Thingspeak libraries
#include <WiFi.h>
#include "time.h"
#include "ThingSpeak.h"

//router access data
const char* ssid       = "BTHub5-SM7M";
const char* password   = "a2d39a76e6";
WiFiClient client;

//data for the NTP routine
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 3600;
char timeStringBuff[50]; //50 chars should be enough to hold out date/ti,e string
struct tm timeinfo;


//ESP32 Deep Sleep parameters
#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  60        /* Time ESP32 will go to sleep (in seconds) */
#define TIME_TO_RESET  60        /* Time ESP32 will go to sleep (in seconds) */
RTC_DATA_ATTR int bootCount = 0;

//Thingspeak parameters
#define THINKSPEAK_API_KEY  "WK8YDXN56S4P9ULR"  //WiFi + SPIFFs
unsigned long myChannelNumber = 728342;
const char * myWriteAPIKey = THINKSPEAK_API_KEY;
int loopcount = 0; //how many times....
String myStatus = "";

//SPFIFS
#include "SPIFFS.h"
///////////////////////////////////////////#include <FS.h>   //Include File System Headers

const char* filename = "/data.csv"; //we'll stick with one filename for the moment

//Add DHT library for sensor
#include "DHT.h"                //added for DHTxx sensors
//Added for DHT22 and WiFi communications. DHT11 not good at 3.3 volts.
// Pin
#define DHTPIN 4  //Try to harmonise with RobotDyn
// Use DHT22 sensor
#define DHTTYPE DHT22
//#define  INPUT_PULLDOWN_16
#define LEDPIN 2  //add a nice blue led
//define a pin to power the DHT22. If we leave the device on the 3v3 rail it drains about 1mA.
#define DHTPOWER 12
float hum;  //Stores humidity value
float temp; //Stores temperature value
float dewpoint;  //Stores dewpoint value
float margin; //Stores magin before condensation occurs
//float millivoltage;   //use this rather than above
float voltage;  //convert from millivolts to volts, then allow for voltage divider chain on analogue pin 680k+220k/100k
float used_space;

//Needed for startup menu
int menucount = 5; //no of seconds to wait for menu input
char menuchar; //return character from Menu
int deleteKey;
char serialInChar;
int readLoopCount = 10;
boolean newData = false;  //flag to show new serial data has arrived
//neede for string input
const byte numChars = 32;
char receivedChars[numChars];   // an array to store the received data
int rxstring;

//Added for FTP function
#include <ESP8266FtpServer.h>
FtpServer ftpSrv;   //set #define FTP_DEBUG in ESP8266FtpServer.h to see ftp verbose on serial
//Let's do a little A->D conversion
//#include <driver/adc.h>
int ADCpin = 36; //define a pin for Vcc readings
float ADCValue;
float ADCadjust = .0019; //convert from disgit in range o-2047 to actual voltage

    
    
void setup()
{
  Serial.begin(115200);
  print_wakeup_reason();
  //connect to WiFi
  Serial.printf("Connecting to %s ", ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" CONNECTED");

  //init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  //SPIFFing
  if (SPIFFS.begin())
  {
    Serial.println("SPIFFS Initialize....ok");
  }
  //initialisation has failed
  else
  {
    Serial.println("SPIFFS Initialization...failed");
  }
  fileCheck();

  //Thingspeak startup
  ThingSpeak.begin(client);  // Initialize ThingSpeak


  pinMode(DHTPOWER, OUTPUT);  //power the DHT22 from an I/O to stop constant power drain
  pinMode(LED_BUILTIN, OUTPUT);

 
  /*
    First we configure the wake up source
    We set our ESP32 to wake up every 5 seconds
  */
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  Serial.println("Setup ESP32 to sleep for  " + String(TIME_TO_SLEEP) + " Seconds");

  clearScreen();
  StartUp();

}

void loop()
{
  //lets ADC to measure Vcc
  ADCValue = analogRead(ADCpin);
  ADCValue = ADCValue * ADCadjust; //make it look something like 3.3 volts...
  Serial.print("Voltage is ");
  Serial.println(ADCValue);
  printLocalTime();
  Serial.println("LED and DHT22 on");
  digitalWrite(LEDPIN, LOW);   // turn the LED on
  digitalWrite(LED_BUILTIN, HIGH); //on board LED
  digitalWrite(DHTPOWER, HIGH);   // turn the DHT22 on
  delay (5000); //allow DHT22 time to stabilise
  /*
  //lets ADC to measure Vcc
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_0,ADC_ATTEN_DB_0); //I think this is GPIO36
  int val = adc1_get_raw(ADC1_CHANNEL_0);
  Serial.print("Voltage is ");
  Serial.println(val);
  */
  readDHT();
  writeThingspeak();
  WriteToFile();
  //dumpFile();

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  //switch everything off
  digitalWrite(LEDPIN, HIGH);   // turn the LED on
  digitalWrite(LED_BUILTIN, LOW); //on board LED
  digitalWrite(DHTPOWER, LOW);   // turn the DHT22 on
  esp_deep_sleep_start();
}

//And now for a few subroutines

void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();


  switch (wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason); break;
  }
}

void fileCheck()
{
  //Check the output file exists
  File f = SPIFFS.open(filename, "r");
  if (!f)
  {
    Serial.print("File does not exist ");
    Serial.println(filename);
  }
  else  //file does exist
  {
    Serial.print("File does exist ");
    Serial.println(filename);
    f.close();  //Close file
  }
}

//quick and dirty dew point calculator
float dewPointFast(double celsius, double humidity)
{
  double a = 17.271;
  double b = 237.7;
  double temp = (a * celsius) / (b + celsius) + log(humidity * 0.01);
  double Td = (b * temp) / (a - temp);
  return Td;
}

void printLocalTime()
{
  //struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time restting ");
    esp_sleep_enable_timer_wakeup(TIME_TO_RESET * uS_TO_S_FACTOR);
    esp_deep_sleep_start();
  };
  strftime(timeStringBuff, sizeof(timeStringBuff), "%d/%m/%y", &timeinfo);
  Serial.print("Date ");
  //print like "const char*"
  Serial.println(timeStringBuff);
  strftime(timeStringBuff, sizeof(timeStringBuff), "%X", &timeinfo);
  Serial.print("Time ");
  //print like "const char*"
  Serial.println(timeStringBuff);
}

void readDHT()
{
  // Init DHT
  Serial.println("Starting readDHT loop");
  DHT dht(DHTPIN, DHTTYPE, 15);
  dht.begin();
  temp = dht.readTemperature(); //Stores temperature value
  hum = dht.readHumidity(); //Stores humidity value
  dewpoint = (dewPointFast(temp, hum)); //Stores dewpoint value
  margin = (temp - dewpoint); //Stores magin before condensation occurs
  //float millivoltage = analogRead (A0);   //use this rather than above
  //float voltage = millivoltage/1000*10;  //convert from millivolts to volts, then allow for voltage divider chain on analogue pin 680k+220k/100k
  //add a fiddle factor. The ADC reads abouut 8% high. Why? V06 allow for voltage divider.
  //voltage=voltage*1.07;
  // set the fields with the values
  // set the fields with the values
  Serial.print("Temperature=");
  Serial.println(temp);
  Serial.print("Humidity=");
  Serial.println(hum);
  Serial.print("Dewpoint=");
  Serial.println(dewpoint);
  Serial.print("Dewpoint margin=");
  Serial.println(margin);
  //Serial.print("Temperature=");
  //Serial.println(temp);
}

void writeThingspeak()
{
  ThingSpeak.setField(1, temp);
  ThingSpeak.setField(2, hum);
  ThingSpeak.setField(3, dewpoint);
  ThingSpeak.setField(4, margin);
  ThingSpeak.setField(5, ADCValue);
  //ThingSpeak.setField(6, now);
  used_space =  SPIFFS.usedBytes();
  ThingSpeak.setField(7,used_space);
  //ThingSpeak.setField(8, freeSpace);

  myStatus = String("Data Tranfer succeeded");
  // set the status
  ThingSpeak.setStatus(myStatus);
  // write to the ThingSpeak channel
  int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
  if (x == 200)
  {
    Serial.println(myStatus);
  }
  else
  {
    Serial.println("Problem updating channel. HTTP error code " + String(x));
  }
}

void WriteToFile()
{
  //let's see how the file system is doing and bang out some data
  //File System Parameters

  /*
    FSInfo fs_info;
    SPIFFS.info(fs_info);
    totalFileSpace = (fs_info.totalBytes);
    usedFileSpace = (fs_info.usedBytes);
    SPIFFS.info(fs_info);
    Serial.print("Total FS space ");
    Serial.println(totalFileSpace);
    Serial.print("Used FS space ");
    Serial.println(usedFileSpace);
    freeSpace = totalFileSpace - usedFileSpace;
    Serial.print("Free FS space ");
    Serial.println(freeSpace);//Open for appending data
  */
  File f = SPIFFS.open(filename, "a+");
  if (!f)
  {
    Serial.print("File does not exist ");
    Serial.println(filename);
  }
  else  //file does exist
  {
    Serial.print("File open for append ");
    Serial.println(filename);
    //f.print(now);
    //f.print(",");
    // Should do this in a loop really
    strftime(timeStringBuff, sizeof(timeStringBuff), "%d/%m/%y", &timeinfo);
    f.print(timeStringBuff);
    f.print(",");
    strftime(timeStringBuff, sizeof(timeStringBuff), "%X", &timeinfo);
    f.print(timeStringBuff);
    f.print(",");
    f.print(temp);
    f.print(",");
    f.print(hum);
    f.print(",");
    f.print(dewpoint);
    f.print(",");
    f.print(margin);
    //f.print(",");
    //f.print((uint32_t)now);
    f.print(",");
    f.print(ADCValue);
    f.print(",");
    f.print(SPIFFS.usedBytes());
    f.print(",");
    //f.println(SPIFFS.freeBytes());
    //f.print(",");
    f.print("\n");
    f.close();  //Close file
    Serial.print("File closed after append ");
    Serial.println(filename);
  }
  Serial.print("Used bytes after write:");
  Serial.println(SPIFFS.usedBytes());
}

void dumpFile()
//let's read our file
{
  int i;
  File g = SPIFFS.open(filename, "r");
  if (!g)
  {
    Serial.print("File does not exist ");
    Serial.println(filename);
    delay(10000);
  }
  else  //file does exist
  {
    Serial.print("File open for read ");
    Serial.println(filename);
    //delay(10000);
    for (i = 0; i < g.size(); i++) //Read upto complete file size
    {
      Serial.print((char)g.read());
      //delay(1000);
    }
    g.close();  //Close file
  }
}

void emptyFile()
{
  Serial.print ("Emptying file");
  Serial.println (filename);
  delay (2000);
  File f = SPIFFS.open(filename, "w");
  if (!f)
  {
    Serial.print("File does not exist ");
    Serial.println(filename);
  }
  else  //file does exist
  {
    Serial.print("File being emptied");
    Serial.println(filename);
    f.print("File Begin");
    f.print("\n");
    f.close();  //Close file
  }
  delay (10000);
}


// Startup menu
void StartUp(void)
{
  Serial.println("MENU");
  Serial.println("Enter character as prompted within 5 secods or logging will resume");
  Serial.println("Character is Case Sensitive");
  Serial.println("1. (C)ontinue normal logging (Default)");
  Serial.println("2. (R)eset clear data file?");
  Serial.println("3. (D)ump data file");
  Serial.println("4. (F)TP server mode");

  while (menucount > 0)
  {
    menuchar = Serial.read(); //read a character
    menuchar = menuchar - 'a' + 'A'; //force upper case
    switch (menuchar)
    {
      case 'C':
        Serial.println("Continue");
        menucount = 0; //force break from while
        break;
      case 'R':
        clearScreen;
        Serial.println("Reset");
        Serial.print("To delete file please key in this number: ");
        //output a 3 digit rand which user must key in to delete data
        deleteKey = random(100, 999);
        Serial.println (deleteKey);
        Serial.println("You have 10 seconds");
        /* This area of code is quite tricky. We are attempting to read in a string from the serial line terminated with a newline "\n".
          Serial.available should tell us how many characters, if any, are in the input buffer. Unfortunately the IDE seems (?) to send a newline
          when sending the chosen character in response to the menu. This can hang around in the serial buffer, so we need to flusg it out, otherwise
          we see a null string. It also seems like Serial.available might be non-zero after a Serial.print operation? So we flush the bufer and immediately
          start reading the inout string.

        */
        while (Serial.available() > 0 )
        {
          serialInChar = Serial.read ();
          Serial.print("Buffer flushed:");
          Serial.println(serialInChar);
          delay (1000);
        }
        readLoopCount = 10;
        while ( readLoopCount > 0 )
        {
          if (Serial.available() > 0)
          {
            Serial.println ("Data Received");
            delay (1000);
            break; //there's data, stop looping
          }
          readLoopCount--; //dec the loop count
          //Serial.print("Waiting, loop count is ");
          //Serial.println (readLoopCount);
          delay (1000);//give the punter some time
        }
        recvWithEndMarker();
        rxstring = atoi(receivedChars);
        //let user see data
        showNewData();
        if (rxstring == deleteKey)
        {
          emptyFile(); //bye bye data
          Serial.println ("String matches. File emptied");
        }
        else
        {
          Serial.print ("String ");
          Serial.print (rxstring);
          Serial.println (" does not match. Press reset to try again or allow logging to continue");
        }
        menucount = 0; //force break from while
        break;
      case 'D':
        menucount = 0; //force break from while
        Serial.println("Dump");
        dumpFile();
        break;
      case 'F':
        menucount = 0; //force break from while
        Serial.println("Start FTP server, reset to quit FTP");
        FTPFile();
        break;
      default:
        Serial.println("Invalid Character or incorrect case. ");
        break;
    }
    delay(3000);
    menucount--;
  }
}
//end of startup menu

//Clear Screen. There's no "proper" way to do this, we just send a few newlines
void clearScreen()
{
  int lines_to_flush = 20;
  while (lines_to_flush > 0)
  {
    Serial.println ("\n");
    lines_to_flush--;

  }
}

//Rotine to read newline terminated (\n) string. It will read in up to 31 cahacters and then truncate
void recvWithEndMarker()
{
  static byte ndx = 0;
  char endMarker = '\n';
  char rc;

  while (Serial.available() > 0 && newData == false)
  {
    rc = Serial.read();
    if (rc != endMarker)
    {
      receivedChars[ndx] = rc;
      Serial.print (rc);
      //delay (2000);
      ndx++;
      if (ndx >= numChars)
      {
        ndx = numChars - 1;
      }
    }
    else
    {
      Serial.println ("Newline received");
      receivedChars[ndx] = '\0'; // terminate the string
      ndx = 0;
      newData = true;
      delay (2000);
    }
  }
}

void showNewData()
{
  if (newData == true)
  {
    Serial.print("Key entered was ... ");
    Serial.println(receivedChars);
    newData = false;
  }

}

void FTPFile()
{
  Serial.println("Starting FTP server");
  ftpSrv.begin("esp8266","esp8266");    //username, password for ftp.  set ports in ESP8266FtpServer.h  (default 21, 50009 for PASV)
  while (true)                  //loop indefinitely around the FTP handler. Use power reset to break free
  {
  ftpSrv.handleFTP();        //make sure in loop you call handleFTP()!!  
  }
}
