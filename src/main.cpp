#include <HeliOS_Arduino.h>
#include <Wire.h>
#include <Adafruit_I2CDevice.h>
#include <AS_BH1750.h>
#include <PxMatrix.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/CustomFont.h>
#include <Fonts/TomThumb.h>
#include <Ticker.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <Secrets.h>
#include <time.h>

Ticker display_ticker;

#define P_LAT 16
#define P_A 5
#define P_B 4
#define P_C 15
#define P_D 12
#define P_E 0
#define P_OE 2
// This defines the 'on' time of the display is us. The larger this number,
// the brighter the display. If too large the ESP will crash
uint8_t display_draw_time = 80; //30-70 is usually fine

struct tm lt;
int currentHour = 0;
int currentMinute = 0;

int minimalBright = 4;

// 0=off, 1=heat, 2=cool
int heatingMode = 0;
float tempIn = 0;
float tempOut = 0;

int clockColon = 1;
bool forward = true;

PxMATRIX display(64, 32, P_LAT, P_OE, P_A, P_B, P_C, P_D, P_E);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

bool BH1750Check = false;
AS_BH1750 lightMeter;
float currentLight = 0;
bool lightMeterDebug = false;
char onScreenDebugBuffer[20];
int brightness = 0;


int colClockNightGreen = 30;
uint16_t colClockNight = display.color565(255, colClockNightGreen, 0);
int colClockGreen = 100;
uint16_t colClock = display.color565(255, colClockGreen, 0);

uint16_t colBlack = display.color565(0, 0, 0);

uint16_t colInsideTemp = display.color565(255, 255, 255);
uint16_t colInsideTempNight = display.color565(255, 116, 26);

uint16_t colWarm = display.color565(255, 0, 0);
uint16_t colCold = display.color565(30, 144, 255);
uint16_t colColdNight = display.color565(138, 138, 193);


void logT(const char *s)
{
  display.clearDisplay();
  display.setTextColor(colCold);
  display.setFont(&TomThumb);
  display.setCursor(0, 10);
  display.print(s);
}

void display_updater()
{
  display.display(display_draw_time);
}

void display_update_enable(bool is_enable)
{
  if (is_enable)
    display_ticker.attach(0.004, display_updater);
  else
    display_ticker.detach();
}

void taskTimeSync(xTaskId id)
{
  configTime(MY_TZ, time_server);
  mqttClient.publish("home/sz/display/time", "sync");
}

void taskColonBlink(xTaskId id)
{
  int steps = 50;
  display.setCursor(29, 14);
  display.setFont(&FreeSans12pt7b);

  if (forward)
  {
    clockColon++;
  }
  else
  {
    clockColon--;
  }

  if (clockColon > steps)
  {
    forward = false;
  }

  if (clockColon < 2)
  {
    forward = true;
  }

  if (currentLight == 0)
  {
    display.setTextColor(display.color565(255 / steps * clockColon, colClockNightGreen / steps * clockColon, 0));
  }
  else
  {
    display.setTextColor(display.color565(255 / steps * clockColon, colClockGreen / steps * clockColon, 0));
  }

  display.print(":");
}

void taskSensor(xTaskId id)
{
  if (BH1750Check)
  {
    currentLight = lightMeter.readLightLevel();
    char buff[10];
    dtostrf(currentLight, 4, 2, buff);
    mqttClient.publish(topSensor, buff);

    int brightness = (int)currentLight;
    brightness = minimalBright + currentLight * 5;
    if (brightness > 255)
    {
      brightness = 255;
    }

    display.setBrightness(brightness);

    memset(onScreenDebugBuffer, 0, sizeof(onScreenDebugBuffer));
    strcat(onScreenDebugBuffer, "Sen:");
    strcat(onScreenDebugBuffer, buff);
    strcat(onScreenDebugBuffer, "lx Bri:");
    char cstr[3];
    itoa(brightness, cstr, 10);
    strcat(onScreenDebugBuffer, cstr);
  }
}

void taskClock(xTaskId id_)
{
  int yPosMainText = 16;
  time_t now = time(&now);
  localtime_r(&now, &lt);

  currentHour = lt.tm_hour;
  currentMinute = lt.tm_min;

  uint16_t clockColor = colClock;
  uint16_t insideTempColor = colInsideTemp;

  if (currentLight == 0)
  {
    clockColor = colClockNight;
    insideTempColor = colInsideTempNight;
  }

  display.clearDisplay();
  display.setTextColor(clockColor);
  display.setCursor(3, yPosMainText);
  display.setFont(&FreeSans12pt7b);
  display.print(currentHour < 10 ? "0" + String(currentHour) : String(currentHour));

  display.setCursor(36, yPosMainText);
  display.setTextColor(clockColor);
  display.print(currentMinute < 10 ? "0" + String(currentMinute) : String(currentMinute));

  if (heatingMode > 0)
  {
    if (heatingMode == 1)
    {
      display.drawFastHLine(3, 19, 2, clockColor);
      display.drawFastHLine(2, 20, 4, clockColor);
      display.drawFastHLine(1, 21, 6, clockColor);
      display.drawFastHLine(0, 22, 8, clockColor);
    }
    else if (heatingMode == 2)
    {
      uint16_t color = currentLight == 0 ? colColdNight : colCold;
      display.drawFastHLine(0, 19, 8, color);
      display.drawFastHLine(1, 20, 6, color);
      display.drawFastHLine(2, 21, 4, color);
      display.drawFastHLine(3, 22, 2, color);
    }
    else
    {
      uint16_t color = colBlack;
      display.drawFastHLine(0, 19, 8, color);
      display.drawFastHLine(0, 20, 8, color);
      display.drawFastHLine(0, 21, 8, color);
      display.drawFastHLine(0, 22, 8, color);
    }
  }

  display.setTextColor(insideTempColor);
  display.setFont(&Lato_Hairline_9);
  display.setCursor(0, 32);
  display.print(tempIn, 1);
  // "$" is a degree char in my font
  display.print("$C ");
  if (tempOut > 23)
  {
    display.setTextColor(colWarm);
  }
  else if (tempOut < 2)
  {
    display.setTextColor(currentLight == 0 ? colColdNight : colCold);
  }
  else
  {
    display.setTextColor(insideTempColor);
  }
  display.print(tempOut, 1);
  display.print("$C");

  if (lightMeterDebug)
  {
    display.setTextColor(colClockNight);
    display.setFont(&TomThumb);
    display.setCursor(0, 23);
    display.print(onScreenDebugBuffer);
  }
}

