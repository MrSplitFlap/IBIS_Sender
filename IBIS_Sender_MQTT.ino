/*
 * IBIS Display Controller via MQTT
 *
 * This Arduino sketch connects to a WiFi network and subscribes to MQTT topics to control
 * a IBIS FlipDot display and builtin lighting. It is intended for use with Home Assistant or
 * other MQTT-based automation systems.
 *
 * Features:
 * - Connects to WiFi and an MQTT broker
 * - Listens for display messages on the topic: "home/flipdot/message"
 * - Listens for lighting control on the topic: "home/flipdot/lighting" (expects "On"/"Off")
 * - Constructs and sends telegrams following the IBIS protocol used by bus FlipDot displays
 * - Displays the message on a IBIS FlipDot screen via UART using 7E2 serial configuration and a logic level shifter (IBIS Wandler)
 *
 * Special thanks to Cato and Matti for sharing their codebases. These helped a lot while reverse engineering the IBIS protocol:
 * CatoLynx - pyFIS: https://github.com/CatoLynx/pyFIS
 * drive-n-code - Open ITCS: https://github.com/open-itcs/onboard-panel-arduino
 * 
 * Copyright (C) 2025 Mitja Stiens - MrSplitFlap
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Date: 13.05.2025
 */

#include <WiFi.h>
#include <PubSubClient.h>

// Pin configuration
#define lightingPin 28 // GPIO pin used to control internal lighting via a relay

// Wi-Fi Credentials
const char* ssid = "WIFI_SSID";            // WiFi SSID
const char* password = "WIFI_PASSWORD";    // WiFi password

// MQTT credentials
const char* MQTTid = "MQTT_CLIENT_ID";       // MQTT client ID
const char* MQTTuser = "MQTT_USERNAME";      // MQTT username
const char* MQTTpassword = "MQTT_PASSWORD";  // MQTT password

// MQTT Broker address (use Home Assistant's IP or hostname)
const char* mqtt_server = "192.168.0.X";

// MQTT Topics
const char* topic_message = "home/flipdot/message";
const char* topic_lighting = "home/flipdot/lighting";

// MQTT Setup
WiFiClient wifiClient;
PubSubClient client(wifiClient);

// Buffer for soon to be printed messages (27 characters maximum per line at smallest font)
char flipdotMessage_str[256];

// ******** FlipDot logic handling ********
// Handle German special characters. Converts UTF-8 sequences like "ä", "ö", etc. to IBIS-compatible placeholders
void process_special_characters(char* telegram) {
    unsigned char* src = (unsigned char*)telegram;
    unsigned char* dst = (unsigned char*)telegram;
    while (*src) {
        if (src[0] == 0xC3 && src[1] == 0xA4) { *dst++ = '{'; src += 2; }
        else if (src[0] == 0xC3 && src[1] == 0xB6) { *dst++ = '|'; src += 2; }
        else if (src[0] == 0xC3 && src[1] == 0xBC) { *dst++ = '}'; src += 2; }
        else if (src[0] == 0xC3 && src[1] == 0x9F) { *dst++ = '~'; src += 2; }
        else if (src[0] == 0xC3 && src[1] == 0x84) { *dst++ = '['; src += 2; }
        else if (src[0] == 0xC3 && src[1] == 0x96) { *dst++ = '\\'; src += 2; }
        else if (src[0] == 0xC3 && src[1] == 0x9C) { *dst++ = ']'; src += 2; }
        else { *dst++ = *src++; }
    }
    *dst = '\0';
}

// Wraps telegram with control characters and checksum
void wrap_telegram(char* telegram) {
    strcat(telegram, "\r");
    uint8_t checksum = compute_checksum(telegram);
    int len = strlen(telegram);
    telegram[len] = (char)checksum;
    telegram[len + 1] = '\0';
}

// Calculates XOR checksum used by IBIS protocol
uint8_t compute_checksum(const char* msg)
{
    uint8_t checksum = 0x7F;
    for (int i = 0; msg[i] != '\0'; i++) {
        checksum ^= msg[i];
    }
    return checksum;
}

// Convert integer to VDV (IBIS) hex format. VDV hex uses ASCII codes "0123456789:;<=>?" instead of standard 0-9A-F representation
void vdv_hex(int value, char* output) {
    const char vdvhex[] = "0123456789:;<=>?";
    if (value < 0 || value > 255) {
        strcpy(output, "??");
        return;
    }
    if (value > 15) {
        sprintf(output, "%c%c", vdvhex[value >> 4], vdvhex[value & 0x0F]);
    } else {
        sprintf(output, "%c", vdvhex[value]);
    }
}

