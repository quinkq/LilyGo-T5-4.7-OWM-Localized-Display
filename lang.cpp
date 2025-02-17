#include "lang.h"

//Temperature - Humidity - Forecast
const String TXT_FORECAST_VALUES  = "Prognoza na 3 dni";
const String TXT_CONDITIONS       = "Warunki";
const String TXT_DAYS             = "(Dni)";
const String TXT_TEMPERATURES     = "Temperatura";
const String TXT_TEMPERATURE_C    = "Temperatura (°C)";
const String TXT_TEMPERATURE_F    = "Temperatura (°F)";
const String TXT_HUMIDITY_PERCENT = "Wilgotnosc (%)";

// Pressure
const String TXT_PRESSURE         = "Cisnienie";
const String TXT_PRESSURE_HPA     = "Cisnienie (hPa)";
const String TXT_PRESSURE_IN      = "Cisnienie (in)";
const String TXT_PRESSURE_STEADY  = "Stabilne";
const String TXT_PRESSURE_RISING  = "Rosnie";
const String TXT_PRESSURE_FALLING = "Spada";

//RainFall / SnowFall
const String TXT_RAINFALL_MM = "Opady deszczu (mm)";
const String TXT_RAINFALL_IN = "Opady deszczu (in)";
const String TXT_SNOWFALL_MM = "Opady sniegu (mm)";
const String TXT_SNOWFALL_IN = "Opady sniegu (in)";
const String TXT_PRECIPITATION_SOON = "Opady wkrotce";

//Sun
const String TXT_SUNRISE  = "Wschod";
const String TXT_SUNSET   = "Zachod";

//Moon
const String TXT_MOON_NEW             = "Nowiu";
const String TXT_MOON_WAXING_CRESCENT = "Wzrastajacy sierp";
const String TXT_MOON_FIRST_QUARTER   = "Pierwsza kwadra";
const String TXT_MOON_WAXING_GIBBOUS  = "Wzrastajacy garb";
const String TXT_MOON_FULL            = "Pelnia";
const String TXT_MOON_WANING_GIBBOUS  = "Malejacy garb";
const String TXT_MOON_THIRD_QUARTER   = "Trzecia kwadra";
const String TXT_MOON_WANING_CRESCENT = "Malejacy sierp";

//Power / WiFi
const String TXT_POWER  = "Zasilanie";
const String TXT_WIFI   = "Wi-Fi";
const char* TXT_UPDATED = "Aktualizacja:";

//Wind
const String TXT_WIND_SPEED_DIRECTION = "Predkość wiatru / Kierunek";
const String TXT_N   = "N";
const String TXT_NNE = "NNE";
const String TXT_NE  = "NE";
const String TXT_ENE = "ENE";
const String TXT_E   = "E";
const String TXT_ESE = "ESE";
const String TXT_SE  = "SE";
const String TXT_SSE = "SSE";
const String TXT_S   = "S";
const String TXT_SSW = "SSW";
const String TXT_SW  = "SW";
const String TXT_WSW = "WSW";
const String TXT_W   = "W";
const String TXT_WNW = "WNW";
const String TXT_NW  = "NW";
const String TXT_NNW = "NNW";

//Day of the week
const char* weekday_D[] = { "Nd", "Pon", "Wt", "Sr", "Czw", "Pt", "Sob" };

//Month
const char* month_M[] = { "Sty", "Lut", "Mar", "Kwi", "Maj", "Cze", "Lip", "Sie", "Wrz", "Paz", "Lis", "Gru" };