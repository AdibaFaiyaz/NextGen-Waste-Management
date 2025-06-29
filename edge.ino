#include <SoftwareSerial.h>
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <Adafruit_Fingerprint.h>
#include <Servo.h>

// WiFi Credentials
const char* ssid = "WIFI_SSID";
const char* password = "WIFI_PASSWORD";
const char* mqtt_server = "broker.hivemq.com";

// MQTT Topics
const char* topicReceive = "smartdustbin/commands";
const char* topicSend = "smartdustbin/responses";

// Pin Definitions
#define UNROLL_PIN1 8
#define UNROLL_PIN2 9
#define COMPACT_PIN1 10
#define COMPACT_PIN2 11
#define TRIG_PIN 3
#define ECHO_PIN 2
#define GAS_SENSOR A5
#define SERVO_PIN 4
#define IR_PIN 5
#define LED_PIN 6

// Globals
WiFiClient espClient;
PubSubClient client(espClient);
Servo servoMotor;
SoftwareSerial mySerial(12, 13); // Fingerprint sensor
Adafruit_Fingerprint finger(&mySerial);

// Function Declarations
bool enrollFingerprint(int id);
bool verifyFingerprint();
void clearFingerprintDatabase();
int readUltrasonicDistance(int trigPin, int echoPin);
bool isNumber(const char* str);
void processCommand(String command);

// WiFi Setup
void setup_wifi() {
  delay(10);
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected. IP: ");
  Serial.println(WiFi.localIP());
}

// MQTT Callback
void callback(char* topic, byte* payload, unsigned int length) {
  String command = "";
  for (int i = 0; i < length; i++) {
    command += (char)payload[i];
  }
  processCommand(command);
}

// MQTT Reconnect Logic
void reconnect() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    if (client.connect("SmartDustbinClient")) {
      Serial.println("connected.");
      client.subscribe(topicReceive);
    } else {
      Serial.print("Failed (rc=");
      Serial.print(client.state());
      Serial.println("). Retrying...");
      delay(5000);
    }
  }
}

// Process Incoming Commands
void processCommand(String command) {
  if (command.startsWith("register")) {
    String userIDStr = command.substring(8);
    userIDStr.trim();
    
    if (userIDStr.length() == 0) {
      client.publish(topicSend, "Invalid command format. Use: register<ID>");
      return;
    }
    
    int userID = userIDStr.toInt();
    if (userID <= 0 || userID >= 127) {
      client.publish(topicSend, "Invalid ID. Use ID between 1-126");
      return;
    }
    
    client.publish(topicSend, "Starting enrollment...");
    if (enrollFingerprint(userID)) {
      client.publish(topicSend, "Enrollment_Done");
      client.publish(topicSend, "registration_success");
    } else {
      client.publish(topicSend, "Enrollment_not_done");
      client.publish(topicSend, "registration_failed");
    }
  }

  else if (command == "verify") {
    client.publish(topicSend, verifyFingerprint() ? "approved" : "denied");
  }

  else if (command == "clear_all_users") {
    clearFingerprintDatabase();
  }

  else if (command == "get_ultra") {
    int distance = readUltrasonicDistance(TRIG_PIN, ECHO_PIN);
    char distanceStr[10];
    sprintf(distanceStr, "%d", distance);
    client.publish(topicSend, distanceStr);
  }

  else if (command == "get_gas") {
    int gasLevel = analogRead(GAS_SENSOR);
    char gasStr[10];
    sprintf(gasStr, "%d", gasLevel);
    client.publish(topicSend, gasStr);
  }

  else if (command == "get_ir") {
    int ir_status = digitalRead(IR_PIN);
    client.publish(topicSend, ir_status == LOW ? "Detected" : "Not Detected");
  }

  else if (command == "compaction") {
    // Unroll
    digitalWrite(UNROLL_PIN1, HIGH); digitalWrite(UNROLL_PIN2, LOW); delay(5000);
    digitalWrite(UNROLL_PIN1, LOW);  digitalWrite(UNROLL_PIN2, LOW);

    // Compact
    digitalWrite(COMPACT_PIN1, HIGH); digitalWrite(COMPACT_PIN2, LOW); delay(5000);
    digitalWrite(COMPACT_PIN1, LOW);  digitalWrite(COMPACT_PIN2, HIGH); delay(5000);
    digitalWrite(COMPACT_PIN1, LOW);  digitalWrite(COMPACT_PIN2, LOW);

    // Rollback
    digitalWrite(UNROLL_PIN1, LOW); digitalWrite(UNROLL_PIN2, HIGH); delay(5000);
    digitalWrite(UNROLL_PIN1, LOW); digitalWrite(UNROLL_PIN2, LOW);

    client.publish(topicSend, "Compaction done");
  }

  else if (command == "uv_led") {
    digitalWrite(LED_PIN, HIGH); delay(8000); digitalWrite(LED_PIN, LOW);
    client.publish(topicSend, "sterilised");
  }

  else if (command == "open_lid") {
    servoMotor.write(90);
  }

  else if (command == "close_lid") {
    servoMotor.write(0);
  }
}

