#include "ButtonStatus.h"
#include <ESPAsyncWebServer.h>
#include "private.h"
#include "time.h"
#include <WiFi.h>

const int PIN_BUZZER = 12;
const int PIN_BUTTON = 27;
bool buzz_status = false;
ButtonStatus button(PIN_BUTTON);

AsyncWebServer *webServer;
String header;

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 3600;

struct tm currentTime;

struct Time
{
    int hour;
    int minute;
    bool complete;
};

std::vector<Time> alarms;

bool anyAlarms();

void handleRoot(AsyncWebServerRequest *request);
void handleAdd(AsyncWebServerRequest *request);
void handleDelete(AsyncWebServerRequest *request);

void setup()
{
    Serial.begin(115200);
    pinMode(PIN_BUZZER, OUTPUT);
    pinMode(PIN_BUTTON, INPUT_PULLUP);

    Serial.printf("Connecting to network %s\n", wifiSSID);
    WiFi.begin(wifiSSID, wifiPassword);
    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
        Serial.println("WiFi could not be connected?");
        while (true) {}
    }
    Serial.println("Connected to WiFi!");

    Serial.println("Setting time...");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    Serial.println("Time set!");

    Serial.println("Hosting on: ");
    Serial.println(WiFi.localIP());

    Time test1 = {22, 9, false};
    Time test2 = {22, 10, false};
    Time test3 = {22, 11, false};
    alarms.push_back(test1);
    alarms.push_back(test2);
    alarms.push_back(test3);

    webServer = new AsyncWebServer(80);
    webServer->on("/", handleRoot);
    webServer->on("/add", handleAdd);
    webServer->on("/delete", handleDelete);
    webServer->begin();
}

void loop()
{
    getLocalTime(&currentTime);
    Serial.printf("%d:%d:%d\n", currentTime.tm_hour, currentTime.tm_min, currentTime.tm_sec);

    if (anyAlarms())
    {
        Serial.println("Valid alarm(s) detected!");
        digitalWrite(PIN_BUZZER, HIGH);
        int loop = 0;
        while (!button.status())
        {
            if (loop % 80 == 0)
            {
                digitalWrite(PIN_BUZZER, HIGH);
            }
            else if (loop % 80 == 70)
            {
                digitalWrite(PIN_BUZZER, LOW);
            }
            loop++;
            delay(5);
        }
        digitalWrite(PIN_BUZZER, LOW);
        Serial.println("Alarm disabled!");
    }

    delay(1000);
}

void handleRoot(AsyncWebServerRequest *request)
{
    String page = "";
    if (request->hasHeader("error_message"))
    {
        page += "Error: ";
        page += request->getHeader("error_message")->value();
    }
    if (request->hasHeader("success_message"))
    {
        page += "Success: ";
        page += request->getHeader("success_message")->value();
    }
    if (page == "")
    {
        page = "Root page";
    }
    request->send(200, "text/html", page);
}

void handleAdd(AsyncWebServerRequest *request)
{
    Serial.println("New alarm request");

    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "Hello World!");
    if (!(request->hasParam("hours") && request->hasParam("minutes")))
    {
        response->addHeader("error_message", "Hours or minutes was not set");
        request->redirect("/");
    }

    int hours;
    int minutes;

    try 
    {
        hours = atoi(request->getParam("hours")->value().c_str());
        minutes = atoi(request->getParam("minutes")->value().c_str());
    }
    catch (std::invalid_argument const &e)
    {
        response->addHeader("error_message", "Hours and/or minutes was a non integer value");
        request->redirect("/");
    }
    catch (std::out_of_range const &e)
    {
        response->addHeader("error_message", "Please enter proper values");
        request->redirect("/");
    }

    if (hours < 0 || hours > 23)
    {
        response->addHeader("error_message", "Hours must be between 0-23");
        request->redirect("/");
    }

    if (minutes < 0 || minutes > 59)
    {
        response->addHeader("error_message", "Minutes must be between 0-59");
        request->redirect("/");
    }

    bool alreadySet = false;
    for (int i = 0; i < alarms.size(); i++) {
        Time alarm = alarms[i];
        if (alarm.hour == hours && alarm.minute == minutes)
        {
            response->addHeader("error_message", "This alarm already exists");
            request->redirect("/");
        }
    }

    Serial.println("Valid alarm, adding");

    Time newAlarm = {hours, minutes, false};
    alarms.push_back(newAlarm);

    response->addHeader("success_message", "New alarm has been set");
    request->redirect("/");
}

void handleDelete(AsyncWebServerRequest *request)
{
    request->redirect("/");
}

bool anyAlarms() 
{
    bool result = false;
    for (int i = 0; i < alarms.size(); i++)
    {
        if (alarms[i].hour == currentTime.tm_hour && alarms[i].minute == currentTime.tm_min && !alarms[i].complete)
        {
            alarms[i].complete = true;
            result = true;
        }
    }
    return result;
}
