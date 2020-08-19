#include <HeliOS_Arduino.h>
#include <Wire.h>
//#include <ErriezBH1750.h>
#include <PxMatrix.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/CustomFont.h>
#include <Fonts/TomThumb.h>
#include <Ticker.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Secrets.h>

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
uint8_t display_draw_time = 30; //30-70 is usually fine
int clockColon = 0;
bool forward = true;
int currentHour = 0;
int currentMinute = 0;
// 0=off, 1=heat, 2=cool
int heatingMode = 0;
float tempIn = 0;
float tempOut = 0;
const long utcOffsetInSeconds = 7200;

PxMATRIX display(64, 32, P_LAT, P_OE, P_A, P_B, P_C, P_D, P_E);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, time_server, utcOffsetInSeconds);
//BH1750 lightSensor(LOW);

uint16_t colOrange = display.color565(255, 100, 0);
uint16_t colLightOrange = display.color565(255, 204, 153);
uint16_t colBlack = display.color565(0, 0, 0);
uint16_t colWhite = display.color565(255, 255, 255);
uint16_t colLightBlue = display.color565(30, 144, 255);

void logT(const char *s)
{
  display.clearDisplay();
  display.setTextColor(colLightBlue);
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

void taskCol(int id)
{
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

  if (clockColon > 99)
  {
    forward = false;
  }

  if (clockColon < 1)
  {
    forward = true;
  }

  display.setTextColor(display.color565(clockColon * 2.5, clockColon, 0));
  display.print(":");
}

void taskClock(int id_)
{
  int yPosMainText = 16;
  timeClient.update();
  currentHour = timeClient.getHours();
  currentMinute = timeClient.getMinutes();

  display.clearDisplay();
  display.setTextColor(colOrange);
  display.setCursor(3, yPosMainText);
  display.setFont(&FreeSans12pt7b);
  display.print(currentHour < 10 ? "0" + String(currentHour) : String(currentHour));

  display.setCursor(36, yPosMainText);
  display.setTextColor(colOrange);
  display.print(currentMinute < 10 ? "0" + String(currentMinute) : String(currentMinute));

  if (heatingMode > 0)
  {
    uint16_t color;
    if (heatingMode == 1)
    {
      color = colOrange;
      display.drawFastHLine(3, 19, 2, color);
      display.drawFastHLine(2, 20, 4, color);
      display.drawFastHLine(1, 21, 6, color);
      display.drawFastHLine(0, 22, 8, color);
    }
    else if (heatingMode == 2)
    {
      color = colLightBlue;
      display.drawFastHLine(0, 19, 8, color);
      display.drawFastHLine(1, 20, 6, color);
      display.drawFastHLine(2, 21, 4, color);
      display.drawFastHLine(3, 22, 2, color);
    }
    else
    {
      color = colBlack;
      display.drawFastHLine(0, 19, 8, color);
      display.drawFastHLine(0, 20, 8, color);
      display.drawFastHLine(0, 21, 8, color);
      display.drawFastHLine(0, 22, 8, color);
    }
  }

  display.setTextColor(colWhite);
  display.setFont(&Lato_Hairline_9);
  display.setCursor(0, 32);
  display.print(tempIn, 1);
  // "$" is a degree char in my font
  display.print("$C ");
  if (tempOut > 23)
  {
    display.setTextColor(colOrange);
  }
  else if (tempOut < 2)
  {
    display.setTextColor(colLightBlue);
  }
  else
  {
    display.setTextColor(colWhite);
  }
  display.print(tempOut, 1);
  display.print("$C");
}

void callback(char *topic, byte *payload, unsigned int length)
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
}

void setup()
{
  //pinMode(1, FUNCTION_3);
  //pinMode(3, FUNCTION_3);

  display.begin(16);
  display_update_enable(true);
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(callback);
  startWifi();
  xHeliOSSetup();
  int id = xTaskAdd("TASKCLOCK", &taskClock);
  xTaskWait(id);
  // two seconds for the time
  xTaskSetTimer(id, 2 * 1000 * 1000);

  id = xTaskAdd("TASKCOL", &taskCol);
  xTaskWait(id);
  // 10 milliseconds for the colon
  xTaskSetTimer(id, 10 * 1000);

  timeClient.begin();

  //Wire.begin(3, 1);
  //lightSensor.begin(ModeContinuous, ResolutionMid);
  //lightSensor.startConversion();
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

  //uint16_t lux;
  // Read light without wait
  //lux = lightSensor.read();
  //itoa(lux, tempIn, 10);
}