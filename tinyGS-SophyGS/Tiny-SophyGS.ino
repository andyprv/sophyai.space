/***********************************************************************
  tinyGS.ini - GroundStation firmware
  
  Copyright (C) 2020 -2021 @G4lile0, @gmag12 and @dev_4m1g0

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.

  ***********************************************************************

  TinyGS is an open network of Ground Stations distributed around the
  world to receive and operate LoRa satellites, weather probes and other
  flying objects, using cheap and versatile modules.

  This project is based on ESP32 boards and is compatible with sx126x and
  sx127x you can build you own board using one of these modules but most
  of us use a development board like the ones listed in the Supported
  boards section.

  Supported boards
    Heltec WiFi LoRa 32 V1 (433MHz & 863-928MHz versions)
    Heltec WiFi LoRa 32 V2 (433MHz & 863-928MHz versions)
    TTGO LoRa32 V1 (433MHz & 868-915MHz versions)
    TTGO LoRa32 V2 (433MHz & 868-915MHz versions)
    TTGO LoRa32 V2 (Manually swapped SX1267 to SX1278)
    T-BEAM + OLED (433MHz & 868-915MHz versions)
    T-BEAM V1.0 + OLED
    FOSSA 1W Ground Station (433MHz & 868-915MHz versions)
    ESP32 dev board + SX126X with crystal (Custom build, OLED optional)
    ESP32 dev board + SX126X with TCXO (Custom build, OLED optional)
    ESP32 dev board + SX127X (Custom build, OLED optional)

  Supported modules
    sx126x
    sx127x

    World Map with active Ground Stations and satellite stimated position 
    Web of the project: https://tinygs.com/
    Github: https://github.com/G4lile0/tinyGS
    Main community chat: https://t.me/joinchat/DmYSElZahiJGwHX6jCzB3Q

    In order to onfigure your Ground Station please open a private chat to get your credentials https://t.me/tinygs_personal_bot
    Data channel (station status and received packets): https://t.me/tinyGS_Telemetry
    Test channel (simulator packets received by test groundstations): https://t.me/TinyGS_Test

    Developers:
      @gmag12       https://twitter.com/gmag12
      @dev_4m1g0    https://twitter.com/dev_4m1g0
      @g4lile0      https://twitter.com/G4lile0

====================================================
  IMPORTANT:
    - Change libraries/PubSubClient/src/PubSubClient.h
        #define MQTT_MAX_PACKET_SIZE 1000

**************************************************************************/

#if defined(ARDUINO) && ARDUINO >= 100
#include "Arduino.h"
#else
#include "WProgram.h"
#endif
#include "src/ConfigManager/ConfigManager.h"
#include "src/Display/Display.h"
#include "src/Mqtt/MQTT_Client.h"
#include "src/Mqtt/MQTT_Client_Fees.h"
#include "src/Status.h"
#include "src/Radio/Radio.h"
#include "src/ArduinoOTA/ArduinoOTA.h"
#include "src/OTA/OTA.h"
#include <ESPNtpClient.h>
#include <FailSafe.h>
#include "src/Logger/Logger.h"

#define MQTT_SOPHY
#define MQTT_TINY

#if MQTT_MAX_PACKET_SIZE != 1000
"Remeber to change libraries/PubSubClient/src/PubSubClient.h"
        "#define MQTT_MAX_PACKET_SIZE 1000"
#endif

#if  RADIOLIB_VERSION_MAJOR != (0x04) || RADIOLIB_VERSION_MINOR != (0x02) || RADIOLIB_VERSION_PATCH != (0x01) || RADIOLIB_VERSION_EXTRA != (0x00)
#error "You are not using the correct version of RadioLib please copy TinyGS/lib/RadioLib on Arduino/libraries"
#endif

#ifndef RADIOLIB_GODMODE
#error "Seems you are using Arduino IDE, edit /RadioLib/src/BuildOpt.h and uncomment #define RADIOLIB_GODMODE around line 367" 
#endif


const int MAX_CONSECUTIVE_BOOT = 10; // Number of rapid boot cycles before enabling fail safe mode
const time_t BOOT_FLAG_TIMEOUT = 10000; // Time in ms to reset fail safe mode activation flag

