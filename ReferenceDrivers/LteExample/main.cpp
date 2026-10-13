#include "GenerationConfig.h"

#if GENERATION == V1_MACHINES
    #pragma message "GENERATION=V1_MACHINES"
#elif GENERATION == V2_MACHINES
    #pragma message "GENERATION=V2_MACHINES"
#elif GENERATION == V3_MACHINES
    #pragma message "GENERATION=V3_MACHINES"
#endif

#include <Arduino.h>
#include <SPI.h>
#include <map>

#include "GlobalVariables.h"
#include "I2CAddressBook.h"

#include "PinOut.h"
#include "bufferprint.h"

#include "digitalWriteFast.h"
#include "machine.h"
#include "mixing.h"
#include "reactor.h"
#include "Teensy_HID.h"
#include "PinOutIOChips.h"

#include "ChipManager.h"

#include <Metro.h>
#include "InternalTemperature.h"

#include <malloc.h>


//#include "dma_flexio.h"

// Define global variables declared in GlobalVariables.h

#define DMABUFFER_SIZE 4
DMAChannel dmaChannelOD;
DMAChannel dmaChannelLoadCell;
volatile uint64_t dmaBufferOD[DMABUFFER_SIZE];
volatile uint64_t dmaBufferLoadCell[DMABUFFER_SIZE];

// Define global ChipManager instance
ChipManager CHIPS;

extern "C" {
  #include "pb_encode.h"
  #include "pb_decode.h"
  #include <packet_config.pb.h>
}

// Device Config
bool systemEnable = false;  // Flag to indicate if the system is enabled or not. When set true the system will be able to do things e.g. IOs and heartbeat are on.
// Note any time that we have the system disabled we should also have the IO disabled. This is because the IO is used to control the system.



///// Timing Loops
uint8_t current_iterator;
uint8_t topboard_thermo_mux_iterator;
// constexpr guarantees this is assigned at compile time i.e. none of these change during programme running
constexpr uint32_t heartbeat_update_freq_ms = 100;  // How often we want the heartbeat to pulse in millis
constexpr uint32_t RTD_ADC_update_freq_ms = 25; // How often we run the various state machines for temperature control and other such measurements in ms. This could be made faster but is basically there as a crude timeout to avoid too much logic every loop cycle...
constexpr uint32_t reactor_heater_pid_update_freq_ms = 10000; // reactor temperatures only update once every 9 seconds or so, so do not gain anything if its less than 9000
constexpr uint32_t mixing_heater_pid_update_freq_ms = 1000; // mixing temperatures only update once every 10 seconds or so, so do not gain anything if its less than 9000
constexpr uint32_t health_report_freq_ms = 10000; // how often we send a health report to the host in ms
constexpr uint32_t current_report_freq_ms = 1000; // how often we get the current readings from the heatboard in ms there are 10 steps, so will take 10x this value to return the next value for one of the currents
constexpr uint32_t reactor_temperature_report_freq_ms = 10000; // how often we send a temperature report to the host in ms
constexpr uint32_t other_temperature_report_freq_ms = 100; // how often we send a temperature report to the host in ms * 10
int32_t OD_output_freq_ms = 50; // Data output frequency in ms, this is the frequency at which the data is sent to the host
int32_t weight_output_freq_ms = 1000; // Data output frequency in ms, this is the frequency at which the data is sent to the host


/////////////////////////////////////////////////
////////////////// TIMER SETUP //////////////////
/////////////////////////////////////////////////
uint32_t max_process_time_us = 0;
elapsedMicros packet_timer;
elapsedMicros loop_timer;
uint32_t max_loop_time_us = 0;
uint16_t max_process_id = 0;
uint16_t process_id = 0;
struct max_process_time_details {
    uint32_t time_ms;
    SystemName system;
    PacketType type;
    RequestedData requested_data;
};

// these are set in enablesystem to be off, and then incoming "initialise_fast_reading" packets set the frequency
Metro OD_output_timer = Metro(OD_output_freq_ms); // default will be every 1 second, but the handler is not called until machine.SEND_FAST_OD_READINGS is set true by relevant packet
Metro weight_output_timer = Metro(weight_output_freq_ms);; // default will be every 1 second, but the handler is not called until machine.SEND_FAST_WEIGHT_READINGS is set true by relevant packet
// These timer intervals do not change ever, and so we can set them in their initialisation function
Metro reactor_heater_control_timer = Metro(reactor_heater_pid_update_freq_ms);
Metro mixing_heater_control_timer = Metro(mixing_heater_pid_update_freq_ms);
Metro RTD_ADC_timer = Metro(RTD_ADC_update_freq_ms);
Metro health_report_timer = Metro(health_report_freq_ms); // every 10 seconds
Metro current_report_timer = Metro(current_report_freq_ms); // will send packet every 10*current_report_freq_ms
Metro reactor_temperature_report_timer = Metro(reactor_temperature_report_freq_ms); // will send packet every 10*reactor_temperature_report_freq_ms
Metro other_temperature_report_timer = Metro(other_temperature_report_freq_ms); // will send packet every 10*other_temperature_report_freq_ms

bool heartbeat_started = false; // this is set true when the first heartbeat packet is received from the host, and then we start the heartbeat timer    
elapsedMillis heartbeat_timer = 0; // this is used to check the python is still communicating with the teensy
unsigned long max_heartbeat_interval_ms = 4000; // if we have not received a heartbeat packet from the host in this time then we assume the host has died and we disable the system for safety

// IntervalTimers are hardware timers so are more accurate than Metro timers, but there are only a limited number of them, so just using it for critical stuff
IntervalTimer HB_trigger; // the interval is set in Setup() 

void HeartbeatInterruptHandler() {
    digitalWriteFast(Heartbeat, !digitalReadFast(Heartbeat));  // Toggle the heartbeat pin.
}


/*
This whole bit is a little clumsy, im on a MAPS purge, 

mixing_bottles and reactors should be created on machine, 
and then we should be able to access them through machine.reactors[AL].setStirSpeed(100); for example
and SystemName_REACTOR_AL should be mapped to AL in machine.cpp
*/

// Create reactors and pass the Chips reference
// std::map<SYSTEM_NAMES, Reactor> reactors = {
//     {AL, Reactor(AL)},
//     {AR, Reactor(AR)},
//     {BL, Reactor(BL)},
//     {BR, Reactor(BR)},
//     {CL, Reactor(CL)},
//     {CR, Reactor(CR)},
//     {DL, Reactor(DL)},
//     {DR, Reactor(DR)},
// };

// std::map<SYSTEM_NAMES, Mixing> mixing_bottles = {
//     {A, Mixing(A)},
//     {B, Mixing(B)},
//     {C, Mixing(C)},
//     {D, Mixing(D)},
// };

Machine machine = Machine();

// maps to convert from auto generated SystemName enum from Proto file to Reactor or Mixing enum
std::map<SystemName, REACTOR::Type> rmap = {
    {SystemName_REACTOR_AL, REACTOR::AL},
    {SystemName_REACTOR_AR, REACTOR::AR},
    {SystemName_REACTOR_BL, REACTOR::BL},
    {SystemName_REACTOR_BR, REACTOR::BR},
    {SystemName_REACTOR_CL, REACTOR::CL},
    {SystemName_REACTOR_CR, REACTOR::CR},
    {SystemName_REACTOR_DL, REACTOR::DL},
    {SystemName_REACTOR_DR, REACTOR::DR},
};

std::map<SystemName, MIXING_BOTTLE::Type> mmap = {
    {SystemName_MIXING_A, MIXING_BOTTLE::A},
    {SystemName_MIXING_B, MIXING_BOTTLE::B},
    {SystemName_MIXING_C, MIXING_BOTTLE::C},
    {SystemName_MIXING_D, MIXING_BOTTLE::D},
};


//////////////////////////////////////////////////////////////
////////////////// INTERRUPT Handler Setups //////////////////
//////////////////////////////////////////////////////////////

Packet od_fast_packet = Packet_init_zero;
Packet weight_fast_packet = Packet_init_zero;
Packet health_report_packet = Packet_init_zero;
Packet current_report_packet = Packet_init_zero;
Packet reactor_temp_report_packet = Packet_init_zero;
Packet other_temp_report_packet = Packet_init_zero;

void initialise_fast_packets() {
    memset(&od_fast_packet, 0, sizeof(od_fast_packet)); // clear the packet again
    od_fast_packet.type = PacketType_DATA;
    od_fast_packet.has_timestamp = true;
    od_fast_packet.has_unique_id = true;
    od_fast_packet.has_data = true;
    od_fast_packet.data.has_FAST_ODS = true;
    od_fast_packet.data.FAST_ODS.has_scatter_status = true;
    od_fast_packet.data.FAST_ODS.has_AL = true;
    od_fast_packet.data.FAST_ODS.has_AR = true;
    od_fast_packet.data.FAST_ODS.has_BL = true;
    od_fast_packet.data.FAST_ODS.has_BR = true;
    od_fast_packet.data.FAST_ODS.has_CL = true;
    od_fast_packet.data.FAST_ODS.has_CR = true;
    od_fast_packet.data.FAST_ODS.has_DL = true;
    od_fast_packet.data.FAST_ODS.has_DR = true;

    memset(&weight_fast_packet, 0, sizeof(weight_fast_packet)); // clear the packet again
    weight_fast_packet.type = PacketType_DATA;
    weight_fast_packet.has_unique_id = true;
    weight_fast_packet.has_timestamp = true;
    weight_fast_packet.has_data = true;
    weight_fast_packet.data.has_FAST_WEIGHTS = true;
    weight_fast_packet.data.FAST_WEIGHTS.has_A = true;
    weight_fast_packet.data.FAST_WEIGHTS.has_B = true;
    weight_fast_packet.data.FAST_WEIGHTS.has_C = true;
    weight_fast_packet.data.FAST_WEIGHTS.has_D = true;
    weight_fast_packet.data.FAST_WEIGHTS.has_WASTE = true;
    
    memset(&health_report_packet, 0, sizeof(health_report_packet)); // clear the packet again
    health_report_packet.type = PacketType_DATA;
    health_report_packet.has_unique_id = true;
    health_report_packet.has_timestamp = true;
    health_report_packet.has_data = true;
    health_report_packet.data.has_MEMORY_REPORT = true;
    health_report_packet.data.MEMORY_REPORT.has_free_stack       = true;
    health_report_packet.data.MEMORY_REPORT.has_free_heap        = true;
    health_report_packet.data.MEMORY_REPORT.has_max_loop_time    = true;
    health_report_packet.data.MEMORY_REPORT.has_uordblks         = true;
    health_report_packet.data.MEMORY_REPORT.has_fordblks         = true;
    health_report_packet.data.MEMORY_REPORT.has_max_process_time = true;
    health_report_packet.data.MEMORY_REPORT.has_max_process_id   = true;
    health_report_packet.data.MEMORY_REPORT.has_teensy_temp      = true;
    health_report_packet.data.MEMORY_REPORT.has_rel_humidity_p   = true;

    memset(&current_report_packet, 0, sizeof(current_report_packet)); // clear the packet again
    current_report_packet.type = PacketType_DATA;
    current_report_packet.has_unique_id = true;
    current_report_packet.has_timestamp = true;
    current_report_packet.has_data = true;
    current_report_packet.data.has_CURRENT_REPORT = true;
    current_report_packet.data.CURRENT_REPORT.has_UV_CURRENT_mA             = true;       
    current_report_packet.data.CURRENT_REPORT.has_RGB_CURRENT_mA            = true;
    current_report_packet.data.CURRENT_REPORT.has_STIR_CURRENT_mA           = true;
    current_report_packet.data.CURRENT_REPORT.has_REACTOR_HEATER_CURRENT_mA = true;
    current_report_packet.data.CURRENT_REPORT.has_FAN_CURRENT_mA            = true;
    current_report_packet.data.CURRENT_REPORT.has_VALVE_CURRENT_mA          = true;
    current_report_packet.data.CURRENT_REPORT.has_MACHINE_CURRENT_mA        = true;
    current_report_packet.data.CURRENT_REPORT.has_OD_LED_CURRENT_mA         = true;
    current_report_packet.data.CURRENT_REPORT.has_MIXING_HEATER_CURRENT_mA  = true;

    memset(&reactor_temp_report_packet, 0, sizeof(reactor_temp_report_packet)); // clear the packet again
    reactor_temp_report_packet.type = PacketType_DATA;
    reactor_temp_report_packet.has_timestamp = true;
    reactor_temp_report_packet.has_unique_id = true;
    reactor_temp_report_packet.has_data = true;
    reactor_temp_report_packet.data.has_REACTOR_TEMPERATURE_REPORT = true;
    reactor_temp_report_packet.data.REACTOR_TEMPERATURE_REPORT.has_AL = true;       
    reactor_temp_report_packet.data.REACTOR_TEMPERATURE_REPORT.has_AR = true;
    reactor_temp_report_packet.data.REACTOR_TEMPERATURE_REPORT.has_BL = true;
    reactor_temp_report_packet.data.REACTOR_TEMPERATURE_REPORT.has_BR = true;
    reactor_temp_report_packet.data.REACTOR_TEMPERATURE_REPORT.has_CL = true;
    reactor_temp_report_packet.data.REACTOR_TEMPERATURE_REPORT.has_CR = true;
    reactor_temp_report_packet.data.REACTOR_TEMPERATURE_REPORT.has_DL = true;
    reactor_temp_report_packet.data.REACTOR_TEMPERATURE_REPORT.has_DR = true;

    memset(&other_temp_report_packet, 0, sizeof(other_temp_report_packet)); // clear the packet again
    other_temp_report_packet.type = PacketType_DATA;
    other_temp_report_packet.has_timestamp = true;
    other_temp_report_packet.has_unique_id = true;
    other_temp_report_packet.has_data = true;
    other_temp_report_packet.data.has_OTHER_TEMPERATURE_REPORT = true;
    other_temp_report_packet.data.OTHER_TEMPERATURE_REPORT.has_A = true;
    other_temp_report_packet.data.OTHER_TEMPERATURE_REPORT.has_B = true;
    other_temp_report_packet.data.OTHER_TEMPERATURE_REPORT.has_C = true;
    other_temp_report_packet.data.OTHER_TEMPERATURE_REPORT.has_D = true;
    other_temp_report_packet.data.OTHER_TEMPERATURE_REPORT.has_Topboard12 = true;
    other_temp_report_packet.data.OTHER_TEMPERATURE_REPORT.has_Topboard45 = true;
    other_temp_report_packet.data.OTHER_TEMPERATURE_REPORT.has_Topboard78 = true;
    other_temp_report_packet.data.OTHER_TEMPERATURE_REPORT.has_Senseboard = true;
}

void CurrentReportHandler(){
    current_report_packet.unique_id = machine.CURRENT_REPORT_PACKET_ID;
    // we break getting the current readings into 10 steps to avoid blocking the main loop for too long
    // we call this function every current_report_freq_ms, so it will take 10*current_report_freq_ms to get all the readings and send the packet
    if (current_iterator == 0) {
        current_report_packet.data.CURRENT_REPORT.REACTOR_HEATER_CURRENT_mA = machine.getCurrent(); // I2C request 3 bytes
    } else if (current_iterator == 1) {
        current_report_packet.data.CURRENT_REPORT.RGB_CURRENT_mA = machine.getCurrent();
    } else if (current_iterator == 2) {
        current_report_packet.data.CURRENT_REPORT.VALVE_CURRENT_mA = machine.getCurrent();
    } else if (current_iterator == 3) {
        current_report_packet.data.CURRENT_REPORT.MACHINE_CURRENT_mA = machine.getCurrent();
    } else if (current_iterator == 4) {
        current_report_packet.data.CURRENT_REPORT.FAN_CURRENT_mA = machine.getCurrent();
    } else if (current_iterator == 5) {
        current_report_packet.data.CURRENT_REPORT.UV_CURRENT_mA = machine.getCurrent();
    } else if (current_iterator == 6) {
        current_report_packet.data.CURRENT_REPORT.STIR_CURRENT_mA = machine.getCurrent();
    } else if (current_iterator == 7) {
        machine.runRTDADC(); // This makes sure that we've done an RTDADC loop so that it has saved the conversion result from the fastread command in reality not really needed as this gets run every 25ms but here in case those timings change
        current_report_packet.data.CURRENT_REPORT.OD_LED_CURRENT_mA = machine.getLEDCurrent(); // using same ADC as temp sensors, SPI comms
    } else if (current_iterator == 8) {
        current_report_packet.data.CURRENT_REPORT.MIXING_HEATER_CURRENT_mA = machine.getMixingHeaterCurrent(); // from DMA
    } else if (current_iterator == 9) {
        // after we have filled in all the values we send the packet, roughly once a second
        current_report_packet.timestamp = millis();
        teensy_device.sendPacket(&current_report_packet);
    }

    if (current_iterator == 9){
        current_iterator = 0;
    } else {
        current_iterator++;
    }

    // if the next iterator object is 0-6 incl. or 7 then we need to prep the channel at least 50ms before taking the reading 
    // so we do this here so that they are ready for the read on the next cycle
    if (current_iterator <= 6){
        machine.setCurrentChannel(current_iterator);
    } else if (current_iterator == 7){
        // teensy_device.sendInfo("Getting OD LED current 1");
        machine.runRTDADC(false, true, 1); // This runs the RTD ADC to read the OD LED current. It takes approx 15ms to have data ready.
    }

}

void HealthReportHandler(){
    // send memory usage
    // send teensy temperature
    // send all board temperatures
    // send all reactor and mixing bottle temperatures
    // send all reactor and mixing bottle temperature set points
    // send humidity
    // send all board currents

    health_report_packet.unique_id = machine.MEMORY_REPORT_PACKET_ID;

    // note: these values are defined by the linker, they are not valid memory
    // locations in all cases - by defining them as arrays, the C++ compiler
    // will use the address of these definitions - it's a big hack, but there's
    // really no clean way to get at linker-defined symbols from the .ld file
    struct mallinfo mi = mallinfo();

    extern char _ebss[], _heap_end[], *__brkval;

    auto sp = (char*) __builtin_frame_address(0);
    uint32_t stack = (sp - _ebss) >> 10;             // available stack size in kb
    uint32_t heap = (_heap_end - __brkval) >> 10;    // available heap size in kb

    if (health_report_packet.data.MEMORY_REPORT.free_stack != stack) {
        health_report_packet.data.MEMORY_REPORT.free_stack = stack;
        health_report_packet.data.MEMORY_REPORT.has_free_stack = true;
    } else {
        health_report_packet.data.MEMORY_REPORT.has_free_stack = false;
    }

    if (health_report_packet.data.MEMORY_REPORT.free_heap != heap) {
        health_report_packet.data.MEMORY_REPORT.free_heap = heap;
        health_report_packet.data.MEMORY_REPORT.has_free_heap = true;
    } else {
        health_report_packet.data.MEMORY_REPORT.has_free_heap = false;
    }

    if (health_report_packet.data.MEMORY_REPORT.uordblks != mi.uordblks) {
        health_report_packet.data.MEMORY_REPORT.uordblks = mi.uordblks;
        health_report_packet.data.MEMORY_REPORT.has_uordblks = true;
    } else {
        health_report_packet.data.MEMORY_REPORT.uordblks = false;
    }

    if (health_report_packet.data.MEMORY_REPORT.fordblks != mi.fordblks) {
        health_report_packet.data.MEMORY_REPORT.fordblks = mi.fordblks;
        health_report_packet.data.MEMORY_REPORT.has_fordblks = true;
    } else {
        health_report_packet.data.MEMORY_REPORT.fordblks = false;
    }
    
    if (max_process_time_us > health_report_packet.data.MEMORY_REPORT.max_process_time) {
        health_report_packet.data.MEMORY_REPORT.max_process_time = max_process_time_us;
        health_report_packet.data.MEMORY_REPORT.max_process_id   = max_process_id;    
        health_report_packet.data.MEMORY_REPORT.has_max_process_time = true;
        health_report_packet.data.MEMORY_REPORT.has_max_process_id   = true;    
    } else {
        health_report_packet.data.MEMORY_REPORT.has_max_process_time = false;
        health_report_packet.data.MEMORY_REPORT.has_max_process_id   = false;    
    }

    if (max_loop_time_us > health_report_packet.data.MEMORY_REPORT.max_loop_time) {
        health_report_packet.data.MEMORY_REPORT.max_loop_time = max_loop_time_us;
        health_report_packet.data.MEMORY_REPORT.has_max_loop_time = true;
    } else {
        health_report_packet.data.MEMORY_REPORT.has_max_loop_time = false;
    }
    
    health_report_packet.data.MEMORY_REPORT.teensy_temp      = InternalTemperature.readTemperatureC();
    #if GENERATION != V2_MACHINES
        health_report_packet.data.MEMORY_REPORT.rel_humidity_p   = CHIPS.Senseboard_HDC.readContinuous().humidity;
    #endif

    health_report_packet.timestamp = millis();
    teensy_device.sendPacket(&health_report_packet);
}

void ReactorTempReportHandler(){
    reactor_temp_report_packet.unique_id = machine.REACTOR_TEMPERATURE_REPORT_PACKET_ID;
    reactor_temp_report_packet.data.REACTOR_TEMPERATURE_REPORT.AL = machine.reactors[REACTOR::AL].getCuvetteTemp();
    reactor_temp_report_packet.data.REACTOR_TEMPERATURE_REPORT.AR = machine.reactors[REACTOR::AR].getCuvetteTemp();
    reactor_temp_report_packet.data.REACTOR_TEMPERATURE_REPORT.BL = machine.reactors[REACTOR::BL].getCuvetteTemp();
    reactor_temp_report_packet.data.REACTOR_TEMPERATURE_REPORT.BR = machine.reactors[REACTOR::BR].getCuvetteTemp();
    reactor_temp_report_packet.data.REACTOR_TEMPERATURE_REPORT.CL = machine.reactors[REACTOR::CL].getCuvetteTemp();
    reactor_temp_report_packet.data.REACTOR_TEMPERATURE_REPORT.CR = machine.reactors[REACTOR::CR].getCuvetteTemp();
    reactor_temp_report_packet.data.REACTOR_TEMPERATURE_REPORT.DL = machine.reactors[REACTOR::DL].getCuvetteTemp();
    reactor_temp_report_packet.data.REACTOR_TEMPERATURE_REPORT.DR = machine.reactors[REACTOR::DR].getCuvetteTemp();
    // reactor_temp_report_packet.data.REACTOR_TEMPERATURE_REPORT.A = machine.mixing_bottles[MIXING_BOTTLE::A].getMixingTemp();
    // reactor_temp_report_packet.data.REACTOR_TEMPERATURE_REPORT.B = machine.mixing_bottles[MIXING_BOTTLE::B].getMixingTemp();
    // reactor_temp_report_packet.data.TEMPERATURE_REPORT.C = machine.mixing_bottles[MIXING_BOTTLE::C].getMixingTemp();
    // reactor_temp_report_packet.data.TEMPERATURE_REPORT.D = machine.mixing_bottles[MIXING_BOTTLE::D].getMixingTemp();

    reactor_temp_report_packet.timestamp = millis();
    teensy_device.sendPacket(&reactor_temp_report_packet);
}

