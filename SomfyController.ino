#include <WiFi.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include <esp_partition.h>
#include "ConfigSettings.h"
#include "Network.h"
#include "Web.h"
#include "Sockets.h"
#include "Utils.h"
#include "Somfy.h"
#include "MQTT.h"
#include "GitOTA.h"

ConfigSettings settings;
Web webServer;
SocketEmitter sockEmit;
Network net;
rebootDelay_t rebootDelay;
SomfyShadeController somfy;
MQTTClass mqtt;
GitUpdater git;

uint32_t oldheap = 0;

bool mountFileSystem() {
  const esp_partition_t *partition = esp_partition_find_first(
    ESP_PARTITION_TYPE_DATA,
    ESP_PARTITION_SUBTYPE_DATA_SPIFFS,
    nullptr
  );
  if(!partition) {
    Serial.println("Error locating LittleFS partition");
    return false;
  }

  Serial.printf(
    "Mounting LittleFS partition '%s' at 0x%06lx (%lu bytes)...\n",
    partition->label,
    (unsigned long)partition->address,
    (unsigned long)partition->size
  );
  return LittleFS.begin(false, "/littlefs", 10, partition->label);
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("Startup/Boot....");
  Serial.println("Mounting File System...");
  if(mountFileSystem()) Serial.println("File system mounted successfully");
  else Serial.println("Error mounting file system");
  settings.begin();
  if(WiFi.status() == WL_CONNECTED) WiFi.disconnect(true);
  delay(10);
  Serial.println();
  webServer.startup();
  webServer.begin();
  delay(1000);
  net.setup();  
  somfy.begin();
  //git.checkForUpdate();
  esp_task_wdt_init(30, true); //enable panic so ESP32 restarts (increased to 30 seconds for W5500)
  esp_task_wdt_add(NULL); //add current thread to WDT watch

}

void loop() {
  esp_task_wdt_reset();
  // put your main code here, to run repeatedly:
  //uint32_t heap = ESP.getFreeHeap();
  if(rebootDelay.reboot && millis() > rebootDelay.rebootTime) {
    Serial.print("Rebooting after ");
    Serial.print(rebootDelay.rebootTime);
    Serial.println("ms");
    net.end();
    ESP.restart();
    return;
  }

  uint32_t timing = millis();

  net.loop();
  if(millis() - timing > 100) {
    Serial.printf("Timing Net: %ldms\n", millis() - timing);
  }
  esp_task_wdt_reset();
  timing = millis();

  somfy.loop();
  if(millis() - timing > 100) {
    Serial.printf("Timing Somfy: %ldms\n", millis() - timing);
  }
  esp_task_wdt_reset();
  timing = millis();

  if(net.connected() || net.softAPOpened) {
    if(!rebootDelay.reboot && net.connected() && !net.softAPOpened) {
      git.loop();
      esp_task_wdt_reset();
    }

    webServer.loop();
    if(millis() - timing > 100) {
      Serial.printf("Timing WebServer: %ldms\n", millis() - timing);
    }
    esp_task_wdt_reset();
    timing = millis();

    sockEmit.loop();
    if(millis() - timing > 100) {
      Serial.printf("Timing Socket: %ldms\n", millis() - timing);
    }
    esp_task_wdt_reset();
  }

  if(rebootDelay.reboot && millis() > rebootDelay.rebootTime) {
    net.end();
    ESP.restart();
    return;
  }

  // Final watchdog reset before end of loop
  esp_task_wdt_reset();

  // Small delay to prevent tight loop from consuming too much CPU
  delay(1);
}
