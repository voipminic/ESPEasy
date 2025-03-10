#include "_Plugin_Helper.h"
#ifdef USES_P105

// #######################################################################################################
// ######################## Plugin 105 AHT I2C Temperature and Humidity Sensor  ##########################
// #######################################################################################################
// data sheet AHT10: https://wiki.liutyi.info/display/ARDUINO/AHT10
// device AHT10: http://www.aosong.com/en/products-40.html
// device and manual AHT20: http://www.aosong.com/en/products-32.html
// device and manual AHT21: http://www.aosong.com/en/products-60.html

/* AHT10/15/20 - Temperature and Humidity
 *
 * (Comment copied from _P248_TempHumidity_AHT1x.ino)
 *
 * AHT1x I2C Address: 0x38, 0x39
 * the driver supports two I2c adresses but only one Sensor allowed.
 *
 * ATTENTION: The AHT10/15 Sensor is incompatible with other I2C devices on I2C bus.
 *
 * The Datasheet write:
 * "Only a single AHT10 can be connected to the I2C bus and no other I2C
 *  devices can be connected".
 *
 * after lot of search and tests, now is confirmed that works only reliable with one sensor
 * on I2C Bus
 */

/** History:
 * 2024-12-03 tonhuisman: Add alternative initialization for AHT10 (clone), see https://github.com/letscontrolit/ESPEasy/issues/5172
 *                        Small code optimization.
 * 2024-04-28 tonhuisman: Update plugin name and documentation as DHT20 and AM2301B actually contain an AHT20!
 *                        DHT20: https://www.adafruit.com/product/5183 (Description)
 *                        AM2301B: https://www.adafruit.com/product/5181 (Description)
 * 2021-08-01 tonhuisman: Plugin migrated from ESPEsyPluginPlayground repository
 *                        Minor adjustments, changed castings to use static_cast<type>(var) method,
 *                        Added check for other I2C devices configured on ESPEasy tasks to give a warning
 *                        about I2C incompatibility, for AHT10/AHT15 device only
 * 2021-03 sakinit:       Initial plugin, added on ESPEasyPluginPlayground
 */

# include "src/PluginStructs/P105_data_struct.h"

# define PLUGIN_105
# define PLUGIN_ID_105         105
# define PLUGIN_NAME_105       "Environment - AHT1x/AHT2x/DHT20/AM2301B"
# define PLUGIN_VALUENAME1_105 "Temperature"
# define PLUGIN_VALUENAME2_105 "Humidity"


