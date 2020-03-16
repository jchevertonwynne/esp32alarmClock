#include "ButtonStatus.h"
#include "EEPROM.h"
#include <ESPAsyncWebServer.h>
#include "private.h"
#include <string>
#include "time.h"
#include <WiFi.h>

const int PIN_BUZZER = 15;
const int PIN_BUTTON = 13;
const int LED_RED = 32;
const int LED_GREEN = 33;
const int MAX_ALARMS = 250;

const int TIME_TO_SLEEP = 120;

struct Time;
struct AlarmEntry;

void monitorWifiConnection();

bool anyAlarmsDueNow();
void loadAlarms();
void saveAlarms();

void initServer();
void handleRoot(AsyncWebServerRequest * request);
void handleAdd(AsyncWebServerRequest * request);
void handleDelete(AsyncWebServerRequest * request);
void handleBeepNow(AsyncWebServerRequest * request);
void handleSleep(AsyncWebServerRequest * request);

bool validDeletion(AsyncWebServerRequest * request);
bool validNewAlarm(AsyncWebServerRequest * request);

void beepAlarm();

Time nextAlarm();
Time timeToAlarm(Time other);
int timeToSeconds(Time time);
String timeToString(Time time);

ButtonStatus button(PIN_BUTTON);

int lastInteraction = 0;

AsyncWebServer *webServer;

SemaphoreHandle_t semaphore;
TaskHandle_t Task1;

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 0;

struct tm currentTime;
struct tm lastTime;

void setup()
{
    Serial.begin(115200);
    EEPROM.begin(512);
    pinMode(PIN_BUZZER, OUTPUT);
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    pinMode(LED_RED, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);

    digitalWrite(LED_GREEN, HIGH);
    monitorWifiConnection();

    Serial.println("Setting time...");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    Serial.println("Time set!");

    loadAlarms();
    initServer();

    semaphore = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(
        beepAlarm,   /* Function to implement the task */
        "beepAlarm", /* Name of the task */
        10000,       /* Stack size in words */
        NULL,        /* Task input parameter */
        0,           /* Priority of the task */
        &Task1,      /* Task handle. */
        0            /* Core where the task should run */
    );
}

void loop()
{
    if (++lastInteraction > TIME_TO_SLEEP)
        goToSleep();
    monitorWifiConnection();
    getLocalTime(&currentTime);
    if (currentTime.tm_hour == 0 && currentTime.tm_min == 0 && lastTime.tm_hour == 23 && lastTime.tm_min == 59)
        enableAlarms();
    Serial.printf("%02d:%02d:%02d\n", currentTime.tm_hour, currentTime.tm_min, currentTime.tm_sec);
    if (anyAlarmsDueNow())
        xSemaphoreGive(semaphore);
    lastTime = currentTime;
    delay(1000);
}

struct Time
{
    int hour;
    int minute;
};

struct AlarmEntry
{
    Time time;
    bool complete;
};

bool compareAlarms(AlarmEntry a, AlarmEntry b)
{
    Time aTime = a.time;
    Time bTime = b.time;

    return compareTimes(aTime, bTime);
}

bool compareTimes(Time a, Time b)
{
    return a.hour == b.hour ? a.minute < b.minute : a.hour < b.hour;
}

std::vector<AlarmEntry> alarms;

Time nextAlarm()
{
    Time now = {
        currentTime.tm_hour,
        currentTime.tm_min
    };
    for (int i = 0; i < alarms.size(); i++) 
    {
        Time alarmTime = alarms[i].time;
        if (compareTimes(now, alarmTime)) 
            return alarmTime;
    }
    return alarms[0].time;
}

String timeToString(Time time) {
    char buffer[10];
    sprintf(buffer, "%02d:%02d", time.hour, time.minute);
    String result = "";
    result += buffer;
    return result;
}

Time timeToAlarm(Time other)
{
    Time now = { currentTime.tm_hour, currentTime.tm_min };
    int hourDiff;
    int minuteDiff;
    if (compareTimes(now, other))
    {
        hourDiff = other.hour - now.hour;
        minuteDiff = other.minute - now.minute - 1;
    }
    else
    {
        hourDiff = 23 - (now.hour - other.hour);
        minuteDiff = 59 - (now.minute - other.minute);
    }
    if (minuteDiff < 0)
    {
        hourDiff--;
        minuteDiff += 60;
    }
    return {
        hourDiff,
        minuteDiff
    };
}

