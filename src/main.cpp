#include <HeliOS_Arduino.h>
#include <Wire.h>
#include <PxMatrix.h>
#include <FreeSans12pt7b.h>
#include <Font.h>
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
int currentHour = 0;
int currentMinute = 0;
const long utcOffsetInSeconds = 7200;

PxMATRIX display(64, 32, P_LAT, P_OE, P_A, P_B, P_C, P_D, P_E);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "192.168.155.1", utcOffsetInSeconds);

// Some standard colors
uint16_t myRED = display.color565(255, 0, 0);
uint16_t myGREEN = display.color565(0, 255, 0);
uint16_t myBLUE = display.color565(0, 0, 255);
uint16_t myWHITE = display.color565(255, 255, 255);
uint16_t myYELLOW = display.color565(255, 255, 0);
uint16_t myCYAN = display.color565(0, 255, 255);
uint16_t myMAGENTA = display.color565(255, 0, 255);
uint16_t myBLACK = display.color565(0, 0, 0);

uint16_t myCOLORS[8] = {myRED, myGREEN, myBLUE, myWHITE, myYELLOW, myCYAN, myMAGENTA, myBLACK};

void logT(const char *s)
{
  display.clearDisplay();
  display.setTextColor(myCYAN);
  display.setFont(&Lato_Hairline_9);
  display.setCursor(2, 0);
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

void taskClock(int id_)
{
  timeClient.update();
  currentHour = timeClient.getHours();
  currentMinute = timeClient.getMinutes();

  display.clearDisplay();
  display.setTextColor(display.color565(255, 100, 0));
  display.setCursor(3, 17);
  display.setFont(&FreeSans12pt7b);
  display.print(currentHour < 10 ? "0" + String(currentHour) : String(currentHour));

  if (clockColon)
  {
    display.print(":");
    clockColon = 0;
  }
  else
  {
    display.print(" ");
    clockColon = 1;
  }
  display.print(currentMinute < 10 ? "0" + String(currentMinute) : String(currentMinute));

  display.setTextColor(myWHITE);
  display.setFont(&Lato_Hairline_9);
  display.setCursor(0, 32);
  display.print("23.2$C ");
  display.setTextColor(myBLUE);
  display.print("12.8$C");
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
      mqttClient.subscribe(mqttTopicText);
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
  display.begin(16);
  display_update_enable(true);
  mqttClient.setServer(mqtt_server, mqtt_port);
  startWifi();
  xHeliOSSetup();
  int id = xTaskAdd("TASKCLOCK", &taskClock);
  xTaskWait(id);
  xTaskSetTimer(id, 1000000);
  timeClient.begin();
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