void OtherTempReportHandler(){
    other_temp_report_packet.unique_id = machine.OTHER_TEMPERATURE_REPORT_PACKET_ID;
    // we break getting the temperature readings into 10 steps to avoid blocking the main loop for too long
    // we call this function every other_report_freq_ms, so it will take 10*other_report_freq_ms to get all the readings and send the packet
    float mixing_temp;

    if (topboard_thermo_mux_iterator == 0) {
        mixing_temp = CHIPS.IR_Temp_Sensor.getObjectTempC();
        machine.mixing_bottles[MIXING_BOTTLE::A].updateMixingTemp(mixing_temp); // update the mixing bottle temp in the mixing object
        other_temp_report_packet.data.OTHER_TEMPERATURE_REPORT.A = mixing_temp;
    } else if (topboard_thermo_mux_iterator == 1) {
        mixing_temp = CHIPS.IR_Temp_Sensor.getObjectTempC();
        machine.mixing_bottles[MIXING_BOTTLE::B].updateMixingTemp(mixing_temp);
        other_temp_report_packet.data.OTHER_TEMPERATURE_REPORT.B = mixing_temp;
    } else if (topboard_thermo_mux_iterator == 2) {
        mixing_temp = CHIPS.IR_Temp_Sensor.getObjectTempC();
        machine.mixing_bottles[MIXING_BOTTLE::C].updateMixingTemp(mixing_temp);
        other_temp_report_packet.data.OTHER_TEMPERATURE_REPORT.C = mixing_temp;
    } else if (topboard_thermo_mux_iterator == 3) {
        mixing_temp = CHIPS.IR_Temp_Sensor.getObjectTempC();
        machine.mixing_bottles[MIXING_BOTTLE::D].updateMixingTemp(mixing_temp);
        other_temp_report_packet.data.OTHER_TEMPERATURE_REPORT.D = mixing_temp;

    #if GENERATION == V2_MACHINES || GENERATION == V3_MACHINES
        } else if (topboard_thermo_mux_iterator == 4) {
            CHIPS.Topboard_TMP_1.updateTemp(); 
            other_temp_report_packet.data.OTHER_TEMPERATURE_REPORT.Topboard12 = CHIPS.Topboard_TMP_1.getTemp();
        } else if (topboard_thermo_mux_iterator == 5) {
            CHIPS.Topboard_TMP_2.updateTemp();
            other_temp_report_packet.data.OTHER_TEMPERATURE_REPORT.Topboard45 = CHIPS.Topboard_TMP_2.getTemp();
        } else if (topboard_thermo_mux_iterator == 6) {
            CHIPS.Topboard_TMP_3.updateTemp();
            other_temp_report_packet.data.OTHER_TEMPERATURE_REPORT.Topboard78 = CHIPS.Topboard_TMP_3.getTemp();
    #endif

    } else if (topboard_thermo_mux_iterator == 7) {
        CHIPS.Senseboard_TMP.updateTemp(); 
        other_temp_report_packet.data.OTHER_TEMPERATURE_REPORT.Senseboard = CHIPS.Senseboard_TMP.getTemp(); // using same ADC as temp sensors, SPI comms
    
        
    } else if (topboard_thermo_mux_iterator == 9) {
        // after we have filled in all the values we send the packet, roughly once a second
        other_temp_report_packet.timestamp = millis();
        teensy_device.sendPacket(&other_temp_report_packet);
    }

    if (topboard_thermo_mux_iterator == 9){
        topboard_thermo_mux_iterator = 0;
    } else {
        topboard_thermo_mux_iterator++;
    }

    // Do the MUX config 1 cycle before taking the new temp
    if (topboard_thermo_mux_iterator <= 6){
        CHIPS.Topboard_Thermo_MUX.setChannel(topboard_thermo_mux_iterator);
    } 

}

void ODOutputTimerFunc() {
    // elapsedMicros elapsed_time = 0; // This is used to measure the time taken to send the data output packet

    od_fast_packet.unique_id = machine.OD_FAST_READINGS_PACKET_ID;
   
    // teensy_device.sendInfo("ERROR: %d %d %d %d %d %d %d %d", raw_fast_od_data[7].error_flag, raw_fast_od_data[6].error_flag, raw_fast_od_data[5].error_flag, raw_fast_od_data[4].error_flag,
    //                                                                                   raw_fast_od_data[0].error_flag, raw_fast_od_data[1].error_flag, raw_fast_od_data[2].error_flag, raw_fast_od_data[3].error_flag);
    // teensy_device.sendInfo("FILTER: %d %d %d %d %d %d %d %d\n", raw_fast_od_data[7].filter_type, raw_fast_od_data[6].filter_type, raw_fast_od_data[5].filter_type, raw_fast_od_data[4].filter_type,
    //                                                                                   raw_fast_od_data[0].filter_type, raw_fast_od_data[1].filter_type, raw_fast_od_data[2].filter_type, raw_fast_od_data[3].filter_type);
    bool AL_adsorption = machine.reactors[REACTOR::AL].isPDAdsorption();
    if (AL_adsorption){
        od_fast_packet.data.FAST_ODS.AL = machine.reactors[REACTOR::AL].AdsorptionVoltage;
        od_fast_packet.data.FAST_ODS.scatter_status &= ~(1 << 0); // clear bit 0 for adsorption mode
    } else {
        od_fast_packet.data.FAST_ODS.AL = machine.reactors[REACTOR::AL].ScatterVoltage;
        od_fast_packet.data.FAST_ODS.scatter_status |=  (1 << 0); // set bit 0 for scatter mode
    }

    bool AR_adsorption = machine.reactors[REACTOR::AR].isPDAdsorption();
    if (AR_adsorption){
        od_fast_packet.data.FAST_ODS.AR = machine.reactors[REACTOR::AR].AdsorptionVoltage;
        od_fast_packet.data.FAST_ODS.scatter_status &= ~(1 << 1); // clear bit 1 for adsorption
    } else {
        od_fast_packet.data.FAST_ODS.AR = machine.reactors[REACTOR::AR].ScatterVoltage;
        od_fast_packet.data.FAST_ODS.scatter_status |=  (1 << 1); // set bit 1 for scatter
    }
    bool BL_adsorption = machine.reactors[REACTOR::BL].isPDAdsorption();
    if (BL_adsorption){
        od_fast_packet.data.FAST_ODS.BL = machine.reactors[REACTOR::BL].AdsorptionVoltage;
        od_fast_packet.data.FAST_ODS.scatter_status &= ~(1 << 2); // clear bit 2 for adsorption
    } else {
        od_fast_packet.data.FAST_ODS.BL = machine.reactors[REACTOR::BL].ScatterVoltage;
        od_fast_packet.data.FAST_ODS.scatter_status |=  (1 << 2); // set bit 2 for scatter
    }
    bool BR_adsorption = machine.reactors[REACTOR::BR].isPDAdsorption();
    if (BR_adsorption){
        od_fast_packet.data.FAST_ODS.BR = machine.reactors[REACTOR::BR].AdsorptionVoltage;
        od_fast_packet.data.FAST_ODS.scatter_status &= ~(1 << 3); // clear bit 3 for adsorption
    } else {
        od_fast_packet.data.FAST_ODS.BR = machine.reactors[REACTOR::BR].ScatterVoltage;
        od_fast_packet.data.FAST_ODS.scatter_status |=  (1 << 3); // set bit 3 for scatter
    }
    bool CL_adsorption = machine.reactors[REACTOR::CL].isPDAdsorption();
    if (CL_adsorption){
        od_fast_packet.data.FAST_ODS.CL = machine.reactors[REACTOR::CL].AdsorptionVoltage;
        od_fast_packet.data.FAST_ODS.scatter_status &= ~(1 << 4); // clear bit 4 for adsorption
    } else {
        od_fast_packet.data.FAST_ODS.CL = machine.reactors[REACTOR::CL].ScatterVoltage;
        od_fast_packet.data.FAST_ODS.scatter_status |=  (1 << 4); // set bit 4 for scatter
    }
    bool CR_adsorption = machine.reactors[REACTOR::CR].isPDAdsorption();
    if (CR_adsorption){
        od_fast_packet.data.FAST_ODS.CR = machine.reactors[REACTOR::CR].AdsorptionVoltage;
        od_fast_packet.data.FAST_ODS.scatter_status &= ~(1 << 5); // clear bit 5 for adsorption
    } else {
        od_fast_packet.data.FAST_ODS.CR = machine.reactors[REACTOR::CR].ScatterVoltage;
        od_fast_packet.data.FAST_ODS.scatter_status |=  (1 << 5); // set bit 5 for scatter
    }
    bool DL_adsorption = machine.reactors[REACTOR::DL].isPDAdsorption();
    if (DL_adsorption){
        od_fast_packet.data.FAST_ODS.DL = machine.reactors[REACTOR::DL].AdsorptionVoltage;
        od_fast_packet.data.FAST_ODS.scatter_status &= ~(1 << 6); // clear bit 6 for adsorption
    } else {
        od_fast_packet.data.FAST_ODS.DL = machine.reactors[REACTOR::DL].ScatterVoltage;
        od_fast_packet.data.FAST_ODS.scatter_status |=  (1 << 6); // set bit 6 for scatter
    }
    bool DR_adsorption = machine.reactors[REACTOR::DR].isPDAdsorption();
    if (DR_adsorption){
        od_fast_packet.data.FAST_ODS.DR = machine.reactors[REACTOR::DR].AdsorptionVoltage;
        od_fast_packet.data.FAST_ODS.scatter_status &= ~(1 << 7); // clear bit 7 for adsorption
    } else {
        od_fast_packet.data.FAST_ODS.DR = machine.reactors[REACTOR::DR].ScatterVoltage;
        od_fast_packet.data.FAST_ODS.scatter_status |=  (1 << 7); // set bit 7 for scatter
    }

   
    // only send the packet if there is new data to send
    od_fast_packet.timestamp = millis();
    // od_fast_packet.data.FAST_ODS.timestamp = od_fast_packet.timestamp;
    teensy_device.sendPacket(&od_fast_packet);
    


}


// float AL_value;
// float AR_value;
// float BL_value;
// float BR_value;
// float CL_value;
// float CR_value;
// float DL_value;
// float DR_value;
// AD7768Data* raw_fast_od_data;
//
// void ODOutputTimerFunc() {
//     // elapsedMicros elapsed_time = 0; // This is used to measure the time taken to send the data output packet
//     od_fast_packet.unique_id = machine.OD_FAST_READINGS_PACKET_ID;
//     od_fast_packet.data.FAST_ODS.scatter_status =   uint8_t(
//                                                     !machine.reactors[REACTOR::DR].isPDAdsorption()<<7 | 
//                                                     !machine.reactors[REACTOR::DL].isPDAdsorption()<<6 | 
//                                                     !machine.reactors[REACTOR::CR].isPDAdsorption()<<5 | 
//                                                     !machine.reactors[REACTOR::CL].isPDAdsorption()<<4 |
//                                                     !machine.reactors[REACTOR::BR].isPDAdsorption()<<3 | 
//                                                     !machine.reactors[REACTOR::BL].isPDAdsorption()<<2 | 
//                                                     !machine.reactors[REACTOR::AR].isPDAdsorption()<<1 | 
//                                                     !machine.reactors[REACTOR::AL].isPDAdsorption()<<0);
//     raw_fast_od_data = CHIPS.Senseboard_ADC.getADCData();
//     // teensy_device.sendInfo("ERROR: %d %d %d %d %d %d %d %d", raw_fast_od_data[7].error_flag, raw_fast_od_data[6].error_flag, raw_fast_od_data[5].error_flag, raw_fast_od_data[4].error_flag,
//     //                                                                                   raw_fast_od_data[0].error_flag, raw_fast_od_data[1].error_flag, raw_fast_od_data[2].error_flag, raw_fast_od_data[3].error_flag);
//     // teensy_device.sendInfo("FILTER: %d %d %d %d %d %d %d %d\n", raw_fast_od_data[7].filter_type, raw_fast_od_data[6].filter_type, raw_fast_od_data[5].filter_type, raw_fast_od_data[4].filter_type,
//     //                                                                                   raw_fast_od_data[0].filter_type, raw_fast_od_data[1].filter_type, raw_fast_od_data[2].filter_type, raw_fast_od_data[3].filter_type);
//     // Reactor AL = Channel 7
//     AL_value = float(raw_fast_od_data[7].value) * 1.25 / 8388607.0;
//     // if (bool(raw_fast_od_data[7].repeated_data) or bool(raw_fast_od_data[7].error_flag) or bool(raw_fast_od_data[7].filter_not_settled)){
//     if (bool(raw_fast_od_data[7].repeated_data) or bool(raw_fast_od_data[7].filter_not_settled) and (machine.reactors[REACTOR::AL].outputOD)){
//         od_fast_packet.data.FAST_ODS.has_AL = false; // dont send any data if its repeated data, because if it is repeated data then its value gets set to 0 which is obvs not a true reading
//     } else {
//         od_fast_packet.data.FAST_ODS.has_AL = true;
//         od_fast_packet.data.FAST_ODS.AL = AL_value;     
//     }
//
//     // Reactor AR = Channel 6
//     AR_value = float(raw_fast_od_data[6].value) * 1.25 / 8388607.0;
//     // if (bool(raw_fast_od_data[6].repeated_data) or bool(raw_fast_od_data[6].error_flag) or bool(raw_fast_od_data[6].filter_not_settled)){
//     if (bool(raw_fast_od_data[6].repeated_data) or bool(raw_fast_od_data[6].filter_not_settled) and machine.reactors[REACTOR::AR].outputOD){
//         od_fast_packet.data.FAST_ODS.has_AR = false;
//     } else {
//         od_fast_packet.data.FAST_ODS.has_AR = true;
//         od_fast_packet.data.FAST_ODS.AR = AR_value;
//     }
//
//     // Reactor BL = Channel 5
//     BL_value = float(raw_fast_od_data[5].value) * 1.25 / 8388607.0;
//     // if (bool(raw_fast_od_data[5].repeated_data) or bool(raw_fast_od_data[5].error_flag) or bool(raw_fast_od_data[5].filter_not_settled)){
//     if (bool(raw_fast_od_data[5].repeated_data) or bool(raw_fast_od_data[5].filter_not_settled) and machine.reactors[REACTOR::BL].outputOD){
//         od_fast_packet.data.FAST_ODS.has_BL = false;
//     } else {
//         od_fast_packet.data.FAST_ODS.has_BL = true;
//         od_fast_packet.data.FAST_ODS.BL = BL_value;
//     }
//    
//     // Reactor BR = Channel 4
//     BR_value = float(raw_fast_od_data[4].value) * 1.25 / 8388607.0;
//     // if (bool(raw_fast_od_data[4].repeated_data) or bool(raw_fast_od_data[4].error_flag) or bool(raw_fast_od_data[4].filter_not_settled)){
//     if (bool(raw_fast_od_data[4].repeated_data) or bool(raw_fast_od_data[4].filter_not_settled) and machine.reactors[REACTOR::BR].outputOD){
//         od_fast_packet.data.FAST_ODS.has_BR = false;
//     } else {
//         od_fast_packet.data.FAST_ODS.has_BR = true;
//         od_fast_packet.data.FAST_ODS.BR = BR_value;
//     }
//
//     // Reactor CL = Channel 0
//     CL_value = float(raw_fast_od_data[0].value) * 1.25 / 8388607.0;
//     // if (bool(raw_fast_od_data[0].repeated_data) or bool(raw_fast_od_data[0].error_flag) or bool(raw_fast_od_data[0].filter_not_settled)){
//     if (bool(raw_fast_od_data[0].repeated_data) or bool(raw_fast_od_data[0].filter_not_settled) and machine.reactors[REACTOR::CL].outputOD){
//         od_fast_packet.data.FAST_ODS.has_CL = false;
//     } else {
//         od_fast_packet.data.FAST_ODS.has_CL = true;
//         od_fast_packet.data.FAST_ODS.CL = CL_value;     
//     }
//
//     // Reactor CR = Channel 1
//     CR_value = float(raw_fast_od_data[1].value) * 1.25 / 8388607.0;
//     // if (bool(raw_fast_od_data[1].repeated_data) or bool(raw_fast_od_data[1].error_flag) or bool(raw_fast_od_data[1].filter_not_settled)){
//     if (bool(raw_fast_od_data[1].repeated_data) or bool(raw_fast_od_data[1].filter_not_settled) and machine.reactors[REACTOR::CR].outputOD){
//         od_fast_packet.data.FAST_ODS.has_CR = false;
//     } else {
//         od_fast_packet.data.FAST_ODS.has_CR = true;
//         od_fast_packet.data.FAST_ODS.CR = CR_value;
//     }
//
//     // Reactor DL = Channel 2
//     // TODO set this to always TRUE after receiving a debug command and false after receiving it again, so we can test if it sends a 0 or nothing
//     DL_value = float(raw_fast_od_data[2].value) * 1.25 / 8388607.0;
//     // if (bool(raw_fast_od_data[2].repeated_data ) or bool(raw_fast_od_data[2].error_flag) or bool(raw_fast_od_data[2].filter_not_settled)){
//     if (bool(raw_fast_od_data[2].repeated_data ) or bool(raw_fast_od_data[2].filter_not_settled and machine.reactors[REACTOR::DL].outputOD)){
//         od_fast_packet.data.FAST_ODS.has_DL = false;
//     } else {
//         od_fast_packet.data.FAST_ODS.has_DL = true;
//         od_fast_packet.data.FAST_ODS.DL = DL_value;
//     }
//
//     // Reactor DR = Channel 3
//     DR_value = float(raw_fast_od_data[3].value) * 1.25 / 8388607.0;
//     // if (bool(raw_fast_od_data[3].repeated_data) or bool(raw_fast_od_data[3].error_flag) or bool(raw_fast_od_data[3].filter_not_settled)){
//     if (bool(raw_fast_od_data[3].repeated_data) or bool(raw_fast_od_data[3].filter_not_settled) and machine.reactors[REACTOR::DR].outputOD){
//         od_fast_packet.data.FAST_ODS.has_DR = false;
//     } else {
//         od_fast_packet.data.FAST_ODS.has_DR = true;
//         od_fast_packet.data.FAST_ODS.DR = DR_value;
//     }
//    
//     if (od_fast_packet.data.FAST_ODS.has_AL || od_fast_packet.data.FAST_ODS.has_AR || od_fast_packet.data.FAST_ODS.has_BL || od_fast_packet.data.FAST_ODS.has_BR ||
//         od_fast_packet.data.FAST_ODS.has_CL || od_fast_packet.data.FAST_ODS.has_CR || od_fast_packet.data.FAST_ODS.has_DL || od_fast_packet.data.FAST_ODS.has_DR) {
//
//         // only send the packet if there is new data to send
//         od_fast_packet.timestamp = millis();
//         // od_fast_packet.data.FAST_ODS.timestamp = od_fast_packet.timestamp;
//         teensy_device.sendPacket(&od_fast_packet);
//     }
// }

// TODO: im sure this function can be majorly simplified...
void ODOutputIntIO(int32_t update_freq_ms) {
    if (update_freq_ms != OD_output_freq_ms) {
        OD_output_freq_ms = update_freq_ms; // Update the frequency of the data output interrupt

        if (update_freq_ms > 0) {
            OD_output_timer.interval(update_freq_ms);
            OD_output_timer.reset();

            machine.SEND_FAST_OD_READINGS = true;
            teensy_device.sendDebug("OD interrupt set to %d ms", OD_output_freq_ms);

        } else {
            if (machine.SEND_FAST_OD_READINGS){
                machine.SEND_FAST_OD_READINGS = false;
                teensy_device.sendInfo("OD data output interrupt disabled");
            } else {
                teensy_device.sendInfo("OD interrupt already disabled");
            }
        }
    } else {
        if (OD_output_freq_ms > 0) {
            teensy_device.sendInfo("OD interrupt already set to %d ms", OD_output_freq_ms);
        } else {
            teensy_device.sendInfo("OD interrupt already disabled");
        }
    }
}


float weight_A_value;
float weight_B_value;
float weight_C_value;
float weight_D_value;
float weight_WASTE_value;
AD7768Data* raw_fast_weight_data;

// elapsedMillis elapsed_weight_time = 0; // This is used to measure the time taken to send the data output packet
void WeightOutputTimerFunc() {

    weight_fast_packet.unique_id = machine.WEIGHT_FAST_READINGS_PACKET_ID;

    raw_fast_weight_data = CHIPS.Topboard_ADC.getADCData();

    weight_A_value = float(raw_fast_weight_data[0].value) * 1.33 / 8388607.0;
    if (bool(raw_fast_weight_data[0].repeated_data) || weight_fast_packet.data.FAST_WEIGHTS.A == weight_A_value) {
        weight_fast_packet.data.FAST_WEIGHTS.has_A = false;
    } else {
        weight_fast_packet.data.FAST_WEIGHTS.has_A = true;
        weight_fast_packet.data.FAST_WEIGHTS.A = weight_A_value;
    }

    weight_B_value = float(raw_fast_weight_data[1].value) * 1.33 / 8388607.0;
    if (bool(raw_fast_weight_data[1].repeated_data) || weight_fast_packet.data.FAST_WEIGHTS.B == weight_B_value) {
        weight_fast_packet.data.FAST_WEIGHTS.has_B = false;
    } else {
        weight_fast_packet.data.FAST_WEIGHTS.has_B = true;
        weight_fast_packet.data.FAST_WEIGHTS.B = weight_B_value;
    }

    weight_C_value = float(raw_fast_weight_data[2].value) * 1.33 / 8388607.0;
    if (bool(raw_fast_weight_data[2].repeated_data) || weight_fast_packet.data.FAST_WEIGHTS.C == weight_C_value) {
        weight_fast_packet.data.FAST_WEIGHTS.has_C = false;
    } else {
        weight_fast_packet.data.FAST_WEIGHTS.has_C = true;
        weight_fast_packet.data.FAST_WEIGHTS.C = weight_C_value;
    }

    weight_D_value = float(raw_fast_weight_data[3].value) * 1.33 / 8388607.0;
    if (bool(raw_fast_weight_data[3].repeated_data) || weight_fast_packet.data.FAST_WEIGHTS.D == weight_D_value) {
        weight_fast_packet.data.FAST_WEIGHTS.has_D = false;
    } else {
        weight_fast_packet.data.FAST_WEIGHTS.has_D = true;
        weight_fast_packet.data.FAST_WEIGHTS.D = weight_D_value;
    }

    weight_WASTE_value = float(raw_fast_weight_data[4].value) * 1.33 / 8388607.0;
    if (bool(raw_fast_weight_data[4].repeated_data) || weight_fast_packet.data.FAST_WEIGHTS.WASTE == weight_WASTE_value) {
        weight_fast_packet.data.FAST_WEIGHTS.has_WASTE = false;
    } else {
        weight_fast_packet.data.FAST_WEIGHTS.has_WASTE = true;
        weight_fast_packet.data.FAST_WEIGHTS.WASTE = weight_WASTE_value;
    }

    if (weight_fast_packet.data.FAST_WEIGHTS.has_A || 
        weight_fast_packet.data.FAST_WEIGHTS.has_B ||
        weight_fast_packet.data.FAST_WEIGHTS.has_C || 
        weight_fast_packet.data.FAST_WEIGHTS.has_D || 
        weight_fast_packet.data.FAST_WEIGHTS.has_WASTE) {
        
        // only send the packet if there is new data to send
        weight_fast_packet.data.FAST_WEIGHTS.timestamp = weight_fast_packet.timestamp;
        weight_fast_packet.timestamp = millis();
        teensy_device.sendPacket(&weight_fast_packet);

    }

    // elapsed_weight_time = 0;
}
// TODO: im sure this function can be majorly simplified...
void WeightOutputIntIO(int32_t update_freq_ms) {
    if (update_freq_ms != weight_output_freq_ms) {
        weight_output_freq_ms = update_freq_ms; // Update the frequency of the data output interrupt

        if (update_freq_ms > 0) {

            weight_output_timer.interval(update_freq_ms);
            weight_output_timer.reset();

            machine.SEND_FAST_WEIGHT_READINGS = true;
            teensy_device.sendDebug("Weight interrupt set to %d ms", weight_output_freq_ms);

        } else {
            if (machine.SEND_FAST_WEIGHT_READINGS){
                machine.SEND_FAST_WEIGHT_READINGS = false;
                teensy_device.sendInfo("Weight data output interrupt disabled");
            } else {
                teensy_device.sendInfo("Weight interrupt already disabled");
            }
        }

    } else {
        if (weight_output_freq_ms > 0) {
            teensy_device.sendInfo("Weight interrupt already set to %d ms", weight_output_freq_ms);
        } else {
            teensy_device.sendInfo("Weight interrupt already disabled");
        }
    }
}
int IOInterruptCount = 0;

