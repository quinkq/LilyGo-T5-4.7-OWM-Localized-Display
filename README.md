My take on #LilyGo-EPD-4-7-OWM-Weather-Display with adapting for own purposes.

<img src="https://github.com/user-attachments/assets/960a10ec-9fd1-4a30-b223-8aa1ab20414d" width="533" height="380">

Maintains original functionality - downloading OWM Weather via API, configuration AP, E-INK display handling and now:
- switched to freertos to facilitate concurrent sensor readings, calculations, running webserver and future tasks in between.
- introduced i2c bme280/sht40 sensors for localized temperature and humidity readouts
- ESP-NOW wireless data exchange with auxiliary esp32 using bme280/sht40 and taking temperature, humidity and pressure measurements and sending them to the master in between deep sleep periods
- storing/buffering data on sd card
- webserver for an easy access to recent and saved historical data using graphs
  
<img src="https://github.com/user-attachments/assets/ed6c1b58-6862-463b-b6b7-1f70694c3fef" width="533" height="450">

- employed button for display selection/switching to better handle data (more to be added)

<img src="https://github.com/user-attachments/assets/95a5b678-3eb7-4179-ae55-b6922c1c2e5f" width="533" height="380">
<img src="https://github.com/user-attachments/assets/cc47ac2d-3de3-46f2-a563-65018a9e7be8" width="533" height="380">

Planned:
- command repeating via ESP-NOW issued via web
- much needed optimization



