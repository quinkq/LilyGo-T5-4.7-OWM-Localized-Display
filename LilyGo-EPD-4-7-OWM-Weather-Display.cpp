// ESP32 Weather Display and a LilyGo EPD 4.7" Display, obtains Open Weather Map data, decodes and then displays it.
// This software, the ideas and concepts is Copyright (c) David Bird 2021. All rights to this software are reserved.
// #################################################################################################################

#include <Arduino.h>           // In-built
#include <esp_task_wdt.h>      // In-built
#include "freertos/FreeRTOS.h" // In-built
#include "freertos/task.h"     // In-built
#include "epd_driver.h"        // https://github.com/Xinyuan-LilyGO/LilyGo-EPD47
#include "esp_adc_cal.h"       // In-built
#include "i2s_data_bus.h"      // In-built          //////////////////////////////////////////////////////////////////////

#include <ArduinoJson.h> // https://github.com/bblanchon/ArduinoJson
#include <HTTPClient.h>  // In-built

#include <SPI.h>  // In-built
#include <time.h> // In-built
#include "SPIFFS.h"
#include "FS.h"

// Sensor reading tasks
#include "sht4x.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_system.h"
#include "esp_err.h"

// Misc / initial configuration webserver
#include "owm_credentials.h"
#include "forecast_record.h"
#include "web.h"
#include "LilyGo-EPD-4-7-OWM-Weather-Display.h"

//Webserver / logging
#include <SD.h>
#include <logWebServer.h>
//!
//#include <ESPAsyncWebServer.h>

//Power saving
#include "esp_pm.h"
#include "driver/rtc_io.h"
#include "driver/adc.h"


#if T5_47_PLUS_V2
#define USR_BUTTON GPIO_NUM_10
#define I2C_MASTER_SDA GPIO_NUM_17
#define I2C_MASTER_SCL GPIO_NUM_18
// Define SD card pins for LilyGo T5 4.7
/*
#define SD_CS GPIO_NUM_42
#define SD_SCLK GPIO_NUM_11
#define SD_MOSI GPIO_NUM_15
#define SD_MISO GPIO_NUM_16
*/
#elif CONFIG_IDF_TARGET_ESP32
#define USR_BUTTON GPIO_NUM_21
#elif CONFIG_IDF_TARGET_ESP32S3
#define USR_BUTTON GPIO_NUM_21
#endif

#define SCREEN_WIDTH EPD_WIDTH
#define SCREEN_HEIGHT EPD_HEIGHT
#define SECONDS_TO_TICKS(seconds) pdMS_TO_TICKS((seconds) * 1000) // converting seconds from miliseconds
#define MINUTES_TO_TICKS(minutes) pdMS_TO_TICKS((minutes) * 60 * 1000) // converting minutes to seconds and then miliseconds

//################  VERSION  ##################################################
String version = "2.5 / 4.7in"; // Programme version, see change log at end

//################ PROGRAM VARIABLES and OBJECTS ##############################

String Time_str = "--:--:--";
String Date_str = "-- --- ----";

#define max_readings 8 // (WAS 24!) Limited to 3-days here, but could go to 5-days = 40

//RTC_DATA_ATTR
RTC_DATA_ATTR int screenState = 0; // default screen state

Forecast_record_type WxConditions[1];
Forecast_record_type WxForecast[max_readings];

float pressure_readings[max_readings]    = {0};
float temperature_readings[max_readings] = {0};
float humidity_readings[max_readings]    = {0};
float rain_readings[max_readings]        = {0};
float snow_readings[max_readings]        = {0};

// obsolete - now calculated in Idle task //const int SleepDuration = 2; // Sleep time in minutes, aligned to the nearest minute boundary, so if 30 will always update at 00 or 30 past the hour
bool SleepHoursEnabled = false;
bool DeepSleepEnabled = false;
int WakeupHour    = 5;  // Don't wakeup until after 07:00 to save battery power
int SleepHour     = 1; // Sleep after 23:00 to save battery power
long StartTime     = 0;
long SleepTimer    = 0;
// obsolete //long Delta         = 30; // ESP32 rtc speed compensation, prevents display at xx:59:yy and then xx:00:yy (one minute later) to save power

uint64_t IdleStartTime = 0;
bool skipOWMUpdate = false;

// Semaphore handles
SemaphoreHandle_t configSemaphore;
SemaphoreHandle_t idleEndedSem;
SemaphoreHandle_t ButtonWakeSem;
SemaphoreHandle_t ESPNowWakeSem;
SemaphoreHandle_t dataExhangeCompleteSem;
SemaphoreHandle_t sht4xTriggerSem;
SemaphoreHandle_t sht4xCompleteSem;
SemaphoreHandle_t i2cMutex;

// Queue handles
QueueHandle_t sensorDataQueue;
QueueHandle_t logQueue;
QueueSetHandle_t queueSet = xQueueCreateSet(2);

// Global variables for sensor readings
typedef struct {
    float temperature;
    float humidity;
    float pressure;
    int32_t timestamp;
} SensorData;

SensorData localData = {0};
SensorData receivedData = {0};

// WiFi / ESP-NOW transmission settings
uint8_t receiverMac[] = {0x84, 0xF7, 0x03, 0x3A, 0xA8, 0x58};
bool dataReceivedFlag = false;
bool timeIsSet = false;
int WiFiStatus = WL_DISCONNECTED;

// Web logserver
#define MAX_ENTRIES 365

File insideFile, outsideFile;

// Struct for storing sensor data in the queue
typedef struct {
    int32_t timestamp;
    float temperature;
    float humidity;
    float pressure;
    char filename[20];  // Name of the file to store data
} SensorLogEntry;



//################ ISR ##################################################

// Button ISR
volatile unsigned long lastButtonPress = 0;
const unsigned long DEBOUNCE_TIME = 200;
void IRAM_ATTR ButtonISR()
{
    unsigned long currentTime = millis();
    if ((currentTime - lastButtonPress) >= DEBOUNCE_TIME) 
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(ButtonWakeSem, &xHigherPriorityTaskWoken);
        lastButtonPress = currentTime;
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

// ESP-NOW Callback Function
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) 
{
    ESP_LOGI("ESP-NOW", "ESP-NOW - packet received");

    // Verify and ignore false interrupt triggers
    if (len == 0 || incomingData == NULL)
    {
        ESP_LOGW("ESP-NOW", "WARNING: Received empty or invalid ESP-NOW packet, ignoring.");
        return;
    }
    else
    {
        ESP_LOGI("ESP-NOW", "Packet valid, breaking the idle.");
        xSemaphoreGiveFromISR(ESPNowWakeSem, NULL); // Wake up from ESP-NOW
        memcpy(&receivedData, incomingData, sizeof(SensorData));
        queueLogData("/outside_log.csv", receivedData.temperature, receivedData.humidity, receivedData.pressure, receivedData.timestamp);
        ESP_LOGI("ESP-NOW", "Received ESP-NOW Data: Temp: %.2f, Humidity: %.1f, Pressure: %.1f", receivedData.temperature, receivedData.humidity, receivedData.pressure);
        dataReceivedFlag = true;

        // Send the most recent NTP time back to ESP32-C3
        struct tm timeinfo;
        getLocalTime(&timeinfo);
        SensorData responseData = {0};
        Serial.printf("Time sent: %s\n", asctime(&timeinfo));
        responseData.timestamp = (int32_t) time(NULL);

        // Verify time correctness and as an respose send it for ntp update
        if (responseData.timestamp < 1700000000)
        {
            ESP_LOGE("ESP-NOW", "ERROR: Received invalid timestamp! Aborting sending.");
            xSemaphoreGiveFromISR(dataExhangeCompleteSem, NULL);
            return;
        }
        else
        {
            ESP_LOGI("ESP-NOW", "Sending sensor data...");
            esp_err_t result = esp_now_send(receiverMac, (uint8_t *)&responseData, sizeof(SensorData));
            if (result == ESP_OK) 
            {
                ESP_LOGI("ESP-NOW", "Successfully sent NTP timestamp: %" PRId32, responseData.timestamp);
            }

            xSemaphoreGiveFromISR(dataExhangeCompleteSem, NULL);    
        }
    }
}

