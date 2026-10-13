#ifndef _mixing_H_
#define _mixing_H_

#include <Arduino.h>
#include <Wire.h>  //includes wire library//

#include <map>

#include "MLX90614.h"
#include "GlobalVariables.h"
#include "ChipManager.h"

class Mixing {
  private:
    struct MixingConfig {
        uint8_t ADC_Channel;
        uint8_t Thermo_Mux_PIN;
        uint8_t HEAT_1_PIN;
        uint8_t HEAT_2_PIN;
    };

    MIXING_BOTTLE::Type mixing_name;
    
    MixingConfig pinMap;

    void setPinMap();

    bool HeaterPIDState = false;             // default is disabled
    float mixingTemp = -1.0; 
    uint16_t heater_duty_12bit = 0;  


  public:
    Mixing();
    Mixing(MIXING_BOTTLE::Type name);

    AD7768Data getLoadCellData();

    void setToDefault();

    void setTargetTemp(float target_temp);
    float getTargetTemp();
    void setPIDIO(bool state);
    bool getPIDIO();

    void updateMixingTemp(float temp);
    float getMixingTemp();
    
    float requested_heater_temp;
    void setHeaterDuty(uint16_t pwm_duty_cycle);
    uint16_t getHeaterDuty();
    void HeaterPIDLoop();

    


    bool send_pid_packets = false; // this is used to send the PID packets to the python script
    uint64_t pid_packet_id = 0; // this is used to identify the PID packets
    
    float requested_mixing_temp;

    float last_measured_temperature; // Get the last measured temperature
    
    float kc    = 400.0;
    float taui  = 700.0;
    float taud  =   0.0;

    float kp = kc;
    float ki = kc/taui; // (2*kp) / Tu
    float kd = kc*taud;;

    float p_term = 0.0;
    float i_term = 0.0;
    float d_term = 0.0;

    float error = 0.0;

    float dt = 1.0; // time step in seconds, must be same as how fast the interrupt is running

    uint16_t new_duty_cycle;

  };

#endif
