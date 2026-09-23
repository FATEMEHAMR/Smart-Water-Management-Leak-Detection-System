# 💧 Smart Water Management & Monitoring Embedded System

An automated, safety-critical water tank management and quality monitoring system developed for Arduino (tested and verified on Arduino Uno via Wokwi). The system provides intelligent water level regulation based on real-time tariffs (RTC), dual-sensor leak detection, continuous water quality assessment (turbidity, pH, temperature), and failsafe emergency shutdown mechanisms.

---

## 📌 Features

- **Automated Level Control (Off-Peak vs. Normal Mode):**
  - Interfaces with an HC-SR04 ultrasonic distance sensor to calculate real-time tank water percentage.
  - Integrates a DS3231/DS1307 Real-Time Clock (RTC) to adjust fill thresholds based on peak and off-peak electricity hours.
- **Differential Flow Leak Detection:**
  - Employs interrupt-driven pulse counting across two flow sensors (Inlet vs. Outlet).
  - Automatically flags persistent discrepancies exceeding configured volumetric/time thresholds and shuts down the main supply valve.
- **Multi-Parameter Water Quality Monitoring:**
  - Evaluates Turbidity, pH, and Temperature (DS18B20 1-Wire).
  - Isolates inflow via the inlet solenoid valve when safety standards are violated.
- **Emergency Watchdog (Dry-Run Protection):**
  - Monitors tank replenishment behavior to prevent pump burn-out or pipe bursts during pump activation if water level does not increase.
- **Multi-Level Priority Alarms:**
  - Distinct auditory and visual feedback using a passive buzzer and status LEDs for quality warnings, leak cutoffs, and critical emergency states.

---

## 🛠 Hardware Architecture & Pinout

| Component | Pin / Interface | Arduino Pin | Description |
| :--- | :--- | :--- | :--- |
| **HC-SR04 Ultrasonic** | Trigger / Echo | `D6` / `D7` | Water level distance measurement |
| **Flow Sensor (Inlet)** | Digital (Interrupt 0) | `D2` | Input flow pulse counter (`450 pulses/L`) |
| **Flow Sensor (Outlet)** | Digital (Interrupt 1) | `D3` | Output flow pulse counter (`450 pulses/L`) |
| **Turbidity Sensor** | Analog Out | `A0` | Water clarity & suspended solids |
| **pH Sensor** | Analog Out | `A1` | Acidity / Alkalinity level |
| **DS18B20 Temperature** | 1-Wire (DQ) | `D8` | Water temperature sensor (4.7kΩ pull-up) |
| **RTC (DS3231 / DS1307)** | I2C (SDA / SCL) | `A4` / `A5` | Real-Time Clock |
| **Pump Relay** | Control Signal (IN) | `D9` | Active-HIGH pump switching |
| **Main Valve Relay** | Control Signal (IN) | `D10` | Main water distribution valve |
| **Inlet Valve Relay** | Control Signal (IN) | `D11` | Inflow cutoff safety valve |
| **Red Alarm LED** | Anode (via 220Ω) | `D12` | Leak alarm indicator |
| **Yellow Warning LED**| Anode (via 220Ω) | `D13` | Water quality warning indicator |
| **Piezo Buzzer** | Positive Terminal | `D5` | Audible alert system |

---

## ⚙️ Operating Logic & Parameters

### 1. Water Level Regulation (Task 1 - 1 Hz)
- **Normal Hours (06:00 – 23:00):**
  - Pump ON threshold: $\le 20\%$
  - Pump OFF threshold: $\ge 90\%$
- **Off-Peak Hours (23:00 – 06:00):**
  - Pump ON threshold: $\le 40\%$
  - Pump OFF threshold: $= 100\%$

### 2. Leak Detection (Task 2 - 1 Hz)
- Measures differential volume: $\Delta V = V_{\text{in}} - V_{\text{out}}$.
- If volumetric accumulation reaches `10.0 L` over sustained intervals (`5 minutes`), the system executes emergency cutoff:
  - De-energizes the main valve.
  - Halts pump operation.
  - Triggers the red strobe and a 1000 Hz pulsing alarm.

### 3. Water Quality Assurance (Task 3 - 0.5 Hz)
Safe operating parameters:
- **Turbidity:** $\le 5.0\text{ NTU}$
- **pH:** $6.5 - 8.5$
- **Temperature:** $5.0^\circ\text{C} - 35.0^\circ\text{C}$
- *Action on violation:* Shuts inlet valve and activates yellow alert LED with triple-chirp buzzer pattern (2000 Hz).

### 4. Emergency Dry-Run Watchdog (Task 4 - 1 Hz)
- If the pump runs for more than 20 seconds while the tank level remains critical ($< 5\%$), the watchdog trips into a permanent `emergencyStop` state to prevent pump burn-out.
- Activates a continuous 1500 Hz tone.

---

## 📦 Required Libraries

Install the following libraries via the Arduino IDE Library Manager:
- [RTClib](https://github.com/adafruit/RTClib) by Adafruit
- [OneWire](https://github.com/PaulStoffregen/OneWire) by Paul Stoffregen
- [DallasTemperature](https://github.com/milesburton/Arduino-Temperature-Control-Library) by Miles Burton

---

## 🚀 Getting Started

1. Clone or download the project files:
   ```bash
   git clone [https://github.com/](https://github.com/)<your-username>/<repo-name>.git
   cd <repo-name>
