#include "display_dma.h"
#include <Adafruit_Arcada.h>
extern Adafruit_Arcada arcada;


#define KEYMAP_PRESENT 1

extern "C" {
  #include "emuapi.h"
  #include "iopins.h"  
}

#include "logo.h"
#include "bmpjoy.h"
#include "bmpvbar.h"
#include "bmpvga.h"
#include "bmptft.h"


extern Display_DMA tft;
static File file;

#define TEXT_HEIGHT         16
#define TEXT_WIDTH          10
#define MAX_FILENAME_SIZE   ((ARCADA_TFT_WIDTH / TEXT_WIDTH) - 2)
#define MAX_MENULINES       ((ARCADA_TFT_HEIGHT / TEXT_HEIGHT) - 2)
#define MENU_FILE_XOFFSET   (2*TEXT_WIDTH)
#define MENU_FILE_YOFFSET   (2*TEXT_HEIGHT)
#define MENU_FILE_W         (MAX_FILENAME_SIZE*TEXT_WIDTH)
#define MENU_FILE_H         (MAX_MENULINES*TEXT_HEIGHT)
#define MENU_FILE_BGCOLOR   RGBVAL16(0x00,0x00,0x20)
#define MENU_JOYS_YOFFSET   (12*TEXT_HEIGHT)
#define MENU_VBAR_XOFFSET   (0*TEXT_WIDTH)
#define MENU_VBAR_YOFFSET   (MENU_FILE_YOFFSET)

#define MENU_TFT_XOFFSET    (MENU_FILE_XOFFSET+MENU_FILE_W+8)
#define MENU_TFT_YOFFSET    (MENU_VBAR_YOFFSET+32)

static bool menuOn=true;
static bool menuRedraw=true;
static int nbFiles=0;
static int curFile=0;
static int topFile=0;
static char selection[MAX_FILENAME_SIZE+1]="";
static uint8_t prev_zt=0; 

void toggleMenu(bool on) {
  if (on) {
    menuOn=true;
    menuRedraw=true;  
    arcada.fillScreen(ARCADA_BLACK);
    arcada.setTextColor(ARCADA_WHITE, ARCADA_BLUE);
    arcada.setTextSize(2);
    arcada.setTextWrap(false);
    arcada.setCursor(0, 0);
    arcada.print(TITLE);
    //arcada.drawRGBBitmap(MENU_VBAR_XOFFSET, MENU_VBAR_YOFFSET, (uint16_t*)bmpvbar+2, ((uint16_t*)bmpvbar)[0], ((uint16_t*)bmpvbar)[1]);
  } else {
    menuOn = false;    
  }
}


bool menuActive(void) 
{
  return (menuOn);
}

int handleMenu(uint16_t bClick)
{
  int action = ACTION_NONE;

  File entry = arcada.open(selection);

  char c = 255;

  if ( (bClick & ARCADA_BUTTONMASK_A)  && (entry.isDirectory()) ) {
      menuRedraw=true;
      arcada.chdir(selection);
      curFile = 0;
      nbFiles = arcada.filesysListFiles();     
  }
  else if (bClick & ARCADA_BUTTONMASK_START) {
      menuRedraw=true;
      action = ACTION_RUNTFT;       
  }
  else if (bClick & ARCADA_BUTTONMASK_UP) {
    if (curFile!=0) {
      menuRedraw=true;
      curFile--;
    }
  } 
  else if (bClick & ARCADA_BUTTONMASK_DOWN)  {
    if ((curFile<(nbFiles-1)) && (nbFiles)) {
      curFile++;
      menuRedraw=true;
    }
  }
    
  if (menuRedraw && nbFiles) {
         
    int fileIndex = 0;
    char filename[MAX_FILENAME_SIZE+1];
    File entry;    
    file = arcada.open(); 
    arcada.fillRect(MENU_FILE_XOFFSET,MENU_FILE_YOFFSET, MENU_FILE_W, MENU_FILE_H, MENU_FILE_BGCOLOR);
//    if (curFile <= (MAX_MENULINES/2-1)) topFile=0;
//    else topFile=curFile-(MAX_MENULINES/2);
    if (curFile <= (MAX_MENULINES-1)) topFile=0;
    else topFile=curFile-(MAX_MENULINES/2);

    //Serial.print("curfile: ");
    //Serial.println(curFile);
    //Serial.print("topFile: ");
    //Serial.println(topFile);
    
    int i=0;
    while (i<MAX_MENULINES) {
      entry = file.openNextFile();
      if (!entry) {
          // no more files
          break;
      }  
      entry.getName(&filename[0], MAX_FILENAME_SIZE);     
      if ( (!entry.isDirectory()) || ((entry.isDirectory()) && (strcmp(filename,".")) && (strcmp(filename,"..")) ) ) {
        if (fileIndex >= topFile) {              
          if ((i+topFile) < nbFiles ) {
            arcada.setTextSize(2);
            arcada.setCursor(MENU_FILE_XOFFSET,i*TEXT_HEIGHT+MENU_FILE_YOFFSET);
            if ((i+topFile)==curFile) {
              arcada.setTextColor(ARCADA_YELLOW, ARCADA_RED);
              arcada.print(filename);
              strcpy(selection,filename);            
            } else {
              arcada.setTextColor(ARCADA_WHITE, ARCADA_BLACK);
              arcada.print(filename);
            }
          }
          i++; 
        }
        fileIndex++;    
      }
      entry.close();
    }

    file.close();
    menuRedraw=false;     
  }

  return (action);  
}