//################ FUNCTIONS ##################################################

void setupSDCard() 
{
    Serial.println("Initializing SD card...");
    if (!SD.begin(SD_CS, SPI)) {
        Serial.println("SD Card initialization failed!");
        return;
    }
    delay(500); // Allow time for SD to stabilize
    uint64_t cardSize = SD.cardSize();
    if (cardSize == 0) {
        Serial.println("Error: SD Card size not detected correctly!");
    } else {
        Serial.printf("SD Card initialized. Size: %.2f GB\n", cardSize / 1024.0 / 1024.0 / 1024.0);
    }
}

// Maintaining circular buffer by limiting entries to 365
void maintainCircularBuffer(const char *filename) {
    File file = SD.open(filename, FILE_READ);
    if (!file) {
        Serial.printf("Failed to open %s for buffer check!\n", filename);
        return;
    }

    // Count lines
    int lineCount = 0;
    while (file.available()) {
        file.readStringUntil('\n'); // Read each line
        lineCount++;
    }
    file.close();

    // If we exceed max entries, remove the oldest
    if (lineCount > MAX_ENTRIES) {
        Serial.printf("Trimming %s: Found %d entries, keeping last %d\n", filename, lineCount, MAX_ENTRIES);

        File tempFile = SD.open("/temp.csv", FILE_WRITE);
        file = SD.open(filename, FILE_READ);
        for (int i = lineCount - MAX_ENTRIES; i > 0; i--) {
            file.readStringUntil('\n'); // Skip older lines
        }

        // Copy only the latest 365 entries
        while (file.available()) {
            tempFile.println(file.readStringUntil('\n'));
        }

        file.close();
        tempFile.close();
        SD.remove(filename);             // Delete old file
        SD.rename("/temp.csv", filename); // Replace with trimmed file
    }
}

// Queing most recent sensor data for logging
void queueLogData(const char *filename, float temperature, float humidity, float pressure, int32_t timestamp) 
{
    SensorLogEntry logEntry;
    logEntry.temperature = temperature;
    logEntry.humidity = humidity;
    logEntry.pressure = pressure;
    logEntry.timestamp = timestamp;
    strncpy(logEntry.filename, filename, sizeof(logEntry.filename) - 1);

    if (xQueueSend(logQueue, &logEntry, portMAX_DELAY) != pdTRUE) {
        Serial.println("Log queue is full! Data lost.");
    }
}

void InitialiseDisplay()
{
    epd_init();
    
    framebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
    if (!framebuffer)
        Serial.println("Memory alloc failed!");
    memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
}

void InitialiseSystem()
{
    StartTime = millis();
    Serial.begin(115200);

    unsigned long serialTimeout = millis(); // if serial hasn't started, wait for it 3000 
    while (!Serial && (millis() - serialTimeout < 3000))
    {
        delay(200);
    }
    Serial.println(String(__FILE__) + "\nInitializing System...");
    
    SPI.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS);  // Initialise SPI bus for SD card
    ESP_ERROR_CHECK(i2cdev_init()); // Initialize the I2C bus
    //delay(1000);
    InitialiseDisplay();
    setupSDCard();
}

boolean SetTime()
{
    const char* ntpServerCStr = ntpServer.c_str();  // Convert String to const char*
    configTime(0, 0, const_cast<const char *>(ntpServer.c_str()), ntpServerCStr); //(gmtOffset_sec, daylightOffset_sec, ntpServer)
    setenv("TZ", const_cast<const char *>(Timezone.c_str()), 1);                                                 //setenv()adds the "TZ" variable to the environment with a value TimeZone, only used if set to 1, 0 means no change
    tzset();                                                                   // Set the TZ environment variable
    delay(100);
    return UpdateLocalTime();
}



boolean UpdateLocalTime()
{
    struct tm timeinfo;
    char time_output[10], day_output[30];

    // Wait up to 5 seconds for time sync
    if (!getLocalTime(&timeinfo, 5000)) {
        Serial.println("Failed to obtain time");
        return false;
    }

    // Store individual values
    CurrentHour = timeinfo.tm_hour;
    CurrentMin = timeinfo.tm_min;
    CurrentSec = timeinfo.tm_sec;

    // Print full date & time for debugging
    Serial.printf("Current date: %s\n", asctime(&timeinfo));

    // Format date based on unit system
    if (Units == "M") {
        //strftime(day_output, sizeof(day_output), "%A, %d %B", &timeinfo);  // "Saturday, 24 June 2023"
        sprintf(day_output, "%s, %02u %s %04u", weekday_D[timeinfo.tm_wday], timeinfo.tm_mday, month_M[timeinfo.tm_mon], (timeinfo.tm_year) + 1900);
        strftime(time_output, sizeof(time_output), "%H:%M:%S", &timeinfo);    // "14:05:49"
    } else {
        strftime(day_output, sizeof(day_output), "%a %b-%d-%Y", &timeinfo);   // "Sat Jun-24-2023"
        strftime(time_output, sizeof(time_output), "%I:%M:%S %p", &timeinfo); // "02:05:49 PM"
    }

    // Save formatted strings
    Date_str = day_output;
    Time_str = time_output;

    return true;
}

String ConvertUnixTimeForDisplay(int32_t unix_time)
{
    // Returns either '21:12  ' or ' 09:12pm' depending on Units mode
    struct tm local_time;
    localtime_r((time_t *)&unix_time, &local_time);
    char output[40];
    if (Units == "M")
    {
        strftime(output, sizeof(output), "%H:%M %d/%m/%y", &local_time);
    }
    else
    {
        strftime(output, sizeof(output), "%I:%M%P %m/%d/%y", &local_time);
    }
    return output;
}

uint8_t StartWiFi()
{
    Serial.println("\r\nConnecting to: " + String(ssid));
    IPAddress dns(8, 8, 8, 8); // Use Google DNS

    WiFi.persistent(false); 
    WiFi.softAPdisconnect(true);  
    WiFi.mode(WIFI_STA);  // Start only in STA mode first

    WiFi.disconnect();
    WiFi.setAutoConnect(true);
    WiFi.setAutoReconnect(true);
    WiFi.begin(const_cast<const char *>(ssid.c_str()), const_cast<const char *>(password.c_str()));

    if (WiFi.waitForConnectResult() != WL_CONNECTED)
    {
        Serial.printf("STA: Failed!\n");
        WiFi.disconnect(false);
        delay(500);
        WiFi.begin(const_cast<const char *>(ssid.c_str()), const_cast<const char *>(password.c_str()));
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        wifi_signal = WiFi.RSSI();
        Serial.println("WiFi connected at: " + WiFi.localIP().toString());
    }
    else
    {
        Serial.println("WiFi connection *** FAILED ***");
    }

    // Get current Wi-Fi channel
    wifi_second_chan_t secondChan;
    uint8_t channel;
    esp_wifi_get_channel(&channel, &secondChan);

    // Start ESP-NOW AP on the same channel
    WiFi.softAP("ESP-NOW", NULL, channel, true, 1);
    Serial.printf("ESP-NOW AP Started (Hidden) on Channel %d\n", channel);

    // Ensure ESP-NOW channel is synced
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    Serial.printf("WiFi/ESP-NOW channel set: %d\n", channel);

    return WiFi.status();
}

void StopWiFi()
{
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    Serial.println("WiFi switched Off");
}

