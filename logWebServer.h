#ifndef LOGWEBSERVER_H
#define LOGWEBSERVER_H

#include <ESPAsyncWebServer.h>
#include <SD.h>
#include <ArduinoJson.h>

// Web server object
extern AsyncWebServer logServer;  

void setupLogWebServer();
int getTimeLimit(const String &range);
void calculateMinMax(const char* filename, int timeLimit, StaticJsonDocument<512> &jsonDoc, const char* prefix);
String generateCSVData(File &file, String range);
void generateJSONData(String range);

#endif