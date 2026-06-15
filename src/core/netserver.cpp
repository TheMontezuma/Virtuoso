#include "netserver.h"
#include <SPIFFS.h>

#include "config.h"
#include "player.h"
#include "telnet.h"
#include "display.h"
#include "options.h"
#include "network.h"
#include "mqtt.h"
#include "controls.h"
#include <Update.h>
#include <ESPmDNS.h>
#ifdef USE_SD
#include "sdmanager.h"
#endif
#ifndef MIN_MALLOC
#define MIN_MALLOC 24112
#endif
#ifndef NSQ_SEND_DELAY
  #define NSQ_SEND_DELAY       (TickType_t)100  //portMAX_DELAY?
#endif

//#define CORS_DEBUG

NetServer netserver;

AsyncWebServer webserver(80);
AsyncWebSocket websocket("/ws");
AsyncUDP udp;

static const uint8_t kWsClientSlots = 16;
static uint32_t wsClientIds[kWsClientSlots] = {0};
static bool wsClientTelemetry[kWsClientSlots] = {false};
static bool wsTelemetryDefault = false;

static int wsClientFindSlot(uint32_t id) {
  if (id == 0) return -1;
  for (int i = 0; i < kWsClientSlots; i++) {
    if (wsClientIds[i] == id) return i;
  }
  return -1;
}

static int wsClientFindFreeSlot() {
  for (int i = 0; i < kWsClientSlots; i++) {
    if (wsClientIds[i] == 0) return i;
  }
  return -1;
}

static void wsClientOnConnect(uint32_t id) {
  int s = wsClientFindSlot(id);
  if (s < 0) s = wsClientFindFreeSlot();
  if (s < 0) return;
  wsClientIds[s] = id;
  wsClientTelemetry[s] = wsTelemetryDefault;
}

static void wsClientOnDisconnect(uint32_t id) {
  int s = wsClientFindSlot(id);
  if (s < 0) return;
  wsClientIds[s] = 0;
  wsClientTelemetry[s] = false;
}

static void wsClientSetTelemetry(uint32_t id, bool enabled) {
  int s = wsClientFindSlot(id);
  if (s < 0) return;
  wsClientTelemetry[s] = enabled;
  wsTelemetryDefault = enabled;
}

static bool wsClientTelemetryEnabled(uint32_t id) {
  int s = wsClientFindSlot(id);
  if (s < 0) return false;
  return wsClientTelemetry[s];
}

static bool wsAnyTelemetryEnabled() {
  for (int i = 0; i < kWsClientSlots; i++) {
    if (wsClientIds[i] != 0 && wsClientTelemetry[i]) return true;
  }
  return false;
}

static void wsTextTelemetryAll(const char* msg) {
  for (int i = 0; i < kWsClientSlots; i++) {
    if (wsClientIds[i] != 0 && wsClientTelemetry[i]) websocket.text(wsClientIds[i], msg);
  }
}

String processor(const String& var);
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void handleUploadWeb(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void handleUpdate(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void handleHTTPArgs(AsyncWebServerRequest * request);
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);

bool  shouldReboot  = false;
#ifdef MQTT_ROOT_TOPIC
Ticker mqttplaylistticker;
bool  mqttplaylistblock = false;
void mqttplaylistSend() {
  mqttplaylistblock = true;
  mqttplaylistticker.detach();
  mqttPublishPlaylist();
  mqttplaylistblock = false;
}
#endif

char* updateError() {
  static char ret[140] = {0};
  snprintf(ret, sizeof(ret), "Aktualizacja nie powiodła się (błąd %d)<br /> %s", (int)Update.getError(), Update.errorString());
  return ret;
}

bool NetServer::begin(bool quiet) {
  if(network.status==SDREADY) return true;
  if(!quiet) Serial.print("##[BOOT]#\tnetserver.begin\t");
  importRequest = IMDONE;
  irRecordEnable = false;
  nsQueue = xQueueCreate( 20, sizeof( nsRequestParams_t ) );
  while(nsQueue==NULL){;}
  if(config.emptyFS){
    webserver.on("/", HTTP_GET, [](AsyncWebServerRequest * request) { request->send_P(200, "text/html", emptyfs_html, processor); });
    webserver.on("/", HTTP_POST, [](AsyncWebServerRequest *request) { 
      if(request->arg("ssid")!="" && request->arg("pass")!=""){
        char buf[BUFLEN];
        memset(buf, 0, BUFLEN);
        snprintf(buf, BUFLEN, "%s\t%s", request->arg("ssid").c_str(), request->arg("pass").c_str());
        request->redirect("/");
        config.saveWifiFromNextion(buf);
        return;
      }
      request->redirect("/"); 
      ESP.restart(); 
    }, handleUploadWeb);
  }else{
    webserver.on("/", HTTP_ANY, handleHTTPArgs);
    webserver.on("/webboard", HTTP_GET, [](AsyncWebServerRequest * request) { request->send_P(200, "text/html", emptyfs_html, processor); });
    webserver.on("/webboard", HTTP_POST, [](AsyncWebServerRequest *request) { request->redirect("/"); }, handleUploadWeb);
  }
  
  webserver.on(PLAYLIST_PATH, HTTP_GET, handleHTTPArgs);
  webserver.on(INDEX_PATH, HTTP_GET, handleHTTPArgs);
  webserver.on(PLAYLIST_SD_PATH, HTTP_GET, handleHTTPArgs);
  webserver.on(INDEX_SD_PATH, HTTP_GET, handleHTTPArgs);
  webserver.on(SSIDS_PATH, HTTP_GET, handleHTTPArgs);
  
  webserver.on("/upload", HTTP_POST, beginUpload, handleUpload);
  webserver.on("/update", HTTP_GET, handleHTTPArgs);
  webserver.on("/update", HTTP_POST, beginUpdate, handleUpdate);
  webserver.on("/settings", HTTP_GET, handleHTTPArgs);
  if (IR_PIN != 255) webserver.on("/ir", HTTP_GET, handleHTTPArgs);
  webserver.on("/api/i2c", HTTP_GET, [](AsyncWebServerRequest *request) {
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"i2c_hz\":%u}", (unsigned)I2C_FREQ_HZ);
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", buf);
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
  });
  webserver.on("/api/dstauto", HTTP_ANY, [](AsyncWebServerRequest *request) {
    if(request->hasParam("en")) {
      bool enabled = request->getParam("en")->value().toInt() != 0;
      config.setDstAutoEU(enabled);
      if(!enabled) config.setTimezoneOffset(0);
      network.applyTimezone(true);
      if(getLocalTime(&network.timeinfo)){
        mktime(&network.timeinfo);
        display.putRequest(CLOCK);
        #if RTCSUPPORTED
          if (config.isRTCFound()) rtc.setTime(&network.timeinfo);
        #endif
      }
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "{\"dstauto\":%d}", config.getDstAutoEU() ? 1 : 0);
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", buf);
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
  });
  webserver.serveStatic("/", SPIFFS, "/www/").setCacheControl("max-age=31536000");
#ifdef CORS_DEBUG
  DefaultHeaders::Instance().addHeader(F("Access-Control-Allow-Origin"), F("*"));
  DefaultHeaders::Instance().addHeader(F("Access-Control-Allow-Headers"), F("content-type"));
#endif
  webserver.begin();
  if(strlen(config.store.mdnsname)>0)
    MDNS.begin(config.store.mdnsname);
  websocket.onEvent(onWsEvent);
  webserver.addHandler(&websocket);

  //echo -n "helle?" | socat - udp-datagram:255.255.255.255:44490,broadcast
  if (udp.listen(44490)) {
    udp.onPacket([](AsyncUDPPacket packet) {
      if (strcmp((char*)packet.data(), "helle?") == 0)
        packet.println(WiFi.localIP());
    });
  }
  if(!quiet) Serial.println("gotowe");
  return true;
}

