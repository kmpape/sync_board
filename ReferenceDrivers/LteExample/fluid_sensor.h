// only one object of this class can be operating at a time

class fluid_sensor {
public:
    fluid_sensor();
    void begin();
    float readFluidLevel();
private:
    bool initial_readings_stabilised = false;
    bool in_use = false;

};

/*
requirements:
only one fluid sensor object can be in use at a time
run in a timer loop constantly getting the latest value

fluid sensors timing:
SPI command to ADC GPIO config 
Multiplexer switching time
ADC settle time on new value

so after sending the SPI command we need to wait X seconds before we can be confident the value echod by the ADC is for the correct sensor

since to get meaningful values from the waste scale we would only want to send things one at a time, we will only be reading one fluid sensor at a time
no need for a routine that switches between them trying to read them all simultaneously

start with IB filling blockage/empty bottle detection:
Flush shared IB input line with water then air
Fluid sensor transition detection of air
Prime input line with filling liquid, check that fluid sensor transition is true
Fill IB with liquid, if gradient of line is flat for 20 seconds, 20 seconds after operation began then timeout and report blockage or empty supply bottle
Record flow rate for the specific liquid
If flow rate for the specific liquid is significantly lower than the running average, panic

start with peristaltic pump, waste weighing protocol. 

def blockage_detection_checker(expected_gain)
weight of waste scales to start 

evacuate ib

fill ib

evacuate reactor

fill reactor


*/