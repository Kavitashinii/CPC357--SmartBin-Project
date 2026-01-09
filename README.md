🗑️ Smart Bin Monitoring System (IoT)
Project Overview

An IoT-based Smart Bin system that monitors trash level, gas presence, and user motion using ESP32.
The system publishes real-time data to a cloud MQTT server and displays it on a web dashboard, while also storing sensor data in Supabase for analysis.

This project supports UN SDG 11: Sustainable Cities and Communities by improving waste monitoring efficiency.

🔧 System Features

Real-time bin level monitoring (Ultrasonic sensor)

Motion detection for automatic lid opening (IR sensor + Servo)

Gas detection alert (Gas sensor)

LED status indicators (Full / Empty)

MQTT cloud communication (Google Cloud VM)

Web dashboard with real-time charts

Cloud database storage using Supabase

🧰 Hardware & Software Requirements
Hardware

ESP32-S3 Development Board

HC-SR04 Ultrasonic Sensor

IR Motion Sensor

Gas Sensor (MQ series)

Servo Motor

Red & Green LEDs

Software & Services

Arduino IDE

Google Cloud Platform (VM)

Mosquitto MQTT Broker

Supabase Account

Web Browser (for dashboard)

📁 Project Structure
Smart-Bin-IoT/
│
├── esp32/            # ESP32 firmware (Arduino)
│   └── smartbin.ino
│
├── dashboard/        # Web dashboard
│   └── index.html
│
├── vm/               # MQTT server setup guide
│   └── README.md
│
├── supabase/         # Database setup
│   └── README.md
│
└── README.md         # Project documentation

✅ PART A — ESP32 SETUP (LOCAL COMPUTER)
Step 1: Configure ESP32 Code

Open esp32/smartbin.ino in Arduino IDE

Update these fields:

const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* MQTT_BROKER = "YOUR_VM_PUBLIC_IP";
const char* SUPABASE_KEY = "YOUR_SUPABASE_API_KEY";

Step 2: Upload Firmware

Connect ESP32 to PC via USB

Select correct Board and Port

Click Upload

Open Serial Monitor to verify:

Expected output:

WiFi connected

MQTT connected

Sensor readings printed

✅ PART B — CLOUD MQTT SERVER (GOOGLE CLOUD VM)
Step 3: Create VM Instance

OS: Ubuntu 22.04

Machine: e2-micro

Open Firewall Ports:

TCP 1883 (MQTT)

TCP 9001 (WebSocket)

Step 4: Install Mosquitto on VM
sudo apt update
sudo apt install mosquitto mosquitto-clients -y


Edit config:

sudo nano /etc/mosquitto/mosquitto.conf


Add:

listener 1883
protocol mqtt

listener 9001
protocol websockets

allow_anonymous true


Restart:

sudo systemctl restart mosquitto
sudo systemctl enable mosquitto

Step 5: Test MQTT

Subscribe:

mosquitto_sub -t smartbin/data -v


When ESP32 runs, JSON messages should appear.

✅ PART C — WEB DASHBOARD
Step 6: Configure Dashboard

Open:

dashboard/index.html


Update:

const MQTT_BROKER = 'ws://YOUR_VM_PUBLIC_IP:9001';

Step 7: Run Dashboard

Just double-click:

index.html


Dashboard will show:

Bin status

Fill percentage

Gas alert

Charts

✅ PART D — SUPABASE DATABASE
Step 8: Create Table

In Supabase SQL Editor:

create table sensor_readings (
  id bigint generated always as identity primary key,
  distance_cm int,
  gas_value int,
  motion_detected boolean,
  created_at timestamp with time zone default now()
);

Step 9: Get API Credentials

From Supabase:

Project URL → SUPABASE_URL

anon public key → SUPABASE_KEY

ESP32 sends data using HTTP POST every few seconds.

