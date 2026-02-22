/*
 * CardputerESPFlasherLib - M5Stack Cardputer ESP32-C5 Flasher (esp-serial-flasher library)
 *
 * Flashes a connected ESP32-C5-DevKitC-N8R8 from .bin files on SD card.
 *
 *
 * Requires: M5Cardputer, EspSerialFlasher (esp-serial-flasher Arduino port)
 *
 * 
 *   Cardputer TX (13) -> ESP32-C5 RX (U0RXD)
 *   Cardputer RX (15) <- ESP32-C5 TX (U0TXD)
 *   Cardputer G3 (3)  -> ESP32-C5 GPIO28 (strapping: low = download)
 *   Cardputer G4 (4)  -> ESP32-C5 RESET (RST_N)
 *   Cardputer G5 (5)  -> ESP32-C5 GPIO27 (must be HIGH for download; wire to EXT pin 1)
 */

#include <M5Cardputer.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <EspSerialFlasher.h>
#include <esp_loader.h>

#define DEBUG_KEYBOARD  0
#define DEBUG_LOG       1
#define LOG_FILENAME    "/flasher_log.txt"

// SD card pins (M5Stack Cardputer)
#define SD_SPI_SCK_PIN  40
#define SD_SPI_MISO_PIN 39
#define SD_SPI_MOSI_PIN 14
#define SD_SPI_CS_PIN   12

// Pin definitions for ESP32-C5 
#define ESP_TX_PIN      13   // Cardputer TX -> ESP32-C5 RX (U0RXD)
#define ESP_RX_PIN      15   // Cardputer RX <- ESP32-C5 TX (U0TXD)
#define ESP_BOOT_PIN    3    // Cardputer G3 -> ESP32-C5 GPIO28 (low = download)
#define ESP_RESET_PIN   4    // Cardputer G4 -> ESP32-C5 RESET (RST_N)
#define ESP_GPIO27_PIN  5    // Cardputer G5 -> ESP32-C5 GPIO27 (must be HIGH for download)

#define ESP_ROM_BAUD    115200
#define FLASH_OFFSET    0x10000
#define FLASH_BLOCK_SIZE 1024  // esp-serial-flasher ROM block size

HardwareSerial targetSerial(1);
EspSerialFlasher flasher(targetSerial, ESP_RESET_PIN, ESP_BOOT_PIN);

// UI theme (RGB565) 
#define UI_BG          0x0000
#define UI_TITLE_BG    0x07FF
#define UI_FOOTER_BG   0x3186
#define UI_ACCENT      0x07FF
#define UI_SELECT_BG   0x07FF
#define UI_BORDER      0x5AEB
#define UI_SUCCESS     0x07E0
#define UI_WARNING     0xFFE0
#define UI_ERROR       0xF800
#define UI_TEXT        0xFFFF
#define UI_DIM         0x7BEF

enum FlasherState {
  STATE_MENU,
  STATE_FILE_SELECT,
  STATE_FLASHING,
  STATE_DONE,
  STATE_ERROR
};

FlasherState currentState = STATE_MENU;
String statusMessage = "ESP32 Flasher Ready";
String selectedFile = "";
int menuSelection = 0;
String binFiles[50];
int binFileCount = 0;
int fileScrollOffset = 0;
#define MAX_MESSAGE_LINES 16
String messageLines[MAX_MESSAGE_LINES];
int messageLineCount = 0;
int messageScrollOffset = 0;
const int MAX_CHARS_PER_LINE = 28;
const int MESSAGE_LINE_HEIGHT = 10;

volatile bool g_abortSync = false;

#if DEBUG_LOG
File logFile;
void logInit() {
  if (SD.exists(LOG_FILENAME)) SD.remove(LOG_FILENAME);
  logFile = SD.open(LOG_FILENAME, FILE_WRITE);
  if (!logFile) return;
  logFile.print("===== Flasher debug log ");
  logFile.print(millis());
  logFile.println(" ms =====\n");
  logFile.flush();
}
void logMsg(const char* msg) {
  if (!logFile) return;
  logFile.print(millis());
  logFile.print("\t");
  logFile.println(msg);
  logFile.flush();
}
void logMsg(const String& msg) {
  if (!logFile) return;
  logFile.print(millis());
  logFile.print("\t");
  logFile.println(msg);
  logFile.flush();
}
void logClose() {
  if (logFile) logFile.close();
}
#else
inline void logInit() {}
inline void logMsg(const char*) {}
inline void logMsg(const String&) {}
inline void logClose() {}
#endif

