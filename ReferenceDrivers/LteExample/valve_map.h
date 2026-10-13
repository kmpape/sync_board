#include "GlobalVariables.h"
#include <Arduino.h>
struct valveLocation {
    uint8_t chip; // Heatboard_IO_N for valveMap2, Topboard_IO_N for indicatorMap2
    uint8_t bank;
    uint8_t position;
};

#define VALVE_COUNT 32

const valveLocation valveMap[4][VALVE_COUNT] = {
    // Letter A
    {
    // {HEATBOARD_IO_chip, bank, position}
        {3,3,7},  // IB_PRS
        {3,1,4},  // IB_FILL
        {3,1,3},  // IB_IO
        {4,3,7},  // SEL_R
        {4,4,6},  // SAMPLE
        {3,3,3},  // R_PRS
        {1,1,6},  // R_IN
        {1,1,7},  // R_OUT
        {1,2,0},  // R_TFR
        {1,1,5},  // R_SPR 
        {3,3,2},  // L_PRS
        {1,0,4},  // L_IN
        {1,0,5},  // L_OUT
        {1,0,6},  // L_TFR
        {1,0,3},  // L_SPR 
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {3,0,1},  // EXT_IN1
        {3,0,2},  // EXT_IN2
        {3,0,3},  // EXT_IN3
        {3,0,4},  // EXT_IN4
        {3,0,6},  // EXT_IN5
        {3,0,7},  // EXT_IN6
        {3,1,0},  // EXT_IN7
        {3,1,1},  // EXT_IN8
        {3,2,7},  // OZ_SW
        {3,2,6},  // FILL_SPR
        {3,2,5},  // IO_SPR
        {0,0,0}   // (none)
    },

    // Letter B
    {
        {3,4,0},  // IB_PRS
        {3,1,6},  // IB_FILL
        {3,1,5},  // IB_IO
        {4,4,0},  // SEL_R
        {4,4,7},  // SAMPLE
        {3,3,5},  // R_PRS
        {1,4,2},  // R_IN
        {1,4,3},  // R_OUT
        {1,4,4},  // R_TFR
        {1,4,1},  // R_SPR
        {3,3,4},  // L_PRS
        {1,3,0},  // L_IN
        {1,3,1},  // L_OUT
        {1,3,2},  // L_TFR
        {1,2,7},  // L_SPR
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {3,0,1},  // EXT_IN1
        {3,0,2},  // EXT_IN2
        {3,0,3},  // EXT_IN3
        {3,0,4},  // EXT_IN4
        {3,0,6},  // EXT_IN5
        {3,0,7},  // EXT_IN6
        {3,1,0},  // EXT_IN7
        {3,1,1},  // EXT_IN8
        {3,2,7},  // OZ_SW
        {3,2,6},  // FILL_SPR
        {3,2,5},  // IO_SPR
        {0,0,0}   // (none)
    },

    // Letter C
    {
        {3,4,1},  // IB_PRS
        {3,2,1},  // IB_FILL
        {3,2,0},  // IB_IO
        {4,4,1},  // LR_SEL
        {4,4,5},  // SAMPLE
        {3,4,5},  // R_PRS
        {2,1,6},  // R_IN
        {2,1,7},  // R_OUT
        {2,2,0},  // R_TFR
        {2,1,5},  // R_SPR
        {3,4,4},  // L_PRS
        {2,0,4},  // L_IN
        {2,0,5},  // L_OUT
        {2,0,6},  // L_TFR
        {2,0,3},  // L_SPR
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {3,0,1},  // EXT_IN1
        {3,0,2},  // EXT_IN2
        {3,0,3},  // EXT_IN3
        {3,0,4},  // EXT_IN4
        {3,0,6},  // EXT_IN5
        {3,0,7},  // EXT_IN6
        {3,1,0},  // EXT_IN7
        {3,1,1},  // EXT_IN8
        {3,2,7},  // OZ_SW
        {3,2,6},  // FILL_SPR
        {3,2,5},  // IO_SPR
        {0,0,0}   // (none)
    },

    // Letter D
    {
        {3,4,2},  // IB_PRS
        {3,2,3},  // IB_FILL
        {3,2,2},  // IB_IO
        {4,4,2},  // SEL_R
        {4,4,4},  // SAMPLE
        {3,4,7},  // R_PRS
        {2,4,2},  // R_IN
        {2,4,3},  // R_OUT
        {2,4,4},  // R_TFR
        {2,4,1},  // R_SPR
        {3,4,6},  // L_PRS
        {2,3,0},  // L_IN
        {2,3,1},  // L_OUT
        {2,3,2},  // L_TFR
        {2,2,7},  // L_SPR
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {3,0,1},  // EXT_IN1
        {3,0,2},  // EXT_IN2
        {3,0,3},  // EXT_IN3
        {3,0,4},  // EXT_IN4
        {3,0,6},  // EXT_IN5
        {3,0,7},  // EXT_IN6
        {3,1,0},  // EXT_IN7
        {3,1,1},  // EXT_IN8
        {3,2,7},  // OZ_SW
        {3,2,6},  // FILL_SPR
        {3,2,5},  // IO_SPR
        {0,0,0}   // (none)
    }
};