bool DecodeWeather(WiFiClient &json, String Type)
{
    Serial.print(F("\nCreating object...and "));
    DynamicJsonDocument doc(64 * 1024);                      // allocate the JsonDocument
    DeserializationError error = deserializeJson(doc, json); // Deserialize the JSON document
    if (error)
    { // Test if parsing succeeds.
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.c_str());
        return false;
    }
    // convert it to a JsonObject
    JsonObject root = doc.as<JsonObject>();
    Serial.println(" Decoding " + Type + " data");
    if (Type == "weather")
    {
        // All Serial.println statements are for diagnostic purposes and some are not required, remove if not needed with //
        //WxConditions[0].lon         = root["coord"]["lon"].as<float>();              Serial.println(" Lon: " + String(WxConditions[0].lon));
        //WxConditions[0].lat         = root["coord"]["lat"].as<float>();              Serial.println(" Lat: " + String(WxConditions[0].lat));
        WxConditions[0].Main0 = root["weather"][0]["main"].as<const char *>();
        //Serial.println("Main: " + String(WxConditions[0].Main0));
        WxConditions[0].Forecast0 = root["weather"][0]["description"].as<const char *>();
        //Serial.println("For0: " + String(WxConditions[0].Forecast0));
        //WxConditions[0].Forecast1   = root["weather"][1]["description"].as<char*>(); Serial.println("For1: " + String(WxConditions[0].Forecast1));
        //WxConditions[0].Forecast2   = root["weather"][2]["description"].as<char*>(); Serial.println("For2: " + String(WxConditions[0].Forecast2));
        WxConditions[0].Icon = root["weather"][0]["icon"].as<const char *>();
        //Serial.println("Icon: " + String(WxConditions[0].Icon));
        WxConditions[0].Temperature = root["main"]["temp"].as<float>();
        //Serial.println("Temp: " + String(WxConditions[0].Temperature));
        WxConditions[0].Pressure = root["main"]["pressure"].as<float>();
        //Serial.println("Pres: " + String(WxConditions[0].Pressure));
        WxConditions[0].Humidity = root["main"]["humidity"].as<float>();
        //Serial.println("Humi: " + String(WxConditions[0].Humidity));
        WxConditions[0].Low = root["main"]["temp_min"].as<float>();
        //Serial.println("TLow: " + String(WxConditions[0].Low));
        WxConditions[0].High = root["main"]["temp_max"].as<float>();
        //Serial.println("THig: " + String(WxConditions[0].High));
        WxConditions[0].Windspeed = root["wind"]["speed"].as<float>();
        //Serial.println("WSpd: " + String(WxConditions[0].Windspeed));
        WxConditions[0].Winddir = root["wind"]["deg"].as<float>();
        //Serial.println("WDir: " + String(WxConditions[0].Winddir));
        WxConditions[0].Cloudcover = root["clouds"]["all"].as<int>();
        //Serial.println("CCov: " + String(WxConditions[0].Cloudcover)); // in % of cloud cover
        WxConditions[0].Visibility = root["visibility"].as<int>();
        //Serial.println("Visi: " + String(WxConditions[0].Visibility)); // in metres
        WxConditions[0].Rainfall = root["rain"]["1h"].as<float>();
        //Serial.println("Rain: " + String(WxConditions[0].Rainfall));
        WxConditions[0].Snowfall = root["snow"]["1h"].as<float>();
        //Serial.println("Snow: " + String(WxConditions[0].Snowfall));
        //WxConditions[0].Country     = root["sys"]["country"].as<char*>();            Serial.println("Ctry: " + String(WxConditions[0].Country));
        WxConditions[0].Sunrise = root["sys"]["sunrise"].as<int>();
        //Serial.println("SRis: " + String(WxConditions[0].Sunrise));
        WxConditions[0].Sunset = root["sys"]["sunset"].as<int>();
        //Serial.println("SSet: " + String(WxConditions[0].Sunset));
        WxConditions[0].Timezone = root["timezone"].as<int>();
        //Serial.println("TZon: " + String(WxConditions[0].Timezone));
    }
    if (Type == "forecast")
    {
        //Serial.println(json);
        Serial.print(F("\nReceiving Forecast period - ")); //------------------------------------------------
        JsonArray list = root["list"];
        for (byte r = 0; r < max_readings; r++)
        {
            //Serial.println("\nPeriod-" + String(r) + "--------------");
            WxForecast[r].Dt = list[r]["dt"].as<int>();
            WxForecast[r].Temperature = list[r]["main"]["temp"].as<float>();
            //Serial.println("Temp: " + String(WxForecast[r].Temperature));
            WxForecast[r].Low = list[r]["main"]["temp_min"].as<float>();
            //Serial.println("TLow: " + String(WxForecast[r].Low));
            WxForecast[r].High = list[r]["main"]["temp_max"].as<float>();
            //Serial.println("THig: " + String(WxForecast[r].High));
            WxForecast[r].Pressure = list[r]["main"]["pressure"].as<float>();
            //Serial.println("Pres: " + String(WxForecast[r].Pressure));
            WxForecast[r].Humidity = list[r]["main"]["humidity"].as<float>();
            //Serial.println("Humi: " + String(WxForecast[r].Humidity));
            //WxForecast[r].Forecast0         = list[r]["weather"][0]["main"].as<char*>();        Serial.println("For0: " + String(WxForecast[r].Forecast0));
            //WxForecast[r].Forecast1         = list[r]["weather"][1]["main"].as<char*>();        Serial.println("For1: " + String(WxForecast[r].Forecast1));
            //WxForecast[r].Forecast2         = list[r]["weather"][2]["main"].as<char*>();        Serial.println("For2: " + String(WxForecast[r].Forecast2));
            WxForecast[r].Icon = list[r]["weather"][0]["icon"].as<const char *>();
            //Serial.println("Icon: " + String(WxForecast[r].Icon));
            //WxForecast[r].Description       = list[r]["weather"][0]["description"].as<char*>(); Serial.println("Desc: " + String(WxForecast[r].Description));
            //WxForecast[r].Cloudcover        = list[r]["clouds"]["all"].as<int>();               Serial.println("CCov: " + String(WxForecast[r].Cloudcover)); // in % of cloud cover
            //WxForecast[r].Windspeed         = list[r]["wind"]["speed"].as<float>();             Serial.println("WSpd: " + String(WxForecast[r].Windspeed));
            //WxForecast[r].Winddir           = list[r]["wind"]["deg"].as<float>();               Serial.println("WDir: " + String(WxForecast[r].Winddir));
            WxForecast[r].Rainfall = list[r]["rain"]["3h"].as<float>();
            //Serial.println("Rain: " + String(WxForecast[r].Rainfall));
            WxForecast[r].Snowfall = list[r]["snow"]["3h"].as<float>();
            //Serial.println("Snow: " + String(WxForecast[r].Snowfall));
            WxForecast[r].Period = list[r]["dt_txt"].as<const char *>();
            //Serial.println("Peri: " + String(WxForecast[r].Period));
        }
        //------------------------------------------
        float pressure_trend = WxForecast[0].Pressure - WxForecast[2].Pressure; // Measure pressure slope between ~now and later
        pressure_trend = ((int)(pressure_trend * 10)) / 10.0;                   // Remove any small variations less than 0.1
        WxConditions[0].Trend = "=";
        if (pressure_trend > 0)
            WxConditions[0].Trend = "+";
        if (pressure_trend < 0)
            WxConditions[0].Trend = "-";
        if (pressure_trend == 0)
            WxConditions[0].Trend = "0";

        if (Units == "I")
            Convert_Readings_to_Imperial();
    }
    return true;
}

bool obtainWeatherData(WiFiClient &client, const String &RequestType)
{
    const String units = (Units == "M" ? "metric" : "imperial");
    client.stop(); // close connection before sending a new request
    HTTPClient http;
    String uri = "/data/2.5/" + RequestType + "?q=" + City + "," + Country + "&APPID=" + apikey + "&mode=json&units=" + units + "&lang=" + Language;
    if (RequestType != "weather")
    {
        uri += "&cnt=" + String(max_readings);
    }
    http.begin(client, server, 80, uri); //http.begin(uri,test_root_ca); //HTTPS example connection
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK)
    {
        if (!DecodeWeather(http.getStream(), RequestType))
            return false;
        client.stop();
        http.end();
        return true;
    }
    else
    {
        Serial.printf("connection failed, error: %s", http.errorToString(httpCode).c_str());
        client.stop();
        http.end();
        return false;
    }
    http.end();
    return true;
}