void NetServer::beginUpdate(AsyncWebServerRequest *request) {
  shouldReboot = !Update.hasError();
  AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldReboot ? "OK" : updateError());
  response->addHeader("Connection", "close");
  request->send(response);
}

void handleUpdate(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (!index) {
    int target = (request->getParam("updatetarget", true)->value() == "spiffs") ? U_SPIFFS : U_FLASH;
    Serial.printf("Start aktualizacji: %s\n", filename.c_str());
    player.sendCommand({PR_STOP, 0});
    display.putRequest(NEWMODE, UPDATING);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, target)) {
      Update.printError(Serial);
      request->send(200, "text/html", updateError());
    }
  }
  if (!Update.hasError()) {
    if (Update.write(data, len) != len) {
      Update.printError(Serial);
      request->send(200, "text/html", updateError());
    }
  }
  if (final) {
    if (Update.end(true)) {
      Serial.printf("Aktualizacja zakończona: %uB\n", index + len);
    } else {
      Update.printError(Serial);
      request->send(200, "text/html", updateError());
    }
  }
}

void NetServer::beginUpload(AsyncWebServerRequest *request) {
  if (request->hasParam("plfile", true, true)) {
    netserver.importRequest = IMPL;
    request->send(200);
  } else if (request->hasParam("wifile", true, true)) {
    netserver.importRequest = IMWIFI;
    request->send(200);
  } else {
    request->send(404);
  }
}

size_t NetServer::chunkedHtmlPageCallback(uint8_t* buffer, size_t maxLen, size_t index){
  File requiredfile;
  bool sdpl = strcmp(netserver.chunkedPathBuffer, PLAYLIST_SD_PATH) == 0;
  if(sdpl){
    requiredfile = config.SDPLFS()->open(netserver.chunkedPathBuffer, "r");
  }else{
    requiredfile = SPIFFS.open(netserver.chunkedPathBuffer, "r");
  }
  if (!requiredfile) return 0;
  size_t filesize = requiredfile.size();
  size_t needread = filesize - index;
  if (!needread) {
    requiredfile.close();
    return 0;
  }
  size_t canread = (needread > maxLen) ? maxLen : needread;
  DBGVB("[%s] seek to %d in %s and read %d bytes with maxLen=%d", __func__, index, netserver.chunkedPathBuffer, canread, maxLen);
  requiredfile.seek(index, SeekSet);
  requiredfile.read(buffer, canread);
  index += canread;
  if (requiredfile) requiredfile.close();
  delay(0);
  return canread;
}

void NetServer::chunkedHtmlPage(const String& contentType, AsyncWebServerRequest *request, const char * path, bool doproc) {
  memset(chunkedPathBuffer, 0, sizeof(chunkedPathBuffer));
  strlcpy(chunkedPathBuffer, path, sizeof(chunkedPathBuffer)-1);
  AsyncWebServerResponse *response;
  if(doproc)
    response = request->beginChunkedResponse(contentType, chunkedHtmlPageCallback, processor);
  else
    response = request->beginChunkedResponse(contentType, chunkedHtmlPageCallback);
  request->send(response);
}

#ifndef DSP_NOT_FLIPPED
  #define DSP_CAN_FLIPPED true
#else
  #define DSP_CAN_FLIPPED false
#endif
#if !defined(HIDE_WEATHER) && (!defined(DUMMYDISPLAY) && !defined(USE_NEXTION))
  #define SHOW_WEATHER  true
#else
  #define SHOW_WEATHER  false
#endif
#if (!defined(DUMMYDISPLAY) && !defined(USE_NEXTION))
  #define SHOW_STOCKS   true
#else
  #define SHOW_STOCKS   false
#endif

#ifndef NS_QUEUE_TICKS
  #define NS_QUEUE_TICKS 0
#endif

const char *getFormat(BitrateFormat _format) {
  switch (_format) {
    case BF_MP3:  return "MP3";
    case BF_AAC:  return "AAC";
    case BF_FLAC: return "FLC";
    case BF_OGG:  return "OGG";
    case BF_WAV:  return "WAV";
    default:      return "bitrate";
  }
}

