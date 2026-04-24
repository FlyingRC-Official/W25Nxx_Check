/*
  W25Nxx_Check - Read-only bad-block scanner for Winbond W25N01 SPI NAND

  Target: ESP32 + Arduino framework
  Flash:  Winbond W25N01GV or compatible 1G-bit SPI NAND

  What it does:
  - Reads JEDEC ID
  - Forces buffer-read mode
  - Scans every block's factory bad-block markers
  - Does NOT erase or program the flash

  Notes:
  - W25N01 is SPI NAND, not W25Q SPI NOR.
  - A block is reported as bad when Page 0 Byte 0 or Page 0 spare-area Byte 0
    is not 0xFF.
  - If the flash has already been used for normal data storage, Page 0 Byte 0 in
    the main array may contain user data. In that case, set CHECK_MAIN_ARRAY_MARKER
    to false and only check the spare marker.
*/

#include <Arduino.h>
#include <SPI.h>

// ===================== User configuration =====================
// Change these pins to match your ESP32 board wiring.
#ifndef PIN_FLASH_CS
#define PIN_FLASH_CS    10
#endif

#ifndef PIN_FLASH_SCK
#define PIN_FLASH_SCK   12
#endif

#ifndef PIN_FLASH_MISO
#define PIN_FLASH_MISO  13   // W25N01 DO / IO1
#endif

#ifndef PIN_FLASH_MOSI
#define PIN_FLASH_MOSI  11   // W25N01 DI / IO0
#endif

// Safe default: full read-only factory marker scan.
// For brand-new chips, checking both markers is recommended.
// For chips that already contain user data, main-array byte 0 may be overwritten;
// in that case, set CHECK_MAIN_ARRAY_MARKER to false.
static const bool CHECK_MAIN_ARRAY_MARKER = true;
static const bool CHECK_SPARE_MARKER      = true;

// W25N01GV geometry.
static const uint16_t TOTAL_BLOCKS     = 1024;
static const uint8_t  PAGES_PER_BLOCK  = 64;
static const uint16_t PAGE_SIZE        = 2048;
static const uint16_t SPARE_SIZE       = 64;
static const uint16_t SPARE_OFFSET     = PAGE_SIZE;
static const uint32_t SPI_CLOCK_HZ     = 8000000;

// ===================== W25Nxx command set =====================
static const uint8_t CMD_RESET         = 0xFF;
static const uint8_t CMD_READ_JEDEC    = 0x9F;
static const uint8_t CMD_GET_FEATURES  = 0x0F;
static const uint8_t CMD_SET_FEATURES  = 0x1F;
static const uint8_t CMD_WRITE_ENABLE  = 0x06;
static const uint8_t CMD_PAGE_READ     = 0x13;
static const uint8_t CMD_READ_DATA     = 0x03;

// Feature/status register addresses.
static const uint8_t REG_PROTECTION    = 0xA0;
static const uint8_t REG_CONFIG        = 0xB0;
static const uint8_t REG_STATUS        = 0xC0;

// Status/config bits.
static const uint8_t STATUS_BUSY       = 0x01;
static const uint8_t CONFIG_BUF        = 0x08;

SPISettings flashSettings(SPI_CLOCK_HZ, MSBFIRST, SPI_MODE0);

static inline void csLow() {
  digitalWrite(PIN_FLASH_CS, LOW);
}

static inline void csHigh() {
  digitalWrite(PIN_FLASH_CS, HIGH);
}

static inline uint8_t xfer(uint8_t data) {
  return SPI.transfer(data);
}

void singleByteCommand(uint8_t cmd) {
  SPI.beginTransaction(flashSettings);
  csLow();
  xfer(cmd);
  csHigh();
  SPI.endTransaction();
}

void writeEnable() {
  singleByteCommand(CMD_WRITE_ENABLE);
}

uint8_t getFeature(uint8_t regAddr) {
  SPI.beginTransaction(flashSettings);
  csLow();
  xfer(CMD_GET_FEATURES);
  xfer(regAddr);
  uint8_t value = xfer(0x00);
  csHigh();
  SPI.endTransaction();
  return value;
}

void setFeature(uint8_t regAddr, uint8_t value) {
  writeEnable();

  SPI.beginTransaction(flashSettings);
  csLow();
  xfer(CMD_SET_FEATURES);
  xfer(regAddr);
  xfer(value);
  csHigh();
  SPI.endTransaction();
}

bool waitReady(uint32_t timeoutMs = 100) {
  const uint32_t start = millis();

  while ((millis() - start) < timeoutMs) {
    const uint8_t status = getFeature(REG_STATUS);
    if ((status & STATUS_BUSY) == 0) {
      return true;
    }
    delayMicroseconds(50);
  }

  return false;
}

void resetFlash() {
  singleByteCommand(CMD_RESET);
  delay(2);
  waitReady(100);
}

void readJedecId(uint8_t &manufacturer, uint8_t &id1, uint8_t &id2) {
  SPI.beginTransaction(flashSettings);
  csLow();
  xfer(CMD_READ_JEDEC);
  xfer(0x00);  // dummy byte for W25Nxx JEDEC-ID read
  manufacturer = xfer(0x00);
  id1 = xfer(0x00);
  id2 = xfer(0x00);
  csHigh();
  SPI.endTransaction();
}