char * menuSelection(void)
{
  return (selection);  
}
  


void emu_init(void)
{
  Serial.begin(115200);

  nbFiles = arcada.filesysListFiles();

  Serial.print("SD initialized, files found: ");
  Serial.println(nbFiles);

  toggleMenu(true);
}


void emu_printf(const char * format)
{
  Serial.println(format);
}

void emu_printf(int val)
{
  Serial.println(val);
}

void emu_printi(int val)
{
  Serial.println(val);
}

void * emu_Malloc(int size)
{
  void * retval =  malloc(size);
  if (!retval) {
    emu_printf("failed to allocate ");
    emu_printf(size);
  }
  else {
    emu_printf("could allocate ");
    emu_printf(size);    
  }
  
  return retval;
}

void emu_Free(void * pt)
{
  free(pt);
}

int emu_FileOpen(char * filename)
{
  int retval = 0;
  emu_printf("FileOpen...");
  emu_printf(filename);
  
  if (file = arcada.open(filename, O_READ)) {
    retval = 1;  
  } else {
    emu_printf("FileOpen failed");
  }
  return (retval);
}

int emu_FileRead(char * buf, int size)
{
  int retval = file.read(buf, size);
  if (retval != size) {
    emu_printf("FileRead failed");
  }
  return (retval);     
}

unsigned char emu_FileGetc(void) {
  unsigned char c;
  int retval = file.read(&c, 1);
  if (retval != 1) {
    emu_printf("emu_FileGetc failed");
  }  
  return c; 
}


void emu_FileClose(void)
{
  file.close();  
}

int emu_FileSize(char * filename) 
{
  int filesize=0;
  emu_printf("FileSize...");
  emu_printf(filename);

  if (file = arcada.open(filename, O_READ)) 
  {
    emu_printf("filesize is...");
    filesize = file.fileSize(); 
    emu_printf(filesize);
    file.close();    
  }
 
  return(filesize);  
}

int emu_FileSeek(int pos) 
{
  file.seek(pos);
  return pos;
}

int emu_LoadFile(char * filename, char * buf, int numbytes) {
  int filesize = 0;
    
  emu_printf("LoadFile...");
  emu_printf(filename);
  
  if (file = arcada.open(filename, O_READ)) {
    filesize = file.fileSize(); 
    emu_printf(filesize);
    if (numbytes >= filesize)
    {
      if (file.read(buf, filesize) != filesize) {
        emu_printf("File read failed");
      }        
    }
    file.close();
  }
  
  return(filesize);
}

int emu_LoadFileSeek(char * filename, char * buf, int numbytes, int pos) {
  int filesize = 0;

  emu_printf("LoadFileSeek...");
  emu_printf(filename);
  
  if (file = arcada.open(filename, O_READ)) 
  {
    file.seek(pos);
    emu_printf(numbytes);
    if (file.read(buf, numbytes) != numbytes) {
      emu_printf("File read failed");
    }        
    file.close();
  }
  
  return(filesize);
}

static uint16_t bLastState;
uint16_t emu_DebounceLocalKeys(void) {
  uint16_t bCurState = arcada.readButtons();

  uint16_t bClick = bCurState & ~bLastState;
  bLastState = bCurState;

  return (bClick);
}

uint32_t emu_ReadKeys(void)  {
  return arcada.readButtons();
}
