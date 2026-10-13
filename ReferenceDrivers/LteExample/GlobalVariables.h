#pragma once

extern bool serialReady; //Defined in SerialCconroller.cpp
extern bool systemEnable; // defined in main.cpp
extern bool LEDAttached; // defined in main.cpp
extern int syncMode; // defined in main.cpp
extern bool debugmode; // defined in main.cpp
extern int NumberLEDsBeingTimed; // defined in main.cpp

namespace REACTOR {
enum Type : uint8_t {
    AL,
    AR,
    BL,
    BR,
    CL,
    CR,
    DL,
    DR,
    NONE
};
}

namespace MIXING_BOTTLE {
enum Type : uint8_t {
    A,
    B,
    C,
    D,
    NONE
};
}
