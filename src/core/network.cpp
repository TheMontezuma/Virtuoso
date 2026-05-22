#include "network.h"
#include "display.h"
#include "options.h"
#include "config.h"
#include "telnet.h"
#include "netserver.h"
#include "player.h"
#include "mqtt.h"
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <Client.h>

#ifndef WIFI_ATTEMPTS
  #define WIFI_ATTEMPTS  16
#endif

MyNetwork network;

TaskHandle_t syncTaskHandle;
//TaskHandle_t reconnectTaskHandle;

bool getWeather(char *wstr);
bool getCalendar(char *l1, char *l2);
void doSync(void * pvParameters);

static int lastSundayOfMonth(int year, int month) {
  static const int daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  int dim = daysInMonth[month - 1];
  if(month == 2){
    bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    if(leap) dim = 29;
  }
  static const int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
  int y = year;
  int m = month;
  if(m < 3) y -= 1;
  int dow = (y + y/4 - y/100 + y/400 + t[m-1] + dim) % 7;
  return dim - dow;
}

static bool isEUDstUtc(time_t nowUtc){
  struct tm utc;
  gmtime_r(&nowUtc, &utc);
  int year = utc.tm_year + 1900;
  int mon = utc.tm_mon + 1;
  int mday = utc.tm_mday;
  int hour = utc.tm_hour;
  int startDay = lastSundayOfMonth(year, 3);
  int endDay = lastSundayOfMonth(year, 10);
  if(mon < 3 || mon > 10) return false;
  if(mon > 3 && mon < 10) return true;
  if(mon == 3){
    if(mday > startDay) return true;
    if(mday < startDay) return false;
    return hour >= 1;
  }
  if(mon == 10){
    if(mday < endDay) return true;
    if(mday > endDay) return false;
    return hour < 1;
  }
  return false;
}

bool MyNetwork::applyTimezone(bool forceSync){
  if(strlen(config.store.sntp1) == 0) return false;
  uint16_t prev = config.getTimezoneOffset();
  uint16_t dst = prev;
  if(config.getDstAutoEU()){
    time_t now = time(nullptr);
    if(now > 1577836800){
      dst = isEUDstUtc(now) ? 3600 : 0;
    }
  }
  if(dst != prev){
    config.setTimezoneOffset(dst);
  }
  if(strlen(config.store.sntp2) > 0){
    configTime(config.store.tzHour * 3600 + config.store.tzMin * 60, dst, config.store.sntp1, config.store.sntp2);
  }else{
    configTime(config.store.tzHour * 3600 + config.store.tzMin * 60, dst, config.store.sntp1);
  }
  if(forceSync) this->forceTimeSync = true;
  return dst != prev;
}