const valveLocation indicatorMap[4][VALVE_COUNT] = {
    // Letter A
    {
    // {TOPBOARD_IO_chip, bank, position}
        {1,3,7},  // IB_PRS
        {1,4,1},  // IB_FILL
        {1,4,2},  // IB_IO
        {1,2,0},  // SEL_R
        {2,0,7},  // SAMPLE
        {1,4,0},  // R_PRS
        {1,1,6},  // R_IN
        {1,1,5},  // R_OUT
        {1,1,4},  // R_TFR
        {1,1,7},  // R_SPR
        {1,3,6},  // L_PRS
        {1,2,3},  // L_IN
        {1,2,2},  // L_OUT
        {1,2,1},  // L_TFR
        {1,2,4},  // L_SPR 
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {1,2,5},  // EXT_IN1
        {1,2,6},  // EXT_IN2
        {1,2,7},  // EXT_IN3
        {1,3,0},  // EXT_IN4
        {1,3,1},  // EXT_IN5
        {1,3,2},  // EXT_IN6
        {1,3,3},  // EXT_IN7
        {1,3,4},  // EXT_IN8
        {1,3,5},  // OZ_SW
        {1,0,1},  // FILL_SPR
        {1,0,2},  // IO_SPR
        {0,0,0}   // (none)
    },

    // Letter B
    {
        {1,4,3},  // IB_PRS
        {1,4,4},  // IB_FILL
        {1,4,5},  // IB_IO
        {1,0,7},  // SEL_R
        {2,0,6},  // SAMPLE
        {1,0,4},  // R_PRS
        {2,3,5},  // R_IN
        {2,3,4},  // R_OUT
        {2,3,3},  // R_TFR
        {2,3,6},  // R_SPR
        {1,0,0},  // L_PRS
        {1,1,2},  // L_IN
        {1,1,1},  // L_OUT
        {1,1,0},  // L_TFR
        {1,1,3},  // L_SPR
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {1,2,5},  // EXT_IN1
        {1,2,6},  // EXT_IN2
        {1,2,7},  // EXT_IN3
        {1,3,0},  // EXT_IN4
        {1,3,1},  // EXT_IN5
        {1,3,2},  // EXT_IN6
        {1,3,3},  // EXT_IN7
        {1,3,4},  // EXT_IN8
        {1,3,5},  // OZ_SW
        {1,0,1},  // FILL_SPR
        {1,0,2},  // IO_SPR
        {0,0,0}   // (none)
    },

    // Letter C
    {
        {1,0,3},  // IB_PRS
        {1,4,6},  // IB_FILL
        {1,4,7},  // IB_IO
        {2,2,5},  // SEL_R
        {2,0,5},  // SAMPLE
        {2,0,1},  // R_PRS
        {2,2,3},  // R_IN
        {2,2,2},  // R_OUT
        {2,2,1},  // R_TFR
        {2,2,4},  // R_SPR
        {2,0,0},  // L_PRS
        {2,3,0},  // L_IN
        {2,2,7},  // L_OUT
        {2,2,6},  // L_TFR
        {2,3,1},  // L_SPR
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {1,2,5},  // EXT_IN1
        {1,2,6},  // EXT_IN2
        {1,2,7},  // EXT_IN3
        {1,3,0},  // EXT_IN4
        {1,3,1},  // EXT_IN5
        {1,3,2},  // EXT_IN6
        {1,3,3},  // EXT_IN7
        {1,3,4},  // EXT_IN8
        {1,3,5},  // OZ_SW
        {1,0,1},  // FILL_SPR
        {1,0,2},  // IO_SPR
        {0,0,0}   // (none)
    },

    // Letter D
    {
        {2,3,2},  // IB_PRS
        {1,0,5},  // IB_FILL
        {1,0,6},  // IB_IO
        {2,1,4},  // SEL_R
        {2,0,4},  // SAMPLE
        {2,0,3},  // R_PRS
        {2,1,2},  // R_IN
        {2,1,1},  // R_OUT
        {2,1,0},  // R_TFR
        {2,1,3},  // R_SPR
        {2,0,2},  // L_PRS
        {2,1,7},  // L_IN
        {2,1,6},  // L_OUT
        {2,1,5},  // L_TFR
        {2,2,0},  // L_SPR
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {0,0,0},  // (none)
        {1,2,5},  // EXT_IN1
        {1,2,6},  // EXT_IN2
        {1,2,7},  // EXT_IN3
        {1,3,0},  // EXT_IN4
        {1,3,1},  // EXT_IN5
        {1,3,2},  // EXT_IN6
        {1,3,3},  // EXT_IN7
        {1,3,4},  // EXT_IN8
        {1,3,5},  // OZ_SW
        {1,0,1},  // FILL_SPR
        {1,0,2},  // IO_SPR
        {0,0,0}   // (none)
    }
};
