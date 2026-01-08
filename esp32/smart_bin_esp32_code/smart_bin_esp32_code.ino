#define MQTT_MAX_PACKET_SIZE 512  

#include <ESP32Servo.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <HTTPClient.h>      
#include <ArduinoJson.h>     

// ==================== WIFI CREDENTIALS ====================
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ==================== GOOGLE CLOUD MQTT ====================
const char* MQTT_BROKER = "YOUR_VM_PUBLIC_IP"; ;
const int MQTT_PORT = 1883;
const char* MQTT_CLIENT_ID = "esp32_smartbin";

// ==================== SUPABASE CONFIG ====================
const char* SUPABASE_URL = "https://sdubvhobnpsierccdqgb.supabase.co";
const char* SUPABASE_KEY = "YOUR_SUPABASE_API_KEY";
// No need for SUPABASE_ENDPOINT constant anymore

// ==================== PIN DEFINITIONS ====================
const int trigPin = 4;     // HC-SR04 TRIG pin
const int echoPin = 7;     // HC-SR04 ECHO pin
const int irPin2  = 42;    // IR sensor 2 (motion detect)
const int ledPinR = 9;     // Red LED (bin full)
const int ledPinG = 6;     // Green LED (bin empty)
const int servoPin = 5;    // Servo motor
const int gasPin = A6;     // Gas sensor

// ==================== THRESHOLDS ====================
const int gasThreshold = 1000;  // Gas threshold
const int fullDistance = 3;     // Bin full threshold in cm
const float BIN_HEIGHT = 17.0;  // Bin height in cm

// ==================== SERVO ====================
Servo binServo;
bool lidOpen = false;
bool lastMotionState = false;

// ==================== MQTT CLIENT ====================
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ==================== TIMING ====================
unsigned long lastMqttUpdate = 0;
unsigned long lastReconnectAttempt = 0;
unsigned long lastMotionTime = 0;
unsigned long lastSupabaseUpdate = 0;  
const unsigned long MQTT_UPDATE_INTERVAL = 3000;  // Send data every 3 seconds
const unsigned long RECONNECT_INTERVAL = 5000;    // Try reconnecting every 5 seconds
const unsigned long LID_OPEN_DURATION = 3000;     // Lid stays open for 3 seconds
const unsigned long SUPABASE_UPDATE_INTERVAL = 3000;  // ADDED: Send to Supabase every 3 seconds

// ==================== FUNCTION PROTOTYPES ====================
void sendToSupabase(int distance, int gasValue, bool motionDetected);  // FIXED: Changed signature!

// ==================== SETUP ====================
void setup() {
  Serial.begin(9600);
  delay(1000);  // Give serial time to initialize

  // Setup WiFi
  setupWiFi();
  
  // Setup MQTT
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setKeepAlive(60);  // Keep connection alive
  
  // Setup pins
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(irPin2, INPUT_PULLUP);
  pinMode(ledPinR, OUTPUT);
  pinMode(ledPinG, OUTPUT);
  pinMode(gasPin, INPUT);

  analogReadResolution(12); // ESP32 ADC (0–4095)

  // Servo setup
  binServo.setPeriodHertz(50);
  binServo.attach(servoPin, 500, 2500);
  binServo.write(0);  // Lid closed

  // Start with green LED ON (bin empty)
  digitalWrite(ledPinG, HIGH);
  digitalWrite(ledPinR, LOW);

  Serial.println("\n=================================");
  Serial.println("Smart Bin MQTT & Supabase System Started");
  Serial.println("=================================");
  Serial.print("MQTT Broker: ");
  Serial.print(MQTT_BROKER);
  Serial.print(":");
  Serial.println(MQTT_PORT);
  Serial.print("Supabase: ");
  Serial.println(SUPABASE_URL);
  
  // Warm up gas sensor
  Serial.println("Warming up gas sensor (15 seconds)...");
  delay(15000);
  Serial.println("Gas sensor ready!");
  Serial.println("=================================\n");
}

// ==================== WIFI SETUP ====================
void setupWiFi() {
  Serial.println("Connecting to WiFi...");
  Serial.print("SSID: ");
  Serial.println(WIFI_SSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signal Strength: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("\n✗ WiFi connection failed!");
    Serial.println("Please check your credentials and try again.");
  }
}

