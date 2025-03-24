My take on #LilyGo-EPD-4-7-OWM-Weather-Display with adapting for own purposes.

<img src="https://github.com/user-attachments/assets/33f88d13-cfac-4c53-ae1c-6c4ebd69de17" width="533" height="380">

Maintains original functionality - downloading OWM Weather via API, configuration AP, E-INK display handling and now:
- employed FreeRTOS to facilitate concurrent sensor readings, calculations, running webserver and future tasks
- introduced i2c sht40 sensor for localized temperature and humidity readouts
- added ESP-NOW wireless data exchanges with auxiliary esp32 that reads bme280/sht40 environment sensors and sends them to the master (esp32s3 handling e-ink display) in between deep sleep periods
- storing/buffering data as .csv on sd card
- async webserver for an easy access to recent and saved historical data using graphs, converting .csv to jsons
  
<img src="https://github.com/user-attachments/assets/f52dc17b-2dce-4d8e-971e-f06844adc592" width="533" height="450">

- configuration webserver uses same async server now and offers OTA update functionality
- employed external tactile switch for display selection/switching to better handle available data (more to be added)

<img src="https://github.com/user-attachments/assets/80480141-9f3e-458a-a08e-a9a692c5db92" width="533" height="380">
<img src="https://github.com/user-attachments/assets/d98e1fc9-a67f-4e72-9a7a-6392e64241f0" width="533" height="380">

- included modified original font converting script to create headers with polish diacritics

PLANNED:
- adding rainfall measurement and wind force/direction to the auxiliary ESP32 measuring station
- cataloging and sharing 3d models
- optimization


auxiliary station:

<img src="https://github.com/user-attachments/assets/1ca64978-6d8e-41b8-9564-cbe4c4ec27e0" width="533" height="380">
<img src="https://github.com/user-attachments/assets/95319df9-c829-40e5-94ff-6c9d3cd74f79" width="533" height="380">
<img src="https://github.com/user-attachments/assets/98b82852-1dbc-4b7d-8d0f-bd9042ccf343" width="533" height="380">




