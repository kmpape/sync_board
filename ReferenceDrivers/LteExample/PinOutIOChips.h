#ifndef _PinOutIOChips_H_
#define _PinOutIOChips_H_

typedef enum {
    cfg_OUTPUT = 0,
    cfg_INPUT,
    cfg_FAULT_STIR,
    cfg_FAULT_HEATER,
    cfg_FAULT_VALVE,
    cfg_FAULT_EXT,
    cfg_FAULT_FANS
} pinout_io_config_e;



/*
get the interrupt

Chip 0x23 State:4 13 4C 30 C1
Chip 0x24 State:4 13 4C 30 C1
Chip 0x25 State:21 84 10 42 8
Chip 0x26 State:90 FF 1 41 8
Chip 0x20 State:DB B6 6D 0 C0
*/


const pinout_io_config_e Senseboard_IO_1_PINOUT[5][8] = {
    // {bank 0 pos 0, bank 0 pos 1, bank 0 pos 2, bank 0 pos 3, bank 0 pos 4, bank 0 pos 5, bank 0 pos 6, bank 0 pos 7}    
    {cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT}, 
    // {bank 1 pos 0, bank 1 pos 1, bank 1 pos 2, bank 1 pos 3, bank 1 pos 4, bank 1 pos 5, bank 1 pos 6, bank 1 pos 7}
    {cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT},
    {cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT},
    {cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT},
    {cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT}
};

const pinout_io_config_e Topboard_IO_1_PINOUT[5][8] = {
    {cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT},
    {cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT},
    {cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT},
    {cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT},
    {cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT, cfg_OUTPUT}
};

const pinout_io_config_e Topboard_IO_2_PINOUT[5][8] = {
    {cfg_OUTPUT,        cfg_OUTPUT,         cfg_OUTPUT,         cfg_OUTPUT,         cfg_OUTPUT,         cfg_OUTPUT,         cfg_OUTPUT,         cfg_OUTPUT},
    {cfg_OUTPUT,        cfg_OUTPUT,         cfg_OUTPUT,         cfg_OUTPUT,         cfg_OUTPUT,         cfg_OUTPUT,         cfg_OUTPUT,         cfg_OUTPUT},
    {cfg_OUTPUT,        cfg_OUTPUT,         cfg_OUTPUT,         cfg_OUTPUT,         cfg_OUTPUT,         cfg_OUTPUT,         cfg_OUTPUT,         cfg_OUTPUT},
    {cfg_OUTPUT,        cfg_OUTPUT,         cfg_OUTPUT,         cfg_OUTPUT,         cfg_OUTPUT,         cfg_OUTPUT,         cfg_OUTPUT,         cfg_INPUT},
    {cfg_FAULT_HEATER,  cfg_FAULT_HEATER,   cfg_FAULT_HEATER,   cfg_FAULT_HEATER,   cfg_FAULT_HEATER,   cfg_FAULT_HEATER,   cfg_FAULT_HEATER,   cfg_FAULT_HEATER}
};

const pinout_io_config_e Heatboard_IO_1_PINOUT[5][8] = {
    {cfg_OUTPUT,      cfg_OUTPUT,      cfg_FAULT_VALVE, cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT     },
    {cfg_OUTPUT,      cfg_FAULT_STIR,  cfg_OUTPUT,      cfg_OUTPUT,      cfg_FAULT_VALVE, cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT     },
    {cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_FAULT_STIR,  cfg_OUTPUT,      cfg_OUTPUT,      cfg_FAULT_VALVE, cfg_OUTPUT     },
    {cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_FAULT_STIR,  cfg_OUTPUT,      cfg_OUTPUT     },
    {cfg_FAULT_VALVE, cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_FAULT_STIR }
};

const pinout_io_config_e Heatboard_IO_2_PINOUT[5][8] = {
    {cfg_OUTPUT,      cfg_OUTPUT,      cfg_FAULT_VALVE, cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT     },
    {cfg_OUTPUT,      cfg_FAULT_STIR,  cfg_OUTPUT,      cfg_OUTPUT,      cfg_FAULT_VALVE, cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT     },
    {cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_FAULT_STIR,  cfg_OUTPUT,      cfg_OUTPUT,      cfg_FAULT_VALVE, cfg_OUTPUT     },
    {cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_FAULT_STIR,  cfg_OUTPUT,      cfg_OUTPUT     },
    {cfg_FAULT_VALVE, cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_FAULT_STIR }
};

const pinout_io_config_e Heatboard_IO_3_PINOUT[5][8] = {
    {cfg_FAULT_VALVE, cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_FAULT_VALVE, cfg_OUTPUT,      cfg_OUTPUT     },
    {cfg_OUTPUT,      cfg_OUTPUT,      cfg_FAULT_VALVE, cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_FAULT_VALVE},
    {cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_FAULT_VALVE, cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT     },
    {cfg_OUTPUT,      cfg_FAULT_VALVE, cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_FAULT_VALVE, cfg_OUTPUT     },
    {cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_FAULT_VALVE, cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT     }
};

const pinout_io_config_e Heatboard_IO_4_PINOUT[5][8] = {
    {cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_FAULT_EXT,   cfg_OUTPUT,      cfg_OUTPUT,      cfg_FAULT_FANS },
    {cfg_FAULT_EXT,   cfg_FAULT_EXT,   cfg_FAULT_EXT,   cfg_FAULT_EXT,   cfg_FAULT_EXT,   cfg_FAULT_EXT,   cfg_FAULT_EXT,   cfg_FAULT_EXT  },
    {cfg_FAULT_EXT,   cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT     },
    {cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_FAULT_VALVE, cfg_OUTPUT     },
    {cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_FAULT_VALVE, cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT,      cfg_OUTPUT     }
};

#endif