void IOInterruptHandler() {
    // This function is called when one of the fault signals is triggered.
    // The fault signals all have pull up resistors so they are nominally high, and go low when a fault is triggered.
    // When the pca9505 is pulled low, its setup up with the interrupt mask to set its interrupt pin low, which triggers this interrupt.
    // To clear the interrupt pin on the pca9505 we must read the input register associated to that pin
    //
    // The valve and heater faults are cleared by toggling the input off and on, 
    //  The heaters are driven by a pwm signal so this will happen automatically
    //  The valves are driven by a digital signal so we must do this manually 
    // The Stirring related faults are cleared automatically by the chip when StirEnable pins on the IO chips are set as output high, if set to inputs they are latched
    //   However a latch is only cleared by setting sleep pin low and high again, which we cannot do. So best to keep auto retry

    // Process will be when the interrupt triggered, read all the io pins set to input.
    // If there is an input pin that is low, then this is causing the fault line to be triggered
    // If this pin is stirring related then just send a message like stir fault detected
    // If this pin is a heater, then also send a message like heater fault detected
    // If this pin is a valve, send the valve fault detected, and then toggle the related io output off and then on
    
    // loop through all pins of the chip
    // if (IOInterruptCount <= 3){
    teensy_device.sendError("IO Interrupt Triggered %d", IOInterruptCount);

    CHIPS.Heatboard_IO_1.clearInterrupt();
    CHIPS.Heatboard_IO_2.clearInterrupt();
    CHIPS.Heatboard_IO_3.clearInterrupt();
    CHIPS.Heatboard_IO_4.clearInterrupt();
    // CHIPS.Topboard_IO_1.clearInterrupt();
    // CHIPS.Topboard_IO_2.clearInterrupt();
    CHIPS.Senseboard_IO_1.clearInterrupt();
    // } else {
    //     teensy_device.sendError("IO Interrupt Triggered");
    // }
    IOInterruptCount++;
    // CHIPS.Heatboard_IO_1.clearInterrupt();
    // CHIPS.Heatboard_IO_2.clearInterrupt();
    // CHIPS.Heatboard_IO_3.clearInterrupt();
    // CHIPS.Heatboard_IO_4.clearInterrupt();
    // CHIPS.Topboard_IO_1.clearInterrupt();
    // CHIPS.Topboard_IO_2.clearInterrupt();
    // CHIPS.Senseboard_IO_1.clearInterrupt();
}
void IOInterruptHandler2() {
    CHIPS.Heatboard_IO_1.clearInterrupt();
    CHIPS.Heatboard_IO_2.clearInterrupt();
    CHIPS.Heatboard_IO_3.clearInterrupt();
    CHIPS.Heatboard_IO_4.clearInterrupt();
    // CHIPS.Topboard_IO_1.clearInterrupt();
    // CHIPS.Topboard_IO_2.clearInterrupt();
    CHIPS.Senseboard_IO_1.clearInterrupt();
}

void AccelInterruptHandler(){
    CHIPS.Senseboard_ACCEL.clearInterrupt();
}

void sendFluidDetectedPacket(uint64_t packet_id, bool fluid_detection_successful){
    Packet packet = Packet_init_zero;
    packet.type = PacketType_DATA;

    packet.system = SystemName_MACHINE;

    packet.has_unique_id = true;
    packet.unique_id = packet_id;

    packet.has_data = true;
    packet.data.has_sendAsyncFluidDetected = true;

    // Set the fluid sensor detected value to true
    packet.data.sendAsyncFluidDetected = fluid_detection_successful;

    teensy_device.sendPacket(&packet);
}

elapsedMillis fluid_sensor_detect_cutoff_timer;
elapsedMillis change_debounce_timer; // This is used to debounce the fluid sensor change detection
IntervalTimer fluid_sensor_timer;
bool fluid_change_detected = false;
bool initial_reading_stabilised = false; // This is used to check if the initial reading has stabilised before starting the fluid sensor detection
int reading_number = 0; // This is used to count the number of readings taken

constexpr int stab_readings = 20;
float sensor_readings[stab_readings]; 
// This function is run as an interrupt every X seconds to check the state of the fluid sensors, the interrupt is enabled by DETECT_FLUID_TRANSITION_IO and disables itself when detected
void FluidSensorInterruptHandler(){ 

    // first get an initial reading
    if (initial_reading_stabilised == false) {
        // teensy_device.sendInfo("Initial reading stabilised");
        if (reading_number >= stab_readings) {
            float max_val = 0;
            float min_val = sensor_readings[reading_number-1];
            for (int i = 0; i < stab_readings; i++) {
                if (sensor_readings[i] > max_val) max_val = sensor_readings[i];
                if (sensor_readings[i] < min_val) min_val = sensor_readings[i];
            }
            teensy_device.sendInfo("Max: %f Min: %f", max_val, min_val);

            if ((max_val - min_val) < machine.fluid_sensor_stabilisation_threshold) { // Check if the difference between the max and min values is less than the threshold
                teensy_device.sendInfo("Initial reading stabilised after %d ms", fluid_sensor_detect_cutoff_timer);
                machine.fluid_sensor_init_value = machine.getFluidSensorVoltage(); // Set the initial value to the average of the max and min values
                initial_reading_stabilised = true;
                teensy_device.sendInfo("Initial reading: %f", machine.fluid_sensor_init_value);
            }
            reading_number = 0; // Reset the reading number if it exceeds 10
        }
        
        sensor_readings[reading_number] = machine.getFluidSensorVoltage(); // Get the ADC value from the fluid sensor
        reading_number++;
        
    } else {

        float current_value = machine.getFluidSensorVoltage(); // Get the ADC value from the fluid sensor

        // Check if the fluid sensor has detected a change, if it has we can start the debounce timer
        if ((fluid_change_detected == false) && (abs(machine.fluid_sensor_init_value - current_value) > machine.fluid_sensor_change_threshold)) { // Check if the change is greater than the threshold
            change_debounce_timer = 0;
            fluid_change_detected = true;
        }

        if (fluid_change_detected && (abs(machine.fluid_sensor_init_value - current_value) < machine.fluid_sensor_change_threshold)) { // Check if the change is greater than the threshold
            fluid_change_detected = false;
            teensy_device.sendInfo("Detection Reset");
        }

        if (fluid_change_detected && (abs(machine.fluid_sensor_init_value - current_value) > machine.fluid_sensor_change_threshold) && (change_debounce_timer > machine.fluid_sensor_debounce_time_ms)) { // Check if the change is greater than the debounce time
            teensy_device.sendInfo("Fluid sensor detected a change in fluid level");
            sendFluidDetectedPacket(machine.fluid_sensor_packet_id, true);
            // teensy_device.sendData(SystemName_MACHINE, RequestedData_FLUID_SENSOR_DETECT_tag, true);
            fluid_sensor_timer.end(); // Stop the interrupt if the changed level persists for debounce time
            fluid_change_detected = false;
        }

    }

    // End the timer if we have not detected a change in the fluid sensor for a certain amount of time
    if (fluid_sensor_detect_cutoff_timer > machine.fluid_sensor_cutoff_time_ms) {
        teensy_device.sendInfo("No fluid change detected within the %dms timeout", machine.fluid_sensor_cutoff_time_ms);
        // teensy_device.sendData(SystemName_MACHINE, RequestedData_FLUID_SENSOR_DETECT_tag, false);
        fluid_sensor_timer.end(); // Stop the interrupt if a change is detected
        sendFluidDetectedPacket(machine.fluid_sensor_packet_id, false);
        return;
    }
}

/*
request fluid sensor transition for specific sensor

choose which fluid sensor you want

set asyncfluiddetect to true
*/

void sendPIDPacket(SystemName system, uint64_t packet_id){
    Packet packet = Packet_init_zero;
    packet.type = PacketType_DATA;

    packet.system = system;

    packet.has_unique_id = true;
    packet.unique_id = packet_id;

    packet.has_data = true;
    packet.data.has_PID = true;

    packet.data.PID.has_error_p                = true;
    packet.data.PID.has_error_i                = true;
    packet.data.PID.has_error_d                = true;
    packet.data.PID.has_kp                     = true;
    packet.data.PID.has_ki                     = true;
    packet.data.PID.has_kd                     = true;
    packet.data.PID.has_setpoint               = true;
    packet.data.PID.has_temperature            = true;
    packet.data.PID.has_duty_cycle             = true;
    packet.data.PID.has_timestamp              = true;

    packet.has_timestamp = true;
    packet.timestamp = millis();

    if (        packet.system == SystemName_REACTOR_AL || packet.system == SystemName_REACTOR_AR ||
                packet.system == SystemName_REACTOR_BL || packet.system == SystemName_REACTOR_BR ||
                packet.system == SystemName_REACTOR_CL || packet.system == SystemName_REACTOR_CR ||
                packet.system == SystemName_REACTOR_DL || packet.system == SystemName_REACTOR_DR) {
        packet.data.PID.error_p                = machine.reactors[rmap[packet.system]].p_term;
        packet.data.PID.error_i                = machine.reactors[rmap[packet.system]].i_term;
        packet.data.PID.error_d                = machine.reactors[rmap[packet.system]].d_term;
        packet.data.PID.kp                     = machine.reactors[rmap[packet.system]].kp;
        packet.data.PID.ki                     = machine.reactors[rmap[packet.system]].ki;
        packet.data.PID.kd                     = machine.reactors[rmap[packet.system]].kd;
        packet.data.PID.setpoint               = machine.reactors[rmap[packet.system]].getTargetTemp();
        packet.data.PID.temperature            = machine.reactors[rmap[packet.system]].getCuvetteTemp();
        packet.data.PID.duty_cycle             = machine.reactors[rmap[packet.system]].getCuvetteHeaterDutyCycle();

    } else if (packet.system == SystemName_MIXING_A || packet.system == SystemName_MIXING_B ||
                packet.system == SystemName_MIXING_C || packet.system == SystemName_MIXING_D) {
        packet.data.PID.error_p                = machine.mixing_bottles[mmap[packet.system]].p_term;
        packet.data.PID.error_i                = machine.mixing_bottles[mmap[packet.system]].i_term;
        packet.data.PID.error_d                = machine.mixing_bottles[mmap[packet.system]].d_term;
        packet.data.PID.kp                     = machine.mixing_bottles[mmap[packet.system]].kp;
        packet.data.PID.ki                     = machine.mixing_bottles[mmap[packet.system]].ki;
        packet.data.PID.kd                     = machine.mixing_bottles[mmap[packet.system]].kd;
        packet.data.PID.setpoint               = machine.mixing_bottles[mmap[packet.system]].getTargetTemp();
        packet.data.PID.temperature            = machine.mixing_bottles[mmap[packet.system]].getMixingTemp();
        packet.data.PID.duty_cycle             = machine.mixing_bottles[mmap[packet.system]].getHeaterDuty();
    }
    
    teensy_device.sendPacket(&packet);

}

void ReactorHeaterTimerFunc(){
    // If the heater of a reactor/mixer is turned on, run the PID to make sure its at its set temperature
    // this runs every RTD_ADC_update_freq_ms * 8 ms since every state machine loop updates one temperature sensor, so new temperatures for all 8 are only retrieved 8x this time.
    
    SystemName reactor_names[8] = {SystemName_REACTOR_AL, SystemName_REACTOR_AR, SystemName_REACTOR_BL, SystemName_REACTOR_BR,
                                    SystemName_REACTOR_CL, SystemName_REACTOR_CR, SystemName_REACTOR_DL, SystemName_REACTOR_DR};
    
    // loops through all the reactors and runs the PID loop if the PID is enabled,
    // and sends the PID packet if the reactor is set to send it
    for (uint16_t i = 0; i < sizeof(reactor_names); i++) {
        if (machine.reactors[rmap[reactor_names[i]]].getPIDIO()) {

            machine.reactors[rmap[reactor_names[i]]].HeaterPIDLoop();
        }
        if (machine.reactors[rmap[reactor_names[i]]].send_pid_packets) {
            sendPIDPacket(reactor_names[i], machine.reactors[rmap[reactor_names[i]]].pid_packet_id);
        }    
    }
    teensy_device.sendDebug("%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f",
                            machine.reactors[REACTOR::AL].getCuvetteTemp(),
                            machine.reactors[REACTOR::AR].getCuvetteTemp(),
                            machine.reactors[REACTOR::BL].getCuvetteTemp(),
                            machine.reactors[REACTOR::BR].getCuvetteTemp(),
                            machine.reactors[REACTOR::CL].getCuvetteTemp(),
                            machine.reactors[REACTOR::CR].getCuvetteTemp(),
                            machine.reactors[REACTOR::DL].getCuvetteTemp(),
                            machine.reactors[REACTOR::DR].getCuvetteTemp());
}

void MixingHeaterTimerFunc(){
    // this is a separate function as the temperature measurements are made over i2c
    // so we want to run this slower than the reactor loop so as to not flood the i2c bus 
    SystemName mixing_names[4] = {SystemName_MIXING_A, SystemName_MIXING_B, SystemName_MIXING_C, SystemName_MIXING_D};
    
    // loops through all the mixing bottles and runs the PID loop if the PID is enabled,
    // and sends the PID packet if the reactor is set to send it
    for (uint16_t i = 0; i < sizeof(mixing_names); i++) {
        if (machine.mixing_bottles[mmap[mixing_names[i]]].getPIDIO()) {

            machine.mixing_bottles[mmap[mixing_names[i]]].HeaterPIDLoop();
        }
        if (machine.mixing_bottles[mmap[mixing_names[i]]].send_pid_packets) {
            sendPIDPacket(mixing_names[i], machine.mixing_bottles[mmap[mixing_names[i]]].pid_packet_id);
        }    
        
    }
}

elapsedMicros od_interrupt_time;
elapsedMicros interrupt_interval; 
uint max_interrupt_interval;
uint min_interrupt_interval;
uint max_od_interrupt_time = 0;
uint8_t iteration = 0;

void ODReadyInterrupt() {
    od_interrupt_time=0;
    dmaChannelOD.clearInterrupt();  
    if (interrupt_interval > max_interrupt_interval){
        max_interrupt_interval = interrupt_interval;
    }
    if (interrupt_interval < min_interrupt_interval){
        min_interrupt_interval = interrupt_interval;
    }
    interrupt_interval = 0;

    /*
    Aim of this function is to save the ADC bits as voltages as soon as they are ready onto the reactors
    This will get called based on the ODR of the AD7768 on the senseboard either:
        // ODR = 1.2288 MHz / (1024 * 8) = 150 SPS every 6.67 ms
        // ODR = 1.2288 MHz / (512 * 32) = 75 SPS every 13.3 ms
    So would want this to be on the order of microseconds really, measured runtime as max 3us
    */
    // arm_dcache_delete((void*)dmaBufferOD, sizeof(dmaBufferOD));
    uint64_t non_volatile_ods[DMABUFFER_SIZE];
    // memcpy(non_volatile_ods, dmaBufferOD, sizeof(dmaBufferOD));
    for (int i = 0; i < DMABUFFER_SIZE; i++) {
        non_volatile_ods[i] = dmaBufferOD[i];
    }

    // // Accessor-style macros: use like error_flag(bits) to get the bitfield value
    #define error_flag(bits)           (bool)((((bits) >> 31) & 0b1))
    #define filter_not_settled(bits)   (bool)((((bits) >> 30) & 0b1))
    #define repeated_data(bits)        (bool)((((bits) >> 29) & 0b1))
    #define filter_type(bits)          (bool)((((bits) >> 28) & 0b1))
    #define filter_saturated(bits)     (bool)((((bits) >> 27) & 0b1))
    #define channel_ID_of(bits)        (uint8_t)((((bits) >> 24) & 0x7))
    #define raw_value_of(bits)         (uint32_t)((bits) & 0x00FFFFFF)
    #define channel_value_of(bits)     (int32_t)( (((bits) & 0x800000) ? int32_t((bits) | 0xFF000000) : int32_t((bits) & 0x00FFFFFF)) )
    // will try 1 second window to start with
    const float alpha = 0.03; // smoothing factor for the moving average calculation time window roughly equal to (2/alpha) - 1 samples, each of our samples is ODR apart (in worst case 16.67ms) 10 second average window would be 600 samples which would be alpha - 0.003
    // need some kind of anomoly detected and rejection here otherwise when an anomaly occurs it skews the next 1 seconds worth of results 
    #define PROCESS_CHANNEL(reactor, bits, now_time)                           \
        if(machine.reactors[reactor].allow_od_readings and !error_flag(bits) and !filter_not_settled(bits) and !repeated_data(bits) and (raw_value_of(bits) != 0) and (machine.reactors[reactor].scatter_adsorption_switch_delay_timer > machine.reactors[reactor].scatter_adsorption_switch_delay_ms)){ \
            if (machine.reactors[reactor].isPDAdsorption()){ \
                if (abs( (float(channel_value_of(bits)) * 1.25 / 8388607.0)) > abs(machine.reactors[reactor].max_ad_voltage)){\
                    machine.reactors[reactor].max_ad_voltage =  (float(channel_value_of(bits)) * 1.25 / 8388607.0);\
                }\
                if (abs( (float(channel_value_of(bits)) * 1.25 / 8388607.0)) < abs(machine.reactors[reactor].min_ad_voltage)){\
                    machine.reactors[reactor].min_ad_voltage =  (float(channel_value_of(bits)) * 1.25 / 8388607.0);\
                }\
                machine.reactors[reactor].AdsorptionVoltage = float(channel_value_of(bits)) * 1.25 / 8388607.0;    \
                machine.reactors[reactor].AdsorptionTimestamp = now_time; \
                if (machine.reactors[reactor].first_reading_after_pd_switch) { \
                    machine.reactors[reactor].average_ad_voltage = machine.reactors[reactor].AdsorptionVoltage; \
                    machine.reactors[reactor].first_reading_after_pd_switch = false; \
                } else { \
                    machine.reactors[reactor].average_ad_voltage = ((1-alpha)*machine.reactors[reactor].average_ad_voltage + (alpha)*machine.reactors[reactor].AdsorptionVoltage); \
                } \
                machine.reactors[reactor].ScatterVoltage = 0.0;    \
                machine.reactors[reactor].ScatterTimestamp = 0.0; \
            } \
            else { \
                if (abs( (float(channel_value_of(bits)) * 1.25 / 8388607.0)) > abs(machine.reactors[reactor].max_scatter_voltage)){\
                    machine.reactors[reactor].max_scatter_voltage =  (float(channel_value_of(bits)) * 1.25 / 8388607.0);\
                }\
                if (abs( (float(channel_value_of(bits)) * 1.25 / 8388607.0)) < abs(machine.reactors[reactor].min_scatter_voltage)){\
                    machine.reactors[reactor].min_scatter_voltage =  (float(channel_value_of(bits)) * 1.25 / 8388607.0);\
                }\
                machine.reactors[reactor].ScatterVoltage = float(channel_value_of(bits)) * 1.25 / 8388607.0;    \
                machine.reactors[reactor].ScatterTimestamp = now_time; \
                if (machine.reactors[reactor].first_reading_after_pd_switch) { \
                    machine.reactors[reactor].average_scatter_voltage = machine.reactors[reactor].ScatterVoltage; \
                    machine.reactors[reactor].first_reading_after_pd_switch = false; \
                } else { \
                    machine.reactors[reactor].average_scatter_voltage = ((1-alpha)*machine.reactors[reactor].average_scatter_voltage + (alpha)*machine.reactors[reactor].ScatterVoltage); \
                } \
                machine.reactors[reactor].AdsorptionVoltage = 0.0;    \
                machine.reactors[reactor].AdsorptionTimestamp = 0.0; \
            } \
        } else {\
            machine.reactors[reactor].AdsorptionVoltage = 0.0;    \
            machine.reactors[reactor].AdsorptionTimestamp = 0.0; \
            machine.reactors[reactor].ScatterVoltage = 0.0;    \
            machine.reactors[reactor].ScatterTimestamp = 0.0; \
        }

    PROCESS_CHANNEL(REACTOR::AL, uint32_t(non_volatile_ods[3]>>32), millis());
    PROCESS_CHANNEL(REACTOR::AR, uint32_t(non_volatile_ods[3]), millis());

    PROCESS_CHANNEL(REACTOR::BL, uint32_t(non_volatile_ods[2] >> 32), millis());
    PROCESS_CHANNEL(REACTOR::BR, uint32_t(non_volatile_ods[2]), millis());

    PROCESS_CHANNEL(REACTOR::CL, uint32_t(non_volatile_ods[0]), millis());
    PROCESS_CHANNEL(REACTOR::CR, uint32_t(non_volatile_ods[0] >> 32), millis());

    PROCESS_CHANNEL(REACTOR::DL, uint32_t(non_volatile_ods[1]), millis());
    PROCESS_CHANNEL(REACTOR::DR, uint32_t(non_volatile_ods[1] >> 32), millis());

    #undef PROCESS_CHANNEL
    #undef error_flag
    #undef filter_not_settled
    #undef repeated_data
    #undef filter_type
    #undef filter_saturated
    #undef channel_ID_of
    #undef raw_value_of
    #undef channel_value_of
    if (od_interrupt_time > max_od_interrupt_time){
        max_od_interrupt_time = od_interrupt_time;
    }
    
}

/////////////////////////////////////////
// OTHER SETUP //////////////////////////
/////////////////////////////////////////

