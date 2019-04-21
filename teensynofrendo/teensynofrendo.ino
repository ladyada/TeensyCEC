extern "C" {
  #include "emuapi.h"
  #include "iopins.h"  
  #include "nes_emu.h"
}


#include "keyboard_osd.h"
#include "ili9341_samd51.h"
#include <Adafruit_Arcada.h>
Adafruit_Arcada arcada;
// use SPI
ILI9341_t3DMA tft = ILI9341_t3DMA(TFT_CS, TFT_DC, TFT_RST);

bool vgaMode = false;

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
  if (bClick & ARCADA_BUTTONMASK_START) {  
    emu_printf("Sta");
  }
  if (bClick & ARCADA_BUTTONMASK_SELECT) {  
    emu_printf("Sel");
  }
  if (bClick & ARCADA_BUTTONMASK_B) {  
    emu_printf("B");
  }
  if (bClick & ARCADA_BUTTONMASK_A) {  
    emu_printf("A");
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

    delay(20);
  }
  else {
    if ( (!virtualkeyboardIsActive()) || (vgaMode) ) {
      emu_Step();
    }
  }
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

  arcada.begin();
  tft.begin();
  tft.flipscreen(true);  
  tft.start();

  emu_init();  

  myTimer.configure(TC_CLOCK_PRESCALER_DIV4, // prescaler
                TC_COUNTER_SIZE_16BIT,   // bit width of timer/counter
                TC_WAVE_GENERATION_MATCH_PWM // frequency or PWM mode
                );

  myTimer.setCompare(0, 48000000/4/200);  // 48mhz clock, div 4, div 200 hz -> 5 ms vsync
  myTimer.setCallback(true, TC_CALLBACK_CC_CHANNEL0, Timer5Callback);
  myTimer.enable(true);

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

  while (vbl==vb) {};
}

void emu_DrawLine(unsigned char * VBuf, int width, int height, int line) 
{
  tft.writeLine(width,1,line, VBuf, palette16);
}  

void emu_DrawScreen(unsigned char * VBuf, int width, int height, int stride) 
{
  if (skip==0) {
    tft.writeScreen(width,height-TFT_VBUFFER_YCROP,stride, VBuf+(TFT_VBUFFER_YCROP/2)*stride, palette16);
  }
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
