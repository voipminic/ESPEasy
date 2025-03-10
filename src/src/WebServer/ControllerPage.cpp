#include "../WebServer/ControllerPage.h"


#ifdef WEBSERVER_CONTROLLERS

# include "../WebServer/ESPEasy_WebServer.h"
# include "../WebServer/HTML_wrappers.h"
# include "../WebServer/Markup.h"
# include "../WebServer/Markup_Buttons.h"
# include "../WebServer/Markup_Forms.h"

# include "../DataStructs/ESPEasy_EventStruct.h"

# include "../ESPEasyCore/Controller.h"

# include "../Globals/CPlugins.h"
# include "../Globals/Settings.h"

# if FEATURE_MQTT
#  include "../Globals/MQTT.h"
# endif // if FEATURE_MQTT

# include "../Helpers/_CPlugin_init.h"
# include "../Helpers/_CPlugin_Helper_webform.h"
# include "../Helpers/_Plugin_SensorTypeHelper.h"
# include "../Helpers/ESPEasy_Storage.h"
# include "../Helpers/StringConverter.h"

# include "../Helpers/_CPlugin_Helper_webform.h"
# include "../Helpers/_Plugin_SensorTypeHelper.h"
# include "../Helpers/ESPEasy_Storage.h"
# include "../Helpers/StringConverter.h"

// ********************************************************************************
// Web Interface controller page
// ********************************************************************************
void handle_controllers() {
  # ifndef BUILD_NO_RAM_TRACKER
  checkRAM(F("handle_controllers"));
  # endif // ifndef BUILD_NO_RAM_TRACKER

  if (!isLoggedIn()) { return; }
  navMenuIndex = MENU_INDEX_CONTROLLERS;
  TXBuffer.startStream();
  sendHeadandTail_stdtemplate(_HEAD);

  // 'index' value in the URL
  uint8_t controllerindex  = getFormItemInt(F("index"), 0);
  boolean controllerNotSet = controllerindex == 0;
  --controllerindex; // Index in URL is starting from 1, but starting from 0 in the array.

  const int protocol_webarg_value = getFormItemInt(F("protocol"), -1);

  // submitted data
  if ((protocol_webarg_value != -1) && !controllerNotSet)
  {
    const protocolIndex_t protocolIndex = protocol_webarg_value;
    bool mustInit                       = false;
    bool mustCallCpluginSave            = false;
    {
      // Place in a scope to free ControllerSettings memory ASAP
      MakeControllerSettings(ControllerSettings); // -V522

      if (!AllocatedControllerSettings()) {
        addHtmlError(F("Not enough free memory to save settings"));
      } else {
        // Need to make sure every byte between the members is also zero
        // Otherwise the checksum will fail and settings will be saved too often.
        ControllerSettings->reset();

        if (Settings.Protocol[controllerindex] != protocolIndex)
        {
          // Protocol has changed.
          Settings.Protocol[controllerindex] = protocolIndex;

          // there is a protocolIndex selected?
          if (protocolIndex != 0)
          {
            mustInit = true;
            handle_controllers_clearLoadDefaults(controllerindex, *ControllerSettings);
          }
        }

        // subitted same protocolIndex
        else
        {
          // there is a protocolIndex selected
          if (protocolIndex != 0)
          {
            mustInit = true;
            handle_controllers_CopySubmittedSettings(controllerindex, *ControllerSettings);
            mustCallCpluginSave = true;
          }
        }
        addHtmlError(SaveControllerSettings(controllerindex, *ControllerSettings));
      }
    }

    if (mustCallCpluginSave) {
      // Call CPLUGIN_WEBFORM_SAVE after destructing ControllerSettings object to reduce RAM usage.
      // Controller plugin almost only deals with custom controller settings.
      // Even if they need to save things to the ControllerSettings, then the changes must
      // already be saved first as the CPluginCall does not have the ControllerSettings as argument.
      handle_controllers_CopySubmittedSettings_CPluginCall(controllerindex);
    }
    addHtmlError(SaveSettings());

    if (mustInit) {
      // Init controller plugin using the new settings.
      protocolIndex_t ProtocolIndex = getProtocolIndex_from_ControllerIndex(controllerindex);

      if (validProtocolIndex(ProtocolIndex)) {
        struct EventStruct TempEvent;
        TempEvent.ControllerIndex = controllerindex;
        String dummy;
        CPlugin::Function cfunction =
          Settings.ControllerEnabled[controllerindex] ? CPlugin::Function::CPLUGIN_INIT : CPlugin::Function::CPLUGIN_EXIT;
        CPluginCall(ProtocolIndex, cfunction, &TempEvent, dummy);
      }
    }
  }

  html_add_form();

  if (controllerNotSet)
  {
    handle_controllers_ShowAllControllersTable();
  }
  else
  {
    handle_controllers_ControllerSettingsPage(controllerindex);
  }

  sendHeadandTail_stdtemplate(_TAIL);
  TXBuffer.endStream();
}

