#include <Arduino.h>
#include <BlynkSimpleEsp8266.h>
#include <ESP8266WiFi.h>
#include <Ticker.h>
#include <WidgetRTC.h>

#include "privates.h"
#include "typedefs.h"
#include "OTA_updater.h"
#include "command_parser.h"

#define LED D2

#define PWM_FREQUENCY 1000   // 1 kHz
#define PWM_RESOLUTION 1023  // 10 bit
#define SAMPLE_INTERVAL 100  // 100 milliseconds
#define SAMPLE_COUNT 8       // 32 samples
#define LOOP_INTERVAL 10000   // 10 seconds

std::map<int, String> weekdays = {
    {1, "Monday"},
    {2, "Tuesday"},
    {3, "Wednesday"},
    {4, "Thursday"},
    {5, "Friday"},
    {6, "Saturday"},
    {7, "Sunday"}
};

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
#ifdef DEBUG_TERMINAL
WidgetTerminal terminal(DEBUG_TERMINAL);
#elif DEBUG_BRIDGE
BridgeTerminal bridge(DEBUG_BRIDGE);
#endif

// Modifying lib to avoid linker errors (multiple file projects)
static WiFiClient _blynkWifiClient;
static BlynkArduinoClient _blynkTransport(_blynkWifiClient);
BlynkWifi Blynk(_blynkTransport);

// lib instances
WidgetRTC rtc;
Ticker updater;

// Global vars
int speed = 0;
unsigned int avg_current = 0;
int state = 0;

int day_of_week;
int cur_day;
int cur_month;
int cur_year;
char Date[16];
char Time[16];
char daysstring[128];           // string to hold days of week
long startsecondswd;            // weekday start time in seconds
long stopsecondswd;             // weekday stop  time in seconds
long nowseconds;                // time now in seconds

// Rather then using local EEPROM (hence slowly degrading it), use syncVirtual for brightness (reads last known values from server)
//static void storeBrightness(void);

static void setLED(int);
static void measure_ADC(void);
static void time_loop(void);

void setup() {
    setLED(0);

    analogWriteFreq(PWM_FREQUENCY);
    analogWriteRange(PWM_RESOLUTION);

    // Debug console
    //Serial.begin(115200);
    pinMode(LED, OUTPUT);  //Set the LED (D2) as an output
    
    // Problematic since there may be multiple devices with the same name
    // Add serial number to hostname
    WiFi.hostname(String(name) + String("_") + String(ESP.getChipId()));

    Blynk.begin(auth, ssid, pass, HOSTNAME, 8080);

    //updateVirtualPins();

    rtc.begin();

    Blynk.syncVirtual(V3);
    Blynk.syncVirtual(V2);
    Blynk.syncVirtual(V0);
}

BLYNK_CONNECTED() {
#ifdef DEBUG_BRIDGE
    bridge.setAuthToken(remoteAuth);
#endif
    INFO_PRINT("Just connected.\n");
    DEBUG_PRINT("Debug mode is on which is why I will spam here :-)\n\n");

    // OTA Server update controller
    checkForUpdates();


    // Don't do this when updates are found
    updater.once_ms(SAMPLE_INTERVAL, measure_ADC);
    updater.once_ms(LOOP_INTERVAL, time_loop);
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

BLYNK_WRITE(V2) {  // Time Input as Schedule see here: https://community.blynk.cc/t/automatic-scheduler-esp-01-with-4-time-input-widgets/10658
    static bool active = false;
    TimeInputParam t(param);

    if(cur_day != day() || cur_month != month() || cur_year != year()) {
        day_of_week = weekday() == 1?7:weekday() - 1;
        cur_day = day();
        cur_month = month();
        cur_year = year();
        
        sprintf(Date, "%02d/%02d/%04d",  cur_day, cur_month, cur_year);
    }

    //DEBUG_PRINTF("Checking schedule for %s %s:", weekdays[weekday() + dayadjustment].c_str(), Date);
    if(t.isWeekdaySelected(day_of_week)) { //Time library starts week on Sunday, Blynk on Monday
        //DEBUG_PRINTLN("\tACTIVE today!");
        /*if (t.hasStartTime()) { // Process start time
            DEBUG_PRINTF("\tStart: %02d:%02d:%02d", t.getStartHour(), t.getStartMinute(), t.getStartSecond());
        }
        if (t.hasStopTime()) { // Process stop time
            DEBUG_PRINTF("\tStop: %02d:%02d:%02d", t.getStopHour(), t.getStopMinute(), t.getStopSecond());
        }*/

        // DEBUG_PRINTLN(String("Time zone offset: ") + t.getTZ_Offset()); // Get timezone offset (in seconds)

        /*DEBUG_PRINTLN("Days of week:");
        daysstring[0] = '\0';  // clear the string
        for (int i = 1; i <= 7; i++) {  // Process weekdays (1. Mon, 2. Tue, 3. Wed, ...)
            if (t.isWeekdaySelected(i)) {
                sprintf(daysstring, "%s%s, ", daysstring, weekdays[i].c_str());
            }
        } 
        DEBUG_PRINTLN(daysstring);*/
        nowseconds = ((hour() * 3600) + (minute() * 60) + second());
        startsecondswd = (t.getStartHour() * 3600) + (t.getStartMinute() * 60) + t.getStartSecond();
        stopsecondswd = (t.getStopHour() * 3600) + (t.getStopMinute() * 60) + t.getStopSecond();
        BlynkTime now = BlynkTime(nowseconds);
        if(nowseconds >= startsecondswd && nowseconds < stopsecondswd) {
            if(!active) {
                active = true;
                DEBUG_PRINTF("\tSTARTED: %s %s at %02d:%02d:%02d", weekdays[day_of_week].c_str(), Date, now.hour(), now.minute(), now.second());
                setLED(1);
            } /*else {
                BlynkTime stopping = BlynkTime(stopsecondswd - nowseconds);
                DEBUG_PRINTF("\tStopping in %d hours %d minutes and %d seconds.", stopping.hour(), stopping.minute(), stopping.second());
            }*/
        } else if(active && nowseconds > stopsecondswd) {
            active = false;
            DEBUG_PRINTF("\tSTOPPED: %s %s at %02d:%02d:%02d", weekdays[day_of_week].c_str(), Date, now.hour(), now.minute(), now.second());
            setLED(0);          
        } /*else if(nowseconds < startsecondswd) {
            BlynkTime running_before = BlynkTime(startsecondswd - nowseconds);
            DEBUG_PRINTF("\tRunning in %d hours %d minutes and %d seconds.", running_before.hour(), running_before.minute(), running_before.second());
        } else {
            BlynkTime running_after = BlynkTime(nowseconds - stopsecondswd);
            DEBUG_PRINTF("\tRan %d hours %d minutes and %d seconds ago.", running_after.hour(), running_after.minute(), running_after.second());
        }*/
    } else {
        //DEBUG_PRINTLN("\tINACTIVE today");
    }
}

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

    updater.once_ms(SAMPLE_INTERVAL, measure_ADC);
}

static void time_loop() {        // check if schedule should run today
    if(year() != 1970){
        Blynk.syncVirtual(V2);
    }
    
    updater.once_ms(LOOP_INTERVAL, time_loop);
}