void mqttMessageReceived(char *topic, byte *payload, unsigned int length)
{
  if (strcmp(topic, topTempIn) == 0)
  {
    payload[length] = '\0';
    tempIn = atof((char *)payload);
  }

  if (strcmp(topic, topTempOut) == 0)
  {
    payload[length] = '\0';
    tempOut = atof((char *)payload);
  }

  // sets brightness of the screen, will be overwritten almost immediately by the sensor values
  if (strcmp(topic, topBright) == 0)
  {
    payload[length] = '\0';
    char *cstring = (char *)payload;
    int i = atoi(cstring);
    display.setBrightness(i);
  }
  
  // sets minimal brightness of the screen (used if sensor says zero light)
  if (strcmp(topic, topMinimalBright) == 0)
  {
    payload[length] = '\0';
    char *cstring = (char *)payload;
    minimalBright = atoi(cstring);
  }

  // enable/disable light sensor and brightness information on the screen
  if (strcmp(topic, topLightMeterDeb) == 0)
  {
    payload[length] = '\0';
    char *cstring = (char *)payload;
    if (strcmp(cstring, "1") == 0)
    {
      lightMeterDebug = true;
    } else {
      lightMeterDebug = false;
    }
  }

  // flag for cooling indicator
  if (strcmp(topic, topCool) == 0)
  {
    payload[length] = '\0';
    char *cstring = (char *)payload;
    if (strcmp(cstring, "On") == 0)
    {
      heatingMode = 2;
    }
    else
    {
      heatingMode = 0;
    }
  }

  // flag for heating indicator
  if (strcmp(topic, topHeat) == 0)
  {
    payload[length] = '\0';
    char *cstring = (char *)payload;
    if (strcmp(cstring, "1") == 0)
    {
      heatingMode = 1;
    }
    else
    {
      heatingMode = 0;
    }
  }
}

void startMqtt()
{
  while (!mqttClient.connected())
  {
    String clientId = "iotdisplay-";
    clientId += String(random(0xffff), HEX);
    logT("MQTT connecting ...");
    delay(1000);
    if (mqttClient.connect(clientId.c_str()))
    {
      logT("MQTT connected");
      delay(1000);

      mqttClient.subscribe(topTempOut);
      mqttClient.subscribe(topTempIn);
      mqttClient.subscribe(topHeat);
      mqttClient.subscribe(topCool);
      mqttClient.subscribe(topBright);
      mqttClient.subscribe(topMinimalBright);
      mqttClient.subscribe(topLightMeterDeb);
      logT("MQTT subscribed");
    }
    else
    {
      logT("MQTT failed, retrying...");
      delay(5000);
    }
  }
}

void startWifi(void)
{
  WiFi.begin(wifiAP, wifiPassword);
  logT("Wifi connecting ...");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
  }
  logT("Wifi connected");
  WiFi.softAPdisconnect(true);
}

void setup()
{
  pinMode(1, FUNCTION_3);
  pinMode(3, FUNCTION_3);

  display.begin(16);
  display_update_enable(true);
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttMessageReceived);
  startWifi();
  xHeliOSSetup();
  taskTimeSync(1);

  Wire.begin(1, 3); //SDA(tx), SCL(rx)
  BH1750Check = lightMeter.begin(RESOLUTION_AUTO_HIGH, true);
  if (BH1750Check)
  {
    logT("Sensor connected.");
    delay(1000);
  }
  else
  {
    logT("Sensor failed.");
    delay(1000);
  }

  display.setBrightness(255);

  // two seconds for the time
  int id = xTaskAdd("TASKCLOCK", &taskClock);
  xTaskWait(id);
  xTaskSetTimer(id, 2 * 1000 * 1000);

  // 20 milliseconds for the colon
  id = xTaskAdd("TASKCOL", &taskColonBlink);
  xTaskWait(id);
  xTaskSetTimer(id, 20 * 1000);

  // five seconds for the sensor
  id = xTaskAdd("TASKSENSOR", &taskSensor);
  xTaskWait(id);
  xTaskSetTimer(id, 5 * 1000 * 1000);

  // four hours for the timesync
  id = xTaskAdd("TASKTIMESYNC", &taskTimeSync);
  xTaskWait(id);
  time_t fourHours = 60 * 60 * 1000 * 1000;
  xTaskSetTimer(id, fourHours);
}

uint8_t icon_index = 0;
void loop()
{
  xHeliOSLoop();
  if (!mqttClient.connected())
  {
    startMqtt();
  }
  mqttClient.loop();
}