#ifndef LANG_H
#define LANG_H

#include <Arduino.h>
#include <ArduinoJson.h>

#define FONT(x) x##_tf

//Temperature - Humidity - Forecast
extern const String TXT_FORECAST_VALUES;
extern const String TXT_CONDITIONS;
extern const String TXT_DAYS;
extern const String TXT_TEMPERATURES;
extern const String TXT_TEMPERATURE_C;
extern const String TXT_TEMPERATURE_F;
extern const String TXT_HUMIDITY_PERCENT;

// Pressure
extern const String TXT_PRESSURE;
extern const String TXT_PRESSURE_HPA;
extern const String TXT_PRESSURE_IN;
extern const String TXT_PRESSURE_STEADY;
extern const String TXT_PRESSURE_RISING;
extern const String TXT_PRESSURE_FALLING;

//RainFall / SnowFall
extern const String TXT_RAINFALL_MM;
extern const String TXT_RAINFALL_IN;
extern const String TXT_SNOWFALL_MM;
extern const String TXT_SNOWFALL_IN;
extern const String TXT_PRECIPITATION_SOON;

//Sun
extern const String TXT_SUNRISE;
extern const String TXT_SUNSET;

//Moon
extern const String TXT_MOON_NEW;
extern const String TXT_MOON_WAXING_CRESCENT;
extern const String TXT_MOON_FIRST_QUARTER;
extern const String TXT_MOON_WAXING_GIBBOUS;
extern const String TXT_MOON_FULL;
extern const String TXT_MOON_WANING_GIBBOUS;
extern const String TXT_MOON_THIRD_QUARTER;
extern const String TXT_MOON_WANING_CRESCENT;

//Power / WiFi
extern const String TXT_POWER;
extern const String TXT_WIFI;
extern const char* TXT_UPDATED;

//Wind
extern const String TXT_WIND_SPEED_DIRECTION;
extern const String TXT_N;
extern const String TXT_NNE;
extern const String TXT_NE;
extern const String TXT_ENE;
extern const String TXT_E;
extern const String TXT_ESE;
extern const String TXT_SE;
extern const String TXT_SSE;
extern const String TXT_S;
extern const String TXT_SSW;
extern const String TXT_SW;
extern const String TXT_WSW;
extern const String TXT_W;
extern const String TXT_WNW;
extern const String TXT_NW;
extern const String TXT_NNW;

//Day of the week
extern const char* weekday_D[];

//Month
extern const char* month_M[];

#endif // LANG_H