boolean Plugin_105(uint8_t function, struct EventStruct *event, String& string)
{
  boolean success = false;

  switch (function)
  {
    case PLUGIN_DEVICE_ADD:
    {
      Device[++deviceCount].Number       = PLUGIN_ID_105;
      Device[deviceCount].Type           = DEVICE_TYPE_I2C;
      Device[deviceCount].VType          = Sensor_VType::SENSOR_TYPE_TEMP_HUM;
      Device[deviceCount].Ports          = 0;
      Device[deviceCount].FormulaOption  = true;
      Device[deviceCount].ValueCount     = 2;
      Device[deviceCount].SendDataOption = true;
      Device[deviceCount].TimerOption    = true;
      Device[deviceCount].PluginStats    = true;
      break;
    }

    case PLUGIN_GET_DEVICENAME:
    {
      string = F(PLUGIN_NAME_105);
      break;
    }

    case PLUGIN_GET_DEVICEVALUENAMES:
    {
      strcpy_P(ExtraTaskSettings.TaskDeviceValueNames[0], PSTR(PLUGIN_VALUENAME1_105));
      strcpy_P(ExtraTaskSettings.TaskDeviceValueNames[1], PSTR(PLUGIN_VALUENAME2_105));
      break;
    }

    case PLUGIN_I2C_HAS_ADDRESS:
    case PLUGIN_WEBFORM_SHOW_I2C_PARAMS:
    {
      const uint8_t i2cAddressValues[2] = { 0x38, 0x39 };

      if (function == PLUGIN_WEBFORM_SHOW_I2C_PARAMS) {
        addFormSelectorI2C(F("i2c_addr"), 2, i2cAddressValues, PCONFIG(0));
        addFormNote(F("SDO Low=0x38, High=0x39. NB: Only available on AHT1x sensors."));
      } else {
        success = intArrayContains(2, i2cAddressValues, event->Par1);
      }

      break;
    }

    # if FEATURE_I2C_GET_ADDRESS
    case PLUGIN_I2C_GET_ADDRESS:
    {
      event->Par1 = PCONFIG(0);
      success     = true;
      break;
    }
    # endif // if FEATURE_I2C_GET_ADDRESS

    case PLUGIN_SET_DEFAULTS:
    {
      PCONFIG(1) = static_cast<int>(AHTx_device_type::AHT20_DEVICE);
      break;
    }

    case PLUGIN_WEBFORM_LOAD:
    {
      if (static_cast<AHTx_device_type>(PCONFIG(1)) ==  AHTx_device_type::AHT10_DEVICE) {
        bool hasOtherI2CDevices = false;

        for (taskIndex_t x = 0; validTaskIndex(x) && !hasOtherI2CDevices; ++x) {
          const deviceIndex_t DeviceIndex = getDeviceIndex_from_TaskIndex(x);

          if (validDeviceIndex(DeviceIndex)
              && (Settings.TaskDeviceDataFeed[x] == 0)
              && ((Device[DeviceIndex].Type == DEVICE_TYPE_I2C)
                # ifdef PLUGIN_USES_SERIAL // Has I2C Serial option
                  || (Device[DeviceIndex].Type == DEVICE_TYPE_SERIAL)
                  || (Device[DeviceIndex].Type == DEVICE_TYPE_SERIAL_PLUS1)
                # endif // ifdef PLUGIN_USES_SERIAL
                  )
              ) {
            hasOtherI2CDevices = true;
          }
        }

        if (hasOtherI2CDevices) {
          addRowLabel(EMPTY_STRING, EMPTY_STRING);
          addHtmlDiv(F("note warning"),
                     F("Attention: Sensor model AHT1x may cause I2C issues when combined with other I2C devices on the same bus!"));
        }
      }
      {
        const __FlashStringHelper *options[] = { F("AHT1x"), F("AHT20"), F("AHT21") };
        const int indices[]                  = { static_cast<int>(AHTx_device_type::AHT10_DEVICE),
                                                 static_cast<int>(AHTx_device_type::AHT20_DEVICE),
                                                 static_cast<int>(AHTx_device_type::AHT21_DEVICE) };
        addFormSelector(F("Sensor model"), F("ahttype"), 3, options, indices, PCONFIG(1), true);
        addFormNote(F("Changing Sensor model will reload the page."));

        if (static_cast<int>(AHTx_device_type::AHT10_DEVICE) == PCONFIG(1)) {
          addFormCheckBox(F("AHT10 Alternative initialization"), F("altinit"), PCONFIG(2));
        }
      }

      success = true;
      break;
    }

    case PLUGIN_WEBFORM_SAVE:
    {
      PCONFIG(1) = getFormItemInt(F("ahttype"));

      if (static_cast<AHTx_device_type>(PCONFIG(1)) != AHTx_device_type::AHT10_DEVICE) {
        PCONFIG(0) = 0x38; // AHT20/AHT21 only support a single I2C address.
      } else {
        PCONFIG(0) = getFormItemInt(F("i2c_addr"));
        PCONFIG(2) = isFormItemChecked(F("altinit")) ? 1 : 0;
      }
      success = true;
      break;
    }

    case PLUGIN_INIT:
    {
      success = initPluginTaskData(
        event->TaskIndex,
        new (std::nothrow) P105_data_struct(PCONFIG(0), static_cast<AHTx_device_type>(PCONFIG(1)), 1 == PCONFIG(2)));
      break;
    }

    case PLUGIN_ONCE_A_SECOND:
    {
      P105_data_struct *P105_data =
        static_cast<P105_data_struct *>(getPluginTaskData(event->TaskIndex));

      if (nullptr != P105_data) {
        if (P105_data->updateMeasurements(event->TaskIndex)) {
          // Update was succesfull, schedule a read.
          Scheduler.schedule_task_device_timer(event->TaskIndex, millis() + 10);
        }
      }
      break;
    }

    case PLUGIN_READ:
    {
      P105_data_struct *P105_data =
        static_cast<P105_data_struct *>(getPluginTaskData(event->TaskIndex));

      if (nullptr != P105_data) {
        if (P105_data->state != AHTx_state::AHTx_New_values) {
          break;
        }
        P105_data->state = AHTx_state::AHTx_Values_read;

        UserVar.setFloat(event->TaskIndex, 0, P105_data->getTemperature());
        UserVar.setFloat(event->TaskIndex, 1, P105_data->getHumidity());

        if (loglevelActiveFor(LOG_LEVEL_INFO)) {
          addLogMove(LOG_LEVEL_INFO, strformat(F("%s : Addr: 0x%02x"), P105_data->getDeviceName().c_str(), PCONFIG(0)));
          addLogMove(LOG_LEVEL_INFO,
                     strformat(F("%s : Temperature: %s : Humidity: %s"),
                               P105_data->getDeviceName().c_str(),
                               formatUserVarNoCheck(event, 0).c_str(),
                               formatUserVarNoCheck(event, 1).c_str()));
        }
        success = true;
      }
      break;
    }
  }
  return success;
}

#endif // USES_P105
