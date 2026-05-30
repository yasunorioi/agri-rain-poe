// led_state.h — single-pixel WS2812 status LED (M5 ATOM on G27).
//   blue   = boot
//   red    = no link / no DHCP lease
//   magenta= sensor not detected
//   yellow = MQTT host configured but not connected
//   green  = all good
//   white  = brief flash on each successful publish

#pragma once

#include <Arduino.h>
#include <FastLED.h>

static const int   PIN_NEOPIXEL = 27;
static const int   NEOPIXEL_NUM = 1;
static const uint8_t LED_BRIGHTNESS = 40;

extern CRGB g_led[NEOPIXEL_NUM];

enum LedState {
  LED_BOOT,
  LED_NO_LINK,
  LED_NO_SENSOR,
  LED_NO_MQTT,
  LED_OK,
  LED_PUB
};

extern LedState g_led_state;

inline void ledBegin() {
  FastLED.addLeds<WS2812, PIN_NEOPIXEL, GRB>(g_led, NEOPIXEL_NUM);
  FastLED.setBrightness(LED_BRIGHTNESS);
  g_led_state = LED_BOOT;
}

inline void ledApply() {
  switch (g_led_state) {
    case LED_BOOT:      g_led[0] = CRGB(8, 8, 8);    break;
    case LED_NO_LINK:   g_led[0] = CRGB(60, 0, 0);   break;
    case LED_NO_SENSOR: g_led[0] = CRGB(60, 0, 60);  break;
    case LED_NO_MQTT:   g_led[0] = CRGB(60, 40, 0);  break;
    case LED_OK:        g_led[0] = CRGB(0, 30, 0);   break;
    case LED_PUB:       g_led[0] = CRGB(60, 60, 60); break;
  }
  FastLED.show();
}

inline void ledFlashPublish() {
  LedState prev = g_led_state;
  g_led_state = LED_PUB;
  ledApply();
  delay(40);
  g_led_state = prev;
  ledApply();
}