int timeToSeconds(Time time) {
    return time.hour * 3600 + time.minute * 60;
}

void goToSleep()
{
    if (alarms.size() > 0)
    {
        Time next = nextAlarm();
        Time timeToNext = timeToAlarm(next);
        int secondsToAlarm = timeToSeconds(timeToNext);
        uint64_t mul = 1000000LL;
        uint64_t sleepTime = secondsToAlarm * mul;
        esp_sleep_enable_timer_wakeup(sleepTime);
        Serial.printf("Sleeping for %s (%d seconds) until %s\n", timeToString(timeToNext), secondsToAlarm, timeToString(next));
        Serial.printf("%u ns\n", sleepTime);
    }
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_13, 0);
    Serial.println("Sleeping alarm...");
    esp_deep_sleep_start();
}

void enableAlarms()
{
    Serial.println("Just past midnight, enabling all alarms...");
    for (int i = 0; i < alarms.size(); i++) 
        alarms[i].complete = false;
    Serial.println("Alarms enabled!");
}

void  monitorWifiConnection()
{
    uint8_t status = WiFi.status();

    if (status != WL_CONNECTED)
    {
        digitalWrite(LED_RED, HIGH);
        Serial.printf("Connecting to network %s...\n", wifiSSID);

        while (status != WL_CONNECTED)
        {
            WiFi.disconnect(true);
            WiFi.begin(wifiSSID, wifiPassword);
            status = WiFi.waitForConnectResult();
        }

        Serial.println("Connected to WiFi!");
        digitalWrite(LED_RED, LOW);
    }
}

void initServer()
{
    Serial.println("Setting up server...");
    webServer = new AsyncWebServer(80);
    webServer->on("/", handleRoot);
    webServer->on("/add", handleAdd);
    webServer->on("/delete", handleDelete);
    webServer->on("/beepNow", handleBeepNow);
    webServer->on("/sleep", handleSleep);

    webServer->onNotFound(handleRoot);
    webServer->begin();

    Serial.println("Hosting on: ");
    Serial.println(WiFi.localIP());
}

void handleRoot(AsyncWebServerRequest * request)
{
    lastInteraction = 0;
    Serial.println("Homepage requested...");
    char buffer[10];
    String page = "";
    page += "<!DOCTYPE html>";
    page += "<html>";

    page += "<head>";
    page += "<title>Alarms</title>";
    page += "</head>";

    page += "<body>";

    page += "<h1> Current Time: ";
    sprintf(buffer, "%02d:%02d", currentTime.tm_hour, currentTime.tm_min);
    page += buffer;
    page += "</h1>";

    page += "<h1> Active alarms </h1>";
    page += "<ol start=\"0\">";
    for (int i = 0; i < alarms.size(); i++)
    {
        AlarmEntry alarm = alarms[i];

        sprintf(buffer, "%02d:%02d", alarm.time.hour, alarm.time.minute);
        page += "<li>";
        page += buffer;
        page += "<a href=\"delete?alarmNumber=";
        sprintf(buffer, "%d", i);
        page += buffer;
        page += "\">Delete</a>";
        page += "</li>";
    }
    page += "</ol>";

    page += "<form action=\"/add\">";
    page += "Hours: <input type=\"number\" name=\"hours\" max=\"23\">";
    page += "Minutes: <input type=\"number\" name=\"minutes\" max=\"59\">";
    page += "<br><br>";
    page += "<input type=\"submit\" value=\"Submit\">";
    page += "</form>";

    page += "<a href=\"/beepNow\">Beep right now</a>";
    page += "<br>";
    page += "<a href=\"/sleep\">Sleep</a>";

    page += "</body>";

    page += "</html>";

    request->send(200, "text/html", page);
    Serial.println("Homepage handled!");
}

void handleAdd(AsyncWebServerRequest * request)
{
    lastInteraction = 0;
    Serial.println("New alarm request");

    int hours;
    int minutes;

    if (validNewAlarm(request, &hours, &minutes))
    {
        Serial.println("Valid alarm, adding");

        AlarmEntry newAlarm = {{ hours, minutes }, false };
        alarms.push_back(newAlarm);
        sort(alarms.begin(), alarms.end(), compareAlarms);

        Serial.printf("New alarm added at %d:%d\n", hours, minutes);

        saveAlarms();
    }

    request->redirect("/");
    Serial.println("New alarm handled!");
}

