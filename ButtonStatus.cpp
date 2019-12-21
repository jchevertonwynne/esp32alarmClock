#include "ButtonStatus.h"
#include "Arduino.h"

ButtonStatus::ButtonStatus(int pin)
{
    buttonPin = pin;
    pressed = false;
}

bool ButtonStatus::status()
{
    if (digitalRead(buttonPin) == LOW && !pressed)
    {
        pressed = true;
        return true;
    }
    else
    {
        if (pressed)
        {
            pressed = false;
        } 
    }
    return false;
}