void setupFlexIOInput()
{
    // This code creates an interrupt each time the shifter buffer is filled
    // #define FLEXIO_ISR_PRIORITY 64 // interrupt is timing sensitive, so use relatively high priority (supersedes USB)
    // FlexIOHandler *pFlex;
    // const FlexIOHandler::FLEXIO_Hardware_t *hw;
    // pFlex = FlexIOHandler::flexIOHandler_list[1];
    // hw = &pFlex->hardware();
    // attachInterruptVector(hw->flex_irq, isr);
    // NVIC_ENABLE_IRQ(hw->flex_irq);
    // NVIC_SET_PRIORITY(hw->flex_irq, FLEXIO_ISR_PRIORITY);
    // FLEXIO2_SHIFTSIEN	=   1	;		// enable shifter status flag interrupt
    /*
    Shifter Operation: 
        The shifter shifts data in
        When a store event is triggered by the timer, the shifter buffer is filled
    
    Timer Operation:
        Loads the content of the compare register and decrements down to 0 every clock edge
        Timers can be configured to be enabled, in response triggers, pin, or shifter condition
        Decrement always, or only on a trigger or pin edge
        Reset in response to trigger or pin condition
        And disable on a trigger or pin condition or timer compare

        A timer can be configured to enable or disable at the same time as the previous timer
        A timer output can be used to trigger any other timer
        Trigger used by each timer is a timer output, shifter status flag, pin input, or external trigger input

        When the timer reaches 0, the time output toggles, counter reloads from the compare register
        Timer status flag is set when the timer reaches 0 and then decrements
    
        Shiter status flag can generate interrupts or DMA requests
        Timer status flag can generate interrupts
    */

    // These need to be defined manually as they are not included in the teensy header files
    // #define FLEXIO_TIMCTL_TRGPOL(n) ((n) << 23) // inverts the polarity of the trigger pin 
    // #define FLEXIO_TIMCTL_TRGSRC(n) ((n) << 22) // define if an external or internal trigger is being used
    // #define FLEXIO_SHIFTCTL_TIMPOL(n) ((n) << 23)
    // #define FLEXIO_SHIFTCFG_INSRC(n) ((n) << 8)
    // #define FLEXIO_SHIFTCTL_PINPOL(n) ((n) << 8)
    // #define DMA_TCD_CSR_ESG(n) ((n) << 4)
    // #define DMA_TCD_CSR_DREQ(n) ((n) << 3)
    // #define DMA_TCD_CSR_INTMAJOR(n) ((n) << 1)
    //////////////////////////////////////////////
    // Enable the FlexIO2 module
    //////////////////////////////////////////////
    
    CCM_CCGR3 |= CCM_CCGR3_FLEXIO2(CCM_CCGR_OFF); // disables the flexio2 clock

    // Set the clock frequency, this sets it to divide the PLL frequency by 4 so that it is at 
    // the maximum flexio2 frequency which is 120 MHz, if we did not have this code the defaul is 30 MHz, 
    // which is probably fine - so commented this out for now
    // CCM_CS1CDR &= ~( CCM_CS1CDR_FLEXIO2_CLK_PODF( 7 ) );
    // CCM_CS1CDR |= CCM_CS1CDR_FLEXIO2_CLK_PODF( 1 );

    CCM_CCGR3 |= CCM_CCGR3_FLEXIO2(CCM_CCGR_ON); // enables the flexio2 clock
    FLEXIO2_CTRL |= 1; // turns on flexio2 module

    // enable DMA request when shifter buffer 0 is filled and shifter buffer 2 is filled
	FLEXIO2_SHIFTSDEN |= 1 << 0;
    FLEXIO2_SHIFTSDEN |= 1 << 2;		

    /////////////////////////////////////////
    ////// OD Measurement ADC FLEX IO SETUP
    /////////////////////////////////////////
    // //////////////////////////////////////////////
    // // Set the pins to their appropriate modes
    // //////////////////////////////////////////////
    // D_CLK_S Teensy pin 34 is the clock signal - FLEXIO2 pin 29
    IOMUXC_SW_MUX_CTL_PAD_GPIO_B1_13 = 4; // set pin 34 to be ALT4 mux port: FLEXIO2_FLEXIO29 of instance: flexio2, pg 533
    // D_OUT_S Teensy pin 35 is the data coming in - FLEXIO2 pin 28
    IOMUXC_SW_MUX_CTL_PAD_GPIO_B1_12 = 4; // set pin 35 to be ALT4 mux port: FLEXIO2_FLEXIO28 of instance: flexio2, pg 532
     // D_RDY_S Teensy pin 9 is the data ready signal - FLEXIO2 pin 11
    IOMUXC_SW_MUX_CTL_PAD_GPIO_B0_11 = 4; // set pin 9 to be ALT4 mux port: FLEXIO2_FLEXIO11 of instance: flexio2

    //////////////////////////////////////////////
    // SHIFTER CONFIG
    //////////////////////////////////////////////
    // Some lines have been commented out as those attribute names have not been configured in the header file, and they are their default value
    //	Shifter 0 receives bits from shifter 1, one bit at a time on the negative edge of each clock signal
    FLEXIO2_SHIFTCTL0	=   FLEXIO_SHIFTCTL_TIMSEL( 2 )		|		// timer 2 triggers when shifter data should move to the shifter buffer (and trigger the dma request)
                            FLEXIO_SHIFTCTL_TIMPOL  	    |		// on positive edge [0] or negative edge [1] of clock
                            FLEXIO_SHIFTCTL_PINCFG( 0 )	    |		// pin output disabled
                            FLEXIO_SHIFTCTL_PINSEL( 0 )    |		// pin not used
                            // FLEXIO_SHIFTCTL_PINPOL( 0 )	|		// not used as shifter not an output
                            FLEXIO_SHIFTCTL_SMOD( 1 );		    	// receive mode, when timer 2 counts down to 0, shifts shifter contents to shift buffer

    FLEXIO2_SHIFTCFG0	=   FLEXIO_SHIFTCFG_PWIDTH( 0 )	    |		// one bit shift on each clock
                            FLEXIO_SHIFTCFG_INSRC           ;		// input source is shifter n+1
                            //FLEXIO_SHIFTCFG_SSTOP( 0 )	|		// stop bit disabled
                            //FLEXIO_SHIFTCFG_SSTART( 0 )   ;	    // start bit disabled

    //	Shifter 1 receives bits from the data in pin, one bit at a time on the negative edge of each clock signal
    FLEXIO2_SHIFTCTL1	=   FLEXIO_SHIFTCTL_TIMSEL( 2 )		|		// timer 2 triggers when shifter data should move to the shifter buffer (and trigger the dma request)
                            FLEXIO_SHIFTCTL_TIMPOL  	    |		// on positive edge [0] or negative edge [1] of clock
                            FLEXIO_SHIFTCTL_PINCFG( 0 )	    |		// pin output disabled
                            FLEXIO_SHIFTCTL_PINSEL( 28 )    |		// pin B1-12 data in pin
                            // FLEXIO_SHIFTCTL_PINPOL( 0 )	|		// not used as shifter is not an output
                            FLEXIO_SHIFTCTL_SMOD( 1 );		    	// receive mode, when timer 2 counts down to 0, shifts shifter contents to shift buffer

    FLEXIO2_SHIFTCFG1	=   FLEXIO_SHIFTCFG_PWIDTH( 0 )	    ;		// one bit shift on each clock
                            //FLEXIO_SHIFTCFG_INSRC          ;		// input source is the pin
                            //FLEXIO_SHIFTCFG_SSTOP( 0 )	|		// stop bit disabled
                            //FLEXIO_SHIFTCFG_SSTART( 0 )   ;	    // start bit disabled
    
    // //////////////////////////////
    // //////// TIMER CONFIG ////////
    // //////////////////////////////
    // // The overall purpose of timer 0 is to offset the start of the data capture by 2 clock edges from when a datardy signal is seen, this is because datardy is triggered before the data should start being captured, not at the same time
    // // Timer 0 is in pwm mode, it will output logic high for 2 clock edges, and then logic low for 2 clock edges before disabling (we dont really care about how long it is low for, just used to generate the falling edge)
    // // The falling edge when it goes from high to low after 2 clock edges is used to trigger the start of timer 1 which in turn enables timer 2, which captures our data
    // // lower 8 bits is high period, upper 8 bits is low period, disaables when upper 8 bits match and decrement, does high and then low
    FLEXIO2_TIMCMP0		= 	2<<8 | 2; // how many counts to do after a datardy signal is seen before starting the data capture
    
    FLEXIO2_TIMCFG0		=   FLEXIO_TIMCFG_TIMOUT( 0 )		|	    // not used as timer output disabled
                            FLEXIO_TIMCFG_TIMDEC( 2 )		|	    // decrement on pin input (both edges, so shiter counter will count on every rising and falling edge of the clock signal)
                            FLEXIO_TIMCFG_TIMRST( 0 )		|	    // timer is not reset
                            FLEXIO_TIMCFG_TIMDIS( 2 )		|	    // timer disabled on compare, so when it counts from TIMCMP to 0 it will stop
                            FLEXIO_TIMCFG_TIMENA( 7 )		;	    // timer is enabled on trigger rising or falling edge, so when datardy is high
                            //FLEXIO_TIMCFG_TSTOP( 0 )		;	    // stop bit disabled
                            //FLEXIO_TIMCFG_TSTART( 0 )		;	    // start bit disabled	

    FLEXIO2_TIMCTL0		=   FLEXIO_TIMCTL_TRGSEL( 22 )		|		// trigger is datardy flexio pin 11 (teensy pin 9)
                            // FLEXIO_TIMCTL_TRGPOL           |	    // dont flip the polarity of the trigger
                            FLEXIO_TIMCTL_TRGSRC            |	    // internal trigger used 
                            FLEXIO_TIMCTL_PINCFG( 0 )		|		// timer pin output disabled - i.e. dont write the timer output to the selected pin (B1-13)
                            FLEXIO_TIMCTL_PINSEL( 29 )	    |		// timer pin DCLK B1-13 used to clock the shifting of data in
                            //FLEXIO_TIMCTL_PINPOL( 0 )		|		// not used, i think this is only applicable if the timer is set to output on a pin
                            FLEXIO_TIMCTL_TIMOD( 2 )        ;		// timer mode is dual 8 bit PWM mode

    // Timer 1 is used to control when timer 2 is enabled, and thus how much total data we are capturing, it disables after we have shifted in 256 bits worth of data
    FLEXIO2_TIMCMP1		= 	511; // how many counts to do to capture the full 8 channels of data after we have done the timer0 counts, 8*32 bits

    FLEXIO2_TIMCFG1		=   FLEXIO_TIMCFG_TIMOUT( 0 )		|	    // not used as timer output disabled
                            FLEXIO_TIMCFG_TIMDEC( 2 )		|	    // decrement on pin input (both edges, so shiter counter will count on every rising and falling edge of the clock signal)
                            FLEXIO_TIMCFG_TIMRST( 0 )		|	    // timer is not reset
                            FLEXIO_TIMCFG_TIMDIS( 2 )		|	    // timer disabled when it finishes counting down from 511
                            FLEXIO_TIMCFG_TIMENA( 6 )		;	    // timer is enabled on trigger rising edge - we have inverted the polarity so timer0 outputs low to start with and a rising edge will be triggered on the MSB count
                            //FLEXIO_TIMCFG_TSTOP( 0 )		;	    // stop bit disabled
                            //FLEXIO_TIMCFG_TSTART( 0 )		;	    // start bit disabled	

    FLEXIO2_TIMCTL1		=   FLEXIO_TIMCTL_TRGSEL( 3 )		|		// trigger is when timer 0 output triggers (4*N+3 - Timer N trigger output) 
                            FLEXIO_TIMCTL_TRGPOL            |	    // flip the trigger polarity
                            FLEXIO_TIMCTL_TRGSRC            |	    // internal trigger used 
                            FLEXIO_TIMCTL_PINCFG( 0 )		|		// timer pin output disabled
                            FLEXIO_TIMCTL_PINSEL( 29 )	    |		// timer pin DCLK B1-13 used to clock the shifting of data in
                            //FLEXIO_TIMCTL_PINPOL( 0 )		|		// timer pin active high
                            FLEXIO_TIMCTL_TIMOD( 3 )        ;		// timer mode is a single 16bit counter
  
    // Timer 2 shifts in 64 bits of data at a time (two channels worth), and will keep doing this until it is disabled by timer 1
    FLEXIO2_TIMCMP2		= 	127; // how many counts to do before shifting 64 bits to the two shifter buffers 0 and 1 (two channels worth of data each time)

    FLEXIO2_TIMCFG2		=   FLEXIO_TIMCFG_TIMOUT( 0 )		|	    // not used as timer output disabled
                            FLEXIO_TIMCFG_TIMDEC( 2 )		|	    // decrement on pin input (both edges, so shiter counter will count on every rising and falling edge of the clock signal)
                            FLEXIO_TIMCFG_TIMRST( 0 )		|	    // timer is not reset
                            FLEXIO_TIMCFG_TIMDIS( 1 )		|	    // timer is disabled when timer n-1 disables
                            FLEXIO_TIMCFG_TIMENA( 1 )		;	    // timer is enabled when timer n-1 enables
                            //FLEXIO_TIMCFG_TSTOP( 0 )		;	    // stop bit disabled
                            //FLEXIO_TIMCFG_TSTART( 0 )		;	    // start bit disabled	

    FLEXIO2_TIMCTL2		=   FLEXIO_TIMCTL_TRGSEL( 0 )		|		// trigger not used
                            // FLEXIO_TIMCTL_TRGPOL           |	    // trigger not used
                            // FLEXIO_TIMCTL_TRGSRC           |	    // trigger not used
                            FLEXIO_TIMCTL_PINCFG( 0 )		|		// timer pin output disabled
                            FLEXIO_TIMCTL_PINSEL( 29 )	    |		// timer pin DCLK B1-13 used to clock the shifting of data in
                            //FLEXIO_TIMCTL_PINPOL( 0 )		|		// timer pin active high
                            FLEXIO_TIMCTL_TIMOD( 3 )        ;		// timer mode is a single 16bit counter


    /////////////////////////////////////////
    /////// Load Cell ADC FLEXIO Setup
    /////////////////////////////////////////
    
    /// ON V2 BOARD
    /// D_OUT_T is teensy pin 32. - FLEXIO 12
    /// D_OUT_CLK is teensy pin 8 - FLEXIO 16
    /// D_RDY_T is teensy pin 37. - FLEXIO 19
    #if GENERATION == V2_MACHINES || GENERATION == V3_MACHINES
        constexpr uint8_t FLEXIO_DOUTT_PIN = 12;
        constexpr uint8_t FLEXIO_DCLKT_PIN = 16;
        constexpr uint8_t FLEXIO_DRDYT_PIN = 19;
    #elif GENERATION == V1_MACHINES
        constexpr uint8_t FLEXIO_DOUTT_PIN = 16;
        constexpr uint8_t FLEXIO_DCLKT_PIN = 12;
        constexpr uint8_t FLEXIO_DRDYT_PIN = 19;
    #endif

    // D_OUT_T Teensy pin 8 is the data coming in signal
    IOMUXC_SW_MUX_CTL_PAD_GPIO_B1_00 = 4; // set pin 8 to be ALT4 mux port:  FLEXIO2_FLEXIO16 of instance: flexio2 pg 520
    // D_CLK_T Teensy pin 32 is the clock in 
    IOMUXC_SW_MUX_CTL_PAD_GPIO_B0_12 = 4; // set pin 32 to be ALT4 mux port: FLEXIO2_FLEXIO12 of instance: flexio2, pg 516
    // D_RDY_T Teensy pin 37 is the data ready signal 
    IOMUXC_SW_MUX_CTL_PAD_GPIO_B1_03 = 4; // set pin 37 to be ALT4 mux port: FLEXIO2_FLEXIO19 of instance: flexio2, pg 523

    //////////////////////////////////////////////
    // SHIFTER CONFIG
    //////////////////////////////////////////////
    FLEXIO2_SHIFTCTL2	=   FLEXIO_SHIFTCTL_TIMSEL( 5 )		|		// timer 5 triggers when shifter data should move to the shifter buffer (and trigger the dma request)
                            FLEXIO_SHIFTCTL_TIMPOL  	    |		// on positive edge [0] or negative edge [1] of clock
                            FLEXIO_SHIFTCTL_PINCFG( 0 )	    |		// pin output disabled
                            FLEXIO_SHIFTCTL_PINSEL( 0 )    |		// pin not used
                            // FLEXIO_SHIFTCTL_PINPOL( 0 )	|		// not used as shifter not an output
                            FLEXIO_SHIFTCTL_SMOD( 1 );		    	// receive mode, when timer 5 counts down to 0, shifts shifter contents to shift buffer

    FLEXIO2_SHIFTCFG2	=   FLEXIO_SHIFTCFG_PWIDTH( 0 )	    |		// one bit shift on each clock
                            FLEXIO_SHIFTCFG_INSRC           ;		// input source is shifter n+1
                            //FLEXIO_SHIFTCFG_SSTOP( 0 )	|		// stop bit disabled
                            //FLEXIO_SHIFTCFG_SSTART( 0 )   ;	    // start bit disabled

    FLEXIO2_SHIFTCTL3	=   FLEXIO_SHIFTCTL_TIMSEL( 5 )		|		// timer 5 triggers when shifter data should move to the shifter buffer (and trigger the dma request)
                            FLEXIO_SHIFTCTL_TIMPOL  	    |		// on positive edge [0] or negative edge [1] of clock
                            FLEXIO_SHIFTCTL_PINCFG( 0 )	    |		// pin output disabled
                            FLEXIO_SHIFTCTL_PINSEL( FLEXIO_DOUTT_PIN )    |		// data in pin is flexio2 pin 12
                            // FLEXIO_SHIFTCTL_PINPOL( 0 )	|		// not used as shifter is not an output
                            FLEXIO_SHIFTCTL_SMOD( 1 );		    	// receive mode, when timer 5 counts down to 0, shifts shifter contents to shift buffer

    FLEXIO2_SHIFTCFG3	=   FLEXIO_SHIFTCFG_PWIDTH( 0 )	    ;		// one bit shift on each clock
                            //FLEXIO_SHIFTCFG_INSRC          ;		// input source is pin B0-12
                            //FLEXIO_SHIFTCFG_SSTOP( 0 )	|		// stop bit disabled
                            //FLEXIO_SHIFTCFG_SSTART( 0 )   ;	    // start bit disabled
    
    //////////////////////////////
    //////// TIMER CONFIG ////////
    //////////////////////////////
    FLEXIO2_TIMCMP3		= 	2<<8 | 2; // how many counts to do after a datardy signal is seen before starting the data capture
    
    FLEXIO2_TIMCFG3		=   FLEXIO_TIMCFG_TIMOUT( 0 )		|	    // Timer output is logic one when enabled 
                            FLEXIO_TIMCFG_TIMDEC( 2 )		|	    // decrement on pin input (both edges, so shiter counter will count on every rising and falling edge of the clock signal)
                            FLEXIO_TIMCFG_TIMRST( 0 )		|	    // timer is not reset
                            FLEXIO_TIMCFG_TIMDIS( 2 )		|	    // timer disabled on compare, so when it counts from TIMCMP to 0 it will stop
                            FLEXIO_TIMCFG_TIMENA( 7 )		;	    // timer is enabled on trigger rising or falling edge, so when datardy is high
                            //FLEXIO_TIMCFG_TSTOP( 0 )		;	    // stop bit disabled
                            //FLEXIO_TIMCFG_TSTART( 0 )		;	    // start bit disabled	

    FLEXIO2_TIMCTL3		=   FLEXIO_TIMCTL_TRGSEL( FLEXIO_DRDYT_PIN * 2 )		|		// trigger is datardy flexio2 pin 19
                            // FLEXIO_TIMCTL_TRGPOL           |	    // dont flip the polarity of the trigger
                            FLEXIO_TIMCTL_TRGSRC            |	    // internal trigger used 
                            FLEXIO_TIMCTL_PINCFG( 0 )		|		// timer pin output disabled
                            FLEXIO_TIMCTL_PINSEL( FLEXIO_DCLKT_PIN )	    |		// timer pin DCLK flexio2 pin 12 used to clock the shifting of data in
                            //FLEXIO_TIMCTL_PINPOL( 0 )		|		// not used, i think this is only applicable if the timer is set to output on a pin
                            FLEXIO_TIMCTL_TIMOD( 2 )        ;		// timer mode is dual 8 bit PWM mode

    FLEXIO2_TIMCMP4		= 	511; // how many counts to do to capture the full 8 channels of data after we have done the timer0 counts, 8*32 bits

    FLEXIO2_TIMCFG4		=   FLEXIO_TIMCFG_TIMOUT( 0 )		|	    // not used as timer output disabled
                            FLEXIO_TIMCFG_TIMDEC( 2 )		|	    // decrement on pin input (both edges, so shiter counter will count on every rising and falling edge of the clock signal)
                            FLEXIO_TIMCFG_TIMRST( 0 )		|	    // timer is not reset
                            FLEXIO_TIMCFG_TIMDIS( 2 )		|	    // timer disabled when it finishes counting down from 511
                            FLEXIO_TIMCFG_TIMENA( 6 )		;	    // timer is enabled on trigger rising edge - we have inverted the polarity so timer3 outputs low to start with and a rising edge will be triggered on the MSB count
                            //FLEXIO_TIMCFG_TSTOP( 0 )		;	    // stop bit disabled
                            //FLEXIO_TIMCFG_TSTART( 0 )		;	    // start bit disabled	

    FLEXIO2_TIMCTL4		=   FLEXIO_TIMCTL_TRGSEL( 15 )		|		// trigger is when timer 3 output triggers (4*N+3 - Timer N trigger output) 
                            FLEXIO_TIMCTL_TRGPOL            |	    // dont flip the trigger polarity
                            FLEXIO_TIMCTL_TRGSRC            |	    // internal trigger used 
                            FLEXIO_TIMCTL_PINCFG( 0 )		|		// timer pin output disabled
                            FLEXIO_TIMCTL_PINSEL( FLEXIO_DCLKT_PIN )	    |		// timer pin DCLK flexio2 pin 12 used to clock the shifting of data in
                            //FLEXIO_TIMCTL_PINPOL( 0 )		|		// timer pin active high
                            FLEXIO_TIMCTL_TIMOD( 3 )        ;		// timer mode is a single 16bit counter
  
    // Timer 2 shifts in 64 bits of data at a time (two channels worth), and will keep doing this until it is disabled by timer 1
    FLEXIO2_TIMCMP5		= 	127; // how many counts to do before shifting 64 bits to the two shifter buffers 0 and 1 (two channels worth of data each time)

    FLEXIO2_TIMCFG5		=   FLEXIO_TIMCFG_TIMOUT( 0 )		|	    // not used as timer output disabled
                            FLEXIO_TIMCFG_TIMDEC( 2 )		|	    // decrement on pin input (both edges, so shiter counter will count on every rising and falling edge of the clock signal)
                            FLEXIO_TIMCFG_TIMRST( 0 )		|	    // timer is not reset
                            FLEXIO_TIMCFG_TIMDIS( 1 )		|	    // timer is disabled when timer n-1 disables
                            FLEXIO_TIMCFG_TIMENA( 1 )		;	    // timer is enabled when timer n-1 enables
                            //FLEXIO_TIMCFG_TSTOP( 0 )		;	    // stop bit disabled
                            //FLEXIO_TIMCFG_TSTART( 0 )		;	    // start bit disabled	

    FLEXIO2_TIMCTL5		=   FLEXIO_TIMCTL_TRGSEL( 0 )		|		// trigger not used
                            // FLEXIO_TIMCTL_TRGPOL           |	    // trigger not used
                            // FLEXIO_TIMCTL_TRGSRC           |	    // trigger not used
                            FLEXIO_TIMCTL_PINCFG( 0 )		|		// timer pin output disabled
                            FLEXIO_TIMCTL_PINSEL( FLEXIO_DCLKT_PIN )	    |		// timer pin DCLK flexio2 pin 12 used to clock the shifting of data in
                            //FLEXIO_TIMCTL_PINPOL( 0 )		|		// timer pin active high
                            FLEXIO_TIMCTL_TIMOD( 3 )        ;		// timer mode is a single 16bit counter

    //////////////////////////////
    //////// DMA            
    //////////////////////////////
    ///////////////////
    ///// OD DMA Setup
    ///////////////////
	dmaChannelOD.begin();

	dmaChannelOD.TCD->SADDR		= &FLEXIO2_SHIFTBUFBIS0; //FLEXIO2_SHIFTBUFBIS0 for flipped FLEXIO2_SHIFTBUF0 for not flipped
	dmaChannelOD.TCD->SOFF		= 0;    // in bytes
	dmaChannelOD.TCD->ATTR_SRC	= 3;	// 0 = 8 bit, 1 = 16 bit, 2 = 32 bit, 3 = 64 bit, how much data we are transfering each minor loop iteration
	dmaChannelOD.TCD->SLAST		= 0;

	dmaChannelOD.TCD->DADDR		= dmaBufferOD;
	dmaChannelOD.TCD->DOFF		= 8;    // offset in bytes after each write - dependent on the fact that we are writing one 64 bit word each time
	dmaChannelOD.TCD->ATTR_DST	= 3;	// 0 = 8 bit, 1 = 16 bit, 2 = 32 bit, 3 = 64 bit, how much data we are receiving each minor loop iteration
	dmaChannelOD.TCD->DLASTSGA	= -8 * DMABUFFER_SIZE;   // after the last major iteration offset in bytes 
    
	dmaChannelOD.TCD->NBYTES	= 8;    // number of bytes written each time
	dmaChannelOD.TCD->BITER		= DMABUFFER_SIZE; // how many major loop iterations to complete
	dmaChannelOD.TCD->CITER		= DMABUFFER_SIZE; // how many major loop iterations to complete
	
	dmaChannelOD.TCD->CSR		= DMA_TCD_CSR_INTMAJOR   ; // do not interrupt at completion 

	dmaChannelOD.attachInterrupt( ODReadyInterrupt );



	dmaChannelOD.triggerAtHardwareEvent( DMAMUX_SOURCE_FLEXIO2_REQUEST0 ); // trigger the dma request when content is moved to the shifter buffer
	
    // enable DMA
	dmaChannelOD.enable();


    ///////////////////
    ///// Load Cell DMA Setup
    ///////////////////
	dmaChannelLoadCell.begin();

	dmaChannelLoadCell.TCD->SADDR		= &FLEXIO2_SHIFTBUFBIS2; 
	dmaChannelLoadCell.TCD->SOFF		= 0;    // in bytes
	dmaChannelLoadCell.TCD->ATTR_SRC	= 3;	// 0 = 8 bit, 1 = 16 bit, 2 = 32 bit, 3 = 64 bit, how much data we are transfering each minor loop iteration
	dmaChannelLoadCell.TCD->SLAST		= 0;

	dmaChannelLoadCell.TCD->DADDR		= dmaBufferLoadCell;
	dmaChannelLoadCell.TCD->DOFF		= 8;    // offset in bytes after each write - dependent on the fact that we are writing one 64 bit word each time
	dmaChannelLoadCell.TCD->ATTR_DST	= 3;	// 0 = 8 bit, 1 = 16 bit, 2 = 32 bit, 3 = 64 bit, how much data we are receiving each minor loop iteration
	dmaChannelLoadCell.TCD->DLASTSGA	= -8 * DMABUFFER_SIZE;   // after the last major iteration offset in bytes 

	dmaChannelLoadCell.TCD->NBYTES		= 8;    // number of bytes written each time
	dmaChannelLoadCell.TCD->BITER		= DMABUFFER_SIZE; // how many major loop iterations to complete
	dmaChannelLoadCell.TCD->CITER		= DMABUFFER_SIZE; // how many major loop iterations to complete

	// dmaChannelLoadCell.TCD->CSR		=   DMA_TCD_CSR_DREQ       | // do not disable the channel after it completes - so it just keeps going 
    //                                     DMA_TCD_CSR_ESG	    |				
	// 	                                DMA_TCD_CSR_INTMAJOR   ; // do not interrupt at completion 
    
    // dmaChannelLoadCell.TCD->CSR	|= DMA_TCD_CSR_INTMAJOR;
	// dmaChannelLoadCell.attachInterrupt( WeightOutputInterrupt );
	
    dmaChannelLoadCell.triggerAtHardwareEvent( DMAMUX_SOURCE_FLEXIO2_REQUEST2 ); // trigger the dma request when content is moved to the shifter buffer
	
    // enable DMA
	dmaChannelLoadCell.enable();
}

