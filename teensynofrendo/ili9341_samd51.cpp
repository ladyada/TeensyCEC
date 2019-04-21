/*
  Copyright Frank Bösing, 2017
*/

#if defined(__SAMD51__)


#include "ili9341_samd51.h"
#if defined(USE_SPI_DMA)
  #error("Must not have SPI DMA enabled in Adafruit_SPITFT.h"
#endif

#include <Adafruit_ZeroDMA.h>
#include "wiring_private.h"  // pinPeripheral() function
#include <malloc.h>          // memalign() function

Adafruit_ZeroDMA dma;                  ///< DMA instance
DmacDescriptor  *dptr         = NULL;  ///< 1st descriptor
DmacDescriptor  *descriptor    = NULL; ///< Allocated descriptor list
int numDescriptors;
int descriptor_bytes;
volatile uint8_t ntransfer = 0;

// DMA transfer-in-progress indicator and callback
static volatile bool dma_busy = false;
static void dma_callback(Adafruit_ZeroDMA *dma) {
  dma_busy = false;
  ntransfer++;
  if (ntransfer >= SCREEN_DMA_NUM_SETTINGS) {   
    ntransfer = 0;
   // if (cancelled) {
        //dmatx.disable();
        //rstop = 1;
   // }
  }
}

#include "font8x8.h"
#include "iopins.h"

#define SPICLOCK 24000000


#ifdef DMA_FULL
//static DMAMEM uint16_t dmascreen[ILI9341_TFTHEIGHT*ILI9341_TFTWIDTH+ILI9341_VIDEOMEMSPARE];
static uint16_t * screen=NULL; //=dmascreen;
#else
static uint16_t * screen;
#endif

/*
static DMASetting dmasettings[SCREEN_DMA_NUM_SETTINGS];
static DMAChannel dmatx;
volatile uint8_t rstop = 0;
volatile bool cancelled = false;
volatile uint8_t ntransfer = 0;
*/

/*
//  3, 0xb1, 0x00, 0x1f, // FrameRate Control 61Hz
  3, 0xb1, 0x00, 0x10, // FrameRate Control 119Hz
  2, ILI9341_MADCTL, MADCTL_MX | MADCTL_MY | MADCTL_MV | MADCTL_BGR,
  0
*/


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


ILI9341_t3DMA::ILI9341_t3DMA(int8_t cs, int8_t dc, int8_t rst) :
_tft(cs, dc)
{
  _cs   = cs;
  _dc   = dc;
  _rst  = rst;
  pinMode(_dc, OUTPUT);
  pinMode(_cs, OUTPUT);
  digitalWrite(_cs, 1);
  digitalWrite(_dc, 1);
}

void ILI9341_t3DMA::setFrameBuffer(uint16_t * fb) {
  screen = fb;
}

uint16_t * ILI9341_t3DMA::getFrameBuffer(void) {
  return(screen);
}

void ILI9341_t3DMA::setArea(uint16_t x1,uint16_t y1,uint16_t x2,uint16_t y2) {
  _tft.startWrite();
  _tft.setAddrWindow(x1, y1, x2-x1+1, y2-y1+1);
}


void ILI9341_t3DMA::begin(void) {
  _tft.begin();
  if (! setDmaStruct()) {
    Serial.println("Failed to set up DMA");
  }

  dma.setCallback(dma_callback);
  //dma_cancelled = false; 
#ifdef FLIP_SCREEN          
  _tft.setRotation(1);
#endif            

};

void ILI9341_t3DMA::flipscreen(bool flip)
{
  if (flip) {
    _tft.setRotation(1);
  } else {
    _tft.setRotation(0);
  } 
}


void ILI9341_t3DMA::start(void) {
  Serial.println("DMA start");
  /*
  uint16_t centerdx = (ILI9341_TFTREALWIDTH - max_screen_width)/2;
  uint16_t centerdy = (ILI9341_TFTREALHEIGHT - max_screen_height)/2;
  setArea(centerdx, centerdy, max_screen_width+centerdx, max_screen_height+centerdy);
  SPI0_RSER |= SPI_RSER_TFFF_DIRS | SPI_RSER_TFFF_RE;  // Set ILI_DMA Interrupt Request Select and Enable register
  SPI0_MCR &= ~SPI_MCR_HALT;  //Start transfers.
  SPI0_CTAR0 = SPI0_CTAR1;
  (*(volatile uint16_t *)((int)&SPI0_PUSHR + 2)) = (SPI_PUSHR_CTAS(1) | SPI_PUSHR_CONT) >> 16; //Enable 16 Bit Transfers + Continue-Bit
  */
}


