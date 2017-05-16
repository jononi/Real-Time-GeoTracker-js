// This #include statement was automatically added by the Particle IDE.
#include <FastLED.h>

FASTLED_USING_NAMESPACE;
#define PARTICLE_NO_ARDUINO_COMPATIBILITY 1
#include "Particle.h"

#define NUM_LEDS 60

CRGB leds[NUM_LEDS];


int _lightLevel = 100;

int _hue = 260;
int _saturation = 255;
int _brightness = 255;
int _adjustedBrightness = 100;


void setup() { 
    FastLED.addLeds<WS2812, 6, GRB>(leds, NUM_LEDS);
    
    Particle.function("setLight", setLight);
    Particle.variable("lightLevel", _lightLevel);
    Particle.variable("hue",_hue);
    Particle.variable("saturation",_saturation);
    Particle.variable("brightness",_brightness);
    Particle.variable("aBrightness",_adjustedBrightness);
    updateLights();
}

void loop() { 
    
   
    
}

void updateLights() {
    
    if (_lightLevel < 10 and _lightLevel > 0) {
            _lightLevel = 10;
    }
    
    
    _adjustedBrightness = _brightness * _lightLevel / 100;
    
    // HSV (Spectrum) to RGB color conversion
    CHSV hsv( _hue, _saturation, _adjustedBrightness); // pure blue in HSV Spectrum space
    CRGB rgb;
    hsv2rgb_spectrum( hsv, rgb);
    
    
    for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CRGB(rgb);//CHSV( _hue, _saturation, _adjustedBrightness);
    }
    /*
    leds[0] = CRGB::Black;
    leds[1] = CRGB::Red;
    leds[2] = CRGB::Green;
    leds[3] = CRGB::Blue;
    leds[4] = CRGB::Blue;
    leds[5] = CRGB::Black;
    */
    
    FastLED.show();
}


int setLight(String level) {
    _lightLevel = translateLevel(level, _lightLevel);
    updateLights();
}


int stoi(String number) {
    char inputStr[64];
    number.toCharArray(inputStr,64);
    return atoi(inputStr);
}

/*---------------------------------------------------------------
 * translateLevel takes a string input from the Alexa controller
 *  and converts it into a level between 0 and 100
/---------------------------------------------------------------*/
int translateLevel(String level, int currentLevel)
{
    level = level.toUpperCase();
    if (level == "ON") {
         currentLevel = 100;
    } 
    else if (level == "OFF") {
        currentLevel = 0;
    } 
    else if (level.substring(0,1) == "+") {
        level = level.substring(1,level.length());
        currentLevel += stoi(level);
    } 
    else if (level.substring(0,1) == "-") {
        level = level.substring(1,level.length());
        currentLevel -= stoi(level);
    } 
    else if (level.substring(0,6) == "COLOR:") {
        level = level.substring(6,level.length());
        _hue = stoi(getValue(level,':',0));
        _saturation = stoi(getValue(level,':',1));
        _brightness = stoi(getValue(level,':',2));
        if (_lightLevel == 0)  {
            _lightLevel == 100;
        } 
    } else {
        currentLevel = stoi(level);
    }
   
    //If the current level is out of range, return top or bottom of the range.
    if (currentLevel > 100) return 100;
    if (currentLevel < 0) return 0;
    return currentLevel;
}

String getValue(String data, char separator, int index)
{
    int found = 0;
    int strIndex[] = { 0, -1 };
    int maxIndex = data.length() - 1;

    for (int i = 0; i <= maxIndex && found <= index; i++) {
        if (data.charAt(i) == separator || i == maxIndex) {
            found++;
            strIndex[0] = strIndex[1] + 1;
            strIndex[1] = (i == maxIndex) ? i+1 : i;
        }
    }
    return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

