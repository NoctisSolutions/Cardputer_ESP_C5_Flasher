#ifndef ESP_LOADER_CONFIG_H
#define ESP_LOADER_CONFIG_H

#define SERIAL_FLASHER_INTERFACE_UART 1  // Enable UART
#define MD5_ENABLED 0                    // Disable MD5 for smaller footprint
#define SERIAL_FLASHER_RESET_HOLD_TIME_MS 200  // Hold RESET low (match C5)
#define SERIAL_FLASHER_BOOT_HOLD_TIME_MS 900   // Wait after release RESET for C5 ROM to be ready
#define SERIAL_FLASHER_RESET_INVERT 0    // 0 = active low, adjust if needed
#define SERIAL_FLASHER_BOOT_INVERT 0
/* 1 = release BOOT pin after reset (default). 0 = keep BOOT asserted (required for ESP32-C5 download; sketch must release BOOT after flashing). */
#define SERIAL_FLASHER_BOOT_RELEASE 0

#define SERIAL_FLASHER_WRITE_BLOCK_RETRIES 3

#endif