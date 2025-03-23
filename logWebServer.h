#ifndef LOGWEBSERVER_H
#define LOGWEBSERVER_H

#include <ESPAsyncWebServer.h>
#include <SD.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <freertos/queue.h>


// Queue handle for sensor data from main
extern QueueHandle_t serverLatestQueue;

class SimpleZipCreator {
    private:
        File zipFile;
        uint32_t fileCount;
    
    public:
        SimpleZipCreator(const char* zipFilename) {
            // Create or overwrite ZIP file
            if (SD.exists(zipFilename)) {
                SD.remove(zipFilename);
            }
            
            zipFile = SD.open(zipFilename, FILE_WRITE);
            fileCount = 0;
        }
        
        ~SimpleZipCreator() {
            if (zipFile) {
                writeEndOfCentralDirectory();
                zipFile.close();
            }
        }
        
        bool isOpen() {
            return zipFile;
        }
        
        bool addFile(const char* filepath) {
            File srcFile = SD.open(filepath, FILE_READ);
            if (!srcFile) {
                return false;
            }
            
            // Extract filename without path
            const char* filename = filepath;
            if (strchr(filepath, '/')) {
                filename = strrchr(filepath, '/') + 1;
            }
            
            // Get file size
            size_t fileSize = srcFile.size();
            
            // Local file header
            zipFile.write((uint8_t*)"\x50\x4b\x03\x04", 4); // Local file header signature
            zipFile.write((uint8_t*)"\x0a\x00", 2);         // Version needed to extract
            zipFile.write((uint8_t*)"\x00\x00", 2);         // General purpose bit flag
            zipFile.write((uint8_t*)"\x00\x00", 2);         // Compression method (0=store)
            zipFile.write((uint8_t*)"\x00\x00", 2);         // File last modification time
            zipFile.write((uint8_t*)"\x00\x00", 2);         // File last modification date
            
            // CRC-32 of uncompressed data (0 for simplicity)
            zipFile.write((uint8_t*)"\x00\x00\x00\x00", 4);
            
            // Compressed size (same as uncompressed since we're not compressing)
            zipFile.write((uint8_t*)&fileSize, 4);
            
            // Uncompressed size
            zipFile.write((uint8_t*)&fileSize, 4);
            
            // File name length
            uint16_t nameLength = strlen(filename);
            zipFile.write((uint8_t*)&nameLength, 2);
            
            // Extra field length
            zipFile.write((uint8_t*)"\x00\x00", 2);
            
            // File name
            zipFile.write((uint8_t*)filename, nameLength);
            
            // File data
            uint8_t buffer[512];
            while (srcFile.available()) {
                size_t bytesRead = srcFile.read(buffer, sizeof(buffer));
                zipFile.write(buffer, bytesRead);
            }
            
            srcFile.close();
            fileCount++;
            return true;
        }
        
    private:
        void writeEndOfCentralDirectory() {
            // This is a very simplified implementation
            // A proper implementation would need to include the central directory
            // For each file before this record
            
            zipFile.write((uint8_t*)"\x50\x4b\x05\x06", 4); // End of central directory signature
            zipFile.write((uint8_t*)"\x00\x00", 2);         // Number of this disk
            zipFile.write((uint8_t*)"\x00\x00", 2);         // Disk where central directory starts
            zipFile.write((uint8_t*)&fileCount, 2);         // Number of central directory records on this disk
            zipFile.write((uint8_t*)&fileCount, 2);         // Total number of central directory records
            zipFile.write((uint8_t*)"\x00\x00\x00\x00", 4); // Size of central directory
            zipFile.write((uint8_t*)"\x00\x00\x00\x00", 4); // Offset of start of central directory
            zipFile.write((uint8_t*)"\x00\x00", 2);         // Comment length
        }
    };

void setupLogWebServer();
float roundToOneDecimal(float value);
int getTimeLimitHours(const char* range);
void calculateMinMaxAvg(const char* filename, int range_hours, JsonDocument &jsonDoc, const char* prefix);
bool generateStreamingJSONData(const char* range);
bool streamProcessLogToJSON(const char* inputFilename, const char* outputFilename, int timeLimit, 
                          int aggregationStep, bool isOutsideData, int startTime, int endTime, bool isCustom);
void cleanupOldCustomJSONs();

// Function to update latest sensor data from main loop
void updateLatestSensorData(const char* type, int timestamp, float temperature, float humidity, float pressure = 0);

#endif /* LOGWEBSERVER_H */