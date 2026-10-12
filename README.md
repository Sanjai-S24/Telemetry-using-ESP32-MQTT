Software used:



1. ESP-IDF using Visual Code Studio



Hardware used:



1. FTDI (optional) if the board has no in built USB to UART bridge



Building the firmware:



1. Select the set-target as esp32c6
2. Run idf.py menuconfig in ESP-IDF terminal and in Component config, toggle the Bluetooth option to enable it.
3. Select the serial flasher option and change the size to 4 MB.
4. Save the changes.
5. Press the build button.



Flashing the firmware:



1. Select the respective COMM port and debug method as UART.
2. Press the flash button to download firmware to the microcontroller.



Verification (Logging):



1. Open the serial monitor to observe the loggings done by the microcontroller.

