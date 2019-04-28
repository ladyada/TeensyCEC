/*
  Copyright Frank Bösing, 2017
  GPL license at https://github.com/FrankBoesing/Teensy64
*/

#if defined(__SAMD51__)
#include <Adafruit_Arcada.h>
extern Adafruit_Arcada arcada;

#include "display_dma.h"
#if defined(USE_SPI_DMA)
  #error("Must not have SPI DMA enabled in Adafruit_SPITFT.h"
#endif

#include <Adafruit_ZeroDMA.h>
#include "wiring_private.h"  // pinPeripheral() function
#include <malloc.h>          // memalign() function

#include "font8x8.h"
#include "iopins.h"

// Actually 50 MHz due to timer shenanigans below, but SPI lib still thinks it's 24 MHz
#define SPICLOCK 24000000

Adafruit_ZeroDMA dma;                  ///< DMA instance
DmacDescriptor  *dptr         = NULL;  ///< 1st descriptor
DmacDescriptor  *descriptor    = NULL; ///< Allocated descriptor list
int numDescriptors;
int descriptor_bytes;

static uint16_t *screen = NULL;

volatile bool cancelled = false;
volatile uint8_t ntransfer = 0;

Display_DMA *foo; // Pointer into class so callback can access stuff

// DMA transfer-in-progress indicator and callback
static volatile bool dma_busy = false;
static void dma_callback(Adafruit_ZeroDMA *_dma) {
  dma_busy = false;
  foo->dmaFrame();
}

void Display_DMA::dmaFrame(void) {
  ntransfer++;
  if (ntransfer >= SCREEN_DMA_NUM_SETTINGS) {   
    ntransfer = 0;
  }

  digitalWrite(ARCADA_TFT_DC, 0);
  SPI.transfer(ILI9341_SLPOUT);
  digitalWrite(ARCADA_TFT_DC, 1);
  digitalWrite(ARCADA_TFT_CS, 1);
  SPI.endTransaction();
  if (cancelled) {
    return;
  }
  setAreaCentered();
  cancelled = false;
  
  SPI.beginTransaction(SPISettings(SPICLOCK, MSBFIRST, SPI_MODE0));
  digitalWrite(ARCADA_TFT_CS, 0);
  digitalWrite(ARCADA_TFT_DC, 0);
  SPI.transfer(ILI9341_RAMWR);
  digitalWrite(ARCADA_TFT_DC, 1);

  int sercom_id_core, sercom_id_slow;
  sercom_id_core = SERCOM7_GCLK_ID_CORE;
  sercom_id_slow = SERCOM7_GCLK_ID_SLOW;

  // Override SPI clock source to use 100 MHz peripheral clock (for 50 MHz SPI)
  GCLK_PCHCTRL_Type pchctrl;

  GCLK->PCHCTRL[sercom_id_core].bit.CHEN = 0;     // Disable timer
  while(GCLK->PCHCTRL[sercom_id_core].bit.CHEN);  // Wait for disable
  pchctrl.bit.GEN                        = 2;     // Use GENERIC_CLOCK_GENERATOR_100M defined in startup.c
  pchctrl.bit.CHEN                       = 1;
  GCLK->PCHCTRL[sercom_id_core].reg      = pchctrl.reg;
  while(!GCLK->PCHCTRL[sercom_id_core].bit.CHEN); // Wait for enable

  GCLK->PCHCTRL[sercom_id_slow].bit.CHEN = 0;     // Disable timer
  while(GCLK->PCHCTRL[sercom_id_slow].bit.CHEN);  // Wait for disable
  pchctrl.bit.GEN                        = 2;     // Use GENERIC_CLOCK_GENERATOR_100M defined in startup.c
  pchctrl.bit.CHEN                       = 1;
  GCLK->PCHCTRL[sercom_id_slow].reg      = pchctrl.reg;
  while(!GCLK->PCHCTRL[sercom_id_slow].bit.CHEN); // Wait for enable

  dma_busy = true;
  dma.startJob(); // Trigger next SPI DMA transfer
}