ConfigManager& configManager = ConfigManager::getInstance();
#ifdef MQTT_TINY
MQTT_Client& mqtt = MQTT_Client::getInstance();
#endif
#ifdef MQTT_SOPHY 
MQTT_Client_Fees& mqtt_fees = MQTT_Client_Fees::getInstance();
#endif 
Radio& radio = Radio::getInstance();

TaskHandle_t dispUpdate_handle;

const char* ntpServer = "time.cloudflare.com";
void printLocalTime();

// Global status
Status status;
Status status_sophy;


void printControls();
void switchTestmode();

void ntp_cb (NTPEvent_t e)
{
  switch (e.event) {
    case timeSyncd:
    case partlySync:
      //Serial.printf ("[NTP Event] %s\n", NTP.ntpEvent2str (e));
      status.time_offset = e.info.offset;
      break;
    default:
      break;
  }
}

void displayUpdate_task (void* arg)
{
  for (;;){
      displayUpdate ();
  }
}

void wifiConnected()
{
  NTP.setInterval (120); // Sync each 2 minutes
  NTP.setTimeZone (configManager.getTZ ()); // Get TX from config manager
  NTP.onNTPSyncEvent (ntp_cb); // Register event callback
  NTP.setMinSyncAccuracy (2000); // Sync accuracy target is 2 ms
  NTP.settimeSyncThreshold (1000); // Sync only if calculated offset absolute value is greater than 1 ms
  NTP.setMaxNumSyncRetry (2); // 2 resync trials if accuracy not reached
  NTP.begin (ntpServer); // Start NTP client
  Serial.printf ("NTP started");
  
  time_t startedSync = millis ();
  while (NTP.syncStatus() != syncd && millis() - startedSync < 5000) // Wait 5 seconds to get sync
  {
    delay (100);
  }

  printLocalTime();

  configManager.printConfig();
  arduino_ota_setup();
  displayShowConnected();

  radio.init();
  if (!radio.isReady())
  {
    Serial.println("LoRa initialization failed. Please connect to " + WiFi.localIP().toString() + " and make sure the board selected matches your hardware.");
    displayShowLoRaError();
  }

  configManager.delay(1000); // wait to show the connected screen
#ifdef MQTT_TINY
  mqtt.begin();
#endif
#ifdef MQTT_SOPHY
  mqtt_fees.begin();
#endif 

  // TODO: Make this beautiful
  displayShowWaitingMqttConnection();
  printControls();
}

void setup()
{
  Serial.begin(115200);
  delay(299);

  //FailSafe.checkBoot (MAX_CONSECUTIVE_BOOT); // Parameters are optional
  //if (FailSafe.isActive ()) // Skip all user setup if fail safe mode is activated
  //  return;

  Log::console(PSTR("SophyGS-TinyGS Version %d - %s"), status.version, status.git_version);
  configManager.setWifiConnectionCallback(wifiConnected);
  configManager.init();
  // make sure to call doLoop at least once before starting to use the configManager
  configManager.doLoop();
  pinMode (configManager.getBoardConfig().PROG__BUTTON, INPUT_PULLUP);
  displayInit();
  displayShowInitialCredits();
  configManager.printConfig();
#define WAIT_FOR_BUTTON 3000
#define RESET_BUTTON_TIME 5000
  unsigned long start_waiting_for_button = millis ();
  unsigned long button_pushed_at;
  Log::debug(PSTR("Waiting for reset config button"));
  bool button_pushed = false;
  while (millis () - start_waiting_for_button < WAIT_FOR_BUTTON)
  {
	  if (!digitalRead (configManager.getBoardConfig().PROG__BUTTON))
    {
		  button_pushed = true;
		  button_pushed_at = millis ();
		  Log::debug(PSTR("Reset button pushed"));
		  while (millis () - button_pushed_at < RESET_BUTTON_TIME)
      {
			  if (digitalRead (configManager.getBoardConfig().PROG__BUTTON))
        {
				  Log::debug(PSTR("Reset button released"));
				  button_pushed = false;
				  break;
			  }
		  }
		  if (button_pushed)
      {
			  Log::debug(PSTR("Reset config triggered"));
			  WiFi.begin ("0", "0");
			  WiFi.disconnect ();
		  }
	  }
  }

  if (button_pushed)
  {
    configManager.resetAPConfig();
    ESP.restart();
  }
  
  if (configManager.isApMode())
    displayShowApMode();
  else 
    displayShowStaMode(false);
  
  delay(500);  
}

