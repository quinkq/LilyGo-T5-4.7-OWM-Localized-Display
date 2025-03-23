#include "logWebServer.h"
#include <FS.h>
#include <time.h>

const char* insideLogFile = "/inside_log.csv";
const char* outsideLogFile = "/outside_log.csv";

AsyncWebServer logServer(80);

// Cache file validity time 15 minutes (in seconds)
const int JSON_CACHE_VALIDITY = 900;

// Track JSON generation status to prevent concurrent operations
volatile bool jsonGenerationInProgress = false;

// Queue for latest sensor readings
typedef struct {
    int32_t timestamp;
    float temperature;
    float humidity;
    float pressure;
    const char *filename;  // Name of the file to store data - "/outside_log.csv" or "/inside_log.csv"
    uint8_t batPercentage;
} SensorData;

SensorData latestOutside, latestInside;

// Handles
TaskHandle_t maintenanceTaskHandle = NULL;
QueueHandle_t serverLatestQueue;


void maintenanceTask(void *parameter) {
    SensorData tempData;
    uint8_t cleanCounter = 0;
    while(1) {
        // Queue for receiving data for /latest display - to avoid SD wear - requires both sensors to be successfully received
        for (int i = 0; i < 2; i++) {
            if (xQueueReceive(serverLatestQueue, &tempData, portMAX_DELAY) == pdTRUE){
                if (strcmp(tempData.filename, "/outside_log.csv") == 0) {
                    latestOutside = tempData;
                } else if (strcmp(tempData.filename, "/inside_log.csv") == 0) {
                    latestInside = tempData;
                }
            }
        }
        cleanCounter++;
        // After receiving 12 data sets (every 15 min) - roughly 3 hours, run cleanup for custom json's
        if(cleanCounter > 12){
            cleanupOldCustomJSONs();
            cleanCounter = 0;
        }
    }
}