void checkSyncAbort() {
  M5Cardputer.update();
  if (M5Cardputer.Keyboard.isPressed()) {
    Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();
    if (st.del) g_abortSync = true;
  }
}

static const char* loaderErrStr(esp_loader_error_t err) {
  switch (err) {
    case ESP_LOADER_SUCCESS: return "Success";
    case ESP_LOADER_ERROR_FAIL: return "Fail";
    case ESP_LOADER_ERROR_TIMEOUT: return "Timeout";
    case ESP_LOADER_ERROR_IMAGE_SIZE: return "Image size";
    case ESP_LOADER_ERROR_INVALID_PARAM: return "Invalid param";
    case ESP_LOADER_ERROR_INVALID_TARGET: return "Invalid target";
    case ESP_LOADER_ERROR_UNSUPPORTED_CHIP: return "Unsupported chip";
    case ESP_LOADER_ERROR_UNSUPPORTED_FUNC: return "Unsupported func";
    case ESP_LOADER_ERROR_INVALID_RESPONSE: return "Invalid response";
    default: return "Unknown";
  }
}

bool flashFirmware(const String& filename) {
  g_abortSync = false;
  String path = filename;
  if (path.length() > 0 && path[0] != '/') path = "/" + path;
  File file = SD.open(path, FILE_READ);
  if (!file) file = SD.open(filename, FILE_READ);
  if (!file) {
    statusMessage = "Error: Cannot open file";
    logMsg("Error: Cannot open file " + path);
    return false;
  }

  size_t fileSize = file.size();
  statusMessage = "Connecting...";
  updateDisplay();
  logMsg("Flash file: " + path + " size=" + String(fileSize));

#if ESP_GPIO27_PIN >= 0
  digitalWrite(ESP_GPIO27_PIN, HIGH);
#endif
  delay(50);
  targetSerial.flush();
  while (targetSerial.available()) targetSerial.read();

  esp_loader_error_t err = flasher.connect();
  if (err != ESP_LOADER_SUCCESS) {
    statusMessage = "Error: Connect failed (" + String(loaderErrStr(err)) + ")";
    logMsg("esp_loader_connect failed: " + String(loaderErrStr(err)));
    file.close();
    digitalWrite(ESP_BOOT_PIN, HIGH);
    return false;
  }
  logMsg("Connected to target");

  statusMessage = "Flashing " + String(fileSize) + " bytes...";
  updateDisplay();

  err = esp_loader_flash_start((uint32_t)FLASH_OFFSET, (uint32_t)fileSize, FLASH_BLOCK_SIZE);
  if (err != ESP_LOADER_SUCCESS) {
    statusMessage = "Error: Flash start (" + String(loaderErrStr(err)) + ")";
    logMsg("esp_loader_flash_start failed: " + String(loaderErrStr(err)));
    file.close();
    digitalWrite(ESP_BOOT_PIN, HIGH);
    return false;
  }

  uint8_t buffer[FLASH_BLOCK_SIZE];
  size_t bytesFlashed = 0;

  while (bytesFlashed < fileSize && !g_abortSync) {
    checkSyncAbort();
    size_t toRead = (fileSize - bytesFlashed) >= FLASH_BLOCK_SIZE ? FLASH_BLOCK_SIZE : (fileSize - bytesFlashed);
    size_t n = file.read(buffer, toRead);
    if (n != toRead) {
      statusMessage = "Error: Read failed";
      logMsg("File read failed");
      file.close();
      esp_loader_flash_finish(false);
      digitalWrite(ESP_BOOT_PIN, HIGH);
      return false;
    }
    err = esp_loader_flash_write(buffer, (uint32_t)n);
    if (err != ESP_LOADER_SUCCESS) {
      statusMessage = "Error: Flash write (" + String(loaderErrStr(err)) + ")";
      logMsg("esp_loader_flash_write failed: " + String(loaderErrStr(err)));
      file.close();
      esp_loader_flash_finish(false);
      digitalWrite(ESP_BOOT_PIN, HIGH);
      return false;
    }
    bytesFlashed += n;
    int progress = (int)((bytesFlashed * 100) / fileSize);
    statusMessage = "Flashing: " + String(progress) + "%";
    updateDisplay();
  }

  file.close();

  if (g_abortSync) {
    statusMessage = "Flash aborted";
    logMsg("Flash aborted by user");
    esp_loader_flash_finish(false);
    digitalWrite(ESP_BOOT_PIN, HIGH);
    return false;
  }

  err = esp_loader_flash_finish(true);
  digitalWrite(ESP_BOOT_PIN, HIGH);   // Release BOOT so C5 runs app (not download mode)
  // C5 ROM often reboots before sending FLASH_END response; treat timeout/invalid response as success
  if (err == ESP_LOADER_SUCCESS) {
    logMsg("Flash complete!");
    statusMessage = "Flash complete!";
    delay(50);
    esp_loader_reset_target();       // Reset C5 so new firmware runs
    return true;
  }
  if (err == ESP_LOADER_ERROR_TIMEOUT || err == ESP_LOADER_ERROR_INVALID_RESPONSE) {
    logMsg("Flash complete (end cmd no reply - device may have rebooted)");
    statusMessage = "Flash complete!";
    delay(50);
    esp_loader_reset_target();       // Reset C5 so new firmware runs
    return true;
  }
  statusMessage = "Error: Flash end (" + String(loaderErrStr(err)) + ")";
  logMsg("esp_loader_flash_finish failed: " + String(loaderErrStr(err)));
  return false;
}