void ticks() {
  if(!display.ready()) return; //waiting for SD is ready
  pm.on_ticker();
  static const uint32_t weatherSyncInterval=86400;
  //static const uint16_t weatherSyncIntervalFail=10;
#if RTCSUPPORTED
  static const uint32_t timeSyncInterval=86400;
  static uint32_t timeSyncTicks = 0;
#else
  static const uint16_t timeSyncInterval=3600;
  static uint16_t timeSyncTicks = 0;
#endif
  static uint32_t weatherSyncTicks = 0;
  static uint16_t calendarSyncTicks = 0;
  static const uint16_t calendarSyncInterval = 900;
  static bool divrssi;
  timeSyncTicks++;
  weatherSyncTicks++;
  calendarSyncTicks++;
  divrssi = !divrssi;
  if(network.status == CONNECTED){
    if (syncTaskHandle == NULL) {
      if (network.forceTimeSync || (!player.isRunning() && (network.forceWeather || network.forceCalendar))) {
        xTaskCreatePinnedToCore(doSync, "doSync", 1024 * 4, NULL, 0, &syncTaskHandle, 0);
      }
    }
    if(timeSyncTicks >= timeSyncInterval){
      timeSyncTicks=0;
      network.forceTimeSync = true;
    }
    if(weatherSyncTicks >= weatherSyncInterval){
      weatherSyncTicks=0;
      network.forceWeather = true;
    }
    if(calendarSyncTicks >= calendarSyncInterval){
      calendarSyncTicks = 0;
      network.forceCalendar = true;
      if (!player.isRunning() && syncTaskHandle == NULL) {
        xTaskCreatePinnedToCore(doSync, "doSync", 1024 * 4, NULL, 0, &syncTaskHandle, 0);
      }
    }
  }
#ifndef DSP_LCD
  if (config.getMode() == PM_BLUETOOTH) {
    config.screensaverTicks = 0;
    config.screensaverPlayingTicks = 0;
  } else if(config.store.screensaverEnabled && display.mode()==PLAYER && !player.isRunning()){
    config.screensaverTicks++;
    if(config.screensaverTicks > config.store.screensaverTimeout+SCREENSAVERSTARTUPDELAY){
      if(config.store.screensaverBlank){
        display.putRequest(NEWMODE, SCREENBLANK);
      }else{
        display.putRequest(NEWMODE, SCREENSAVER);
      }
    }
  }
  if(config.store.screensaverPlayingEnabled && config.getMode() != PM_BLUETOOTH && display.mode()==PLAYER && player.isRunning()){
    config.screensaverPlayingTicks++;
    if(config.screensaverPlayingTicks > config.store.screensaverPlayingTimeout*60+SCREENSAVERSTARTUPDELAY){
      if(config.store.screensaverPlayingBlank){
        display.putRequest(NEWMODE, SCREENBLANK);
      }else{
        display.putRequest(NEWMODE, SCREENSAVER);
      }
    }
  }
#endif
#if RTCSUPPORTED
  if(config.isRTCFound()){
    rtc.getTime(&network.timeinfo);
    mktime(&network.timeinfo);
    display.putRequest(CLOCK);
  }
#else
  if(network.timeinfo.tm_year>100 || network.status == SDREADY) {
    network.timeinfo.tm_sec++;
    mktime(&network.timeinfo);
    display.putRequest(CLOCK);
  }
#endif
  if(player.isRunning() && config.getMode()==PM_SDCARD) netserver.requestOnChange(SDPOS, 0);
  if(divrssi) {
    if(network.status == CONNECTED){
      netserver.setRSSI(WiFi.RSSI());
      netserver.requestOnChange(NRSSI, 0);
      display.putRequest(DSPRSSI, netserver.getRSSI());
    }
#ifdef USE_SD
    if(display.mode()!=SDCHANGE) player.sendCommand({PR_CHECKSD, 0});
#endif
    player.sendCommand({PR_VUTONUS, 0});
  }
}

void MyNetwork::WiFiReconnected(WiFiEvent_t event, WiFiEventInfo_t info){
  network.beginReconnect = false;
  player.lockOutput = false;
  delay(100);
  display.putRequest(NEWMODE, PLAYER);
  if(config.getMode()==PM_SDCARD) {
    network.status=CONNECTED;
    display.putRequest(NEWIP, 0);
  }else{
    display.putRequest(NEWMODE, PLAYER);
    if (network.lostPlaying) player.sendCommand({PR_PLAY, config.lastStation()});
  }
  #ifdef MQTT_ROOT_TOPIC
    connectToMqtt();
  #endif
}

void MyNetwork::WiFiLostConnection(WiFiEvent_t event, WiFiEventInfo_t info){
  if(!network.beginReconnect){
    Serial.printf("Utracono połączenie, ponowne łączenie z %s...\n", config.ssids[config.store.lastSSID-1].ssid);
    if(config.getMode()==PM_SDCARD) {
      network.status=SDREADY;
      display.putRequest(NEWIP, 0);
    }else{
      network.lostPlaying = player.isRunning();
      if (network.lostPlaying) { player.lockOutput = true; player.sendCommand({PR_STOP, 0}); }
      display.putRequest(NEWMODE, LOST);
    }
  }
  network.beginReconnect = true;
  WiFi.reconnect();
}