void setupLogWebServer() {
    // Create maintenance task
    xTaskCreate(maintenanceTask, "maintenanceTask", 4096, NULL, 1, &maintenanceTaskHandle);

    logServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(SD, "/index.html", "text/html");
    });

    //Fetching latest
    logServer.on("/latest", HTTP_GET, [](AsyncWebServerRequest *request) {
        StaticJsonDocument<512> jsonDoc;
        
        // Check if queue has fresh data
        if (time(NULL) - ((latestOutside.timestamp + latestInside.timestamp)/2) <= 900) {
            Serial.println("[DEBUG] Using queue data for /latest.");
            jsonDoc["iT"] = roundToOneDecimal(latestInside.temperature);
            jsonDoc["iH"] = roundToOneDecimal(latestInside.humidity);
            jsonDoc["itS"] = latestInside.timestamp;
            jsonDoc["iBat"] = latestInside.batPercentage;
            jsonDoc["oT"] = roundToOneDecimal(latestOutside.temperature);
            jsonDoc["oH"] = roundToOneDecimal(latestOutside.humidity);
            jsonDoc["oP"] = roundToOneDecimal(latestOutside.pressure);
            jsonDoc["otS"] = latestOutside.timestamp;
            jsonDoc["oBat"] = latestOutside.batPercentage;
                
        } else {
            // Fallback to file reading if queue data isn't available
            Serial.println("[ERROR] /latest queue data outdated or invalid, falling back to file reading");
            char buffer[64];

            // Process inside data
            File file = SD.open(insideLogFile, FILE_READ);
            if (file) {
                char lastLine[64] = {0};
                while (file.available()) {
                    memset(buffer, 0, sizeof(buffer));
                    size_t bytesRead = file.readBytesUntil('\n', buffer, sizeof(buffer)-1);
                    if (bytesRead > 0) {
                        memcpy(lastLine, buffer, min(bytesRead, sizeof(lastLine)-1));
                        lastLine[min(bytesRead, sizeof(lastLine)-1)] = '\0';
                    }
                }
                file.close();
                
                if (strlen(lastLine) > 0) {
                    int timestamp;
                    float temperature, humidity;
                    sscanf(lastLine, "%d,%f,%f", &timestamp, &temperature, &humidity);
                    jsonDoc["iT"] = roundToOneDecimal(temperature);
                    jsonDoc["iH"] = roundToOneDecimal(humidity);
                    jsonDoc["itS"] = timestamp;
                    jsonDoc["iBat"] = latestInside.batPercentage;
                }
            }

            // Process outside data
            file = SD.open(outsideLogFile, FILE_READ);
            if (file) {
                char lastLine[64] = {0};
                while (file.available()) {
                    memset(buffer, 0, sizeof(buffer));
                    size_t bytesRead = file.readBytesUntil('\n', buffer, sizeof(buffer)-1);
                    if (bytesRead > 0) {
                        memcpy(lastLine, buffer, min(bytesRead, sizeof(lastLine)-1));
                        lastLine[min(bytesRead, sizeof(lastLine)-1)] = '\0';
                    }
                }
                file.close();
                
                if (strlen(lastLine) > 0) {
                    int timestamp;
                    float temperature, humidity, pressure;
                    sscanf(lastLine, "%d,%f,%f,%f", &timestamp, &temperature, &humidity, &pressure);
                    jsonDoc["oT"] = roundToOneDecimal(temperature);
                    jsonDoc["oH"] = roundToOneDecimal(humidity);
                    jsonDoc["oP"] = roundToOneDecimal(pressure);
                    jsonDoc["otS"] = timestamp;
                    jsonDoc["oBat"] = latestOutside.batPercentage;
                }
            }
        }
    
        char response[256];
        serializeJson(jsonDoc, response, sizeof(response));
        Serial.printf("[DEBUG] Sending response: %s\n", response);
        request->send(200, "application/json", response);
    });

    // Min/Max/Avg Data
    logServer.on("/minmax", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("range")) {
            request->send(400, "text/plain", "Missing range parameter");
            return;
        }

        const char* range = request->getParam("range")->value().c_str();
        int range_hours = getTimeLimitHours(range);

        StaticJsonDocument<1024> jsonDoc;
        calculateMinMaxAvg(insideLogFile, range_hours, jsonDoc, "inside");
        calculateMinMaxAvg(outsideLogFile, range_hours, jsonDoc, "outside");

        char response[1024];
        serializeJson(jsonDoc, response, sizeof(response));
        request->send(200, "application/json", response);
    });

    // JSON data for charts with streaming
    logServer.on("/chart-data", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("range")) {
            request->send(400, "text/plain", "Missing range parameter");
            Serial.println("[ERROR] Missing range parameter in request!");
            return;
        }
    
        const char* range = request->getParam("range")->value().c_str();
        Serial.printf("[DEBUG] Received /chart-data request for range: %s\n", range);
    
        char insideFile[32], outsideFile[32];
        snprintf(insideFile, sizeof(insideFile), "/inside_%s.json", range);
        snprintf(outsideFile, sizeof(outsideFile), "/outside_%s.json", range);
    
        Serial.printf("[DEBUG] Expected file names: %s | %s\n", insideFile, outsideFile);
    
        // Check if files exist and are up-to-date (15 min)
        bool needsGeneration = true;
        
        if (SD.exists(insideFile) && SD.exists(outsideFile)) {
            File file = SD.open(insideFile, FILE_READ);
            if (file) {
                time_t fileTime = file.getLastWrite();
                time_t currentTime = time(nullptr);
                file.close();
                
                if (currentTime - fileTime < JSON_CACHE_VALIDITY) {
                    needsGeneration = false;
                    Serial.printf("[INFO] Using cached JSON files for displaying chart data (age: %ld seconds)\n", 
                                  currentTime - fileTime);
                }
            }
        }
    
        if (needsGeneration) {
            if (!generateStreamingJSONData(range)) {
                request->send(500, "text/plain", "Failed to generate data");
                return;
            }
        }
    
        // Read Inside JSON
        String insideData = "[]";
        File file = SD.open(insideFile, FILE_READ);
        if (file) {
            size_t size = file.size();
            if (size > 0) {
                char* buffer = (char*)malloc(size + 1);
                if (buffer) {
                    size_t bytesRead = file.readBytes(buffer, size);
                    buffer[bytesRead] = '\0';
                    insideData = buffer;
                    free(buffer);
                }
            }
            file.close();
        } else {
            Serial.printf("[ERROR] Missing file: %s\n", insideFile);
        }
    
        // Read Outside JSON
        String outsideData = "[]";
        file = SD.open(outsideFile, FILE_READ);
        if (file) {
            size_t size = file.size();
            if (size > 0) {
                char* buffer = (char*)malloc(size + 1);
                if (buffer) {
                    size_t bytesRead = file.readBytes(buffer, size);
                    buffer[bytesRead] = '\0';
                    outsideData = buffer;
                    free(buffer);
                }
            }
            file.close();
        } else {
            Serial.printf("[ERROR] Missing file: %s\n", outsideFile);
        }
    
        String response = "{\"inside\": " + insideData + ", \"outside\": " + outsideData + "}";
        request->send(200, "application/json", response);
    });

    // New endpoint to list available files for download
    logServer.on("/list-files", HTTP_GET, [](AsyncWebServerRequest *request) {
        File root = SD.open("/");
        if (!root) {
            request->send(500, "text/plain", "Failed to open root directory");
            return;
        }
        
        StaticJsonDocument<1024> jsonDoc;
        JsonArray filesArray = jsonDoc.createNestedArray("files");
        
        File file = root.openNextFile();
        while (file) {
            const char* filename = file.name();
            if (strstr(filename, ".csv") != NULL && 
                (strstr(filename, "inside_log") != NULL || strstr(filename, "outside_log") != NULL)) {
                filesArray.add(filename);
            }
            file.close();
            file = root.openNextFile();
        }
        root.close();
        
        String response;
        serializeJson(jsonDoc, response);
        request->send(200, "application/json", response);
    });

    // New endpoint to download a specific file
    logServer.on("/download", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("file")) {
            request->send(400, "text/plain", "Missing file parameter");
            return;
        }
        
        String filename = request->getParam("file")->value();
        
        // Security check - only allow CSV files to be downloaded
        if (!filename.endsWith(".csv")) {
            request->send(403, "text/plain", "Only CSV files can be downloaded");
            return;
        }
        
        // For safety, prepend with slash if not already present
        if (!filename.startsWith("/")) {
            filename = "/" + filename;
        }
        
        if (!SD.exists(filename)) {
            request->send(404, "text/plain", "File not found");
            return;
        }
        
        // Setting appropriate headers for file download
        AsyncWebServerResponse *response = request->beginResponse(SD, filename, "text/csv");
        response->addHeader("Content-Disposition", "attachment; filename=" + filename.substring(filename.lastIndexOf('/') + 1));
        request->send(response);
    });

    // Update your download-multiple endpoint to use this class
    logServer.on("/download-multiple", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("pattern")) {
            request->send(400, "text/plain", "Missing pattern parameter");
            return;
        }
        
        String pattern = request->getParam("pattern")->value();
        
        // Security check - only allow certain patterns
        if (pattern != "all_inside" && pattern != "all_outside") {
            request->send(403, "text/plain", "Invalid pattern parameter");
            return;
        }
        
        String zipFilename = "/" + pattern + ".zip";
        
        // Create a zip file
        SimpleZipCreator zipper(zipFilename.c_str());
        if (!zipper.isOpen()) {
            request->send(500, "text/plain", "Failed to create zip file");
            return;
        }
        
        // Find matching files
        File root = SD.open("/");
        if (!root) {
            request->send(500, "text/plain", "Failed to open root directory");
            return;
        }
        
        bool filesAdded = false;
        File file = root.openNextFile();
        while (file) {
            String filename = file.name();
            
            bool shouldInclude = false;
            if (pattern == "all_inside" && filename.indexOf("inside_log") >= 0) {
                shouldInclude = true;
            } else if (pattern == "all_outside" && filename.indexOf("outside_log") >= 0) {
                shouldInclude = true;
            }
            
            if (shouldInclude) {
                zipper.addFile(filename.c_str());
                filesAdded = true;
            }
            
            file.close();
            file = root.openNextFile();
        }
        root.close();
        
        // Zipper destructor will finalize the ZIP file
        
        if (!filesAdded) {
            SD.remove(zipFilename);
            request->send(404, "text/plain", "No matching files found");
            return;
        }
        
        // Serve the ZIP file
        AsyncWebServerResponse *response = request->beginResponse(SD, zipFilename, "application/zip");
        response->addHeader("Content-Disposition", "attachment; filename=" + zipFilename.substring(zipFilename.lastIndexOf('/') + 1));
        request->send(response);
        
        // Note: You might want to delete the ZIP file after serving it,
        // but AsyncWebServer doesn't provide an easy way to do this.
        // You may need to implement a cleanup task to remove old ZIP files.
    });

    logServer.begin();
    Serial.println("Web logServer started.");
}

