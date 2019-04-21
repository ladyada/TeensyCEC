#ifndef IOPINS_H
#define IOPINS_H

//#include <Arduino.h>

// ILI9341
#ifdef TEENSYDUINO
  #define TFT_SCLK        13
  #define TFT_MOSI        12
  #define TFT_MISO        11  
  #define TFT_DC          9
  #define TFT_CS          10
  #define TFT_RST         255  // 255 = unused, connected to 3.3V

#ifdef TOUCHSCREEN_SUPPORT
  #define TFT_TOUCH_CS    38
  #define TFT_TOUCH_INT   37
#endif
  
  #define PIN_JOY1_BTN     30
  #define PIN_JOY1_1       16
  #define PIN_JOY1_2       17
  #define PIN_JOY1_3       18
  #define PIN_JOY1_4       19
  
  // Analog joystick for JOY2 and 5 extra buttons
  #define PIN_JOY2_A1X    A12
  #define PIN_JOY2_A2Y    A13
  #define PIN_JOY2_BTN    36
  #define PIN_KEY_USER1   35
  #define PIN_KEY_USER2   34
  #define PIN_KEY_USER3   33
  #define PIN_KEY_USER4   39
  #define PIN_KEY_ESCAPE  23

  #define SD_CS           

#elif defined(ARDUINO_GRAND_CENTRAL_M4) // TFT Shield
  #define TFT_SPI         SPI
  #define TFT_SERCOM      SERCOM7
  #define TFT_DC          9
  #define TFT_CS          10
  #define TFT_RST         -1  // unused
  #define SD_SPI_PORT     SDCARD_SPI
  #define SD_CS           SDCARD_SS_PIN

  #define PIN_JOY1_BTN     22
  #define PIN_JOY1_1       23
  #define PIN_JOY1_2       24
  #define PIN_JOY1_3       25
  #define PIN_JOY1_4       26
  
  // Analog joystick for JOY2 and 5 extra buttons
  #define PIN_JOY2_A1X    A8
  #define PIN_JOY2_A2Y    A9
  #define PIN_JOY2_BTN    27
  #define PIN_KEY_USER1   A10
  #define PIN_KEY_USER2   29
  #define PIN_KEY_USER3   30
  #define PIN_KEY_USER4   31
  #define PIN_KEY_ESCAPE  32

#endif



// I2C keyboard
//#define I2C_SCL_IO        (gpio_num_t)5 
//#define I2C_SDA_IO        (gpio_num_t)4 





#endif