void loop() {
  static bool startDisplayTask = true;
    
  FailSafe.loop (BOOT_FLAG_TIMEOUT); // Use always this line
  if (FailSafe.isActive ()) // Skip all user loop code if Fail Safe mode is active
    return;
    
  configManager.doLoop();

  static bool wasConnected = false;
  if (!configManager.isConnected())
  {
    if (configManager.isApMode() && wasConnected)
      displayShowApMode();
    else 
      displayShowStaMode(configManager.isApMode());

    return;
  }
  wasConnected = true;
  #ifdef MQTT_TINY
  mqtt.loop();
  #endif 
  #ifdef MQTT_SOPHY
  mqtt_fees.loop();
  #endif 
  ArduinoOTA.handle();
  OTA::loop();
  
  if(Serial.available())
  {
    radio.disableInterrupt();

    // get the first character
    char serialCmd = Serial.read();

    // wait for a bit to receive any trailing characters
    delay(50);

    // dump the serial buffer
    while(Serial.available())
    {
      Serial.read();
    }

    // process serial command
    switch(serialCmd) {
      case 'e':
        configManager.resetAllConfig();
        ESP.restart();
        break;
      case 't':
        switchTestmode();
        break;
      case 'b':
        ESP.restart();
        break;
      case 'p':
        if (!configManager.getAllowTx())
        {
          Log::console(PSTR("Radio transmission is not allowed by config! Check your config on the web panel and make sure transmission is allowed by local regulations"));
          break;
        }

        static long lastTestPacketTime = 0;
        if (millis() - lastTestPacketTime < 20*1000)
        {
          Log::console(PSTR("Please wait a few seconds to send another test packet."));
          break;
        }
        
        radio.sendTestPacket();
        lastTestPacketTime = millis();
        Log::console(PSTR("Sending test packet to nearby stations!"));
        break;
      default:
        Log::console(PSTR("Unknown command: %c"), serialCmd);
        break;
    }

    radio.enableInterrupt();
  }

  if (!radio.isReady())
  {
    displayShowLoRaError();
    return;
  }

  if (startDisplayTask)
  {
    startDisplayTask = false;
    xTaskCreateUniversal (
            displayUpdate_task,           // Display loop function
            "Display Update",             // Task name
            4096,                         // Stack size
            NULL,                         // Function argument, not needed
            1,                            // Priority, running higher than 1 causes errors on MQTT comms
            &dispUpdate_handle,           // Task handle
            CONFIG_ARDUINO_RUNNING_CORE); // Running core, should be 1
  }

  radio.listen();
}

void switchTestmode()
{  
  if (configManager.getTestMode())
  {
      configManager.setTestMode(false);
      Log::console(PSTR("Changed from test mode to normal mode"));
  }
  else
  {
      configManager.setTestMode(true);
      Log::console(PSTR("Changed from normal mode to test mode"));
  }
}

void printLocalTime()
{
    time_t currenttime = time (NULL);
    if (currenttime < 0) {
        Log::error (PSTR ("Failed to obtain time: %d"), currenttime);
        return;
    }
    struct tm* timeinfo;
    
    timeinfo = localtime (&currenttime);
  
  Serial.println(timeinfo, "%A, %B %d %Y %H:%M:%S");
}

// function to print controls
void printControls()
{
  Log::console(PSTR("------------- Controls -------------"));
  Log::console(PSTR("t - change the test mode and restart"));
  Log::console(PSTR("e - erase board config and reset"));
  Log::console(PSTR("b - reboot the board"));
  Log::console(PSTR("p - send test packet to nearby stations (to check transmission)"));
  Log::console(PSTR("------------------------------------"));
}