// ==================== MQTT RECONNECT (NON-BLOCKING) ====================
void reconnectMQTT() {
  unsigned long now = millis();
  
  // Only attempt reconnection every 5 seconds
  if (now - lastReconnectAttempt > RECONNECT_INTERVAL) {
    lastReconnectAttempt = now;
    
    if (!mqttClient.connected()) {
      Serial.print("Attempting MQTT connection to ");
      Serial.print(MQTT_BROKER);
      Serial.print(":");
      Serial.print(MQTT_PORT);
      Serial.print("...");
      
      // Attempt to connect
      if (mqttClient.connect(MQTT_CLIENT_ID)) {
        Serial.println(" ✓ Connected!");
      } else {
        Serial.print(" ✗ Failed, rc=");
        Serial.print(mqttClient.state());
        
        // Decode the error for easier debugging
        switch(mqttClient.state()) {
          case -4:
            Serial.println(" - Connection timeout (check network)");
            break;
          case -3:
            Serial.println(" - Connection lost");
            break;
          case -2:
            Serial.println(" - Connect failed (check firewall/broker)");
            Serial.println("  → Make sure port 1883 is open in GCP firewall");
            Serial.println("  → Verify Mosquitto is running on the server");
            break;
          case -1:
            Serial.println(" - Disconnected");
            break;
          case 1:
            Serial.println(" - Bad protocol version");
            break;
          case 2:
            Serial.println(" - Bad client ID rejected");
            break;
          case 3:
            Serial.println(" - Server unavailable");
            break;
          case 4:
            Serial.println(" - Bad username/password");
            break;
          case 5:
            Serial.println(" - Not authorized");
            break;
          default:
            Serial.println(" - Unknown error");
        }
      }
    }
  }
}

// ==================== FILL PERCENTAGE CALCULATION ====================
const float EMPTY_DISTANCE = 16.0;  // distance to back wall when bin is empty

float calculateFillPercentage(int distance_cm) {
  // Clamp distance to realistic range
  if (distance_cm <= 0 || distance_cm > EMPTY_DISTANCE) distance_cm = EMPTY_DISTANCE;

  float filledHeight = EMPTY_DISTANCE - distance_cm;  // only count trash
  float percentage = (filledHeight / EMPTY_DISTANCE) * 100.0;

  if (percentage < 0) percentage = 0;
  if (percentage > 100) percentage = 100;

  return percentage;
}

// ==================== SEND DATA TO MQTT ====================
void sendToMQTT(int distance, float fillPercentage, bool binFull, bool motionDetected, int gasValue, bool gasDetected) {
  // Check if MQTT is connected
  if (!mqttClient.connected()) {
    Serial.println("⚠ MQTT not connected, skipping publish");
    return;
  }
  
  // Check if lid should be open based on recent motion
  bool lidCurrentlyOpen = (millis() - lastMotionTime < LID_OPEN_DURATION) && lidOpen;
  
  // Create JSON payload 
  String payload = "{";
  payload += "\"distance\":" + String(distance) + ",";
  payload += "\"FillPercentage\":" + String(fillPercentage, 1) + ",";
  payload += "\"BinStatus\":\"" + String(binFull ? "FULL" : "EMPTY") + "\",";
  payload += "\"MotionDetected\":" + String(motionDetected ? "true" : "false") + ",";
  payload += "\"GasValue\":" + String(gasValue) + ",";
  payload += "\"GasStatus\":\"" + String(gasDetected ? "DETECTED" : "SAFE") + "\",";
  payload += "\"RedLED\":" + String(binFull ? "true" : "false") + ",";  // FIXED: boolean values
  payload += "\"GreenLED\":" + String(binFull ? "false" : "true") + ","; // FIXED: boolean values
  payload += "\"LidOpen\":" + String(lidCurrentlyOpen ? "true" : "false");  // Send current lid state
  payload += "}";
  
  // Debug: Show payload
  Serial.print("→ MQTT Payload: ");
  Serial.println(payload);
  Serial.print("Payload size: ");
  Serial.print(payload.length());
  Serial.println(" bytes");
  
  // Publish main data topic
  bool success = mqttClient.publish("smartbin/data", payload.c_str());
  
  if (success) {
    Serial.println("✓ Data published to MQTT");
  } else {
    Serial.println("✗ Failed to publish to MQTT!");
  }
}