void Render_Screen0()
{   // 4.7" e-paper display is 960x540 resolution
    RenderStatusSection(600, 20, wifi_signal); // Wi-Fi signal strength and Battery voltage
    RenderGeneralInfoSection();                // Top line of the display

    RenderWindSection(137, 150, WxConditions[0].Winddir, WxConditions[0].Windspeed*3.6, 100); // Converting wind speed from m/s to km/h for display
    RenderAstronomySection(5, 255);     // Astronomy section Sun rise/set, Moon phase and Moon icon

    RenderMainWeatherSection(320, 130); // Centre section of display for Location, temperature, Weather report, current Wx Symbol
    RenderSensorReadings(320, 210);     // Local sensor readings section
    RenderWeatherIcon(780, 133);        // Display weather icon    scale = Large
//////////////
    RenderForecastSection(0, 370);    // 3hr forecast boxes (was 320x220)
}

void Render_Screen1()
{   // 4.7" e-paper display is 960x540 resolution
    RenderStatusSection(600, 20, wifi_signal); // Wi-Fi signal strength and Battery voltage
    RenderGeneralInfoSection();                // Top line of the display

    RenderSensorReadings(30, 80);
    RenderWeatherIcon(650, 150);        // Display weather icon    scale = Large
}

void Render_Screen2()
{   // 4.7" e-paper display is 960x540 resolution
    RenderStatusSection(600, 20, wifi_signal); // Wi-Fi signal strength and Battery voltage
    RenderGeneralInfoSection();                // Top line of the display

    RenderGraphs();
}

// Array of function pointers to select different screens for display
void (*screens[])() = {Render_Screen0, Render_Screen1, Render_Screen2};
void renderDisplay(volatile int &screenState) 
{
    if ((screenState >= 0) && (screenState < (sizeof(screens) / sizeof(screens[0])))) {
        screens[screenState]();
        ESP_LOGI("DISPLAY", "Displaying screen nr: %d", screenState);
    } else {
        ESP_LOGW("DISPLAY", "Invalid screen state: %d. Defaulting to Screen 0.", screenState);
        screens[0]();
    }
}

void RenderWeatherIcon(int x, int y)
{
    RenderConditionsSection(x, y, WxConditions[0].Icon, LargeIcon);
}

void RenderMainWeatherSection(int x, int y)
{
    RenderTemperatureSection(x, y - 40);
    RenderForecastTextSection(x-30, y-10);
    RenderPressureSection(x+20, y+32, WxConditions[0].Pressure, WxConditions[0].Trend);
}

void RenderGeneralInfoSection()
{
    setFont(OpenSans12B);
    drawString(5, 5, City, LEFT);
    setFont(OpenSans12B);
    drawString(110, 2, Date_str, LEFT);
    setFont(OpenSans10B);
    drawString(320, 2, "Aktualizacja: " + String(ConvertUnixTimeForDisplay(time(NULL))), LEFT);
}

void RenderSensorReadings(int x, int y)
{
    //drawLine(480, 10, 480, 500, DarkGrey);
    //drawLine(200, 40, 910, 40, DarkGrey);
    RenderSensorReadingsGarden(x, y);
    RenderSensorReadingsRoom(x+245, y);
    //drawLine(480, 310, 940, 310, DarkGrey);
}

void RenderSensorReadingsGarden(int x, int y)
{
    if(dataReceivedFlag == true) 
    {
        setFont(OpenSans12B);
        drawString(x, y, "Czujnik ZEWN.", LEFT);
        setFont(OpenSans8B);
        drawString(x, y+30, String(ConvertUnixTimeForDisplay(receivedData.timestamp)), LEFT);
        setFont(OpenSans24B);
        drawString(x, y+50, String(receivedData.temperature, 1) + "°", LEFT);
        setFont(OpenSans18B);
        drawString(x+135, y+50, " " + String(receivedData.humidity, 0) + "%", LEFT);
        drawString(x, y+100, String(receivedData.pressure, 0) + " hPa", LEFT);

        dataReceivedFlag == false;
    }
    else
    {
        setFont(OpenSans12B);
        drawString(x, y, "Czujnik ZEWN.", LEFT);
        setFont(OpenSans8B);
        drawString(x, y+30, "Akt.: --:--:--", LEFT);
        setFont(OpenSans24B);
        drawString(x, y+50, String("--.-") + "°", LEFT);
        setFont(OpenSans18B);
        drawString(x+135, y+50, " " + String("--.-") + "%", LEFT);
        drawString(x, y+100, String("--.-") + " hPa", LEFT);
    }
}

void RenderSensorReadingsRoom(int x, int y)
{
    SensorData tempData;
    bool dataAvailable = false;

    // Try to receive multiple times to empty queue
    while (xQueueReceive(sensorDataQueue, &tempData, pdMS_TO_TICKS(500)) == pdTRUE)
    {
        localData = tempData;  // Store the latest received data
        dataAvailable = true;
    }

    if (dataAvailable) {
    //if (xQueueReceive(sensorDataQueue, &localData, pdMS_TO_TICKS(500)) == pdTRUE)
    //{
        ESP_LOGI("DISPLAY", "Local sensor data available.");
        setFont(OpenSans12B);
        drawString(x, y, "Czujnik DOM", LEFT);
        setFont(OpenSans8B);
        drawString(x, y+30, String(ConvertUnixTimeForDisplay(localData.timestamp)), LEFT);
        setFont(OpenSans24B);
        drawString(x, y+50, String(localData.temperature, 1) + "°", LEFT);
        setFont(OpenSans18B);
        drawString(x+135, y+50, " " + String(localData.humidity, 0) + "%", LEFT);
    }
    else
    {
        ESP_LOGW("DISPLAY", "Failed to receive processed data from queue");
        setFont(OpenSans12B);
        drawString(x, y, "Czujnik dom", LEFT);
        setFont(OpenSans8B);
        drawString(x, y+30, "Akt.: --:--:--", LEFT);
        setFont(OpenSans24B);
        drawString(x, y+50, String("--.-") + "°", LEFT);
        setFont(OpenSans18B);
        drawString(x+135, y+50, " " + String("--") + "%", LEFT);
    }    
}

void RenderWindSection(int x, int y, float angle, float windspeed, int Cradius)
{
    angle = fmod((angle + 180), 360); // Ensure the angle points opposite direction and wraps correctly between 0-360°
    arrow(x, y, Cradius - 22, angle, 18+2, 33+2); // Show wind direction on outer circle of width and length
    setFont(OpenSans8B);
    int dxo, dyo, dxi, dyi;
    drawCircle(x, y, Cradius, Black);       // Draw compass circle
    drawCircle(x, y, Cradius + 1, Black);   // Draw compass circle
    drawCircle(x, y, Cradius * 0.7, Black); // Draw compass inner circle
    for (float a = 0; a < 360; a = a + 22.5)
    {
        dxo = Cradius * cos((a - 90) * PI / 180);
        dyo = Cradius * sin((a - 90) * PI / 180);
        if (a == 45)
            drawString(dxo + x + 15, dyo + y - 18, TXT_NE, CENTER);
        if (a == 135)
            drawString(dxo + x + 20, dyo + y - 2, TXT_SE, CENTER);
        if (a == 225)
            drawString(dxo + x - 20, dyo + y - 2, TXT_SW, CENTER);
        if (a == 315)
            drawString(dxo + x - 15, dyo + y - 18, TXT_NW, CENTER);
        dxi = dxo * 0.9;
        dyi = dyo * 0.9;
        drawLine(dxo + x, dyo + y, dxi + x, dyi + y, Black);
        dxo = dxo * 0.7;
        dyo = dyo * 0.7;
        dxi = dxo * 0.9;
        dyi = dyo * 0.9;
        drawLine(dxo + x, dyo + y, dxi + x, dyi + y, Black);
    }
    drawString(x, y - Cradius - 20, TXT_N, CENTER);
    drawString(x, y + Cradius + 10, TXT_S, CENTER);
    drawString(x - Cradius - 15, y - 5, TXT_W, CENTER);
    drawString(x + Cradius + 10, y - 5, TXT_E, CENTER);
    drawString(x + 3, y + 50, String(angle, 0) + "°", CENTER);
    setFont(OpenSans12B);
    drawString(x, y - 50, WindDegToOrdinalDirection(angle), CENTER);
    setFont(OpenSans24B);
    drawString(x + 3, y - 18, String(windspeed, 1), CENTER);
    setFont(OpenSans12B);
    drawString(x, y + 25, (Units == "M" ? "km/h" : "mph"), CENTER); // change from m/s
}

