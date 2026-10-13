#ifndef _machine_H_
#define _machine_H_
#include <Arduino.h>
#include <Wire.h>  //includes wire library//
#include "PinOut.h"
#include "valve_map.h"
#include <map>

#include "ChipManager.h"
#include "reactor.h"
#include "mixing.h"
#include "GlobalVariables.h"

#define VALVE_COUNT 32
#define SHARED_VALVE_START_INDEX 20 // this is the valve_ID in the valve_map.csv at which the shared valves start - i.e. EXT_IN1 and onwards
#define REACTOR_COUNT 8
#define MIXING_BOTTLE_COUNT 4

namespace HEATBOARD_CURRENT_SELECTION {
  enum TYPE : uint8_t {
      REACTOR_HEATER = 0,
      RGB = 1,
      VALVE = 2,
      MACHINE = 3,
      FAN = 4,
      UV = 5,
      STIR = 6,
    };
}

class Machine {
  private:
    struct PresVoltConversion {
      float voltage;
      float pressure;
    };

    struct valveLocation {
        uint8_t chip; // Heatboard_IO_N for valveMap2, Topboard_IO_N for indicatorMap2
        uint8_t bank;
        uint8_t position;
    };
    void setStirVoltage(uint8_t level); //Switches between level 0: 4.7V, 1: 6V, and 2: 8V

    bool fluid_sensor_state; // default is disabled   
    uint32_t valve_state[MIXING_BOTTLE_COUNT];   // 4 32 bit integers to store the state of all the valves - separated by which letter they belong to
    bool fanState = false;  // default is disabled
    int fanDutyCycle;       // controls the speed of the fan
    int peristalticDutyCycle; // controls the peristaltic pump duty cycle

    // Stuff for the RTD ADC
    uint RTDADC_commandStarted = 0; //What time in millis did it become busy with whatever it is currently doing. 
    
    bool RTDADC_Calibrating= false;

    static constexpr uint RTDADC_calibrationTimeout = 1000; //How long do we wait for each calibration step in ms. 
    int RTDADC_calibrationStep=0;// Which step thropughj the calibration procedure is it up to. Anything >0 means the system is mid way through calibration so should not do anything else.

    bool RTDADC_Fastreading = false; //Are we awaiting a fast readout of something?
    static constexpr uint RTDADC_fastreadTimeout_ms = 30; // How long in ms do we wait to let this thing settle. Dont make this shorter than RTD_ADC_update_freq_ms. This may need to change if you mess with the DATARATE registers in ADS124S08::Configure
    int RTDADC_fastreadWhich = -1;// Which fast readout are we doing? 0 is nothing/error, 1 is LED current, 2 is pressure sensor 1, 3 is pressure sensor 2.

    bool RTDADC_RTDReading = false; //Are we awaiting a RTD readout?
    static constexpr uint RTDADC_RTDTimeout_ms = 180; // How long in ms do we wait to read out the RTD - depends on how much averaging we are doing. This may need to change if you mess with the DATARATE registers in ADS124S08::Configure. Note it is currently set from the 113 in Table 18 plus two times our 13ms delay which sets a minimum at 149 ms
    int RTDADC_RTDIndex = 0; // Starting state is reading RTD 0 
    bool RTDADC_RTDForceShutdown = false; // This flag gets set if the RTD is forced to shut down by either the calibration or fastread protocol and basically tells us to go back and repeat that measurement
    int RTDADC_RTDStep = 0;// Which stgep through the RTD readout procedure are we up to. Starts at 0. -1 means it is done.

    // these values are set on the pressure regulator, update them here dependent on what they are set to
    // F1 cannot be < 0, F2 is 10-120
    static constexpr int pressure_reg_F1 = 0;   // what F1 is set as a percentage of the minimum pressure. e.g. at 10%, 0% voltage would correlate to 10% of the pressure range
    static constexpr int pressure_reg_F2 = 100; // what F2 is set as a percentage of the maximum. e.g. at 120 it is 120% so the maximum pressure is reached at 80% of the FS voltage - 4V
    
    // constant values as a result of the pressure regulator spec SMC ITV1011- 21N1CL
    static constexpr float min_pressure_MPa = 0.005;
    static constexpr float max_pressure_MPa = 0.1;
    static constexpr float min_voltage_reg_input = 0.0; // the minimum voltage that the pressure reg associates with min pressure on the input
    static constexpr float max_voltage_reg_input = 5.0;
    static constexpr float min_voltage_reg_output = 1.0; // the minimum voltage that the pressure reg associates with min pressure on the input
    static constexpr float max_voltage_reg_output = 5.0;
    static constexpr float max_voltage_dac_output = AD5675_MaxVoltage;
    static constexpr float min_voltage_dac_output = 0.0;

    static constexpr float Pmin = max_pressure_MPa * (float(pressure_reg_F1)/100.0);
    static constexpr float Pmax = max_pressure_MPa * (float(pressure_reg_F2)/100.0);

    float input_pressure_voltage;
    float applied_pressure;



  public:
    Machine();
    // Machine(std::map<SYSTEM_NAMES, Reactor>& reactors, std::map<SYSTEM_NAMES, Mixing>& mixing_bottles);