void initialiseChips() {
    // This command is designed to initialise all digital chips in the system. It shoud power them up to a safe/default-on state. Y
    // it should function as a reset command i.e. you should sendit before shutting things down/powering off to make sure it turns off elegantly.
    CHIPS.Heatboard_IO_1.Initialise();
    CHIPS.Heatboard_IO_2.Initialise();
    CHIPS.Heatboard_IO_3.Initialise();
    CHIPS.Heatboard_IO_4.Initialise();
    CHIPS.Senseboard_IO_1.Initialise();
    CHIPS.Topboard_IO_1.Initialise();
    CHIPS.Topboard_IO_2.Initialise();

    CHIPS.Topboard_PWM_1.Initialise();
    CHIPS.Topboard_PWM_2.Initialise();
    CHIPS.Heatboard_PWM_1.Initialise();
    CHIPS.Heatboard_PWM_2.Initialise();
    CHIPS.Heatboard_PWM_3.Initialise();

    CHIPS.Senseboard_DAC.Initialise();
    CHIPS.Heatboard_DAC.Initialise();

    CHIPS.Senseboard_ADC.Initialise(dmaBufferOD);
    CHIPS.Topboard_ADC.Initialise(dmaBufferLoadCell); //NOTE you need to set this up before you try to intialise Senseboard ADC2 otherwise CS0 will be held low and this breaks Senseboard ADC2!
    CHIPS.Senseboard_ADC2.Initialise();
    CHIPS.Heatboard_ADC.Initialise();

    CHIPS.Senseboard_TMP.Initialise();

    #if GENERATION == V2_MACHINES || GENERATION == V3_MACHINES
        CHIPS.Topboard_TMP_1.Initialise(); // These chips never got soldered on LTEM V1
        CHIPS.Topboard_TMP_2.Initialise();
        CHIPS.Topboard_TMP_3.Initialise();
    #endif

    
    CHIPS.Senseboard_HDC.Initialise();
    CHIPS.Senseboard_ACCEL.Initialise(); 
    
}

uint32_t crash_teensy() {
    Wire1.beginTransmission(0x00); // Address 0x00 is not used by any device
    Wire1.write(0x00);              // Send a dummy byte
    Wire1.endTransmission();        // This should cause a NACK and potentially hang the I2C bus
    // then dont return anything so it will time out

}

void enableSystem(bool enable = false) {
    // Function to enable or disable the system.. This is the only place you should change the value of systemEnable.
    //Note this function is called with disable whenever Teensy boots. Conceptually the DISABLE command should turn off power/busses, but not worry about doing I2C/chip resets unless you already have it enabled when you enter.
    if (enable) {
        if (CrashReport) {
            char crash_buffer[512];  // Adjust size as needed
            BufferPrint bufferPrint(crash_buffer, sizeof(crash_buffer));
            
            // Use the Print interface to write CrashReport to buffer
            bufferPrint.print(CrashReport);

            // Now send via your sendError function
            teensy_device.sendError("Crash Report:%s", crash_buffer);
        }
        machine.RTDADC_Calibrated = false; // Reset the RTD ADC calibration flag

        current_iterator = 0;
        // RESET Global Variable Values, so behaviour is like its starting up for the first time
        // is_this_a_scatter_reading = false;
        teensy_device.sendDebug("Enabling system");
        reading_number = 0;

        teensy_device.clearBuffer();  // Clear the buffer to make sure we are not receiving any old data
        initialise_fast_packets();
        
        // First turn i2c chips on
        #if GENERATION == V1_MACHINES
            digitalWrite(Senseboard_PowerEnable, HIGH);
        #elif GENERATION == V2_MACHINES || GENERATION == V3_MACHINES
            digitalWrite(D_Power_En, HIGH);  
        #endif

        delay(100);  // Wait for power supplies to turn on

        teensy_device.sendDebug("Setup I2C");
        // Set up your busses
        Wire1.begin(); //Start I2c byus, we shouldnt need to mess with this again
        Wire1.setClock(100000L);
        // Wire1.setTimeout(500);  // Set the timeout for I2C to 0.5s

        teensy_device.sendDebug("Setup SPI");
        SPI.begin();
        SPI.setMOSI(SPI_MOSI0);
        SPI.setMISO(SPI_MISO0);
        SPI.setSCK(SPI_SCK0);
        // SPI.setBitOrder(MSBFIRST); Dont use this as specified here: https://arduino.stackexchange.com/questions/79728/when-is-spi-begintransaction-required

        teensy_device.sendDebug("Resetting I2C and ADC");
        //do an i2c reset of the system
        digitalWrite(I2C_Reset, LOW);
        delay(10);
        digitalWrite(I2C_Reset, HIGH);
        delay(10);

        #if GENERATION == V2_MACHINES || GENERATION == V3_MACHINES
            digitalWrite(I2C_MUX_Reset, LOW);
            delay(10);
            digitalWrite(I2C_MUX_Reset, HIGH);
            delay(10);
        #endif

        // Reset the ADC
        digitalWrite(ADC_Reset, LOW);
        delay(10);
        digitalWrite(ADC_Reset, HIGH);
        delay(10);
        teensy_device.sendDebug("I2C and ADC reset complete");

        initialiseChips();  // Here we start turning on I2C chips and initialiseing them but note this will crash your code if it cannot actually find that chip!
        delay(100);
        
        // After chips have been initialised we can enable the power to heaters, leds, UV, stir coils, fans 
        #if GENERATION == V1_MACHINES
            digitalWrite(Topboard_and_Heatboard_PowerEnable, HIGH);
            digitalWrite(Actuators_Enable, LOW); // NOTE THIS IS ACTIVE LOW!! I.e. stuff(PCA9505 and PCA9635) are turned ON when this is low.
        #elif GENERATION == V2_MACHINES || GENERATION == V3_MACHINES
            digitalWrite(P_Power_En, HIGH);
        #endif
        // You dont want to leave Actuators_Enable high since it means all the outputs become high impedance which could lead to weirdness.

        teensy_device.sendDebug("Starting general config");
        machine.generalConfig();  // This function does the general configuration of the machine. It should be called when you enable system. It does things that are not reactor-specific. You dont need to call it when disabling the system
        teensy_device.sendDebug("General config complete");

        delay(100); // Delay so there is some time for heartbeat to get its act together. I think this isn't needed since all digiatl comms shouldn't rely on it.
        
        machine.setFanDuty(1500); // turn on state is fans at 1500/4096 power

        // Interrupts - we do this here to try to reduce ambiguiuity and make it so they arent being created in multiple places in code, also so we can detatch them when we disable system...
        attachInterrupt(digitalPinToInterrupt(Other_Interrupt), IOInterruptHandler, FALLING);  // Attach the interrupt for the interrupt pin.
        attachInterrupt(digitalPinToInterrupt(Accel_Interrupt), AccelInterruptHandler, RISING);  // Attach the interrupt for the interrupt pin. pin is low, and interrupt pulls high

        ODOutputIntIO(-1); // Disable the data output interrupt by default, this is so that it does not run at startup and flood the host with data
        WeightOutputIntIO(-1);

        machine.setPressure_MPa(0.04,1); // Set the default pressure for the machine
        machine.setPressure_MPa(0.02,2); // Set the default pressure for the machine
        
        machine.runRTDADC(true); // Start the RTD ADC calibration

        while (!machine.RTDADC_Calibrated) {
            // Wait for calibration to complete before moving on
            machine.runRTDADC();
            delay(30);
        }

        machine.reactors[REACTOR::AL].setIRIO(true);
        machine.reactors[REACTOR::AR].setIRIO(true);
        machine.reactors[REACTOR::BL].setIRIO(true);
        machine.reactors[REACTOR::BR].setIRIO(true);
        machine.reactors[REACTOR::CL].setIRIO(true);
        machine.reactors[REACTOR::CR].setIRIO(true);
        machine.reactors[REACTOR::DL].setIRIO(true);
        machine.reactors[REACTOR::DR].setIRIO(true);

        heartbeat_timer = 0;
        heartbeat_started = false;  // Stop the heartbeat timer from running
        systemEnable = true;  // Don't try to set this anywhere other than here!

        teensy_device.sendInfo("System Enabled, Code is Generation %d", GENERATION);
        
        // dmaChannelOD.TCD->CSR	|= DMA_TCD_CSR_INTMAJOR;
	    // dmaChannelOD.attachInterrupt( int_func );
    } else {  //If we are disabling the system the below does various hard-coded things to turn off different parts. Many of these are inefficient double ups but we have them anyway to make the code.... less likely to break in future maybe?
        //First do things quickly that need to be turned off.
        if (systemEnable) {     // In this case we ALREADY have the system on but are turning it off. In such a case we therefor return chip sto default state
            // We put things in this if statement that get completed in systemenable
            // Detach interrupts
            fluid_sensor_timer.end();
            // heater_pid_timer.end();
            // mixing_heater_pid_timer.end();
            detachInterrupt(digitalPinToInterrupt(Other_Interrupt));  // Detach the interrupt for the interrupt pin.
            detachInterrupt(digitalPinToInterrupt(Accel_Interrupt));  // Detach the interrupt for the interrupt pin. pin is low, and interrupt pulls high
            //detachInterrupt(digitalPinToInterrupt(Senseboard_ADC_RDY));  // Detach the interrupt for the Senseboard ADC ready pin.

            ODOutputIntIO(-2);
            WeightOutputIntIO(-2);
            
            initialiseChips();  //We want to go and reset the chips in this case - and since we are enabled the bus and power shoudl be on to do so
            delay(50);

            //Now turn off busses
            SPI.end();
            Wire1.end();
        }

        // The only time these will be called and not the things in the previous if statement is on initial device startup
        #if GENERATION == V1_MACHINES
            digitalWrite(Topboard_and_Heatboard_PowerEnable, LOW);
            digitalWrite(Senseboard_PowerEnable, LOW);
            digitalWrite(Actuators_Enable, HIGH); // NOTE THIS IS ACTIVE LOW!
        #elif GENERATION == V2_MACHINES || GENERATION == V3_MACHINES
            digitalWrite(D_Power_En, LOW);       
            digitalWrite(P_Power_En, LOW);
        #endif
        
        delay(100);           // Delay so there is some time for heartbeat to shut down
        heartbeat_started = false;  // Stop the heartbeat timer from running
        systemEnable = false;  // Don't try to set this anywhere other than here!
        teensy_device.sendInfo("System disabled");
    }
}

void findI2C(uint32_t* found_devices_ptr) {
    // 7 bit address range from 0-127
    uint8_t index = 0;
    uint8_t error, address;

    for (uint8_t i = 0; i < 32; i++) {
        found_devices_ptr[i] = 0;
    }
    // address starts at 1 not 0 since 0 is a general call address and not used by devices
    for (address = 1; address < 127; address++) {
        // The i2c_scanner uses the return value of
        // the Write.endTransmisstion to see if
        // a device did acknowledge to the address.
        Wire1.beginTransmission(address);
        error = Wire1.endTransmission();

        if (error == 0) {
            teensy_device.sendInfo("I2C device found at address 0x%02X", address);
            found_devices_ptr[index] = address; // If a device is found, store its address in the array
            index++;
            if (index >= 32) { // Prevent overflow
                // teensy_device.sendError("Too many I2C devices found");
                break;
            }
        } else if (error == 4) {
            teensy_device.sendInfo("Unknown error at address 0x%02X", address);
        } else if (error == 5) {
            teensy_device.sendInfo("Timeout when accessing 0x%02X", address);
        } 
    }

    for (uint8_t i = 0; i < 4; i++) {
        address = IR_temp_chip_ADR;
        CHIPS.Topboard_Thermo_MUX.setChannel(i);
        delay(50);
        Wire1.beginTransmission(address);
        error = Wire1.endTransmission();

        if (error == 0) {
            teensy_device.sendInfo("IR Thermometer %d found at address 0x%02X", i, address);
            found_devices_ptr[index] = address; // If a device is found, store its address in the array
            index++;
            if (index >= 32) { // Prevent overflow
                // teensy_device.sendError("Too many I2C devices found");
                break;
            }
        } else if (error == 4) {
            teensy_device.sendInfo("Unknown error for IR Thermometer %d at address 0x%02X", i, address);
        } else if (error == 5) {
            teensy_device.sendInfo("Timeout when accessing IR Thermometer %d at address 0x%02X", i, address);
        } 
    }

    for (uint8_t i = 4; i < 7; i++) {
        address = Topboard_Thermo_ADR;
        CHIPS.Topboard_Thermo_MUX.setChannel(i);
        delay(50);
        Wire1.beginTransmission(address);
        error = Wire1.endTransmission();

        if (error == 0) {
            teensy_device.sendInfo("Topboard Thermometer %d found at address 0x%02X", i, address);
            found_devices_ptr[index] = address; // If a device is found, store its address in the array
            index++;
            if (index >= 32) { // Prevent overflow
                // teensy_device.sendError("Too many I2C devices found");
                break;
            }
        } else if (error == 4) {
            teensy_device.sendInfo("Unknown error for Topboard Thermometer %d at address 0x%02X", i, address);
        } else if (error == 5) {
            teensy_device.sendInfo("Timeout when accessing Topboard Thermometer %d at address 0x%02X", i, address);
        } 
    }



    if (index == 0) {
        teensy_device.sendInfo("No I2C devices found");
    } else {
        teensy_device.sendInfo("Found %d Devices", index);
    }
    
    return;
}

void scanI2C() {
    // This function is used to scan the I2C bus and see what devices are connected. It is useful for debugging and checking that the devices are connected correctly.
    // Note this function is not used in the main code and is only here for debugging purposes.
    uint8_t error, address;
    uint8_t nDevices = 0;

    teensy_device.sendInfo("Scanning...");
    // uint32_t found_devices[127];

    for (address = 1; address < 127; address++) {
        // The i2c_scanner uses the return value of
        // the Write.endTransmisstion to see if
        // a device did acknowledge to the address.
        Wire1.beginTransmission(address);
        error = Wire1.endTransmission();

        if (error == 0) {
            teensy_device.sendInfo("I2C device found at address 0x%02X", address);
            nDevices++;
        } 
        else if (error == 4) {
            teensy_device.sendInfo("Unknown error at address 0x%02X", address);
        } else if (error == 5) {
            teensy_device.sendInfo("Timeout when accessing 0x%02X", address);
        } 
    }
    if (nDevices == 0)
        teensy_device.sendInfo("No I2C devices found");
    else
        teensy_device.sendInfo("Found %d Devices", nDevices);
}
int AD7768_debug_counter = 0;


void send_auto_readings(bool send_readings){
    machine.SEND_FAST_OD_READINGS = send_readings;
    machine.SEND_FAST_WEIGHT_READINGS = send_readings;
    machine.SEND_MEMORY_REPORT = send_readings;
    machine.SEND_CURRENT_REPORT = send_readings;
    machine.SEND_REACTOR_TEMPERATURE_REPORT = send_readings;
    machine.SEND_OTHER_TEMPERATURE_REPORT = send_readings;
}