void ILI9341_t3DMA::refresh(void) {
  Serial.println("DMA refresh");

/*
#ifdef DMA_FULL
  if (screen != NULL) {
    setDmaStruct();
    start();
    fillScreen(RGBVAL16(0x00,0x00,0x00));
    digitalWrite(_cs, 0);  
    dmasettings[SCREEN_DMA_NUM_SETTINGS - 1].TCD->CSR &= ~DMA_TCD_CSR_DREQ; //disable "disableOnCompletion"
    dmatx.enable();
    ntransfer = 0;  
    dmatx = dmasettings[0];
    rstop = 0;     
  }
#endif      
*/
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
  digitalWrite(_cs, 1);
  digitalWrite(_dc, 1);     
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
  return(&screen[j*ILI9341_TFTREALWIDTH]);
}

/***********************************************************************************************
    no DMA functions
 ***********************************************************************************************/
void ILI9341_t3DMA::fillScreenNoDma(uint16_t color) {
  _tft.fillScreen(color);
}

boolean first_time = true;
void ILI9341_t3DMA::writeScreenNoDma(const uint16_t *pcolors) {
  setArea(0, 0, EMUDISPLAY_TFTWIDTH-1, EMUDISPLAY_TFTHEIGHT-1);  
  
  SPI.beginTransaction(SPISettings(SPICLOCK, MSBFIRST, SPI_MODE0));
  digitalWrite(_cs, 0);
  digitalWrite(_dc, 0);
  SPI.transfer(ILI9341_RAMWR);
  digitalWrite(_dc, 1);

  // Initialize descriptor list SRC addrs
  for(int d=0; d<numDescriptors; d++) {
    descriptor[d].SRCADDR.reg = (uint32_t)pcolors + descriptor_bytes * (d+1);
    //Serial.print("DMA descriptor #"); Serial.print(d); Serial.print(" $"); Serial.println(descriptor[d].SRCADDR.reg, HEX);
  }
  // Move new descriptor into place...
  memcpy(dptr, &descriptor[0], sizeof(DmacDescriptor));
  dma_busy = true;
  //Serial.print("DMA kick");
  dma.startJob();                // Trigger SPI DMA transfer
  while (dma_busy) {
    //Serial.print("."); delay(10);
  }

           /*
  for (int j=0; j<240*EMUDISPLAY_TFTWIDTH; j++) {
    uint16_t color = *pcolors++;
    SERCOM7->SPI.DATA.bit.DATA = color >> 8; // Writing data into Data register
    while( SERCOM7->SPI.INTFLAG.bit.TXC == 0 ); // Waiting Complete Reception
    SERCOM7->SPI.DATA.bit.DATA = color & 0xFF; // Writing data into Data register
    while( SERCOM7->SPI.INTFLAG.bit.TXC == 0 ); // Waiting Complete Reception
  }*/
  
  //Serial.print("DMA done");
  digitalWrite(_dc, 0);
  SPI.transfer(ILI9341_SLPOUT);
  digitalWrite(_dc, 1);
  digitalWrite(_cs, 1);
  SPI.endTransaction();  
  
  setArea(0, 0, max_screen_width, max_screen_height);
}

void ILI9341_t3DMA::drawSpriteNoDma(int16_t x, int16_t y, const uint16_t *bitmap) {
    drawSpriteNoDma(x,y,bitmap, 0,0,0,0);
}

void ILI9341_t3DMA::drawSpriteNoDma(int16_t x, int16_t y, const uint16_t *bitmap, uint16_t arx, uint16_t ary, uint16_t arw, uint16_t arh)
{
  int bmp_offx = 0;
  int bmp_offy = 0;
  uint16_t *bmp_ptr;
    
  int w =*bitmap++;
  int h = *bitmap++;
//Serial.println(w);
//Serial.println(h);

  if ( (arw == 0) || (arh == 0) ) {
    // no crop window
    arx = x;
    ary = y;
    arw = w;
    arh = h;
  }
  else {
    if ( (x>(arx+arw)) || ((x+w)<arx) || (y>(ary+arh)) || ((y+h)<ary)   ) {
      return;
    }
    
    // crop area
    if ( (x > arx) && (x<(arx+arw)) ) { 
      arw = arw - (x-arx);
      arx = arx + (x-arx);
    } else {
      bmp_offx = arx;
    }
    if ( ((x+w) > arx) && ((x+w)<(arx+arw)) ) {
      arw -= (arx+arw-x-w);
    }  
    if ( (y > ary) && (y<(ary+arh)) ) {
      arh = arh - (y-ary);
      ary = ary + (y-ary);
    } else {
      bmp_offy = ary;
    }
    if ( ((y+h) > ary) && ((y+h)<(ary+arh)) ) {
      arh -= (ary+arh-y-h);
    }     
  }

  setArea(arx, ary, arx+arw-1, ary+arh-1);  
  
  SPI.beginTransaction(SPISettings(SPICLOCK, MSBFIRST, SPI_MODE0));
  digitalWrite(_cs, 0);
  digitalWrite(_dc, 0);
  SPI.transfer(ILI9341_RAMWR);      

  bitmap = bitmap + bmp_offy*w + bmp_offx;
  for (int row=0;row<arh; row++)
  {
    bmp_ptr = (uint16_t*)bitmap;
    for (int col=0;col<arw; col++)
    {
        uint16_t color = *bmp_ptr++;
        digitalWrite(_dc, 1);
        SPI.transfer16(color);             
    } 
    bitmap +=  w;
  }
  digitalWrite(_dc, 0);
  SPI.transfer(ILI9341_SLPOUT);
  digitalWrite(_dc, 1);
  digitalWrite(_cs, 1);
  SPI.endTransaction();   
  setArea(0, 0, EMUDISPLAY_TFTWIDTH-1, ILI9341_TFTHEIGHT-1);  
}