// Round to one decimal place reliably
float roundToOneDecimal(float value) {
    return round(value * 10.0) / 10.0;
}

// Determine time range in hours
int getTimeLimitHours(const char* range) {
    if (strcmp(range, "24h") == 0) return 24;
    if (strcmp(range, "week") == 0) return 168;
    if (strcmp(range, "month") == 0) return 720;
    if (strcmp(range, "year") == 0) return 8760;
    
    if (strncmp(range, "custom_", 7) == 0) {
        int startTime, endTime;
        sscanf(range, "custom_%d_%d", &startTime, &endTime);
        return (endTime - startTime) / 3600; // Convert seconds to hours
    }
    return 24;  // Default
}

// Min/Max/Avg calculation
void calculateMinMaxAvg(const char* filename, int range_hours, JsonDocument &jsonDoc, const char* prefix) {
    File file = SD.open(filename, FILE_READ);
    if (!file) return;

    float minTemp = 999, maxTemp = -999, sumTemp = 0;
    float minHumidity = 999, maxHumidity = -999, sumHumidity = 0;
    float minPressure = 9999, maxPressure = -9999, sumPressure = 0;
    int count = 0, currentTime = time(nullptr);
    int timeLimit = currentTime - (range_hours * 3600);

    int minTempTime = 0, maxTempTime = 0;
    int minHumidityTime = 0, maxHumidityTime = 0;
    int minPressureTime = 0, maxPressureTime = 0;

    char line[64];
    while (file.available()) {
        size_t bytesRead = file.readBytesUntil('\n', line, sizeof(line)-1);
        line[bytesRead] = '\0';
        
        int timestamp;
        float temperature, humidity, pressure = 0;
        if (strcmp(prefix, "outside") == 0) {
            sscanf(line, "%d,%f,%f,%f", &timestamp, &temperature, &humidity, &pressure);
        } else {
            sscanf(line, "%d,%f,%f", &timestamp, &temperature, &humidity);
        }

        if (timestamp >= timeLimit) {
            // Temperature
            if (temperature < minTemp) {
                minTemp = temperature;
                minTempTime = timestamp;
            }
            if (temperature > maxTemp) {
                maxTemp = temperature;
                maxTempTime = timestamp;
            }
            sumTemp += temperature;

            // Humidity
            if (humidity < minHumidity) {
                minHumidity = humidity;
                minHumidityTime = timestamp;
            }
            if (humidity > maxHumidity) {
                maxHumidity = humidity;
                maxHumidityTime = timestamp;
            }
            sumHumidity += humidity;

            // Pressure (Only for outside)
            if (strcmp(prefix, "outside") == 0) {
                if (pressure < minPressure) {
                    minPressure = pressure;
                    minPressureTime = timestamp;
                }
                if (pressure > maxPressure) {
                    maxPressure = pressure;
                    maxPressureTime = timestamp;
                }
                sumPressure += pressure;
            }
            count++;
        }
    }
    file.close();

    if (count > 0) {
        char fieldName[32];
        
        // Temperature
        snprintf(fieldName, sizeof(fieldName), "%s_temp_min", prefix);
        jsonDoc[fieldName] = roundToOneDecimal(minTemp);
        
        snprintf(fieldName, sizeof(fieldName), "%s_temp_min_time", prefix);
        jsonDoc[fieldName] = minTempTime;
        
        snprintf(fieldName, sizeof(fieldName), "%s_temp_max", prefix);
        jsonDoc[fieldName] = roundToOneDecimal(maxTemp);
        
        snprintf(fieldName, sizeof(fieldName), "%s_temp_max_time", prefix);
        jsonDoc[fieldName] = maxTempTime;
        
        snprintf(fieldName, sizeof(fieldName), "%s_temp_avg", prefix);
        jsonDoc[fieldName] = roundToOneDecimal(sumTemp / count);

        // Humidity
        snprintf(fieldName, sizeof(fieldName), "%s_humidity_min", prefix);
        jsonDoc[fieldName] = roundToOneDecimal(minHumidity);
        
        snprintf(fieldName, sizeof(fieldName), "%s_humidity_min_time", prefix);
        jsonDoc[fieldName] = minHumidityTime;
        
        snprintf(fieldName, sizeof(fieldName), "%s_humidity_max", prefix);
        jsonDoc[fieldName] = roundToOneDecimal(maxHumidity);
        
        snprintf(fieldName, sizeof(fieldName), "%s_humidity_max_time", prefix);
        jsonDoc[fieldName] = maxHumidityTime;
        
        snprintf(fieldName, sizeof(fieldName), "%s_humidity_avg", prefix);
        jsonDoc[fieldName] = roundToOneDecimal(sumHumidity / count);

        // Pressure (only for outside)
        if (strcmp(prefix, "outside") == 0) {
            jsonDoc["outside_pressure_min"] = roundToOneDecimal(minPressure);
            jsonDoc["outside_pressure_min_time"] = minPressureTime;
            jsonDoc["outside_pressure_max"] = roundToOneDecimal(maxPressure);
            jsonDoc["outside_pressure_max_time"] = maxPressureTime;
            jsonDoc["outside_pressure_avg"] = roundToOneDecimal(sumPressure / count);
        }
    } else {
        Serial.printf("[WARNING] No data found for range: %d hours\n", range_hours);
    }
}

