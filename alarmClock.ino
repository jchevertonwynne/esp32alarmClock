#include "ButtonStatus.h"
#include "EEPROM.h"
#include <ESPAsyncWebServer.h>
#include "private.h"
#include <string>
#include "time.h"
#include <WiFi.h>

const int PIN_BUZZER = 15;
const int PIN_BUTTON = 13;
const int LED = 32;
const int MAX_ALARMS = 250;
ButtonStatus button(PIN_BUTTON);

AsyncWebServer *webServer;

SemaphoreHandle_t semaphore;
TaskHandle_t Task1;

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 3600;

struct tm currentTime;
struct tm lastTime;

struct AlarmEntry
{
    int hour;
    int minute;
    bool complete;
};

bool compareTimes(AlarmEntry a, AlarmEntry b) {
    return a.hour == b.hour ? a.minute < b.minute : a.hour < b.hour;
}

std::vector<AlarmEntry> alarms;

void handleWifiConnection();

bool anyAlarmsDueNow();
void loadAlarms();
void saveAlarms();

void initServer();
void handleRoot(AsyncWebServerRequest *request);
void handleAdd(AsyncWebServerRequest *request);
void handleDelete(AsyncWebServerRequest *request);
void handleBeepNow(AsyncWebServerRequest *request);

bool validDeletion(AsyncWebServerRequest *request);
bool validNewAlarm(AsyncWebServerRequest *request);

void beepAlarm();

void setup()
{
    Serial.begin(115200);
    EEPROM.begin(512);
    pinMode(PIN_BUZZER, OUTPUT);
    pinMode(PIN_BUTTON, INPUT_PULLUP);
    pinMode(LED, OUTPUT);

    handleWifiConnection();

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
        0);          /* Core where the task should run */
}

void loop()
{
    handleWifiConnection();

    getLocalTime(&currentTime);

    // at midnight enable all alarms for the new day
    if (currentTime.tm_hour == 0 && currentTime.tm_min == 0 && lastTime.tm_hour == 23 && lastTime.tm_min == 59){
        Serial.println("Just past midnight, enabling all alarms...");
        for (int i = 0; i < alarms.size(); i++) 
        {
            alarms[i].complete = false;
        }
        Serial.println("Alarms enabled!");
    }

    Serial.printf("%02d:%02d:%02d\n", currentTime.tm_hour, currentTime.tm_min, currentTime.tm_sec);

    if (anyAlarmsDueNow())
    {
        Serial.println("Valid alarm(s) detected!");
        xSemaphoreGive(semaphore);
        Serial.println("Alarm disabled!");
    }
    lastTime = currentTime;
    delay(1000);
}

void handleWifiConnection()
{
    uint8_t status = WiFi.status();

    if (status != WL_CONNECTED)
    {
        digitalWrite(LED, HIGH);
        Serial.printf("Connecting to network %s...\n", wifiSSID);
        
        while (status != WL_CONNECTED)
        {
            WiFi.disconnect(true);
            WiFi.begin(wifiSSID, wifiPassword);
            status = WiFi.waitForConnectResult();
        }

        Serial.println("Connected to WiFi!");
        digitalWrite(LED, LOW);
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
    webServer->onNotFound(handleRoot);
    webServer->begin();

    Serial.println("Hosting on: ");
    Serial.println(WiFi.localIP());
}

void handleRoot(AsyncWebServerRequest *request)
{
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

        sprintf(buffer, "%02d:%02d", alarm.hour, alarm.minute);
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
    page += "Hours: <input type=\"text\" name=\"hours\">";
    page += "Minutes: <input type=\"text\" name=\"minutes\">";
    page += "<br><br>";
    page += "<input type=\"submit\" value=\"Submit\">";
    page += "</form>";

    page += "<a href=\"/beepNow\">Beep right now</a>";

    page += "<br>";

    page += "<a href=\"/add?hours=";
    itoa(currentTime.tm_hour + 1 % 24, buffer, 10);
    page += buffer;
    page += "&minutes=";
    itoa(currentTime.tm_min, buffer, 10);
    page += buffer;
    page += "\">Set alarm for in one hour</a>";

    page += "</body>";

    page += "</html>";

    request->send(200, "text/html", page);
    Serial.println("Homepage handled!");
}

void handleAdd(AsyncWebServerRequest *request)
{
    Serial.println("New alarm request");

    int hours;
    int minutes;

    if (validNewAlarm(request, &hours, &minutes))
    {
        Serial.println("Valid alarm, adding");

        AlarmEntry newAlarm = {hours, minutes, false};
        alarms.push_back(newAlarm);
        sort(alarms.begin(), alarms.end(), compareTimes);

        Serial.printf("New alarm added at %d:%d\n", hours, minutes);

        saveAlarms();
    }

    request->redirect("/");
    Serial.println("New alarm handled!");
}

void handleDelete(AsyncWebServerRequest *request)
{
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

void handleBeepNow(AsyncWebServerRequest *request)
{
    Serial.println("Beep now request");
    xSemaphoreGive(semaphore);
    request->redirect("/");
}

bool anyAlarmsDueNow() 
{
    bool result = false;
    for (int i = 0; i < alarms.size(); i++)
    {
        AlarmEntry* alarm = &alarms[i];
        if (alarm->hour == currentTime.tm_hour && alarm->minute == currentTime.tm_min && !alarm->complete)
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
        AlarmEntry storedAlarm = {hours, minutes, false};
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
        EEPROM.write(1 + i * 2, alarm->hour);
        EEPROM.write(2 + i * 2, alarm->minute);
    }
    EEPROM.commit();
    Serial.println("Alarms saved!");
}

bool validNumber(AsyncWebServerRequest *request, int *dest, char* fieldName)
{
    if (!request->hasParam(fieldName))
    {
        return false;
    }

    try {
        *dest = atoi(request->getParam(fieldName)->value().c_str());
    }
    catch (std::invalid_argument const &e)
    {
        return false;
    }
    catch (std::out_of_range const &e)
    {
        return false;
    }

    return true;
}

bool validTime(AsyncWebServerRequest *request, int *hours, int *minutes)
{
    if (!validNumber(request, hours, "hours") || !validNumber(request, minutes, "minutes"))
    {
        return false;
    }

    if (*minutes < 0 || *minutes > 59)
    {
        return false;
    }

    return true;
}

bool validNewAlarm(AsyncWebServerRequest *request, int *hours, int *minutes)
{
    if (alarms.size() == MAX_ALARMS)
    {
        return false;
    }

    if (!validTime(request, hours, minutes)) 
    {
        return false;
    }

    for (int i = 0; i < alarms.size(); i++)
    {
        AlarmEntry alarm = alarms[i];
        if (alarm.hour == *hours && alarm.minute == *minutes)
        {
            return false;
        }
    }

    return true;
}

bool validDeletion(AsyncWebServerRequest *request, int *alarmNumber)
{
    if (!validNumber(request, alarmNumber, "alarmNumber"))
    {
        return false;
    }

    if (*alarmNumber < 0 || *alarmNumber >= alarms.size())
    {
        return false;
    }

    return true;
}

void beepAlarm(void *parameter)
{
    while (true)
    {
        xSemaphoreTake(semaphore, portMAX_DELAY);

        digitalWrite(PIN_BUZZER, HIGH);
        digitalWrite(LED, HIGH);
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
        digitalWrite(LED, LOW);
    }
}