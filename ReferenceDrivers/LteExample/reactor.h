#ifndef _reactor_H_
#define _reactor_H_

#include <Arduino.h>
#include <Wire.h>  //includes wire library//

#include <map>

#include "PinOut.h"
#include "GlobalVariables.h"
#include "ChipManager.h"

// #include "dma_flexio.h"

class Reactor {
  private:
    // struct pinLocation {
    //     CHIPS chip;
    //     uint8_t pin;
    // };
    
    // Struct for complete reactor configuration
    struct ReactorConfig {
        uint8_t M0;
        uint8_t M1;
        uint8_t StirEnable;
        uint8_t StirCurrent;
        uint8_t StirFault;
        uint8_t StirStep;
        uint8_t UV;
        uint8_t INN;
        uint8_t DA;
        uint8_t DB;
        uint8_t DC;
        uint8_t DF;
        uint8_t IR_SYNC;
        uint8_t ADC_Channel;
        uint8_t SLA;
        uint8_t SLB;
        uint8_t LDA;
        uint8_t LDB;
        uint16_t sync_frequency;
    };

    PCA9505* heatboard_io_chip;

    // Static configuration table - one per reactor type
    ReactorConfig pinMap;

    REACTOR::Type reactor_name;
    // std::map<CHIPS, PCA9505>& io_chips;
    // std::map<CHIPS, PCA9685>& pwm_chips;
    // std::map<CHIPS, AD5673>& dac_chips;
    // std::map<CHIPS, AD5675>& dac_chips2;
    // std::map<CHIPS, AD7768>& adc_chips;
    // std::map<CHIPS, ADS124S08>& adc_chips2;
    // std::map<CHIPS, TMP117>& temp_chips;

    void setPinMap();
    void setSYNC();

    bool stirState = false;             // default is disabled
    float stirSpeedRPM;                 // controls RPM of stir bar
    float stirPower;                    // controls current going through coils
    float cuvetteTemp = -1.0; 
    uint16_t microstep_per_full_step = 1; // default is full step

    bool UVState = false;
    uint16_t UV_Duty_Cycle_12Bit = 0;  // UV intensity between 0-4096, default is off
    
    bool IRState = false;  // default is disabled
    bool SYNCState = false;  // default is disabled
    uint16_t IRWavelength = 0;  // default is zero

    float ODVoltage;  // the 24 bit number provided by the ADC

    bool PD_is_adsorption = true;  // We launch in adsorption mode
    bool ODFilterIsFastMode = true;  // possible values are lownoise and fast
    bool HeaterPIDState = false; //default is false
    uint16_t CuvetterHeaterDutyCycle = 0; //default is 0

    bool blue_indicator_state = 0;
    bool orange_indicator_state = 0;


  public:
    Reactor();
    Reactor(REACTOR::Type name);

    float AdsorptionVoltage;
    uint AdsorptionTimestamp;
    float ScatterVoltage;
    uint ScatterTimestamp;
    uint max_od_interval;
    uint min_od_interval;
    float average_ad_voltage;
    float max_ad_voltage;
    float min_ad_voltage;
    float max_scatter_voltage;
    float min_scatter_voltage;
    float average_scatter_voltage;
    bool error_occured;
    bool repeated_data_occured;
    elapsedMillis scatter_adsorption_switch_delay_timer;
    bool allow_od_readings = true;
    bool first_reading_after_pd_switch = false;

    // How much we wait after switching photodiodes to ensure there is no averaging across readings - this is dependent on the 
    // MCLK, Div, and Dec settings of the ADC channel
    // For a clock of 1.2288 Mhz (V2 machines) with 32 div and 1024 dec (i.e. the slowest possible comfig) its about 160ms settling so having a 250ms delay is adequately safe
    const uint32_t scatter_adsorption_switch_delay_ms = 300;

    
    // RGB LED defaults to off and white;
    uint8_t rgb_red_8bit        = 255;
    uint8_t rgb_green_8bit      = 255;
    uint8_t rgb_blue_8bit       = 255;
    uint8_t rgb_intensity_8bit  = 0;
    
    void setToDefault();
    
    double getPressureReg(int pressure_regulator_id);

    // Stirring related
    void setMicrostepsPerFullStep(uint16_t steps);
    uint16_t getMicrostepsPerFullStep();
    void setStirPower(float power_percentage);
    float getStirPower();
    void setStirSpeed(float input_speed_RPM);
    float getStirSpeed();
    void setStirIO(bool state); //invoked when speed set to 0
    bool getStirIO();


    // UV related
    void setUVIO(bool state);
    bool getUVIO();
    void setUVDutyCycle(uint16_t PWM_12bit);
    uint16_t getUVDutyCycle();


    // Visible LED related
    void setRGBColor(uint8_t red, uint8_t green, uint8_t blue, uint8_t intensity);
    void setBlueIndicator(bool state);
    void setOrangeIndicator(bool state);

    // IR related
    void setIRIO(bool state);
    bool getIRIO();
    void setIRWavelength(uint16_t wavelength);
    uint16_t getIRWavelength();
    void selectAdsorptionPD(bool state);
    bool isPDAdsorption();
    AD7768Data getODData();
    void setPDFilterFastMode(bool state);
    bool isPDFilterFast();
    bool getSYNC();
    void enableSYNC(bool state);
    bool outputOD = true;


    // Temperature related
    void setTargetTemp(float target_temp);
    float getTargetTemp();
    void updateCuvetteTemp(float temp);
    float getCuvetteTemp();
    void setPIDIO(bool state);
    bool getPIDIO();
    void setCuvetteHeaterDutyCycle(uint16_t duty_cycle);
    uint16_t getCuvetteHeaterDutyCycle();
    void HeaterPIDLoop();

    bool send_pid_packets = false; // this is used to send the PID packets to the python script
    uint64_t pid_packet_id = 0; // this is used to identify the PID packets

    float requested_cuvette_temp;
    bool reached_temp_threshold;
    float last_measured_temperature; // Get the last measured temperature
    
    float kc    = 300;
    float taui  = 260.0;
    float taud  =  20.0;

    float kp = kc;
    float ki = kc/taui; // (2*kp) / Tu
    float kd = kc*taud;;

    float p_term = 0.0;
    float i_term = 0.0;
    float d_term = 0.0;

    float error = 0.0;
    
    float dt = 10.0; // time step in seconds, must be same as how fast the interrupt is running
    // float error_old = 0; // this starts at 0 for the first pid loop
    // float integral  = 0; // explicitly start at 0 first time pid loop is run
    uint16_t new_duty_cycle;
    // float estimated_duty_cycle_to_reach_setpoint = 0; // this is used to estimate the duty cycle needed to reach the set point

};

#endif

#define stir_max_power_ref_voltage 2.64  // the voltage at the Vref pin corresponding to allowing maximum current through the coils
#define degrees_rotation_per_step 90.0
#define teensy_frequency_divider 256.0
#define seconds_per_minute 60.0