    Reactor reactors[REACTOR_COUNT] = {
      Reactor(REACTOR::AL),
      Reactor(REACTOR::AR),
      Reactor(REACTOR::BL),
      Reactor(REACTOR::BR),
      Reactor(REACTOR::CL),
      Reactor(REACTOR::CR),
      Reactor(REACTOR::DL),
      Reactor(REACTOR::DR)
    };

    Mixing mixing_bottles[MIXING_BOTTLE_COUNT] = {
      Mixing(MIXING_BOTTLE::A),
      Mixing(MIXING_BOTTLE::B),
      Mixing(MIXING_BOTTLE::C),
      Mixing(MIXING_BOTTLE::D)
    };

    bool enable_valve_leds = true; // Flag to enable or disable the valve LEDs

    bool RTDADC_Calibrated = false ; // Flag that says ADC has been calibrated AT LEAST once.

    uint64_t fluid_sensor_packet_id;
    uint64_t temperatures_packet_id;

    bool SEND_MEMORY_REPORT = false; // Flag to indicate if health report should be sent
    uint64_t MEMORY_REPORT_PACKET_ID;

    bool SEND_REACTOR_TEMPERATURE_REPORT = false; // Flag to indicate if temperature report should be sent
    uint64_t REACTOR_TEMPERATURE_REPORT_PACKET_ID;

    bool SEND_OTHER_TEMPERATURE_REPORT = false; // Flag to indicate if temperature report should be sent
    uint64_t OTHER_TEMPERATURE_REPORT_PACKET_ID;

    // uint32_t default_OD_data_rate_ms = 500; // Default fast data rate in ms
    // uint32_t default_weight_data_rate_ms = 50; // Default weight data rate in ms

    bool SEND_FAST_OD_READINGS = false; // Flag to indicate if fast readings should be sent
    uint64_t OD_FAST_READINGS_PACKET_ID;

    bool SEND_FAST_WEIGHT_READINGS = false; // Flag to indicate if fast readings should be sent
    uint64_t WEIGHT_FAST_READINGS_PACKET_ID;

    bool SEND_CURRENT_REPORT = false; // Flag to indicate if current readings should be sent
    uint64_t CURRENT_REPORT_PACKET_ID;

    void setValveState(MIXING_BOTTLE::Type system_int, uint32_t state);
    void generalConfig();
    
    float getWasteLoadCellVoltage();

    void setFluidSensorState(bool state); // Set the state of the fluid sensor (enabled or disabled)
    bool getFluidSensorState(); // Get the state of the fluid sensor (enabled or disabled)

    uint8_t selected_fluid_sensor;
    float fluid_sensor_init_value;
    uint32_t fluid_sensor_cutoff_time_ms = 10000; // default cutoff time is 10 seconds
    static constexpr float fluid_sensor_change_threshold = 0.1;
    static constexpr uint32_t fluid_sensor_polling_rate_us = 10000; // 200ms
    static constexpr uint32_t fluid_sensor_debounce_time_ms = 500; // 500ms debounce time
    static constexpr float fluid_sensor_stabilisation_threshold = 0.002; // Machine will wait until initial readings are within this threshold before starting fluid detection routine
    
    bool send_waste_weight_packet = false; // Flag to indicate if the waste weight packet should be sent
    uint64_t waste_weight_packet_id = 0; // Packet ID for the waste weight packet
    // int32_t fluid_sensor_blanks[16] = {0}; // 16 blanks for the liquid sensors
    // void getFluidSensorBlanks();

    /**
     * @brief Retrieves the current state of all valves as a 64-bit integer.
     *
     * This function reads the digital state of each valve from the corresponding
     * IO chip and pin specified in the valveMap array. It then constructs a 64-bit
     * integer where each bit represents the state of a valve (1 for active, 0 for inactive).
     *
     * @return A 64-bit integer representing the state of all valves.
     *         Each bit in the integer corresponds to the state of a valve.
     */

    void setPeristalticDuty(uint16_t peristaltic_duty_cycle_12bit);
    uint16_t getPeristalticDuty();

    uint32_t getValveState(uint8_t system_int);
    void setFanDuty(uint16_t fan_duty_cycle_12bit);
    uint16_t getFanDuty();
    float getLEDCurrent();
    float getPressure1();
    float getPressure2();
    PresVoltConversion pressure_to_voltage(float requested_pressure);
    float voltage_to_pressure_input(float requested_voltage);
    float voltage_to_pressure_output(float output_voltage);

    void setPolarityChannelRTDADC(int channel, bool polarity);
    void setReactorCuvetteTemp(int channel, float temp);
    void processRTDADCFastread(int fastreadtype, float value);
    void setRTDADCFastread(int fastreadtype);
    void runRTDADC(bool calibrate = false, bool fastread = false, int fastreadType = 0);
    float RTDReadings[8] = {0, 0, 0, 0, 0, 0, 0, 0}; // Float array to store most recent RTD readings - note it isn't currently necessary as we are also directly senidng read values to reactor objects.
    float LEDCurrent = 0.0; // LED current in mA
    float Pressure1_MPa = 0.0; // MPa
    float Pressure2_MPa = 0.0; // MPa


    void setPressure_MPa(float pressure, int reg_sel); // reg_sel is 1 or 2 dependent on the regulator

    float getMixingHeaterCurrent();
    // void getFluidSensorTransition();
    float getFluidSensorVoltage();
    void setCurrentChannel(uint8_t channel);
    float getCurrent();
};

  extern Machine machine;

#endif