bool MyNetwork::wifiBegin(bool silent){
  uint8_t ls = (config.store.lastSSID == 0 || config.store.lastSSID > config.ssidsCount) ? 0 : config.store.lastSSID - 1;
  uint8_t startedls = ls;
  uint8_t errcnt = 0;
  WiFi.mode(WIFI_STA);
  /*
  char buf[MDNS_LENGTH];
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  if(strlen(config.store.mdnsname)>0){
    WiFi.setHostname(config.store.mdnsname);
  }else{
    snprintf(buf, MDNS_LENGTH, "yoradio-%x", config.getChipId());
    WiFi.setHostname(buf);
  }
  */
  while (true) {
    if(!silent){
      Serial.printf("##[BOOT]#\tPróba połączenia z %s\n", config.ssids[ls].ssid);
      Serial.print("##[BOOT]#\t");
      display.putRequest(BOOTSTRING, ls);
    }
    WiFi.begin(config.ssids[ls].ssid, config.ssids[ls].password);
    while (WiFi.status() != WL_CONNECTED) {
      if(!silent) Serial.print(".");
      delay(500);
      if(REAL_LEDBUILTIN!=255 && !silent) digitalWrite(REAL_LEDBUILTIN, !digitalRead(REAL_LEDBUILTIN));
      errcnt++;
      if (errcnt > WIFI_ATTEMPTS) {
        errcnt = 0;
        ls++;
        if (ls > config.ssidsCount - 1) ls = 0;
        if(!silent) Serial.println();
        break;
      }
    }
    if (WiFi.status() != WL_CONNECTED && ls == startedls) {
      return false; break;
    }
    if (WiFi.status() == WL_CONNECTED) {
      config.setLastSSID(ls + 1);
      return true; break;
    }
  }
  return false;
}

void searchWiFi(void * pvParameters){
  if(!network.wifiBegin(true)){
    delay(10000);
    xTaskCreatePinnedToCore(searchWiFi, "searchWiFi", 1024 * 4, NULL, 0, NULL, 0);
  }else{
    network.status = CONNECTED;
    netserver.begin(true);
    telnet.begin(true);
    network.setWifiParams();
    display.putRequest(NEWIP, 0);
  }
  vTaskDelete( NULL );
}

#define DBGAP false

void MyNetwork::begin() {
  BOOTLOG("network.begin");
  config.initNetwork();
  ctimer.detach();
  forceTimeSync = forceWeather = true;
  if (config.ssidsCount == 0 || DBGAP) {
    raiseSoftAP();
    return;
  }
  if(config.getMode()!=PM_SDCARD){
    if(!wifiBegin()){
      raiseSoftAP();
      Serial.println("##[BOOT]#\tdone");
      return;
    }
    Serial.println(".");
    status = CONNECTED;
    setWifiParams();
  }else{
    status = SDREADY;
    xTaskCreatePinnedToCore(searchWiFi, "searchWiFi", 1024 * 4, NULL, 0, NULL, 0);
  }
  
  Serial.println("##[BOOT]#\tdone");
  if(REAL_LEDBUILTIN!=255) digitalWrite(REAL_LEDBUILTIN, LOW);
  
#if RTCSUPPORTED
  if(config.isRTCFound()){
    rtc.getTime(&network.timeinfo);
    mktime(&network.timeinfo);
    display.putRequest(CLOCK);
  }
#endif
  ctimer.attach(1, ticks);
  if (network_on_connect) network_on_connect();
  pm.on_connect();
}

void MyNetwork::setWifiParams(){
  WiFi.setSleep(false);
  WiFi.onEvent(WiFiReconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(WiFiLostConnection, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  weatherBuf=NULL;
  trueWeather = false;
  #if (DSP_MODEL!=DSP_DUMMY || defined(USE_NEXTION)) && !defined(HIDE_WEATHER)
    weatherBuf = (char *) malloc(sizeof(char) * WEATHER_STRING_L);
    memset(weatherBuf, 0, WEATHER_STRING_L);
  #endif
  calendar1Buf = NULL;
  calendar2Buf = NULL;
  calendar1Buf = (char *) malloc(sizeof(char) * CALENDAR_STRING_L);
  calendar2Buf = (char *) malloc(sizeof(char) * CALENDAR_STRING_L);
  if (calendar1Buf) memset(calendar1Buf, 0, CALENDAR_STRING_L);
  if (calendar2Buf) memset(calendar2Buf, 0, CALENDAR_STRING_L);
  applyTimezone(false);
}

void MyNetwork::requestTimeSync(bool withTelnetOutput, uint8_t clientId) {
  if (withTelnetOutput) {
    char timeStringBuff[50];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%dT%H:%M:%S", &timeinfo);
    if (config.store.tzHour < 0) {
      telnet.printf(clientId, "##SYS.DATE#: %s%03d:%02d\n> ", timeStringBuff, config.store.tzHour, config.store.tzMin);
    } else {
      telnet.printf(clientId, "##SYS.DATE#: %s+%02d:%02d\n> ", timeStringBuff, config.store.tzHour, config.store.tzMin);
    }
  }
}

void rebootTime() {
  ESP.restart();
}

void MyNetwork::raiseSoftAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid, apPassword);
  Serial.println("##[BOOT]#");
  BOOTLOG("************************************************");
  BOOTLOG("Running in AP mode");
  BOOTLOG("Connect to AP %s", apSsid);
  BOOTLOG("and go to http:/192.168.4.1/ to configure");
  BOOTLOG("************************************************");
  status = SOFT_AP;
  if(config.store.softapdelay>0)
    rtimer.once(config.store.softapdelay*60, rebootTime);
}

