# M5Paper Firmware Binaries

Pre-built firmware binaries for M5Paper.

## Files

- `bootloader.bin` (18KB) - ESP32 bootloader
- `partitions.bin` (3KB) - Partition table
- `firmware.bin` (2.0MB) - Main firmware application

## Installation

### Using esptool.py

```bash
# Install esptool.py
pip install esptool

# Upload firmware (change COM4 to your port)
esptool.py --chip esp32 --port COM4 --baud 921600 write_flash \
  0x1000 bootloader.bin \
  0x8000 partitions.bin \
  0x10000 firmware.bin
```

### Using PlatformIO

```bash
git clone https://github.com/maskelog/diy-esp32s3-epub-reader.git
cd diy-esp32s3-epub-reader
pio run -e m5paper -t upload
```

## Notes

- Enter download mode: Hold BOOT button, press RESET, then release BOOT
- Windows: Ports are `COM3`, `COM4`, etc. (check Device Manager)
- Linux/Mac: Ports are `/dev/ttyUSB0` or `/dev/tty.usbserial-*`

## Build Info

- Build environment: PlatformIO
- Target: M5Paper (ESP32)
- Framework: Arduino
- Partition table: `partitions.csv` (app0: 3MB, spiffs: 832KB)