// ==================== SEND DATA TO SUPABASE ====================
void sendToSupabase(int distance, int gasValue, bool motionDetected) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, skipping Supabase");
    return;
  }

  HTTPClient http;

  
  String url = String(SUPABASE_URL) + "/rest/v1/sensor_readings";  

  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", SUPABASE_KEY);
  http.addHeader("Authorization", "Bearer " + String(SUPABASE_KEY));
  http.addHeader("Prefer", "return=minimal");

  // Create JSON payload for Supabase 
  StaticJsonDocument<200> doc; 
  doc["distance_cm"] = distance;
  doc["gas_value"] = gasValue;
  doc["motion_detected"] = motionDetected;

  String jsonString;
  serializeJson(doc, jsonString);

  Serial.print("→ Supabase (RAW): ");
  Serial.println(jsonString);

  int httpCode = http.POST(jsonString);

  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_CREATED || httpCode == HTTP_CODE_OK) {
      Serial.println("✓ Raw data saved to Supabase");
    } else {
      Serial.print("✗ Supabase HTTP Error: ");
      Serial.println(httpCode);
      String response = http.getString();
      Serial.print("Response: ");
      Serial.println(response);
    }
  } else {
    Serial.print("✗ Supabase POST failed: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
}

// ==================== MAIN LOOP ====================
void loop() {
  // Maintain WiFi connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠ WiFi disconnected! Reconnecting...");
    setupWiFi();
  }
  
  // Maintain MQTT connection (non-blocking)
  reconnectMQTT();
  mqttClient.loop();  // Process MQTT messages
  
  // ================= ULTRASONIC SENSOR =================
  long duration;
  int distance;

  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  duration = pulseIn(echoPin, HIGH, 25000); // timeout safety
  distance = duration * 0.034 / 2;

  bool binFull = (distance > 0 && distance <= fullDistance);

  // ================= CALCULATE FILL PERCENTAGE =================
  float fillPercentage = calculateFillPercentage(distance);

  // ================= MOTION SENSOR =================
  bool motionDetected = !digitalRead(irPin2);
  
  // Detect motion edge (when motion changes from false to true)
  if (motionDetected && !lastMotionState) {
    lastMotionTime = millis();  // Record when motion started
  }
  lastMotionState = motionDetected;

  // ================= GAS SENSOR =================
  int gasValue = analogRead(gasPin);
  bool gasDetected = (gasValue > gasThreshold);

  // ================= DEBUG OUTPUT =================
  Serial.print("Distance: ");
  Serial.print(distance);
  Serial.print(" cm | Fill: ");
  Serial.print(fillPercentage, 1);
  Serial.print("% | Bin: ");
  Serial.print(binFull ? "FULL" : "EMPTY");
  Serial.print(" | Motion: ");
  Serial.print(motionDetected ? "YES" : "NO");
  Serial.print(" | Gas: ");
  Serial.print(gasValue);
  Serial.print(" (");
  Serial.print(gasDetected ? "ALERT" : "SAFE");
  Serial.print(") | Lid: ");
  Serial.print(lidOpen ? "OPEN" : "CLOSED");
  Serial.print(" | Red LED: ");
  Serial.print(binFull ? "ON" : "OFF");
  Serial.print(" | Green LED: ");
  Serial.println(binFull ? "OFF" : "ON");

  // ================= LED LOGIC =================
  if (binFull) {
    digitalWrite(ledPinR, HIGH);
    digitalWrite(ledPinG, LOW);
  } else {
    digitalWrite(ledPinR, LOW);
    digitalWrite(ledPinG, HIGH);
  }

  // ================= SERVO LOGIC =================
  // Check if motion was detected and lid should open
  if (motionDetected && !lidOpen && !binFull && (millis() - lastMotionTime < 1000)) {
    Serial.println("→ Opening bin lid...");
    lidOpen = true;
    binServo.write(180);   // Open
    delay(3000);
    binServo.write(60);    // Close
    lidOpen = false;
    Serial.println("→ Bin lid closed");
  }

  // ================= SEND TO MQTT =================
  if (millis() - lastMqttUpdate > MQTT_UPDATE_INTERVAL) {
    lastMqttUpdate = millis();
    sendToMQTT(distance, fillPercentage, binFull, motionDetected, gasValue, gasDetected);
  }

  // ================= SEND TO SUPABASE =================
  if (millis() - lastSupabaseUpdate > SUPABASE_UPDATE_INTERVAL) {
    lastSupabaseUpdate = millis();
    // FIXED: Call with the correct signature - only 3 parameters
    sendToSupabase(distance, gasValue, motionDetected);  // FIXED THIS LINE!
  }
  
  delay(100);  // Small delay to prevent overwhelming the system
}