char wsbuf[512];
static size_t jsonEscapeAppend(char* dst, size_t dstSize, const char* src) {
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

void NetServer::processQueue(){
  if(nsQueue==NULL) return;
  nsRequestParams_t request;
  if(xQueueReceive(nsQueue, &request, NS_QUEUE_TICKS)){
    memset(wsbuf, 0, sizeof(wsbuf));
    uint8_t clientId = request.clientId;
    switch (request.type) {
      case PLAYLIST:        getPlaylist(clientId); break;
      case PLAYLISTSAVED:   {
        #ifdef USE_SD
        if(config.getMode()==PM_SDCARD) {
        //  config.indexSDPlaylist();
          config.initSDPlaylist();
        }
        #endif
        if(config.getMode()==PM_WEB){
          config.indexPlaylist(); 
          config.initPlaylist(); 
        }
        getPlaylist(clientId); break;
      }
      case GETACTIVE: {
          bool dbgact = false, nxtn=false;
          String act = F("\"group_wifi\",");
          if (network.status == CONNECTED) {
                                                                act += F("\"group_system\",");
            if (BRIGHTNESS_PIN != 255 || DSP_CAN_FLIPPED || DSP_MODEL == DSP_NOKIA5110 || dbgact)    act += F("\"group_display\",");
          #ifdef USE_NEXTION
                                                                act += F("\"group_nextion\",");
            if (!SHOW_WEATHER || dbgact)                        act += F("\"group_weather\",");
            nxtn=true;
          #endif
                                                              #if defined(LCD_I2C) || defined(DSP_OLED)
                                                                act += F("\"group_oled\",");
                                                              #endif
                                                              #ifndef HIDE_VU
                                                                act += F("\"group_vu\",");
                                                              #endif
            if (BRIGHTNESS_PIN != 255 || nxtn || dbgact)                act += F("\"group_brightness\",");
            if (DSP_CAN_FLIPPED || dbgact)                      act += F("\"group_tft\",");
            if (TS_MODEL != TS_MODEL_UNDEFINED || dbgact)       act += F("\"group_touch\",");
            if (DSP_MODEL == DSP_NOKIA5110)                     act += F("\"group_nokia\",");
                                                                act += F("\"group_timezone\",");
            if (SHOW_WEATHER || dbgact)                         act += F("\"group_weather\",");
            if (SHOW_STOCKS || dbgact)                          act += F("\"group_stocks\",");
                                                                act += F("\"group_controls\",");
            if (ENC_BTNL != 255 || ENC2_BTNL != 255 || dbgact)  act += F("\"group_encoder\",");
            if (IR_PIN != 255 || dbgact)                        act += F("\"group_ir\",");
          }
                                                                act = act.substring(0, act.length() - 1);
          snprintf(wsbuf, sizeof(wsbuf), "{\"act\":[%s]}", act.c_str());
          break;
        }
      case GETMODE:       snprintf(wsbuf, sizeof(wsbuf), "{\"pmode\":\"%s\"}", network.status == CONNECTED ? "player" : "ap"); break;
      case GETINDEX:      {
          requestOnChange(STATION, clientId); 
          requestOnChange(TITLE, clientId); 
          requestOnChange(VOLUME, clientId); 
          requestOnChange(EQUALIZER, clientId); 
          requestOnChange(BALANCE, clientId); 
          requestOnChange(BITRATE, clientId); 
          requestOnChange(MODE, clientId); 
          requestOnChange(SDINIT, clientId);
          requestOnChange(GETPLAYERMODE, clientId); 
          if (config.getMode()==PM_SDCARD) { requestOnChange(SDPOS, clientId); requestOnChange(SDLEN, clientId); requestOnChange(SDSNUFFLE, clientId); } 
          return; 
          break;
        }
      case GETSYSTEM:     {
                                  size_t n = snprintf(wsbuf, sizeof(wsbuf),
                                                      "{\"sst\":%d,\"aif\":%d,\"vu\":%d,\"vup\":%d,\"softr\":%d,\"vut\":%d,\"mdns\":\"",
                                                      config.store.smartstart != 2,
                                                      config.store.audioinfo,
                                                      config.store.vumeter,
                                                      config.store.vumeter_parallel,
                                                      config.store.softapdelay,
                                                      config.vuThreshold);
                                  if (n < sizeof(wsbuf)) n += jsonEscapeAppend(wsbuf + n, sizeof(wsbuf) - n, config.store.mdnsname);
                                  if (n < sizeof(wsbuf)) snprintf(wsbuf + n, sizeof(wsbuf) - n, "\"}");
                                  break;
                                }
      case GETSCREEN:     {
                                  size_t n = snprintf(wsbuf, sizeof(wsbuf),
                                                      "{\"flip\":%d,\"inv\":%d,\"nump\":%d,\"tsf\":%d,\"tsd\":%d,\"dspon\":%d,\"br\":%d,\"con\":%d,\"scre\":%d,\"scrt\":%d,\"scrb\":%d,\"scrpe\":%d,\"scrpt\":%d,\"scrpb\":%d,\"sstext\":\"",
                                                      config.store.flipscreen,
                                                      config.store.invertdisplay,
                                                      config.store.numplaylist,
                                                      config.store.fliptouch,
                                                      config.store.dbgtouch,
                                                      config.store.dspon,
                                                      config.store.brightness,
                                                      config.store.contrast,
                                                      config.store.screensaverEnabled,
                                                      config.store.screensaverTimeout,
                                                      config.store.screensaverBlank,
                                                      config.store.screensaverPlayingEnabled,
                                                      config.store.screensaverPlayingTimeout,
                                                      config.store.screensaverPlayingBlank);
                                  if (n < sizeof(wsbuf)) n += jsonEscapeAppend(wsbuf + n, sizeof(wsbuf) - n, config.store.screensaverText);
                                  if (n < sizeof(wsbuf)) snprintf(wsbuf + n, sizeof(wsbuf) - n, "\"}");
                                  break;
                                }
      case GETTIMEZONE:   {
                                  size_t n = snprintf(wsbuf, sizeof(wsbuf), "{\"tzh\":%d,\"tzm\":%d,\"sntp1\":\"", config.store.tzHour, config.store.tzMin);
                                  if (n < sizeof(wsbuf)) n += jsonEscapeAppend(wsbuf + n, sizeof(wsbuf) - n, config.store.sntp1);
                                  if (n < sizeof(wsbuf)) n += snprintf(wsbuf + n, sizeof(wsbuf) - n, "\",\"sntp2\":\"");
                                  if (n < sizeof(wsbuf)) n += jsonEscapeAppend(wsbuf + n, sizeof(wsbuf) - n, config.store.sntp2);
                                  if (n < sizeof(wsbuf)) snprintf(wsbuf + n, sizeof(wsbuf) - n, "\"}");
                                  break;
                                }
      case GETWEATHER:    {
                                  size_t n = snprintf(wsbuf, sizeof(wsbuf), "{\"wen\":%d,\"wlat\":\"", config.store.showweather);
                                  if (n < sizeof(wsbuf)) n += jsonEscapeAppend(wsbuf + n, sizeof(wsbuf) - n, config.store.weatherlat);
                                  if (n < sizeof(wsbuf)) n += snprintf(wsbuf + n, sizeof(wsbuf) - n, "\",\"wlon\":\"");
                                  if (n < sizeof(wsbuf)) n += jsonEscapeAppend(wsbuf + n, sizeof(wsbuf) - n, config.store.weatherlon);
                                  if (n < sizeof(wsbuf)) n += snprintf(wsbuf + n, sizeof(wsbuf) - n, "\",\"wkey\":\"");
                                  if (n < sizeof(wsbuf)) n += jsonEscapeAppend(wsbuf + n, sizeof(wsbuf) - n, config.store.weatherkey);
                                  if (n < sizeof(wsbuf)) snprintf(wsbuf + n, sizeof(wsbuf) - n, "\"}");
                                  break;
                                }
      case GETSTOCKS:     {
                                  size_t n = snprintf(wsbuf, sizeof(wsbuf), "{\"sen\":%d,\"symbols\":\"", config.store.showstocks);
                                  if (n < sizeof(wsbuf)) n += jsonEscapeAppend(wsbuf + n, sizeof(wsbuf) - n, config.store.stocksSymbols);
                                  if (n < sizeof(wsbuf)) snprintf(wsbuf + n, sizeof(wsbuf) - n, "\"}");
                                  break;
                                }
      case GETCALENDAR:   {
                                  size_t n = snprintf(wsbuf, sizeof(wsbuf), "{\"cen\":%d,\"ics\":\"", config.store.showcalendar);
                                  if (n < sizeof(wsbuf)) n += jsonEscapeAppend(wsbuf + n, sizeof(wsbuf) - n, config.store.calendarics);
                                  if (n < sizeof(wsbuf)) snprintf(wsbuf + n, sizeof(wsbuf) - n, "\"}");
                                  break;
                                }
      case GETCONTROLS:   snprintf(wsbuf, sizeof(wsbuf), "{\"vols\":%d,\"enca\":%d,\"irtl\":%d,\"skipup\":%d}", 
                                  config.store.volsteps, 
                                  config.store.encacc, 
                                  config.store.irtlp,
                                  config.store.skipPlaylistUpDown); 
                                  break;
      case DSPON:         snprintf(wsbuf, sizeof(wsbuf), "{\"dspontrue\":%d}", 1); break;
      case STATION:       requestOnChange(STATIONNAME, clientId); requestOnChange(ITEM, clientId); break;
      case STATIONNAME:   {
                                  size_t n = snprintf(wsbuf, sizeof(wsbuf), "{\"nameset\":\"");
                                  if (n < sizeof(wsbuf)) n += jsonEscapeAppend(wsbuf + n, sizeof(wsbuf) - n, config.station.name);
                                  if (n < sizeof(wsbuf)) snprintf(wsbuf + n, sizeof(wsbuf) - n, "\"}");
                                  break;
                                }
      case ITEM:          snprintf(wsbuf, sizeof(wsbuf), "{\"current\": %d}", config.lastStation()); break;
      case TITLE:         {
                                  size_t n = snprintf(wsbuf, sizeof(wsbuf), "{\"meta\":\"");
                                  if (n < sizeof(wsbuf)) n += jsonEscapeAppend(wsbuf + n, sizeof(wsbuf) - n, config.station.title);
                                  if (n < sizeof(wsbuf)) snprintf(wsbuf + n, sizeof(wsbuf) - n, "\"}");
                                  telnet.printf("##CLI.META#: %s\n> ", config.station.title);
                                  break;
                                }
      case VOLUME:        snprintf(wsbuf, sizeof(wsbuf), "{\"vol\": %d}", config.store.volume); telnet.printf("##CLI.VOL#: %d\n", config.store.volume); break;
      case NRSSI:         snprintf(wsbuf, sizeof(wsbuf), "{\"rssi\": %d}", rssi); /*rssi = 255;*/ break;
      case VULEVEL:       {
                                  uint16_t vulevel = player.get_VUlevel(255);
                                  uint8_t L = (vulevel >> 8) & 0xFF;
                                  uint8_t R = vulevel & 0xFF;
                                  snprintf(wsbuf, sizeof(wsbuf), "{\"vul\":%u,\"vur\":%u}", L, R);
                                  break;
                                }
      case SDPOS:         snprintf (wsbuf, sizeof(wsbuf), "{\"sdpos\": %d,\"sdend\": %d,\"sdtpos\": %d,\"sdtend\": %d}", 
                                  player.getFilePos(), 
                                  player.getFileSize(), 
                                  player.getAudioCurrentTime(), 
                                  player.getAudioFileDuration()); 
                                  break;
      case SDLEN:         snprintf(wsbuf, sizeof(wsbuf), "{\"sdmin\": %d,\"sdmax\": %d}", player.sd_min, player.sd_max); break;
      case SDSNUFFLE:     snprintf(wsbuf, sizeof(wsbuf), "{\"snuffle\": %d}", config.store.sdsnuffle); break;
      case BITRATE:       snprintf(wsbuf, sizeof(wsbuf), "{\"bitrate\": %d, \"format\": \"%s\"}", config.station.bitrate, getFormat(config.configFmt)); break;
      case MODE:          snprintf(wsbuf, sizeof(wsbuf), "{\"mode\": \"%s\"}", player.status() == PLAYING ? "playing" : "stopped"); break;
      case EQUALIZER:     snprintf(wsbuf, sizeof(wsbuf), "{\"bass\": %d, \"middle\": %d, \"trebble\": %d}", config.store.bass, config.store.middle, config.store.trebble); break;
      case BALANCE:       snprintf(wsbuf, sizeof(wsbuf), "{\"balance\": %d}", config.store.balance); break;
      case SDINIT:        snprintf(wsbuf, sizeof(wsbuf), "{\"sdinit\": %d}", SDC_CS!=255); break;
      case GETPLAYERMODE: snprintf(wsbuf, sizeof(wsbuf), "{\"playermode\": \"%s\"}", config.getMode()==PM_SDCARD?"modesd":"modeweb"); break;
      #ifdef USE_SD
        case CHANGEMODE:    config.changeMode(newConfigMode); return; break;
      #endif
      default:          break;
    }
    if (strlen(wsbuf) > 0) {
      if (clientId == 0) {
        if (request.type == NRSSI || request.type == VULEVEL || request.type == SDPOS || request.type == BITRATE) {
          wsTextTelemetryAll(wsbuf);
        } else {
          websocket.textAll(wsbuf);
        }
      } else {
        if (request.type == NRSSI || request.type == VULEVEL || request.type == SDPOS || request.type == BITRATE) {
          if (wsClientTelemetryEnabled(clientId)) websocket.text(clientId, wsbuf);
        } else {
          websocket.text(clientId, wsbuf);
        }
      }
  #ifdef MQTT_ROOT_TOPIC
      if (clientId == 0 && (request.type == STATION || request.type == ITEM || request.type == TITLE || request.type == MODE)) mqttPublishStatus();
      if (clientId == 0 && request.type == VOLUME) mqttPublishVolume();
  #endif
    }
  }
}

void NetServer::loop() {
  if(network.status==SDREADY) return;
  if (shouldReboot) {
    Serial.println("Ponowne uruchamianie...");
    delay(100);
    ESP.restart();
  }
  const uint32_t now = millis();
  static uint32_t lastWsCleanMs = 0;
  if ((int32_t)(now - lastWsCleanMs) >= 2000) {
    lastWsCleanMs = now;
    websocket.cleanupClients();
  }
  switch (importRequest) {
    case IMPL:    importPlaylist();  importRequest = IMDONE; break;
    case IMWIFI:  config.saveWifi(); importRequest = IMDONE; break;
    default:      break;
  }
  //if (rssi < 255) requestOnChange(NRSSI, 0);
  processQueue();
  static uint32_t lastVuMs = 0;
  if (config.store.vumeter && websocket.count() > 0 && wsAnyTelemetryEnabled()) {
    if ((int32_t)(now - lastVuMs) >= 160 && websocket.availableForWriteAll()) {
      lastVuMs = now;
      #if I2S_DOUT == 255
        player.computeVUlevel();
      #endif
      uint16_t vulevel = player.get_VUlevel(255);
      uint8_t L = (vulevel >> 8) & 0xFF;
      uint8_t R = vulevel & 0xFF;
      char buf[48];
      snprintf(buf, sizeof(buf), "{\"vul\":%u,\"vur\":%u}", L, R);
      wsTextTelemetryAll(buf);
    }
  }
}

#if IR_PIN!=255
void NetServer::irToWs(const char* protocol, uint64_t irvalue) {
  char buf[BUFLEN] = { 0 };
  snprintf(buf, sizeof(buf), "{\"ircode\": %llu, \"protocol\": \"%s\"}", irvalue, protocol);
  websocket.textAll(buf);
}
void NetServer::irValsToWs() {
  if (!irRecordEnable) return;
  char buf[BUFLEN] = { 0 };
  snprintf(buf, sizeof(buf), "{\"irvals\": [%llu, %llu, %llu]}",
           config.getIRVal(config.irindex, 0),
           config.getIRVal(config.irindex, 1),
           config.getIRVal(config.irindex, 2));
  websocket.textAll(buf);
}
#endif

void NetServer::onWsMessage(void *arg, uint8_t *data, size_t len, uint8_t clientId) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    if (len >= 256) return;
    char msg[256];
    memcpy(msg, data, len);
    msg[len] = 0;
    char cmd[65];
    char val[SCREENSAVER_TEXT_LEN];
    if (config.parseWsCommand(msg, cmd, sizeof(cmd), val, sizeof(val))) {
      if (strcmp(cmd, "telemetry") == 0) {
        wsClientSetTelemetry(clientId, atoi(val) != 0);
        return;
      }
      if (strcmp(cmd, "getmode") == 0     ) { requestOnChange(GETMODE, clientId);     return; }
      if (strcmp(cmd, "getindex") == 0    ) { requestOnChange(GETINDEX, clientId);    return; }
      if (strcmp(cmd, "getsystem") == 0   ) { requestOnChange(GETSYSTEM, clientId);   return; }
      if (strcmp(cmd, "getscreen") == 0   ) { requestOnChange(GETSCREEN, clientId);   return; }
      if (strcmp(cmd, "gettimezone") == 0 ) { requestOnChange(GETTIMEZONE, clientId); return; }
      if (strcmp(cmd, "getcontrols") == 0 ) { requestOnChange(GETCONTROLS, clientId); return; }
      if (strcmp(cmd, "getweather") == 0  ) { requestOnChange(GETWEATHER, clientId);  return; }
      if (strcmp(cmd, "getstocks") == 0   ) { requestOnChange(GETSTOCKS, clientId);   return; }
      if (strcmp(cmd, "getcalendar") == 0 ) { requestOnChange(GETCALENDAR, clientId); return; }
      if (strcmp(cmd, "getactive") == 0   ) { requestOnChange(GETACTIVE, clientId);   return; }
      if (strcmp(cmd, "newmode") == 0     ) { newConfigMode = atoi(val); requestOnChange(CHANGEMODE, 0); return; }
      if (strcmp(cmd, "smartstart") == 0) {
        uint8_t valb = atoi(val);
        uint8_t ss = valb == 1 ? 1 : 2;
        if (!player.isRunning() && ss == 1) ss = 0;
        config.setSmartStart(ss);
        return;
      }
      if (strcmp(cmd, "audioinfo") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.audioinfo, valb);
        display.putRequest(AUDIOINFO);
        return;
      }
      if (strcmp(cmd, "vumeter") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.vumeter, valb);
        display.putRequest(SHOWVUMETER);
        return;
      }
      if (strcmp(cmd, "vumeter_parallel") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.vumeter_parallel, valb);
        display.putRequest(SHOWVUMETER);
        display.putRequest(NEWMODE, CLEAR); display.putRequest(NEWMODE, PLAYER);
        return;
      }
      if (strcmp(cmd, "softap") == 0) {
        uint8_t valb = atoi(val);
        config.saveValue(&config.store.softapdelay, valb);
        return;
      }
      if (strcmp(cmd, "mdnsname") == 0) {
        config.saveValue(config.store.mdnsname, val, MDNS_LENGTH);
        return;
      }
      if (strcmp(cmd, "rebootmdns") == 0) {
        char buf[MDNS_LENGTH*2];
        if(strlen(config.store.mdnsname)>0)
          snprintf(buf, MDNS_LENGTH*2, "{\"redirect\": \"http://%s.local\"}", config.store.mdnsname);
        else
          snprintf(buf, MDNS_LENGTH*2, "{\"redirect\": \"http://%s/\"}", WiFi.localIP().toString().c_str());
        websocket.text(clientId, buf);
        delay(500);
        ESP.restart();
        return;
      }
      if (strcmp(cmd, "invertdisplay") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.invertdisplay, valb);
        display.invert();
        return;
      }
      if (strcmp(cmd, "numplaylist") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.numplaylist, valb);
        display.putRequest(NEWMODE, CLEAR); display.putRequest(NEWMODE, PLAYER);
        return;
      }
      if (strcmp(cmd, "fliptouch") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.fliptouch, valb);
        flipTS();
        return;
      }
      if (strcmp(cmd, "dbgtouch") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.dbgtouch, valb);
        return;
      }
      if (strcmp(cmd, "flipscreen") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.flipscreen, valb);
        display.flip();
        display.putRequest(NEWMODE, CLEAR); display.putRequest(NEWMODE, PLAYER);
        return;
      }
      if (strcmp(cmd, "brightness") == 0) {
        uint8_t valb = atoi(val);
        if (!config.store.dspon) requestOnChange(DSPON, 0);
        config.store.brightness = valb;
        config.setBrightness(true);
        return;
      }
      if (strcmp(cmd, "screenon") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.setDspOn(valb);
        return;
      }
      if (strcmp(cmd, "contrast") == 0) {
        uint8_t valb = atoi(val);
        config.saveValue(&config.store.contrast, valb);
        display.setContrast();
        return;
      }
      if (strcmp(cmd, "screensaverenabled") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.screensaverEnabled, valb);
        #ifndef DSP_LCD
        display.putRequest(NEWMODE, PLAYER);
        #endif
        return;
      }
      if (strcmp(cmd, "screensavertimeout") == 0) {
        uint16_t valb = atoi(val);
        valb = constrain(valb,5,65520);
        config.saveValue(&config.store.screensaverTimeout, valb);
        #ifndef DSP_LCD
        display.putRequest(NEWMODE, PLAYER);
        #endif
        return;
      }
      if (strcmp(cmd, "screensaverblank") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.screensaverBlank, valb);
        #ifndef DSP_LCD
        display.putRequest(NEWMODE, PLAYER);
        #endif
        return;
      }
      if (strcmp(cmd, "screensaverplayingenabled") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.screensaverPlayingEnabled, valb);
        #ifndef DSP_LCD
        display.putRequest(NEWMODE, PLAYER);
        #endif
        return;
      }
      if (strcmp(cmd, "screensaverplayingtimeout") == 0) {
        uint16_t valb = atoi(val);
        valb = constrain(valb,1,1080);
        config.saveValue(&config.store.screensaverPlayingTimeout, valb);
        #ifndef DSP_LCD
        display.putRequest(NEWMODE, PLAYER);
        #endif
        return;
      }
      if (strcmp(cmd, "screensaverplayingblank") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.screensaverPlayingBlank, valb);
        #ifndef DSP_LCD
        display.putRequest(NEWMODE, PLAYER);
        #endif
        return;
      }
      if (strcmp(cmd, "screensavertext") == 0) {
        config.saveValue(config.store.screensaverText, val, sizeof(config.store.screensaverText));
        display.putRequest(NEWWEATHER);
        requestOnChange(GETSCREEN, clientId);
        return;
      }
      if (strcmp(cmd, "tzh") == 0) {
        int8_t vali = atoi(val);
        config.saveValue(&config.store.tzHour, vali);
        return;
      }
      if (strcmp(cmd, "tzm") == 0) {
        int8_t vali = atoi(val);
        config.saveValue(&config.store.tzMin, vali);
        return;
      }
      if (strcmp(cmd, "sntp2") == 0) {
        config.saveValue(config.store.sntp2, val, 35, false);
        return;
      }
      if (strcmp(cmd, "sntp1") == 0) {
        strlcpy(config.store.sntp1, val, 35);
        bool tzdone = false;
        if (strlen(config.store.sntp1) > 0 && strlen(config.store.sntp2) > 0) {
          configTime(config.store.tzHour * 3600 + config.store.tzMin * 60, config.getTimezoneOffset(), config.store.sntp1, config.store.sntp2);
          tzdone = true;
        } else if (strlen(config.store.sntp1) > 0) {
          configTime(config.store.tzHour * 3600 + config.store.tzMin * 60, config.getTimezoneOffset(), config.store.sntp1);
          tzdone = true;
        }
        if (tzdone) {
          network.forceTimeSync = true;
          config.saveValue(config.store.sntp1, val, 35);
        }
        return;
      }
      if (strcmp(cmd, "volsteps") == 0) {
        uint8_t valb = atoi(val);
        config.saveValue(&config.store.volsteps, valb);
        return;
      }
      if (strcmp(cmd, "encacceleration") == 0) {
        uint16_t valb = atoi(val);
        setEncAcceleration(valb);
        return;
      }
      if (strcmp(cmd, "irtlp") == 0) {
        uint8_t valb = atoi(val);
        setIRTolerance(valb);
        return;
      }
      if (strcmp(cmd, "oneclickswitching") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.skipPlaylistUpDown, valb);
        return;
      }
      if (strcmp(cmd, "showweather") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.showweather, valb);
        network.trueWeather=false;
        network.forceWeather = true;
        display.putRequest(SHOWWEATHER);
        return;
      }
      if (strcmp(cmd, "showcalendar") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.showcalendar, valb);
        network.forceCalendar = true;
        network.requestCalendarFetchNow();
        display.putRequest(SHOWCALENDAR);
        return;
      }
      if (strcmp(cmd, "showstocks") == 0) {
        bool valb = static_cast<bool>(atoi(val));
        config.saveValue(&config.store.showstocks, valb);
        display.putRequest(SHOWSTOCKS);
        return;
      }
      if (strcmp(cmd, "lat") == 0) {
        config.saveValue(config.store.weatherlat, val, 10, false);
        return;
      }
      if (strcmp(cmd, "lon") == 0) {
        config.saveValue(config.store.weatherlon, val, 10, false);
        return;
      }
      if (strcmp(cmd, "key") == 0) {
        config.saveValue(config.store.weatherkey, val, WEATHERKEY_LENGTH);
        network.trueWeather=false;
        network.forceWeather = true;
        display.putRequest(SHOWWEATHER);
        display.putRequest(NEWMODE, CLEAR); display.putRequest(NEWMODE, PLAYER);
        return;
      }
      if (strcmp(cmd, "stockssymbols") == 0) {
        config.saveValue(config.store.stocksSymbols, val, 80);
        display.putRequest(SHOWSTOCKS);
        return;
      }
      if (strcmp(cmd, "calendarics") == 0) {
        config.saveValue(config.store.calendarics, val, sizeof(config.store.calendarics));
        network.forceCalendar = true;
        network.requestCalendarFetchNow();
        display.putRequest(SHOWCALENDAR);
        return;
      }
      /*  RESETS  */
      if (strcmp(cmd, "reset") == 0) {
        if (strcmp(val, "system") == 0) {
          config.saveValue(&config.store.smartstart, (uint8_t)2, false);
          config.saveValue(&config.store.audioinfo, false, false);
          config.saveValue(&config.store.vumeter, false, false);
          config.saveValue(&config.store.vumeter_parallel, false, false);
          config.saveValue(&config.store.softapdelay, (uint8_t)0, false);
          snprintf(config.store.mdnsname, MDNS_LENGTH, "yoradio-%x", config.getChipId());
          config.saveValue(config.store.mdnsname, config.store.mdnsname, MDNS_LENGTH, true, true);
          display.putRequest(NEWMODE, CLEAR); display.putRequest(NEWMODE, PLAYER);
          requestOnChange(GETSYSTEM, clientId);
          return;
        }
        if (strcmp(val, "screen") == 0) {
          config.saveValue(&config.store.flipscreen, false, false);
          display.flip();
          config.saveValue(&config.store.invertdisplay, false, false);
          display.invert();
          config.saveValue(&config.store.dspon, true, false);
          config.store.brightness = 100;
          config.setBrightness(false);
          config.saveValue(&config.store.contrast, (uint8_t)55, false);
          display.setContrast();
          config.saveValue(&config.store.numplaylist, false);
          config.saveValue(&config.store.screensaverEnabled, false);
          config.saveValue(&config.store.screensaverTimeout, (uint16_t)20);
          config.saveValue(&config.store.screensaverBlank, false);
          config.saveValue(&config.store.screensaverPlayingEnabled, false);
          config.saveValue(&config.store.screensaverPlayingTimeout, (uint16_t)5);
          config.saveValue(&config.store.screensaverPlayingBlank, false);
          config.saveValue(config.store.screensaverText, "", sizeof(config.store.screensaverText));
          display.putRequest(NEWMODE, CLEAR); display.putRequest(NEWMODE, PLAYER);
          requestOnChange(GETSCREEN, clientId);
          return;
        }
        if (strcmp(val, "timezone") == 0) {
          config.saveValue(&config.store.tzHour, (int8_t)3, false);
          config.saveValue(&config.store.tzMin, (int8_t)0, false);
          config.saveValue(config.store.sntp1, "pool.ntp.org", 35, false);
          config.saveValue(config.store.sntp2, "0.ru.pool.ntp.org", 35);
          configTime(config.store.tzHour * 3600 + config.store.tzMin * 60, config.getTimezoneOffset(), config.store.sntp1, config.store.sntp2);
          network.forceTimeSync = true;
          requestOnChange(GETTIMEZONE, clientId);
          return;
        }
        if (strcmp(val, "weather") == 0) {
          config.saveValue(&config.store.showweather, false, false);
          config.saveValue(config.store.weatherlat, "55.7512", 10, false);
          config.saveValue(config.store.weatherlon, "37.6184", 10, false);
          config.saveValue(config.store.weatherkey, "", WEATHERKEY_LENGTH);
          network.trueWeather=false;
          display.putRequest(NEWMODE, CLEAR); display.putRequest(NEWMODE, PLAYER);
          requestOnChange(GETWEATHER, clientId);
          return;
        }
        if (strcmp(val, "calendar") == 0) {
          config.saveValue(&config.store.showcalendar, false, false);
          config.saveValue(config.store.calendarics, "", sizeof(config.store.calendarics));
          display.putRequest(SHOWCALENDAR);
          requestOnChange(GETCALENDAR, clientId);
          return;
        }
        if (strcmp(val, "stocks") == 0) {
          config.saveValue(&config.store.showstocks, false, false);
          config.saveValue(config.store.stocksSymbols, "", 80);
          display.putRequest(SHOWSTOCKS);
          requestOnChange(GETSTOCKS, clientId);
          return;
        }
        if (strcmp(val, "controls") == 0) {
          config.saveValue(&config.store.volsteps, (uint8_t)1, false);
          config.saveValue(&config.store.fliptouch, false, false);
          config.saveValue(&config.store.dbgtouch, false, false);
          config.saveValue(&config.store.skipPlaylistUpDown, false);
          
          setEncAcceleration(200);
          setIRTolerance(40);
          requestOnChange(GETCONTROLS, clientId);
          return;
        }
      } /*  EOF RESETS  */
      if (strcmp(cmd, "volume") == 0) {
        uint8_t v = atoi(val);
        player.setVol(v);
      }
      if (strcmp(cmd, "sdpos") == 0) {
        //return;
        if (config.getMode()==PM_SDCARD){
          config.sdResumePos = 0;
          if(!player.isRunning()){
            player.setResumeFilePos(atoi(val)-player.sd_min);
            player.sendCommand({PR_PLAY, config.store.lastSdStation});
          }else{
            player.setFilePos(atoi(val)-player.sd_min);
          }
        }
        return;
      }
      if (strcmp(cmd, "snuffle") == 0) {
        config.setSnuffle(strcmp(val, "true") == 0);
        return;
      }
      if (strcmp(cmd, "balance") == 0) {
        int8_t valb = atoi(val);
        player.setBalance(valb);
        config.setBalance(valb);
        netserver.requestOnChange(BALANCE, 0);
        return;
      }
      if (strcmp(cmd, "treble") == 0) {
        int8_t valb = atoi(val);
        player.setTone(config.store.bass, config.store.middle, valb);
        config.setTone(config.store.bass, config.store.middle, valb);
        netserver.requestOnChange(EQUALIZER, 0);
        return;
      }
      if (strcmp(cmd, "middle") == 0) {
        int8_t valb = atoi(val);
        player.setTone(config.store.bass, valb, config.store.trebble);
        config.setTone(config.store.bass, valb, config.store.trebble);
        netserver.requestOnChange(EQUALIZER, 0);
        return;
      }
      if (strcmp(cmd, "bass") == 0) {
        int8_t valb = atoi(val);
        player.setTone(valb, config.store.middle, config.store.trebble);
        config.setTone(valb, config.store.middle, config.store.trebble);
        netserver.requestOnChange(EQUALIZER, 0);
        return;
      }
      if (strcmp(cmd, "submitplaylist") == 0) {
        return;
      }
      if (strcmp(cmd, "submitplaylistdone") == 0) {
#ifdef MQTT_ROOT_TOPIC
        //mqttPublishPlaylist();
        mqttplaylistticker.attach(5, mqttplaylistSend);
#endif
        if (player.isRunning()) {
          player.sendCommand({PR_PLAY, -config.lastStation()});
        }
        return;
      }
