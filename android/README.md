# AtriOS Station Manager (Android Companion App)

Material 3 Companion Application for **Yandex Station Max** running **AtriOS Linux**.

---

## 📱 Features

1. **Dashboard & Telemetry**:
   - Live hardware status of Yandex Station Max (Amlogic S905X3, Linux kernel 4.9.113).
   - Real-time CPU temperature, RAM usage, and system uptime.
   - Live connection state (Wi-Fi SSID/IP, Bluetooth A2DP stream).

2. **Acoustic & Audio Control**:
   - Master volume slider (0..100%) mapped to wide dynamic range curve ($-105$ dB to $0$ dB).
   - Instant mute toggle and audio chime test.
   - Hardware amplifier status (Silergy SY6045S Woofer 30W + Tweeters 20W, 20V PVDD rail, ES8156 headphone DAC).
   - Acoustic EQ presets (Flat, Bass Boost, Acoustic, Vocal, Night).

3. **LED Ring & Screen Management**:
   - Top LED Ring (24 RGB LEDs): Volume tracking, clock pulse, rainbow loop, breathing, off.
   - Ring brightness slider (0..100%).
   - Front LED Matrix Display (monochrome): 24h digital clock, weather, audio spectrum, off.
   - Matrix brightness slider.

4. **Wireless & Connectivity**:
   - Wi-Fi 802.11ac scan and status.
   - Bluetooth 5.0 (RTL8822CS UART) streaming status and connected device details.
   - Zigbee 3.0 coordinator integration (Silicon Labs EFR32MG21).

5. **System & Maintenance**:
   - Remote station reboot.
   - Sound diagnostic sweep.
   - Hardware profile inspector.

---

## 🎨 Design System
- Built with **Jetpack Compose** and **Material Design 3 (Material You)**.
- Dark & Light mode support with dynamic color theming.
- Bottom navigation bar with smooth screen transitions.

---

## 🛠️ Building & Installation

1. Open the `android/` directory in **Android Studio Ladybug (2024.2+)** or newer.
2. Ensure JDK 17+ is selected in **Settings $\to$ Build $\to$ Build Tools $\to$ Gradle**.
3. Sync Gradle and run on an Android device or emulator running **Android 8.0 (API 26)** or newer.
4. When launching the app, enter the local IP address of your Yandex Station Max (e.g. `192.168.1.100`), which serves the REST API on port `8080`.
