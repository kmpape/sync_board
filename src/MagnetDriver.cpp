#include <Arduino.h>
#include "GlobalVariables.h"
#include "ICDrivers.h"
#include "SerialController.h"
#include "MagnetDriver.h"
#include "PinOut.h"
#include "IOController.h"
#include <digitalWriteFast.h>

bool adcEnabled = false;
bool dacEnabled = false;
bool magEnabled = false;

const int V_CURRENT_ID = 1;
const int V_COIL_ID = 2;

bool  magnet_calibrated = false;
float magnet_current_voltage_slope_NC = 1.0; // Check this value
float magnet_current_voltage_intercept_NC = 1.65; // x-intercept of the line of best fit. i.e the voltage at 0A
float magnet_current_voltage_slope_NO = 1.0; // Check this value
float magnet_current_voltage_intercept_NO = 1.65; // x-intercept of the line of best fit. i.e the voltage at 0A
const int callibration_pts = 20; // Number of points to take for callibration
const int callibration_avg = 20; // Number of points to average for each callibration point

const int MAG_DAC_NO_ID = 2;
const int MAG_DAC_NC_ID = 3;

// Called before system enabled
void attachMagnetBoard() {
    if (systemEnable == true){ // If the system is disabled then we can't change the GPIO.
        raiseError("MagnetBoard: Cant attach board while system enabled");
        return;
    }
    // Configure GPIO_29 for magnet enable
    pinMode(GPIO_29, OUTPUT);
    // And make sure it's off
    digitalWriteFast(GPIO_29, LOW);

    // Configure GPIO_30 for magnet switch
    pinMode(GPIO_30, OUTPUT);
    // And make sure it's off (=> NC => DAC3 if using magboard)
    digitalWriteFast(GPIO_30, LOW);
}

// Called after system enabled
void setupMagnetBoard() {
    if (MagAttached == false) {
        raiseError("MagnetBoard: No Magnet Board Attached");
        return;
    }

    if (checkForDevice(MagBoard_ADC_ADR) != 0) {
        raiseError("MagnetBoard: No ADC detected at address 0x"+String(MagBoard_ADC_ADR, HEX));
    } else {
        adcEnabled = true;
    }

    if (checkForDevice(MagBoard_DAC_ADR) != 0) {
        raiseError("MagnetBoard: No DAC detected at address 0x"+String(MagBoard_DAC_ADR, HEX));
    } else {
        setupDACI2C(MagBoard_DAC_ADR, true);
        dacEnabled = true;
    }
}

void enableMagnet(bool enable = false) {
    if (MagAttached == false) {
        raiseError("MagnetBoard: No Magnet Board Attached");
        return;
    }
    if (dacEnabled == false) {
        raiseError("MagnetBoard: DAC not enabled. Enable it using setupMagnetBoard()");
        return;
    }
    if (adcEnabled == false) {
        raiseError("MagnetBoard: ADC not enabled. Enable it using setupMagnetBoard()");
        return;
    }
    if (enable) {
        digitalWriteFast(GPIO_29, HIGH);
    } else {
        digitalWriteFast(GPIO_29, LOW);
    }
}

void switchMagnetOutput(bool NC = true) {
    if (MagAttached == false) {
        raiseError("MagnetBoard: No Magnet Board Attached");
        return;
    }
    if (dacEnabled == false) {
        raiseError("MagnetBoard: DAC not enabled. Enable it using setupMagnetBoard()");
        return;
    }
    if (NC) {
        digitalWriteFast(GPIO_30, LOW);
    } else {
        digitalWriteFast(GPIO_30, HIGH);
    }
}

