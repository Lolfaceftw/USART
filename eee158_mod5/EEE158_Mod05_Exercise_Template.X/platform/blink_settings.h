#ifndef BLINK_SETTINGS_H
#define BLINK_SETTINGS_H

typedef enum {
    OFF,
    SLOW,
    MEDIUM,
    FAST,
    ON,
    NUM_SETTINGS
} BlinkSetting;

extern BlinkSetting currentSetting;
extern const char *blinkSettingStrings[NUM_SETTINGS];

#endif // BLINK_SETTINGS_H