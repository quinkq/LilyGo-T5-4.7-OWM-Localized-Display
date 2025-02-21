#include "logWebServer.h"
#include <Arduino.h>
//#include <ArduinoJson.h>

const char* insideLogFile = "/inside_log.csv";
const char* outsideLogFile = "/outside_log.csv";

AsyncWebServer logServer(80);  

void setupLogWebServer() {
    logServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(SD, "/index.html", "text/html");
    });

    // Serve latest sensor readings
    logServer.on("/latest", HTTP_GET, [](AsyncWebServerRequest *request) {
        StaticJsonDocument<512> jsonDoc;

        File file = SD.open(insideLogFile, FILE_READ);
        if (file) {
            String lastLine;
            while (file.available()) lastLine = file.readStringUntil('\n');
            file.close();
            if (lastLine.length() > 0) {
                int timestamp;
                float temperature, humidity;
                sscanf(lastLine.c_str(), "%d,%f,%f", &timestamp, &temperature, &humidity);
                jsonDoc["inside_temperature"] = temperature;
                jsonDoc["inside_humidity"] = humidity;
            }
        }

        file = SD.open(outsideLogFile, FILE_READ);
        if (file) {
            String lastLine;
            while (file.available()) lastLine = file.readStringUntil('\n');
            file.close();
            if (lastLine.length() > 0) {
                int timestamp;
                float temperature, humidity, pressure;
                sscanf(lastLine.c_str(), "%d,%f,%f,%f", &timestamp, &temperature, &humidity, &pressure);
                jsonDoc["outside_temperature"] = temperature;
                jsonDoc["outside_humidity"] = humidity;
                jsonDoc["outside_pressure"] = pressure;
            }
        }

        String response;
        serializeJson(jsonDoc, response);
        request->send(200, "application/json", response);
    });

    // Dynamic Min/Max calculation
    logServer.on("/minmax", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("range")) {
            request->send(400, "text/plain", "Missing range parameter");
            return;
        }

        String range = request->getParam("range")->value();
        int timeLimit = getTimeLimit(range);

        StaticJsonDocument<512> jsonDoc;
        calculateMinMax(insideLogFile, timeLimit, jsonDoc, "inside");
        calculateMinMax(outsideLogFile, timeLimit, jsonDoc, "outside");

        String response;
        serializeJson(jsonDoc, response);
        request->send(200, "application/json", response);
    });

    // JSON data for charts (filterable by time range)
    logServer.on("/chart-data", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("range")) {
            request->send(400, "text/plain", "Missing range parameter");
            return;
        }

        String range = request->getParam("range")->value();
        generateJSONData(range);
        request->send(SD, "/data.json", "application/json");
    });

    // CSV download
    logServer.on("/download", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("type") || !request->hasParam("range")) {
            request->send(400, "text/plain", "Missing parameters: type or range");
            return;
        }

        String type = request->getParam("type")->value();
        String range = request->getParam("range")->value();
        String filename = (type == "inside") ? insideLogFile : outsideLogFile;

        File file = SD.open(filename, FILE_READ);
        if (!file) {
            request->send(500, "text/plain", "Failed to open data file");
            return;
        }

        String csvData = generateCSVData(file, range);
        file.close();

        request->send(200, "text/csv", csvData);
    });

    logServer.begin();
    Serial.println("Web logServer started.");
}

// Get time limit for data filtering
int getTimeLimit(const String &range) {
    if (range == "24h") return 86400;
    if (range == "week") return 604800;
    if (range == "month") return 2592000;
    if (range == "year") return 31536000;
    return 86400;
}

// Min/Max calculation for a given log file
void calculateMinMax(const char* filename, int timeLimit, StaticJsonDocument<512> &jsonDoc, const char* prefix) {
    File file = SD.open(filename, FILE_READ);
    if (!file) return;

    float minTemp = 999, maxTemp = -999;
    float minHumidity = 999, maxHumidity = -999;
    float minPressure = 9999, maxPressure = -9999;
    int currentTime = time(NULL);

    while (file.available()) {
        String line = file.readStringUntil('\n');
        int timestamp;
        float temperature, humidity, pressure = 0;
        sscanf(line.c_str(), "%d,%f,%f,%f", &timestamp, &temperature, &humidity, &pressure);

        if (currentTime - timestamp <= timeLimit) {
            if (temperature < minTemp) minTemp = temperature;
            if (temperature > maxTemp) maxTemp = temperature;
            if (humidity < minHumidity) minHumidity = humidity;
            if (humidity > maxHumidity) maxHumidity = humidity;
            if (String(prefix) == "outside") { // Only outside has pressure
                if (pressure < minPressure) minPressure = pressure;
                if (pressure > maxPressure) maxPressure = pressure;
            }
        }
    }
    file.close();

    jsonDoc[String(prefix) + "_temp_min"] = minTemp;
    jsonDoc[String(prefix) + "_temp_max"] = maxTemp;
    jsonDoc[String(prefix) + "_humidity_min"] = minHumidity;
    jsonDoc[String(prefix) + "_humidity_max"] = maxHumidity;
    if (String(prefix) == "outside") {
        jsonDoc["outside_pressure_min"] = minPressure;
        jsonDoc["outside_pressure_max"] = maxPressure;
    }
}

String generateCSVData(File &file, String range) 
{
    String csv = "timestamp,temperature,humidity,pressure\n";
    time_t now = time(NULL);
    time_t rangeStart = now - getTimeLimit(range); // Use getTimeLimit()

    while (file.available()) {
        String line = file.readStringUntil('\n');
        int timestamp;
        float temperature, humidity, pressure;
        sscanf(line.c_str(), "%d,%f,%f,%f", &timestamp, &temperature, &humidity, &pressure);

        if (timestamp >= rangeStart) {
            csv += line + "\n";
        }
    }
    return csv;
}

// Generate JSON for charts based on selected time range
void generateJSONData(String range) {
    File file = SD.open(insideLogFile, FILE_READ);
    if (!file) return;

    int timeLimit = getTimeLimit(range);
    int currentTime = time(NULL);

    String json = "[";
    int count = 0;
    
    while (file.available()) {
        String line = file.readStringUntil('\n');
        int timestamp;
        float temperature, humidity;
        sscanf(line.c_str(), "%d,%f,%f", &timestamp, &temperature, &humidity);

        if (currentTime - timestamp <= timeLimit) {
            if (count > 0) json += ",";
            json += "{\"timestamp\":" + String(timestamp) + 
                    ",\"inside_temperature\":" + String(temperature, 1) + 
                    ",\"inside_humidity\":" + String(humidity, 1) + "}";
            count++;
        }
    }
    file.close();

    file = SD.open(outsideLogFile, FILE_READ);
    while (file.available()) {
        String line = file.readStringUntil('\n');
        int timestamp;
        float temperature, humidity, pressure;
        sscanf(line.c_str(), "%d,%f,%f,%f", &timestamp, &temperature, &humidity, &pressure);

        if (currentTime - timestamp <= timeLimit) {
            json += ",{\"timestamp\":" + String(timestamp) + 
                    ",\"outside_temperature\":" + String(temperature, 1) + 
                    ",\"outside_humidity\":" + String(humidity, 1) +
                    ",\"outside_pressure\":" + String(pressure, 0) + "}";
        }
    }
    file.close();

    json += "]";
    File jsonFile = SD.open("/data.json", FILE_WRITE);
    if (jsonFile) {
        jsonFile.print(json);
        jsonFile.close();
    }
}