#if IR_PIN!=255
      if (strcmp(cmd, "irbtn") == 0) {
        config.irindex = atoi(val);
        irRecordEnable = (config.irindex >= 0) && (config.irindex < IR_SLOTS_TOTAL);
        config.irchck = 0;
        irValsToWs();
        if (config.irindex < 0) config.saveIR();
      }
      if (strcmp(cmd, "chkid") == 0) {
        config.irchck = atoi(val);
      }
      if (strcmp(cmd, "irclr") == 0) {
        uint8_t cl = atoi(val);
        if (config.irindex >= 0 && config.irindex < IR_SLOTS_TOTAL) {
          config.setIRVal(config.irindex, cl, 0);
          config.saveIR();
          irValsToWs();
        }
      }
#endif
    }
  }
}

void NetServer::getPlaylist(uint8_t clientId) {
  char buf[160] = {0};
  snprintf(buf, sizeof(buf), "{\"file\": \"http://%s%s\"}", WiFi.localIP().toString().c_str(), PLAYLIST_PATH);
  if (clientId == 0) { websocket.textAll(buf); } else { websocket.text(clientId, buf); }
}

int NetServer::_readPlaylistLine(File &file, char * line, size_t size){
  int bytesRead = file.readBytesUntil('\n', line, size);
  if(bytesRead>0){
    line[bytesRead] = 0;
    if(line[bytesRead-1]=='\r') line[bytesRead-1]=0;
  }
  return bytesRead;
}