void process_packet(Packet* packet) {
    packet_timer = 0;
    // see packet_config.proto -> message RequestedData for available options

    // our big if statement is based on what the requested data is
    // dependent on what the requested data it then queries the correct system, either a machine, reactor, or mixing bottle object

    // bool keep_open = false;

    /////////////////////////////////////////
    ////////////////// VALVES ///////////////
    /////////////////////////////////////////
    if(packet->data.has_VALVE_IO && packet->type == PacketType_GET) {
        process_id = 1;
        packet->data.VALVE_IO = machine.getValveState(mmap[packet->system]);
    } else if (packet->data.has_VALVE_IO && packet->type == PacketType_SET) {
        process_id = 2;
        machine.setValveState(mmap[packet->system], packet->data.VALVE_IO);
    
    /////////////////////////////////////////
    ////////////////// PRESSURE_REG /////////
    /////////////////////////////////////////
    } else if (packet->data.has_PRES_REG_1_MPA && packet->type == PacketType_GET) {
        process_id = 3;
        machine.runRTDADC(false, true, 2);                      // This runs the RTD ADC for the pressure reg
        delay(35);                                              // Wait for conversion to complete
        machine.runRTDADC();                                    // this saves the value of the ADC read onto the machine object

        packet->data.PRES_REG_1_MPA = machine.getPressure1();   // This retrieves the value from the machine object
    
    } else if (packet->data.has_PRES_REG_1_MPA && packet->type == PacketType_SET) {
        process_id = 4;
        machine.setPressure_MPa(packet->data.PRES_REG_1_MPA , 1);
    
    } else if (packet->data.has_PRES_REG_2_MPA && packet->type == PacketType_GET) {
        process_id = 5;
        machine.runRTDADC(false, true, 3);  
        delay(35);
        machine.runRTDADC();  

        packet->data.PRES_REG_2_MPA = machine.getPressure2();

    } else if (packet->data.has_PRES_REG_2_MPA && packet->type == PacketType_SET) {
        process_id = 6;
        machine.setPressure_MPa(packet->data.PRES_REG_2_MPA, 2);
    
    /////////////////////////////////////////
    ////////////////// OD ///////////////////
    /////////////////////////////////////////
    } else if (packet->data.has_OD_CONFIG && packet->type == PacketType_SET) {
        process_id = 7;
        teensy_device.sendDebug("Doing OD Config now");

        if (packet->data.OD_CONFIG.has_PD_IS_ADSORPTION){
            if (machine.reactors[rmap[packet->system]].isPDAdsorption() != (packet->data.OD_CONFIG.PD_IS_ADSORPTION)) { 
                //Note reason for these if statments is we only want to update the config if it actually needs to change to save us sending unnecessary i2c commands.
                machine.reactors[rmap[packet->system]].selectAdsorptionPD(packet->data.OD_CONFIG.PD_IS_ADSORPTION); // Set whether you will use absorotion or scatter measurement
            }
        }
        // Now set IR Wavelength to use
        if (packet->data.OD_CONFIG.has_OD_WAVELENGTH){
            if (machine.reactors[rmap[packet->system]].getIRWavelength() != packet->data.OD_CONFIG.OD_WAVELENGTH) {
                machine.reactors[rmap[packet->system]].setIRWavelength(packet->data.OD_CONFIG.OD_WAVELENGTH); // SET the wavelength channel to use
            }
        }

        // Now selecting fast or low noise readout of photodiode
        if (packet->data.OD_CONFIG.has_OD_ADC_FILTER_FAST_MODE){
            if (machine.reactors[rmap[packet->system]].isPDFilterFast() != packet->data.OD_CONFIG.OD_ADC_FILTER_FAST_MODE) {
                machine.reactors[rmap[packet->system]].setPDFilterFastMode(packet->data.OD_CONFIG.OD_ADC_FILTER_FAST_MODE); // Set the ADC mode
            }
        }

        //Turning on cuyrrent to the LED
        if (packet->data.OD_CONFIG.has_OD_LED_ENABLE) {
            if (machine.reactors[rmap[packet->system]].getIRIO() != packet->data.OD_CONFIG.OD_LED_ENABLE) {
                machine.reactors[rmap[packet->system]].setIRIO(packet->data.OD_CONFIG.OD_LED_ENABLE); // Toggle the IR LED
            }
        }

        // Turning on the SYNC signal to the LED
        if (packet->data.OD_CONFIG.has_OD_LED_SYNC) {
            if (machine.reactors[rmap[packet->system]].getSYNC() != packet->data.OD_CONFIG.OD_LED_SYNC) {
                machine.reactors[rmap[packet->system]].enableSYNC(packet->data.OD_CONFIG.OD_LED_SYNC);  // Toggle sync signal to that LED
            }
        }

    
    } else if (packet->data.has_OD_CONFIG && packet->type == PacketType_GET) {
        process_id = 8;
        packet->data.OD_CONFIG.has_PD_IS_ADSORPTION = true;
        packet->data.OD_CONFIG.PD_IS_ADSORPTION = machine.reactors[rmap[packet->system]].isPDAdsorption();

        packet->data.OD_CONFIG.has_OD_WAVELENGTH = true;
        packet->data.OD_CONFIG.OD_WAVELENGTH = machine.reactors[rmap[packet->system]].getIRWavelength();

        packet->data.OD_CONFIG.has_OD_ADC_FILTER_FAST_MODE = true;
        packet->data.OD_CONFIG.OD_ADC_FILTER_FAST_MODE = machine.reactors[rmap[packet->system]].isPDFilterFast();

        packet->data.OD_CONFIG.has_OD_LED_ENABLE = true;
        packet->data.OD_CONFIG.OD_LED_ENABLE = machine.reactors[rmap[packet->system]].getIRIO();

        packet->data.OD_CONFIG.has_OD_LED_SYNC = true;
        packet->data.OD_CONFIG.OD_LED_SYNC = machine.reactors[rmap[packet->system]].getSYNC();
    
    
    } else if (packet->data.has_OD_VOLTAGE && packet->type == PacketType_GET) {
        process_id = 9;
        packet->data.OD_VOLTAGE = machine.reactors[rmap[packet->system]].getODData().voltage;
    
    } else if (packet->data.OD_BITS_count > 0 && packet->type == PacketType_GET) {
        process_id = 10;
        for (int i = 0; i<packet->data.OD_BITS_count; i++) {
            packet->data.OD_BITS[i] = CHIPS.Topboard_ADC.getADCBits()[i];
            // packet->data.OD_BITS[i] = CHIPS."Senseboard_ADC"].getADCBits()[i];
        }
    
    } else if (packet->data.has_AD7768_CHANNEL_DATA && packet->type == PacketType_GET) {
        process_id = 11;
        
        if (        packet->system == SystemName_REACTOR_AL || packet->system == SystemName_REACTOR_AR ||
            packet->system == SystemName_REACTOR_BL || packet->system == SystemName_REACTOR_BR ||
            packet->system == SystemName_REACTOR_CL || packet->system == SystemName_REACTOR_CR ||
            packet->system == SystemName_REACTOR_DL || packet->system == SystemName_REACTOR_DR) {
            
            AD7768Data reactor_od_data = machine.reactors[rmap[packet->system]].getODData();
            packet->data.AD7768_CHANNEL_DATA.has_error_flag         = true;
            packet->data.AD7768_CHANNEL_DATA.has_filter_not_settled = true;
            packet->data.AD7768_CHANNEL_DATA.has_repeated_data      = true;
            packet->data.AD7768_CHANNEL_DATA.has_filter_type        = true;
            packet->data.AD7768_CHANNEL_DATA.has_filter_saturated   = true;
            packet->data.AD7768_CHANNEL_DATA.has_channel_ID         = true;
            packet->data.AD7768_CHANNEL_DATA.error_flag         = reactor_od_data.error_flag;
            packet->data.AD7768_CHANNEL_DATA.filter_not_settled = reactor_od_data.filter_not_settled;
            packet->data.AD7768_CHANNEL_DATA.repeated_data      = reactor_od_data.repeated_data;
            packet->data.AD7768_CHANNEL_DATA.filter_type        = reactor_od_data.filter_type;
            packet->data.AD7768_CHANNEL_DATA.filter_saturated   = reactor_od_data.filter_saturated;
            packet->data.AD7768_CHANNEL_DATA.channel_ID         = reactor_od_data.channel_ID;
            packet->data.AD7768_CHANNEL_DATA.voltage            = reactor_od_data.voltage;

        } else if (packet->system == SystemName_MIXING_A || packet->system == SystemName_MIXING_B ||
                packet->system == SystemName_MIXING_C || packet->system == SystemName_MIXING_D) {
            
            AD7768Data mixing_weight_data = machine.mixing_bottles[mmap[packet->system]].getLoadCellData();
            packet->data.AD7768_CHANNEL_DATA.has_error_flag         = true;
            packet->data.AD7768_CHANNEL_DATA.has_filter_not_settled = true;
            packet->data.AD7768_CHANNEL_DATA.has_repeated_data      = true;
            packet->data.AD7768_CHANNEL_DATA.has_filter_type        = true;
            packet->data.AD7768_CHANNEL_DATA.has_filter_saturated   = true;
            packet->data.AD7768_CHANNEL_DATA.has_channel_ID         = true;
            packet->data.AD7768_CHANNEL_DATA.error_flag         = mixing_weight_data.error_flag;
            packet->data.AD7768_CHANNEL_DATA.filter_not_settled = mixing_weight_data.filter_not_settled;
            packet->data.AD7768_CHANNEL_DATA.repeated_data      = mixing_weight_data.repeated_data;
            packet->data.AD7768_CHANNEL_DATA.filter_type        = mixing_weight_data.filter_type;
            packet->data.AD7768_CHANNEL_DATA.filter_saturated   = mixing_weight_data.filter_saturated;
            packet->data.AD7768_CHANNEL_DATA.channel_ID         = mixing_weight_data.channel_ID;
            packet->data.AD7768_CHANNEL_DATA.voltage            = mixing_weight_data.voltage;
        }
    
    // } else if (packet->data.has_OD_LED_CURRENT_mA && packet->type == PacketType_SET) { //SET case here is to tell it we want to start reading this data with RTD ADC
    //     process_id = 12;
    //     machine.runRTDADC(false, true, 1); // This runs the RTD ADC to read the OD LED current. It takes approx 25ms to have data ready.
    //     // delay(35);
    //     // machine.runRTDADC();  
    //     // delay(35);    

    } else if (packet->data.has_OD_LED_CURRENT_mA && packet->type == PacketType_GET) { //GET here is to retrieve the value that has been stored
        process_id = 13;
        machine.runRTDADC(false, true, 1); // This makes sure that we've done an RTDADC loop so that it has saved the conversion result from the fastread command in reality not really needed as this gets run every 25ms but here in case those timings change
        delay(35);    
        machine.runRTDADC(); // This makes sure that we've done an RTDADC loop so that it has saved the conversion result from the fastread command in reality not really needed as this gets run every 25ms but here in case those timings change
        delay(35);    

        // NOTE you need to run  the set version approx 20ms before this if you want an up to date version! 
        packet->data.OD_LED_CURRENT_mA = machine.getLEDCurrent();  // This retrieves the value from the machine object

    /////////////////////////////////////////
    ////////////////// STIRRING /////////////
    /////////////////////////////////////////
    } else if (packet->data.has_STIR_IO && packet->type == PacketType_GET) {
        process_id = 14;
        packet->data.STIR_IO = machine.reactors[rmap[packet->system]].getStirIO();
    } else if (packet->data.has_STIR_IO && packet->type == PacketType_SET) {
        process_id = 15;
        machine.reactors[rmap[packet->system]].setStirIO(packet->data.STIR_IO);

    } else if (packet->data.has_STIR_POWER && packet->type == PacketType_GET) {
        process_id = 16;
        packet->data.STIR_POWER = machine.reactors[rmap[packet->system]].getStirPower();
    } else if (packet->data.has_STIR_POWER && packet->type == PacketType_SET) {
        process_id = 17;
        float power = packet->data.STIR_POWER;
        if (power < 0) power = 0;
        if (power > 100) power = 100;
        machine.reactors[rmap[packet->system]].setStirPower(power);
    
    } else if (packet->data.has_STIR_SPEED_RPM && packet->type == PacketType_GET) {
        process_id = 18;
        packet->data.STIR_SPEED_RPM = machine.reactors[rmap[packet->system]].getStirSpeed();
    } else if (packet->data.has_STIR_SPEED_RPM && packet->type == PacketType_SET) {
        process_id = 19;
        machine.reactors[rmap[packet->system]].setStirSpeed(packet->data.STIR_SPEED_RPM);
    
    } else if (packet->data.has_MICROSTEPS_PER_FULL_STEP && packet->type == PacketType_GET) {
        process_id = 20;
        packet->data.MICROSTEPS_PER_FULL_STEP = machine.reactors[rmap[packet->system]].getMicrostepsPerFullStep();
        
    } else if (packet->data.has_MICROSTEPS_PER_FULL_STEP && packet->type == PacketType_SET) {
        process_id = 21;
        machine.reactors[rmap[packet->system]].setMicrostepsPerFullStep(packet->data.MICROSTEPS_PER_FULL_STEP);
    
    /////////////////////////////////////////
    ////////////////// VISIBLE LEDS /////////
    /////////////////////////////////////////
    } else if (packet->data.has_RGB_LED && packet->type == PacketType_SET) {
        process_id = 22;
        machine.reactors[rmap[packet->system]].setRGBColor(packet->data.RGB_LED.red, packet->data.RGB_LED.green, packet->data.RGB_LED.blue, packet->data.RGB_LED.intensity);
    
    } else if (packet->data.has_RGB_LED && packet->type == PacketType_GET) {
        process_id = 23;
        packet->data.RGB_LED.has_red = true;
        packet->data.RGB_LED.has_green = true;
        packet->data.RGB_LED.has_blue = true;
        packet->data.RGB_LED.has_intensity = true;
        packet->data.RGB_LED.red = machine.reactors[rmap[packet->system]].rgb_red_8bit;
        packet->data.RGB_LED.green = machine.reactors[rmap[packet->system]].rgb_green_8bit;
        packet->data.RGB_LED.blue = machine.reactors[rmap[packet->system]].rgb_blue_8bit;
        packet->data.RGB_LED.intensity = machine.reactors[rmap[packet->system]].rgb_intensity_8bit;

    } else if (packet->data.has_REACTOR_BLUE_LED_IO && packet->type == PacketType_SET) {
        process_id = 24;
        machine.reactors[rmap[packet->system]].setBlueIndicator(packet->data.REACTOR_BLUE_LED_IO);

    } else if (packet->data.has_REACTOR_ORANGE_LED_IO && packet->type == PacketType_SET) {
        process_id = 25;
        machine.reactors[rmap[packet->system]].setOrangeIndicator(packet->data.REACTOR_ORANGE_LED_IO);

    /////////////////////////////////////////
    ////////////////// UV ///////////////////
    /////////////////////////////////////////
    } else if (packet->data.has_UV_POWER && packet->type == PacketType_GET) {
        process_id = 28;
        float uv_power = (float(machine.reactors[rmap[packet->system]].getUVDutyCycle()) / 4096.0) * 100.0;
        packet->data.UV_POWER = uv_power;
    } else if (packet->data.has_UV_POWER && packet->type == PacketType_SET) {
        process_id = 29;
        float power = packet->data.UV_POWER;
        if (power < 0) power = 0;
        if (power > 100) power = 100;
        uint16_t uv_duty_cycle = uint16_t((power / 100.0) * 4096.0);
        machine.reactors[rmap[packet->system]].setUVDutyCycle(uv_duty_cycle);


    /////////////////////////////////////////
    ////////////////// DATA OUTPUT //////////
    /////////////////////////////////////////

    ////////////////// ODS /////////////////////
    } else if (packet->data.has_FAST_ODS_OUTPUT_RATE_MS && packet->type == PacketType_SET) {
        process_id = 30;
        // This is the rate at which we send data to python. Note this is not the rate at which we read data from the ADCs.
        ODOutputIntIO(packet->data.FAST_ODS_OUTPUT_RATE_MS); 
       
    } else if (packet->data.has_FAST_ODS_OUTPUT_RATE_MS && packet->type == PacketType_GET) {
        process_id = 31;
        packet->data.FAST_ODS_OUTPUT_RATE_MS = OD_output_freq_ms; // Get the data output frequency in ms

    } else if (packet->data.has_INITIALISE_FAST_OD_READINGS && packet->type == PacketType_SET) {
        process_id = 32;
        machine.OD_FAST_READINGS_PACKET_ID = packet->unique_id;

    //////////////// WEIGHTS ////////////////////////
    } else if (packet->data.has_FAST_WEIGHT_OUTPUT_RATE_MS && packet->type == PacketType_SET) {
        process_id = 33;
        // This is the rate at which we send data to python. Note this is not the rate at which we read data from the ADCs.
        WeightOutputIntIO(packet->data.FAST_WEIGHT_OUTPUT_RATE_MS); 
       
    } else if (packet->data.has_FAST_WEIGHT_OUTPUT_RATE_MS && packet->type == PacketType_GET) {
        process_id = 34;
        packet->data.FAST_WEIGHT_OUTPUT_RATE_MS = weight_output_freq_ms; // Get the data output frequency in ms

    } else if (packet->data.has_INITIALISE_FAST_WEIGHT_READINGS && packet->type == PacketType_SET) {
        process_id = 35;
        // dependent on which system is defined on the packet a True value will output the OD for that reactor every datarate ms, a false value will stop it
        machine.WEIGHT_FAST_READINGS_PACKET_ID = packet->unique_id;

    /////////////////////////////////////////
    ////////////////// HEATING /////////////
    /////////////////////////////////////////
    } else if (packet->data.has_sendAsyncPID && packet->type == PacketType_SET) {
        process_id = 36;
        if (        packet->system == SystemName_REACTOR_AL || packet->system == SystemName_REACTOR_AR ||
            packet->system == SystemName_REACTOR_BL || packet->system == SystemName_REACTOR_BR ||
            packet->system == SystemName_REACTOR_CL || packet->system == SystemName_REACTOR_CR ||
            packet->system == SystemName_REACTOR_DL || packet->system == SystemName_REACTOR_DR) {
            
            machine.reactors[rmap[packet->system]].send_pid_packets = packet->data.sendAsyncPID;
            machine.reactors[rmap[packet->system]].pid_packet_id    = packet->unique_id;

        } else if (packet->system == SystemName_MIXING_A || packet->system == SystemName_MIXING_B ||
                packet->system == SystemName_MIXING_C || packet->system == SystemName_MIXING_D) {
            
            machine.mixing_bottles[mmap[packet->system]].send_pid_packets = packet->data.sendAsyncPID;
            machine.mixing_bottles[mmap[packet->system]].pid_packet_id    = packet->unique_id;
        }

    } else if (packet->data.has_PID && packet->type == PacketType_GET) {
        process_id = 37;
        packet->data.PID.has_error_p                = true;
        packet->data.PID.has_error_i                = true;
        packet->data.PID.has_error_d                = true;
        packet->data.PID.has_kp                     = true;
        packet->data.PID.has_ki                     = true;
        packet->data.PID.has_kd                     = true;
        packet->data.PID.has_setpoint               = true;
        packet->data.PID.has_temperature            = true;
        packet->data.PID.has_duty_cycle             = true;
        packet->data.PID.has_timestamp              = true;
        packet->data.PID.timestamp = millis();

        if (        packet->system == SystemName_REACTOR_AL || packet->system == SystemName_REACTOR_AR ||
                    packet->system == SystemName_REACTOR_BL || packet->system == SystemName_REACTOR_BR ||
                    packet->system == SystemName_REACTOR_CL || packet->system == SystemName_REACTOR_CR ||
                    packet->system == SystemName_REACTOR_DL || packet->system == SystemName_REACTOR_DR) {
            packet->data.PID.error_p                = machine.reactors[rmap[packet->system]].p_term;
            packet->data.PID.error_i                = machine.reactors[rmap[packet->system]].i_term;
            packet->data.PID.error_d                = machine.reactors[rmap[packet->system]].d_term;
            packet->data.PID.kp                     = machine.reactors[rmap[packet->system]].kp;
            packet->data.PID.ki                     = machine.reactors[rmap[packet->system]].ki;
            packet->data.PID.kd                     = machine.reactors[rmap[packet->system]].kd;
            packet->data.PID.setpoint               = machine.reactors[rmap[packet->system]].getTargetTemp();
            packet->data.PID.temperature            = machine.reactors[rmap[packet->system]].getCuvetteTemp();
            packet->data.PID.duty_cycle             = machine.reactors[rmap[packet->system]].getCuvetteHeaterDutyCycle();

        } else if (packet->system == SystemName_MIXING_A || packet->system == SystemName_MIXING_B ||
                   packet->system == SystemName_MIXING_C || packet->system == SystemName_MIXING_D) {
            packet->data.PID.error_p                = machine.mixing_bottles[mmap[packet->system]].p_term;
            packet->data.PID.error_i                = machine.mixing_bottles[mmap[packet->system]].i_term;
            packet->data.PID.error_d                = machine.mixing_bottles[mmap[packet->system]].d_term;
            packet->data.PID.kp                     = machine.mixing_bottles[mmap[packet->system]].kp;
            packet->data.PID.ki                     = machine.mixing_bottles[mmap[packet->system]].ki;
            packet->data.PID.kd                     = machine.mixing_bottles[mmap[packet->system]].kd;
            packet->data.PID.setpoint               = machine.mixing_bottles[mmap[packet->system]].getTargetTemp();
            packet->data.PID.temperature            = machine.mixing_bottles[mmap[packet->system]].getMixingTemp();
            packet->data.PID.duty_cycle             = machine.mixing_bottles[mmap[packet->system]].getHeaterDuty();
        } else {
            // Do nothing
        }



    } else if (packet->data.has_PCB_TEMPERATURE_CELSIUS && packet->data.has_PCB_SENSOR && packet->type == PacketType_GET) {
        // this whole function is kind of redundant now we're doing constant health monitoring
        process_id = 38;
        if (packet->data.PCB_SENSOR == PCBSensorNames_SENSEBOARD_TMP) {
            CHIPS.Senseboard_TMP.updateTemp(); 
            packet->data.PCB_TEMPERATURE_CELSIUS = CHIPS.Senseboard_TMP.getTemp();
        } else if (packet->data.PCB_SENSOR == PCBSensorNames_TOPBOARD_TMP12) {
            CHIPS.Topboard_Thermo_MUX.setChannel(4);
            delay(100);
            CHIPS.Topboard_TMP_1.updateTemp(); 
            packet->data.PCB_TEMPERATURE_CELSIUS = CHIPS.Topboard_TMP_1.getTemp();
        } else if (packet->data.PCB_SENSOR == PCBSensorNames_TOPBOARD_TMP45) {
            CHIPS.Topboard_Thermo_MUX.setChannel(5);
            delay(100);
            CHIPS.Topboard_TMP_2.updateTemp();
            packet->data.PCB_TEMPERATURE_CELSIUS = CHIPS.Topboard_TMP_2.getTemp();
        } else if (packet->data.PCB_SENSOR == PCBSensorNames_TOPBOARD_TMP78) {
            CHIPS.Topboard_Thermo_MUX.setChannel(6);
            delay(100);
            CHIPS.Topboard_TMP_3.updateTemp();
            packet->data.PCB_TEMPERATURE_CELSIUS = CHIPS.Topboard_TMP_3.getTemp();
        } else if (packet->data.PCB_SENSOR == PCBSensorNames_SENSEBOARD_HUM) {
            packet->data.PCB_TEMPERATURE_CELSIUS = CHIPS.Senseboard_HDC.readContinuous().temperature;
        } else if (packet->data.PCB_SENSOR == PCBSensorNames_SENSEBOARD_ACCEL) {
            packet->data.PCB_TEMPERATURE_CELSIUS = CHIPS.Senseboard_ACCEL.readTemperature();
        }

    } else if (packet->data.has_CALIBRATE_RTD_ADC && packet->type == PacketType_SET) {
        process_id = 39;
        machine.runRTDADC(true); // This runs the calibration of the RTD readout ADC which takes approx 5 seconds and you should do once at startup.

    //Read and set the heating on off state and duty cycle in the Mixing Bottless 
    } else if (packet->data.has_MIXING_TEMPERATURE_CELSIUS && packet->type == PacketType_GET) {
        process_id = 40;
        CHIPS.Topboard_Thermo_MUX.setChannel(mmap[packet->system]);
        delay(100);
        float mixing_temp = CHIPS.IR_Temp_Sensor.getObjectTempC();
        teensy_device.sendInfo("Read mixing temp as %.2f channel set to %d", mixing_temp, mmap[packet->system]);
        machine.mixing_bottles[mmap[packet->system]].updateMixingTemp(mixing_temp); // update the mixing bottle temp in the mixing object
        packet->data.MIXING_TEMPERATURE_CELSIUS = machine.mixing_bottles[mmap[packet->system]].getMixingTemp();

    } else if (packet->data.has_MIXING_TARGET_TEMP_CELSIUS && packet->type == PacketType_GET) {
        process_id = 41;
        packet->data.MIXING_TARGET_TEMP_CELSIUS = machine.mixing_bottles[mmap[packet->system]].getTargetTemp();
    } else if (packet->data.has_MIXING_TARGET_TEMP_CELSIUS && packet->type == PacketType_SET) {
        process_id = 42;
        machine.mixing_bottles[mmap[packet->system]].setTargetTemp(packet->data.MIXING_TARGET_TEMP_CELSIUS);

    } else if (packet->data.has_MIXING_HEATER_PID_IO && packet->type == PacketType_SET) {
        process_id = 43;
        machine.mixing_bottles[mmap[packet->system]].setPIDIO(packet->data.MIXING_HEATER_PID_IO);
    } else if (packet->data.has_MIXING_HEATER_PID_IO && packet->type == PacketType_GET) {
        process_id = 44;
        packet->data.MIXING_HEATER_PID_IO = machine.mixing_bottles[mmap[packet->system]].getPIDIO(); 

    } else if (packet->data.has_MIXING_HEATER_POWER && packet->type == PacketType_GET) {
        process_id = 45;
        float power = (float(machine.mixing_bottles[mmap[packet->system]].getHeaterDuty()) / 4096.0) * 100.0;
        packet->data.MIXING_HEATER_POWER = power;
    } else if (packet->data.has_MIXING_HEATER_POWER && packet->type == PacketType_SET) {
        process_id = 46;
        float power = packet->data.MIXING_HEATER_POWER;
        if (power < 0) power = 0;
        if (power > 100) power = 100;
        uint16_t duty_cycle = uint16_t((power / 100.0) * 4096.0);
        machine.mixing_bottles[mmap[packet->system]].setHeaterDuty(duty_cycle);

    //read and set the heating duty cycles in the reactors
    } else if (packet->data.has_REACTOR_TEMPERATURE_CELSIUS && packet->type == PacketType_GET) {
        process_id = 47;
        packet->data.REACTOR_TEMPERATURE_CELSIUS = machine.reactors[rmap[packet->system]].getCuvetteTemp();
    
    } else if (packet->data.has_REACTOR_TARGET_TEMP_CELSIUS && packet->type == PacketType_GET) {
        process_id = 48;
        packet->data.REACTOR_TARGET_TEMP_CELSIUS = machine.reactors[rmap[packet->system]].getTargetTemp();
    } else if (packet->data.has_REACTOR_TARGET_TEMP_CELSIUS && packet->type == PacketType_SET) {
        process_id = 49;
        machine.reactors[rmap[packet->system]].setTargetTemp(packet->data.REACTOR_TARGET_TEMP_CELSIUS);
    
    } else if (packet->data.has_REACTOR_HEATER_PID_IO && packet->type == PacketType_SET ){
        process_id = 50;
        machine.reactors[rmap[packet->system]].setPIDIO(packet->data.REACTOR_HEATER_PID_IO);
    } else if (packet->data.has_REACTOR_HEATER_PID_IO && packet->type == PacketType_GET){
        process_id = 51;
        packet ->data.REACTOR_HEATER_PID_IO = machine.reactors[rmap[packet->system]].getPIDIO();

    } else if (packet->data.has_REACTOR_HEATER_POWER && packet->type == PacketType_SET) {
        process_id = 52;
        float power = packet->data.REACTOR_HEATER_POWER;
        if (power < 0) power = 0;
        if (power > 100) power = 100;
        uint16_t duty_cycle = uint16_t((power / 100.0) * 4096.0);
        machine.reactors[rmap[packet->system]].setCuvetteHeaterDutyCycle(duty_cycle);
    } else if (packet->data.has_REACTOR_HEATER_POWER && packet->type == PacketType_GET) {
        process_id = 53;
        float heater_power = (float(machine.reactors[rmap[packet->system]].getCuvetteHeaterDutyCycle()) / 4096.0) * 100.0;
        packet->data.REACTOR_HEATER_POWER = heater_power;


    /////////////////////////////////////////
    ////////////////// SCALES ///////////////
    /////////////////////////////////////////
    } else if (packet->data.has_SCALES_VOLTAGE && packet->type == PacketType_GET) {
        process_id = 54;
        if (packet->system == SystemName_MIXING_A || packet->system == SystemName_MIXING_B || packet->system == SystemName_MIXING_C || packet->system == SystemName_MIXING_D) {
            packet->data.SCALES_VOLTAGE = machine.mixing_bottles[mmap[packet->system]].getLoadCellData().voltage;
        } else if (packet->system == SystemName_MACHINE) {
            packet->data.SCALES_VOLTAGE = machine.getWasteLoadCellVoltage();
        } else {
            teensy_device.sendError("System %d is not a mixing bottle system", packet->system);
            packet->data.SCALES_VOLTAGE = 0;
        }
    
    } else if (packet->data.LOAD_CELL_BITS_count > 0 && packet->type == PacketType_GET) {
        process_id = 55;
        for (int i = 0; i<packet->data.LOAD_CELL_BITS_count; i++) {
            packet->data.LOAD_CELL_BITS[i] = CHIPS.Topboard_ADC.getADCBits()[i];
        }
    
    /////////////////////////////////////////
    ////////////////// FANS /////////////////
    /////////////////////////////////////////
    } else if (packet->data.has_FAN_POWER && packet->type == PacketType_GET) {
        process_id = 56;
        float power = (float(machine.getFanDuty()) / 4096.0) * 100.0;
        packet->data.FAN_POWER = power;
    } else if (packet->data.has_FAN_POWER && packet->type == PacketType_SET) {
        process_id = 57;
        float power = packet->data.FAN_POWER;
        if (power < 0) power = 0;
        if (power > 100) power = 100;
        uint16_t duty_cycle = uint16_t((power / 100.0) * 4096.0);
        machine.setFanDuty(duty_cycle);
    
    /////////////////////////////////////////
    ////////////////// MISC BOARD SENSORS ///
    /////////////////////////////////////////
    } else if (packet->data.has_HUMIDITY_PERCENTAGE && packet->type == PacketType_GET) {
        process_id = 58;
        packet->data.HUMIDITY_PERCENTAGE = CHIPS.Senseboard_HDC.readContinuous().humidity;
    
    } else if (packet->data.has_ACCELEROMETER && packet->type == PacketType_GET) {
        process_id = 59;
        packet->data.ACCELEROMETER.has_x = true;
        packet->data.ACCELEROMETER.has_y = true;
        packet->data.ACCELEROMETER.has_z = true;
        FXLS8964::FXLS8964Data accel_data = CHIPS.Senseboard_ACCEL.readAccel();
        // uint8_t accel_data = CHIPS.Senseboard_ACCEL.clearInterrupt();
        packet->data.ACCELEROMETER.x = accel_data.x;
        packet->data.ACCELEROMETER.y = accel_data.y;
        packet->data.ACCELEROMETER.z = accel_data.z;
    
    } else if (packet->data.has_sendAsyncFluidDetected && packet->type == PacketType_SET) {
        process_id = 60;
        // packet->data.FLUID_SENSOR is an enum between 1 and 16 corresponding to liq sensor number, 
        // fluid sensor 1 is requested by sending 0 to SetGPIOMode, so we -1 from the enum value

        teensy_device.sendDebug("Fluid sensor selected: %d", machine.selected_fluid_sensor);
        initial_reading_stabilised = false;  // Reset the initial reading stabilised flag
        fluid_change_detected = false;
        machine.fluid_sensor_packet_id = packet->unique_id;  // Store the packet ID so we can send it back when fluid is detected
        if (packet->data.sendAsyncFluidDetected) {
            CHIPS.Topboard_ADC.SetGPIOMode(machine.selected_fluid_sensor);
            if (packet->data.has_TIMEOUT_MS) {
                machine.fluid_sensor_cutoff_time_ms = packet->data.TIMEOUT_MS;
            } else {
                teensy_device.sendDebug("No timeout given, default is 10s");
            }
            
            fluid_sensor_timer.begin(FluidSensorInterruptHandler, machine.fluid_sensor_polling_rate_us);  // Set the timer to call the fluid sensor timer callback every 0.5 second (500000 microseconds)    
            fluid_sensor_detect_cutoff_timer = 0;

        } else {
            teensy_device.sendDebug("Manually stopped fluid detection routine");
            if (packet->data.has_FLUID_SENSOR){
                if (machine.selected_fluid_sensor != (packet->data.FLUID_SENSOR-1)){
                    teensy_device.sendDebug("Requested stop for sensor %d but active sensor is: %d", packet->data.FLUID_SENSOR-1, machine.selected_fluid_sensor);
                }
            }
            fluid_sensor_timer.end();
        }

    } else if (packet->data.has_FLUID_SENSOR && packet->type == PacketType_SET) {
        process_id = 61;
        // packet->data.FLUID_SENSOR is an enum between 1 and 16 corresponding to liq sensor number, 
        // fluid sensor 1 is requested by sending 0 to SetGPIOMode, so we -1 from the enum value
        machine.selected_fluid_sensor = uint8_t(packet->data.FLUID_SENSOR) - 1;
        // TODO check if its safe to switch gpiomode here
        CHIPS.Topboard_ADC.SetGPIOMode(machine.selected_fluid_sensor);
    
    } else if (packet->data.has_FLUID_SENSOR && packet->type == PacketType_GET) {
        process_id = 62;
        // packet->data.FLUID_SENSOR is an enum between 1 and 16 corresponding to liq sensor number, 
        // fluid sensor 1 is requested by sending 0 to SetGPIOMode, so we -1 from the enum value
        packet->data.FLUID_SENSOR = FluidSensorNames(machine.selected_fluid_sensor + 1);

    } else if (packet->data.has_FLUID_SENSOR_VOLTAGE && packet->type == PacketType_GET) {
        process_id = 63;
        packet->data.FLUID_SENSOR_VOLTAGE = machine.getFluidSensorVoltage();
    /////////////////////////////////////////
    ////////////////// SYSTEM ///////////////
    /////////////////////////////////////////
    } else if (packet->data.has_SYSTEM_IO && packet->type == PacketType_GET) {
        process_id = 64;
        packet->data.SYSTEM_IO = systemEnable;
    } else if (packet->data.has_SYSTEM_IO && packet->type == PacketType_SET) {
        process_id = 65;
        if (packet->data.SYSTEM_IO == true && systemEnable == true) {
            teensy_device.sendInfo("System already enabled, doing nothing");
        } else {
            enableSystem(packet->data.SYSTEM_IO);
        }
    
    // Current sensing on the heatboard
    } else if (packet->data.has_UV_CURRENT_mA && packet->type == PacketType_GET) {
        process_id = 66;
        machine.setCurrentChannel(HEATBOARD_CURRENT_SELECTION::UV);
        delay(ADS122C04_CONVERSION_TIME); // small delay for ADC conversion
        packet->data.UV_CURRENT_mA = machine.getCurrent();

    } else if (packet->data.has_RGB_CURRENT_mA && packet->type == PacketType_GET) {
        process_id = 67;
        machine.setCurrentChannel(HEATBOARD_CURRENT_SELECTION::RGB);
        delay(ADS122C04_CONVERSION_TIME); // small delay for ADC conversion
        packet->data.RGB_CURRENT_mA = machine.getCurrent();

    } else if (packet->data.has_STIR_CURRENT_mA && packet->type == PacketType_GET) {
        process_id = 68;
        machine.setCurrentChannel(HEATBOARD_CURRENT_SELECTION::STIR);
        delay(ADS122C04_CONVERSION_TIME); // small delay for ADC conversion
        packet->data.STIR_CURRENT_mA = machine.getCurrent();

    } else if (packet->data.has_REACTOR_HEATER_CURRENT_mA && packet->type == PacketType_GET) {
        process_id = 69;
        machine.setCurrentChannel(HEATBOARD_CURRENT_SELECTION::REACTOR_HEATER);
        delay(ADS122C04_CONVERSION_TIME); // small delay for ADC conversion
        packet->data.REACTOR_HEATER_CURRENT_mA = machine.getCurrent();

    } else if (packet->data.has_MIXING_HEATER_CURRENT_mA && packet->type == PacketType_GET) {
        process_id = 70;
        packet->data.MIXING_HEATER_CURRENT_mA = machine.getMixingHeaterCurrent();

    } else if (packet->data.has_FAN_CURRENT_mA && packet->type == PacketType_GET) {
        process_id = 71;
        machine.setCurrentChannel(HEATBOARD_CURRENT_SELECTION::FAN);
        delay(ADS122C04_CONVERSION_TIME); // small delay for ADC conversion
        packet->data.FAN_CURRENT_mA = machine.getCurrent();

    } else if (packet->data.has_VALVE_CURRENT_mA && packet->type == PacketType_GET) {
        process_id = 72;
        machine.setCurrentChannel(HEATBOARD_CURRENT_SELECTION::VALVE);
        delay(ADS122C04_CONVERSION_TIME); // small delay for ADC conversion
        packet->data.VALVE_CURRENT_mA = machine.getCurrent();

    } else if (packet->data.has_MACHINE_CURRENT_mA && packet->type == PacketType_GET) {
        process_id = 73;
        machine.setCurrentChannel(HEATBOARD_CURRENT_SELECTION::MACHINE);
        delay(ADS122C04_CONVERSION_TIME); // small delay for ADC conversion
        packet->data.MACHINE_CURRENT_mA = machine.getCurrent();
     
    /////////////////////////////////////////
    ///////////////// TESTING ///////////////
    /////////////////////////////////////////
    } else if (packet->data.has_SCAN_I2C_CHANNELS) {
        process_id = 74;
        scanI2C();


    // } else if (packet->data.has_TEENSY_FREQ && packet->type == PacketType_SET) {
    //     analogWriteFrequency(23, packet->data.TEENSY_FREQ);  // Set the frequency of pin 18
    
    // } else if (packet->data.has_TEENSY_DUTY && packet->type == PacketType_SET) {
    //     analogWrite(23, packet->data.TEENSY_DUTY); 
    
    // } else if (packet->data.has_TEENSY_RES && packet->type == PacketType_SET) {
    //     analogWriteResolution(packet->data.TEENSY_RES); 

    // } else if (packet->data.has_run_debug_script_cg) { ////////////////////////////////////////////////////////////////////////////////////////////////////
        // keep_open = true; // this means the python will keep listening for more data until it receives close
        // test_packet_id = packet->unique_id;
        // test_timeout = 0;
        // // caleb_test_timer.begin(CalebTestInterrupt, 1000000);
    
    } else if (packet->data.has_run_debug_script && packet->type == PacketType_SET) {
        process_id = 75;
        // This is a debug packet, we can do whatever we want with it
        if (packet->data.run_debug_script == 1) {
            teensy_device.sendInfo("Running debug script 1: Init Heatboard DAC");
            CHIPS.Heatboard_DAC.Initialise();
        } else if (packet->data.run_debug_script == 2) {
            teensy_device.sendInfo("Running debug script 2: Init Senseboard DAC");
            CHIPS.Senseboard_DAC.Initialise();
        } else if (packet->data.run_debug_script == 3) {
            teensy_device.sendInfo("Running debug script 3: Crash Teensy");
            crash_teensy();
        } else if (packet->data.run_debug_script == 4) {
            teensy_device.sendInfo("Running debug script 4: Enable RTD");
            CHIPS.Senseboard_IO_1.digitalPinWrite(RTD_ENABLE_PIN, 1);
        } else if (packet->data.run_debug_script == 5) {
            teensy_device.sendInfo("Running debug script 5: Disable RTD");
            CHIPS.Senseboard_IO_1.digitalPinWrite(RTD_ENABLE_PIN, 0);
        } else if (packet->data.run_debug_script == 6) {
            teensy_device.sendInfo("Running debug script 6: RESET AD7768");
            CHIPS.Senseboard_ADC.Initialise(dmaBufferOD);
        } else if (packet->data.run_debug_script == 7) {
            teensy_device.sendInfo("Running debug script 7: AD7768 GPIO SET: %d", AD7768_debug_counter);
            // CHIPS.Senseboard_ADC.SetGPIOMode(AD7768_debug_counter); // Set to no chopping
            CHIPS.Senseboard_ADC.readStatusReg();
            AD7768_debug_counter++;
            if (AD7768_debug_counter > 3) {

                // CHIPS.Senseboard_ADC.Initialise(dmaBufferOD);
                // CHIPS.Senseboard_ADC.readStatusReg();
                AD7768_debug_counter = 0;
            }
        } else if (packet->data.run_debug_script == 8) {
            IOInterruptHandler2();
        
        } else {
            teensy_device.sendInfo("Debug script %d not recognised", packet->data.run_debug_script);
        }

    } else if (packet->data.has_SYNC_TIMESTAMP) {
        process_id = 76;
        packet->data.SYNC_TIMESTAMP = millis();

    } else if (packet->data.has_CLEAR_BUFFERS) {
        process_id = 77;
        // Clear the input and output buffers
        teensy_device.clearBuffer();
        // teensy_device.sendError("Buffers cleared");

    } else if (packet->data.has_INITIALISE_MEMORY_REPORT && packet->type == PacketType_SET) {
        process_id = 78;
        machine.MEMORY_REPORT_PACKET_ID = packet->unique_id;
        machine.SEND_MEMORY_REPORT = packet->data.INITIALISE_MEMORY_REPORT;

    } else if (packet->data.has_INITIALISE_CURRENT_REPORT && packet->type == PacketType_SET) {
        process_id = 79;
        machine.CURRENT_REPORT_PACKET_ID = packet->unique_id;
        machine.SEND_CURRENT_REPORT = packet->data.INITIALISE_CURRENT_REPORT;

    } else if (packet->data.has_INITIALISE_REACTOR_TEMPERATURE_REPORT && packet->type == PacketType_SET) {
        process_id = 80;
        machine.REACTOR_TEMPERATURE_REPORT_PACKET_ID = packet->unique_id;
        machine.SEND_REACTOR_TEMPERATURE_REPORT = packet->data.INITIALISE_REACTOR_TEMPERATURE_REPORT;

    } else if (packet->data.has_INITIALISE_OTHER_TEMPERATURE_REPORT && packet->type == PacketType_SET) {
        process_id = 81;
        machine.OTHER_TEMPERATURE_REPORT_PACKET_ID = packet->unique_id;
        machine.SEND_OTHER_TEMPERATURE_REPORT = packet->data.INITIALISE_OTHER_TEMPERATURE_REPORT;
    
    } else if (packet->data.has_SEND_AUTO_READINGS && packet->type == PacketType_SET) {
        process_id = 82;
        send_auto_readings(packet->data.SEND_AUTO_READINGS);

    } else if (packet->data.FIND_I2C_CHANNELS_count > 0 && packet->type == PacketType_GET) {
        process_id = 83;
        findI2C(packet->data.FIND_I2C_CHANNELS);

    } else if (packet->data.has_PERISTALTIC_POWER && packet->type == PacketType_SET) {
        process_id = 84;
        float power = packet->data.PERISTALTIC_POWER;
        if (power < 0) power = 0;
        if (power > 100) power = 100;
        uint16_t duty_cycle = uint16_t((power / 100.0) * 4096.0);
        machine.setPeristalticDuty(duty_cycle);
    
    } else if (packet->data.has_PERISTALTIC_POWER && packet->type == PacketType_GET) {
        process_id = 85;
        float power = (float(machine.getPeristalticDuty()) / 4096.0) * 100.0;
        packet->data.PERISTALTIC_POWER = power;
    
    } else if (packet->data.has_CHIP_NAME && packet->data.has_PIN && packet->type == PacketType_SET) {
        process_id = 86;
        uint8_t chip_id = packet->data.CHIP_NAME;
        if (chip_id >= 1 && chip_id <= 7) {
            // IO CHIPS - either set a pin high/low or read a pin
            if (!packet->data.has_PIN || !packet->data.has_IO_STATE) {
                teensy_device.sendError("IO CHIP command requires both PIN and IO_STATE to be set");
            } else {
                PCA9505* target_chip = nullptr;
                if (chip_id == 1) {
                    target_chip = &CHIPS.Heatboard_IO_1;
                } else if (chip_id == 2) {
                    target_chip = &CHIPS.Heatboard_IO_2;
                } else if (chip_id == 3) {
                    target_chip = &CHIPS.Heatboard_IO_3;
                } else if (chip_id == 4) {
                    target_chip = &CHIPS.Heatboard_IO_4;
                } else if (chip_id == 5) {
                    target_chip = &CHIPS.Senseboard_IO_1;
                } else if (chip_id == 6) {
                    target_chip = &CHIPS.Topboard_IO_1;
                } else if (chip_id == 7) {
                    target_chip = &CHIPS.Topboard_IO_2;
                } 
                if (target_chip != nullptr) {
                    target_chip->digitalPinWrite(packet->data.PIN, packet->data.IO_STATE);
                }
            }

        } else if (chip_id >= 8 && chip_id <= 12) {
            // PWM CHIPS - set duty cycle and frequency 
            if (!packet->data.has_PIN || !packet->data.has_DUTY_CYCLE) {
                teensy_device.sendError("PWM CHIP command requires PIN, and DUTY_CYCLE to be set");
            } else {
                PCA9685* target_chip = nullptr;
                if (chip_id == 8) {
                    target_chip = &CHIPS.Heatboard_PWM_1;
                } else if (chip_id == 9) {
                    target_chip = &CHIPS.Heatboard_PWM_2;
                } else if (chip_id == 10) {
                    target_chip = &CHIPS.Heatboard_PWM_3;
                } else if (chip_id == 11) {
                    target_chip = &CHIPS.Topboard_PWM_1;
                } else if (chip_id == 12) {
                    target_chip = &CHIPS.Topboard_PWM_2;
                } 
                if (target_chip != nullptr) {
                    target_chip->SetPWM(packet->data.PIN, packet->data.DUTY_CYCLE);
                }
            }
        
        } else if (chip_id >= 13 && chip_id <= 14) {
            // DAC CHIPS - set a voltage
            if (!packet->data.has_PIN || !packet->data.has_VOLTAGE) {
                teensy_device.sendError("DAC CHIP command requires PIN, and VOLTAGE to be set");
            } else {
                if (chip_id == 13) {
                    AD5673* target_chip = &CHIPS.Heatboard_DAC;
                    target_chip->SetVoltage(packet->data.PIN, packet->data.VOLTAGE);
                } else if (chip_id == 14) {
                    AD5675* target_chip = &CHIPS.Senseboard_DAC;
                    target_chip->SetVoltage(packet->data.PIN, packet->data.VOLTAGE);
                }
            }

        } else {
            teensy_device.sendError("Chip ID %d not recognised", chip_id);
        }
    } else if (packet->data.has_CHIP_NAME && packet->data.has_STRESS_TEST) {
        // Stress test i2c chip
        process_id = 87;

        uint32_t num_testing_times = packet->data.STRESS_TEST;
        uint8_t testing_delay = 2; //ms - iteration time is 2x this

        uint8_t chip_id = packet->data.CHIP_NAME;
        if (chip_id >= 1 && chip_id <= 7) {
            // IO CHIPS - either set a pin high/low or read a pin
            PCA9505* target_chip = nullptr;
            if (chip_id == 1) {
                target_chip = &CHIPS.Heatboard_IO_1;
            } else if (chip_id == 2) {
                target_chip = &CHIPS.Heatboard_IO_2;
            } else if (chip_id == 3) {
                target_chip = &CHIPS.Heatboard_IO_3;
            } else if (chip_id == 4) {
                target_chip = &CHIPS.Heatboard_IO_4;
            } else if (chip_id == 5) {
                target_chip = &CHIPS.Senseboard_IO_1;
            } else if (chip_id == 6) {
                target_chip = &CHIPS.Topboard_IO_1;
            } else if (chip_id == 7) {
                target_chip = &CHIPS.Topboard_IO_2;
            } 
            // Toggle all pins that are set to be output on and off 100 times with a short delay, then restore state
            if (target_chip != nullptr) {
                for (uint8_t bank = 0; bank <= 4; bank++){
                    uint8_t initial_state = target_chip->digitalBankRead(bank); // Read the initial state of the bank so we can restore it later
                    for (uint32_t testing_times = 0; testing_times < num_testing_times; testing_times++){
                        target_chip->digitalBankWrite(bank, 0b00000000); // Set all pins to off
                        delay(testing_delay);
                        target_chip->digitalBankWrite(bank, 0b11111111); // Set all pins to on
                        delay(testing_delay);
                    }
                    target_chip->digitalBankWrite(bank, initial_state); // Restore initial state
                    // teensy_device.sendInfo("Bank %d of chip %d stress test complete, state restored to 0b%08b", bank, chip_id, initial_state);
                }
            }

        } else if (chip_id >= 8 && chip_id <= 12) {
            // PWM CHIPS - set duty cycle and frequency 
            PCA9685* target_chip = nullptr;
            // Safe pin is a pin that will not cause chaos if turned on, most of these are NC but heatboardPWM3 uses all pins so an RGB light will come on 
            uint8_t safe_pin = 0;
            if (chip_id == 8) {
                target_chip = &CHIPS.Heatboard_PWM_1;
                safe_pin = 21; //NC
            } else if (chip_id == 9) {
                target_chip = &CHIPS.Heatboard_PWM_2;
                safe_pin = 15; // NC
            } else if (chip_id == 10) {
                target_chip = &CHIPS.Heatboard_PWM_3;
                safe_pin = 22; // RGB shenanigans (will probably turn the led on)
            } else if (chip_id == 11) {
                target_chip = &CHIPS.Topboard_PWM_1;
                safe_pin = 15; // NC
            } else if (chip_id == 12) {
                target_chip = &CHIPS.Topboard_PWM_2;
                safe_pin = 7; // NC
            } 
            // Change between two duty cycles 100 times with a short delay, then restore state
            if (target_chip != nullptr) {
                for (uint32_t testing_times = 0; testing_times < num_testing_times; testing_times++){
                    target_chip->SetPWM(safe_pin, 4096);
                    delay(testing_delay);
                    target_chip->SetPWM(safe_pin, 0);
                    delay(testing_delay);
                }
            }
            
        } else if (chip_id >= 13 && chip_id <= 14) {
            // DAC CHIPS - set a voltage
            if (chip_id == 13) {
                AD5673* target_chip = &CHIPS.Heatboard_DAC;
                uint8_t safe_pin = 28; // This will just be changing a voltage that set microstepping level on stirrer
                for (uint32_t testing_times = 0; testing_times < num_testing_times; testing_times++){
                    target_chip->SetVoltage(safe_pin, 1.0);
                    delay(testing_delay);
                    target_chip->SetVoltage(safe_pin, 0);
                    delay(testing_delay);
                }
            } else if (chip_id == 14) {
                AD5675* target_chip = &CHIPS.Senseboard_DAC;
                uint8_t safe_channel = 5; // NC
                for (uint32_t testing_times = 0; testing_times < num_testing_times; testing_times++){
                    target_chip->SetVoltage(safe_channel, 1.0);
                    delay(testing_delay);
                    target_chip->SetVoltage(safe_channel, 0);
                    delay(testing_delay);
                }
            }
            

        } else {
            teensy_device.sendError("Chip ID %d not recognised", chip_id);
        }
    } else if (packet->data.has_HEARTBEAT && packet->data.HEARTBEAT) {
        process_id = 88;
        if (systemEnable == true) {
            heartbeat_started = true;
        }
        heartbeat_timer = 0; // reset the heartbeat timer
    
    } else if (packet->data.has_VALVE_LEDS && packet->type == PacketType_SET) {
        process_id = 89;
        machine.enable_valve_leds = packet->data.VALVE_LEDS;

    } else if (packet->data.has_ENABLE_FLUID_SENSORS && packet->type == PacketType_SET) {
        process_id = 108;
        if(packet->data.ENABLE_FLUID_SENSORS){
            CHIPS.Topboard_PWM_1.SetPWM(FLUIDSENSE_EN_PIN, 4096);
        } else {
            CHIPS.Topboard_PWM_1.SetPWM(FLUIDSENSE_EN_PIN, 0);
        }
    /////////////////////////////////////////
    ////////////     END          ///////////
    /////////////////////////////////////////
    } else {
        process_id = 0;
        // teensy_device.sendError("Requested data not recognised");
        packet->type = PacketType_ERROR;  // Set the packet type to error
        packet->has_unique_id = false;
        packet->data.has_info = true;
        strncpy(packet->data.info, "Requested data not recognised", sizeof(packet->data.info)-1);
    }

    if (packet_timer > max_process_time_us && process_id != 65 && process_id != 77) { // dont count the enable/disable process time as its not really relevant, also dont count clearbuffers (normally around 1.8ms)
        max_process_time_us = packet_timer;
        max_process_id = process_id;
    }
    
    // Set the type of acknowledgement to send back to python
    if (packet->type == PacketType_GET){
        packet->type = PacketType_DATA;  // Get packets respond with data
    } else if (packet->type == PacketType_SET) {
        packet->type = PacketType_ACK; // Set packets should respond with no data, just an acknowledgement that the command has been received
    }

    packet->has_timestamp = true;
    packet->timestamp = millis();

    return;
    
}

