#include <Arduino.h>
#include <BlynkSimpleEsp8266.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>

#include "OTA_updater.h"
#include "privates.h"
#include "typedefs.h"

#define LED D2

#define PWM_FREQUENCY 1000   // 1 kHz
#define PWM_RESOLUTION 1023  // 10 bit
#define SAMPLE_INTERVAL 100  // 100 milliseconds
#define SAMPLE_COUNT 8       // 32 samples

// extern in privates.h
DEVICE_INFO_T device_info;
const char name[] = DEVICE_NAME;
const char auth[] = AUTH_TOKEN;
const char version[] = VERSION;
char hostname[] = HOSTNAME;
char ssid[] = SSID;
char pass[] = WIFI_PASS;
char remoteAuth[] = REMOTE_AUTH;

// extern in typedefs.h
#ifdef ERROR_TERMINAL
WidgetTerminal terminal(DEBUG_TERMINAL);
#elif ERROR_BRIDGE
BridgeTerminal bridge(DEBUG_BRIDGE);
#endif

// Modifying lib to avoid linker errors (multiple file projects)
static WiFiClient _blynkWifiClient;
static BlynkArduinoClient _blynkTransport(_blynkWifiClient);
BlynkWifi Blynk(_blynkTransport);

// lib instances
Ticker updater;

// Global vars
int speed = 0;
unsigned int avg_current = 0;
int state = 0;
char *timerParam = nullptr;

char Date[16];
char Time[16];

// Rather then using local EEPROM (hence slowly degrading it), use syncVirtual for brightness (reads last known values from server)
//static void storeBrightness(void);

static void setLED(int);
static void measure_ADC(void);

void setup() {
    setLED(0);

    analogWriteFreq(PWM_FREQUENCY);
    analogWriteRange(PWM_RESOLUTION);

    // Debug console
    //Serial.begin(115200);
    pinMode(LED, OUTPUT);  //Set the LED (D2) as an output
    Blynk.begin(auth, ssid, pass, HOSTNAME, 8080);

    //updateVirtualPins();

    Blynk.syncVirtual(V3);
    Blynk.syncVirtual(V0);
}

BLYNK_CONNECTED() {
    bridge.setAuthToken(remoteAuth);
    INFO_PRINT("Just connected.\n");
    DEBUG_PRINT("Debug mode is on which is why I will spam here :-)\n\n");

    updater.attach_ms(SAMPLE_INTERVAL, measure_ADC);
    // OTA Server update controller
    checkForUpdates();
}

void loop() {
    Blynk.run();
}

BLYNK_WRITE(V0) {
    speed = (int)((float)SPEED_MULTIPLIER * param.asInt());
    if (state == 1) {
        analogWrite(LED, speed);  // Turn LED on.
    } else {
        digitalWrite(LED, LOW);  // Turn LED off.
    }
}

BLYNK_WRITE(V3) {
    setLED(param.asInt());
}

/*BLYNK_WRITE(V4) {  // Time Input as Schedule see here: https://community.blynk.cc/t/automatic-scheduler-esp-01-with-4-time-input-widgets/10658
    size_t len = param.getLength();
    timerParam = new char[len];
    strncpy(timerParam, (char*)param.getBuffer(), len);
    
    Serial.print("\nBuffer:\n");
    for(int i = 0;  i < len; i++)
        Serial.printf("%x", timerParam[i]);
    //setLED(param.asInt());
}*/

static void setLED(int value) {
    state = value;
    if (state == 1) {
        analogWrite(LED, speed);
    } else {
        digitalWrite(LED, LOW);
    }
}

static void measure_ADC() {
    static int n_samples = 0;
    avg_current = (avg_current * (SAMPLE_COUNT - 1) + analogRead(A0)) / SAMPLE_COUNT;

    if (n_samples++ >= SAMPLE_COUNT) {
        n_samples = 0;
        // Fix Android scaling bug
        Blynk.virtualWrite(V1, (int)(avg_current * ((float)1500 / 1023)));
        Blynk.virtualWrite(V2, state ? ((MAX_POWERCORRECTION_PERCENT * avg_current) / 1023) : 0);
    }
}