void setup() {
  Serial.begin(9600);
  mySerial.begin(57600);

  if (finger.verifyPassword()) {
    Serial.println("Fingerprint sensor detected!");
  } else {
    Serial.println("Fingerprint sensor not found.");
    while (1);
  }

  // Pin Modes
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(GAS_SENSOR, INPUT);
  pinMode(IR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(UNROLL_PIN1, OUTPUT); pinMode(UNROLL_PIN2, OUTPUT);
  pinMode(COMPACT_PIN1, OUTPUT); pinMode(COMPACT_PIN2, OUTPUT);
  servoMotor.attach(SERVO_PIN);

  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();
}

// Helper Functions Implementation
bool enrollFingerprint(int id) {
  Serial.print("Enrolling fingerprint ID #");
  Serial.println(id);
  
  if (id == 0 || id >= 127) {
    Serial.println("Invalid ID (1-126)");
    return false;
  }
  
  Serial.println("Place finger on sensor...");
  int p = -1;
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch (p) {
      case FINGERPRINT_OK:
        Serial.println("Image taken");
        break;
      case FINGERPRINT_NOFINGER:
        Serial.print(".");
        delay(100);
        break;
      case FINGERPRINT_PACKETRECIEVEERR:
        Serial.println("Communication error");
        return false;
      case FINGERPRINT_IMAGEFAIL:
        Serial.println("Imaging error");
        return false;
      default:
        Serial.println("Unknown error");
        return false;
    }
  }

  // Convert image to template
  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) {
    Serial.println("Error converting image");
    return false;
  }

  Serial.println("Remove finger");
  delay(2000);
  p = 0;
  while (p != FINGERPRINT_NOFINGER) {
    p = finger.getImage();
  }

  Serial.println("Place same finger again");
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch (p) {
      case FINGERPRINT_OK:
        Serial.println("Image taken");
        break;
      case FINGERPRINT_NOFINGER:
        Serial.print(".");
        delay(100);
        break;
      case FINGERPRINT_PACKETRECIEVEERR:
        Serial.println("Communication error");
        return false;
      case FINGERPRINT_IMAGEFAIL:
        Serial.println("Imaging error");
        return false;
      default:
        Serial.println("Unknown error");
        return false;
    }
  }

  // Convert second image
  p = finger.image2Tz(2);
  if (p != FINGERPRINT_OK) {
    Serial.println("Error converting second image");
    return false;
  }

  // Create model
  p = finger.createModel();
  if (p == FINGERPRINT_OK) {
    Serial.println("Prints matched!");
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
    return false;
  } else if (p == FINGERPRINT_ENROLLMISMATCH) {
    Serial.println("Fingerprints did not match");
    return false;
  } else {
    Serial.println("Unknown error");
    return false;
  }

  // Store model
  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK) {
    Serial.println("Fingerprint enrolled successfully!");
    return true;
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
    return false;
  } else if (p == FINGERPRINT_BADLOCATION) {
    Serial.println("Could not store in that location");
    return false;
  } else if (p == FINGERPRINT_FLASHERR) {
    Serial.println("Error writing to flash");
    return false;
  } else {
    Serial.println("Unknown error");
    return false;
  }
}

bool verifyFingerprint() {
  Serial.println("Place finger for verification...");
  
  int p = finger.getImage();
  if (p != FINGERPRINT_OK) {
    Serial.println("Error getting image");
    return false;
  }

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) {
    Serial.println("Error converting image");
    return false;
  }

  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK) {
    Serial.print("Found ID #"); 
    Serial.print(finger.fingerID);
    Serial.print(" with confidence of "); 
    Serial.println(finger.confidence);
    return true;
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
    return false;
  } else if (p == FINGERPRINT_NOTFOUND) {
    Serial.println("Fingerprint not found");
    return false;
  } else {
    Serial.println("Unknown error");
    return false;
  }
}

void clearFingerprintDatabase() {
  Serial.println("Clearing fingerprint database...");
  finger.emptyDatabase();
  Serial.println("Database cleared!");
  client.publish(topicSend, "database_cleared");
}

int readUltrasonicDistance(int trigPin, int echoPin) {
  // Clear the trigger pin
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  
  // Send 10 microsecond pulse
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  // Read echo pin and calculate duration
  long duration = pulseIn(echoPin, HIGH);
  
  // Calculate distance in cm (duration/2 because sound travels to object and back)
  // Speed of sound is 343 m/s or 0.0343 cm/microsecond
  int distance = duration * 0.0343 / 2;
  
  return distance;
}

bool isNumber(const char* str) {
  for (int i = 0; str[i] != 0; i++) {
    if (!isdigit(str[i])) return false;
  }
  return strlen(str) > 0;
}