void handleDelete(AsyncWebServerRequest * request)
{
    lastInteraction = 0;
    Serial.println("New delete request");

    int alarmNumber;

    if (validDeletion(request, &alarmNumber))
    {
        Serial.println("Valid alarm removal request. Removing...");
        alarms.erase(alarms.begin() + alarmNumber);
        saveAlarms();
        Serial.println("Alarm removed!");
    }

    request->redirect("/");
    Serial.println("Delete request handled!");
}

void handleBeepNow(AsyncWebServerRequest * request)
{
    lastInteraction = 0;
    Serial.println("Beep now request");
    xSemaphoreGive(semaphore);
    request->redirect("/");
}

void handleSleep(AsyncWebServerRequest *request)
{
    request->redirect("https://github.com/jchevertonwynne/esp32alarmClock");
    goToSleep();
}

bool anyAlarmsDueNow()
{
    bool result = false;
    for (int i = 0; i < alarms.size(); i++)
    {
        AlarmEntry *alarm = &alarms[i];
        if (alarm->time.hour == currentTime.tm_hour && alarm->time.minute == currentTime.tm_min && !alarm->complete)
        {
            alarm->complete = true;
            result = true;
        }
    }
    return result;
}

void loadAlarms()
{
    Serial.println("Loading alarms...");
    int alarmCount = EEPROM.read(0);
    for (int i = 0; i < alarmCount; i++)
    {
        int hours = EEPROM.read(1 + i * 2);
        int minutes = EEPROM.read(2 + i * 2);
        AlarmEntry storedAlarm = {{hours, minutes}, false};
        alarms.push_back(storedAlarm);
    }
    Serial.println("Alarms loaded");
}

void saveAlarms()
{
    Serial.println("Saving alarms...");
    EEPROM.write(0, alarms.size());
    for (int i = 0; i < alarms.size(); i++)
    {
        AlarmEntry *alarm = &alarms[i];
        EEPROM.write(1 + i * 2, alarm->time.hour);
        EEPROM.write(2 + i * 2, alarm->time.minute);
    }
    EEPROM.commit();
    Serial.println("Alarms saved!");
}

bool validNumber(AsyncWebServerRequest * request, int *dest, char *fieldName)
{
    if (!request->hasParam(fieldName)) 
        return false;
    try {
        *dest = atoi(request->getParam(fieldName)->value().c_str());
    }
    catch (std::invalid_argument const &e) 
    {
        return false;
    }
    catch (std::out_of_range const &e) {
        return false;
    }
    return true;
}

bool validTime(AsyncWebServerRequest *request, int *hours, int *minutes)
{
    if (!validNumber(request, hours, "hours") || !validNumber(request, minutes, "minutes")) 
        return false;
    if (*minutes < 0 || *minutes > 59) 
        return false;
    return true;
}

bool validNewAlarm(AsyncWebServerRequest *request, int *hours, int *minutes)
{
    if (alarms.size() == MAX_ALARMS) 
        return false;
    if (!validTime(request, hours, minutes)) 
        return false;
    for (int i = 0; i < alarms.size(); i++)
    {
        AlarmEntry alarm = alarms[i];
        if (alarm.time.hour == *hours && alarm.time.minute == *minutes) 
            return false;
    }
    return true;
}

bool validDeletion(AsyncWebServerRequest *request, int *alarmNumber)
{
    if (!validNumber(request, alarmNumber, "alarmNumber")) 
        return false;
    if (*alarmNumber < 0 || *alarmNumber >= alarms.size()) 
        return false;
    return true;
}

void beepAlarm(void *parameter)
{
    while (true)
    {
        xSemaphoreTake(semaphore, portMAX_DELAY);
        Serial.println("Alarm enabled!");
        digitalWrite(PIN_BUZZER, HIGH);
        digitalWrite(LED_RED, HIGH);
        int repeats = 0;
        int loop = 0;
        while (!button.status() && repeats < 60)
        {
            int step = loop % 100;
            if (step == 0 || step == 50)
            {
                digitalWrite(PIN_BUZZER, HIGH);
                repeats++;
            }
            else if (step == 40 || step == 90)
            {
                digitalWrite(PIN_BUZZER, LOW);
            }
            loop++;
            delay(5);
        }
        digitalWrite(PIN_BUZZER, LOW);
        digitalWrite(LED_RED, LOW);
        Serial.println("Alarm disabled!");
    }
}