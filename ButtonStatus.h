#ifndef BUTTONSTATUS_H
#define BUTTONSTATUS_H

class ButtonStatus
{
private:
    bool pressed;
    int buttonPin;

public:
    ButtonStatus(int);
    bool status();
};

#endif