String WindDegToOrdinalDirection(float winddirection)
{
    if (winddirection >= 348.75 || winddirection < 11.25)
        return TXT_N;
    if (winddirection >= 11.25 && winddirection < 33.75)
        return TXT_NNE;
    if (winddirection >= 33.75 && winddirection < 56.25)
        return TXT_NE;
    if (winddirection >= 56.25 && winddirection < 78.75)
        return TXT_ENE;
    if (winddirection >= 78.75 && winddirection < 101.25)
        return TXT_E;
    if (winddirection >= 101.25 && winddirection < 123.75)
        return TXT_ESE;
    if (winddirection >= 123.75 && winddirection < 146.25)
        return TXT_SE;
    if (winddirection >= 146.25 && winddirection < 168.75)
        return TXT_SSE;
    if (winddirection >= 168.75 && winddirection < 191.25)
        return TXT_S;
    if (winddirection >= 191.25 && winddirection < 213.75)
        return TXT_SSW;
    if (winddirection >= 213.75 && winddirection < 236.25)
        return TXT_SW;
    if (winddirection >= 236.25 && winddirection < 258.75)
        return TXT_WSW;
    if (winddirection >= 258.75 && winddirection < 281.25)
        return TXT_W;
    if (winddirection >= 281.25 && winddirection < 303.75)
        return TXT_WNW;
    if (winddirection >= 303.75 && winddirection < 326.25)
        return TXT_NW;
    if (winddirection >= 326.25 && winddirection < 348.75)
        return TXT_NNW;
    return "?";
}

void RenderTemperatureSection(int x, int y)
{
    setFont(OpenSans18B);
    drawString(x, y - 40, String(WxConditions[0].Temperature, 1) + "°     " + String(WxConditions[0].Humidity, 0) + "%", LEFT);
    setFont(OpenSans12B);
    drawString(x, y-5, "Max " + String(WxConditions[0].High, 0) + "° | Min " + String(WxConditions[0].Low, 0) + "°", LEFT); // Show forecast high and Low
}

void RenderForecastTextSection(int x, int y)
{
#define lineWidth 34
    setFont(OpenSans12B);
    //Wx_Description = WxConditions[0].Main0;          // e.g. typically 'Clouds'
    String Wx_Description = WxConditions[0].Forecast0; // e.g. typically 'overcast clouds' ... you choose which
    Wx_Description.replace(".", "");                   // remove any '.'
    int spaceRemaining = 0, p = 0, charCount = 0, Width = lineWidth;
    while (p < Wx_Description.length())
    {
        if (Wx_Description.substring(p, p + 1) == " ")
            spaceRemaining = p;
        if (charCount > Width - 1)
        { // '~' is the end of line marker
            Wx_Description = Wx_Description.substring(0, spaceRemaining) + "~" + Wx_Description.substring(spaceRemaining + 1);
            charCount = 0;
        }
        p++;
        charCount++;
    }
    if (WxForecast[0].Rainfall > 0)
        Wx_Description += " (" + String(WxForecast[0].Rainfall, 1) + String((Units == "M" ? "mm" : "in")) + ")";
    //Wx_Description = wordWrap(Wx_Description, lineWidth);
    String Line1 = Wx_Description.substring(0, Wx_Description.indexOf("~"));
    String Line2 = Wx_Description.substring(Wx_Description.indexOf("~") + 1);
    drawString(x + 30, y + 5, TitleCase(Line1), LEFT);
    if (Line1 != Line2)
        drawString(x + 30, y + 30, Line2, LEFT);
}

void RenderPressureSection(int x, int y, float pressure, String slope)
{
    setFont(OpenSans12B);
    DrawPressureAndTrend(x - 20, y, pressure, slope);
    /* // Turned off visivility
    if (WxConditions[0].Visibility > 0)
    {
        Visibility(x + 145, y, String(WxConditions[0].Visibility) + "M");
        x += 150; // Draw the text in the same positions if one is zero, otherwise in-line
    }
    */
    // if (WxConditions[0].Cloudcover > 0) // removed if, cloud cover always being drawn
        CloudCover(x + 135, y, WxConditions[0].Cloudcover);
}

void RenderForecastWeather(int x, int y, int index)
{
    int fwidth = 120; // EPD_WIDTH
    x = x + fwidth * index;
    RenderConditionsSection(x + fwidth / 2, y + 90, WxForecast[index].Icon, MediumIcon); // changed from SmallIcon 
    drawLine(x+fwidth, y+10, x+fwidth, y + 160, DarkGrey); // separators
    setFont(OpenSans12B);
    drawString(x + fwidth / 2, y + 10, String(ConvertUnixTimeForDisplay(WxForecast[index].Dt + WxConditions[0].Timezone).substring(0, 5)), CENTER);
    drawString(x + fwidth / 2, y + 135, String(WxForecast[index].High, 0) + "°/" + String(WxForecast[index].Low, 0) + "°", CENTER);
}

void RenderAstronomySection(int x, int y)
{
    setFont(OpenSans12B);
    drawString(x + 5, y + 30, TXT_SUNRISE + ": " + ConvertUnixTimeForDisplay(WxConditions[0].Sunrise).substring(0, 5), LEFT);
    drawString(x + 5, y + 50, TXT_SUNSET + ":  " + ConvertUnixTimeForDisplay(WxConditions[0].Sunset).substring(0, 5), LEFT);
    time_t now = time(NULL);
    struct tm *now_utc = gmtime(&now);
    const int day_utc = now_utc->tm_mday;
    const int month_utc = now_utc->tm_mon + 1;
    const int year_utc = now_utc->tm_year + 1900;
    drawString(x + 5, y + 70, MoonPhase(day_utc, month_utc, year_utc, Hemisphere), LEFT);
    DrawMoon(x + 173, y - 33, day_utc, month_utc, year_utc, Hemisphere);
}

void RenderForecastSection(int x, int y)
{
    drawLine(20, 365, 940, 365, DarkGrey);
    int f = 0;
    do
    {
        RenderForecastWeather(x, y, f);
        f++;
    } while (f < max_readings);
}