void MyNetwork::requestWeatherSync(){
  display.putRequest(NEWWEATHER);
}

void MyNetwork::requestWeatherFetchNow(){
  forceWeather = true;
  if (status != CONNECTED) return;
  if (syncTaskHandle != NULL) return;
  xTaskCreatePinnedToCore(doSync, "doSync", 1024 * 4, NULL, 0, &syncTaskHandle, 0);
}

void MyNetwork::requestCalendarSync(){
  display.putRequest(NEWCALENDAR);
}

void MyNetwork::requestCalendarFetchNow(){
  forceCalendar = true;
  xTaskCreatePinnedToCore(doSync, "doSync", 1024 * 4, NULL, 0, &syncTaskHandle, 0);
}


void doSync( void * pvParameters ) {
  static uint8_t tsFailCnt = 0;
  //static uint8_t wsFailCnt = 0;
  if(network.forceTimeSync){
    network.forceTimeSync = false;
    if(getLocalTime(&network.timeinfo)){
      tsFailCnt = 0;
      network.forceTimeSync = false;
      mktime(&network.timeinfo);
      display.putRequest(CLOCK);
      network.requestTimeSync(true);
        bool tzChanged = network.applyTimezone(false);
        if(tzChanged){
          if(getLocalTime(&network.timeinfo)){
            mktime(&network.timeinfo);
            display.putRequest(CLOCK);
            network.requestTimeSync(true);
          }
        }
      #if RTCSUPPORTED
        if (config.isRTCFound()) rtc.setTime(&network.timeinfo);
      #endif
    }else{
      if(tsFailCnt<4){
        network.forceTimeSync = true;
        tsFailCnt++;
      }else{
        network.forceTimeSync = false;
        tsFailCnt=0;
      }
    }
  }
  if(network.weatherBuf && (strlen(config.store.weatherkey)!=0 && config.store.showweather) && network.forceWeather){
    if (!player.isRunning()) {
      network.forceWeather = false;
      network.trueWeather=getWeather(network.weatherBuf);
    }
  }
  if(network.calendar1Buf && network.calendar2Buf && config.store.showcalendar && strlen(config.store.calendarics)>0 && network.forceCalendar){
    if (!player.isRunning()) {
      network.forceCalendar = false;
      bool ok = getCalendar(network.calendar1Buf, network.calendar2Buf);
      if (ok) network.requestCalendarSync();
    }
  }
  syncTaskHandle = NULL;
  vTaskDelete( NULL );
}