// Process and send message to IBIS display
void send_telegram(const char* telegram) {
    char processed_telegram[256];
    strncpy(processed_telegram, telegram, sizeof(processed_telegram) - 1);
    process_special_characters(processed_telegram);
    wrap_telegram(processed_telegram);

    // Debug output (HEX and ASCII format)
    Serial.print("Sending HEX: ");
    for (int i = 0; processed_telegram[i] != '\0'; i++) {
        char hex[4];
        sprintf(hex, "%02X ", (uint8_t)processed_telegram[i]);
        Serial.print(hex);
    }
    Serial.println();

    Serial.print("Sending RAW: ");
    Serial.println(processed_telegram);

    // Send telegram via serial interface to IBIS display (logic level shifter (IBIS Wandler) necessary)
    Serial1.write((uint8_t*)processed_telegram, strlen(processed_telegram));
}

// Construct DS021t telegram for displaying message on IBIS display
void DS021t(int address, const char* text) {
    char address_hex[3], num_blocks_hex[3];
    char telegram[256];
    char message[256];

    // Automatically append newline character to print single line messages in full height
    int has_newline = strchr(text, '\n') != NULL;
    if (has_newline) {
        snprintf(message, sizeof(message), "%s\n\n", text);
    } else {
        snprintf(message, sizeof(message), "%s\n\n\n", text);
    }

    int len = strlen(message);
    int total_len_with_padding = len + 2;                     // Add 2 for header overhead
    int num_blocks = (total_len_with_padding + 15) / 16;      // Round up to nearest 16-byte block
    int usable_bytes = (num_blocks * 16) - 2;                 // Exclude 2-byte header

    // Pad message with spaces to fill entire 16-byte block
    for (int i = len; i < usable_bytes; i++) {
        message[i] = ' ';
    }
    message[usable_bytes] = '\0';

    // Format address and block count using VDV hex encoding
    vdv_hex(address, address_hex);
    vdv_hex(num_blocks, num_blocks_hex);

    // Build telegram in IBIS format
    snprintf(telegram, sizeof(telegram), "aA%s%sA0%s", address_hex, num_blocks_hex, message);
    send_telegram(telegram);
}

// ******** MQTT handling ********
// MQTT callback handler
void mqtt_callback(char* topic, byte* payload, unsigned int length) {
    char buffer[256];
    memcpy(buffer, payload, min(length, sizeof(buffer) - 1));
    buffer[min(length, sizeof(buffer) - 1)] = '\0';

    if (strcmp(topic, topic_message) == 0) {
        // Save incoming message to buffer for IBIS display
        int copy_len = min(length, sizeof(flipdotMessage_str) - 1);
        memcpy(flipdotMessage_str, payload, copy_len);
        flipdotMessage_str[copy_len] = '\0';

        // Compose and send display message
        char display[256];
        snprintf(display, sizeof(display), "%s", flipdotMessage_str);
        DS021t(2, display);
    } else if (strcmp(topic, topic_lighting) == 0) {
        if (strncmp(buffer, "On", 2) == 0) {
            // Turn on lighting
            digitalWrite(lightingPin, HIGH);
        } else if (strncmp(buffer, "Off", 3) == 0) {
            // Turn off lighting
            digitalWrite(lightingPin, LOW);
        }
    }
}

// MQTT reconnection handler
void mqtt_reconnect() {
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        if (client.connect(MQTTid, MQTTuser, MQTTpassword)) {
            Serial.println("connected.");
            client.subscribe(topic_message);
            client.subscribe(topic_lighting);
        } else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" retrying in 5s...");
            delay(5000);
        }
    }
}

// ******** Arduino Setup ********
void setup() {
    Serial.begin(9600);  // USB serial debug output
    //while (!Serial);   // Optional: Wait for serial monitor

    Serial1.begin(1200, SERIAL_7E2);  // Serial interface for IBIS display

    pinMode(lightingPin, OUTPUT);
    digitalWrite(lightingPin, LOW);  // Ensure lighting starts off

    // Connect to Wi-Fi
    WiFi.begin(ssid, password);
    Serial.print("Connecting to Wi-Fi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println(" connected!");

    // MQTT setup
    client.setServer(mqtt_server, 1883);
    client.setCallback(mqtt_callback);
}

// ******** Arduino Main Loop ********
void loop() {
    // Maintain MQTT connection
    if (!client.connected()) {
        mqtt_reconnect();
    }
    client.loop();  // Handle incoming MQTT messages
}