// ********************************************************************************
// Selected controller has changed.
// Clear all Controller settings and load some defaults
// ********************************************************************************
void handle_controllers_clearLoadDefaults(uint8_t controllerindex, ControllerSettingsStruct& ControllerSettings)
{
  // Protocol has changed and it was not an empty one.
  // reset (some) default-settings
  protocolIndex_t ProtocolIndex = getProtocolIndex_from_CPluginID(Settings.Protocol[controllerindex]);

  if (!validProtocolIndex(ProtocolIndex)) {
    return;
  }

  const ProtocolStruct& proto = getProtocolStruct(ProtocolIndex);

  ControllerSettings.reset();
# if FEATURE_MQTT_TLS
  ControllerSettings.TLStype(TLS_types::NoTLS);
# endif // if FEATURE_MQTT_TLS
  ControllerSettings.Port = proto.defaultPort;

  // Load some templates from the controller.
  struct EventStruct TempEvent;

  // Hand over the controller settings in the Data pointer, so the controller can set some defaults.
  TempEvent.Data = (uint8_t *)(&ControllerSettings);

  if (proto.usesTemplate) {
    String dummy;
    CPluginCall(ProtocolIndex, CPlugin::Function::CPLUGIN_PROTOCOL_TEMPLATE, &TempEvent, dummy);
  }
  safe_strncpy(ControllerSettings.Subscribe,            TempEvent.String1.c_str(), sizeof(ControllerSettings.Subscribe));
  safe_strncpy(ControllerSettings.Publish,              TempEvent.String2.c_str(), sizeof(ControllerSettings.Publish));
  safe_strncpy(ControllerSettings.MQTTLwtTopic,         TempEvent.String3.c_str(), sizeof(ControllerSettings.MQTTLwtTopic));
  safe_strncpy(ControllerSettings.LWTMessageConnect,    TempEvent.String4.c_str(), sizeof(ControllerSettings.LWTMessageConnect));
  safe_strncpy(ControllerSettings.LWTMessageDisconnect, TempEvent.String5.c_str(), sizeof(ControllerSettings.LWTMessageDisconnect));

  // NOTE: do not enable controller by default, give user a change to enter sensible values first
  Settings.ControllerEnabled[controllerindex] = false;

  // not resetted to default (for convenience)
  // SecuritySettings.ControllerUser[controllerindex]
  // SecuritySettings.ControllerPassword[controllerindex]

  ClearCustomControllerSettings(controllerindex);
}

// ********************************************************************************
// Collect all submitted form data and store in the ControllerSettings
// ********************************************************************************
void handle_controllers_CopySubmittedSettings(uint8_t controllerindex, ControllerSettingsStruct& ControllerSettings)
{
  // copy all settings to controller settings struct
  for (int parameterIdx = 0; parameterIdx <= ControllerSettingsStruct::CONTROLLER_ENABLED; ++parameterIdx) {
    ControllerSettingsStruct::VarType varType = static_cast<ControllerSettingsStruct::VarType>(parameterIdx);
    saveControllerParameterForm(ControllerSettings, controllerindex, varType);
  }
}

void handle_controllers_CopySubmittedSettings_CPluginCall(uint8_t controllerindex) {
  protocolIndex_t ProtocolIndex = getProtocolIndex_from_ControllerIndex(controllerindex);

  if (validProtocolIndex(ProtocolIndex)) {
    struct EventStruct TempEvent;
    TempEvent.ControllerIndex = controllerindex;

    // Call controller plugin to save CustomControllerSettings
    String dummy;
    CPluginCall(ProtocolIndex, CPlugin::Function::CPLUGIN_WEBFORM_SAVE, &TempEvent, dummy);
  }
}