bool loadPageToCache(uint16_t pageAddress) {
  SPI.beginTransaction(flashSettings);
  csLow();
  xfer(CMD_PAGE_READ);
  xfer(0x00);  // dummy byte
  xfer((pageAddress >> 8) & 0xFF);
  xfer(pageAddress & 0xFF);
  csHigh();
  SPI.endTransaction();

  return waitReady(100);
}

uint8_t readCacheByte(uint16_t columnAddress) {
  SPI.beginTransaction(flashSettings);
  csLow();
  xfer(CMD_READ_DATA);
  xfer((columnAddress >> 8) & 0xFF);
  xfer(columnAddress & 0xFF);
  xfer(0x00);  // dummy byte
  const uint8_t value = xfer(0x00);
  csHigh();
  SPI.endTransaction();
  return value;
}

void forceBufferReadMode() {
  const uint8_t before = getFeature(REG_CONFIG);

  if ((before & CONFIG_BUF) == 0) {
    setFeature(REG_CONFIG, before | CONFIG_BUF);
    delay(1);
  }

  const uint8_t after = getFeature(REG_CONFIG);
  Serial.printf("Config register 0xB0: before=0x%02X, after=0x%02X\n", before, after);
}

bool checkBlockBad(uint16_t block, uint8_t &mainMarker, uint8_t &spareMarker) {
  const uint16_t page0 = block * PAGES_PER_BLOCK;

  mainMarker = 0xFF;
  spareMarker = 0xFF;

  if (!loadPageToCache(page0)) {
    Serial.printf("READ_TIMEOUT,block=%u,page=%u\n", block, page0);
    mainMarker = 0x00;
    spareMarker = 0x00;
    return true;
  }

  mainMarker = readCacheByte(0);
  spareMarker = readCacheByte(SPARE_OFFSET);

  bool bad = false;

  if (CHECK_MAIN_ARRAY_MARKER && mainMarker != 0xFF) {
    bad = true;
  }

  if (CHECK_SPARE_MARKER && spareMarker != 0xFF) {
    bad = true;
  }

  return bad;
}

void printCsvHeader() {
  Serial.println();
  Serial.println("block,page,main_byte0,spare_byte0,result");
}

void scanBadBlocks() {
  uint16_t badCount = 0;

  Serial.println();
  Serial.println("Starting read-only W25N01 bad-block marker scan...");
  Serial.printf("Geometry: %u blocks, %u pages/block, %u + %u bytes/page\n",
                TOTAL_BLOCKS, PAGES_PER_BLOCK, PAGE_SIZE, SPARE_SIZE);
  Serial.printf("Checking main marker:  %s\n", CHECK_MAIN_ARRAY_MARKER ? "yes" : "no");
  Serial.printf("Checking spare marker: %s\n", CHECK_SPARE_MARKER ? "yes" : "no");

  printCsvHeader();

  for (uint16_t block = 0; block < TOTAL_BLOCKS; block++) {
    uint8_t mainMarker;
    uint8_t spareMarker;
    const uint16_t page0 = block * PAGES_PER_BLOCK;
    const bool bad = checkBlockBad(block, mainMarker, spareMarker);

    if (bad) {
      badCount++;
      Serial.printf("%u,%u,0x%02X,0x%02X,BAD\n", block, page0, mainMarker, spareMarker);
    }

    if ((block % 64) == 0) {
      Serial.printf("# progress: %u/%u blocks\n", block, TOTAL_BLOCKS);
    }
  }

  Serial.println();
  Serial.printf("Scan finished. Bad blocks found: %u/%u\n", badCount, TOTAL_BLOCKS);

  if (badCount == 0) {
    Serial.println("No bad-block marker found.");
  }

  if (badCount > 20) {
    Serial.println("Warning: more than 20 bad blocks found. Check wiring, voltage, SPI mode, and marker settings.");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(PIN_FLASH_CS, OUTPUT);
  digitalWrite(PIN_FLASH_CS, HIGH);

  SPI.begin(PIN_FLASH_SCK, PIN_FLASH_MISO, PIN_FLASH_MOSI, PIN_FLASH_CS);

  Serial.println();
  Serial.println("W25Nxx_Check - ESP32 read-only SPI NAND bad-block scanner");

  resetFlash();

  uint8_t manufacturer = 0;
  uint8_t id1 = 0;
  uint8_t id2 = 0;
  readJedecId(manufacturer, id1, id2);

  Serial.printf("JEDEC ID: %02X %02X %02X\n", manufacturer, id1, id2);
  if (manufacturer != 0xEF) {
    Serial.println("Warning: Manufacturer ID is not Winbond 0xEF. Check wiring or chip type.");
  }

  Serial.printf("Protection register 0xA0: 0x%02X\n", getFeature(REG_PROTECTION));
  Serial.printf("Config register     0xB0: 0x%02X\n", getFeature(REG_CONFIG));
  Serial.printf("Status register     0xC0: 0x%02X\n", getFeature(REG_STATUS));

  forceBufferReadMode();
  scanBadBlocks();
}

void loop() {
  // Scan runs once in setup(). Press reset to scan again.
}