bool NetServer::importPlaylist() {
  if(config.getMode()==PM_SDCARD) return false;
  File tempfile = SPIFFS.open(TMP_PATH, "r");
  if (!tempfile) {
    return false;
  }
  char sName[BUFLEN], sUrl[BUFLEN], linePl[BUFLEN*3];;
  int sOvol;
  uint16_t yieldCtr = 0;
  _readPlaylistLine(tempfile, linePl, sizeof(linePl)-1);
  if (config.parseCSV(linePl, sName, sUrl, sOvol)) {
    tempfile.close();
    SPIFFS.rename(TMP_PATH, PLAYLIST_PATH);
    requestOnChange(PLAYLISTSAVED, 0);
    return true;
  }
  if (config.parseJSON(linePl, sName, sUrl, sOvol)) {
    File playlistfile = SPIFFS.open(PLAYLIST_PATH, "w");
    snprintf(linePl, sizeof(linePl)-1, "%s\t%s\t%d", sName, sUrl, 0);
    playlistfile.println(linePl);
    while (tempfile.available()) {
      _readPlaylistLine(tempfile, linePl, sizeof(linePl)-1);
      if (config.parseJSON(linePl, sName, sUrl, sOvol)) {
        snprintf(linePl, sizeof(linePl)-1, "%s\t%s\t%d", sName, sUrl, 0);
        playlistfile.println(linePl);
      }
      if((++yieldCtr % 25) == 0) delay(0);
    }
    playlistfile.flush();
    playlistfile.close();
    tempfile.close();
    SPIFFS.remove(TMP_PATH);
    requestOnChange(PLAYLISTSAVED, 0);
    return true;
  }
  tempfile.close();
  SPIFFS.remove(TMP_PATH);
  return false;
}

