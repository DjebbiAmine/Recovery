
#define FASTLED_INTERRUPT_RETRY_COUNT 0 
#define FASTLED_ALLOW_INTERRUPTS 0
#include "constants.h"
#include <ESP.h>

#if defined(ESP8266)
    #include <ESP8266WiFi.h>
#elif defined(ESP32)
    #include <WiFi.h>
#endif
#include <FastLED.h>
#include <WiFiUDP.h>
#include "reactive_common.h"

#define LED_PIN 2
#define NUM_LEDS 144
#define STATUS_CONNECTING CRGB::Orange
#define MIC_LOW 0
#define MIC_HIGH 644

#define SAMPLE_SIZE 20
#define LONG_TERM_SAMPLES 250
#define BUFFER_DEVIATION 400
#define BUFFER_SIZE 3

#define LAMP_ID 1
WiFiUDP UDP;

CRGB leds[NUM_LEDS];

struct averageCounter *samples;
struct averageCounter *longTermSamples;
struct averageCounter* sanityBuffer;

float globalHue;
float globalBrightness = 255;
int hueOffset = 120;
float fadeScale = 1.3;
float hueIncrement = 0.7;

struct led_command {
  uint8_t opmode;
  uint32_t data;
};
bool connected = false;
unsigned long lastReceived = 0;
uint32_t lastSubscribeTime = 0;
unsigned long lastHeartBeatSent;
const int heartBeatInterval = 100;
bool fade = false;

struct led_command cmd;

void setup()
{
  globalHue = 0;
  samples = new averageCounter(SAMPLE_SIZE);
  longTermSamples = new averageCounter(LONG_TERM_SAMPLES);
  sanityBuffer    = new averageCounter(BUFFER_SIZE);
  
  while(sanityBuffer->setSample(250) == true) {}
  while (longTermSamples->setSample(200) == true) {}

  FastLED.addLeds<NEOPIXEL, LED_PIN>(leds, NUM_LEDS);

  Serial.begin(115200); // Start the Serial communication to send messages to the computer
  delay(10);
  Serial.println('\n');

  connectToWiFi(NETWORK_SSID, NETWORK_PASSWORD);
}


void loop()
{


  if (!connected) {
    return;
  }

  checkForNetworkFailure();

  if (lastSubscribeTime == 0 || ms - lastSubscribeTime > SUBSCRIBE_INTERVAL_MS) {
    for (uint8_t i = 0; i < STAGE_PROP_COUNT; i++) {
      subscribe(STAGE_PROP_CODES[i], i);
    }

    lastSubscribeTime = ms;
  }
  
  int packetSize = UDP.parsePacket();
  if (packetSize)
  {
    UDP.read((char *)&cmd, sizeof(struct led_command));
    lastReceived = millis();
  }

    
  int opMode = cmd.opmode;
  int analogRaw = cmd.data;

  switch (opMode) {
    case 1:
      fade = false;
      soundReactive(analogRaw);
      break;

    case 2:
      fade = false;
      allWhite();
      break;

    case 3:
      chillFade();
      break;
  }
  
}

void allWhite() {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB(255, 255, 235);
  }
  delay(5);
  FastLED.show();
}

void chillFade() {
  static int fadeVal = 0;
  static int counter = 0;
  static int from[3] = {0, 234, 255};
  static int to[3]   = {255, 0, 214};
  static int i, j;
  static double dsteps = 500.0;
  static double s1, s2, s3, tmp1, tmp2, tmp3;
  static bool reverse = false;
  if (fade == false) {
    for (int i = 0; i < NUM_LEDS; i++) {
      leds[i] = CRGB(from[0], from[1], from[2]);
    }
    s1 = double((to[0] - from[0])) / dsteps; 
    s2 = double((to[1] - from[1])) / dsteps; 
    s3 = double((to[2] - from[2])) / dsteps; 
    tmp1 = from[0], tmp2 = from[1], tmp3 = from[2];
    fade = true;
  }

  if (!reverse) 
  {
    tmp1 += s1;
    tmp2 += s2; 
    tmp3 += s3; 
  }
  else 
  {
    tmp1 -= s1;
    tmp2 -= s2; 
    tmp3 -= s3; 
  }

  for (j = 0; j < NUM_LEDS; j++)
    leds[j] = CRGB((int)round(tmp1), (int)round(tmp2), (int)round(tmp3)); 
  FastLED.show(); 
  delay(5);

  counter++;
  if (counter == (int)dsteps) {
    reverse = !reverse;
    tmp1 = to[0], tmp2 = to[1], tmp3 = to[2];
    counter = 0;
  }
}