// ********************************************************************************
// Show table with all selected controllers
// ********************************************************************************
void handle_controllers_ShowAllControllersTable()
{
  html_table_class_multirow();
  html_TR();
  html_table_header(F(""),        70);
  html_table_header(F("Nr"),      50);
  html_table_header(F("Enabled"), 100);
  html_table_header(F("Protocol"));
  html_table_header(F("Host"));
  html_table_header(F("Port"));

  MakeControllerSettings(ControllerSettings); // -V522

  if (AllocatedControllerSettings()) {
    for (controllerIndex_t x = 0; x < CONTROLLER_MAX; x++)
    {
      const bool cplugin_set = Settings.Protocol[x] != INVALID_C_PLUGIN_ID;


      LoadControllerSettings(x, *ControllerSettings);
      html_TR_TD();

      if (cplugin_set && !supportedCPluginID(Settings.Protocol[x])) {
        html_add_button_prefix(F("red"), true);
      } else {
        html_add_button_prefix();
      }
      {
        addHtml(F("controllers?index="));
        addHtmlInt(x + 1);
        addHtml(F("'>"));

        if (cplugin_set) {
          addHtml(F("Edit"));
        } else {
          addHtml(F("Add"));
        }
        addHtml(F("</a><TD>"));
        addHtml(getControllerSymbol(x));
      }
      html_TD();

      if (cplugin_set)
      {
        addEnabled(Settings.ControllerEnabled[x]);

        html_TD();
        addHtml(getCPluginNameFromCPluginID(Settings.Protocol[x]));
        html_TD();
        const protocolIndex_t ProtocolIndex = getProtocolIndex_from_ControllerIndex(x);
        {
          String hostDescription;
          CPluginCall(ProtocolIndex, CPlugin::Function::CPLUGIN_WEBFORM_SHOW_HOST_CONFIG, 0, hostDescription);

          if (!hostDescription.isEmpty()) {
            addHtml(hostDescription);
          } else {
            addHtml(ControllerSettings->getHost());
          }
        }

        html_TD();
        const ProtocolStruct& proto = getProtocolStruct(ProtocolIndex);

        if ((INVALID_PROTOCOL_INDEX == ProtocolIndex) || proto.usesPort) {
          addHtmlInt(13 == Settings.Protocol[x] ? Settings.UDPPort : ControllerSettings->Port); // P2P/C013 exception
        }
      }
      else {
        html_TD(3);
      }
    }
  }
  html_end_table();
  html_end_form();
}