void RenderGraphs()
{
    //  4 graphs in the main screen

    int r = 0;
    do
    { // Pre-load temporary arrays with with data - because C parses by reference and remember that[1] has already been converted to I units
        if (Units == "I")
            pressure_readings[r] = WxForecast[r].Pressure * 0.02953;
        else
            pressure_readings[r] = WxForecast[r].Pressure;
        if (Units == "I")
            rain_readings[r] = WxForecast[r].Rainfall * 0.0393701;
        else
            rain_readings[r] = WxForecast[r].Rainfall;
        if (Units == "I")
            snow_readings[r] = WxForecast[r].Snowfall * 0.0393701;
        else
            snow_readings[r] = WxForecast[r].Snowfall;
        temperature_readings[r] = WxForecast[r].Temperature;
        humidity_readings[r] = WxForecast[r].Humidity;
        r++;
    } while (r < max_readings);

    int gwidth = 375, gheight = 180;
    int gx = (SCREEN_WIDTH - gwidth * 2) / 3 + 8; // equals 60px
    int gy = (SCREEN_HEIGHT - gheight - 30); // equals 410
    int gap = gwidth + gx;

    // (x,y,width,height,MinValue, MaxValue, Title, Data Array, AutoScale, ChartMode)
    
    DrawGraph(gx + 0 * gap, 65, gwidth, gheight, 900, 1050, Units == "M" ? TXT_PRESSURE_HPA : TXT_PRESSURE_IN, pressure_readings, max_readings, autoscale_on, barchart_off);
    DrawGraph(gx + 1 * gap, 65, gwidth, gheight, 10, 30, Units == "M" ? TXT_TEMPERATURE_C : TXT_TEMPERATURE_F, temperature_readings, max_readings, autoscale_on, barchart_off);
    DrawGraph(gx + 0 * gap, 140+gheight, gwidth, gheight, 0, 100, TXT_HUMIDITY_PERCENT, humidity_readings, max_readings, autoscale_off, barchart_off);
    if (SumOfPrecip(rain_readings, max_readings) >= SumOfPrecip(snow_readings, max_readings))
        DrawGraph(gx + 1 * gap + 5, 140+gheight, gwidth, gheight, 0, 30, Units == "M" ? TXT_RAINFALL_MM : TXT_RAINFALL_IN, rain_readings, max_readings, autoscale_on, barchart_on);
    else
        DrawGraph(gx + 1 * gap + 5, 140+gheight, gwidth, gheight, 0, 30, Units == "M" ? TXT_SNOWFALL_MM : TXT_SNOWFALL_IN, snow_readings, max_readings, autoscale_on, barchart_on);
}

void RenderConditionsSection(int x, int y, String IconName, IconSize size)
{
    Serial.println("Icon name: " + IconName);
    if (IconName == "01d" || IconName == "01n")
        Sunny(x, y, size, IconName);
    else if (IconName == "02d" || IconName == "02n")
        MostlySunny(x, y, size, IconName);
    else if (IconName == "03d" || IconName == "03n")
        Cloudy(x, y, size, IconName);
    else if (IconName == "04d" || IconName == "04n")
        MostlySunny(x, y, size, IconName);
    else if (IconName == "09d" || IconName == "09n")
        ChanceRain(x, y, size, IconName);
    else if (IconName == "10d" || IconName == "10n")
        Rain(x, y, size, IconName);
    else if (IconName == "11d" || IconName == "11n")
        Tstorms(x, y, size, IconName);
    else if (IconName == "13d" || IconName == "13n")
        Snow(x, y, size, IconName);
    else if (IconName == "50d")
        Haze(x, y, size, IconName);
    else if (IconName == "50n")
        Fog(x, y, size, IconName);
    else
        Nodata(x, y, size, IconName);
}

void RenderStatusSection(int x, int y, int rssi)
{
    setFont(OpenSans10B);
    DrawRSSI(x + 310, y + 15, rssi);
    DrawBattery(x + 150, y);
}

void DrawPressureAndTrend(int x, int y, float pressure, String slope)
{
    drawString(x + 20, y, String(pressure, (Units == "M" ? 0 : 1)) + (Units == "M" ? "hPa" : "in"), LEFT);
    if (slope == "+")
    {
        DrawSegment(x, y + 10, 0, 0, 8, -8, 8, -8, 16, 0);
        DrawSegment(x - 1, y + 10, 0, 0, 8, -8, 8, -8, 16, 0);
    }
    else if (slope == "0")
    {
        DrawSegment(x, y + 10, 8, -8, 16, 0, 8, 8, 16, 0);
        DrawSegment(x - 1, y + 10, 8, -8, 16, 0, 8, 8, 16, 0);
    }
    else if (slope == "-")
    {
        DrawSegment(x, y + 10, 0, 0, 8, 8, 8, 8, 16, 0);
        DrawSegment(x - 1, y + 10, 0, 0, 8, 8, 8, 8, 16, 0);
    }
}

float SumOfPrecip(float DataArray[], int readings)
{
    float sum = 0;
    for (int i = 0; i <= readings; i++)
    {
        sum += DataArray[i];
    }
    return sum;
}

String TitleCase(String text)
{
    if (text.length() > 0)
    {
        String temp_text = text.substring(0, 1);
        temp_text.toUpperCase();
        return temp_text + text.substring(1); // Title-case the string
    }
    else
        return text;
}

double NormalizedMoonPhase(int d, int m, int y)
{
    int j = JulianDate(d, m, y);
    //Calculate approximate moon phase
    double Phase = (j + 4.867) / 29.53059;
    return (Phase - (int)Phase);
}

String MoonPhase(int d, int m, int y, String hemisphere)
{
    int c, e;
    double jd;
    int b;
    if (m < 3)
    {
        y--;
        m += 12;
    }
    ++m;
    c = 365.25 * y;
    e = 30.6 * m;
    jd = c + e + d - 694039.09; /* jd is total days elapsed */
    jd /= 29.53059;             /* divide by the moon cycle (29.53 days) */
    b = jd;                     /* int(jd) -> b, take integer part of jd */
    jd -= b;                    /* subtract integer part to leave fractional part of original jd */
    b = jd * 8 + 0.5;           /* scale fraction from 0-8 and round by adding 0.5 */
    b = b & 7;                  /* 0 and 8 are the same phase so modulo 8 for 0 */
    if (hemisphere == "south")
        b = 7 - b;
    if (b == 0)
        return TXT_MOON_NEW; // New;              0%  illuminated
    if (b == 1)
        return TXT_MOON_WAXING_CRESCENT; // Waxing crescent; 25%  illuminated
    if (b == 2)
        return TXT_MOON_FIRST_QUARTER; // First quarter;   50%  illuminated
    if (b == 3)
        return TXT_MOON_WAXING_GIBBOUS; // Waxing gibbous;  75%  illuminated
    if (b == 4)
        return TXT_MOON_FULL; // Full;            100% illuminated
    if (b == 5)
        return TXT_MOON_WANING_GIBBOUS; // Waning gibbous;  75%  illuminated
    if (b == 6)
        return TXT_MOON_THIRD_QUARTER; // Third quarter;   50%  illuminated
    if (b == 7)
        return TXT_MOON_WANING_CRESCENT; // Waning crescent; 25%  illuminated
    return "";
}

void Convert_Readings_to_Imperial()
{ // Only the first 3-hours are used
    WxConditions[0].Pressure = hPa_to_inHg(WxConditions[0].Pressure);
    WxForecast[0].Rainfall = mm_to_inches(WxForecast[0].Rainfall);
    WxForecast[0].Snowfall = mm_to_inches(WxForecast[0].Snowfall);
}

float mm_to_inches(float value_mm)
{
    return 0.0393701 * value_mm;
}

float hPa_to_inHg(float value_hPa)
{
    return 0.02953 * value_hPa;
}

int JulianDate(int d, int m, int y)
{
    int mm, yy, k1, k2, k3, j;
    yy = y - (int)((12 - m) / 10);
    mm = m + 9;
    if (mm >= 12)
        mm = mm - 12;
    k1 = (int)(365.25 * (yy + 4712));
    k2 = (int)(30.6001 * mm + 0.5);
    k3 = (int)((int)((yy / 100) + 49) * 0.75) - 38;
    // 'j' for dates in Julian calendar:
    j = k1 + k2 + d + 59 + 1;
    if (j > 2299160)
        j = j - k3; // 'j' is the Julian date at 12h UT (Universal Time) For Gregorian calendar:
    return j;
}

void epd_update()
{
    epd_draw_grayscale_image(epd_full_screen(), framebuffer); // Update the screen
}


// Tasks //////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Web used for basic configuration, run by pressing USR_BUTTON during boot
void ConfigWebServerTask(void *pvParameters)
{
    Serial.println("Starting web server for configuration...");
    setupWEB(); // Start the web server
    Serial.println("Web server set up. Waiting for configuration...");
    
    // Keep the server running while waiting for config
    while (!configDone) 
    {
        vTaskDelay(pdMS_TO_TICKS(100)); // Delay to avoid CPU blocking
    }

    Serial.println("Configuration completed. Stopping web server...");
    xSemaphoreGive(configSemaphore); // Signal the config task to continue
    stopWebServer();
    vTaskDelete(NULL); // Delete the task when done
}