Packet received_packet = Packet_init_zero;
int8_t received_flag = 0;

void setup() {
    //First there are various common things we need to do no matter what we are up to - these are things that will persist across disable/enable cycles
    pinModeFast(Heartbeat, OUTPUT);    // Set the heartbeat pin to output and then set its value to low
    
    #if GENERATION == V1_MACHINES
        pinMode(Senseboard_PowerEnable, OUTPUT);
        pinMode(Topboard_and_Heatboard_PowerEnable, OUTPUT);
        pinMode(Actuators_Enable, OUTPUT); 
    #elif GENERATION == V2_MACHINES || GENERATION == V3_MACHINES
        pinMode(D_Power_En, OUTPUT);  // Set up power enable pin
        pinMode(P_Power_En, OUTPUT);  // Set up power enable pin
    #endif

    pinMode(ADC_Reset, OUTPUT);  // Set up the ADC Reseting pin. Note this is common across various ADCs so we do it here.
    pinMode(I2C_Reset, OUTPUT);  // Set up the I2C Reseting pin. Note this is common across various I2C devices so we do it here.
    
    #if GENERATION == V2_MACHINES || GENERATION == V3_MACHINES
        pinMode(I2C_MUX_Reset, OUTPUT); // Set up the Mixing thermometer I2C multiplexer reseting pin. 
    #endif

    pinMode(Load_Cell_Chop, OUTPUT);  // Set up the Load Cell Chopping pin. 
    pinMode(Other_Interrupt, INPUT);
    pinMode(Accel_Interrupt, INPUT);
    pinMode(LED_BUILTIN, OUTPUT);  // Initialize the LED_BUILTIN pin as an output. Note this is going to get overwritten in enabling since Spi CLK shares this pin
    digitalWrite(LED_BUILTIN, HIGH);  // Turn the LED on (LOW is the voltage level for the LED to be on)
    
    pinModeFast(Senseboard_ADC_CS, OUTPUT); //Here need to set whatever teensy pins are to be used for SPI. This is AD7768 on Senseboard
    pinModeFast(Senseboard_ADC2_CS, OUTPUT); //This is ADS124S08 on Senseboard
    pinModeFast(Topboard_ADC_CS, OUTPUT); //This is ADS7768 on Topboard

    pinMode(IR_SYNC_PIN_AL, OUTPUT);  
    pinMode(IR_SYNC_PIN_AR, OUTPUT);  
    pinMode(IR_SYNC_PIN_BL, OUTPUT);  
    pinMode(IR_SYNC_PIN_BR, OUTPUT);  
    pinMode(IR_SYNC_PIN_CL, OUTPUT);  
    pinMode(IR_SYNC_PIN_CR, OUTPUT);  
    pinMode(IR_SYNC_PIN_DL, OUTPUT);  
    pinMode(IR_SYNC_PIN_DR, OUTPUT);  

    // Teensy pins responsible for setting stir frequency, output 50% duty PWM
    pinMode(STIR_STEP_PIN_AL, OUTPUT);  
    pinMode(STIR_STEP_PIN_AR, OUTPUT);  
    pinMode(STIR_STEP_PIN_BL, OUTPUT);  
    pinMode(STIR_STEP_PIN_BR, OUTPUT);  
    pinMode(STIR_STEP_PIN_CL, OUTPUT);  
    pinMode(STIR_STEP_PIN_CR, OUTPUT);  
    pinMode(STIR_STEP_PIN_DL, OUTPUT);  
    pinMode(STIR_STEP_PIN_DR, OUTPUT); 

    HB_trigger.priority(0); // Set the priority of the heatbeat timer interrupt to 0 (most important, this feeds our watchdog signal)
    HB_trigger.begin(HeartbeatInterruptHandler, heartbeat_update_freq_ms);  // Set the heartbeat to toggle every 100ms

    fluid_sensor_timer.priority(128);  // Set the priority of the fluid sensor timer interrupt to 128 (least important)

    enableSystem(false);  // Disable the system. This is the default state at startup.
    setupFlexIOInput(); // this shouldnt change on disable/enable so can just be called on initial startup
}

