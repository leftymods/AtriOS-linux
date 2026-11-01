# AtriOS Station Manager (Android Companion App)

Material 3 Companion Application for **Yandex Station Max** running **AtriOS Linux**.
Operates **100% over Bluetooth** (Bluetooth Classic RFCOMM / Serial Port Profile & BLE). No local Wi-Fi or router required!

---

## 📱 Bluetooth Remote Control Architecture

The Android application communicates directly with the station over an active Bluetooth connection:
* **Protocol**: Bluetooth Serial Port Profile (RFCOMM SPP on Channel 1, standard UUID `00001101-0000-1000-8000-00805F9B34FB`).
* **Coexistence**: Works simultaneously while streaming music from the phone over Bluetooth A2DP audio!
* **Zero Configuration**: Discovers paired `Yandex Station Max` automatically and connects with a single tap.

---

## 🎛️ Features

1. **Dashboard & Telemetry (Over Bluetooth)**:
   - Real-time CPU temperature of Amlogic S905X3, RAM usage, and system uptime.
   - Live connection state and signal diagnostics.

2. **Acoustic Volume & Audio Tuning**:
   - Master volume slider (0..100%) mapped to wide dynamic range curve ($-105$ dB to $0$ dB).
   - Instant mute toggle and remote audio chime diagnostic test.
   - Hardware amplifier stage monitor (Silergy SY6045S Woofer 30W + Tweeters 2x10W, 20V PVDD rail, ES8156 headphone DAC).
   - Acoustic DSP presets (Flat, Bass Boost, Acoustic, Vocal, Night).

3. **LED Ring & Screen Management (Over Bluetooth)**:
   - Top LED Ring (24 RGB LEDs): Volume tracking, clock pulse, rainbow loop, breathing, off.
   - Ring brightness slider (0..100%).
   - Front LED Matrix Display: 24h digital clock, weather, audio spectrum, off.
   - Matrix brightness slider.

4. **Over-the-Air Wi-Fi Provisioning via Bluetooth**:
   - Easily configure the station's home Wi-Fi network credentials (SSID & Password) sent securely over Bluetooth.
   - Eliminates the need for any serial cables or initial IP configuration.

5. **System & Maintenance**:
   - Remote station reboot over Bluetooth.
   - Hardware profile & specifications inspector.

---

## 🎨 Design System
- Built with **Jetpack Compose** and **Material Design 3 (Material You)**.
- Dark & Light mode support with dynamic color theming.
- Bottom navigation bar with smooth screen transitions.

---

## 🛠️ Building & Running

1. Open the `android/` directory in **Android Studio Ladybug (2024.2+)** or newer.
2. Ensure JDK 17+ is selected in **Settings $\to$ Build $\to$ Build Tools $\to$ Gradle**.
3. Pair your Android phone with **"Yandex Station Max"** in Android Bluetooth Settings (the same pairing used for Bluetooth music).
4. Run the app on your phone — it will detect the paired station and connect over Bluetooth automatically!
