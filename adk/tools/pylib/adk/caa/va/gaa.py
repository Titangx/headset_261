############################################################################
# CONFIDENTIAL
#
# Copyright (c) 2021 Qualcomm Technologies International, Ltd.
#
############################################################################

from csr.dev.fw.firmware_component import FirmwareComponent
from csr.dev.env.env_helpers import InvalidDereference
from csr.dev.model import interface


class Gaa(FirmwareComponent):
    """
    Gaa Voice Assistant analysis class
    """

    def __init__(self, env, core, parent=None):
        FirmwareComponent.__init__(self, env, core, parent=parent)
        try:
            self._gaa = env.econst.voice_ui_provider_gaa
        except AttributeError:
            raise self.NotDetected()

    @property
    def active(self):
        try:
            active_va = self.env.vars['active_va'].deref.voice_assistant.deref.va_provider.value
            return active_va == self._gaa
        except InvalidDereference:
            return False

    def _convert_binary_to_fp64(self, value):
        if(value == '0b0'):
            fp64_value = 0.0
        else:
            # Bit 64
            sign = int(value[2:3], 2)
            # Bit 63-53
            exponent = int(value[3:14], 2)
            offset = 1023
            
            if(exponent):    
                exponent = 2 ** (exponent-offset)
            else:
                exponent = 2 ** (offset-1)

            # Bit 52-0
            fraction = int(value[14:], 2)

            if sign == 0:
                exponent = exponent * -1
                
            fp64_value = float(str(exponent) + '.' + str(fraction))
        return fp64_value

    def _create_device_actions_group(self):
        try:
            command_table = self.env.vars['command_table']
        except KeyError:
            return None

        device_actions_grp = interface.Group("Gaa device actions")

        device_actions_storage_ptr_tbl = interface.Table(["all", "battery_details", "noise_cancellation", "ambient_mode"])
        device_actions_storage_ptr_tbl.add_row([self.env.vars['update_all_storage'], 
                                                self.env.vars['battery_level_device_action'].update.storage, 
                                                self.env.vars['noise_cancellation_device_action'].update.storage, 
                                                self.env.vars['ambient_mode_device_action'].update.storage])
        device_actions_grp.append(device_actions_storage_ptr_tbl)

        device_actions_command_tbl = interface.Table(["Command", "Value Kind", "Value"])
        with command_table.footprint_prefetched():
            for i in range(10):
                value_kind = command_table[i].value_kind
                if value_kind.value == self.env.econst.EXECUTION_VALUE__KIND_NULL_VALUE:
                    value = command_table[i].u.null_value.value
                elif value_kind.value == self.env.econst.EXECUTION_VALUE__KIND_NUMBER_VALUE:
                    value = self._convert_binary_to_fp64(bin(command_table[i].u.number_value.value))
                elif value_kind.value == self.env.econst.EXECUTION_VALUE__KIND_STRING_VALUE:
                    value = command_table[i].u.string_value.value
                elif value_kind.value == self.env.econst.EXECUTION_VALUE__KIND_BOOL_VALUE:
                    value = "TRUE" if command_table[i].u.bool_value.value else "FALSE"
                else:
                    value = 'No value found!'

                device_actions_command_tbl.add_row([command_table[i].command,
                                                    value_kind,
                                                    value])

        device_actions_grp.append(device_actions_command_tbl)

        device_actions_update_tbl = interface.Table(["Update", "Value"])
        for i in range(10):
            device_actions_update_tbl.add_row([self.env.vars['update_table'][i].update,
                                               self.env.vars['update_table'][i].value.value])
        device_actions_grp.append(device_actions_update_tbl)

        return device_actions_grp

    def _generate_report_body_elements(self):

        content = []

        grp = interface.Group("Gaa status")
        tbl = interface.Table(["Active"])
        tbl.add_row([
            "Y" if self.active else "N"
        ])
        grp.append(tbl)
        content.append(grp)

        device_actions_grp = self._create_device_actions_group()
        content.append(device_actions_grp)

        return content
