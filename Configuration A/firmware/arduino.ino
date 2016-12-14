#include <Arduino.h>

#define __ARDUINO_X86__ 1

#define DEBUG_ESP_PORT Serial
#define DEBUG_ESP_HTTP_CLIENT 1

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>

#include <ESP8266HTTPClient.h>

#include <ArduinoJson.h>

#define BUTTON_PIN 16 // D0

#define POWER_PIN 14 // D5
#define ERROR_PIN 15  // D8
#define STATUS_PIN 13 // D7

#define DEBOUNCE_DELAY 50
#define BLINK_DELAY 250

const char *ssid = "<SSID>";
const char *password = "<PASSWORD>";

const char *deviceID = "<Device Identifier>";
const char *baseURL = "<Base URL without endpoints>"; // Include trailing "/"

String fingerprint = "<SHA-1 fingerprint (thumbprint) for the server certificate (including spaces)>";

int tool_armed = 0;
int block_retry = 1;

void setup() {
  Serial.begin(115200);
  delay(10);

  // prepare STATUS_PIN
  pinMode( POWER_PIN, OUTPUT );
  pinMode( STATUS_PIN, OUTPUT );
  pinMode( ERROR_PIN, OUTPUT );
  pinMode( BUTTON_PIN, INPUT );

  // Clean up
  setOff();

  // Block trying to arm in case switch was accidentally left on
  block_retry = 1;

  Serial.printf( "LOG: Blinking ERROR_PIN\n" );
  blinkLED( ERROR_PIN );
  Serial.printf( "LOG: Operational\n" );
  blinkLED( STATUS_PIN );
}

String getDeviceStatus() {
  return doGetServerRequest( "state" );
}

String doGetServerRequest( String endpoint ) {
  String requestURL = String( (String) baseURL + endpoint + "?id=" + (String)deviceID );
  
  HTTPClient http;
  http.begin( requestURL, fingerprint );
  int httpCode = http.GET();
  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      return payload;
    } else {
      setError();
      Serial.printf( "ERROR: Invalid response code in HTTP request\n" );
    }
  } else {
    setError();
    Serial.printf( "ERROR: Unexpected error in HTTP request: %s\n", http.errorToString(httpCode).c_str() );
  }
  http.end();
  return "";
}

void loop() {
  // Get button status
  int button_status = digitalRead( BUTTON_PIN );

  // If the button is on, tool is unsafe and we're not blocked from trying, turn it on
  if ( button_status == HIGH && tool_armed == 0 && block_retry == 0 ) {
    delay( DEBOUNCE_DELAY );
    if ( button_status != HIGH ) {
      return;
    }

    // Connect to WiFi network
    Serial.printf( "LOG: Connecting to [%s]...", ssid );

    // Switch wifi on
    WiFi.mode( WIFI_STA );
    WiFi.begin( ssid, password );

    // Wait for connection
    int connection_tries = 0;
    while ( WiFi.status() != WL_CONNECTED && connection_tries < 20 ) {
      Serial.printf( "." );
      delay(250);
    }
    if( WiFi.status() != WL_CONNECTED ) {
      setError();
      Serial.printf( "\nERROR: Failed to connect to wireless network\n" );
      return;
    }
    Serial.printf( "ONLINE!\n" );

    // Wait for network to come up
    delay( 500 );

    Serial.printf( "LOG: Getting status from server\n" );
    String payload = getDeviceStatus();

    Serial.printf( "LOG: Parsing status from server\n" );
    StaticJsonBuffer<1024> jsonBuffer;
    JsonObject& root = jsonBuffer.parseObject(payload);
    if ( ! root.success() ) {
      setError();
      return;
    }

    // Get state from the data
    tool_armed = (int) root["state"]["powered"];

    // Switch on, else set error
    if ( tool_armed == 1 ) {
      setOn();
    } else {
      setError();
    }

    // Go offline again
    Serial.printf( "LOG: Going offline...\n" );
    WiFi.disconnect( true );
  }

  // If button is off and the tool was on, switch it off
  if ( button_status == LOW && tool_armed == 1 ) {
    delay( DEBOUNCE_DELAY );
    if ( button_status != LOW ) {
      return;
    }

    setOff();
  }

  // If switch is off, tool is safe and retry is blocked, clear errors
  if ( button_status == LOW && tool_armed == 0 && block_retry == 1 ) {
    delay( DEBOUNCE_DELAY );
    if ( button_status != LOW ) {
      return;
    }
    clearError();
  }

  // If switch is on, but we're blocked from retrying, set error
  if ( button_status == HIGH && tool_armed == 0 && block_retry == 1 ) {
    delay( DEBOUNCE_DELAY );
    if ( button_status != HIGH ) {
      return;
    }
    setError();
  }

  delay( 1000 );
}

void setOff() {
  digitalWrite( POWER_PIN, LOW );
  tool_armed = 0;
  digitalWrite( STATUS_PIN, LOW );
  Serial.printf( "NOTICE: Tool disabled\n" );
  clearError();
}

void setOn() {
  clearError();
  digitalWrite( STATUS_PIN, HIGH );
  digitalWrite( POWER_PIN, HIGH );
  Serial.printf( "NOTICE: Tool enabled\n" );
}

void setError() {
  blinkLED( ERROR_PIN );
  block_retry = 1;
  Serial.printf( "ERROR: Error set\n" );
  digitalWrite( ERROR_PIN, HIGH );
}

void clearError() {
  digitalWrite( ERROR_PIN, LOW );
  blinkLED( STATUS_PIN );
  block_retry = 0;
  Serial.printf( "NOTICE: Error cleared\n" );
}

void blinkLED( int PIN ) {
  for ( int i = 0 ; i < 5 ; i++ ) {
    digitalWrite( PIN, HIGH );
    delay( BLINK_DELAY );
    digitalWrite( PIN, LOW );
    delay( BLINK_DELAY );
  }
}