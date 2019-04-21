#include "ili9341_samd51.h"
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


extern ILI9341_t3DMA tft;
static File file;
static char romspath[64];

#define MAX_FILENAME_SIZE   28
#define MAX_MENULINES       9
#define TEXT_HEIGHT         16
#define TEXT_WIDTH          8
#define MENU_FILE_XOFFSET   (6*TEXT_WIDTH)
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

static int readNbFiles(void) {
  int totalFiles = 0;
  char filename[MAX_FILENAME_SIZE+1];
  File entry;  
  file = arcada.open(romspath);
  while (true) {
    entry = file.openNextFile();
    if (! entry) {
      // no more files
      break;
    }
    if (!entry.isDirectory())  {
      totalFiles++;
    }
    else {
      entry.getName(&filename[0], MAX_FILENAME_SIZE); 
      if ( (strcmp(filename,".")) && (strcmp(filename,"..")) ) {
        totalFiles++;
      }
    }  
    entry.close();
  }
  file.close();
  return totalFiles;  
}


void toggleMenu(bool on) {
  if (on) {
    menuOn=true;
    menuRedraw=true;  
    tft.fillScreenNoDma(RGBVAL16(0x00,0x00,0x00));
    tft.drawTextNoDma(0,0, TITLE, RGBVAL16(0x00,0xff,0xff), RGBVAL16(0x00,0x00,0xff), true);  
    tft.drawSpriteNoDma(MENU_VBAR_XOFFSET,MENU_VBAR_YOFFSET,(uint16_t*)bmpvbar);
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

  char newpath[80];
  strcpy(newpath, romspath);
  strcat(newpath, "/");
  strcat(newpath, selection);
  File entry = arcada.open(newpath);

  char c = 255;

  if ( (bClick & ARCADA_BUTTONMASK_A)  && (entry.isDirectory()) ) {
      menuRedraw=true;
      strcpy(romspath,newpath);
      curFile = 0;
      nbFiles = readNbFiles();     
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
    file = arcada.open(romspath); 
    tft.drawRectNoDma(MENU_FILE_XOFFSET,MENU_FILE_YOFFSET, MENU_FILE_W, MENU_FILE_H, MENU_FILE_BGCOLOR);
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
            if ((i+topFile)==curFile) {
              tft.drawTextNoDma(MENU_FILE_XOFFSET,i*TEXT_HEIGHT+MENU_FILE_YOFFSET, filename, RGBVAL16(0xff,0xff,0x00), RGBVAL16(0xff,0x00,0x00), true);
              strcpy(selection,filename);            
            }
            else {
              tft.drawTextNoDma(MENU_FILE_XOFFSET,i*TEXT_HEIGHT+MENU_FILE_YOFFSET, filename, 0xFFFF, 0x0000, true);      
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
  //while (!Serial) {}

  if (!arcada.begin()) {
    emu_printf("Couldn't init arcada");
    while (1);
  }
  if (!arcada.filesysBegin()) {
    emu_printf("Filesystem failed");
    while (1);
  }
  strcpy(romspath,ROMSDIR);
  nbFiles = readNbFiles(); 

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

  char filepath[80];
  strcpy(filepath, romspath);
  strcat(filepath, "/");
  strcat(filepath, filename);
  emu_printf("FileOpen...");
  emu_printf(filepath);
  
  if (file.open(filepath, O_READ)) {
    retval = 1;  
  }
  else {
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
  char filepath[80];
  strcpy(filepath, romspath);
  strcat(filepath, "/");
  strcat(filepath, filename);
  emu_printf("FileSize...");
  emu_printf(filepath);

  if (file.open(filepath, O_READ)) 
  {
    emu_printf("filesize is...");
    filesize = file.fileSize(); 
    emu_printf(filesize);
    file.close();    
  }
 
  return(filesize);  
}

int emu_FileSeek(int seek) 
{
  file.seek(seek);
  return (seek);
}

int emu_LoadFile(char * filename, char * buf, int size)
{
  int filesize = 0;
    
  char filepath[80];
  strcpy(filepath, romspath);
  strcat(filepath, "/");
  strcat(filepath, filename);
  emu_printf("LoadFile...");
  emu_printf(filepath);
  
  if (file.open(filepath, O_READ)) 
  {
    filesize = file.fileSize(); 
    emu_printf(filesize);
    if (size >= filesize)
    {
      if (file.read(buf, filesize) != filesize) {
        emu_printf("File read failed");
      }        
    }
    file.close();
  }
  
  return(filesize);
}

int emu_LoadFileSeek(char * filename, char * buf, int size, int seek)
{
  int filesize = 0;
    
  char filepath[80];
  strcpy(filepath, romspath);
  strcat(filepath, "/");
  strcat(filepath, filename);
  emu_printf("LoadFileSeek...");
  emu_printf(filepath);
  
  if (file.open(filepath, O_READ)) 
  {
    file.seek(seek);
    emu_printf(size);
    if (file.read(buf, size) != size) {
      emu_printf("File read failed");
    }        
    file.close();
  }
  
  return(filesize);
}

static int keypadval=0; 
static boolean joySwapped = false;
static uint16_t bLastState;

unsigned short emu_DebounceLocalKeys(void)
{
  uint16_t bCurState = arcada.readButtons();

  uint16_t bClick = bCurState & ~bLastState;
  bLastState = bCurState;

  return (bClick);
}

int emu_GetPad(void) 
{
  return(keypadval|((joySwapped?1:0)<<7));
}

int emu_ReadKeys(void) 
{
  return arcada.readButtons();
}
