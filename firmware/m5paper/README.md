# M5Paper Firmware Binaries

이 디렉토리에는 M5Paper용 펌웨어 바이너리 파일들이 포함되어 있습니다.

## 파일 설명

- `bootloader.bin` (18KB) - ESP32 부트로더
- `partitions.bin` (3KB) - 파티션 테이블
- `firmware.bin` (2.0MB) - 메인 펌웨어 애플리케이션

## 설치 방법

### 방법 1: esptool.py 사용 (권장)

```bash
# esptool.py 설치 (Python이 필요합니다)
pip install esptool

# 펌웨어 업로드 (COM4를 실제 포트로 변경하세요)
esptool.py --chip esp32 --port COM4 --baud 921600 write_flash \
  0x1000 bootloader.bin \
  0x8000 partitions.bin \
  0x10000 firmware.bin
```

### 방법 2: PlatformIO 사용

```bash
# 프로젝트 클론
git clone https://github.com/maskelog/diy-esp32s3-epub-reader.git
cd diy-esp32s3-epub-reader

# 펌웨어 업로드
pio run -e m5paper -t upload
```

### 방법 3: M5Burner 사용

M5Stack 공식 M5Burner 도구를 사용하여 업로드할 수 있습니다.

## 주의사항

- 업로드 전에 M5Paper의 BOOT 버튼을 누른 상태에서 RESET 버튼을 눌러 다운로드 모드로 진입해야 할 수 있습니다.
- 포트 번호(COM4)는 실제 연결된 포트로 변경하세요.
- Windows: `COM4`, Linux/Mac: `/dev/ttyUSB0` 또는 `/dev/tty.usbserial-*`

## 빌드 정보

- 빌드 환경: PlatformIO
- 타겟: M5Paper (ESP32)
- 프레임워크: Arduino
- 파티션 테이블: `partitions.csv` (app0: 3MB, spiffs: 832KB)
