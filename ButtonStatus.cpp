#include "ButtonStatus.h"
#include "Arduino.h"

ButtonStatus::ButtonStatus(int pin)
{
    buttonPin = pin;
    pressed = false;
}

// button must be unpressed and checked before it can return as pressed again
// this stops the endless toggling that could otherwise happen by just
// checking for LOW
bool ButtonStatus::status()
{
    if (digitalRead(buttonPin) == LOW && !pressed)
    {
        pressed = true;
        return true;
    }
    else if (pressed)
    {
        pressed = false;
    }
    return false;
}