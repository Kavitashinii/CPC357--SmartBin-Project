# Smart Bin Monitoring System (IoT)

## Project Overview
An IoT-based Smart Bin system that monitors trash level, gas presence, and user motion using **ESP32**. The system publishes real-time data to a cloud MQTT server and displays it on a web dashboard, while also storing sensor data in **Supabase** for analysis.

This project supports **UN SDG 11: Sustainable Cities and Communities** by improving waste monitoring efficiency.

---

## 🔧 System Features
- Real-time bin level monitoring (Ultrasonic sensor)
- Motion detection for automatic lid opening (IR sensor + Servo)
- Gas detection alert (Gas sensor)
- LED status indicators (Full / Empty)
- MQTT cloud communication (Google Cloud VM)
- Web dashboard with real-time charts
- Cloud database storage using Supabase

---

## Hardware & Software Requirements

### Hardware
- ESP32-S3 Development Board  
- HC-SR04 Ultrasonic Sensor  
- IR Motion Sensor  
- Gas Sensor (MQ series)  
- Servo Motor  
- Red & Green LEDs  

### Software & Services
- Arduino IDE  
- Google Cloud Platform (VM)  
- Mosquitto MQTT Broker  
- Supabase Account  
- Web Browser (for dashboard)  

---

## Project Structure

```bash
smart-bin-iot/
├── esp32/
│   └── smart_bin_esp32_code/
│       └── smart_bin_esp32_code.ino
├── dashboard/
│   └── index.html
└── README.md
````

## PART A — ESP32 SETUP (LOCAL COMPUTER)

### Step 1: Clone Repository for Development

```bash
git clone https://github.com/Kavitashinii/CPC357--SmartBin-Project.git
cd CPC357--SmartBin-Project
````

### Step 2: Configure ESP32 Code

Open `esp32/smart_bin_esp32_code/smart_bin_esp32_code.ino` in Arduino IDE and update these fields:

```cpp
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* MQTT_BROKER = "YOUR_VM_PUBLIC_IP";
const char* SUPABASE_URL = "YOUR_SUPABASE_URL";
const char* SUPABASE_KEY = "YOUR_SUPABASE_API_KEY";
```

### Step 3: Upload Firmware

1. Connect ESP32 to PC via USB
2. Select correct **Board** and **Port**
3. Click **Upload**
4. Open Serial Monitor to verify expected output:

   * WiFi connected
   * MQTT connected
   * Sensor readings printed

---

## ✅ PART B — CLOUD MQTT SERVER (GOOGLE CLOUD VM)

### Step 4: Create VM Instance

* OS: Ubuntu 22.04
* Machine: e2-micro

### Step 5: Open Firewall Ports

* TCP 1883 → MQTT
* TCP 9001 → WebSocket

### Step 6: Install Mosquitto on VM

```bash
sudo apt update
sudo apt install mosquitto mosquitto-clients -y
```

Edit config:

```bash
sudo nano /etc/mosquitto/mosquitto.conf
```

Add:

```
listener 1883
protocol mqtt

listener 9001
protocol websockets

allow_anonymous true
```

Restart Mosquitto:

```bash
sudo systemctl restart mosquitto
sudo systemctl enable mosquitto
```

### Step 7: Test MQTT

```bash
mosquitto_sub -t smartbin/data -v
```

When ESP32 runs, JSON messages should appear.

---

## ✅ PART C — WEB DASHBOARD

### Step 8: Configure Dashboard

Open `dashboard/index.html` and update:

```javascript
const MQTT_BROKER = 'ws://YOUR_VM_PUBLIC_IP:9001';
```

### Step 9: Run Dashboard

* Just double-click `index.html`
* Dashboard will show:

  * Bin status
  * Fill percentage
  * Gas alert
  * Charts

---

## ✅ PART D — SUPABASE DATABASE

### Step 10: Create Supabase Project

1. Go to Supabase and create an account.

2. Click New Project → give it a name (e.g., smart-bin) → choose Password for database authentication → wait for initialization.
   

### Step 11: Create Tables

In Supabase SQL Editor, run:

```sql
-- Enable UUID extension
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

-- SENSOR READINGS (Raw Data)
CREATE TABLE IF NOT EXISTS sensor_readings (
    id UUID DEFAULT uuid_generate_v4() PRIMARY KEY,
    device_id TEXT DEFAULT 'esp32_smartbin',
    distance_cm INTEGER NOT NULL,
    gas_value INTEGER NOT NULL,
    motion_detected BOOLEAN DEFAULT FALSE,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT (NOW() AT TIME ZONE 'Asia/Kuala_Lumpur')
);

-- BIN STATUS (Calculated)
CREATE TABLE IF NOT EXISTS bin_status (
    id UUID DEFAULT uuid_generate_v4() PRIMARY KEY,
    device_id TEXT DEFAULT 'esp32_smartbin',
    fill_percentage DECIMAL(5,2) CHECK (fill_percentage >= 0 AND fill_percentage <= 100),
    bin_status TEXT CHECK (bin_status IN ('EMPTY','HALF_FULL','FULL')),
    gas_status TEXT CHECK (gas_status IN ('SAFE','DETECTED')),
    lid_open BOOLEAN DEFAULT FALSE,
    red_led BOOLEAN DEFAULT FALSE,
    green_led BOOLEAN DEFAULT FALSE,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT (NOW() AT TIME ZONE 'Asia/Kuala_Lumpur')
);

-- ALERTS (Critical Events)
CREATE TABLE IF NOT EXISTS alerts (
    id UUID DEFAULT uuid_generate_v4() PRIMARY KEY,
    device_id TEXT DEFAULT 'esp32_smartbin',
    alert_type TEXT CHECK (alert_type IN ('BIN_FULL','GAS_DETECTED','SYSTEM_ERROR')),
    alert_level TEXT CHECK (alert_level IN ('CRITICAL','WARNING','INFO')),
    alert_message TEXT,
    created_at TIMESTAMP WITH TIME ZONE DEFAULT (NOW() AT TIME ZONE 'Asia/Kuala_Lumpur')
);

```
### Step 12: Enable Row-Level Security (RLS)

