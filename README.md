# Cardputer_ESP_C5_Flasher

  CardputerESPFlasherLib - User Guide
  M5Stack Cardputer → ESP32-C5-DevKitC-N8R8 flasher 


--------------------------------------------------------------------------------
 Prereqs
--------------------------------------------------------------------------------

  1. Hardware

  • M5Stack Cardputer (or Cardputer ADV)
  • ESP32-C5-DevKitC-N8R8 (or compatible C5) to be flashed
  • Jumper wires (Cardputer GND, 3.3V, G3, G4, G5, TX, RX)
  • MicroSD card (for .bin files and optional debug log)
  • USB cable for Cardputer (power and upload)

  2. Software

  • Arduino IDE 2.x or 1.8.x
  • M5Stack ESP32 board support:
    - File → Preferences → Additional Board Manager URLs:
     (https://docs.m5stack.com/en/arduino/arduino_library)
    - Tools → Board → Boards Manager → search "M5Stack" → install
      "M5Stack Boards" (or "M5Stack ESP32")
  • M5Cardputer / M5Unified library:
    - Sketch → Include Library → Manage Libraries → search "M5Cardputer"
      or "M5Unified" → install


--------------------------------------------------------------------------------
ESP SERIAL FLASHER LIBRARY
--------------------------------------------------------------------------------

  The sketch uses the EspSerialFlasher library (Arduino port of esp-serial-flasher).
  The library must be in your Arduino "libraries" folder.

  1. Location

  Put the library here:

    Windows:   Documents\Arduino\libraries\EspSerialFlasher\
    macOS:     ~/Documents/Arduino/libraries/EspSerialFlasher/
    Linux:     ~/Arduino/libraries/EspSerialFlasher/

  2. Required contents (minimum)

  • EspSerialFlasher\esp_loader.h          (redirect to src)
  • EspSerialFlasher\EspSerialFlasher.h   (redirect to src)
  • EspSerialFlasher\library.properties
  • EspSerialFlasher\src\
    - EspSerialFlasher.h, EspSerialFlasher.cpp
    - arduino_port.cpp, esp_loader_config.h
    - esp_loader.h, esp_loader_io.h, esp_loader.c
    - protocol_uart.c, protocol_serial.c, slip.c, slip.h
    - protocol.h, protocol_prv.h
    - esp_stubs.c, esp_stubs.h, esp_targets.c, esp_targets.h
    - (other .c/.h from esp-serial-flasher; protocol_spi.c and
      protocol_sdio.c and esp_sdio_stubs.c should be renamed to
      .c.disabled so only UART is built)

  3. Verify in Arduino IDE

  • Sketch → Include Library → you should see "EspSerialFlasher".
  • If not, check Sketchbook location (File → Preferences) and that
    the folder name is exactly "EspSerialFlasher" under "libraries".


--------------------------------------------------------------------------------
WIRING (Cardputer ↔ ESP32-C5-DevKitC-N8R8)
--------------------------------------------------------------------------------

  Connect cross-over UART and control pins as below. Use a common GND.

  Cardputer          →    ESP32-C5-DevKitC
  ---------               -----------------
  TX  (pin G13)        →    RX  (U0RXD)
  RX  (pin G15)        ←    TX  (U0TXD)
  G3  (pin 3)          →    GPIO28 (strapping: low = download mode)
  G4  (pin 4)          →    RESET (RST_N / CHIP_PU)
  G5  (pin 5)          →    GPIO27 (e.g. EXT pin 1; must be HIGH for download)
  GND                  →    GND
  5V Out               →    5V or LDO (per devkit schematic)

  - Important

  • TX must go to C5 RX, RX to C5 TX (crossed). Same-wire (TX–TX, RX–RX)
    will cause "Connect failed" or timeout.
  • GPIO27 on the C5 must be driven high for download; wiring G5 to
    EXT pin 1 (or the C5’s GPIO27) does that. If GPIO27 is floating or low,
    the ROM may not enter download mode.


--------------------------------------------------------------------------------
OPEN THE SKETCH AND SELECT BOARD
--------------------------------------------------------------------------------

  • File → Open → open the folder:
      ...\Arduino\CardputerESPFlasherLib\
    (Open the folder "CardputerESPFlasherLib", not the parent Arduino folder.)

  • In the IDE you should see:
      CardputerESPFlasherLib.ino

  • Set board and port:
      Tools → Board → M5Stack Cardputer (or your exact Cardputer board name)
      Tools → Port → (the COM port for the Cardputer)


--------------------------------------------------------------------------------
BUILD AND UPLOAD
--------------------------------------------------------------------------------

  • Sketch → Verify/Compile
  • Sketch → Upload

  If compile fails with "EspSerialFlasher.h: No such file or directory":
    - Confirm the EspSerialFlasher library is in Arduino/libraries/EspSerialFlasher.
    - Restart the Arduino IDE and try again.

  After upload, the Cardputer runs the flasher. Insert an SD card with .bin
  files (see next section) and use the on-screen menu.


--------------------------------------------------------------------------------
PREPARE THE SD CARD
--------------------------------------------------------------------------------

  • Format the SD card (FAT32).
  • Copy your .bin firmware file(s) to the root of the SD card (not in a
    subfolder if you want them listed in the flasher menu).
  • Insert the SD card into the Cardputer’s slot.

  - Flash offset
 
  The flasher writes the selected .bin at address 0x10000. Build your
  firmware so the app partition starts at 0x10000 if required by your
  partition table.


--------------------------------------------------------------------------------
USING THE FLASHER (ON CARDPUTER)
--------------------------------------------------------------------------------

  After power-on or reset you see the main menu:

    Select an option:
    1. Select Firmware
    2. Flash Firmware
    3. Reset ESP32
    Selected: (filename or None)

- Keys

  • 1 – Select Firmware: list .bin files on SD; choose one with Up/Down
          (; = up, . = down), confirm with Enter.
  • 2 – Flash Firmware: connect to the C5, flash the selected file,
          then reset the C5 so the new code runs. Press ESC during flash
          to abort.
  • 3 – Reset ESP32: pulse the C5’s RESET pin (run firmware without
          reflashing).
  • ESC – Back (e.g. from file list to main menu).

  Flow for a typical flash
  ------------------------
  1. Power the C5 (USB or 3.3V) and connect the wiring as in section 3.
  2. On Cardputer: press 1, pick a .bin with ;/. , press Enter.
  3. Press 2. The screen shows "Connecting...", then "Flashing: n%", then
     "Flash complete!" and the C5 resets and runs the new firmware.


--------------------------------------------------------------------------------
OPTIONAL: DEBUG LOG ON SD
--------------------------------------------------------------------------------

  In the sketch, with:

    #define DEBUG_LOG 1

  the flasher writes a log file to the SD card:

    /flasher_log.txt

  After a flash (success or failure), open this file on a PC to see
  timestamps and messages (connect, flash steps, errors). Set
  DEBUG_LOG to 0 to disable.

--------------------------------------------------------------------------------
QUICK REFERENCE
--------------------------------------------------------------------------------

  Pinout (Cardputer → C5)
   - TX 13 → C5 RX
   - RX 15 ← C5 TX
   - G3    → C5 GPIO28 (BOOT)
   - G4    → C5 RESET
   - G5    → C5 GPIO27
   - GND   → GND

  Flash offset: 0x10000

  Menu: 1 = Select .bin, 2 = Flash, 3 = Reset C5, ESC = Back

  Schematic Color Code:
  - Green   → GND
  - Red     → 5VO
  - Black   → G3  → GPIO 28
  - Blue    → G4  → Reset
  - Yellow  → G13 → RX (ESP)
  - Orange  → G15 → TX (ESP)
  - Pink    → G5  → GPIO 27
