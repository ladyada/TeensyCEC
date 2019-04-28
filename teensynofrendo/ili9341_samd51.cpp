/*
  Copyright Frank Bösing, 2017
*/

#if defined(__SAMD51__)
#include <Adafruit_Arcada.h>
extern Adafruit_Arcada arcada;

#include "ili9341_samd51.h"
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
volatile uint8_t rstop = 0;
volatile bool cancelled = false;
volatile uint8_t ntransfer = 0;

ILI9341_t3DMA *foo; // Pointer into class so callback can access stuff

// DMA transfer-in-progress indicator and callback
static volatile bool dma_busy = false;
static void dma_callback(Adafruit_ZeroDMA *_dma) {
  dma_busy = false;
  foo->dmaFrame();
}

void ILI9341_t3DMA::dmaFrame(void) {
  ntransfer++;
  if (ntransfer >= SCREEN_DMA_NUM_SETTINGS) {   
    ntransfer = 0;
    if (cancelled) {
      rstop = 1;
    }
  }

  digitalWrite(ARCADA_TFT_DC, 0);
  SPI.transfer(ILI9341_SLPOUT);
  digitalWrite(ARCADA_TFT_DC, 1);
  digitalWrite(ARCADA_TFT_CS, 1);
  SPI.endTransaction();
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

void ILI9341_t3DMA::setFrameBuffer(uint16_t * fb) {
  screen = fb;
}

uint16_t * ILI9341_t3DMA::getFrameBuffer(void) {
  return(screen);
}

void ILI9341_t3DMA::setArea(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2) {
  arcada.startWrite();
  arcada.setAddrWindow(x1, y1, x2-x1+1, y2-y1+1);
}


void ILI9341_t3DMA::refresh(void) {
  while (dma_busy);
  digitalWrite(ARCADA_TFT_DC, 0);
  SPI.transfer(ILI9341_SLPOUT);
  digitalWrite(ARCADA_TFT_DC, 1);
  digitalWrite(ARCADA_TFT_CS, 1);
  SPI.endTransaction();  

  fillScreen(RGBVAL16(0xFF,0xFF,0xFF));
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


void ILI9341_t3DMA::stop(void) {
  Serial.println("DMA stop");

/*
  rstop = 0;
  wait();
  delay(50);
  //dmatx.disable();
  */
  SPI.beginTransaction(SPISettings(SPICLOCK, MSBFIRST, SPI_MODE0));  
  SPI.endTransaction();
  digitalWrite(ARCADA_TFT_CS, 1);
  digitalWrite(ARCADA_TFT_DC, 1);     
}

void ILI9341_t3DMA::wait(void) {
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

uint16_t * ILI9341_t3DMA::getLineBuffer(int j)
{ 
  return(&screen[j*ARCADA_TFT_WIDTH]);
}

/***********************************************************************************************
    no DMA functions
 ***********************************************************************************************/


void ILI9341_t3DMA::writeScreenNoDma(const uint16_t *pcolors) {
  setAreaCentered();
  
  SPI.beginTransaction(SPISettings(SPICLOCK, MSBFIRST, SPI_MODE0));
  digitalWrite(ARCADA_TFT_CS, 0);
  digitalWrite(ARCADA_TFT_DC, 0);
  SPI.transfer(ILI9341_RAMWR);
  digitalWrite(ARCADA_TFT_DC, 1);

  for (int j=0; j<240*EMUDISPLAY_TFTWIDTH; j++) {
    uint16_t color = *pcolors++;
    SERCOM7->SPI.DATA.bit.DATA = color >> 8; // Writing data into Data register
    while( SERCOM7->SPI.INTFLAG.bit.TXC == 0 ); // Waiting Complete Reception
    SERCOM7->SPI.DATA.bit.DATA = color & 0xFF; // Writing data into Data register
    while( SERCOM7->SPI.INTFLAG.bit.TXC == 0 ); // Waiting Complete Reception
  }
  
  //Serial.print("DMA done");
  digitalWrite(ARCADA_TFT_DC, 0);
  SPI.transfer(ILI9341_SLPOUT);
  digitalWrite(ARCADA_TFT_DC, 1);
  digitalWrite(ARCADA_TFT_CS, 1);
  SPI.endTransaction();  
  
  setArea(0, 0, max_screen_width, max_screen_height);
}

void ILI9341_t3DMA::drawTextNoDma(int16_t x, int16_t y, const char * text, uint16_t fgcolor, uint16_t bgcolor, bool doublesize) {
  uint16_t c;
  while ((c = *text++)) {
    const unsigned char * charpt=&font8x8[c][0];

    setArea(x,y,x+7,y+(doublesize?15:7));
  
    digitalWrite(ARCADA_TFT_CS, 0);
    digitalWrite(ARCADA_TFT_DC, 1);
    for (int i=0;i<8;i++)
    {
      unsigned char bits;
      if (doublesize) {
        bits = *charpt;     
        digitalWrite(ARCADA_TFT_DC, 1);
        if (bits&0x01) SPI.transfer16(fgcolor);
        else SPI.transfer16(bgcolor);
        bits = bits >> 1;     
        if (bits&0x01) SPI.transfer16(fgcolor);
        else SPI.transfer16(bgcolor);
        bits = bits >> 1;     
        if (bits&0x01) SPI.transfer16(fgcolor);
        else SPI.transfer16(bgcolor);
        bits = bits >> 1;     
        if (bits&0x01) SPI.transfer16(fgcolor);
        else SPI.transfer16(bgcolor);
        bits = bits >> 1;     
        if (bits&0x01) SPI.transfer16(fgcolor);
        else SPI.transfer16(bgcolor);
        bits = bits >> 1;     
        if (bits&0x01) SPI.transfer16(fgcolor);
        else SPI.transfer16(bgcolor);
        bits = bits >> 1;     
        if (bits&0x01) SPI.transfer16(fgcolor);
        else SPI.transfer16(bgcolor);
        bits = bits >> 1;     
        if (bits&0x01) SPI.transfer16(fgcolor);
        else SPI.transfer16(bgcolor);       
      }
      bits = *charpt++;     
      digitalWrite(ARCADA_TFT_DC, 1);
      if (bits&0x01) SPI.transfer16(fgcolor);
      else SPI.transfer16(bgcolor);
      bits = bits >> 1;     
      if (bits&0x01) SPI.transfer16(fgcolor);
      else SPI.transfer16(bgcolor);
      bits = bits >> 1;     
      if (bits&0x01) SPI.transfer16(fgcolor);
      else SPI.transfer16(bgcolor);
      bits = bits >> 1;     
      if (bits&0x01) SPI.transfer16(fgcolor);
      else SPI.transfer16(bgcolor);
      bits = bits >> 1;     
      if (bits&0x01) SPI.transfer16(fgcolor);
      else SPI.transfer16(bgcolor);
      bits = bits >> 1;     
      if (bits&0x01) SPI.transfer16(fgcolor);
      else SPI.transfer16(bgcolor);
      bits = bits >> 1;     
      if (bits&0x01) SPI.transfer16(fgcolor);
      else SPI.transfer16(bgcolor);
      bits = bits >> 1;     
      if (bits&0x01) SPI.transfer16(fgcolor);
      else SPI.transfer16(bgcolor);
    }
    x +=8;
  
    digitalWrite(ARCADA_TFT_CS, 0);
    SPI.transfer(ILI9341_SLPOUT);
    digitalWrite(ARCADA_TFT_DC, 1);
    digitalWrite(ARCADA_TFT_CS, 1);
    SPI.endTransaction();  
  }
  
  setArea(0, 0, max_screen_width, max_screen_height);  
}

/***********************************************************************************************
    DMA functions
 ***********************************************************************************************/

void ILI9341_t3DMA::fillScreen(uint16_t color) {
  int i,j;
  uint16_t *dst = &screen[0];
  for (j=0; j<EMUDISPLAY_TFTHEIGHT; j++)
  {
    for (i=0; i<EMUDISPLAY_TFTWIDTH; i++)
    {
      *dst++ = color;
    }
  }
}


void ILI9341_t3DMA::writeScreen(int width, int height, int stride, uint8_t *buf, uint16_t *palette16) {
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

void ILI9341_t3DMA::writeLine(int width, int height, int stride, uint8_t *buf, uint16_t *palette16) {
  uint8_t *src=buf;
  uint16_t *dst = &screen[EMUDISPLAY_TFTWIDTH*stride];
  for (int i=0; i<width; i++)
  {
    uint8_t val = *src++;
    *dst++=palette16[val];
  }
}

inline void ILI9341_t3DMA::setAreaCentered(void) {
  setArea((ARCADA_TFT_WIDTH  - EMUDISPLAY_TFTWIDTH ) / 2, 
  (ARCADA_TFT_HEIGHT - EMUDISPLAY_TFTHEIGHT) / 2,
  (ARCADA_TFT_WIDTH  - EMUDISPLAY_TFTWIDTH ) / 2 + EMUDISPLAY_TFTWIDTH  - 1, 
  (ARCADA_TFT_HEIGHT - EMUDISPLAY_TFTHEIGHT) / 2 + EMUDISPLAY_TFTHEIGHT - 1);
}

#endif
