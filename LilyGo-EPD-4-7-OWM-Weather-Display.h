
#ifndef LILYGO_EPD_47_OWM_WEATHER_DISPLAY_H
#define LILYGO_EPD_47_OWM_WEATHER_DISPLAY_H

#include <Arduino.h>           // In-built
#include "epd_driver.h"        // https://github.com/Xinyuan-LilyGO/LilyGo-EPD47
#include "drawingFunctions.h"
#include "lang.h"


void InitialiseDisplay();
void InitialiseSystem();
void queueLogData(const char *filename, float temperature, float humidity, float pressure, int32_t timestamp);
boolean SetTime();
boolean UpdateLocalTime();
String ConvertUnixTimeForDisplay(int32_t unix_time);
uint8_t StartWiFi();
void StopWiFi();

void RenderGeneralInfoSection();
void RenderWeatherIcon(int x, int y);
void RenderMainWeatherSection(int x, int y);
void RenderWindSection(int x, int y, float angle, float windspeed, int Cradius);

String WindDegToOrdinalDirection(float winddirection);

void RenderTemperatureSection(int x, int y);
void RenderSensorReadings(int x, int y);
void RenderSensorReadingsGarden(int x, int y);
void RenderSensorReadingsRoom(int x, int y);
void RenderForecastTextSection(int x, int y);
void RenderPressureSection(int x, int y, float pressure, String slope);
void RenderForecastWeather(int x, int y, int index);
void RenderAstronomySection(int x, int y);
void RenderGraphs();

String MoonPhase(int d, int m, int y, String hemisphere);

void RenderForecastSection(int x, int y);
void RenderConditionsSection(int x, int y, String IconName, IconSize size);
void DrawPressureAndTrend(int x, int y, float pressure, String slope);

void RenderStatusSection(int x, int y, int rssi);


void edp_update();

float SumOfPrecip(float DataArray[], int readings);
void Convert_Readings_to_Imperial();
float mm_to_inches(float value_mm);
float hPa_to_inHg(float value_hPa);
double NormalizedMoonPhase(int d, int m, int y);
int JulianDate(int d, int m, int y);

#endif // #endif