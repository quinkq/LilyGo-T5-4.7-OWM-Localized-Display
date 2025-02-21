#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_heap_caps.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>

#include <web.h>

bool configDone = false;
static AsyncWebServer configServer(80);

void storeConfigFromRequest(AsyncWebServerRequest *request);

void handleRoot(AsyncWebServerRequest *request)
{
    request->send(200, "text/plain", "hello from ESP32!");
}

void handleSettings(AsyncWebServerRequest *request)
{
    request->send(SPIFFS, "/config.html", "text/html");
}

void handleConfig(AsyncWebServerRequest *request)
{
    Serial.println("Saving configuration...");

    if (request->hasParam("ssid", true)) // true = look in POST
    {
        Serial.println("Processing settings...");
        storeConfigFromRequest(request);
        configDone = true;
        request->send(SPIFFS, "/config.html", "text/html");
    }
    else
    {
        request->send(400, "text/plain", "Invalid request");
    }
}

void handleNotFound(AsyncWebServerRequest *request)
{
    String message = "File Not Found\n\n";
    message += "URI: " + request->url();
    request->send(404, "text/plain", message);
}

void setupAP()
{
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);

    Serial.println("Configuring access point...");
    uint8_t mac[6];
    char buff[32] = {0};
    WiFi.macAddress(mac);
    sprintf(buff, "T-EPD47-%02X%02X", mac[4], mac[5]);

    const char *apPassword = "config32s3";
    bool success = WiFi.softAP(buff, apPassword, 1, false, 1);
    if (success)
    {
        Serial.printf("The hotspot has been established");
        Serial.printf("please connect to the %s and output 192.168.4.1/settings in the browser to access the data page \n", buff);
        Serial.printf("for firmware update go to 192.168.4.1/update in the browser \n", buff);
    }
    else
    {
        Serial.println("Failed to start AP!");
    }
}

void setupConfigWEB()
{
    Serial.begin(115200);
    setupAP();
    // Initialize ElegantOTA with AsyncWebServer
    ElegantOTA.begin(&configServer);  // Start ElegantOTA

    // Serve static files from SPIFFS
    configServer.serveStatic("/", SPIFFS, "/").setDefaultFile("config.html");

    // Define routes
    configServer.on("/", HTTP_GET, handleRoot);
    configServer.on("/settings", HTTP_GET, handleSettings);
    configServer.on("/config", HTTP_POST, handleConfig);
    configServer.on("/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(SPIFFS, "/config.json", "application/json");
    });
    configServer.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Restarting...");
        delay(1000);
        ESP.restart();
    });

    // Handle 404 Not Found
    configServer.onNotFound(handleNotFound);
    configServer.begin();

    Serial.println("HTTP configServer started");
}

void storeConfigFromRequest(AsyncWebServerRequest *request)
{
    StaticJsonDocument<1024> doc;

    File configfile = SPIFFS.open("/config.json", "r");
    if (configfile)
    {
        DeserializationError error = deserializeJson(doc, configfile);
        if (error)
            Serial.println(F("Failed to read file, using default configuration"));
        configfile.close();
    }

    // Process form parameters
    if (request->hasParam("ssid", true))
        doc["WLAN"]["ssid"] = request->getParam("ssid", true)->value();
    if (request->hasParam("password", true))
        doc["WLAN"]["password"] = request->getParam("password", true)->value();
    if (request->hasParam("apikey", true))
        doc["OpenWeather"]["apikey"] = request->getParam("apikey", true)->value();
    if (request->hasParam("configServer", true))
        doc["OpenWeather"]["configServer"] = request->getParam("configServer", true)->value();
    if (request->hasParam("country", true))
        doc["OpenWeather"]["country"] = request->getParam("country", true)->value();
    if (request->hasParam("city", true))
        doc["OpenWeather"]["city"] = request->getParam("city", true)->value();
    if (request->hasParam("hemisphere", true))
        doc["OpenWeather"]["hemisphere"] = request->getParam("hemisphere", true)->value();
    if (request->hasParam("units", true))
        doc["OpenWeather"]["units"] = request->getParam("units", true)->value();
    if (request->hasParam("ntp_server", true))
        doc["ntp"]["configServer"] = request->getParam("ntp_server", true)->value();
    if (request->hasParam("ntp_timezone", true))
        doc["ntp"]["timezone"] = request->getParam("ntp_timezone", true)->value();
    if (request->hasParam("on_time", true))
        doc["schedule_power"]["on_time"] = request->getParam("on_time", true)->value();
    if (request->hasParam("off_time", true))
        doc["schedule_power"]["off_time"] = request->getParam("off_time", true)->value();

    configfile = SPIFFS.open("/config.json", FILE_WRITE);
    if (!configfile)
    {
        Serial.println("File error");
    }
    if (serializeJson(doc, configfile) == 0)
    {
        Serial.println(F("Failed to write to file"));
    }
    configfile.close();
}

void stopWebServer()
{
    Serial.println("Stopping web server...");
    configServer.end();
}