// ********************************************************************************
// Show the controller settings page
// ********************************************************************************
void handle_controllers_ControllerSettingsPage(controllerIndex_t controllerindex)
{
  if (!validControllerIndex(controllerindex)) {
    return;
  }

  // Show controller settings page
  html_table_class_normal();
  addFormHeader(F("Controller Settings"));
  addRowLabel(F("Protocol"));
  uint8_t choice = Settings.Protocol[controllerindex];

  addSelector_Head_reloadOnChange(F("protocol"));
  addSelector_Item(F("- Standalone -"), 0, false, false, EMPTY_STRING);

  protocolIndex_t protocolIndex = 0;

  while (validProtocolIndex(protocolIndex))
  {
    const cpluginID_t number = getCPluginID_from_ProtocolIndex(protocolIndex);
    boolean disabled         = false; // !((controllerindex == 0) || !Protocol[x].usesMQTT);
    addSelector_Item(getCPluginNameFromProtocolIndex(protocolIndex),
                     number,
                     choice == number,
                     disabled);
    ++protocolIndex;
  }
  addSelector_Foot();

  addHelpButton(F("EasyProtocols"));

  const protocolIndex_t ProtocolIndex = getProtocolIndex_from_ControllerIndex(controllerindex);
  const ProtocolStruct& proto         = getProtocolStruct(ProtocolIndex);

  # ifndef LIMIT_BUILD_SIZE
  addRTDControllerButton(getCPluginID_from_ProtocolIndex(ProtocolIndex));
  # endif // ifndef LIMIT_BUILD_SIZE

  if (Settings.Protocol[controllerindex])
  {
    {
      MakeControllerSettings(ControllerSettings); // -V522

      if (!AllocatedControllerSettings()) {
        addHtmlError(F("Out of memory, cannot load page"));
      } else {
        LoadControllerSettings(controllerindex, *ControllerSettings);

        if (!proto.Custom)
        {
          if (proto.usesHost) {
            addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_USE_DNS);

            if (ControllerSettings->UseDNS)
            {
              addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_HOSTNAME);
            }
            else
            {
              addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_IP);
            }
          }

          if (proto.usesPort) {
            addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_PORT);
          }
          # if FEATURE_MQTT_TLS

          if (proto.usesMQTT && proto.usesTLS) {
            addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_MQTT_TLS_TYPE);
            addFormNote(F("Default ports: MQTT: 1883 / MQTT TLS: 8883"));
          }
          # endif // if FEATURE_MQTT_TLS
      # ifdef USES_ESPEASY_NOW

          if (proto.usesMQTT) {
            // FIXME TD-er: Currently only enabled for MQTT protocols, later for more
            addControllerParameterForm(*ControllerSettings, controllerindex,
                                       ControllerSettingsStruct::CONTROLLER_ENABLE_ESPEASY_NOW_FALLBACK);
          }
      # endif // ifdef USES_ESPEASY_NOW

          if (proto.usesQueue) {
            addTableSeparator(F("Controller Queue"), 2, 3);
            addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_MIN_SEND_INTERVAL);
            addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_MAX_QUEUE_DEPTH);
            addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_MAX_RETRIES);
            addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_FULL_QUEUE_ACTION);

            if (proto.allowsExpire) {
              addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_ALLOW_EXPIRE);
            }
            addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_DEDUPLICATE);
          }

          if (proto.usesCheckReply) {
            addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_CHECK_REPLY);
          }

          if (proto.usesTimeout) {
            addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_TIMEOUT);

            if (proto.usesHost) {
              addFormNote(F("Typical timeout: 100...300 msec for local host, >500 msec for internet hosts"));
            }
          }

          if (proto.usesSampleSets) {
            addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_SAMPLE_SET_INITIATOR);
          }

          if (proto.allowLocalSystemTime) {
            addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_USE_LOCAL_SYSTEM_TIME);
          }


          if (proto.useCredentials()) {
            addTableSeparator(F("Credentials"), 2, 3);
          }

          if (proto.useExtendedCredentials()) {
            addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_USE_EXTENDED_CREDENTIALS);
          }

          if (proto.usesAccount)
          {
            addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_USER);
          }

          if (proto.usesPassword)
          {
            addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_PASS);
          }
          # if FEATURE_MQTT

          if (proto.usesMQTT) {
            addTableSeparator(F("MQTT"), 2, 3);

            addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_CLIENT_ID);
            addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_UNIQUE_CLIENT_ID_RECONNECT);
            addRowLabel(F("Current Client ID"));
            addHtml(getMQTTclientID(*ControllerSettings));
            addFormNote(F("Updated on load of this page"));
            addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_RETAINFLAG);
          }
          # endif // if FEATURE_MQTT


          if (proto.usesTemplate
          # if FEATURE_MQTT
              || proto.usesMQTT
          # endif // if FEATURE_MQTT
              )
          {
            addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_SUBSCRIBE);
            addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_PUBLISH);
          }
          # if FEATURE_MQTT

          if (proto.usesMQTT)
          {
            addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_LWT_TOPIC);
            addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_LWT_CONNECT_MESSAGE);
            addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_LWT_DISCONNECT_MESSAGE);
            addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_SEND_LWT);
            addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_WILL_RETAIN);
            addControllerParameterForm(*ControllerSettings, controllerindex, ControllerSettingsStruct::CONTROLLER_CLEAN_SESSION);
          }
          # endif // if FEATURE_MQTT
        }
      }

      // End of scope for ControllerSettings, destruct it to save memory.
    }
    {
      // Load controller specific settings
      struct EventStruct TempEvent;
      TempEvent.ControllerIndex = controllerindex;

      String webformLoadString;
      CPluginCall(ProtocolIndex, CPlugin::Function::CPLUGIN_WEBFORM_LOAD, &TempEvent, webformLoadString);

      if (webformLoadString.length() > 0) {
        addHtmlError(F("Bug in CPlugin::Function::CPLUGIN_WEBFORM_LOAD, should not append to string, use addHtml() instead"));
      }
    }
    {
# if FEATURE_MQTT

      if (proto.usesMQTT) {
        addFormSubHeader(F("Connection Status"));
        addRowLabel(F("MQTT Client Connected"));
        addEnabled(MQTTclient_connected);

#  if FEATURE_MQTT_TLS

        if (proto.usesTLS) {
          addRowLabel(F("Last Error"));
          addHtmlInt(mqtt_tls_last_error);
          addHtml(F(": "));
          addHtml(mqtt_tls_last_errorstr);

            #   ifdef ESP32

          if (MQTTclient_connected && (mqtt_tls != nullptr)) {
            MakeControllerSettings(ControllerSettings); // -V522

            if (!AllocatedControllerSettings()) {
              addHtmlError(F("Out of memory, cannot load page"));
            } else {
              LoadControllerSettings(controllerindex, *ControllerSettings);

              // FIXME TD-er: Implement retrieval of certificate
/*

              addFormSubHeader(F("Peer Certificate"));

              {
                addFormTextArea(
                  F("Certificate Info"),
                  F("certinfo"),
                  mqtt_tls->getPeerCertificateInfo(),
                  -1,
                  -1,
                  -1,
                  true);
              }
              {
                String fingerprint;

                if (GetTLSfingerprint(fingerprint)) {
                  addFormTextBox(F("Certificate Fingerprint"),
                                 F("fingerprint"),
                                 fingerprint,
                                 64,
                                 true); // ReadOnly
                  addControllerParameterForm(*ControllerSettings, controllerindex,
                                             ControllerSettingsStruct::CONTROLLER_MQTT_TLS_STORE_FINGERPRINT);
                }
              }
              addFormSubHeader(F("Peer Certificate Chain"));
              {
                // FIXME TD-er: Must wrap this in divs to be able to fold it by default.
                const mbedtls_x509_crt *chain;

                chain = mqtt_tls->getPeerCertificate();

                int error { 0 };

                while (chain != nullptr && error == 0) {
                  //                    const bool mustShow = !chain->ca_istrue || chain->next == nullptr;
                  //                    if (mustShow) {
                  String pem, subject;
                  error = ESPEasy_WiFiClientSecure::cert_to_pem(chain, pem, subject);
                  {
                    String label;

                    if (chain->ca_istrue) {
                      label = F("CA ");
                    }
                    label += F("Certificate <tt>");
                    label += subject;
                    label += F("</tt>");
                    addRowLabel(label);
                  }

                  if (error == 0) {
                    addTextArea(
                      F("peerCertInfo"),
                      mqtt_tls->getPeerCertificateInfo(chain),
                      -1,
                      -1,
                      -1,
                      true,
                      false);

                    addTextArea(
                      F("pem"),
                      pem,
                      -1,
                      -1,
                      -1,
                      true,
                      false);
                  } else {
                    addHtmlInt(error);
                  }

                  if (chain->ca_istrue && (chain->next == nullptr)) {
                    // Add checkbox to store CA cert
                    addControllerParameterForm(*ControllerSettings, controllerindex,
                                               ControllerSettingsStruct::CONTROLLER_MQTT_TLS_STORE_CACERT);
                  }

                  //                    }
                  chain = chain->next;
                }
              }
*/
            }
          }
            #   endif // ifdef ESP32
        }
#  endif // if FEATURE_MQTT_TLS
      }
# endif // if FEATURE_MQTT
    }

    // Separate enabled checkbox as it doesn't need to use the ControllerSettings.
    // So ControllerSettings object can be destructed before controller specific settings are loaded.
    addControllerEnabledForm(controllerindex);
  }

  addFormSeparator(2);
  html_TR_TD();
  html_TD();
  addButton(F("controllers"), F("Close"));
  addSubmitButton();
  html_end_table();
  html_end_form();
}

#endif // ifdef WEBSERVER_CONTROLLERS
