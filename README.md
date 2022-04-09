# SapSense-WisBlock
Code needed for creating a SapSensor with Google Sheets and TagoIO Integration on the Helium network

water_sensor_lora_v4 is the code to run on the sensor. 
Information on how to assemble the sensor and install the correct Arduino libraries
is included in the User Manual document.

Decoder functions for the Google Sheets integration and TagoIO integration are included.
The Google Sheets integration has a specific field name that may need to be modified to match
the intended destination. The TagoIO integration should work as-is in the Decoder field of a 
Helium TagoIO integration.