// This function streams data and writes directly to file
bool generateStreamingJSONData(const char* range) {
    char insideFilename[32], outsideFilename[32];
    int startTime = 0, endTime = 0;
    bool isCustom = false;

    if (strncmp(range, "custom_", 7) == 0) {
        isCustom = true;
        if (sscanf(range, "custom_%d_%d", &startTime, &endTime) != 2) {
            Serial.printf("[ERROR] Invalid custom range format: %s\n", range);
            return false;
        }
        snprintf(insideFilename, sizeof(insideFilename), "/inside_%s.json", range);
        snprintf(outsideFilename, sizeof(outsideFilename), "/outside_%s.json", range);
    } else {
        snprintf(insideFilename, sizeof(insideFilename), "/inside_%s.json", range);
        snprintf(outsideFilename, sizeof(outsideFilename), "/outside_%s.json", range);
    }

    if (jsonGenerationInProgress) {
        Serial.printf("[WARNING] JSON generation already in progress. Skipping request for: %s\n", range);
        return false;
    }
    jsonGenerationInProgress = true;

    Serial.printf("[INFO] Generating JSON for range: %s\n", range);
    
    int currentTime = time(nullptr);
    int timeLimit = isCustom ? startTime : (currentTime - getTimeLimitHours(range) * 3600);
    
    // Dynamic Averaging Rules
    int aggregationStep = 300;  // Default: 5-minute intervals
    if (getTimeLimitHours(range) > 168) aggregationStep = 3600;  // Hourly averages if selected range >7 days
    if (getTimeLimitHours(range) > 672) aggregationStep = 86400;  // Daily averages if selected range >30 days
    if (getTimeLimitHours(range) > 8760) aggregationStep = 604800;  // Weekly averages if selected range >1 year

    // Process inside data - stream to file
    if (!streamProcessLogToJSON(insideLogFile, insideFilename, timeLimit, aggregationStep, false, 
                    startTime, endTime, isCustom)) {
        jsonGenerationInProgress = false;
        return false;
    }
    
    // Process outside data - stream to file
    if (!streamProcessLogToJSON(outsideLogFile, outsideFilename, timeLimit, aggregationStep, true, 
                   startTime, endTime, isCustom)) {
        jsonGenerationInProgress = false;
        return false;
    }

    jsonGenerationInProgress = false;
    return true;
}

