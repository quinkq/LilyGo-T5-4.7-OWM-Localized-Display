#ifndef DRAWINGFUNCTIONS_H
#define DRAWINGFUNCTIONS_H

#include <Arduino.h>           // In-built
#include "esp_task_wdt.h"      // In-built
#include "freertos/FreeRTOS.h" // In-built
#include "freertos/task.h"     // In-built
#include "epd_driver.h"        // https://github.com/Xinyuan-LilyGO/LilyGo-EPD47
#include "esp_adc_cal.h"       // In-built

#include "lang.h"


#define White     0xFF
#define LightGrey 0xBB
#define Grey      0x88
#define DarkGrey  0x44
#define Black     0x00

#define autoscale_on  true
#define autoscale_off false
#define barchart_on   true
#define barchart_off  false

extern int wifi_signal;
extern int CurrentHour;
extern int CurrentMin;
extern int CurrentSec;
extern int EventCnt;
extern int vref;

//fonts
#include "opensans8b.h"
#include "opensansB10PL.h"
#include "opensansB12PL.h"
#include "opensansB16PL.h"
#include "opensansb18pl.h"
#include "opensansb24pl.h"
#include "opensansb28pl.h"
#include "opensansb40pl.h"
#include "opensansb50pl.h"

extern GFXfont currentFont;
extern uint8_t *framebuffer;

class IconSize{
public:
    int scale;
    int Offset;
    int linesize;

    IconSize(int s, int o, int l) : scale(s), Offset(o), linesize(l) {}
};

extern const IconSize SmallIcon;
extern const IconSize MediumIcon;
extern const IconSize ForecastIcon;
extern const IconSize LargeIcon;


enum alignment
{
    LEFT,
    RIGHT,
    CENTER
};



void DrawBattery(int x, int y, float voltage, uint8_t percentage);
void DrawRSSI(int x, int y, int rssi);

void addcloud(int x, int y, int scale, int linesize);
void addrain(int x, int y, int scale, const IconSize &size);
void addsnow(int x, int y, int scale, const IconSize &size);
void addtstorm(int x, int y, int scale);
void addsun(int x, int y, int scale, const IconSize &size);
void addfog(int x, int y, int scale, int linesize, const IconSize &size);

void Sunny(int x, int y, const IconSize &size, String IconName);
void MostlySunny(int x, int y, IconSize size, String IconName);
void MostlyCloudy(int x, int y, IconSize size, String IconName);
void Cloudy(int x, int y, IconSize size, String IconName);
void Rain(int x, int y, IconSize size, String IconName);
void ExpectRain(int x, int y, IconSize size, String IconName);
void ChanceRain(int x, int y, IconSize size, String IconName);
void Tstorms(int x, int y, IconSize size, String IconName);
void Snow(int x, int y, IconSize size, String IconName);
void Fog(int x, int y, IconSize size, String IconName);
void Haze(int x, int y, IconSize size, String IconName);

void CloudCover(int x, int y, int CCover);
void Visibility(int x, int y, String Visi);

void addmoon(int x, int y, int scale, const IconSize &size);
void Nodata(int x, int y, IconSize &size, String IconName);

void DrawGraph(int x_pos, int y_pos, int gwidth, int gheight, float Y1Min, float Y1Max, String title, float DataArray[], int readings, boolean auto_scale, boolean barchart_mode);
void arrow(int x, int y, int asize, float aangle, int pwidth, int plength);
void DrawSegment(int x, int y, int o1, int o2, int o3, int o4, int o11, int o12, int o13, int o14);
void DrawMoon(int x, int y, int dd, int mm, int yy, String hemisphere);

void drawString(int x, int y, String text, alignment align);
void fillCircle(int x, int y, int r, uint8_t color);
void drawFastHLine(int16_t x0, int16_t y0, int length, uint16_t color);
void drawFastVLine(int16_t x0, int16_t y0, int length, uint16_t color);
void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);
void drawCircle(int x0, int y0, int r, uint8_t color);
void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color);
void drawPixel(int x, int y, uint8_t color);

void setFont(GFXfont const &font);
String TitleCase(String text);

#endif // #endif