static bool setDmaStruct() {
  if (dma.allocate() != DMA_STATUS_OK) { // Allocate channel
    Serial.println("Couldn't allocate DMA");
    return false;
  }
  // The DMA library needs to alloc at least one valid descriptor,
  // so we do that here. It's not used in the usual sense though,
  // just before a transfer we copy descriptor[0] to this address.

  descriptor_bytes = EMUDISPLAY_TFTWIDTH * EMUDISPLAY_TFTHEIGHT / 2; // each one is half a screen but 2 bytes per screen so this is correct
  numDescriptors = 4;

  if (NULL == (dptr = dma.addDescriptor(NULL, NULL, descriptor_bytes, DMA_BEAT_SIZE_BYTE, false, false))) {
    Serial.println("Couldn't add descriptor");
    dma.free(); // Deallocate DMA channel
    return false;
  }

  // DMA descriptors MUST be 128-bit (16 byte) aligned.
  // memalign() is considered obsolete but it's replacements
  // (aligned_alloc() or posix_memalign()) are not currently
  // available in the version of ARM GCC in use, but this
  // is, so here we are.
  if (NULL == (descriptor = (DmacDescriptor *)memalign(16, numDescriptors * sizeof(DmacDescriptor)))) {
    Serial.println("Couldn't allocate descriptors");
    return false;
  }
  int                dmac_id;
  volatile uint32_t *data_reg;
  dmac_id  = SERCOM7_DMAC_ID_TX;
  data_reg = &SERCOM7->SPI.DATA.reg;
  dma.setPriority(DMA_PRIORITY_3);
  dma.setTrigger(dmac_id);
  dma.setAction(DMA_TRIGGER_ACTON_BEAT);

  // Initialize descriptor list.
  for(int d=0; d<numDescriptors; d++) {
    descriptor[d].BTCTRL.bit.VALID    = true;
    descriptor[d].BTCTRL.bit.EVOSEL   =
      DMA_EVENT_OUTPUT_DISABLE;
    descriptor[d].BTCTRL.bit.BLOCKACT =
       DMA_BLOCK_ACTION_NOACT;
    descriptor[d].BTCTRL.bit.BEATSIZE =
      DMA_BEAT_SIZE_BYTE;
    descriptor[d].BTCTRL.bit.DSTINC   = 0;
    descriptor[d].BTCTRL.bit.SRCINC   = 1;
    descriptor[d].BTCTRL.bit.STEPSEL  =
      DMA_STEPSEL_SRC;
    descriptor[d].BTCTRL.bit.STEPSIZE =
      DMA_ADDRESS_INCREMENT_STEP_SIZE_1;
    descriptor[d].BTCNT.reg   = descriptor_bytes;
    descriptor[d].DSTADDR.reg = (uint32_t)data_reg;
    // WARNING SRCADDRs MUST BE SET ELSEWHERE!
    if (d == numDescriptors-1) {
      descriptor[d].DESCADDR.reg = 0;
    } else {
      descriptor[d].DESCADDR.reg = (uint32_t)&descriptor[d+1];
    }
  }
  return true;
}

void Display_DMA::setFrameBuffer(uint16_t * fb) {
  screen = fb;
}

uint16_t * Display_DMA::getFrameBuffer(void) {
  return(screen);
}

void Display_DMA::setArea(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2) {
  arcada.startWrite();
  arcada.setAddrWindow(x1, y1, x2-x1+1, y2-y1+1);
}


void Display_DMA::refresh(void) {
  while (dma_busy);
  digitalWrite(ARCADA_TFT_DC, 0);
  SPI.transfer(ILI9341_SLPOUT);
  digitalWrite(ARCADA_TFT_DC, 1);
  digitalWrite(ARCADA_TFT_CS, 1);
  SPI.endTransaction();  

  fillScreen(ARCADA_CYAN);
  if (screen == NULL) {
    Serial.println("No screen framebuffer!");
    return;
  }
  Serial.println("DMA refresh");
  if (! setDmaStruct()) {
    Serial.println("Failed to set up DMA");
    while (1);
  }
  // Initialize descriptor list SRC addrs
  for(int d=0; d<numDescriptors; d++) {
    descriptor[d].SRCADDR.reg = (uint32_t)screen + descriptor_bytes * (d+1);
    Serial.print("DMA descriptor #"); Serial.print(d); Serial.print(" $"); Serial.println(descriptor[d].SRCADDR.reg, HEX);
  }
  // Move new descriptor into place...
  memcpy(dptr, &descriptor[0], sizeof(DmacDescriptor));
  dma_busy = true;
  foo = this; // Save pointer to ourselves so callback (outside class) can reach members
  dma.setCallback(dma_callback);

  setAreaCentered();
  cancelled = false; 
  
  SPI.beginTransaction(SPISettings(SPICLOCK, MSBFIRST, SPI_MODE0));
  digitalWrite(ARCADA_TFT_CS, 0);
  digitalWrite(ARCADA_TFT_DC, 0);
  SPI.transfer(ILI9341_RAMWR);
  digitalWrite(ARCADA_TFT_DC, 1);

  Serial.print("DMA kick");
  dma.startJob();                // Trigger first SPI DMA transfer
}


void Display_DMA::stop(void) {
  Serial.println("DMA stop");

  cancelled = true;
  SPI.beginTransaction(SPISettings(SPICLOCK, MSBFIRST, SPI_MODE0));  
  SPI.endTransaction();
  digitalWrite(ARCADA_TFT_CS, 1);
  digitalWrite(ARCADA_TFT_DC, 1);     
}