This automatically calculates bin fill %, status, LEDs, and inserts alerts when new sensor data is added:

```sql
ALTER TABLE sensor_readings ENABLE ROW LEVEL SECURITY;
ALTER TABLE bin_status ENABLE ROW LEVEL SECURITY;
ALTER TABLE alerts ENABLE ROW LEVEL SECURITY;

-- For testing, allow public access
CREATE POLICY "Allow all operations" ON sensor_readings FOR ALL USING (true) WITH CHECK (true);
CREATE POLICY "Allow all operations" ON bin_status FOR ALL USING (true) WITH CHECK (true);
CREATE POLICY "Allow all operations" ON alerts FOR ALL USING (true) WITH CHECK (true);
```
### Step 13: Auto Calculation Trigger

```sql
-- Function to calculate fill percentage
CREATE OR REPLACE FUNCTION calculate_fill_percentage(distance_cm INTEGER)
RETURNS DECIMAL(5,2) AS $$
DECLARE
    empty_distance CONSTANT DECIMAL(5,2) := 16.0;
BEGIN
    IF distance_cm IS NULL OR distance_cm <= 0 OR distance_cm > empty_distance THEN
        RETURN 0;
    END IF;
    RETURN ROUND(((empty_distance - distance_cm) / empty_distance) * 100.0, 2);
END;
$$ LANGUAGE plpgsql;

-- Trigger function for processing sensor readings
CREATE OR REPLACE FUNCTION process_sensor_reading()
RETURNS TRIGGER AS $$
DECLARE
    fill_percent DECIMAL(5,2);
    bin_status_val TEXT;
    gas_status_val TEXT;
    red_led_val BOOLEAN;
    green_led_val BOOLEAN;
    lid_open_val BOOLEAN;
    current_time_val TIMESTAMP WITH TIME ZONE;
BEGIN
    current_time_val := (NOW() AT TIME ZONE 'Asia/Kuala_Lumpur');
    fill_percent := calculate_fill_percentage(NEW.distance_cm);

    -- Determine bin status & LEDs
    IF fill_percent >= 80 THEN
        bin_status_val := 'FULL'; red_led_val := TRUE; green_led_val := FALSE;
    ELSIF fill_percent >= 30 THEN
        bin_status_val := 'HALF_FULL'; red_led_val := FALSE; green_led_val := TRUE;
    ELSE
        bin_status_val := 'EMPTY'; red_led_val := FALSE; green_led_val := TRUE;
    END IF;

    -- Gas status
    IF NEW.gas_value > 1350 THEN gas_status_val := 'DETECTED'; ELSE gas_status_val := 'SAFE'; END IF;
    lid_open_val := NEW.motion_detected;

    -- Insert calculated bin status
    INSERT INTO bin_status (device_id, fill_percentage, bin_status, gas_status, lid_open, red_led, green_led, created_at)
    VALUES (NEW.device_id, fill_percent, bin_status_val, gas_status_val, lid_open_val, red_led_val, green_led_val, current_time_val);

    -- Insert alerts
    IF bin_status_val = 'FULL' THEN
        INSERT INTO alerts (device_id, alert_type, alert_level, alert_message, created_at)
        VALUES (NEW.device_id, 'BIN_FULL', 'CRITICAL', 'Bin is full and needs emptying', current_time_val);
    END IF;

    IF gas_status_val = 'DETECTED' THEN
        INSERT INTO alerts (device_id, alert_type, alert_level, alert_message, created_at)
        VALUES (NEW.device_id, 'GAS_DETECTED', 'WARNING', 'Gas detected near bin', current_time_val);
    END IF;

    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- Create trigger
DROP TRIGGER IF EXISTS on_sensor_reading_insert ON sensor_readings;
CREATE TRIGGER on_sensor_reading_insert
AFTER INSERT ON sensor_readings
FOR EACH ROW
EXECUTE FUNCTION process_sensor_reading();

```

### Step 14: Get API Credentials

From Supabase:

* **Project URL** → SUPABASE_URL
* **anon public key** → SUPABASE_KEY

ESP32 sends data using HTTP POST every few seconds.

