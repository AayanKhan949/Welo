
/*
  Welo.ino
  ESP32 Weather Pet
  Hardware:
  - ESP32-WROOM-32
  - SH1106 128x64 OLED
  - TTP223 Touch Sensor on GPIO4

  Libraries:
  U8g2
  WiFiManager
  ArduinoJson
*/

#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <Preferences.h>
#include <time.h>

#define TOUCH_PIN 4

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0);

Preferences prefs;

String city = "";
String country = "";

float temperature = 0;
int humidity = 0;
float windSpeed = 0;
int weatherCode = 0;

float forecast1 = 0;
float forecast2 = 0;
float forecast3 = 0;

float latitude = 0;
float longitude = 0;

int screen = 0;
unsigned long lastTouch = 0;
unsigned long lastWeatherUpdate = 0;

const unsigned long WEATHER_INTERVAL = 1800000;
const unsigned long SLEEP_TIMEOUT = 30000;

String expression = "^ ^";

bool geocodeLocation() {
  HTTPClient http;

  String url =
    "https://geocoding-api.open-meteo.com/v1/search?name=" +
    city + "&count=1&language=en&format=json";

  http.begin(url);
  int httpCode = http.GET();

  if (httpCode != 200) {
    http.end();
    return false;
  }

  DynamicJsonDocument doc(8192);
  deserializeJson(doc, http.getString());

  if (!doc["results"].is<JsonArray>() ||
      doc["results"].size() == 0) {
    http.end();
    return false;
  }

  latitude = doc["results"][0]["latitude"];
  longitude = doc["results"][0]["longitude"];

  http.end();
  return true;
}

void chooseExpression() {
  if (weatherCode == 0)
    expression = "^ ^";
  else if (weatherCode < 50)
    expression = "- -";
  else if (weatherCode < 70)
    expression = "T T";
  else if (weatherCode < 90)
    expression = "O O";
  else
    expression = "x x";
}

bool updateWeather() {
  HTTPClient http;

  String url =
    "https://api.open-meteo.com/v1/forecast?latitude=" +
    String(latitude, 6) +
    "&longitude=" +
    String(longitude, 6) +
    "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m"
    "&daily=temperature_2m_max"
    "&timezone=auto";

  http.begin(url);

  int code = http.GET();

  if (code != 200) {
    http.end();
    return false;
  }

  DynamicJsonDocument doc(16384);
  deserializeJson(doc, http.getString());

  temperature = doc["current"]["temperature_2m"];
  humidity = doc["current"]["relative_humidity_2m"];
  weatherCode = doc["current"]["weather_code"];
  windSpeed = doc["current"]["wind_speed_10m"];

  forecast1 = doc["daily"]["temperature_2m_max"][0];
  forecast2 = doc["daily"]["temperature_2m_max"][1];
  forecast3 = doc["daily"]["temperature_2m_max"][2];

  chooseExpression();

  http.end();
  return true;
}

String getTimeString() {
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo))
    return "--:--";

  char buf[10];
  strftime(buf, sizeof(buf), "%H:%M", &timeinfo);

  return String(buf);
}

void drawFace() {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(2, 10, String((int)temperature).c_str());
  u8g2.drawUTF8(18,10,"°C");

  String t = getTimeString();
  u8g2.drawStr(90,10,t.c_str());

  u8g2.setFont(u8g2_font_logisoso20_tf);
  u8g2.drawStr(28,38,expression.c_str());

  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(30,60,city.c_str());

  u8g2.sendBuffer();
}

void drawForecast() {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_6x12_tf);

  u8g2.drawStr(0,12,"Forecast");

  u8g2.drawStr(0,28,("Today: " + String(forecast1) + "C").c_str());
  u8g2.drawStr(0,44,("Day+1: " + String(forecast2) + "C").c_str());
  u8g2.drawStr(0,60,("Day+2: " + String(forecast3) + "C").c_str());

  u8g2.sendBuffer();
}

void drawDetails() {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_6x12_tf);

  u8g2.drawStr(0,12,"Weather Details");

  u8g2.drawStr(0,28,("Temp: " + String(temperature)).c_str());
  u8g2.drawStr(0,40,("Hum : " + String(humidity) + "%").c_str());
  u8g2.drawStr(0,52,("Wind: " + String(windSpeed)).c_str());

  u8g2.sendBuffer();
}

void drawClock() {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_logisoso24_tf);
  String t = getTimeString();
  u8g2.drawStr(10,40,t.c_str());

  u8g2.sendBuffer();
}

void drawSleep() {
  static int frame = 0;

  const char* z[] = {"z","zz","zzz"};

  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_logisoso18_tf);
  u8g2.drawStr(20,35,"- -");

  u8g2.setFont(u8g2_font_6x12_tf);
  u8g2.drawStr(60,50,z[frame]);

  u8g2.sendBuffer();

  frame++;
  if(frame > 2) frame = 0;
}

void touchAction() {
  static bool prev = false;

  bool state = digitalRead(TOUCH_PIN);

  if(state && !prev) {
    screen++;
    if(screen > 3) screen = 0;

    lastTouch = millis();
  }

  prev = state;
}

void setup() {
  Serial.begin(115200);

  pinMode(TOUCH_PIN, INPUT);

  u8g2.begin();

  prefs.begin("welo", false);

  WiFiManager wm;

  char cityBuf[40] = "";
  char countryBuf[40] = "";

  WiFiManagerParameter cityParam(
    "city","City",cityBuf,40);

  WiFiManagerParameter countryParam(
    "country","Country",countryBuf,40);

  wm.addParameter(&cityParam);
  wm.addParameter(&countryParam);

  wm.autoConnect("Welo_Setup");

  city = cityParam.getValue();
  country = countryParam.getValue();

  if(city.length() == 0)
    city = prefs.getString("city","Jangipur");

  if(country.length() == 0)
    country = prefs.getString("country","India");

  prefs.putString("city", city);
  prefs.putString("country", country);

  geocodeLocation();

  configTime(19800, 0, "pool.ntp.org");

  updateWeather();

  lastTouch = millis();
  lastWeatherUpdate = millis();
}

void loop() {

  touchAction();

  if(millis() - lastWeatherUpdate > WEATHER_INTERVAL) {
    updateWeather();
    lastWeatherUpdate = millis();
  }

  if(millis() - lastTouch > SLEEP_TIMEOUT) {
    drawSleep();
    delay(500);
    return;
  }

  switch(screen) {

    case 0:
      drawFace();
      break;

    case 1:
      drawForecast();
      break;

    case 2:
      drawClock();
      break;

    case 3:
      drawDetails();
      break;
  }

  delay(100);
}
