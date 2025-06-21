// Copyright 2024 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*
 * This example is an example code that will create a Matter Device which can be
 * commissioned and controlled from a Matter Environment APP.
 * Additionally the ESP32 will send debug messages indicating the Matter activity.
 * Turning DEBUG Level ON may be useful to following Matter Accessory and Controller messages.
 */

// Matter Manager
#include <Matter.h>
#include <WiFi.h>

#include "Adafruit_DS248x.h"

#define DS18B20_FAMILY_CODE 0x28
#define DS18B20_CMD_CONVERT_T 0x44
#define DS18B20_CMD_MATCH_ROM 0x55
#define DS18B20_CMD_READ_SCRATCHPAD 0xBE

Adafruit_DS248x ds248x;

uint8_t sensor1_rom[8];
uint8_t sensor2_rom[8];
bool sensors_found = false;

// List of Matter Endpoints for this Node
// Matter Temperature Sensor Endpoint
MatterTemperatureSensor TemperatureSensor1;

// WiFi is manually set and started
const char *ssid = "your-ssid";          // Change this to your WiFi SSID
const char *password = "your-password";  // Change this to your WiFi password

// set your board USER BUTTON pin here - decommissioning button
const uint8_t buttonPin = BOOT_PIN;  // Set your pin here. Using BOOT Button.

// Button control - decommision the Matter Node
uint32_t button_time_stamp = 0;                // debouncing control
bool button_state = false;                     // false = released | true = pressed
const uint32_t decommissioningTimeout = 5000;  // keep the button pressed for 5s, or longer, to decommission

void setup() {
  // In.itialize the USER BUTTON (Boot button) that will be used to decommission the Matter Node
  pinMode(buttonPin, INPUT_PULLUP);

  Serial.begin(115200);

  if (!ds248x.begin(&Wire, DS248X_ADDRESS)) {
    Serial.println(F("DS248x initialization failed."));
    while (1);
  }

  Wire.setClock(400000);
  while (!ds248x.OneWireReset()) {
    Serial.println("Failed to do a 1W reset");
    delay(1000);
  }

  Serial.println("One Wire bus reset OK");

  // Search for first 2 sensors
  uint8_t rom[8];
  int count = 0;

  ds248x.OneWireReset();
  while (ds248x.OneWireSearch(rom)) {
    if (rom[0] == DS18B20_FAMILY_CODE) {
      if (count == 0) {
        memcpy(sensor1_rom, rom, 8);
      } else if (count == 1) {
        memcpy(sensor2_rom, rom, 8);
        sensors_found = true;
        break; // We only need 2
      }
      count++;
    }
  }

  if (!sensors_found) {
    Serial.println("Error: Less than two DS18B20 sensors found.");
    while (1);
  }

  Serial.println("Sensors initialized.");

  // Manually connect to WiFi
  WiFi.begin(ssid, password);
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  // set initial temperature sensor measurement
  TemperatureSensor1.begin(0);

  // Matter beginning - Last step, after all EndPoints are initialized
  Matter.begin();

  // Check Matter Accessory Commissioning state, which may change during execution of loop()
  if (!Matter.isDeviceCommissioned()) {
    Serial.println("");
    Serial.println("Matter Node is not commissioned yet.");
    Serial.println("Initiate the device discovery in your Matter environment.");
    Serial.println("Commission it to your Matter hub with the manual pairing code or QR code");
    Serial.printf("Manual pairing code: %s\r\n", Matter.getManualPairingCode().c_str());
    Serial.printf("QR code URL: %s\r\n", Matter.getOnboardingQRCodeUrl().c_str());
    // waits for Matter Temperature Sensor Commissioning.
    uint32_t timeCount = 0;
    while (!Matter.isDeviceCommissioned()) {
      delay(100);
      if ((timeCount++ % 50) == 0) {  // 50*100ms = 5 sec
        Serial.println("Matter Node not commissioned yet. Waiting for commissioning.");
      }
    }
    Serial.println("Matter Node is commissioned and connected to Wi-Fi. Ready for use.");
  }
}

void loop() {
  static uint32_t timeCounter = 0;

  float temp1 = readTemperature(sensor1_rom);
  float temp2 = readTemperature(sensor2_rom);

  Serial.print("Sensor 1 Temperature: ");
  Serial.print(temp1);
  Serial.println(" °C");

  // Print the current temperature value every 5s
  if (!(timeCounter++ % 10)) {  // delaying for 500ms x 10 = 5s
    // Print the current temperature value
    Serial.printf("Current Temperature is %.02fC\r\n", TemperatureSensor1.getTemperature());
    // Update Temperature from the (Simulated) Hardware Sensor
    // Matter APP shall display the updated temperature percent
    TemperatureSensor1.setTemperature(temp1);
  }

  // Check if the button has been pressed
  if (digitalRead(buttonPin) == LOW && !button_state) {
    // deals with button debouncing
    button_time_stamp = millis();  // record the time while the button is pressed.
    button_state = true;           // pressed.
  }

  if (digitalRead(buttonPin) == HIGH && button_state) {
    button_state = false;  // released
  }

  // Onboard User Button is kept pressed for longer than 5 seconds in order to decommission matter node
  uint32_t time_diff = millis() - button_time_stamp;
  if (button_state && time_diff > decommissioningTimeout) {
    Serial.println("Decommissioning Temperature Sensor Matter Accessory. It shall be commissioned again.");
    Matter.decommission();
    button_time_stamp = millis();  // avoid running decommissining again, reboot takes a second or so
  }

  delay(500);
}

float readTemperature(uint8_t *rom) {
  ds248x.OneWireReset();
  ds248x.OneWireWriteByte(DS18B20_CMD_MATCH_ROM);
  for (int i = 0; i < 8; i++) ds248x.OneWireWriteByte(rom[i]);
  ds248x.OneWireWriteByte(DS18B20_CMD_CONVERT_T);
  delay(750);

  ds248x.OneWireReset();
  ds248x.OneWireWriteByte(DS18B20_CMD_MATCH_ROM);
  for (int i = 0; i < 8; i++) ds248x.OneWireWriteByte(rom[i]);
  ds248x.OneWireWriteByte(DS18B20_CMD_READ_SCRATCHPAD);

  uint8_t data[9];
  for (int i = 0; i < 9; i++) {
    ds248x.OneWireReadByte(&data[i]);
  }

  int16_t raw = (data[1] << 8) | data[0];
  return (float)raw / 16.0;
}