void Display_DMA::wait(void) {
  Serial.println("DMA wait");

/*
  rstop = 1;
  unsigned long m = millis(); 
  cancelled = true; 
  while (!rstop)  {
    if ((millis() - m) > 100) break;
    delay(10);
    asm volatile("wfi");
  };
  rstop = 0;
  */
}

uint16_t * Display_DMA::getLineBuffer(int j)
{ 
  return(&screen[j*ARCADA_TFT_WIDTH]);
}

/***********************************************************************************************
    DMA functions
 ***********************************************************************************************/

void Display_DMA::fillScreen(uint16_t color) {
  uint16_t *dst = &screen[0];
  for (int i=0; i<EMUDISPLAY_TFTWIDTH*EMUDISPLAY_TFTHEIGHT; i++)  {
    *dst++ = color;
  }
}


void Display_DMA::writeScreen(int width, int height, int stride, uint8_t *buf, uint16_t *palette16) {
  uint8_t *buffer=buf;
  uint8_t *src; 
  if (screen != NULL) {
  uint16_t *dst = &screen[0];
    int i,j;
    if (width*2 <= EMUDISPLAY_TFTWIDTH) {
      for (j=0; j<height; j++)
      {
        src=buffer;
        for (i=0; i<width; i++)
        {
          uint16_t val = palette16[*src++];
          *dst++ = val;
          *dst++ = val;
        }
        dst += (EMUDISPLAY_TFTWIDTH-width*2);
        if (height*2 <= EMUDISPLAY_TFTHEIGHT) {
          src=buffer;
          for (i=0; i<width; i++)
          {
            uint16_t val = palette16[*src++];
            *dst++ = val;
            *dst++ = val;
          }
          dst += (EMUDISPLAY_TFTWIDTH-width*2);      
        } 
        buffer += stride;      
      }
    }
    else if (width <= EMUDISPLAY_TFTWIDTH) {
      dst += (EMUDISPLAY_TFTWIDTH-width)/2;
      for (j=0; j<height; j++)
      {
        src=buffer;
        for (i=0; i<width; i++)
        {
          uint16_t val = palette16[*src++];
          *dst++ = val;
        }
        dst += (EMUDISPLAY_TFTWIDTH-width);
        if (height*2 <= EMUDISPLAY_TFTHEIGHT) {
          src=buffer;
          for (i=0; i<width; i++)
          {
            uint16_t val = palette16[*src++];
            *dst++ = val;
          }
          dst += (EMUDISPLAY_TFTWIDTH-width);
        }      
        buffer += stride;  
      }
    }    
  }
}

uint8_t last_even_line[256];

void Display_DMA::writeLine(int width, int height, int stride, uint8_t *buf, uint16_t *palette16) {
  uint8_t *src=buf;
  if (EMU_SCALEDOWN == 1) {
    uint16_t *dst = &screen[EMUDISPLAY_TFTWIDTH*stride];
    for (int i=0; i<width; i++) {
      uint8_t val = *src++;
      *dst++=palette16[val];
    }
  } else {
    if (stride % 2 == 0) { // skip even lines, we'll avg it later
      memcpy(last_even_line, buf, 256);
    } else {
      uint16_t *dst = &screen[EMUDISPLAY_TFTWIDTH*stride/2];
      uint8_t red, green, blue;
      uint16_t colors[4], color;
      for (int i=0; i<width; i+=2) {
        colors[0] = palette16[src[i]];
        colors[1] = palette16[src[i+1]];
        colors[2] = palette16[last_even_line[i]];
        colors[3] = palette16[last_even_line[i+1]];
        red = green = blue = 0;
        for (uint8_t c=0; c<4; c++) {      // sum up all 3 colors from 4 pixels
          red += (colors[c] >> 11) & 0x1F;
          green += (colors[c] >> 5) & 0x2F;
          blue += colors[c] & 0x1F;
        }
        red /= 4;  green /= 4;  blue /= 4;
        color = red & 0x1F; color <<= 6; // rejoin into a color
        color |= green & 0x2F; color <<= 5;
        color |= blue & 0x1F;
        *dst++=__builtin_bswap16(color);
      }
    }
  }
}

inline void Display_DMA::setAreaCentered(void) {
  setArea((ARCADA_TFT_WIDTH  - EMUDISPLAY_TFTWIDTH ) / 2, 
  (ARCADA_TFT_HEIGHT - EMUDISPLAY_TFTHEIGHT) / 2,
  (ARCADA_TFT_WIDTH  - EMUDISPLAY_TFTWIDTH ) / 2 + EMUDISPLAY_TFTWIDTH  - 1, 
  (ARCADA_TFT_HEIGHT - EMUDISPLAY_TFTHEIGHT) / 2 + EMUDISPLAY_TFTHEIGHT - 1);
}

#endif