void callibrateMagnet() {
    enableMagnet(true);

    // First callibrate DAC3 (NC)
    switchMagnetOutput(true);

    // Record the current voltage curve around 0A
    float voltage[callibration_pts] = {0};
    float current[callibration_pts] = {0};

    float start_voltage = 1.64;
    float end_voltage   = 1.67;

    setMagDACI2C(MAG_DAC_NC_ID, start_voltage);
    delay(1000);

    for (int i = 0; i < callibration_pts; i++) {
        voltage[i] = start_voltage + (end_voltage-start_voltage)*i/(((float)callibration_pts)-1);
        setMagDACI2C(MAG_DAC_NC_ID, voltage[i]);
            
        //Now we do a Heartbeat trigger since this long calibration functionc an oherwise crash the heartbeat.
        digitalWriteFast(Heartbeat, LOW);
        delay(3);
        digitalWriteFast(Heartbeat, HIGH);
        delay(3);
        digitalWriteFast(Heartbeat, LOW);
        delay(3);

        for(int j = 0; j < callibration_avg; j++) {            
            current[i] += singleReadMagnetADC(V_CURRENT_ID);
        }
        current[i] /= ((float)callibration_avg);
        // Serial.println(String(voltage[i], 4)+","+String(current[i], 4));
    }

    // Now we fit a line to the data
    float sum_x = 0;
    float sum_y = 0;
    float sum_xy = 0;
    float sum_x2 = 0;
    for (int i = 0; i < callibration_pts; i++) {
        sum_x += voltage[i];
        sum_y += current[i];
        sum_xy += voltage[i]*current[i];
        sum_x2 += voltage[i]*voltage[i];
    }
    magnet_current_voltage_slope_NC = (((float)callibration_pts)*sum_xy - sum_x*sum_y)/(((float)callibration_pts)*sum_x2 - sum_x*sum_x);
    float intercept = (sum_y - magnet_current_voltage_slope_NC*sum_x)/((float)callibration_pts);
    magnet_current_voltage_intercept_NC = -intercept/magnet_current_voltage_slope_NC;

    enableMagnet(false);

    if (magnet_current_voltage_intercept_NC <= 1.6 || magnet_current_voltage_intercept_NC >= 1.7) {
        raiseError("MagnetBoard: Callibration of DAC3 failed. Intercept = "+String(magnet_current_voltage_intercept_NC, 4)+"V");
        return;
    }

    Serial.println("MagnetBoard: Callibration of DAC3 complete. Slope = "+String(magnet_current_voltage_slope_NC)+", y-intercept = "+String(intercept)+", 0A occurs at "+String(magnet_current_voltage_intercept_NC, 4)+"V");

    delay(100);
    enableMagnet(true);

    // Now callibrate DAC2 (NO)
    switchMagnetOutput(false);

    // Record the current voltage curve around 0A
    voltage[callibration_pts] = {0};
    current[callibration_pts] = {0};

    setMagDACI2C(MAG_DAC_NO_ID, start_voltage);
    delay(1000);

    for (int i = 0; i < callibration_pts; i++) {
        voltage[i] = start_voltage + (end_voltage-start_voltage)*i/(((float)callibration_pts)-1);
        setMagDACI2C(MAG_DAC_NO_ID, voltage[i]);
            
        //Now we do a Heartbeat trigger since this long calibration functionc an oherwise crash the heartbeat.
        digitalWriteFast(Heartbeat, LOW);
        delay(3);
        digitalWriteFast(Heartbeat, HIGH);
        delay(3);
        digitalWriteFast(Heartbeat, LOW);
        delay(3);

        for(int j = 0; j < callibration_avg; j++) {            
            current[i] += singleReadMagnetADC(V_CURRENT_ID);
        }
        current[i] /= ((float)callibration_avg);
        // Serial.println(String(voltage[i], 4)+","+String(current[i], 4));
    }

    // Now we fit a line to the data
    sum_x = 0;
    sum_y = 0;
    sum_xy = 0;
    sum_x2 = 0;
    for (int i = 0; i < callibration_pts; i++) {
        sum_x += voltage[i];
        sum_y += current[i];
        sum_xy += voltage[i]*current[i];
        sum_x2 += voltage[i]*voltage[i];
    }
    magnet_current_voltage_slope_NO = (((float)callibration_pts)*sum_xy - sum_x*sum_y)/(((float)callibration_pts)*sum_x2 - sum_x*sum_x);
    intercept = (sum_y - magnet_current_voltage_slope_NO*sum_x)/((float)callibration_pts);
    magnet_current_voltage_intercept_NO = -intercept/magnet_current_voltage_slope_NO;

    enableMagnet(false);

    if (magnet_current_voltage_intercept_NO <= 1.6 || magnet_current_voltage_intercept_NO >= 1.7) {
        raiseError("MagnetBoard: Callibration of DAC2 failed. Intercept = "+String(magnet_current_voltage_intercept_NO, 4)+"V");
        return;
    }

    Serial.println("MagnetBoard: Callibration of DAC2 complete. Slope = "+String(magnet_current_voltage_slope_NO)+", y-intercept = "+String(intercept)+", 0A occurs at "+String(magnet_current_voltage_intercept_NO, 4)+"V");

    switchMagnetOutput(true);
    magnet_calibrated = true;    
}

float singleReadMagnetADC(uint8_t channel) {
    if (MagAttached == false) {
        raiseError("MagnetBoard: No Magnet Board Attached");
        return 0;
    }
    if (adcEnabled == false) {
        raiseError("MagnetBoard: ADC not enabled. Enable it using setupMagnetBoard()");
        return 0;
    }
    if (channel > 7) {
        raiseError("MagnetBoard: Tried to read from invalid ADC channel "+String(channel)+".");
        return 0;
    }
    return readADC(channel, 2) - 1.65;
}