void scanBinFiles() {
  binFileCount = 0;
  File root = SD.open("/");
  if (!root) return;
  File file = root.openNextFile();
  while (file && binFileCount < 50) {
    if (!file.isDirectory()) {
      String name = file.name();
      if (name.endsWith(".bin")) binFiles[binFileCount++] = name;
    }
    file = root.openNextFile();
  }
  root.close();
}

void wrapMessageToLines(const String& msg) {
  messageLineCount = 0;
  if (msg.length() == 0) return;
  int start = 0;
  while (messageLineCount < MAX_MESSAGE_LINES && start < (int)msg.length()) {
    int len = MAX_CHARS_PER_LINE;
    if (start + len > (int)msg.length()) len = msg.length() - start;
    else {
      int lastSpace = -1;
      for (int i = 0; i < len && start + i < (int)msg.length(); i++) {
        if (msg[start + i] == ' ' || msg[start + i] == '\n') lastSpace = i;
      }
      if (lastSpace > 0) len = lastSpace + 1;
    }
    messageLines[messageLineCount++] = msg.substring(start, start + len);
    start += len;
  }
}

void updateDisplay() {
  int w = M5Cardputer.Display.width();
  int h = M5Cardputer.Display.height();
  const int titleH = 28;
  const int footerH = 22;
  const int pad = 6;
  const int contentTop = titleH + 4;
  const int contentBottom = h - footerH - 4;

  M5Cardputer.Display.fillScreen(UI_BG);
  M5Cardputer.Display.fillRect(0, 0, w, titleH, UI_TITLE_BG);
  M5Cardputer.Display.drawLine(0, titleH, w, titleH, UI_BORDER);
  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setTextColor(UI_BG);
  M5Cardputer.Display.setCursor(pad + 2, 6);
  M5Cardputer.Display.print("  ESP32-C5 Flasher");

  M5Cardputer.Display.drawRoundRect(2, contentTop - 2, w - 4, contentBottom - contentTop + 4, 6, UI_BORDER);
  M5Cardputer.Display.setTextColor(UI_TEXT);
  M5Cardputer.Display.setTextSize(1);

  switch (currentState) {
    case STATE_MENU: {
      int y = contentTop + 2;
      const int lineH = 10;
      M5Cardputer.Display.setCursor(pad + 4, y);
      M5Cardputer.Display.setTextColor(UI_ACCENT);
      M5Cardputer.Display.println("Select an option:");
      M5Cardputer.Display.setTextColor(UI_TEXT);
      y += lineH;
      M5Cardputer.Display.setCursor(pad + 4, y);
      M5Cardputer.Display.println("1. Select Firmware");
      y += lineH;
      M5Cardputer.Display.setCursor(pad + 4, y);
      M5Cardputer.Display.println("2. Flash Firmware");
      y += lineH;
      M5Cardputer.Display.setCursor(pad + 4, y);
      M5Cardputer.Display.println("3. Reset ESP32");
      y += lineH;
      M5Cardputer.Display.setTextColor(UI_DIM);
      M5Cardputer.Display.setCursor(pad + 4, y);
      M5Cardputer.Display.print("Selected: ");
      M5Cardputer.Display.setTextColor(UI_ACCENT);
      M5Cardputer.Display.println(selectedFile.length() > 0 ? selectedFile : "None");
      y += lineH;
      if (statusMessage.length() > 0 && statusMessage != "ESP32 Flasher Ready") {
        wrapMessageToLines(statusMessage);
        int visibleLines = (contentBottom - y - 2) / MESSAGE_LINE_HEIGHT;
        if (visibleLines < 1) visibleLines = 1;
        int startLine = messageScrollOffset;
        if (startLine > messageLineCount - visibleLines) startLine = (messageLineCount > visibleLines) ? messageLineCount - visibleLines : 0;
        messageScrollOffset = startLine;
        M5Cardputer.Display.setTextColor(UI_WARNING);
        for (int i = 0; i < visibleLines && (startLine + i) < messageLineCount; i++) {
          M5Cardputer.Display.setCursor(pad + 4, y + i * MESSAGE_LINE_HEIGHT);
          M5Cardputer.Display.println(messageLines[startLine + i]);
        }
        M5Cardputer.Display.setTextColor(UI_TEXT);
      }
      break;
    }
    case STATE_FILE_SELECT: {
      int y = contentTop + 2;
      const int lineH = 10;
      M5Cardputer.Display.setCursor(pad + 4, y);
      M5Cardputer.Display.setTextColor(UI_ACCENT);
      M5Cardputer.Display.println("Select .bin file:");
      M5Cardputer.Display.setTextColor(UI_TEXT);
      y += lineH;
      for (int i = 0; i < 8 && (i + fileScrollOffset) < binFileCount; i++) {
        int idx = i + fileScrollOffset;
        int itemY = y + i * lineH;
        if (idx == menuSelection) {
          M5Cardputer.Display.fillRoundRect(4, itemY - 1, w - 10, lineH + 2, 3, UI_SELECT_BG);
          M5Cardputer.Display.setTextColor(UI_BG);
          M5Cardputer.Display.setCursor(pad + 6, itemY);
          M5Cardputer.Display.println(" " + binFiles[idx]);
          M5Cardputer.Display.setTextColor(UI_TEXT);
        } else {
          M5Cardputer.Display.setCursor(pad + 6, itemY);
          M5Cardputer.Display.println(" " + binFiles[idx]);
        }
      }
      break;
    }
    case STATE_FLASHING: {
      wrapMessageToLines(statusMessage);
      int boxTop = contentTop + 4;
      int boxH = contentBottom - contentTop - 8;
      M5Cardputer.Display.fillRoundRect(pad + 4, boxTop, w - 2 * (pad + 4), boxH, 8, UI_ACCENT);
      M5Cardputer.Display.setTextColor(UI_BG);
      int visibleLines = boxH / MESSAGE_LINE_HEIGHT;
      int startLine = messageScrollOffset;
      if (startLine > messageLineCount - visibleLines) startLine = (messageLineCount > visibleLines) ? messageLineCount - visibleLines : 0;
      messageScrollOffset = startLine;
      for (int i = 0; i < visibleLines && (startLine + i) < messageLineCount; i++) {
        M5Cardputer.Display.setCursor(pad + 12, boxTop + 6 + i * MESSAGE_LINE_HEIGHT);
        M5Cardputer.Display.println(messageLines[startLine + i]);
      }
      M5Cardputer.Display.setTextColor(UI_TEXT);
      break;
    }
    case STATE_DONE: {
      wrapMessageToLines(statusMessage);
      int boxTop = contentTop + 4;
      int boxH = contentBottom - contentTop - 8;
      M5Cardputer.Display.fillRoundRect(pad + 4, boxTop, w - 2 * (pad + 4), boxH, 8, UI_SUCCESS);
      M5Cardputer.Display.setTextColor(UI_BG);
      int visibleLines = boxH / MESSAGE_LINE_HEIGHT;
      int startLine = messageScrollOffset;
      if (startLine > messageLineCount - visibleLines) startLine = (messageLineCount > visibleLines) ? messageLineCount - visibleLines : 0;
      messageScrollOffset = startLine;
      for (int i = 0; i < visibleLines && (startLine + i) < messageLineCount; i++) {
        M5Cardputer.Display.setCursor(pad + 12, boxTop + 6 + i * MESSAGE_LINE_HEIGHT);
        M5Cardputer.Display.println(messageLines[startLine + i]);
      }
      M5Cardputer.Display.setTextColor(UI_TEXT);
      break;
    }
    case STATE_ERROR: {
      wrapMessageToLines(statusMessage);
      int boxTop = contentTop + 4;
      int boxH = contentBottom - contentTop - 8;
      M5Cardputer.Display.fillRoundRect(pad + 4, boxTop, w - 2 * (pad + 4), boxH, 8, UI_ERROR);
      M5Cardputer.Display.setTextColor(UI_TEXT);
      int visibleLines = boxH / MESSAGE_LINE_HEIGHT;
      int startLine = messageScrollOffset;
      if (startLine > messageLineCount - visibleLines) startLine = (messageLineCount > visibleLines) ? messageLineCount - visibleLines : 0;
      messageScrollOffset = startLine;
      for (int i = 0; i < visibleLines && (startLine + i) < messageLineCount; i++) {
        M5Cardputer.Display.setCursor(pad + 12, boxTop + 6 + i * MESSAGE_LINE_HEIGHT);
        M5Cardputer.Display.println(messageLines[startLine + i]);
      }
      M5Cardputer.Display.setTextColor(UI_TEXT);
      break;
    }
  }

  M5Cardputer.Display.fillRect(0, h - footerH, w, footerH, UI_FOOTER_BG);
  M5Cardputer.Display.drawLine(0, h - footerH, w, h - footerH, UI_BORDER);
  M5Cardputer.Display.setTextSize(1);
  M5Cardputer.Display.setTextColor(UI_WARNING);
  M5Cardputer.Display.setCursor(pad + 4, h - footerH + 6);
  M5Cardputer.Display.print("ESC=Back   Enter=Select");
}