// OpenWeather configuration setup / reading
void ConfigTask(void *pvParameters) 
{
    // Wait for button press with 3 second timeout
    Serial.println("Awaiting button press to start configuration web server (3 sec)...");
    if (xSemaphoreTake(ButtonWakeSem, SECONDS_TO_TICKS(3)) == pdTRUE || !SPIFFS.exists("/config.json"))
    {
        Serial.println("Button pressed or config file missing. Starting configuration web server...");
        xTaskCreate(ConfigWebServerTask, "ConfigWebServerTask", 8192, NULL, 6, NULL);
        xSemaphoreTake(configSemaphore, portMAX_DELAY); // Wait for the web server to finish
    }
    else
    {
        StaticJsonDocument<1024> json1;
        File configfile = SPIFFS.open("/config.json", "r");
        DeserializationError error = deserializeJson(json1, configfile);

        if (error)
        {
            Serial.println("Failed to read file, using default configuration");
        }
        else
        {
        // Extract configuration values
        ssid = json1["WLAN"]["ssid"].as<String>();
        password = json1["WLAN"]["password"].as<String>();

        apikey     = json1["OpenWeather"]["apikey"].as<String>();
        server     = json1["OpenWeather"]["server"].as<String>();
        Country    = json1["OpenWeather"]["country"].as<String>();
        City       = json1["OpenWeather"]["city"].as<String>();
        Hemisphere = json1["OpenWeather"]["hemisphere"].as<String>();
        Units      = json1["OpenWeather"]["units"].as<String>();

        ntpServer = json1["ntp"]["server"].as<String>();
        Timezone = json1["ntp"]["timezone"].as<String>();

        // WakeupHour = strtok(json1["schedule_power"]["on_time"].as<const char *>());
        // SleepHour  = json1["schedule_power"]["off_time"].as<String>();
        Serial.println("Config loaded successfully.");
        }

        configfile.close();
    }
    xSemaphoreGive(configSemaphore); // Signal the main task to continue
    vTaskDelete(NULL); // Delete the task when done - first boot
}

// SD Logging Task
void SDLoggingTask(void *pvParameters) {
    SensorLogEntry logEntry;
    static int logCounter = 0;  // Track number of logs since last JSON update

    while (1) {
        // Wait indefinitely for new data in the queue
        if (xQueueReceive(logQueue, &logEntry, portMAX_DELAY) == pdTRUE) {
            // Open file for appending
            File file = SD.open(logEntry.filename, FILE_APPEND);
            if (!file) {
                Serial.printf("Failed to open %s for writing!\n", logEntry.filename);
                continue;
            }

            // Write data in CSV format
            file.printf("%d,%.2f,%.1f,%.1f\n", logEntry.timestamp, logEntry.temperature, logEntry.humidity, logEntry.pressure);
            file.close();
            Serial.printf("Data logged to %s: %d, %.2f, %.1f, %f\n", 
                          logEntry.filename, logEntry.timestamp, logEntry.temperature, logEntry.humidity, logEntry.pressure);
            
            // Maintain circular buffer
            maintainCircularBuffer(logEntry.filename);

            // **Update JSON only every 4 logs to reduce SD wear**
            if (++logCounter >= 4) {
                //generateJSONData();
                logCounter = 0;  // Reset counter
            }
        }
    }
}

// Task for obtaining i2c sensor reading
void SHT4xReadTask(void *pvParameters)
{
    static sht4x_t dev;
    SensorData sht4xdata;
    sht4xdata.pressure = 0;
    sht4xdata.timestamp = 0;

    //Init SHT4x sensor
    memset(&dev, 0, sizeof(sht4x_t));
    ESP_ERROR_CHECK(sht4x_init_desc(&dev, 0, I2C_MASTER_SDA, I2C_MASTER_SCL));
    ESP_ERROR_CHECK(sht4x_init(&dev));

    // Get the measurement duration for high repeatability;
    uint8_t duration = sht4x_get_measurement_duration(&dev);

    while (1)
    {   
        // Wait for the semaphore to be given by the main task to start measurement
        ESP_LOGI("SHT40", "Awaiting trigger to start local measurement...");
        xSemaphoreTake(sht4xTriggerSem, portMAX_DELAY);
        
        // Trigger one measurement in single shot mode with high repeatability.
        ESP_ERROR_CHECK(sht4x_start_measurement(&dev));
        // Wait until measurement is ready (duration returned from *sht4x_get_measurement_duration*).
        vTaskDelay(duration); // duration is in ticks

        // retrieve the values and send it to the queue
        if(sht4x_get_results(&dev, &sht4xdata.temperature, &sht4xdata.humidity) == ESP_OK)
        {
            sht4xdata.humidity -= 5; // Rough 5% correction factor
            sht4xdata.timestamp = time(NULL);
            queueLogData("/inside_log.csv", sht4xdata.temperature, sht4xdata.humidity, 0, sht4xdata.timestamp);
            ESP_LOGI("SHT40", "Timestamp: %lu, SHT40  - Temperature: %.2f °C, Humidity: %.2f %%", (unsigned long)xTaskGetTickCount(), sht4xdata.temperature, sht4xdata.humidity);

            if (xQueueSend(sensorDataQueue, &sht4xdata, pdMS_TO_TICKS(5000)) != pdPASS) 
            {
                ESP_LOGI("SHT40", "Failed to send data to sensorDataQueue in time");
            }
            else
            {
                ESP_LOGI("SHT40", "SHT40 Data sent to sensorDataQueue");
            }
        }
        else
        {
            ESP_LOGI("SHT40", "SHT40 Failed to read sensor data.");
        }
        xSemaphoreGive(sht4xCompleteSem);
    }
}

// Task planned for lowering power consumption or entering sleep
void IdleTask(void *pvParameters)
{
    UpdateLocalTime();
    epd_poweroff_all();

    int currentMinutes = CurrentHour * 60 + CurrentMin; // Current time in minutes
    int nextWakeMinutes = ((currentMinutes / 15) + 1) * 15; // Align to next 15-minute mark
    int sleepDuration = (nextWakeMinutes - currentMinutes) * 60 - CurrentSec;

    // Handle long sleep (e.g., overnight)
    if (SleepHoursEnabled && (CurrentHour >= SleepHour || CurrentHour < WakeupHour))
    {   
        int wakeupMinutes = WakeupHour * 60; 
        sleepDuration = ((wakeupMinutes - currentMinutes + 1440) % 1440) * 60;
        Serial.printf("Sleeping until WakeupHour %02d:00\n", WakeupHour);
    }

    Serial.printf("Idling for %d seconds, waking at next 15-min mark\n", sleepDuration);
    
    // Set wakeup timer (1000000LL converts to Secs as unit = 1uSec)
    esp_sleep_enable_timer_wakeup(sleepDuration * 1000000LL);

    /*
    //esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON); // Keep peripherals powered
    //esp_sleep_enable_ext0_wakeup(USR_BUTTON, 0); // Wake up on button press
    Serial.println("Awake for: " + String((millis() - StartTime) / 1000.0, 3) + " secs");
    Serial.println("Entering " + String(SleepTimer) + " (secs) of sleep time");
    */

    //Select deep or light sleep (currently set to only idling)
    if(DeepSleepEnabled == true)
    {
        Serial.println("Starting deep sleep period");
        esp_deep_sleep_start();
    }
    else
    {    
        // Probe time to avoid too often OWM updates, reset flag
        IdleStartTime = esp_timer_get_time();
        skipOWMUpdate = false;
        
        // Wait for either an ESP-NOW packet, a button press, or timeout
        Serial.println("Idle until Button Press, ESP-NOW packet, or timeout...");

        QueueSetMemberHandle_t eventTriggered = xQueueSelectFromSet(queueSet, SECONDS_TO_TICKS(sleepDuration));
        if (eventTriggered == ButtonWakeSem) 
        {
            screenState = (screenState + 1) % 3;
            Serial.println("Button pressed, switching screen: " + String(screenState));
            xSemaphoreGive(dataExhangeCompleteSem);  // Bypass for ESP-NOW transmission timeout

            // Reset the semaphore to allow future presses (for some reason interrupt from esp-now doesn't require it)
            xSemaphoreTake(ButtonWakeSem, 0);
        } 
        else if (eventTriggered == ESPNowWakeSem) 
        {
            Serial.println("Woken up by ESP-NOW packet!");
        }
        else 
        {
            Serial.println("Idle time expired, proceeding with next cycle...");
        }
        ESP_LOGI("SLEEP", "Idling finished.");

        // Tests
        Serial.println("POST IDLE Stack high watermark: " + String(uxTaskGetStackHighWaterMark(NULL)));
        Serial.println("POST IDLE Free heap: " + String(esp_get_free_heap_size()));
        
        // Skip OWM update if less than 3 minutes passed since last sleep entry
        int64_t now = esp_timer_get_time();
        if((now - IdleStartTime <= 180000000)) 
        {
            skipOWMUpdate = true;
            ESP_LOGW("SLEEP", "Less than 3 minutes of sleep, skipping OWM update.");
        }

    }

    // Signal the main task to continue and delete current task
    xSemaphoreGive(idleEndedSem); 
    vTaskDelete(NULL);
}

