#include <stdio.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_err.h>
#include <bmp280.h>
#include <sht4x.h>

#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <time.h>
#include <sys/time.h>

#define TAG "Outside-ESP"


#ifndef APP_CPU_NUM
#define APP_CPU_NUM PRO_CPU_NUM
#endif

// Semaphore handles
static SemaphoreHandle_t espNowSyncSem;
static SemaphoreHandle_t SHT4XTriggerSem;
static SemaphoreHandle_t BME280TriggerSem;
static SemaphoreHandle_t sensorDataReadySem;
static SemaphoreHandle_t dataTransSem;
static SemaphoreHandle_t historyCalcMutex;

// Queue handles
QueueHandle_t sensorDataQueue;
QueueHandle_t processedDataQueue;

// Define structure for sensor data operations
typedef enum {
    SENSOR_NONE = 0,
    SENSOR_BME280,
    SENSOR_SHT4X
} SensorType;

typedef struct {
    SensorType tag;
    float temperature;
    float humidity;
    float pressure;
} TaggedSensorData;

typedef struct {
    float temperature;
    float humidity;
    float pressure;
    int32_t timestamp;
} SensorData;

// History for data bias calculation
int32_t historyIndex = 0;
int32_t numReadings = 0;
SensorData readingHistory[3] = {{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};

// Network properties / partner ESP32 MAC Address
#define MAX_SCAN_RESULTS 10
const char* knownSSIDs[] = {"caladan", "2.4G-Zabezpieczone"}; // Set your target Wi-Fi SSID here for channel scanning
const int numSSIDs = sizeof(knownSSIDs) / sizeof(knownSSIDs[0]);
uint8_t receiverMac[] = {0x84, 0xFC, 0xE6, 0x6C, 0xD2, 0x48};

//######################## Function prototypes ########################//
void syncTimeFromNTP(int32_t receivedTime);
uint8_t getWiFiChannel(const char *knownSSIDs[], int numSSIDs);

void StartWiFi()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

uint8_t getWiFiChannel(const char *knownSSIDs[], int numSSIDs) 
{
    ESP_LOGI(TAG, "Scanning for Wi-Fi networks...");
    esp_wifi_scan_start(NULL, true);  // Blocking scan

    uint16_t numNetworks = 0;
    esp_wifi_scan_get_ap_num(&numNetworks);
    if (numNetworks == 0) 
    {
        ESP_LOGW(TAG, "No Wi-Fi networks found!");
        return 0;
    }

    wifi_ap_record_t apRecords[numNetworks]; // Store scan results
    esp_wifi_scan_get_ap_records(&numNetworks, apRecords);

    // Loop through found networks
    for (int i = 0; i < numNetworks; i++)
    {
        ESP_LOGI(TAG, "Found: %s on Channel: %d", apRecords[i].ssid, apRecords[i].primary);

        // Check if found SSID matches any known network
        for (int j = 0; j < numSSIDs; j++) 
        {
            if (strcmp((char *)apRecords[i].ssid, knownSSIDs[j]) == 0) 
            {
                ESP_LOGI(TAG, "Found target Wi-Fi: '%s' on Channel: %d", knownSSIDs[j], apRecords[i].primary);
                return apRecords[i].primary; // Return the correct channel
            }
        }
    }

    ESP_LOGW(TAG, "No known Wi-Fi networks found!");
    return 0; // Return 0 if no known network is found
}


/////////////// ESP-NOW receive callback (handles NTP sync)

void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) 
{
    const uint8_t *mac = info->src_addr; // Extract MAC address
    ESP_LOGI(TAG, "ESP-NOW packet received from %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
             
    if (len == sizeof(SensorData)) 
    {
        SensorData receivedData;
        memcpy(&receivedData, incomingData, sizeof(SensorData));

        if (receivedData.timestamp > 0)
        {
            ESP_LOGI(TAG, "Received NTP update: %" PRId32, receivedData.timestamp);
            syncTimeFromNTP(receivedData.timestamp);
        } 
        else 
        {
            ESP_LOGW(TAG, "Received invalid NTP update");

        }
        // Signal that an ESP-NOW response was received
        xSemaphoreGive(espNowSyncSem);
    }
}

// Sync internal RTC clock with received NTP time
void syncTimeFromNTP(int32_t receivedTime) 
{
    struct timeval tv = { .tv_sec = receivedTime, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    ESP_LOGI(TAG, "System clock updated from NTP: %" PRId32, receivedTime);
}

//////////////TASKS///////////////////////////

// Transmit sensor data and wait for NTP response
void SendDataTask(void *pvParameters)
{
    SensorData processedData = {0};
    SensorData sendingData = {0};
    
    if (xQueueReceive(processedDataQueue, &processedData, pdMS_TO_TICKS(10000)) == pdTRUE)
    {
        sendingData = processedData;
        ESP_LOGI(TAG, "Received data from ProcessDataTask");
        
        int attempts = 0;
        bool ackReceived = false;

        while (attempts < 3 && !ackReceived)
        {
            ESP_LOGI(TAG, "Sending sensor data...");
            esp_err_t result = esp_now_send(receiverMac, (uint8_t *)&sendingData, sizeof(SensorData));

            if (result == ESP_OK) 
            {
                ESP_LOGI(TAG, "Message sent, waiting for a response...");
                // Wait for response with 10-second timeout
                if (xSemaphoreTake(espNowSyncSem, pdMS_TO_TICKS(10000)) == pdTRUE) 
                {
                    ESP_LOGI(TAG, "Transmission acknowledged.");
                    ackReceived = true;
                }
                else 
                {
                    ESP_LOGW(TAG, "No response received, retrying...");
                }
            } 
            else 
            {
                ESP_LOGE(TAG, "ESP-NOW transmission failed, retrying...");
            }

            attempts++;
            vTaskDelay(pdMS_TO_TICKS(3000)); // Wait before retrying
        }
        
        if(ackReceived == false)
        {
            ESP_LOGW(TAG, "Failed to transmit. Proceeding...");
        }
    }
    else
    {
        ESP_LOGE(TAG, "Failed to receive data from ProcessDataTask");
    }
    
    xSemaphoreGive(dataTransSem);
    vTaskDelete(NULL);
}

void SHT4xReadTask(void *pvParameters)
{
    static sht4x_t dev;
    TaggedSensorData sht4xdata;
    sht4xdata.tag = SENSOR_SHT4X;
    sht4xdata.pressure = 0;

    //Init SHT4x sensor
    memset(&dev, 0, sizeof(sht4x_t));
    ESP_ERROR_CHECK(sht4x_init_desc(&dev, 0, CONFIG_EXAMPLE_I2C_MASTER_SDA, CONFIG_EXAMPLE_I2C_MASTER_SCL));
    ESP_ERROR_CHECK(sht4x_init(&dev));

    // Get the measurement duration for high repeatability;
    uint8_t duration = sht4x_get_measurement_duration(&dev);
    //while (1)
    //{
        // Wait for the semaphore to be given by the main task to start measurement
        //ESP_LOGI("SHT4X", "Waiting for semaphores...");
        //xSemaphoreTake(SHT4XTriggerSem, portMAX_DELAY);
        
        // Trigger one measurement in single shot mode with high repeatability.
        ESP_ERROR_CHECK(sht4x_start_measurement(&dev));
        // Wait until measurement is ready (duration returned from *sht4x_get_measurement_duration*).
        vTaskDelay(duration); // duration is in ticks

        // retrieve the values and send it to the queue
        //ESP_ERROR_CHECK(sht4x_get_results(&dev, &sht4xdata.temperature, &sht4xdata.humidity));
        if(sht4x_get_results(&dev, &sht4xdata.temperature, &sht4xdata.humidity) == ESP_OK)
        {
            ESP_LOGI("SHT40", "Timestamp: %lu, SHT40  - Temperature: %.2f °C, Humidity: %.2f %%", (unsigned long)xTaskGetTickCount(), sht4xdata.temperature, sht4xdata.humidity);
            if (xQueueSend(sensorDataQueue, &sht4xdata, pdMS_TO_TICKS(2000)) != pdPASS) 
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
        xSemaphoreGive(sensorDataReadySem);
    //}
    vTaskDelete(NULL);
}

void BME280ReadTask(void *pvParameters)
{
    bmp280_t dev;
    bmp280_params_t params;

    TaggedSensorData bme280data;
    bme280data.tag = SENSOR_BME280;

    // Initialize the sensor
    bmp280_init_default_params(&params);
    memset(&dev, 0, sizeof(bmp280_t));
    ESP_ERROR_CHECK(bmp280_init_desc(&dev, BMP280_I2C_ADDRESS_0, 0, CONFIG_EXAMPLE_I2C_MASTER_SDA, CONFIG_EXAMPLE_I2C_MASTER_SCL));
    ESP_ERROR_CHECK(bmp280_init(&dev, &params));
    bool bme280p = dev.id == BME280_CHIP_ID;
    printf("BMP280: found %s\n", (bme280p ? "BME280" : "BMP280"));

    //while (1)
    //{
        //ESP_LOGI("BME280", "Waiting for semaphores...");
        //xSemaphoreTake(BME280TriggerSem, portMAX_DELAY);

        // Set the sensor to forced mode to initiate a measurement
        ESP_ERROR_CHECK(bmp280_force_measurement(&dev));

        // Wait for the measurement to complete
        bool busy;
        do
        {
            ESP_ERROR_CHECK(bmp280_is_measuring(&dev, &busy));
            if (busy)
            vTaskDelay(pdMS_TO_TICKS(5)); // Wait for 5ms before checking again
        } while (busy);

        // Read the measurement results
        if (bmp280_read_float(&dev, &bme280data.temperature, &bme280data.pressure, &bme280data.humidity) == ESP_OK)
        {
            ESP_LOGI("BME280", "Timestamp: %lu, BME280 - Temperature: %.2f °C, Humidity: %.2f %%, Pressure: %.2f hPa", (unsigned long)xTaskGetTickCount(),
             bme280data.temperature, bme280data.humidity, bme280data.pressure/100);

            // Send data to the queue
            if (xQueueSend(sensorDataQueue, &bme280data, pdMS_TO_TICKS(2000)) != pdPASS) 
            {
                ESP_LOGI("BME280", "Failed to send data to sensorDataQueue in time");
            }
            else
            {
                ESP_LOGI("BME280", "BME280 Data sent to sensorDataQueue");
            }
        }
        else
        {
            ESP_LOGI("BME280", "BME280 Failed to read sensor data.");
        }
        // Since the sensor automatically goes to sleep after forced mode, we don't need an explicit sleep mode call.
        xSemaphoreGive(sensorDataReadySem);
    //}
    vTaskDelete(NULL);
}

void ProcessDataTask(void *pvParameters) 
{
    TaggedSensorData bme280data = {SENSOR_NONE, 0, 0, 0};
    TaggedSensorData sht4xdata = {SENSOR_NONE, 0, 0, 0};
    TaggedSensorData receivedData = {SENSOR_NONE, 0, 0, 0};

    SensorData calculationData = {0};
    SensorData processedResult = {0};

    bool bme280Ready = false; 
    bool sht4xReady = false;

    TickType_t startTime;

    while (1) 
    {
        // Wait for both sensors to be ready (3 sec)
        ESP_LOGI("DATA", "Waiting for semaphores...");
        xSemaphoreTake(sensorDataReadySem, portMAX_DELAY); 
        xSemaphoreTake(sensorDataReadySem, pdMS_TO_TICKS(1000));

        // Reset historical average before recalculating
        SensorData historicAverage = {0};

        // Wait 3 seconds TOTAL for both sensors
        startTime = xTaskGetTickCount();
        while ((!bme280Ready || !sht4xReady) && ((xTaskGetTickCount() - startTime) < pdMS_TO_TICKS(2000))) // Unless both sensors data has been received, waits for 3 seconds total (1000ms in semaphore delay already)
        {
            if (xQueueReceive(sensorDataQueue, &receivedData, pdMS_TO_TICKS(100)) == pdTRUE) // check if both sensors data were received every 100ms
            {
                // BME280
                if (receivedData.tag == SENSOR_BME280) 
                {  
                    bme280data = receivedData;
                    bme280Ready = true;
                    ESP_LOGI("DATA", "BME280 data received");
                }
                // SHT4X
                else if (receivedData.tag == SENSOR_SHT4X) 
                {  
                    sht4xdata = receivedData;
                    sht4xReady = true;
                    ESP_LOGI("DATA", "SHT4X data received");
                }
            }
        }
        // Process data when BOTH sensors are ready
        if (bme280Ready && sht4xReady) 
        {
            calculationData.temperature = (bme280data.temperature + sht4xdata.temperature) / 2.0; // Average
            calculationData.humidity = (bme280data.humidity + sht4xdata.humidity - 5) / 2.0;   // Weighted average in favour of BME280; -5% bias correction for SHT40
            calculationData.pressure = bme280data.pressure / 100; // Pressure from BME280 only
            calculationData.timestamp = time(NULL);
            bme280Ready = false; // Reset readiness flags
            sht4xReady = false;
            ESP_LOGI("DATA", "Both sensor data available.");
        }
        // Process data when only BME280 is ready
        else if (bme280Ready && !sht4xReady) 
        {
            calculationData.temperature = bme280data.temperature;
            calculationData.humidity = bme280data.humidity;
            calculationData.pressure = bme280data.pressure / 100;
            calculationData.timestamp = time(NULL);
            bme280Ready = false; // Reset readiness flag
            ESP_LOGW("DATA", "Only BME280 data available.");
        }
        // Process data when only SHT40 is ready
        else if (!bme280Ready && sht4xReady) 
        {
            calculationData.temperature = sht4xdata.temperature;
            calculationData.humidity = sht4xdata.humidity - 5 ; // -5% bias correction
            calculationData.pressure = 404;
            calculationData.timestamp = time(NULL);
            sht4xReady = false; // Reset readiness flag
            ESP_LOGW("DATA", "Only SHT40 data available.");
        }
        else
        {
            vTaskDelay(pdMS_TO_TICKS(50));
            ESP_LOGE("DATA", "No sensor data available.");
            continue;
        }

        // Update historical data
        xSemaphoreTake(historyCalcMutex, portMAX_DELAY);
        readingHistory[historyIndex] = calculationData; // Update history with latest data
        historyIndex = (historyIndex + 1) % 3; //circular loop 0 -> 1 -> 2 -> 0, update up to 3 slots of historyIndex 
        numReadings = numReadings < 3 ? numReadings + 1 : 3; // Update number of readings, for checking if bias applies (>=3)

        // Calculate historical average
        if (numReadings > 1)
        {
            for (int i = 0; i < numReadings; i++) 
            {
                historicAverage.temperature += readingHistory[i].temperature;
                historicAverage.humidity += readingHistory[i].humidity;
                historicAverage.pressure += readingHistory[i].pressure;
            }
            historicAverage.temperature /= numReadings;
            historicAverage.humidity /= numReadings;
            historicAverage.pressure /= numReadings;
        }

        // Apply bias with weighted average
        if (numReadings >= 3)
        {
            processedResult.temperature = ((4 * calculationData.temperature) + historicAverage.temperature)/5;
            processedResult.humidity = ((4 * calculationData.humidity) + historicAverage.humidity)/5;
            processedResult.pressure = ((4 * calculationData.pressure) + historicAverage.pressure)/5;
            processedResult.timestamp = calculationData.timestamp;
        } 
        else 
        {
            processedResult = calculationData;
        }

        // Save readingHistory to NVS
        nvs_handle_t nvs_handle;
        if (nvs_open("storage", NVS_READWRITE, &nvs_handle) == ESP_OK) 
        {
            esp_err_t err = nvs_set_blob(nvs_handle, "sensor_history", readingHistory, sizeof(readingHistory));
            if (err == ESP_OK)
            {
                nvs_set_i32(nvs_handle, "history_index", historyIndex); // Save historyIndex
                nvs_set_i32(nvs_handle, "num_readings", numReadings); // Save numReadings
                nvs_commit(nvs_handle);
                ESP_LOGI(TAG, "Sensor history, index & numReadings saved to NVS.");
            }
            else 
            {
                ESP_LOGE(TAG, "Failed to save sensor history!");
            }
            nvs_close(nvs_handle);
        }

        xSemaphoreGive(historyCalcMutex);
        ESP_LOGI("DATA", "Processed Data: Temp=%.2f, Hum=%.2f, Press=%.2f", processedResult.temperature, processedResult.humidity, processedResult.pressure);

        // Pass processed data for sending
        if (xQueueSend(processedDataQueue, &processedResult, portMAX_DELAY) != pdTRUE) 
        {
            ESP_LOGW("DATA", "Failed to enqueue processed data.");
        } 
        else
        {
            ESP_LOGI("DATA", "Processed data enqueued.");
        }
    }
    //vTaskDelete(NULL);
}

void app_main()
{
    esp_log_level_set(TAG, ESP_LOG_INFO);
    ESP_LOGI(TAG, "Starting Outside-ESP...");

    // Set timezone at startup
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    
    // Initialize I2C
    ESP_ERROR_CHECK(i2cdev_init());

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) 
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // Load sensor history from NVS
    nvs_handle_t nvs_handle;
    size_t required_size = sizeof(readingHistory);
    int32_t oldestTimestamp = 0;

    if (nvs_open("storage", NVS_READONLY, &nvs_handle) == ESP_OK) 
    {
        nvs_get_blob(nvs_handle, "sensor_history", readingHistory, &required_size);
        nvs_get_i32(nvs_handle, "history_index", &historyIndex); // Load historyIndex
        nvs_get_i32(nvs_handle, "num_readings", &numReadings); // Load numReadings

        // Get the oldest timestamp in history
        oldestTimestamp = readingHistory[0].timestamp;

        nvs_close(nvs_handle);
    }

    // Data older than 1 hour is considered invalid and is reset (boot after long device off situation)
    if ((time(NULL) - oldestTimestamp) > 3600)
    { 
        ESP_LOGW(TAG, "History too old, resetting data...");
        memset(readingHistory, 0, sizeof(readingHistory)); // Clear stored readings
        numReadings = 0; // Reset numReadings
        historyIndex = 0; // Reset historyIndex
    }

    // Create queues with error checking
    sensorDataQueue = xQueueCreate(10, sizeof(TaggedSensorData));
    processedDataQueue = xQueueCreate(10, sizeof(SensorData));
    
    if (sensorDataQueue == NULL || processedDataQueue == NULL) 
    {
        ESP_LOGE("SETUP", "Failed to create queues");
        return;
    }
    ESP_LOGI("SETUP", "All queues created successfully");

    // Create semaphores
    espNowSyncSem = xSemaphoreCreateBinary();
    SHT4XTriggerSem = xSemaphoreCreateBinary();
    BME280TriggerSem = xSemaphoreCreateBinary();
    sensorDataReadySem = xSemaphoreCreateCounting(2, 0);
    dataTransSem = xSemaphoreCreateBinary();
    historyCalcMutex = xSemaphoreCreateMutex();

    // Create tasks
    xTaskCreate(SHT4xReadTask, "SHT4xReadTask", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL);
    xTaskCreate(BME280ReadTask, "BME280ReadTask", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL);
    xTaskCreate(ProcessDataTask, "ProcessDataTask", configMINIMAL_STACK_SIZE * 5, NULL, 4, NULL);
    
    // Initialize Wi-Fi in Station Mode
    StartWiFi();
    uint8_t wifiChannel = getWiFiChannel(knownSSIDs, numSSIDs);
    if (wifiChannel > 0) 
    {
        esp_wifi_set_channel(wifiChannel, WIFI_SECOND_CHAN_NONE);
        ESP_LOGI(TAG, "ESP-NOW set to Wi-Fi Channel: %d", wifiChannel);
    }
    else 
    {
        ESP_LOGW(TAG, "Failed to detect Wi-Fi channel, using default ESP-NOW channel.");
    }

    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW init failed!");
        return;
    }
    // Register callback for receiving data
    esp_now_register_recv_cb(onDataRecv);

    // Register ESP-NOW peer (ESP32-S3)
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, receiverMac, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add ESP-NOW peer");
        return;
    }

    xTaskCreate(SendDataTask, "SendDataTask", configMINIMAL_STACK_SIZE * 5, NULL, 3, NULL);
    xSemaphoreTake(dataTransSem, portMAX_DELAY);
    
    // Calculate next wake-up time
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);  // Convert UTC to local time
    
    // Extract current local time components
    int currentHour = timeinfo.tm_hour;
    int currentMin = timeinfo.tm_min;
    int currentSec = timeinfo.tm_sec;
    
    // Calculate next wake-up time aligned to :00, :15, :30, :45
    int nextWakeMin = ((currentMin / 15) + 1) * 15;  // Round up to the next 15-minute mark
    int extraMinutes = (nextWakeMin >= 60) ? 1 : 0;  // Handle hour overflow
    int nextWakeHour = (currentHour + extraMinutes) % 24;
    nextWakeMin = nextWakeMin % 60;  // Ensure minutes wrap correctly
    
    // Calculate sleep duration until next wake-up
    int sleepSeconds = ((nextWakeHour * 3600 + nextWakeMin * 60) - (currentHour * 3600 + currentMin * 60 + currentSec));
    if (sleepSeconds < 0) sleepSeconds += 24 * 3600;  // Handle midnight rollover
    
    int64_t sleepDuration = sleepSeconds * 1000000LL;  // Convert to microseconds
    
    ESP_LOGI(TAG, "Current Time: %02d:%02d:%02d", currentHour, currentMin, currentSec);
    ESP_LOGI(TAG, "Next Wake-Up at: %02d:%02d (in %d seconds)", nextWakeHour, nextWakeMin, sleepSeconds);
    ESP_LOGI(TAG, "Sleeping until next 15-min mark, duration: %lld sec", sleepDuration / 1000000);

    esp_deep_sleep(sleepDuration);

}

