//*******************************************************
// mLRS WiFi Bridge
// Copyright (c) www.olliw.eu, OlliW, OlliW42
// License: GPL v3
// https://www.gnu.org/licenses/gpl-3.0.de.html
//*******************************************************
// Basic but effective & reliable transparent WiFi<->serial bridge
//*******************************************************

/*
------------------------------
ESP32-PICO-KIT
------------------------------
board: ESP32-PICO-D4
IO3/IO1: U0RXD/U0TXD, connected via usb-ttl adapter to USB port, is Serial, spits out lots of preamble at power up
IO9/IO10: U1RXD/U1TXD, is Serial1
IO16/IO17: U2RXD/U2TXD, uses IO16/IO17 for internal flash, hence not available as serial

------------------------------
TTGO-MICRO32
------------------------------
board: ESP32-PICO-D4
IO3/IO1: U0RXD/U0TXD, connected via usb-ttl adapter to USB port, is Serial, spits out lots of preamble at power up
IO9/IO10: U1RXD/U1TXD, is Serial1
no IO16/IO17 pads
use only U0

------------------------------
Adafruit QT Py S2
------------------------------
board: Adafruit QT Py ESP32-S2
https://learn.adafruit.com/adafruit-qt-py-esp32-s2/arduino-ide-setup
use only Serial1, Serial is USB port and can be used for debug
RESET and BOOT available on solder pads, BOOT is GPIO0, not very connvenient

------------------------------
M5Stack M5Stamp C3 Mate
------------------------------
board: ESP32C3 Dev Module
IO20/IO21:: U0RXD/U0TXD, connected via usb-ttl adapter to USB port, available on pads, is Serial, spits out lots of preamble at power up
IO18/IO19: U1RXD/U1TXD, is Serial1
UARTs can be mapped to any pins, according to data sheet
ATTENTION: when the 5V pin is used, one MUST not also use the USB port, since they are connected internally!!
*/

/*
ESP32:

shortening GPIO15 to GND suppresses the bootloader preamble on Serial port
GPIO15 = RTC_GPIO13

*/


//-------------------------------------------------------
// board details
//-------------------------------------------------------

//-- ESP32-PICO-KIT
#if defined MODULE_ESP32_PICO_KIT // ARDUINO_ESP32_PICO, ARDUINO_BOARD = ESP32_PICO
    #ifndef ARDUINO_ESP32_PICO // ARDUINO_BOARD != ESP32_PICO
	      #error Select board ARDUINO_ESP32_PICO!
    #endif

    #undef USE_SERIAL_DBG1
    #define USE_SERIAL1_DBG

    #ifndef LED_IO
        #define LED_IO  13
    #endif    
    #define USE_LED


//-- TTGO-MICRO32
#elif defined MODULE_TTGO_MICRO32 // ARDUINO_ESP32_PICO, ARDUINO_BOARD = ESP32_PICO
    #ifndef ARDUINO_ESP32_PICO // ARDUINO_BOARD != ESP32_PICO
	      #error Select board ARDUINO_ESP32_PICO!
    #endif

    #undef USE_SERIAL_DBG1
    #undef USE_SERIAL1_DBG

    #ifndef LED_IO
        #define LED_IO  13
    #endif    
    #define USE_LED


//-- Adafruit QT Py S2
#elif defined MODULE_ADAFRUIT_QT_PY_ESP32_S2 // ARDUINO_ADAFRUIT_QTPY_ESP32S2, ARDUINO_BOARD == ADAFRUIT_QTPY_ESP32S2
    #ifndef ARDUINO_ADAFRUIT_QTPY_ESP32S2 // ARDUINO_BOARD != ADAFRUIT_QTPY_ESP32S2
	      #error Select board ADAFRUIT_QTPY_ESP32S2!
    #endif		

    #undef USE_SERIAL_DBG1
    #define USE_SERIAL1_DBG
    
    #undef LED_IO
    #define USE_LED
    #include <Adafruit_NeoPixel.h>
    #define NUMPIXELS  1
    Adafruit_NeoPixel pixels(NUMPIXELS, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);

    void led_init(void) 
    {
        #if defined(NEOPIXEL_POWER)
        pinMode(NEOPIXEL_POWER, OUTPUT);
        digitalWrite(NEOPIXEL_POWER, HIGH);
        #endif

        pixels.begin();
        pixels.setBrightness(20); // not so bright
    }

    void led_on(void) 
    {
        pixels.fill(0xFF0000); // red
        pixels.show();
    }

    void led_off(void) 
    {
        pixels.fill(0x000000); // off
        pixels.show();
    }


//-- M5Stack M5Stamp C3 Mate
#elif defined MODULE_M5STAMP_C3_MATE // ARDUINO_ESP32C3_DEV, ARDUINO_BOARD == ESP32C3_DEV
    #ifndef ARDUINO_ESP32C3_DEV // ARDUINO_BOARD != ESP32C3_DEV
	      #error Select board ESP32C3_DEV!
    #endif		

    #undef USE_SERIAL_DBG1
    #define USE_SERIAL1_DBG

    #undef LED_IO
    #define USE_LED
    #include <Adafruit_NeoPixel.h>
    #define NUMPIXELS  1
    Adafruit_NeoPixel pixels(NUMPIXELS, 2, NEO_GRB + NEO_KHZ800);

    void led_init(void) 
    {
        pixels.begin();
        pixels.setBrightness(20); // not so bright
    }

    void led_on(void) 
    {
        pixels.fill(0xFF0000); // red
        pixels.show();
    }

    void led_off(void) 
    {
        pixels.fill(0x000000); // off
        pixels.show();
    }


//-- Generic
#else
    #ifdef LED_IO  
        #define USE_LED
    #endif
#endif


//-------------------------------------------------------
// internals
//-------------------------------------------------------

#if defined USE_SERIAL_DBG1
    #define SERIAL Serial
    #define DBG Serial1
    #define DBG_PRINT(x) Serial1.print(x)
    #define DBG_PRINTLN(x) Serial1.println(x)

#elif defined USE_SERIAL1_DBG
    #define SERIAL Serial1
//    #define SERIAL_RXD  9 // = RX1
//    #define SERIAL_TXD  10 // = TX1
    #define DBG Serial
    #define DBG_PRINT(x) Serial.print(x)
    #define DBG_PRINTLN(x) Serial.println(x)
    
#else    
    #define SERIAL Serial
    #define DBG_PRINT(x)
    #define DBG_PRINTLN(x)
#endif


#ifdef DBG
    void dbg_init(void) 
    {
        DBG.begin(115200);
        DBG_PRINTLN();
        DBG_PRINTLN("Hello");
    }
#else
    void dbg_init(void) {}
#endif    


#if defined LED_IO && defined USE_LED
    void led_init(void) 
    {
        pinMode(LED_IO, OUTPUT);
        digitalWrite(LED_IO, LOW);
    }

    void led_on(void) 
    {
        digitalWrite(LED_IO, HIGH);
    }

    void led_off(void) 
    {
        digitalWrite(LED_IO, LOW);
    }
#endif

#ifndef USE_LED
    void led_init(void) {}
    void led_on(void) {}
    void led_off(void) {}
#endif    

