extern "C" {
  #include "emuapi.h"
  #include "iopins.h"  
}


#include "keyboard_osd.h"
#if defined(TEENSYDUINO)
  #include "ili9341_t3dma.h"
#elif defined(__SAMD51__)
  #include "ili9341_samd51.h"
#endif

extern "C" {
#include "nes_emu.h"
}

#ifdef UVGA_SUPPORT 
  #include <uVGA.h>
  uVGA uvga;
  #if F_CPU == 144000000
    #define UVGA_144M_326X240
    #define UVGA_XRES 326
    #define UVGA_YRES 240
    #define UVGA_XRES_EXTRA 10
  #elif  F_CPU == 180000000
    #define UVGA_180M_360X300
    #define UVGA_XRES 360
    #define UVGA_YRES 300
    #define UVGA_XRES_EXTRA 8 
  #elif  F_CPU == 240000000
    #define UVGA_240M_452X240
    #define UVGA_XRES 452
    #define UVGA_YRES 240
    #define UVGA_XRES_EXTRA 12 
  #else
    #error Please select F_CPU=240MHz or F_CPU=180MHz or F_CPU=144MHz
  #endif

  #include <uVGA_valid_settings.h>
  
  #ifdef DMA_FULL
  uint8_t * VGA_frame_buffer;
  #else
  UVGA_STATIC_FRAME_BUFFER(uvga_fb);
  uint8_t * VGA_frame_buffer = uvga_fb;
  #endif
#endif
bool vgaMode = false;

#ifdef TOUCHSCREEN_SUPPORT
ILI9341_t3DMA tft = ILI9341_t3DMA(TFT_CS, TFT_DC, TFT_RST, TFT_MOSI, TFT_SCLK, TFT_MISO, TFT_TOUCH_CS, TFT_TOUCH_INT);
#else
#ifdef TEENSYDUINO
  ILI9341_t3DMA tft = ILI9341_t3DMA(TFT_CS, TFT_DC, TFT_RST, TFT_MOSI, TFT_SCLK, TFT_MISO);
#else
  // use SPI
  ILI9341_t3DMA tft = ILI9341_t3DMA(TFT_CS, TFT_DC, TFT_RST);
#endif
#endif
static unsigned char  palette8[PALETTE_SIZE];
static unsigned short palette16[PALETTE_SIZE];
#if defined(TEENSYDUINO)
  static IntervalTimer myTimer;
  #include <elapsedMillis.h>
  static elapsedMicros tius;
  #define TIMER_LED 01
#elif defined(__SAMD51__)
  #include "Adafruit_ZeroTimer.h"
  #define MY_TIMER_TC 5
  #define TIMER_LED 22
  #define FRAME_LED 23
  Adafruit_ZeroTimer myTimer = Adafruit_ZeroTimer(MY_TIMER_TC);

  void TC5_Handler(){  // change this to match TC #
    Adafruit_ZeroTimer::timerHandler(MY_TIMER_TC);
  }
  void Timer5Callback()
  {
    vblCount();
  }
#endif

volatile boolean vbl=true;
static int skip=0;


static void main_step() {
  uint16_t bClick = emu_DebounceLocalKeys();
  
  // Global key handling
  if (bClick & MASK_KEY_USER1) {  
    emu_printf((char*)"user1");
  }
  if (bClick & MASK_KEY_USER2) {  
    emu_printf((char*)"user2");
  }
  if (bClick & MASK_KEY_USER3) {  
    emu_printf((char*)"user3");
  }
  if (bClick & MASK_KEY_USER4) {  
    emu_printf((char*)"user4");
    emu_SwapJoysticks(0);    
  }  
  if (bClick & MASK_KEY_ESCAPE) {  
    emu_printf((char*)"esc");
    *(volatile uint32_t *)0xE000ED0C = 0x5FA0004;
    while (true) {
      ;
    }    
  }
     
  if (menuActive()) {
    int action = handleMenu(bClick);
    char * filename = menuSelection();
    if (action == ACTION_RUNTFT) {
      tft.setFrameBuffer((uint16_t *)malloc((ILI9341_TFTHEIGHT*ILI9341_TFTWIDTH)*sizeof(uint16_t)));     
      Serial.print("TFT init: ");  
      Serial.println(tft.getFrameBuffer()?"ok":"ko");       
      toggleMenu(false);
      vgaMode = false;   
      tft.fillScreenNoDma( RGBVAL16(0x00,0x00,0x00) );
      tft.refresh();
      emu_Init(filename);
    }
#ifdef UVGA_SUPPORT 
    else if (action == ACTION_RUNVGA) {
      toggleMenu(false);
      vgaMode = true;
      tft.setFrameBuffer((uint16_t *)malloc((UVGA_YRES*(UVGA_XRES+UVGA_XRES_EXTRA))*sizeof(uint8_t)));
      VGA_frame_buffer = (uint8_t *)tft.getFrameBuffer();
      uvga.set_static_framebuffer(VGA_frame_buffer);
      emu_Init(filename);           
      int retvga = uvga.begin(&modeline);
      Serial.print("VGA init: ");  
      Serial.println(retvga);      
      uvga.clear(0x00);
      //tft.start();
      tft.fillScreenNoDma( RGBVAL16(0x00,0x00,0x00) );
      // In VGA mode, we show the keyboard on TFT
      toggleVirtualkeyboard(true); // keepOn
      Serial.println("Starting");              
    }         
#endif
    delay(20);
  }
#ifdef TOUCHSCREEN_SUPPORT
  else if (callibrationActive()) {
    handleCallibration(bClick);
  } 
#endif
  else {
#ifdef TOUCHSCREEN_SUPPORT
    handleVirtualkeyboard();
#endif
    if ( (!virtualkeyboardIsActive()) || (vgaMode) ) {
      emu_Step();
    }
  }

  //emu_printi(tius);
  //tius=0;  
}