// Stream process data from CSV file directly to JSON file
bool streamProcessLogToJSON(const char* inputFilename, const char* outputFilename, int timeLimit, 
                           int aggregationStep, bool isOutsideData, int startTime = 0, 
                           int endTime = 0, bool isCustom = false) {
    File inputFile = SD.open(inputFilename, FILE_READ);
    if (!inputFile) {
        Serial.printf("[ERROR] Failed to open log file: %s\n", inputFilename);
        return false;
    }
    
    // Remove output file if exists
    if (SD.exists(outputFilename)) {
        SD.remove(outputFilename);
    }
    
    File outputFile = SD.open(outputFilename, FILE_WRITE);
    if (!outputFile) {
        Serial.printf("[ERROR] Failed to create output file: %s\n", outputFilename);
        inputFile.close();
        return false;
    }
    
    // Start JSON array
    outputFile.print("[");
    
    int lastTimestamp = 0, count = 0;
    float tempSum = 0, humiditySum = 0, pressureSum = 0;
    bool firstObject = true;
    
    char line[64];
    while (inputFile.available()) {
        size_t bytesRead = inputFile.readBytesUntil('\n', line, sizeof(line)-1);
        line[bytesRead] = '\0';
        
        int timestamp;
        float temperature, humidity, pressure = 0;
        
        if (isOutsideData) {
            sscanf(line, "%d,%f,%f,%f", &timestamp, &temperature, &humidity, &pressure);
        } else {
            sscanf(line, "%d,%f,%f", &timestamp, &temperature, &humidity);
        }
        
        bool inRange;
        if (isCustom) {
            inRange = (timestamp >= startTime && timestamp <= endTime);
        } else {
            inRange = (timestamp >= timeLimit);
        }
        
        if (inRange) {
            if (lastTimestamp == 0) {
                lastTimestamp = timestamp;
            }
            
            if (timestamp - lastTimestamp >= aggregationStep) {
                if (count > 0) {
                    // Write current aggregated data point
                    if (!firstObject) {
                        outputFile.print(",");
                    }
                    firstObject = false;
                    
                    outputFile.print("{\"tS\":");
                    outputFile.print(lastTimestamp);
                    outputFile.print(",\"T\":");
                    outputFile.print(roundToOneDecimal(tempSum / count), 1);
                    outputFile.print(",\"H\":");
                    outputFile.print(roundToOneDecimal(humiditySum / count), 1);
                    
                    if (isOutsideData) {
                        outputFile.print(",\"P\":");
                        outputFile.print(roundToOneDecimal(pressureSum / count), 1);
                    }
                    
                    outputFile.print("}");
                }
                lastTimestamp = timestamp;
                count = 0;
                tempSum = humiditySum = pressureSum = 0;
            }
            
            tempSum += temperature;
            humiditySum += humidity;
            if (isOutsideData) {
                pressureSum += pressure;
            }
            count++;
        }
    }
    
    // Write the last batch if any
    if (count > 0) {
        if (!firstObject) {
            outputFile.print(",");
        }
        
        outputFile.print("{\"tS\":");
        outputFile.print(lastTimestamp);
        outputFile.print(",\"T\":");
        outputFile.print(roundToOneDecimal(tempSum / count), 1);
        outputFile.print(",\"H\":");
        outputFile.print(roundToOneDecimal(humiditySum / count), 1);
        
        if (isOutsideData) {
            outputFile.print(",\"P\":");
            outputFile.print(roundToOneDecimal(pressureSum / count), 1);
        }
        
        outputFile.print("}");
    }
    
    // Close JSON array
    outputFile.print("]");
    
    inputFile.close();
    outputFile.close();
    
    return true;
}

// Cleanup old custom JSON files (older than 12h)
void cleanupOldCustomJSONs() {
    File root = SD.open("/");
    if (!root) {
        Serial.println("[ERROR] Failed to open root directory for cleanup");
        return;
    }
    
    File file = root.openNextFile();
    while (file) {
        const char* name = file.name();
        if (strncmp(name, "/custom_", 8) == 0) {
            time_t createdTime = file.getLastWrite();
            if (time(nullptr) - createdTime > 43200) {  // 12 hours
                file.close();
                SD.remove(name);
                Serial.printf("[INFO] Removed old custom JSON: %s\n", name);
                file = root.openNextFile();
                continue;
            }
        }
        file = root.openNextFile();
    }
    root.close();
}

