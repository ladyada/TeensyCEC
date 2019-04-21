/*
  Copyright Frank Bösing, 2017
*/

#ifndef _ILI9341_T3DMAH_
#define _ILI9341_T3DMAH_


#include <Adafruit_ILI9341.h>
#include <Adafruit_ZeroDMA.h>

#ifdef __cplusplus
#include <Arduino.h>
#include <SPI.h>
#endif

#define DMA_FULL 1
#define FLIP_SCREEN 1


#define RGBVAL32(r,g,b)  ( (r<<16) | (g<<8) | b )
#define RGBVAL16(r,g,b)  ( (((r>>3)&0x1f)<<11) | (((g>>2)&0x3f)<<5) | (((b>>3)&0x1f)<<0) )
#define RGBVAL8(r,g,b)   ( (((r>>5)&0x07)<<5) | (((g>>5)&0x07)<<2) | (((b>>6)&0x3)<<0) )
#define R16(rgb) ((rgb>>8)&0xf8) 
#define G16(rgb) ((rgb>>3)&0xfc) 
#define B16(rgb) ((rgb<<3)&0xf8) 


#define EMUDISPLAY_TFTWIDTH      256
#define EMUDISPLAY_TFTHEIGHT     240
#define ILI9341_TFTREALWIDTH  320
#define ILI9341_TFTREALHEIGHT 240

#define ILI9341_VIDEOMEMSPARE 0

#ifdef __cplusplus

#define SCREEN_DMA_MAX_SIZE 0xD000
#define SCREEN_DMA_NUM_SETTINGS (((uint32_t)((2 * ILI9341_TFTHEIGHT * ILI9341_TFTWIDTH) / SCREEN_DMA_MAX_SIZE))+1)


class ILI9341_t3DMA
{
  public:
  	ILI9341_t3DMA(int8_t _CS, int8_t _DC, int8_t _RST = 255);

	  void setFrameBuffer(uint16_t * fb);
	  static uint16_t * getFrameBuffer(void);

	  void begin(void);
	  void flipscreen(bool flip);
    boolean isflipped(void);
	  void start(void);
	  void refresh(void);
	  void stop();
	  void wait(void);	
    uint16_t * getLineBuffer(int j);
            
    void setArea(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2);  
    void fillScreenNoDma(uint16_t color);
    void writeScreenNoDma(const uint16_t *pcolors);
    void drawTextNoDma(int16_t x, int16_t y, const char * text, uint16_t fgcolor, uint16_t bgcolor, bool doublesize);
    void drawRectNoDma(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void drawSpriteNoDma(int16_t x, int16_t y, const uint16_t *bitmap);
    void drawSpriteNoDma(int16_t x, int16_t y, const uint16_t *bitmap, uint16_t croparx, uint16_t cropary, uint16_t croparw, uint16_t croparh);

    void writeScreen(int width, int height, int stride, uint8_t *buffer, uint16_t *palette16);
    void writeLine(int width, int height, int stride, uint8_t *buffer, uint16_t *palette16);
	  void fillScreen(uint16_t color);
	  void writeScreen(const uint16_t *pcolors);
	  void drawPixel(int16_t x, int16_t y, uint16_t color);
	  uint16_t getPixel(int16_t x, int16_t y);

    void dmaFrame(void); // End DMA-issued frame and start a new one

  protected:
    Adafruit_ILI9341 _tft;

    int8_t _rst, _cs, _dc;
    const uint16_t max_screen_width = ILI9341_TFTWIDTH-1;
    const uint16_t max_screen_height = ILI9341_TFTHEIGHT-1;	
	  bool flipped=false;
};

#endif
#endif