static void vblCount() { 
  digitalWrite(TIMER_LED, vbl);
  vbl = !vbl;
#ifdef TIMER_REND
  main_step();
#endif
}

// ****************************************************
// the setup() method runs once, when the sketch starts
// ****************************************************
void setup() {
  while (!Serial);
  delay(100);
  Serial.println("-----------------------------");
  
  tft.begin();
  tft.flipscreen(true);  
  tft.start();

  emu_init();  

#if defined(TEENSYDUINO)
  #ifdef TIMER_REND
    myTimer.begin(vblCount, 10000);  //to run every 10ms
  #else
    myTimer.begin(vblCount, 5000);  // will try to VSYNC next at 5ms
  #endif
#elif defined(__SAMD51__)
  myTimer.configure(TC_CLOCK_PRESCALER_DIV4, // prescaler
                TC_COUNTER_SIZE_16BIT,   // bit width of timer/counter
                TC_WAVE_GENERATION_MATCH_PWM // frequency or PWM mode
                );

  myTimer.setCompare(0, 48000000/4/200);  // 48mhz clock, div 4, div 200 hz -> 5 ms vsync
  myTimer.setCallback(true, TC_CALLBACK_CC_CHANNEL0, Timer5Callback);
  myTimer.enable(true);
#endif
#ifdef TIMER_LED
  pinMode(TIMER_LED, OUTPUT);
#endif
#ifdef FRAME_LED
  pinMode(FRAME_LED, OUTPUT);
#endif
}


// ****************************************************
// the loop() method runs continuously
// ****************************************************
void loop() {
#ifndef TIMER_REND  
  main_step();
#endif
}



void emu_SetPaletteEntry(unsigned char r, unsigned char g, unsigned char b, int index)
{
  if (index<PALETTE_SIZE) {
    //Serial.println("%d: %d %d %d\n", index, r,g,b);
    palette8[index]  = __builtin_bswap16(RGBVAL8(r,g,b));
    palette16[index] = __builtin_bswap16(RGBVAL16(r,g,b));
  }
}

void emu_DrawVsync(void)
{
  volatile boolean vb=vbl;
  skip += 1;
  skip &= VID_FRAME_SKIP;

  if (!vgaMode) {
    while (vbl==vb) {};
  }
  else {
    //Serial.println("r");
    while (vbl==vb) {};
  }
}

void emu_DrawLine(unsigned char * VBuf, int width, int height, int line) 
{
  if (!vgaMode) {
    tft.writeLine(width,1,line, VBuf, palette16);
  }
#ifdef UVGA_SUPPORT 
  else {
    int fb_width=UVGA_XRES,fb_height=UVGA_YRES;
    fb_width += UVGA_XRES_EXTRA;
    int offx = (fb_width-width)/2;   
    int offy = (fb_height-height)/2+line;
    uint8_t * dst=VGA_frame_buffer+(fb_width*offy)+offx;    
    for (int i=0; i<width; i++)
    {
       uint8_t pixel = palette8[*VBuf++];
        *dst++=pixel;
    }
  }
#endif
}  

void emu_DrawScreen(unsigned char * VBuf, int width, int height, int stride) 
{
  if (!vgaMode) {
    if (skip==0) {
      tft.writeScreen(width,height-TFT_VBUFFER_YCROP,stride, VBuf+(TFT_VBUFFER_YCROP/2)*stride, palette16);
    }
  }
#ifdef UVGA_SUPPORT 
  else {
    int fb_width=UVGA_XRES, fb_height=UVGA_YRES;
    //uvga.get_frame_buffer_size(&fb_width, &fb_height);
    fb_width += UVGA_XRES_EXTRA;
    int offx = (fb_width-width)/2;   
    int offy = (fb_height-height)/2;
    uint8_t * buf=VGA_frame_buffer+(fb_width*offy)+offx;
    for (int y=0; y<height; y++)
    {
      uint8_t * dest = buf;
      for (int x=0; x<width; x++)
      {
        uint8_t pixel = palette8[*VBuf++];
        *dest++=pixel;
        //*dest++=pixel;
      }
      buf += fb_width;
      VBuf += (stride-width);
    }             
  }
#endif
}

int emu_FrameSkip(void)
{
  return skip;
}

void * emu_LineBuffer(int line)
{
  return (void*)tft.getLineBuffer(line);
}

#ifdef HAS_SND

#include <Audio.h>
#include "AudioPlaySystem.h"

AudioPlaySystem mymixer;
AudioOutputAnalog dac1;
AudioConnection   patchCord1(mymixer, dac1);

void emu_sndInit() {
  Serial.println("sound init");  

  AudioMemory(16);
  mymixer.start();
}

void emu_sndPlaySound(int chan, int volume, int freq)
{
  emu_printi(freq);
  if (chan < 6) {
    mymixer.sound(chan, freq, volume); 
  }
  
  /*
  Serial.print(chan);
  Serial.print(":" );  
  Serial.print(volume);  
  Serial.print(":" );  
  Serial.println(freq); 
  */ 
}

void emu_sndPlayBuzz(int size, int val) {
  mymixer.buzz(size,val); 
  //Serial.print((val==1)?1:0); 
  //Serial.print(":"); 
  //Serial.println(size); 
}

#endif