void output_od_stats() {
    // will output max and min OD related stats within the time window between calls of this function
    teensy_device.sendInfo("max intvl:%d, min intvl:%d", max_interrupt_interval, min_interrupt_interval);
    max_interrupt_interval = 0;
    min_interrupt_interval = 99999;

    teensy_device.sendInfo("max: AL %d, AR %d, BL %d, BR %d, CL %d, CR %d, DL %d, DR %d",machine.reactors[REACTOR::AL].max_od_interval,  machine.reactors[REACTOR::AR].max_od_interval, machine.reactors[REACTOR::BL].max_od_interval, machine.reactors[REACTOR::BR].max_od_interval, machine.reactors[REACTOR::CL].max_od_interval, machine.reactors[REACTOR::CR].max_od_interval, machine.reactors[REACTOR::DL].max_od_interval, machine.reactors[REACTOR::DR].max_od_interval);
    teensy_device.sendInfo("min: AL %d, AR %d, BL %d, BR %d, CL %d, CR %d, DL %d, DR %d",machine.reactors[REACTOR::AL].min_od_interval,  machine.reactors[REACTOR::AR].min_od_interval, machine.reactors[REACTOR::BL].min_od_interval, machine.reactors[REACTOR::BR].min_od_interval, machine.reactors[REACTOR::CL].min_od_interval, machine.reactors[REACTOR::CR].min_od_interval, machine.reactors[REACTOR::DL].min_od_interval, machine.reactors[REACTOR::DR].min_od_interval);
    
    if ( (abs(machine.reactors[REACTOR::AL].max_scatter_voltage - machine.reactors[REACTOR::AL].min_scatter_voltage) > 0.01) or (abs(machine.reactors[REACTOR::AL].max_ad_voltage - machine.reactors[REACTOR::AL].min_ad_voltage) > 0.01)){
        teensy_device.sendInfo("AL:\tS: %.2g:%.2g\tA: %.2g:%.2g\terr: %d", machine.reactors[REACTOR::AL].max_scatter_voltage, machine.reactors[REACTOR::AL].min_scatter_voltage, machine.reactors[REACTOR::AL].max_ad_voltage, machine.reactors[REACTOR::AL].min_ad_voltage, machine.reactors[REACTOR::AL].error_occured);
    }
    if ( (abs(machine.reactors[REACTOR::AR].max_scatter_voltage - machine.reactors[REACTOR::AR].min_scatter_voltage) > 0.01) or (abs(machine.reactors[REACTOR::AR].max_ad_voltage - machine.reactors[REACTOR::AR].min_ad_voltage) > 0.01)){
        teensy_device.sendInfo("AR:\tS: %.2g:%.2g\tA: %.2g:%.2g\terr: %d", machine.reactors[REACTOR::AR].max_scatter_voltage, machine.reactors[REACTOR::AR].min_scatter_voltage, machine.reactors[REACTOR::AR].max_ad_voltage, machine.reactors[REACTOR::AR].min_ad_voltage, machine.reactors[REACTOR::AR].error_occured);
    }
    if ( (abs(machine.reactors[REACTOR::BL].max_scatter_voltage - machine.reactors[REACTOR::BL].min_scatter_voltage) > 0.01) or (abs(machine.reactors[REACTOR::BL].max_ad_voltage - machine.reactors[REACTOR::BL].min_ad_voltage) > 0.01)){
        teensy_device.sendInfo("BL:\tS: %.2g:%.2g\tA: %.2g:%.2g\terr: %d", machine.reactors[REACTOR::BL].max_scatter_voltage, machine.reactors[REACTOR::BL].min_scatter_voltage, machine.reactors[REACTOR::BL].max_ad_voltage, machine.reactors[REACTOR::BL].min_ad_voltage, machine.reactors[REACTOR::BL].error_occured);
    }
    if ( (abs(machine.reactors[REACTOR::BR].max_scatter_voltage - machine.reactors[REACTOR::BR].min_scatter_voltage) > 0.01) or (abs(machine.reactors[REACTOR::BR].max_ad_voltage - machine.reactors[REACTOR::BR].min_ad_voltage) > 0.01)){
        teensy_device.sendInfo("BR:\tS: %.2g:%.2g\tA: %.2g:%.2g\terr: %d", machine.reactors[REACTOR::BR].max_scatter_voltage, machine.reactors[REACTOR::BR].min_scatter_voltage, machine.reactors[REACTOR::BR].max_ad_voltage, machine.reactors[REACTOR::BR].min_ad_voltage, machine.reactors[REACTOR::BR].error_occured);
    }
    if ( (abs(machine.reactors[REACTOR::CL].max_scatter_voltage - machine.reactors[REACTOR::CL].min_scatter_voltage) > 0.01) or (abs(machine.reactors[REACTOR::CL].max_ad_voltage - machine.reactors[REACTOR::CL].min_ad_voltage) > 0.01)){
        teensy_device.sendInfo("CL:\tS: %.2g:%.2g\tA: %.2g:%.2g\terr: %d", machine.reactors[REACTOR::CL].max_scatter_voltage, machine.reactors[REACTOR::CL].min_scatter_voltage, machine.reactors[REACTOR::CL].max_ad_voltage, machine.reactors[REACTOR::CL].min_ad_voltage, machine.reactors[REACTOR::CL].error_occured);
    }
    if ( (abs(machine.reactors[REACTOR::CR].max_scatter_voltage - machine.reactors[REACTOR::CR].min_scatter_voltage) > 0.01) or (abs(machine.reactors[REACTOR::CR].max_ad_voltage - machine.reactors[REACTOR::CR].min_ad_voltage) > 0.01)){
        teensy_device.sendInfo("CR:\tS: %.2g:%.2g\tA: %.2g:%.2g\terr: %d", machine.reactors[REACTOR::CR].max_scatter_voltage, machine.reactors[REACTOR::CR].min_scatter_voltage, machine.reactors[REACTOR::CR].max_ad_voltage, machine.reactors[REACTOR::CR].min_ad_voltage, machine.reactors[REACTOR::CR].error_occured);
    }
    if ( (abs(machine.reactors[REACTOR::DL].max_scatter_voltage - machine.reactors[REACTOR::DL].min_scatter_voltage) > 0.01) or (abs(machine.reactors[REACTOR::DL].max_ad_voltage - machine.reactors[REACTOR::DL].min_ad_voltage) > 0.01)){
        teensy_device.sendInfo("DL:\tS: %.2g:%.2g\tA: %.2g:%.2g\terr: %d", machine.reactors[REACTOR::DL].max_scatter_voltage, machine.reactors[REACTOR::DL].min_scatter_voltage, machine.reactors[REACTOR::DL].max_ad_voltage, machine.reactors[REACTOR::DL].min_ad_voltage, machine.reactors[REACTOR::DL].error_occured);
    }
    if ( (abs(machine.reactors[REACTOR::DR].max_scatter_voltage - machine.reactors[REACTOR::DR].min_scatter_voltage) > 0.01) or (abs(machine.reactors[REACTOR::DR].max_ad_voltage - machine.reactors[REACTOR::DR].min_ad_voltage) > 0.01)){
        teensy_device.sendInfo("DR:\tS: %.2g:%.2g\tA: %.2g:%.2g\terr: %d\n", machine.reactors[REACTOR::DR].max_scatter_voltage, machine.reactors[REACTOR::DR].min_scatter_voltage, machine.reactors[REACTOR::DR].max_ad_voltage, machine.reactors[REACTOR::DR].min_ad_voltage, machine.reactors[REACTOR::DR].error_occured);
    }
    
    machine.reactors[REACTOR::AL].max_od_interval = 0;
    machine.reactors[REACTOR::AR].max_od_interval = 0;
    machine.reactors[REACTOR::BL].max_od_interval = 0;
    machine.reactors[REACTOR::BR].max_od_interval = 0;
    machine.reactors[REACTOR::CL].max_od_interval = 0;
    machine.reactors[REACTOR::CR].max_od_interval = 0;
    machine.reactors[REACTOR::DL].max_od_interval = 0;
    machine.reactors[REACTOR::DR].max_od_interval = 0;
    machine.reactors[REACTOR::AL].min_od_interval = 99999;
    machine.reactors[REACTOR::AR].min_od_interval = 99999;
    machine.reactors[REACTOR::BL].min_od_interval = 99999;
    machine.reactors[REACTOR::BR].min_od_interval = 99999;
    machine.reactors[REACTOR::CL].min_od_interval = 99999;
    machine.reactors[REACTOR::CR].min_od_interval = 99999;
    machine.reactors[REACTOR::DL].min_od_interval = 99999;
    machine.reactors[REACTOR::DR].min_od_interval = 99999;

    machine.reactors[REACTOR::AL].max_ad_voltage = 0;
    machine.reactors[REACTOR::AL].min_ad_voltage = 9999;
    machine.reactors[REACTOR::AL].max_scatter_voltage = 0;
    machine.reactors[REACTOR::AL].min_scatter_voltage = 9999;
    machine.reactors[REACTOR::AL].error_occured = false;
    machine.reactors[REACTOR::AL].repeated_data_occured = false;
    machine.reactors[REACTOR::AR].max_ad_voltage = 0;
    machine.reactors[REACTOR::AR].min_ad_voltage = 9999;
    machine.reactors[REACTOR::AR].max_scatter_voltage = 0;
    machine.reactors[REACTOR::AR].min_scatter_voltage = 9999;
    machine.reactors[REACTOR::AR].error_occured = false;
    machine.reactors[REACTOR::AR].repeated_data_occured = false;
    machine.reactors[REACTOR::BL].max_ad_voltage = 0;
    machine.reactors[REACTOR::BL].min_ad_voltage = 9999;
    machine.reactors[REACTOR::BL].max_scatter_voltage = 0;
    machine.reactors[REACTOR::BL].min_scatter_voltage = 9999;
    machine.reactors[REACTOR::BL].error_occured = false;
    machine.reactors[REACTOR::BL].repeated_data_occured = false;
    machine.reactors[REACTOR::BR].max_ad_voltage = 0;
    machine.reactors[REACTOR::BR].min_ad_voltage = 9999;
    machine.reactors[REACTOR::BR].max_scatter_voltage = 0;
    machine.reactors[REACTOR::BR].min_scatter_voltage = 9999;
    machine.reactors[REACTOR::BR].error_occured = false;
    machine.reactors[REACTOR::BR].repeated_data_occured = false;
    machine.reactors[REACTOR::CL].max_ad_voltage = 0;
    machine.reactors[REACTOR::CL].min_ad_voltage = 9999;
    machine.reactors[REACTOR::CL].max_scatter_voltage = 0;
    machine.reactors[REACTOR::CL].min_scatter_voltage = 9999;
    machine.reactors[REACTOR::CL].error_occured = false;
    machine.reactors[REACTOR::CL].repeated_data_occured = false;
    machine.reactors[REACTOR::CR].max_ad_voltage = 0;
    machine.reactors[REACTOR::CR].min_ad_voltage = 9999;
    machine.reactors[REACTOR::CR].max_scatter_voltage = 0;
    machine.reactors[REACTOR::CR].min_scatter_voltage = 9999;
    machine.reactors[REACTOR::CR].error_occured = false;
    machine.reactors[REACTOR::CR].repeated_data_occured = false;
    machine.reactors[REACTOR::DL].max_ad_voltage = 0;
    machine.reactors[REACTOR::DL].min_ad_voltage = 9999;
    machine.reactors[REACTOR::DL].max_scatter_voltage = 0;
    machine.reactors[REACTOR::DL].min_scatter_voltage = 9999;
    machine.reactors[REACTOR::DL].error_occured = false;
    machine.reactors[REACTOR::DL].repeated_data_occured = false;
    machine.reactors[REACTOR::DR].max_ad_voltage = 0;
    machine.reactors[REACTOR::DR].min_ad_voltage = 9999;
    machine.reactors[REACTOR::DR].max_scatter_voltage = 0;
    machine.reactors[REACTOR::DR].min_scatter_voltage = 9999;
    machine.reactors[REACTOR::DR].error_occured = false;
    machine.reactors[REACTOR::DR].repeated_data_occured = false;
    max_od_interrupt_time = 0;
}

void loop() {

    received_flag = teensy_device.receivePacket(&received_packet);
    
    if (received_flag > 0) {
        process_packet(&received_packet);
        //RawHID.send(testbuffer, 0);
        // received packet is modified by process_packet so we can send it back
        teensy_device.sendPacket(&received_packet);
    } 


    if (systemEnable) {
        loop_timer = 0;

        if (heartbeat_started && heartbeat_timer > max_heartbeat_interval_ms){
            // teensy_device.sendError("Heartbeat timeout");
            // enableSystem(false); // If the heartbeat timer has exceeded the maximum interval, disable the system to protect it from potential harm. This likely means we have lost communication with the PC.
        }

        if(machine.SEND_FAST_OD_READINGS && OD_output_timer.check() == 1){
            ODOutputTimerFunc();
        }

        if(machine.SEND_FAST_WEIGHT_READINGS && weight_output_timer.check() == 1){
            WeightOutputTimerFunc();
        }

        if(reactor_heater_control_timer.check() == 1){
            ReactorHeaterTimerFunc();
        }

        if(mixing_heater_control_timer.check() == 1){
            MixingHeaterTimerFunc();
        }

        if(RTD_ADC_timer.check() == 1){
            machine.runRTDADC();  // Run the RTD ADC state machine with no arguments to indicate it should be doing its RTD readout method. 
        }

        if (machine.SEND_MEMORY_REPORT && health_report_timer.check() == 1) {
            // Sends a health report to the PCf
            HealthReportHandler();
        }

        if (machine.SEND_CURRENT_REPORT && current_report_timer.check() == 1) {
            teensy_device.sendDebug("Doing current");
            CurrentReportHandler();    
        }

        if (machine.SEND_REACTOR_TEMPERATURE_REPORT && reactor_temperature_report_timer.check() == 1) {
            teensy_device.sendDebug("Reporting temps");
            ReactorTempReportHandler();    
        }

        if (machine.SEND_OTHER_TEMPERATURE_REPORT && other_temperature_report_timer.check() == 1) {
            teensy_device.sendDebug("Reporting other temps");
            OtherTempReportHandler();    
        }

        if (loop_timer > max_loop_time_us) {max_loop_time_us = loop_timer;}

    } else {
        delayMicroseconds(100); // If the system is disabled, we can slow down the loop a bit more to save power
        heartbeat_timer = 0; // reset the heartbeat timer while disabled so that it doesnt immediately disable again when we enable
    }
}