void doFlashSequence() {
  messageScrollOffset = 0;
  currentState = STATE_FLASHING;
  updateDisplay();
  logInit();
  if (flashFirmware(selectedFile)) {
    messageScrollOffset = 0;
    currentState = STATE_DONE;
  } else {
    messageScrollOffset = 0;
    currentState = STATE_ERROR;
  }
  logClose();
  updateDisplay();
  delay(3000);
  currentState = STATE_MENU;
  updateDisplay();
}

void handleInput() {
  static uint32_t lastKeyTime = 0;
  static const uint32_t KEY_COOLDOWN_MS = 350;
  static char lastMenuKey = '\0';
  static uint32_t lastMenuKeyTime = 0;

  M5Cardputer.update();

  if (currentState == STATE_MENU) {
    Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();
    char menuKey = '\0';
    for (size_t i = 0; i < st.word.size(); i++) {
      char c = st.word[i];
      if (c == '1' || c == '2' || c == '3') { menuKey = c; break; }
    }
    if (menuKey == '\0') lastMenuKey = '\0';
    else if (menuKey != lastMenuKey || (millis() - lastMenuKeyTime > 300)) {
      lastMenuKey = menuKey;
      lastMenuKeyTime = millis();
      if (menuKey == '1') {
        scanBinFiles();
        if (binFileCount > 0) {
          menuSelection = 0;
          fileScrollOffset = 0;
          currentState = STATE_FILE_SELECT;
        } else {
          statusMessage = "No .bin files found!";
          messageScrollOffset = 0;
        }
        updateDisplay();
      } else if (menuKey == '2') {
        if (selectedFile.length() > 0) doFlashSequence();
        else {
          statusMessage = "Select a file first (press 1)";
          messageScrollOffset = 0;
          updateDisplay();
        }
      } else if (menuKey == '3') {
        digitalWrite(ESP_RESET_PIN, LOW);
        delay(100);
        digitalWrite(ESP_RESET_PIN, HIGH);
        statusMessage = "ESP32 reset";
        updateDisplay();
        delay(1000);
        statusMessage = "ESP32 Flasher Ready";
        updateDisplay();
      }
    }
  }

  bool keyChanged = M5Cardputer.Keyboard.isChange();
  bool keyPressed = M5Cardputer.Keyboard.isPressed();
  bool processKey = (keyChanged && keyPressed) || (keyPressed && (millis() - lastKeyTime >= KEY_COOLDOWN_MS));

  if (processKey && keyPressed) {
    lastKeyTime = millis();
    Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

    if (status.del) {
      if (currentState == STATE_FILE_SELECT) {
        currentState = STATE_MENU;
        updateDisplay();
      }
    }

    String keyPressedStr = "";
    bool enterAsChar = false;
    for (char c : status.word) {
      if (c == '\r' || c == '\n') enterAsChar = true;
      else keyPressedStr += c;
    }
#if DEBUG_KEYBOARD
    Serial.printf("key word=%d del=%d enter=%d keyPressed='%s'\n",
      (int)status.word.size(), status.del ? 1 : 0, status.enter ? 1 : 0, keyPressedStr.c_str());
#endif

    if (status.enter || enterAsChar) {
      if (currentState == STATE_FILE_SELECT && binFileCount > 0) {
        selectedFile = binFiles[menuSelection];
        currentState = STATE_MENU;
        updateDisplay();
      }
    }

    if (currentState == STATE_FILE_SELECT) {
      if (keyPressedStr == ";") {
        if (menuSelection > 0) {
          menuSelection--;
          if (menuSelection < fileScrollOffset) fileScrollOffset--;
          updateDisplay();
        }
      }
      if (keyPressedStr == ".") {
        if (menuSelection < binFileCount - 1) {
          menuSelection++;
          if (menuSelection >= fileScrollOffset + 8) fileScrollOffset++;
          updateDisplay();
        }
      }
    } else if (currentState == STATE_FLASHING || currentState == STATE_DONE || currentState == STATE_ERROR ||
               (currentState == STATE_MENU && statusMessage.length() > 0 && statusMessage != "ESP32 Flasher Ready")) {
      wrapMessageToLines(statusMessage);
      int visibleLines = 4;
      if (currentState == STATE_MENU) visibleLines = 2;
      int maxOffset = messageLineCount - visibleLines;
      if (maxOffset < 0) maxOffset = 0;
      if (keyPressedStr == ";" || (keyPressedStr.length() >= 1 && keyPressedStr[0] == ';')) {
        if (messageScrollOffset > 0) { messageScrollOffset--; updateDisplay(); }
      }
      if (keyPressedStr == "." || (keyPressedStr.length() >= 1 && keyPressedStr[0] == '.')) {
        if (messageScrollOffset < maxOffset) { messageScrollOffset++; updateDisplay(); }
      }
    }

    if (currentState == STATE_MENU) {
      char menuKey = '\0';
      if (keyPressedStr == "1" || keyPressedStr == "2" || keyPressedStr == "3") menuKey = keyPressedStr[0];
      else if (keyPressedStr.length() >= 1 && (keyPressedStr[0] == '1' || keyPressedStr[0] == '2' || keyPressedStr[0] == '3')) menuKey = keyPressedStr[0];
      else {
        for (char c : status.word) {
          if (c == '1' || c == '2' || c == '3') { menuKey = c; break; }
        }
      }
      if (menuKey == '1') {
        scanBinFiles();
        if (binFileCount > 0) {
          menuSelection = 0;
          fileScrollOffset = 0;
          currentState = STATE_FILE_SELECT;
        } else {
          statusMessage = "No .bin files found!";
          messageScrollOffset = 0;
        }
        updateDisplay();
      } else if (menuKey == '2') {
        if (selectedFile.length() > 0) doFlashSequence();
        else {
          statusMessage = "Select a file first (press 1)";
          messageScrollOffset = 0;
          updateDisplay();
        }
      } else if (menuKey == '3') {
        digitalWrite(ESP_RESET_PIN, LOW);
        delay(100);
        digitalWrite(ESP_RESET_PIN, HIGH);
        statusMessage = "ESP32 reset";
        updateDisplay();
        delay(1000);
        statusMessage = "ESP32 Flasher Ready";
        updateDisplay();
      }
    }
  }
}

void setup() {
#if DEBUG_KEYBOARD
  Serial.begin(115200);
#endif
  auto cfg = M5.config();
  M5Cardputer.begin(cfg);

  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.fillScreen(BLACK);
  M5Cardputer.Display.setTextColor(WHITE);
  M5Cardputer.Display.setTextSize(1);

  pinMode(ESP_BOOT_PIN, OUTPUT);
  pinMode(ESP_RESET_PIN, OUTPUT);
  digitalWrite(ESP_BOOT_PIN, HIGH);
  digitalWrite(ESP_RESET_PIN, HIGH);
#if ESP_GPIO27_PIN >= 0
  pinMode(ESP_GPIO27_PIN, OUTPUT);
  digitalWrite(ESP_GPIO27_PIN, HIGH);
#endif

  flasher.begin(ESP_ROM_BAUD);
  targetSerial.end();
  targetSerial.begin(ESP_ROM_BAUD, SERIAL_8N1, ESP_RX_PIN, ESP_TX_PIN);

  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
    statusMessage = "SD Card init failed!";
  }

  updateDisplay();
}

void loop() {
  handleInput();
  delay(10);
}