void NetServer::requestOnChange(requestType_e request, uint8_t clientId) {
  if(nsQueue==NULL) return;
  nsRequestParams_t nsrequest;
  nsrequest.type = request;
  nsrequest.clientId = clientId;
  xQueueSend(nsQueue, &nsrequest, NSQ_SEND_DELAY);
}

void NetServer::resetQueue(){
  if(nsQueue!=NULL) xQueueReset(nsQueue);
}

String processor(const String& var) { // %Templates%
  if (var == "ACTION") return (network.status == CONNECTED && !config.emptyFS)?"webboard":"";
  if (var == "UPLOADWIFI") return (network.status == CONNECTED)?" hidden":"";
  if (var == "VERSION") return YOVERSION;
  return String();
}

int freeSpace;
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  if (!index) {
    if(filename!="tempwifi.csv"){
      if(SPIFFS.exists(PLAYLIST_PATH)) SPIFFS.remove(PLAYLIST_PATH);
      if(SPIFFS.exists(INDEX_PATH)) SPIFFS.remove(INDEX_PATH);
      if(SPIFFS.exists(PLAYLIST_SD_PATH)) SPIFFS.remove(PLAYLIST_SD_PATH);
      if(SPIFFS.exists(INDEX_SD_PATH)) SPIFFS.remove(INDEX_SD_PATH);
    }
    freeSpace = (float)SPIFFS.totalBytes()/100*68-SPIFFS.usedBytes();
    request->_tempFile = SPIFFS.open(TMP_PATH , "w");
  }
  if (len) {
    if(freeSpace>index+len){
      request->_tempFile.write(data, len);
    }
  }
  if (final) {
    request->_tempFile.close();
  }
}

