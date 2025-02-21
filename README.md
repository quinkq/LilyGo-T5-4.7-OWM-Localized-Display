My take on #LilyGo-EPD-4-7-OWM-Weather-Display - adapting for own purposes:

    - switched to freertos to facilitate concurrent sensor readings, calculations, running webserver and future tasks in between.
    - introduced i2c bme280/sht40 sensors for localized readouts
    - ESP-NOW wireless data exchange with auxiliary esp32 taking measurements and sending them to the master
    - added webserver for an easy access to recent and saved historical data using graphs
    - employed button for display selection/switching to better handle data (more to be added)
    

Planned:

    - command repeating via ESP-NOW issues via web
    - much needed optimization
	


