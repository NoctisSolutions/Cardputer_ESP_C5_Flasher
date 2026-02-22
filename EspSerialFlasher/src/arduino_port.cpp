#include <Arduino.h>
#include "esp_loader_io.h"
#include "esp_loader_config.h"

// Use Stream* so both HardwareSerial and HWCDC (USB Serial) work
Stream* loaderSerial = &Serial;
extern int loaderResetPin = -1;
extern int loaderBootPin = -1;

static unsigned long timerDeadline = 0;

extern "C" {

    // Core read: Read from UART with timeout
    esp_loader_error_t loader_port_read(uint8_t* data, uint16_t size, uint32_t timeout) {
        if (!loaderSerial) return ESP_LOADER_ERROR_FAIL;
        unsigned long start = millis();
        size_t read = 0;
        while (read < size && (millis() - start) < timeout) {
            if (loaderSerial->available()) {
                data[read++] = loaderSerial->read();
            }
        }
        return (read == size) ? ESP_LOADER_SUCCESS : ESP_LOADER_ERROR_TIMEOUT;
    }

    // Core write: Write to UART with timeout (flush to ensure transmission)
    esp_loader_error_t loader_port_write(const uint8_t* data, uint16_t size, uint32_t timeout) {
        if (!loaderSerial) return ESP_LOADER_ERROR_FAIL;
        size_t written = loaderSerial->write(data, size);
        loaderSerial->flush();
        return (written == size) ? ESP_LOADER_SUCCESS : ESP_LOADER_ERROR_FAIL;
    }

    // Enter bootloader: Toggle RESET and BOOT pins
    void loader_port_enter_bootloader(void) {
        if (loaderResetPin == -1 || loaderBootPin == -1) return;  // Skip if not set

        int resetLevel = SERIAL_FLASHER_RESET_INVERT ? HIGH : LOW;
        int bootLevel = SERIAL_FLASHER_BOOT_INVERT ? HIGH : LOW;

        digitalWrite(loaderBootPin, bootLevel);  // Assert BOOT
        digitalWrite(loaderResetPin, resetLevel);  // Assert RESET
        delay(SERIAL_FLASHER_RESET_HOLD_TIME_MS);
        digitalWrite(loaderResetPin, !resetLevel);  // Release RESET
        delay(SERIAL_FLASHER_BOOT_HOLD_TIME_MS);
#if SERIAL_FLASHER_BOOT_RELEASE
        digitalWrite(loaderBootPin, !bootLevel);  // Release BOOT
#endif
        // Drain serial so ROM boot log doesn't corrupt sync (ESP32-C5 etc.)
        if (loaderSerial) {
            delay(100);
            for (unsigned long t = millis(); millis() - t < 500; ) {
                while (loaderSerial->available()) loaderSerial->read();
                delay(2);
            }
            while (loaderSerial->available()) loaderSerial->read();
        }
    }

    // Delay
    void loader_port_delay_ms(uint32_t ms) {
        delay(ms);
    }

    // Start timer
    void loader_port_start_timer(uint32_t ms) {
        timerDeadline = millis() + ms;
    }

    // Remaining time
    uint32_t loader_port_remaining_time(void) {
        if (millis() >= timerDeadline) return 0;
        return timerDeadline - millis();
    }

    // Optional: Reset target (similar to enter_bootloader but without BOOT)
    void loader_port_reset_target(void) {
        if (loaderResetPin == -1) return;
        int resetLevel = SERIAL_FLASHER_RESET_INVERT ? HIGH : LOW;
        digitalWrite(loaderResetPin, resetLevel);
        delay(SERIAL_FLASHER_RESET_HOLD_TIME_MS);
        digitalWrite(loaderResetPin, !resetLevel);
    }

    // Optional: Debug print (to Serial for logging)
    void loader_port_debug_print(const char* str) {
        Serial.print(str);
    }

    // Optional: Change baud rate (caller must use HardwareSerial)
    esp_loader_error_t loader_port_change_transmission_rate(uint32_t baudrate) {
        if (!loaderSerial) return ESP_LOADER_ERROR_FAIL;
        HardwareSerial* u = (HardwareSerial*)loaderSerial;
        u->end();
        u->begin(baudrate);
        return ESP_LOADER_SUCCESS;
    }

}  // extern "C"