void soundReactive(int analogRaw) {

 int sanityValue = sanityBuffer->computeAverage();
 if (!(abs(analogRaw - sanityValue) > BUFFER_DEVIATION)) {
    sanityBuffer->setSample(analogRaw);
 }
  analogRaw = fscale(MIC_LOW, MIC_HIGH, MIC_LOW, MIC_HIGH, analogRaw, 0.4);

  if (samples->setSample(analogRaw))
    return;
    
  uint16_t longTermAverage = longTermSamples->computeAverage();
  uint16_t useVal = samples->computeAverage();
  longTermSamples->setSample(useVal);

  int diff = (useVal - longTermAverage);
  if (diff > 5)
  {
    if (globalHue < 235)
    {
      globalHue += hueIncrement;
    }
  }
  else if (diff < -5)
  {
    if (globalHue > 2)
    {
      globalHue -= hueIncrement;
    }
  }


  int curshow = fscale(MIC_LOW, MIC_HIGH, 0.0, (float)NUM_LEDS, (float)useVal, 0);
  //int curshow = map(useVal, MIC_LOW, MIC_HIGH, 0, NUM_LEDS)

  for (int i = 0; i < NUM_LEDS; i++)
  {
    if (i < curshow)
    {
      leds[i] = CHSV(globalHue + hueOffset + (i * 2), 255, 255);
    }
    else
    {
      leds[i] = CRGB(leds[i].r / fadeScale, leds[i].g / fadeScale, leds[i].b / fadeScale);
    }
    
  }
  delay(5);
  FastLED.show(); 
}

void showStatus(CRGB statusColor) {

  for (int i = 0; i < NUM_LEDS; i++)
  {
    leds[i] = CHSV(0, 0, 0);
  }
  
  leds[0] = CRGB(0, 255, 0);
  FastLED.show();
}
void checkForNetworkFailure() {
  if (millis() - lastReceived > MAX_CONNECTION_LOSS_MS) {
    Serial.println("No packets received in " + String(MAX_CONNECTION_LOSS_MS) + "ms, restarting.");
    ESP.restart();
    while (true);
  }
}

void subscribe(String stagePropCode, uint8_t clientId) {
  uint32_t ms = millis();

  if (!udp.beginPacket(SERVER_IP_ADDRESS, SERVER_UDP_PORT)) {
    Serial.println("beginPacket() failed.");
    return;
  }
  udp.printf(String(SUBSCRIBE_COMMAND + stagePropCode + ":" + clientId).c_str());
  udp.endPacket();
}

void connectToWiFi(const String ssid, const String pwd) {
  Serial.println("Connecting to network: " + ssid + "...");
  showStatus(STATUS_CONNECTING);

  WiFi.disconnect(true);
  WiFi.onEvent(onWiFiEvent);

  #ifdef STATIC_IP_ADDRESS
  if (WiFi.config(IPAddress(STATIC_IP_ADDRESS), IPAddress(ROUTER_IP_ADDRESS), IPAddress(SUBNET_MASK), IPAddress(DNS_PRIMARY), IPAddress(DNS_SECONDARY))) {
    Serial.println("Using static IP address.");
  } else {
    Serial.println("Failed to configure static IP address.");
  }
  #else
  Serial.println("Using dynamic IP address.");
  #endif

  WiFi.begin(ssid.c_str(), pwd.c_str());
  Serial.println("Waiting for network connection...");
}
void onWiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case EVENT_CONNECTED:
      Serial.println("Connected to network.");
      break;
    case EVENT_GOT_IP:
      if (WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
        Serial.println("Got IP Address of 0.0.0.0, waiting for proper IP address...");
      } else {
        
          Serial.print("Connected with IP address ");
          Serial.println(WiFi.localIP());
          udp.begin(SERVER_UDP_PORT);
          connected = true;
          lastReceived = millis();
        
      }
      break;
    case EVENT_DISCONNECTED:
      Serial.println("Lost network connection, attempting to reconnect...");
      connected = false;
      connectToWiFi(NETWORK_SSID, NETWORK_PASSWORD);
      break;
  }
}

float fscale(float originalMin, float originalMax, float newBegin, float newEnd, float inputValue, float curve)
{

  float OriginalRange = 0;
  float NewRange = 0;
  float zeroRefCurVal = 0;
  float normalizedCurVal = 0;
  float rangedValue = 0;
  boolean invFlag = 0;

  // condition curve parameter
  // limit range

  if (curve > 10)
    curve = 10;
  if (curve < -10)
    curve = -10;

  curve = (curve * -.1);  // - invert and scale - this seems more intuitive - postive numbers give more weight to high end on output
  curve = pow(10, curve); // convert linear scale into lograthimic exponent for other pow function

  // Check for out of range inputValues
  if (inputValue < originalMin)
  {
    inputValue = originalMin;
  }
  if (inputValue > originalMax)
  {
    inputValue = originalMax;
  }

  // Zero Refference the values
  OriginalRange = originalMax - originalMin;

  if (newEnd > newBegin)
  {
    NewRange = newEnd - newBegin;
  }
  else
  {
    NewRange = newBegin - newEnd;
    invFlag = 1;
  }

  zeroRefCurVal = inputValue - originalMin;
  normalizedCurVal = zeroRefCurVal / OriginalRange; // normalize to 0 - 1 float

  // Check for originalMin > originalMax  - the math for all other cases i.e. negative numbers seems to work out fine
  if (originalMin > originalMax)
  {
    return 0;
  }

  if (invFlag == 0)
  {
    rangedValue = (pow(normalizedCurVal, curve) * NewRange) + newBegin;
  }
  else // invert the ranges
  {
    rangedValue = newBegin - (pow(normalizedCurVal, curve) * NewRange);
  }

  return rangedValue;
}
