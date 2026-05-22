#include "mqtt.h"

#ifdef MQTT_ROOT_TOPIC
#include "WiFi.h"

#include "telnet.h"
#include "player.h"
#include "config.h"

AsyncMqttClient mqttClient;
TimerHandle_t mqttReconnectTimer;
char topic[140], status[BUFLEN*3], vol[5], buf[20];

static size_t jsonEscapeTo(char* dst, size_t dstSize, const char* src) {
  if (!dst || dstSize == 0) return 0;
  size_t o = 0;
  if (!src) { dst[0] = 0; return 0; }
  for (size_t i = 0; src[i] && o + 1 < dstSize; i++) {
    const unsigned char c = static_cast<unsigned char>(src[i]);
    const char* esc = nullptr;
    switch (c) {
      case '\"': esc = "\\\""; break;
      case '\\': esc = "\\\\"; break;
      case '\b': esc = "\\b"; break;
      case '\f': esc = "\\f"; break;
      case '\n': esc = "\\n"; break;
      case '\r': esc = "\\r"; break;
      case '\t': esc = "\\t"; break;
      default: esc = nullptr; break;
    }
    if (esc) {
      for (size_t k = 0; esc[k] && o + 1 < dstSize; k++) dst[o++] = esc[k];
      continue;
    }
    if (c < 0x20) {
      if (o + 6 >= dstSize) break;
      const int wrote = snprintf(dst + o, dstSize - o, "\\u%04x", static_cast<unsigned int>(c));
      if (wrote != 6) break;
      o += 6;
      continue;
    }
    dst[o++] = static_cast<char>(c);
  }
  dst[o] = 0;
  return o;
}

void connectToMqtt() {
  mqttClient.connect();
}

void mqttInit() {
  mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  mqttClient.onMessage(onMqttMessage);
  if(strlen(MQTT_USER)>0) mqttClient.setCredentials(MQTT_USER, MQTT_PASS);
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  connectToMqtt();
}

void onMqttConnect(bool sessionPresent) {
  memset(topic, 0, 140);
  snprintf(topic, sizeof(topic), "%s%s", MQTT_ROOT_TOPIC, "command");
  mqttClient.subscribe(topic, 2);
  mqttPublishStatus();
  mqttPublishVolume();
  mqttPublishPlaylist();
}

void mqttPublishStatus() {
  if(mqttClient.connected()){
    memset(topic, 0, 140);
    memset(status, 0, BUFLEN*3);
    snprintf(topic, sizeof(topic), "%s%s", MQTT_ROOT_TOPIC, "status");
    char nameEsc[BUFLEN] = {0};
    char titleEsc[BUFLEN * 2] = {0};
    jsonEscapeTo(nameEsc, sizeof(nameEsc), config.station.name);
    jsonEscapeTo(titleEsc, sizeof(titleEsc), config.station.title);
    snprintf(status, sizeof(status), "{\"status\": %d, \"station\": %d, \"name\": \"%s\", \"title\": \"%s\", \"on\": %d}",
             player.status()==PLAYING?1:0, config.lastStation(), nameEsc, titleEsc, config.store.dspon);
    mqttClient.publish(topic, 0, true, status);
  }
}

void mqttPublishPlaylist() {
  if(mqttClient.connected()){
    memset(topic, 0, 140);
    memset(status, 0, BUFLEN*3);
    snprintf(topic, sizeof(topic), "%s%s", MQTT_ROOT_TOPIC, "playlist");
    snprintf(status, sizeof(status), "http://%s%s", WiFi.localIP().toString().c_str(), PLAYLIST_PATH);
    mqttClient.publish(topic, 0, true, status);
  }
}

void mqttPublishVolume(){
  if(mqttClient.connected()){
    memset(topic, 0, 140);
    memset(vol, 0, 5);
    snprintf(topic, sizeof(topic), "%s%s", MQTT_ROOT_TOPIC, "volume");
    snprintf(vol, sizeof(vol), "%d", config.store.volume);
    mqttClient.publish(topic, 0, true, vol);
  }
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  if (WiFi.isConnected()) {
    xTimerStart(mqttReconnectTimer, 0);
  }
}

void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total) {
  if (len == 0) return;
  if (index != 0) return;
  if (index + len != total) return;
  memset(buf, 0, 20);
  const size_t copyLen = (len < sizeof(buf) - 1) ? len : (sizeof(buf) - 1);
  memcpy(buf, payload, copyLen);
  buf[copyLen] = 0;
  if (strcmp(buf, "prev") == 0) {
    player.prev();
    return;
  }
  if (strcmp(buf, "next") == 0) {
    player.next();
    return;
  }
  if (strcmp(buf, "toggle") == 0) {
    player.toggle();
    return;
  }
  if (strcmp(buf, "stop") == 0) {
    player.sendCommand({PR_STOP, 0});
    //telnet.info();
    return;
  }
  if (strcmp(buf, "start") == 0 || strcmp(buf, "play") == 0) {
    player.sendCommand({PR_PLAY, config.lastStation()});
    return;
  }
  if (strcmp(buf, "boot") == 0 || strcmp(buf, "reboot") == 0) {
    ESP.restart();
    return;
  }
  if (strcmp(buf, "volm") == 0) {
    player.stepVol(false);
    return;
  }
  if (strcmp(buf, "volp") == 0) {
    player.stepVol(true);
    return;
  }
  if (strcmp(buf, "turnoff") == 0) {
    uint8_t sst = config.store.smartstart;
    config.setDspOn(0);
    player.sendCommand({PR_STOP, 0});
    //telnet.info();
    delay(100);
    config.saveValue(&config.store.smartstart, sst);
    return;
  }
  if (strcmp(buf, "turnon") == 0) {
    config.setDspOn(1);
    if (config.store.smartstart == 1) player.sendCommand({PR_PLAY, config.lastStation()});
    return;
  }
  int volume;
  if ( sscanf(buf, "vol %d", &volume) == 1) {
    if (volume < 0) volume = 0;
    if (volume > 254) volume = 254;
    player.setVol(volume);
    return;
  }
  int sb;
  if (sscanf(buf, "play %d", &sb) == 1 ) {
    if (sb < 1) sb = 1;
    if (sb >= config.store.countStation) sb = config.store.countStation;
    player.sendCommand({PR_PLAY, (uint16_t)sb});
    return;
  }
  if (strstr(buf, "http")==buf){
    const size_t burlCopyLen = (len < sizeof(player.burl) - 1) ? len : (sizeof(player.burl) - 1);
    memcpy(player.burl, payload, burlCopyLen);
    player.burl[burlCopyLen] = 0;
    return;
  }
}

#endif // #ifdef MQTT_ROOT_TOPIC
