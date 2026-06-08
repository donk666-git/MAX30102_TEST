# Watch App

Flutter Android dashboard for the ESP32 watch BLE service.

This folder contains the Android Flutter app source, generated Android scaffold,
and pinned dependency versions. Keep `pubspec.lock` under version control.

## Local checks

```powershell
cd E:\Esp32\project\watch_test\watch_app
F:\software\flutter\bin\flutter.bat pub get
F:\software\flutter\bin\flutter.bat analyze
F:\software\flutter\bin\flutter.bat test
```

## Android debug

1. Flash the ESP32 firmware.
2. Open the serial monitor at 115200 baud.
3. Send `BLE ON`; the ESP32 advertises as `ESP32-Watch`.
4. Connect an Android phone with USB debugging enabled.
5. Run:

```powershell
F:\software\flutter\bin\flutter.bat devices
F:\software\flutter\bin\flutter.bat run
```

Use nRF Connect first if the app cannot scan or subscribe, because it separates
firmware/BLE protocol issues from Flutter permission or UI issues.