bool getWeather(char *wstr) {
#if (DSP_MODEL!=DSP_DUMMY || defined(USE_NEXTION)) && !defined(HIDE_WEATHER)
  WiFiClient client;
  const char* host  = "api.openweathermap.org";
  
  if (!client.connect(host, 80)) {
    Serial.println("##WEATHER###: brak połączenia");
    return false;
  }
  char httpget[250] = {0};
  snprintf(httpget, sizeof(httpget), "GET /data/2.5/weather?lat=%s&lon=%s&units=%s&lang=%s&appid=%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", config.store.weatherlat, config.store.weatherlon, weatherUnits, weatherLang, config.store.weatherkey, host);
  client.print(httpget);
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 2000UL) {
      Serial.println("##WEATHER###: przekroczono czas oczekiwania (client available)!");
      client.stop();
      return false;
    }
    delay(1);
  }
  timeout = millis();
  String line = "";
  if (client.connected()) {
    while (client.available())
    {
      line = client.readStringUntil('\n');
      if (strstr(line.c_str(), "\"temp\"") != NULL) {
        client.stop();
        break;
      }
      if ((millis() - timeout) > 500)
      {
        client.stop();
        Serial.println("##WEATHER###: przekroczono czas odczytu klienta!");
        return false;
      }
    }
  }
  if (strstr(line.c_str(), "\"temp\"") == NULL) {
    Serial.println("##WEATHER###: nie znaleziono danych pogodowych!");
    return false;
  }
  char *tmpe;
  char *tmps;
  char *tmpc;
  const char* cursor = line.c_str();
  char desc[120], temp[20], hum[20], press[20], icon[5];

  tmps = strstr(cursor, "\"description\":\"");
  if (tmps == NULL) { Serial.println("##WEATHER###: nie znaleziono opisu!"); return false;}
  tmps += 15;
  tmpe = strstr(tmps, "\",\"");
  if (tmpe == NULL) { Serial.println("##WEATHER###: nie znaleziono opisu!"); return false;}
  strlcpy(desc, tmps, tmpe - tmps + 1);
  cursor = tmpe + 2;
  
  // "ясно","icon":"01d"}],
  tmps = strstr(cursor, "\"icon\":\"");
  if (tmps == NULL) { Serial.println("##WEATHER###: nie znaleziono ikony!"); return false;}
  tmps += 8;
  tmpe = strstr(tmps, "\"}");
  if (tmpe == NULL) { Serial.println("##WEATHER###: nie znaleziono ikony!"); return false;}
  strlcpy(icon, tmps, tmpe - tmps + 1);
  cursor = tmpe + 2;
  
  tmps = strstr(cursor, "\"temp\":");
  if (tmps == NULL) { Serial.println("##WEATHER###: nie znaleziono temperatury!"); return false;}
  tmps += 7;
  tmpe = strstr(tmps, ",\"");
  if (tmpe == NULL) { Serial.println("##WEATHER###: nie znaleziono temperatury!"); return false;}
  strlcpy(temp, tmps, tmpe - tmps + 1);
  cursor = tmpe + 1;
  float tempf = atof(temp);

  tmps = strstr(cursor, "\"feels_like\":");
  if (tmps == NULL) { Serial.println("##WEATHER###: nie znaleziono temperatury odczuwalnej!"); return false;}
  tmps += 13;
  tmpe = strstr(tmps, ",\"");
  if (tmpe == NULL) { Serial.println("##WEATHER###: nie znaleziono temperatury odczuwalnej!"); return false;}
  strlcpy(temp, tmps, tmpe - tmps + 1);
  cursor = tmpe + 2;
  float tempfl = atof(temp); (void)tempfl;

  tmps = strstr(cursor, "\"pressure\":");
  if (tmps == NULL) { Serial.println("##WEATHER###: nie znaleziono ciśnienia!"); return false;}
  tmps += 11;
  tmpe = strstr(tmps, ",\"");
  if (tmpe == NULL) { Serial.println("##WEATHER###: nie znaleziono ciśnienia!"); return false;}
  strlcpy(press, tmps, tmpe - tmps + 1);
  cursor = tmpe + 2;
  int pressi = (float)atoi(press);
  
  tmps = strstr(cursor, "humidity\":");
  if (tmps == NULL) { Serial.println("##WEATHER###: nie znaleziono wilgotności!"); return false;}
  tmps += 10;
  tmpe = strstr(tmps, ",\"");
  tmpc = strstr(tmps, "}");
  if (tmpe == NULL) { Serial.println("##WEATHER###: nie znaleziono wilgotności!"); return false;}
  strlcpy(hum, tmps, tmpe - tmps + (tmpc>tmpe?1:0));
  
  tmps = strstr(cursor, "\"grnd_level\":");
  bool grnd_level_pr = (tmps != NULL);
  if(grnd_level_pr){
    tmps += 13;
    tmpe = strstr(tmps, ",\"");
    if (tmpe == NULL) { Serial.println("##WEATHER###: nie znaleziono ciśnienia gruntu!"); return false;}
    strlcpy(press, tmps, tmpe - tmps + 1);
    cursor = tmpe + 2;
    pressi = (float)atoi(press);
  }
  
  tmps = strstr(cursor, "\"speed\":");
  if (tmps == NULL) { Serial.println("##WEATHER###: nie znaleziono prędkości wiatru!"); return false;}
  tmps += 8;
  tmpe = strstr(tmps, ",\"");
  if (tmpe == NULL) { Serial.println("##WEATHER###: nie znaleziono prędkości wiatru!"); return false;}
  strlcpy(temp, tmps, tmpe - tmps + 1);
  cursor = tmpe + 1;
  float wind_speed = atof(temp); (void)wind_speed;
  
  tmps = strstr(cursor, "\"deg\":");
  if (tmps == NULL) { Serial.println("##WEATHER###: nie znaleziono kierunku wiatru!"); return false;}
  tmps += 6;
  tmpe = strstr(tmps, ",\"");
  if (tmpe == NULL) { Serial.println("##WEATHER###: nie znaleziono kierunku wiatru!"); return false;}
  strlcpy(temp, tmps, tmpe - tmps + 1);
  cursor = tmpe + 1;
  int wind_deg = atof(temp)/22.5;
  if(wind_deg<0) wind_deg = 16+wind_deg;
  
  
  #ifdef USE_NEXTION
    nextion.putcmdf("press_txt.txt=\"%dmm\"", pressi);
    nextion.putcmdf("hum_txt.txt=\"%d%%\"", atoi(hum));
    char cmd[30];
    snprintf(cmd, sizeof(cmd)-1,"temp_txt.txt=\"%.1f\"", tempf);
    nextion.putcmd(cmd);
    int iconofset;
    if(strstr(icon,"01")!=NULL)      iconofset = 0;
    else if(strstr(icon,"02")!=NULL) iconofset = 1;
    else if(strstr(icon,"03")!=NULL) iconofset = 2;
    else if(strstr(icon,"04")!=NULL) iconofset = 3;
    else if(strstr(icon,"09")!=NULL) iconofset = 4;
    else if(strstr(icon,"10")!=NULL) iconofset = 5;
    else if(strstr(icon,"11")!=NULL) iconofset = 6;
    else if(strstr(icon,"13")!=NULL) iconofset = 7;
    else if(strstr(icon,"50")!=NULL) iconofset = 8;
    else                             iconofset = 9;
    nextion.putcmd("cond_img.pic", 50+iconofset);
    nextion.weatherVisible(1);
  #endif
  
  Serial.printf("##WEATHER###: opis: %s, temp:%.1f C, ciśnienie:%dmmHg, wilgotność:%s%%\n", desc, tempf, pressi, hum);
  #ifdef WEATHER_FMT_SHORT
  snprintf(wstr, WEATHER_STRING_L, weatherFmt, tempf, pressi, hum);
  #else
    #if EXT_WEATHER
      snprintf(wstr, WEATHER_STRING_L, weatherFmt, desc, tempf, tempfl, pressi, hum, wind_speed, wind[wind_deg]);
    #else
      snprintf(wstr, WEATHER_STRING_L, weatherFmt, desc, tempf, pressi, hum);
    #endif
  #endif
  network.requestWeatherSync();
  return true;