// Main data fetch and display rendering task
void WeatherUpdateTask(void *pvParameters)
{
    BaseType_t xReturned;
    WiFiClient client;
    while (1)
    {
        ESP_LOGI("wUpdate", "Awaiting OUTSIDE sensor data transmission (30s)...");
        if(xSemaphoreTake(dataExhangeCompleteSem, pdMS_TO_TICKS(30000)) != pdTRUE) // Proceed if received packet (30s timeout) OR button press (same sem)
        {
            ESP_LOGE("wUpdate", "Failed to receive OUTSIDE sensor data in time.");
        }

        // Trigger local sensor read
        xSemaphoreGive(sht4xTriggerSem);

        // Fetch OpeanWeatherMap data - skipping if already updated in last 3 mins
        if(skipOWMUpdate == false)
        {
            if (WiFiStatus == WL_CONNECTED && timeIsSet == true)
            {
                bool RxWeather = false;
                bool RxForecast = false;

                for (int attempts = 0; attempts < 2 && (!RxWeather || !RxForecast); ++attempts)
                {
                    if (!RxWeather)
                        RxWeather = obtainWeatherData(client, "weather");
                    if (!RxForecast)
                        RxForecast = obtainWeatherData(client, "forecast");
                }
            }
            else
            {
                ESP_LOGE("wUpdate", "No WiFi connection or time set, reconnecting...");
                WiFiStatus = StartWiFi();
                timeIsSet = SetTime();
            }
        }

        // Render display
        epd_poweron();
        epd_clear(); // Clearing current display
        memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2); // Clearing previous renders from framebuffer
        if(xSemaphoreTake(sht4xCompleteSem, pdMS_TO_TICKS(3000)) != pdTRUE) // Waiting 3s for local measurements
        {
            ESP_LOGE("wUpdate", "Failed to take sht4xCompleteSem in time");
        }
        renderDisplay(screenState);
        epd_update();
        epd_poweroff_all();

        // Initiating sleeping/idling task
        ESP_LOGI("wUpdate", "Initiating idle mode...");
        xReturned = xTaskCreate(IdleTask, "IdleTask", 8192, NULL, 1, NULL);
        if (xReturned != pdPASS) 
        {
            ESP_LOGE("wUpdate", "Failed to create IdleTask");
            return;
        }
        xSemaphoreTake(idleEndedSem, portMAX_DELAY);

    }
}

void setup()
{
    InitialiseSystem();
    SPIFFS.begin();
    ESP_LOGI("SETUP", "System init ok");

    // Create semaphores with error checking
    configSemaphore = xSemaphoreCreateBinary();
    idleEndedSem = xSemaphoreCreateBinary();
    ButtonWakeSem = xSemaphoreCreateBinary();
    ESPNowWakeSem = xSemaphoreCreateBinary();
    dataExhangeCompleteSem = xSemaphoreCreateBinary();
    sht4xTriggerSem = xSemaphoreCreateBinary();
    sht4xCompleteSem = xSemaphoreCreateBinary();
    if (!configSemaphore || !idleEndedSem || !ButtonWakeSem || !ESPNowWakeSem || !dataExhangeCompleteSem || !sht4xTriggerSem || !sht4xCompleteSem) 
    {
        ESP_LOGE("SETUP", "Failed to create ALL semaphores");
        return;
    }
    ESP_LOGI("SETUP", "All semaphores created successfully");


    // Create queues with error checking
    sensorDataQueue = xQueueCreate(10, sizeof(SensorData));
    logQueue = xQueueCreate(10, sizeof(SensorLogEntry));
    if (sensorDataQueue == NULL || logQueue == NULL) 
    {
        ESP_LOGE("SETUP", "Failed to create queues");
        return;
    }
    ESP_LOGI("SETUP", "Queues created successfully");

    
    // Create an event set to enable multi semaphore condition check
    EventGroupHandle_t eventGroup = xEventGroupCreate();
    xEventGroupSetBits(eventGroup, (1 << 0) | (1 << 1));  // Assign bits to both semaphores
    // Add semaphores to a queue set
    xQueueAddToSet(ButtonWakeSem, queueSet);
    xQueueAddToSet(ESPNowWakeSem, queueSet);


    // Configure button interrupt
    pinMode(USR_BUTTON, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(USR_BUTTON), ButtonISR, RISING);


    // Create tasks with error checking
    BaseType_t xReturned;
    xReturned = xTaskCreate(ConfigTask, "ConfigTask", 8192, NULL, 5, NULL);
    if (xReturned != pdPASS) 
    {
        ESP_LOGE("SETUP", "Failed to create Config Task");
        return;
    }

    xSemaphoreTake(configSemaphore, portMAX_DELAY); // Waiting for the config task to finish

    // Start wifi and get ntp update
    WiFiStatus = StartWiFi();  // Function runs here
    timeIsSet = SetTime();     // Function runs here
    unsigned long WiFiStartTimeout = millis(); // if wifi hasn't started, wait for 5 sec
    while (!(WiFiStatus == WL_CONNECTED && timeIsSet == true) && (millis() - WiFiStartTimeout < 5000))
    {
        delay(200);
        Serial.println(".");
    }

    // Initiate ESP-NOW
    if (esp_now_init() != ESP_OK) 
    {
    Serial.println("ESP-NOW Init Failed");
    return;
    }
    esp_now_register_recv_cb(OnDataRecv); // Register ESP-NOW callback

    // Register ESP-NOW peer (ESP32-C3 - OUTSIDE)
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, receiverMac, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) 
    {
        ESP_LOGE("SETUP", "Failed to add ESP-NOW peer");
        return;
    }


    xReturned = xTaskCreate(SHT4xReadTask, "SHT4xReadTask", 4096, NULL, 3, NULL);
    if (xReturned != pdPASS) 
    {
        ESP_LOGE("SETUP", "Failed to create SHT4x task");
        return;
    }

    xReturned = xTaskCreate(WeatherUpdateTask, "WeatherUpdateTask", 8192, NULL, 4, NULL);
    if (xReturned != pdPASS) 
    {
        ESP_LOGE("SETUP", "Failed to create WeatherUpdate Task");
        return;
    }

    xReturned = xTaskCreate(SDLoggingTask, "SDLoggingTask", 8192, NULL, 2, NULL);
    if (xReturned != pdPASS) 
    {
        ESP_LOGE("SETUP", "Failed to create SDLoggingTask Task");
        return;
    }

    ESP_LOGI("SETUP", "All tasks created successfully");

    setupWebServer();
    
}

void loop()
{
    vTaskDelay(pdMS_TO_TICKS(1000));
}