void ILI9341_t3DMA::drawTextNoDma(int16_t x, int16_t y, const char * text, uint16_t fgcolor, uint16_t bgcolor, bool doublesize) {
  uint16_t c;
  while ((c = *text++)) {
    const unsigned char * charpt=&font8x8[c][0];

    setArea(x,y,x+7,y+(doublesize?15:7));
  
    //SPI.beginTransaction(SPISettings(SPICLOCK, MSBFIRST, SPI_MODE0));
    digitalWrite(_cs, 0);
    //digitalWrite(_dc, 0);
    //SPI.transfer(ILI9341_RAMWR);

    digitalWrite(_dc, 1);
    for (int i=0;i<8;i++)
    {
      unsigned char bits;
      if (doublesize) {
        bits = *charpt;     
        digitalWrite(_dc, 1);
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
      digitalWrite(_dc, 1);
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
  
    digitalWrite(_dc, 0);
    SPI.transfer(ILI9341_SLPOUT);
    digitalWrite(_dc, 1);
    digitalWrite(_cs, 1);
    SPI.endTransaction();  
  }
  
  setArea(0, 0, max_screen_width, max_screen_height);  
}


void ILI9341_t3DMA::drawRectNoDma(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  _tft.fillRect(x, y, w, h, color);
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
#ifdef DMA_FULL
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
  #else
  int i,j,k=0;
  if (width*2 <= EMUDISPLAY_TFTWIDTH) {
    if (height*2 <= EMUDISPLAY_TFTHEIGHT) {
      setArea(0,0,width*2-1,height*2-1);     
    }
    else {
      setArea(0,0,width*2-1,height-1);     
    }
    SPI.beginTransaction(SPISettings(SPICLOCK, MSBFIRST, SPI_MODE0));
    digitalWrite(_cs, 0);
    digitalWrite(_dc, 0);
    SPI.transfer(ILI9341_RAMWR);    

    for (j=0; j<height; j++)
    {    
      src=buffer;
      for (i=0; i<width; i++)
      {
        uint16_t val = palette16[*src++];
    digitalWrite(_dc, 1);   
        SPI.transfer16(val);
    digitalWrite(_dc, 1);   
        SPI.transfer16(val);
      }
      k+=1;
      if (height*2 <= EMUDISPLAY_HEIGHT) {
        src=buffer;
        for (i=0; i<width; i++)
        {
          uint16_t val = palette16[*src++];
    digitalWrite(_dc, 1);   
          SPI.transfer16(val);
    digitalWrite(_dc, 1);   
          SPI.transfer16(val);
        }
      }
      buffer += stride;
    }
  }
  else if (width <= EMUDISPLAY_TFTWIDTH) {
    if (height*2 <= EMUDISPLAY_TFTHEIGHT) {
      setArea((EMUDISPLAY_TFTWIDTH-width)/2,0,(EMUDISPLAY_TFTWIDTH-width)/2+width-1,height*2-1);  
    }
    else {
      setArea((EMUDISPLAY_TFTWIDTHwidth)/2,0,(EMUDISPLAY_TFTWIDTH-width)/2+width-1,height-1);  
    }   
    SPI.beginTransaction(SPISettings(SPICLOCK, MSBFIRST, SPI_MODE0));
    digitalWrite(_cs, 0);
    digitalWrite(_dc, 0);
    SPI.transfer(ILI9341_RAMWR);

    for (j=0; j<height; j++)
    {
      src=buffer;
      for (i=0; i<width; i++)
      {
        uint16_t val = palette16[*src++];
    digitalWrite(_dc, 1);   
        SPI.transfer16(val);
      }
      if (height*2 <= EMUDISPLAY_TFTHEIGHT) {
        src=buffer;
        for (i=0; i<width; i++)
        {
          uint16_t val = palette16[*src++];
    digitalWrite(_dc, 1);   
          SPI.transfer16(val);
        }
      }
      buffer += stride;
    }
  }
  digitalWrite(_dc, 0);
  SPI.transfer(ILI9341_SLPOUT);
  digitalWrite(_dc, 1);
  digitalWrite(_cs, 1);
  SPI.endTransaction();

  setArea(0, 0, max_screen_width, max_screen_height);  
  //setArea(0,0,319,239);   
#endif
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

#endif
