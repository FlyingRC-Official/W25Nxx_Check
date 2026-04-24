# W25Nxx_Check

ESP32 read-only bad-block scanner for Winbond **W25N01 / W25Nxx SPI NAND Flash**.

This project is mainly intended for checking whether a W25N01 SPI NAND chip has factory-marked bad blocks after soldering or before using it in a product.

> W25N01 is SPI NAND, not W25Q SPI NOR.  
> NAND Flash can contain factory bad blocks. This is normal and must be handled by firmware or a filesystem layer.

## Features

- ESP32 + Arduino framework
- Reads JEDEC ID
- Reads W25Nxx feature/status registers
- Scans all W25N01 blocks for bad-block markers
- Prints CSV-like output over Serial
- Read-only operation: no erase, no program
- Optional PlatformIO project file included

## Supported chips

Test target:

- Winbond W25N01GV / W25N01 compatible SPI NAND

Likely adaptable to other W25Nxx SPI NAND parts if the geometry and marker rules are changed.

Default geometry in the code:

| Parameter | Value |
|---|---:|
| Total capacity | 1 Gbit / 128 MB |
| Blocks | 1024 |
| Pages per block | 64 |
| Main page size | 2048 bytes |
| Spare / OOB size | 64 bytes |

## Hardware wiring

Default ESP32 pin assignment in `W25Nxx_Check.ino`:

| W25N01 Pin | Function | ESP32 default pin |
|---|---|---:|
| `/CS` | Chip select | GPIO10 |
| `CLK` | SPI clock | GPIO12 |
| `DO / IO1` | MISO | GPIO13 |
| `DI / IO0` | MOSI | GPIO11 |
| `VCC` | 3.3 V | 3.3 V |
| `GND` | Ground | GND |
| `/WP / IO2` | Write protect | Pull up to 3.3 V if unused |
| `/HOLD / IO3` | Hold | Pull up to 3.3 V if unused |

You can change the pins near the top of `W25Nxx_Check.ino`:

```cpp
#define PIN_FLASH_CS    10
#define PIN_FLASH_SCK   12
#define PIN_FLASH_MISO  13
#define PIN_FLASH_MOSI  11
```

### Voltage warning

W25N01 is a **3.3 V device**. Do not connect it directly to 5 V logic.

## How bad-block detection works

The scanner loads Page 0 of every block into the internal cache and reads:

- Main area byte 0: column `0`
- Spare / OOB area byte 0: column `2048`

A block is reported as bad if the selected marker byte is not `0xFF`.

By default, the code checks both markers:

```cpp
static const bool CHECK_MAIN_ARRAY_MARKER = true;
static const bool CHECK_SPARE_MARKER      = true;
```

For a brand-new chip, checking both is useful.

For a chip that has already been used for storing data, the main-array byte 0 may contain user data. In that case, set:

```cpp
static const bool CHECK_MAIN_ARRAY_MARKER = false;
static const bool CHECK_SPARE_MARKER      = true;
```

## Usage with Arduino IDE

1. Install ESP32 board support in Arduino IDE.
2. Open `W25Nxx_Check.ino`.
3. Select your ESP32 board.
4. Set the correct flash wiring pins in the source file.
5. Upload.
6. Open Serial Monitor at **115200 baud**.
7. Press reset to run the scan again.

## Usage with PlatformIO

Open this repository as a PlatformIO project and build/upload:

```bash
pio run -t upload
pio device monitor -b 115200
```

The default board in `platformio.ini` is `esp32-s3-devkitc-1`. Change it if you are using another ESP32 board.

## Example output

```text
W25Nxx_Check - ESP32 read-only SPI NAND bad-block scanner
JEDEC ID: EF AA 21
Protection register 0xA0: 0x00
Config register     0xB0: 0x10
Status register     0xC0: 0x00
Config register 0xB0: before=0x10, after=0x18

Starting read-only W25N01 bad-block marker scan...
Geometry: 1024 blocks, 64 pages/block, 2048 + 64 bytes/page
Checking main marker:  yes
Checking spare marker: yes

block,page,main_byte0,spare_byte0,result
# progress: 0/1024 blocks
12,768,0x00,0x00,BAD
348,22272,0xF0,0x00,BAD

Scan finished. Bad blocks found: 2/1024
```

CSV rows can be copied into a spreadsheet or script to generate a bad-block table.

## Important notes

- This sketch does not repair bad blocks.
- This sketch does not build a flash translation layer.
- NAND Flash users must avoid bad blocks in their storage layer.
- Do not use this code as a destructive erase/write test unless you intentionally add such logic.
- If many blocks are reported bad, first check wiring, voltage, SPI mode, `/WP`, `/HOLD`, and whether the chip is already storing data.

## Repository structure

```text
.
├── W25Nxx_Check.ino
├── platformio.ini
├── README.md
├── LICENSE
└── .gitignore
```

## License

MIT License.
