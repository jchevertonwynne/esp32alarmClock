const int PIN_BUZZER = 12;
const int PIN_BUTTON = 27;
bool pressed = false;

void setup()
{
    Serial.begin(115200);
    pinMode(PIN_BUZZER, OUTPUT);
    pinMode(PIN_BUTTON, INPUT_PULLUP);
}

void loop() 
{

    digitalWrite(PIN_BUZZER, HIGH);
    delay(1000);
    digitalWrite(PIN_BUZZER, LOW);
    delay(1000);
}