#endif // if (DSP_MODEL!=DSP_DUMMY || defined(USE_NEXTION)) && !defined(HIDE_WEATHER)
  return false;
}

static bool parseDtstart(const char* s, char* outDate, size_t outSize, char* outTime, size_t outTimeSize) {
  const char* p = s;
  int y=0,m=0,d=0,hh=0,mm=0;
  if (strlen(p) < 8) return false;
  y = (p[0]-'0')*1000 + (p[1]-'0')*100 + (p[2]-'0')*10 + (p[3]-'0');
  m = (p[4]-'0')*10 + (p[5]-'0');
  d = (p[6]-'0')*10 + (p[7]-'0');
  snprintf(outDate, outSize, "%04d-%02d-%02d", y, m, d);
  const char* t = strchr(p, 'T');
  if (!t) {
    outTime[0] = 0;
    return true;
  }
  if (strlen(t) < 7) {
    outTime[0] = 0;
    return true;
  }
  hh = (t[1]-'0')*10 + (t[2]-'0');
  mm = (t[3]-'0')*10 + (t[4]-'0');
  snprintf(outTime, outTimeSize, "%02d:%02d", hh, mm);
  return true;
}

static bool readIcsAndFill(Client &client, char *l1, char *l2) {
  unsigned long timeout = millis();
  while (client.connected() && !client.available()) {
    if (millis() - timeout > 5000UL) { client.stop(); return false; }
    delay(10);
  }
  bool inHeaders = true;
  char lineBuf[256];
  char dtstart[32] = {0};
  char dtend[32] = {0};
  char summary[160] = {0};
  int found = 0;
  bool inEvent = false;
  bool isAllDay = false;
  while (client.available()) {
    String ln = client.readStringUntil('\n');
    if (inHeaders) {
      if (ln == "\r" || ln.length() == 1) { inHeaders = false; }
      continue;
    }
    size_t n = ln.length();
    if (n >= sizeof(lineBuf)) n = sizeof(lineBuf)-1;
    memcpy(lineBuf, ln.c_str(), n);
    lineBuf[n] = 0;
    if (strstr(lineBuf, "BEGIN:VEVENT")) { inEvent = true; dtstart[0]=0; dtend[0]=0; summary[0]=0; isAllDay=false; continue; }
    if (!inEvent) continue;
    if (strstr(lineBuf, "DTSTART") == lineBuf) {
      const char* p = strchr(lineBuf, ':');
      if (p) {
        strlcpy(dtstart, p+1, sizeof(dtstart));
        isAllDay = (strstr(lineBuf, "VALUE=DATE") != NULL) || (strchr(p+1, 'T') == NULL);
      }
      continue;
    }
    if (strstr(lineBuf, "DTEND") == lineBuf) {
      const char* p = strchr(lineBuf, ':');
      if (p) strlcpy(dtend, p+1, sizeof(dtend));
      continue;
    }
    if (strstr(lineBuf, "SUMMARY:") == lineBuf) {
      const char* p = lineBuf + 8;
      strlcpy(summary, p, sizeof(summary));
      char* r = strrchr(summary, '\r'); if (r) *r=0;
      continue;
    }
    if (strstr(lineBuf, "END:VEVENT")) {
      if (dtstart[0] && summary[0]) {
        char d1[16]={0}, t1[8]={0}, d2[16]={0}, t2[8]={0}, buf[CALENDAR_STRING_L]={0};
        parseDtstart(dtstart, d1, sizeof d1, t1, sizeof t1);
        if (dtend[0]) parseDtstart(dtend, d2, sizeof d2, t2, sizeof t2);
        if (isAllDay) {
          if (d2[0] && strcmp(d1, d2) != 0) {
            snprintf(buf, sizeof(buf), "%s - %s • %s", d1, d2, summary);
          } else {
            snprintf(buf, sizeof(buf), "%s • %s", d1, summary);
          }
        } else {
          if (t1[0]) {
            snprintf(buf, sizeof(buf), "%s %s • %s", d1, t1, summary);
          } else {
            snprintf(buf, sizeof(buf), "%s • %s", d1, summary);
          }
        }
        if (found == 0) { strlcpy(l1, buf, CALENDAR_STRING_L); found++; }
        else if (found == 1) { strlcpy(l2, buf, CALENDAR_STRING_L); found++; }
      }
      inEvent = false;
      if (found >= 2) break;
    }
  }
  if (found == 0) { l1[0]=0; l2[0]=0; }
  if (found == 1) { l2[0]=0; }
  return found > 0;
}

bool getCalendar(char *l1, char *l2) {
  if (!l1 || !l2) return false;
  if (strlen(config.store.calendarics) == 0) return false;
  const char* url = config.store.calendarics;
  bool is_https = true;
  const char* hostStart = url;
  if (strncmp(url, "https://", 8) == 0) { is_https = true; hostStart = url + 8; }
  else if (strncmp(url, "http://", 7) == 0) { is_https = false; hostStart = url + 7; }
  const char* pathStart = strchr(hostStart, '/');
  if (!pathStart) return false;
  char host[128] = {0};
  char path[256] = {0};
  size_t hl = pathStart - hostStart;
  if (hl >= sizeof(host)) return false;
  strncpy(host, hostStart, hl);
  strlcpy(path, pathStart, sizeof(path));

  if (is_https) {
    WiFiClientSecure client;
    client.setInsecure();
    if (!client.connect(host, 443)) return false;
    client.printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
    bool ok = readIcsAndFill(client, l1, l2);
    client.stop();
    return ok;
  } else {
    WiFiClient client;
    if (!client.connect(host, 80)) return false;
    client.printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
    bool ok = readIcsAndFill(client, l1, l2);
    client.stop();
    return ok;
  }
}
