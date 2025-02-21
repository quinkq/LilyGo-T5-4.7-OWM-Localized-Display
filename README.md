My take on #LilyGo-EPD-4-7-OWM-Weather-Display with adapting for own purposes.

<img src="https://github.com/user-attachments/assets/960a10ec-9fd1-4a30-b223-8aa1ab20414d" width="533" height="380">

Maintains original functionality - downloading OWM Weather via API, configuration AP, E-INK display handling and now:
- employed FreeRTOS to facilitate concurrent sensor readings, calculations, running webserver and future tasks
- introduced i2c bme280/sht40 sensors for localized temperature and humidity readouts
- added ESP-NOW wireless data exchanges with auxiliary esp32 that reads bme280/sht40 environment sensors and sends them to the master (esp32s3 handling e-ink display) in between deep sleep periods
- storing/buffering data as .csv on sd card
- async webserver for an easy access to recent and saved historical data using graphs, converting .csv to jsons
  
<img src="https://github.com/user-attachments/assets/ed6c1b58-6862-463b-b6b7-1f70694c3fef" width="533" height="450">

- figurationched webserver uses same async server now and offers OTA update functionality
- employed external tactile switch for display selection/switching to better handle available data (more to be added)

<img src="https://github.com/user-attachments/assets/95a5b678-3eb7-4179-ae55-b6922c1c2e5f" width="533" height="380">
<img src="https://github.com/user-attachments/assets/cc47ac2d-3de3-46f2-a563-65018a9e7be8" width="533" height="380">

- included modified original font converting script to create headers with polish diacritics

Planned:
- additional control commands repeating via ESP-NOW to auxillary ESP
- much needed optimization



