import logging
from typing import Dict, List, Optional, Tuple, Union

LOGGER = logging.getLogger(__name__)


class Command:
    SETUP_MAGNET = "setupMagnetBoard"
    CALIBRATE_MAGNET = "callibrateMagnet"
    ATTACH_MAGNET = "attachMagnet"
    ATTACH_LED = "attachLED"
    CALIBRATE_LED = "calibrateLED"
    DEBUG = "debug"
    FACTORY_RESET = "factoryReset"
    GET_LED_SETUP = "getLEDSetup"
    GET_SIGNAL_ADC = "getSignalADC"
    MEASURE_LED = "measureLED"
    MEASURE_PHOTODIODE = "measurePhotodiode"
    READ_ADC = "readADC"
    READ_DI = "readDI"
    SETUP_GPIO = "setupGPIO"
    SETUP_IMG_SEQ = "setupImageSequence"
    SETUP_LED = "setupLED"
    SETUP_SIGNAL_MODE = "setupSignalMode"
    SETUP_SIGNAL_DAC = "setupSignalDAC"
    SETUP_SIGNAL_ADC = "setupSignalADC"
    SET_SYNC_MODE = "setSyncMode"
    SET_DAC = "setDAC"
    SET_SWITCH = "setSwitch"
    START_IMG_SEQ = "startImageSequence"
    START_SIGNAL = "startSignal"
    STOP_SIGNAL = "stopSignal"
    SWITCH_LED = "switchLED"
    SWITCH_LED_TIMED = "switchLEDTimed"
    SYSTEM_DISABLE = "systemDisable"
    SYSTEM_ENABLE = "systemEnable"
    WRITE_GPIO = "writeGPIO"
    WRITE_DO = "writeDO"
    SCAN_I2C = "scanI2C"

    _NUM_SIG_FIG_FLOAT = 7
    _START_CHAR = "$"
    _END_CHAR = "#"
    _TRANS_STOP_CHAR = "%"
    _DELIM_CHAR = "/"

    @classmethod
    def format(
        cls,
        command: str,
        *args,
    ) -> str:
        args_str = cls._DELIM_CHAR.join(cls.format_float(arg) if isinstance(arg, float) else str(arg).lower()
                                        for arg in args)
        args_str = "/" + args_str if args_str else args_str
        command_formatted = f"{cls._START_CHAR}{command}{args_str}{cls._END_CHAR}{cls._TRANS_STOP_CHAR}"
        LOGGER.debug(f"Formatted command: {command_formatted}")
        return command_formatted

    @classmethod
    def format_float(
            cls,
            val: float
    ) -> str:
        # TODO warning about precision
        return f"{val:.{cls._NUM_SIG_FIG_FLOAT}f}"

    @staticmethod
    def format_image_seq(
            lst: Union[List, None, int],
            max_length: int
    ) -> List[int]:
        if lst is None:
            lst = [0] * max_length
        else:
            if isinstance(lst, int):
                lst = [lst]
            lst.extend([0] * (max_length - len(lst)))
        return lst

    @classmethod
    def parse_reply(
            cls,
            reply: str,
    ) -> Tuple[str, List[Union[str, float]]]:

        reply = reply.strip()
        if not reply.startswith(cls._START_CHAR) or not reply.endswith(cls._TRANS_STOP_CHAR):
            raise ValueError("Invalid reply: " + reply)

        parts = reply.lstrip(cls._START_CHAR).rstrip(cls._TRANS_STOP_CHAR).rstrip(cls._END_CHAR).split(cls._DELIM_CHAR)

        return parts[0], [cls.convert_float(val) for val in parts[1:]]

    @staticmethod
    def convert_float(val: str) -> Union[str, float]:
        try:
            return float(val)
        except ValueError:
            return val