void handleUploadWeb(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  DBGVB("File: %s, size:%u bytes, index: %u, final: %s\n", filename.c_str(), len, index, final?"true":"false");
  if (!index) {
    String spath = "/www/";
    if(filename=="playlist.csv" || filename=="wifi.csv") spath = "/data/";
    request->_tempFile = SPIFFS.open(spath + filename , "w");
  }
  if (len) {
    request->_tempFile.write(data, len);
  }
  if (final) {
    request->_tempFile.close();
    if(filename=="playlist.csv") config.indexPlaylist();
  }
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      wsClientOnConnect(client->id());
      if (config.store.audioinfo) Serial.printf("[WEBSOCKET] klient #%u połączono z %s\n", client->id(), client->remoteIP().toString().c_str());
      break;
    case WS_EVT_DISCONNECT:
      wsClientOnDisconnect(client->id());
      if (config.store.audioinfo) Serial.printf("[WEBSOCKET] klient #%u rozłączono\n", client->id());
      break;
    case WS_EVT_DATA: netserver.onWsMessage(arg, data, len, client->id()); break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

void handleHTTPArgs(AsyncWebServerRequest * request) {
  if (request->method() == HTTP_GET) {
    DBGVB("[%s] client ip=%s request of %s", __func__, request->client()->remoteIP().toString().c_str(), request->url().c_str());
    if (strcmp(request->url().c_str(), PLAYLIST_PATH) == 0 || 
        strcmp(request->url().c_str(), SSIDS_PATH) == 0 || 
        strcmp(request->url().c_str(), INDEX_PATH) == 0 || 
        strcmp(request->url().c_str(), TMP_PATH) == 0 || 
        strcmp(request->url().c_str(), PLAYLIST_SD_PATH) == 0 || 
        strcmp(request->url().c_str(), INDEX_SD_PATH) == 0) {
#ifdef SSIDS_PATH
      if (strcmp(request->url().c_str(), SSIDS_PATH) == 0 && network.status == CONNECTED) {
        request->send(404);
        return;
      }
#endif
#ifdef MQTT_ROOT_TOPIC
      if (strcmp(request->url().c_str(), PLAYLIST_PATH) == 0) while (mqttplaylistblock) vTaskDelay(5);
#endif
      if(strcmp(request->url().c_str(), PLAYLIST_PATH) == 0 && config.getMode()==PM_SDCARD){
         netserver.chunkedHtmlPage("application/octet-stream", request, PLAYLIST_SD_PATH, false);
      }else{
        netserver.chunkedHtmlPage("application/octet-stream", request, request->url().c_str(), false);
      }
      return;
    }
    if (strcmp(request->url().c_str(), "/") == 0 && request->params() == 0) {
      netserver.chunkedHtmlPage(String(), request, network.status == CONNECTED ? "/www/index.html" : "/www/settings.html");
      return;
    }
    if (strcmp(request->url().c_str(), "/update") == 0 || strcmp(request->url().c_str(), "/settings") == 0 || strcmp(request->url().c_str(), "/ir") == 0) {
      char buf[40] = { 0 };
      snprintf(buf, sizeof(buf), "/www%s.html", request->url().c_str());
      netserver.chunkedHtmlPage(String(), request, buf);
      return;
    }
  }
  if (network.status == CONNECTED) {
    bool commandFound=false;
    if (request->hasArg("start")) { player.sendCommand({PR_PLAY, config.lastStation()}); commandFound=true; }
    if (request->hasArg("stop")) { player.sendCommand({PR_STOP, 0}); commandFound=true; }
    if (request->hasArg("toggle")) { player.toggle(); commandFound=true; }
    if (request->hasArg("prev")) { player.prev(); commandFound=true; }
    if (request->hasArg("next")) { player.next(); commandFound=true; }
    if (request->hasArg("volm")) { player.stepVol(false); commandFound=true; }
    if (request->hasArg("volp")) { player.stepVol(true); commandFound=true; }
    #ifdef USE_SD
    if (request->hasArg("mode")) {
      AsyncWebParameter* p = request->getParam("mode");
      int mm = atoi(p->value().c_str());
      if(mm>2) mm=0;
      if(mm==2)
        config.changeMode();
      else
        config.changeMode(mm);
      commandFound=true;
    }
    #endif
    if (request->hasArg("reset")) { request->redirect("/"); request->send(200); config.reset(); return; }
    if (request->hasArg("trebble") && request->hasArg("middle") && request->hasArg("bass")) {
      AsyncWebParameter* pt = request->getParam("trebble", request->method() == HTTP_POST);
      AsyncWebParameter* pm = request->getParam("middle", request->method() == HTTP_POST);
      AsyncWebParameter* pb = request->getParam("bass", request->method() == HTTP_POST);
      int t = atoi(pt->value().c_str());
      int m = atoi(pm->value().c_str());
      int b = atoi(pb->value().c_str());
      player.setTone(b, m, t);
      config.setTone(b, m, t);
      netserver.requestOnChange(EQUALIZER, 0);
      commandFound=true;
    }
    if (request->hasArg("ballance")) {
      AsyncWebParameter* p = request->getParam("ballance", request->method() == HTTP_POST);
      int b = atoi(p->value().c_str());
      player.setBalance(b);
      config.setBalance(b);
      netserver.requestOnChange(BALANCE, 0);
      commandFound=true;
    }
    if (request->hasArg("playstation") || request->hasArg("play")) {
      AsyncWebParameter* p = request->getParam(request->hasArg("playstation") ? "playstation" : "play", request->method() == HTTP_POST);
      int id = atoi(p->value().c_str());
      if (id < 1) id = 1;
      if (id > config.store.countStation) id = config.store.countStation;
      //config.sdResumePos = 0;
      player.sendCommand({PR_PLAY, id});
      commandFound=true;
      DBGVB("[%s] play=%d", __func__, id);
    }
    if (request->hasArg("vol")) {
      AsyncWebParameter* p = request->getParam("vol", request->method() == HTTP_POST);
      int v = atoi(p->value().c_str());
      if (v < 0) v = 0;
      if (v > 254) v = 254;
      config.store.volume = v;
      player.setVol(v);
      commandFound=true;
      DBGVB("[%s] vol=%d", __func__, v);
    }
    if (request->hasArg("dspon")) {
      AsyncWebParameter* p = request->getParam("dspon", request->method() == HTTP_POST);
      int d = atoi(p->value().c_str());
      config.setDspOn(d!=0);
      commandFound=true;
    }
    if (request->hasArg("dim")) {
      AsyncWebParameter* p = request->getParam("dim", request->method() == HTTP_POST);
      int d = atoi(p->value().c_str());
      if (d < 0) d = 0;
      if (d > 100) d = 100;
      config.store.brightness = (uint8_t)d;
      config.setBrightness(true);
      commandFound=true;
    }
    if (request->hasArg("sleep")) {
      AsyncWebParameter* sfor = request->getParam("sleep", request->method() == HTTP_POST);
      int sford = atoi(sfor->value().c_str());
      int safterd = 0;
      if(request->hasArg("after")){
        AsyncWebParameter* safter = request->getParam("after", request->method() == HTTP_POST);
        safterd = atoi(safter->value().c_str());
      }
      if(sford > 0 && safterd >= 0){
        request->send(200);
        config.sleepForAfter(sford, safterd);
        commandFound=true;
      }
    }
    if (request->hasArg("clearspiffs")) {
      if(config.spiffsCleanup()){
        config.saveValue(&config.store.play_mode, static_cast<uint8_t>(PM_WEB));
        request->redirect("/");
        ESP.restart();
      }else{
        request->send(200);
      }
      return;
    }
    if (request->params() > 0) {
      request->send(commandFound?200:404);
      return;
    }
  } else {
    if (request->params() > 0) {
      request->send(404);
      return;
    }
  }
}
