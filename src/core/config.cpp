#include "config.h"

//#include <SPIFFS.h>
#include "display.h"
#include "player.h"
#include "network.h"
#include "netserver.h"
#if defined(ESP32)
  #include <esp_system.h>
#endif

#ifndef BT_RELAY_ACTIVE_LOW
#define BT_RELAY_ACTIVE_LOW true
#endif

#ifndef BT_MUTE_PIN
#define BT_MUTE_PIN 255
#endif
#ifndef BT_MUTE_ACTIVE_LOW
#define BT_MUTE_ACTIVE_LOW true
#endif
#ifndef BT_MUTE_SWITCH_DELAY_MS
#define BT_MUTE_SWITCH_DELAY_MS 120
#endif

static inline void _setBtMute(bool muteOn) {
#if BT_MUTE_PIN != 255
  uint8_t v = muteOn ? (BT_MUTE_ACTIVE_LOW ? LOW : HIGH) : (BT_MUTE_ACTIVE_LOW ? HIGH : LOW);
  digitalWrite(BT_MUTE_PIN, v);
#else
  (void)muteOn;
#endif
}

static inline void _initBtMutePin() {
#if BT_MUTE_PIN != 255
  _setBtMute(true);
  pinMode(BT_MUTE_PIN, OUTPUT);
#endif
}
void Config::changeMode(int newmode){
  bool pir = player.isRunning();
  uint8_t oldMode = store.play_mode; // Zapamiętaj poprzedni tryb
  if(newmode<0){
    store.play_mode++;
    if(store.play_mode > MAX_PLAY_MODE) store.play_mode=0;
    
    #ifndef USE_SD
    if(store.play_mode == PM_SDCARD) store.play_mode = PM_BLUETOOTH;
    #else
    if(SDC_CS==255 && store.play_mode == PM_SDCARD) store.play_mode = PM_BLUETOOTH;
    #endif

    #if BT_RELAY_PIN == 255
    if(store.play_mode == PM_BLUETOOTH) store.play_mode = PM_WEB;
    #endif
  }else{
    store.play_mode=(uint8_t)newmode;
  }
  
  #ifdef USE_SD
  if(getMode()==PM_SDCARD) {
     sdResumePos = player.getFilePos();
  }
  if(store.play_mode == PM_SDCARD && (network.status==SOFT_AP || display.mode()==LOST)){
      saveValue(&store.play_mode, static_cast<uint8_t>(PM_SDCARD));
      delay(50);
      ESP.restart();
  }
  if(store.play_mode == PM_SDCARD && !sdman.ready) {
    if(!sdman.start()){
      Serial.println("##[ERROR]#\tNie znaleziono karty SD");
      netserver.requestOnChange(GETPLAYERMODE, 0);
      sdman.stop();
      changeMode(-1); 
      return;
    }
  }
  #endif

  if(store.play_mode == PM_BLUETOOTH){
      _setBtMute(true);
      #if BT_RELAY_PIN != 255
      digitalWrite(BT_RELAY_PIN, BT_RELAY_ACTIVE_LOW ? LOW : HIGH);
      #endif
      if (BT_MUTE_SWITCH_DELAY_MS > 0) delay(BT_MUTE_SWITCH_DELAY_MS);
      _setBtMute(false);
      player.sendCommand({PR_STOP, 0});
      
      setStation("Bluetooth");
      setTitle("");
      station.bitrate = 0;
      setBitrateFormat(BF_UNCNOWN);

      display.putRequest(NEWMODE, PLAYER);
      display.putRequest(PSTOP);
      display.putRequest(NEWSTATION);
      display.putRequest(NEWTITLE);
      display.putRequest(DBITRATE);
      saveValue(&store.play_mode, store.play_mode, true, true);
      netserver.requestOnChange(GETPLAYERMODE, 0);
      netserver.requestOnChange(BITRATE, 0);
      return;
  }else{
      _setBtMute(true);
      #if BT_RELAY_PIN != 255
      digitalWrite(BT_RELAY_PIN, BT_RELAY_ACTIVE_LOW ? HIGH : LOW);
      #endif
  }

  saveValue(&store.play_mode, store.play_mode, true, true);

  #ifdef USE_SD
  _SDplaylistFS = getMode()==PM_SDCARD?&sdman:(true?&SPIFFS:_SDplaylistFS);
  #endif

  if(pir) player.sendCommand({PR_STOP, 0});

  #ifdef USE_SD
  if(getMode()==PM_SDCARD){
    display.putRequest(NEWMODE, SDCHANGE);
    while(display.mode()!=SDCHANGE)
      delay(10);
    delay(50);
  }
  #endif
  
  if(getMode()==PM_WEB) {
    #ifdef USE_SD
    if(network.status==SDREADY) ESP.restart();
    sdman.stop();
    #endif
  }

  if(!_bootDone) return;
  
  initPlaylistMode();
  
  // Jeśli wracamy z Bluetooth, wymuś odtwarzanie (bo w trybie BT player.isRunning() zwraca false)
  if (oldMode == PM_BLUETOOTH && getMode() != PM_BLUETOOTH) {
      pir = true;
  }
  
  if (pir && getMode()!=PM_BLUETOOTH) player.sendCommand({PR_PLAY, getMode()==PM_WEB?store.lastStation:store.lastSdStation});
  
  netserver.resetQueue();
  netserver.requestOnChange(GETPLAYERMODE, 0);
  netserver.requestOnChange(GETMODE, 0);
  display.resetQueue();
  display.putRequest(NEWMODE, PLAYER);
  display.putRequest(NEWSTATION);
}

#ifdef USE_SD
#include "sdmanager.h"
#endif
#include <cstddef>

Config config;

void u8fix(char *src){
  if (!src) return;
  const size_t len = strlen(src);
  if (len == 0) return;
  const uint8_t last = static_cast<uint8_t>(src[len - 1]);
  if (last >= 0xC2) src[len - 1] = '\0';
}

bool Config::_isFSempty() {
  const char* reqiredFiles[] = {"dragpl.js.gz","elogo.png","elogo84.png","index.html",
                                "ir.css.gz","ir.html","ir.js.gz","script.js.gz",
                                "settings.css.gz","settings.html","style.css.gz","update.html"};
  const uint8_t reqiredFilesSize = 12;
  char fullpath[28];
  for (uint8_t i=0; i<reqiredFilesSize; i++){
    snprintf(fullpath, sizeof(fullpath), "/www/%s", reqiredFiles[i]);
    if(!SPIFFS.exists(fullpath)) return true;
  }
  return false;
}

void Config::init() {
  EEPROM.begin(EEPROM_SIZE);
  sdResumePos = 0;
  screensaverTicks = 0;
  screensaverPlayingTicks = 0;
  isScreensaver = false;
  clockOnly = false;
  clockOnlyOffsetX = 0;
  bootInfo();
#if RTCSUPPORTED
  _rtcFound = false;
  BOOTLOG("RTC begin(SDA=%d,SCL=%d)", RTC_SDA, RTC_SCL);
  if(rtc.init()){
    BOOTLOG("gotowe");
    _rtcFound = true;
  }else{
    BOOTLOG("[ERROR] - Nie znaleziono RTC");
  }
#endif
  emptyFS = true;
#if IR_PIN!=255
    irindex=-1;
    memset(irValsExtra, 0, sizeof(irValsExtra));
#endif
#if defined(SD_SPIPINS) || SD_HSPI
  #if !defined(SD_SPIPINS)
    SDSPI.begin();
  #else
    SDSPI.begin(SD_SPIPINS); // SCK, MISO, MOSI
  #endif
#endif
  eepromRead(EEPROM_START, store);
  
  if (store.config_set != 4262) {
    setDefaults();
  }
  if(store.version>CONFIG_VERSION) {
    saveValue(&store.version, (uint16_t)CONFIG_VERSION, true, true);
  }
  while(store.version!=CONFIG_VERSION) _setupVersion();
  BOOTLOG("CONFIG_VERSION\t%d", store.version);
  store.play_mode = store.play_mode & 0b11;
  if(store.play_mode>1) store.play_mode=PM_WEB;
  _initHW();
  if (!SPIFFS.begin(true)) {
    Serial.println("##[ERROR]#\tNie udało się zamontować SPIFFS");
    return;
  }
  BOOTLOG("SPIFFS zamontowany");
  emptyFS = _isFSempty();
  if(emptyFS) BOOTLOG("SPIFFS jest pusty!");
  ssidsCount = 0;
  #ifdef USE_SD
  _SDplaylistFS = getMode()==PM_SDCARD?&sdman:(true?&SPIFFS:_SDplaylistFS);
  #else
  _SDplaylistFS = &SPIFFS;
  #endif
  _bootDone=false;
}

void Config::_setupVersion(){
  uint16_t currentVersion = store.version;
  switch(currentVersion){
    case 1:
      saveValue(&store.screensaverEnabled, false);
      saveValue(&store.screensaverTimeout, (uint16_t)20);
      break;
    case 2:
      char buf[MDNS_LENGTH];
      snprintf(buf, MDNS_LENGTH, "yoradio-%x", getChipId());
      saveValue(store.mdnsname, buf, MDNS_LENGTH);
      saveValue(&store.skipPlaylistUpDown, false);
      break;
    case 3:
      saveValue(&store.screensaverBlank, false);
      saveValue(&store.screensaverPlayingEnabled, false);
      saveValue(&store.screensaverPlayingTimeout, (uint16_t)5);
      saveValue(&store.screensaverPlayingBlank, false);
      break;
    case 4:
      saveValue(&store.showcalendar, false);
      saveValue(store.calendarics, "", sizeof(store.calendarics));
      break;
    case 5:
      saveValue(&store.vumeter_parallel, false);
      break;
    case 6:
      saveValue(store.screensaverText, "", sizeof(store.screensaverText));
      break;
    case 7:
      setDstAutoEU(true);
      break;
    default:
      break;
  }
  currentVersion++;
  saveValue(&store.version, currentVersion);
}

#ifdef USE_SD


void Config::initSDPlaylist() {
  store.countStation = 0;
  bool doIndex = !sdman.exists(INDEX_SD_PATH);
  if(doIndex) sdman.indexSDPlaylist();
  if (SDPLFS()->exists(INDEX_SD_PATH)) {
    File index = SDPLFS()->open(INDEX_SD_PATH, "r");
    store.countStation = index.size() / 4;
    if(doIndex){
      lastStation(_randomStation());
      sdResumePos = 0;
    }
    index.close();
    saveValue(&store.countStation, store.countStation, true, true);
  }
}

#endif //#ifdef USE_SD

bool Config::spiffsCleanup(){
  bool ret = (SPIFFS.exists(PLAYLIST_SD_PATH)) || (SPIFFS.exists(INDEX_SD_PATH)) || (SPIFFS.exists(INDEX_PATH));
  if(SPIFFS.exists(PLAYLIST_SD_PATH)) SPIFFS.remove(PLAYLIST_SD_PATH);
  if(SPIFFS.exists(INDEX_SD_PATH)) SPIFFS.remove(INDEX_SD_PATH);
  if(SPIFFS.exists(INDEX_PATH)) SPIFFS.remove(INDEX_PATH);
  return ret;
}

void Config::initPlaylistMode(){
  uint16_t _lastStation = 0;
  #ifdef USE_SD
    if(getMode()==PM_SDCARD){
      if(!sdman.start()){
        store.play_mode=PM_WEB;
        Serial.println("Nie udało się zamontować SD");
        changeMode(PM_WEB);
        _lastStation = store.lastStation;
      }else{
        if(_bootDone) Serial.println("SD zamontowane"); else BOOTLOG("SD zamontowane");
          if(_bootDone) Serial.println("Czekam na indeksowanie karty SD..."); else BOOTLOG("Czekam na indeksowanie karty SD...");
          initSDPlaylist();
          if(_bootDone) Serial.println("gotowe"); else BOOTLOG("gotowe");
          _lastStation = store.lastSdStation;
          if(_lastStation>store.countStation && store.countStation>0){
            _lastStation=1;
          }
          if(_lastStation==0) {
            _lastStation = _randomStation();
          }
      }
    }else{
      Serial.println("gotowe");
      _lastStation = store.lastStation;
    }
  #else //ifdef USE_SD
    store.play_mode=PM_WEB;
    _lastStation = store.lastStation;
  #endif
  if(getMode()==PM_WEB && !emptyFS) initPlaylist();
  log_i("%d" ,_lastStation);
  if (_lastStation == 0 && store.countStation > 0) {
    _lastStation = getMode()==PM_WEB?1:_randomStation();
  }
  lastStation(_lastStation);
  saveValue(&store.play_mode, store.play_mode, true, true);
  _bootDone = true;
  loadStation(_lastStation);
}

void Config::_initHW(){
  loadTheme();
  #if IR_PIN!=255
  eepromRead(EEPROM_START_IR, ircodes);
  if(ircodes.ir_set!=4224){
    ircodes.ir_set=4224;
    memset(ircodes.irVals, 0, sizeof(ircodes.irVals));
  } else {
    for (uint8_t t = 0; t < IR_SLOTS_EEPROM; t++) {
      for (uint8_t j = 0; j < 3; j++) {
        if (ircodes.irVals[t][j] == UINT64_MAX) ircodes.irVals[t][j] = 0;
      }
    }
  }
  loadIRExtra();
  #endif
  #if BRIGHTNESS_PIN!=255
    pinMode(BRIGHTNESS_PIN, OUTPUT);
    setBrightness(false);
  #endif
  _initBtMutePin();
  #if BT_RELAY_PIN!=255
    uint8_t v = (getMode() == PM_BLUETOOTH) ? (BT_RELAY_ACTIVE_LOW ? LOW : HIGH) : (BT_RELAY_ACTIVE_LOW ? HIGH : LOW);
    digitalWrite(BT_RELAY_PIN, v);
    pinMode(BT_RELAY_PIN, OUTPUT);
  #endif
}

uint16_t Config::color565(uint8_t r, uint8_t g, uint8_t b)
{
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

void Config::loadTheme(){
  theme.background    = color565(COLOR_BACKGROUND);
  theme.meta          = color565(COLOR_STATION_NAME);
  theme.metabg        = color565(COLOR_STATION_BG);
  theme.metafill      = color565(COLOR_STATION_FILL);
  theme.title1        = color565(COLOR_SNG_TITLE_1);
  theme.title2        = color565(COLOR_SNG_TITLE_2);
  theme.digit         = color565(COLOR_DIGITS);
  theme.div           = color565(COLOR_DIVIDER);
  theme.weather       = color565(COLOR_WEATHER);
  theme.vumax         = color565(COLOR_VU_MAX);
  theme.vumin         = color565(COLOR_VU_MIN);
  theme.clock         = color565(COLOR_CLOCK);
  theme.clockbg       = color565(COLOR_CLOCK_BG);
  theme.seconds       = color565(COLOR_SECONDS);
  theme.dow           = color565(COLOR_DAY_OF_W);
  theme.date          = color565(COLOR_DATE);
  theme.heap          = color565(COLOR_HEAP);
  theme.buffer        = color565(COLOR_BUFFER);
  theme.ip            = color565(COLOR_IP);
  theme.vol           = color565(COLOR_VOLUME_VALUE);
  theme.rssi          = color565(COLOR_RSSI);
  theme.bitrate       = color565(COLOR_BITRATE);
  theme.volbarout     = color565(COLOR_VOLBAR_OUT);
  theme.volbarin      = color565(COLOR_VOLBAR_IN);
  theme.plcurrent     = color565(COLOR_PL_CURRENT);
  theme.plcurrentbg   = color565(COLOR_PL_CURRENT_BG);
  theme.plcurrentfill = color565(COLOR_PL_CURRENT_FILL);
  theme.playlist[0]   = color565(COLOR_PLAYLIST_0);
  theme.playlist[1]   = color565(COLOR_PLAYLIST_1);
  theme.playlist[2]   = color565(COLOR_PLAYLIST_2);
  theme.playlist[3]   = color565(COLOR_PLAYLIST_3);
  theme.playlist[4]   = color565(COLOR_PLAYLIST_4);
  #include "../displays/tools/tftinverttitle.h"
}

template <class T> int Config::eepromWrite(int ee, const T& value) {
  const uint8_t* p = (const uint8_t*)(const void*)&value;
  int i;
  for (i = 0; i < sizeof(value); i++)
    EEPROM.write(ee++, *p++);
  EEPROM.commit();
  return i;
}

template <class T> int Config::eepromRead(int ee, T& value) {
  uint8_t* p = (uint8_t*)(void*)&value;
  int i;
  for (i = 0; i < sizeof(value); i++)
    *p++ = EEPROM.read(ee++);
  return i;
}

void Config::reset(){
  setDefaults();
  delay(500);
  ESP.restart();
}

void Config::setDefaults() {
  store.config_set = 4262;
  store.version = CONFIG_VERSION;
  store.volume = 12;
  store.balance = 0;
  store.trebble = 0;
  store.middle = 0;
  store.bass = 0;
  store.lastStation = 0;
  store.countStation = 0;
  store.lastSSID = 0;
  store.audioinfo = true;
  store.smartstart = 2;
  store.tzHour = 2;
  store.tzMin = 0;
  store.timezoneOffset = 0;

  store.vumeter=true;
  store.softapdelay=0;
  store.flipscreen=false;
  store.invertdisplay=false;
  store.numplaylist=true;
  store.fliptouch=false;
  store.dbgtouch=false;
  store.dspon=true;
  store.brightness=100;
  store.contrast=55;
  strlcpy(store.sntp1,"tempus1.gum.gov.pl", 35);
  strlcpy(store.sntp2,"tempus2.gum.gov.pl", 35);
  store.showweather=true;
  strlcpy(store.weatherlat,"52.13", 10);
  strlcpy(store.weatherlon,"21.00", 10);
  strlcpy(store.weatherkey,"", WEATHERKEY_LENGTH);
  store._reserved = 1;
  store.lastSdStation = 0;
  store.sdsnuffle = false;
  store.volsteps = 1;
  store.encacc = 200;
  store.play_mode = 0;
  store.irtlp = 35;
  store.btnpullup = true;
  store.btnlongpress = 200;
  store.btnclickticks = 300;
  store.btnpressticks = 500;
  store.encpullup = false;
  store.enchalf = false;
  store.enc2pullup = false;
  store.enc2half = false;
  store.forcemono = false;
  store.i2sinternal = false;
  store.rotate90 = false;
  store.screensaverEnabled = true;
  store.screensaverTimeout = 10;
  store.screensaverBlank = false;
  snprintf(store.mdnsname, MDNS_LENGTH, "Virtuoso-%x", getChipId());
  store.skipPlaylistUpDown = false;
  store.screensaverPlayingEnabled = false;
  store.screensaverPlayingTimeout = 5;//------bylo 5 -------
  store.screensaverPlayingBlank = false;
  strlcpy(store.screensaverText, "", sizeof(store.screensaverText));
  store.showcalendar = false;
  strlcpy(store.calendarics, "", sizeof(store.calendarics));
  store.vumeter_parallel = false;
  eepromWrite(EEPROM_START, store);
}

void Config::setTimezone(int8_t tzh, int8_t tzm) {
  saveValue(&store.tzHour, tzh, false);
  saveValue(&store.tzMin, tzm);
}

void Config::setTimezoneOffset(uint16_t tzo) {
  saveValue(&store.timezoneOffset, tzo);
}

uint16_t Config::getTimezoneOffset() {
  return store.timezoneOffset;
}

bool Config::getDstAutoEU(){
  return (store._reserved & 0x0001) != 0;
}

void Config::setDstAutoEU(bool enabled){
  uint16_t r = store._reserved;
  if(enabled) r |= 0x0001;
  else r &= (uint16_t)~0x0001;
  saveValue(&store._reserved, r);
}

void Config::setSnuffle(bool sn){
  saveValue(&store.sdsnuffle, sn);
  if(store.sdsnuffle) player.next();
}

#if IR_PIN!=255
void Config::saveIR(){
  eepromWrite(EEPROM_START_IR, ircodes);
  saveIRExtra();
}
#endif

#if IR_PIN!=255
void Config::loadIRExtra() {
  Preferences prefs;
  if (!prefs.begin("virtuoso-ir", true)) return;
  size_t got = prefs.getBytes("ir_extra", irValsExtra, sizeof(irValsExtra));
  prefs.end();
  if (got != sizeof(irValsExtra)) {
    memset(irValsExtra, 0, sizeof(irValsExtra));
  }
}

void Config::saveIRExtra() {
  Preferences prefs;
  if (!prefs.begin("virtuoso-ir", false)) return;
  prefs.putBytes("ir_extra", irValsExtra, sizeof(irValsExtra));
  prefs.end();
}

uint64_t Config::getIRVal(uint8_t slot, uint8_t alt) const {
  if (alt >= 3) return 0;
  if (slot < IR_SLOTS_EEPROM) return ircodes.irVals[slot][alt];
  uint8_t ext = slot - IR_SLOTS_EEPROM;
  if (ext < IR_SLOTS_EXTRA) return irValsExtra[ext][alt];
  return 0;
}

void Config::setIRVal(uint8_t slot, uint8_t alt, uint64_t value) {
  if (alt >= 3) return;
  if (slot < IR_SLOTS_EEPROM) {
    ircodes.irVals[slot][alt] = value;
    return;
  }
  uint8_t ext = slot - IR_SLOTS_EEPROM;
  if (ext < IR_SLOTS_EXTRA) irValsExtra[ext][alt] = value;
}
#endif

void Config::saveVolume(){
  saveValue(&store.volume, store.volume, true, true);
}

uint8_t Config::setVolume(uint8_t val) {
  store.volume = val;
  display.putRequest(DRAWVOL);
  netserver.requestOnChange(VOLUME, 0);
  return store.volume;
}

void Config::setTone(int8_t bass, int8_t middle, int8_t trebble) {
  saveValue(&store.bass, bass, false);
  saveValue(&store.middle, middle, false);
  saveValue(&store.trebble, trebble);
}

void Config::setSmartStart(uint8_t ss) {
  saveValue(&store.smartstart, ss);
}

void Config::setBalance(int8_t balance) {
  saveValue(&store.balance, balance);
}

uint8_t Config::setLastStation(uint16_t val) {
  lastStation(val);
  return store.lastStation;
}

uint8_t Config::setCountStation(uint16_t val) {
  saveValue(&store.countStation, val);
  return store.countStation;
}

uint8_t Config::setLastSSID(uint8_t val) {
  saveValue(&store.lastSSID, val);
  return store.lastSSID;
}

void Config::setTitle(const char* title) {
  vuThreshold = 0;
  char inbuf[BUFLEN] = {0};
  if (title) strlcpy(inbuf, title, BUFLEN);
  size_t _tl = strlen(inbuf);
  char _lb[BUFLEN] = {0};
  for (size_t i = 0; i < _tl; i++) {
    char ch = inbuf[i];
    _lb[i] = (ch >= 'A' && ch <= 'Z') ? ch + 32 : ch;
  }
  _lb[_tl] = 0;
  if (strstr(_lb, "connect") != NULL || strstr(_lb, "conect") != NULL) {
    inbuf[0] = 0;
  }
  memset(config.station.title, 0, BUFLEN);
  strlcpy(config.station.title, inbuf, BUFLEN);
  u8fix(config.station.title);
  netserver.requestOnChange(TITLE, 0);
  display.putRequest(NEWTITLE);
}

void Config::setStation(const char* station) {
  memset(config.station.name, 0, BUFLEN);
  strlcpy(config.station.name, station, BUFLEN);
  u8fix(config.station.name);
}

void Config::indexPlaylist() {
  File playlist = SPIFFS.open(PLAYLIST_PATH, "r");
  if (!playlist) {
    return;
  }
  char sName[BUFLEN], sUrl[BUFLEN];
  int sOvol;
  File index = SPIFFS.open(INDEX_PATH, "w");
  uint16_t yieldCtr = 0;
  while (playlist.available()) {
    uint32_t pos = playlist.position();
    if (parseCSV(playlist.readStringUntil('\n').c_str(), sName, sUrl, sOvol)) {
      index.write((uint8_t *) &pos, 4);
    }
    if((++yieldCtr % 25) == 0) delay(0);
  }
  index.close();
  playlist.close();
}

void Config::initPlaylist() {
  store.countStation = 0;
  if (!SPIFFS.exists(INDEX_PATH)) indexPlaylist();

  if (SPIFFS.exists(INDEX_PATH)) {
    File index = SPIFFS.open(INDEX_PATH, "r");
    store.countStation = index.size() / 4;
    index.close();
    saveValue(&store.countStation, store.countStation, true, true);
  }
}

void Config::loadStation(uint16_t ls) {
  char sName[BUFLEN], sUrl[BUFLEN];
  int sOvol;
  if (store.countStation == 0) {
    memset(station.url, 0, BUFLEN);
    memset(station.name, 0, BUFLEN);
    strncpy(station.name, "ёRadio", BUFLEN);
    station.ovol = 0;
    return;
  }
  if (ls > store.countStation) {
    ls = 1;
  }
  File playlist = SDPLFS()->open(REAL_PLAYL, "r");
  File index = SDPLFS()->open(REAL_INDEX, "r");
  index.seek((ls - 1) * 4, SeekSet);
  uint32_t pos;
  index.readBytes((char *) &pos, 4);
  index.close();
  playlist.seek(pos, SeekSet);
  if (parseCSV(playlist.readStringUntil('\n').c_str(), sName, sUrl, sOvol)) {
    memset(station.url, 0, BUFLEN);
    memset(station.name, 0, BUFLEN);
    strncpy(station.name, sName, BUFLEN);
    strncpy(station.url, sUrl, BUFLEN);
    station.ovol = sOvol;
    setLastStation(ls);
  }
  playlist.close();
}

char * Config::stationByNum(uint16_t num){
  File playlist = SDPLFS()->open(REAL_PLAYL, "r");
  File index = SDPLFS()->open(REAL_INDEX, "r");
  index.seek((num - 1) * 4, SeekSet);
  uint32_t pos;
  memset(_stationBuf, 0, BUFLEN/2);
  index.readBytes((char *) &pos, 4);
  index.close();
  playlist.seek(pos, SeekSet);
  strncpy(_stationBuf, playlist.readStringUntil('\t').c_str(), BUFLEN/2);
  playlist.close();
  return _stationBuf;
}

uint8_t Config::fillPlMenu(int from, uint8_t count, bool fromNextion) {
  int     ls      = from;
  uint8_t c       = 0;
  bool    finded  = false;
  if (store.countStation == 0) {
    return 0;
  }
  File playlist = SDPLFS()->open(REAL_PLAYL, "r");
  File index = SDPLFS()->open(REAL_INDEX, "r");
  while (true) {
    if (ls < 1) {
      ls++;
      if(!fromNextion) display.printPLitem(c, "");
  #ifdef USE_NEXTION
    if(fromNextion) nextion.printPLitem(c, "");
  #endif
      c++;
      continue;
    }
    if (!finded) {
      index.seek((ls - 1) * 4, SeekSet);
      uint32_t pos;
      index.readBytes((char *) &pos, 4);
      finded = true;
      index.close();
      playlist.seek(pos, SeekSet);
    }
    bool pla = true;
    while (pla) {
      pla = playlist.available();
      String stationName = playlist.readStringUntil('\n');
      stationName = stationName.substring(0, stationName.indexOf('\t'));
      if(config.store.numplaylist && stationName.length()>0) stationName = String(from+c)+" "+stationName;
      if(!fromNextion) display.printPLitem(c, stationName.c_str());
      #ifdef USE_NEXTION
        if(fromNextion) nextion.printPLitem(c, stationName.c_str());
      #endif
      c++;
      if (c >= count) break;
    }
    break;
  }
  playlist.close();
  return c;
}

bool Config::parseCSV(const char* line, char* name, char* url, int &ovol) {
  char *tmpe;
  const char* cursor = line;
  char buf[5];
  tmpe = strstr(cursor, "\t");
  if (tmpe == NULL) return false;
  if (!name) return false;
  memset(name, 0, BUFLEN);
  const size_t nameLen = static_cast<size_t>(tmpe - cursor);
  const size_t nameCopy = (nameLen < (BUFLEN - 1)) ? nameLen : (BUFLEN - 1);
  memcpy(name, cursor, nameCopy);
  name[nameCopy] = 0;
  if (strlen(name) == 0) return false;
  cursor = tmpe + 1;
  tmpe = strstr(cursor, "\t");
  if (tmpe == NULL) return false;
  if (!url) return false;
  memset(url, 0, BUFLEN);
  const size_t urlLen = static_cast<size_t>(tmpe - cursor);
  const size_t urlCopy = (urlLen < (BUFLEN - 1)) ? urlLen : (BUFLEN - 1);
  memcpy(url, cursor, urlCopy);
  url[urlCopy] = 0;
  if (strlen(url) == 0) return false;
  cursor = tmpe + 1;
  if (strlen(cursor) == 0) return false;
  strlcpy(buf, cursor, 4);
  ovol = atoi(buf);
  return true;
}

bool Config::parseJSON(const char* line, char* name, char* url, int &ovol) {
  return parseJSON(line, name, BUFLEN, url, BUFLEN, ovol);
}

bool Config::parseJSON(const char* line, char* name, size_t nameSize, char* url, size_t urlSize, int &ovol) {
  char* tmps, *tmpe;
  const char* cursor = line;
  char port[8], host[246], file[254];
  tmps = strstr(cursor, "\":\"");
  if (tmps == NULL) return false;
  tmpe = strstr(tmps, "\",\"");
  if (tmpe == NULL) return false;
  if (!name || nameSize == 0) return false;
  memset(name, 0, nameSize);
  {
    const char* p = tmps + 3;
    const size_t len = static_cast<size_t>(tmpe - p);
    const size_t copy = (len < (nameSize - 1)) ? len : (nameSize - 1);
    memcpy(name, p, copy);
    name[copy] = 0;
  }
  if (strlen(name) == 0) return false;
  cursor = tmpe + 3;
  tmps = strstr(cursor, "\":\"");
  if (tmps == NULL) return false;
  tmpe = strstr(tmps, "\",\"");
  if (tmpe == NULL) return false;
  {
    const char* p = tmps + 3;
    const size_t len = static_cast<size_t>(tmpe - p);
    const size_t copy = (len < (sizeof(host) - 1)) ? len : (sizeof(host) - 1);
    memset(host, 0, sizeof(host));
    memcpy(host, p, copy);
    host[copy] = 0;
  }
  if (strlen(host) == 0) return false;
  if (strstr(host, "http://") == NULL && strstr(host, "https://") == NULL) {
    snprintf(file, sizeof(file), "http://%s", host);
    strlcpy(host, file, sizeof(host));
  }
  cursor = tmpe + 3;
  tmps = strstr(cursor, "\":\"");
  if (tmps == NULL) return false;
  tmpe = strstr(tmps, "\",\"");
  if (tmpe == NULL) return false;
  {
    const char* p = tmps + 3;
    const size_t len = static_cast<size_t>(tmpe - p);
    const size_t copy = (len < (sizeof(file) - 1)) ? len : (sizeof(file) - 1);
    memset(file, 0, sizeof(file));
    memcpy(file, p, copy);
    file[copy] = 0;
  }
  cursor = tmpe + 3;
  tmps = strstr(cursor, "\":\"");
  if (tmps == NULL) return false;
  tmpe = strstr(tmps, "\",\"");
  if (tmpe == NULL) return false;
  {
    const char* p = tmps + 3;
    const size_t len = static_cast<size_t>(tmpe - p);
    const size_t copy = (len < (sizeof(port) - 1)) ? len : (sizeof(port) - 1);
    memset(port, 0, sizeof(port));
    memcpy(port, p, copy);
    port[copy] = 0;
  }
  int p = atoi(port);
  if (!url || urlSize == 0) return false;
  memset(url, 0, urlSize);
  if (p > 0) {
    snprintf(url, urlSize, "%s:%d%s", host, p, file);
  } else {
    snprintf(url, urlSize, "%s%s", host, file);
  }
  cursor = tmpe + 3;
  tmps = strstr(cursor, "\":\"");
  if (tmps == NULL) return false;
  tmpe = strstr(tmps, "\"}");
  if (tmpe == NULL) return false;
  {
    const char* p2 = tmps + 3;
    const size_t len = static_cast<size_t>(tmpe - p2);
    const size_t copy = (len < (sizeof(port) - 1)) ? len : (sizeof(port) - 1);
    memset(port, 0, sizeof(port));
    memcpy(port, p2, copy);
    port[copy] = 0;
  }
  ovol = atoi(port);
  return true;
}

bool Config::parseWsCommand(const char* line, char* cmd, size_t cmdSize, char* val, size_t valSize) {
  if (!line || !cmd || cmdSize == 0 || !val || valSize == 0) return false;
  const char* eq = strchr(line, '=');
  if (!eq) return false;
  const size_t cmdLen = static_cast<size_t>(eq - line);
  const size_t cmdCopy = (cmdLen < (cmdSize - 1)) ? cmdLen : (cmdSize - 1);
  memcpy(cmd, line, cmdCopy);
  cmd[cmdCopy] = 0;
  const char* v = eq + 1;
  const size_t valLen = strlen(v);
  const size_t valCopy = (valLen < (valSize - 1)) ? valLen : (valSize - 1);
  memcpy(val, v, valCopy);
  val[valCopy] = 0;
  return true;
}

bool Config::parseSsid(const char* line, char* ssid, char* pass) {
  char *tmpe;
  tmpe = strstr(line, "\t");
  if (tmpe == NULL) return false;
  uint16_t pos = tmpe - line;
  if (pos > 29 || strlen(line) > 71) return false;
  memset(ssid, 0, 30);
  strlcpy(ssid, line, pos + 1);
  memset(pass, 0, 40);
  strlcpy(pass, line + pos + 1, 40);
  return true;
}

bool Config::saveWifiFromNextion(const char* post){
  File file = SPIFFS.open(SSIDS_PATH, "w");
  if (!file) {
    return false;
  } else {
    file.print(post);
    file.close();
    ESP.restart();
    return true;
  }
}

bool Config::saveWifi() {
  if (!SPIFFS.exists(TMP_PATH)) return false;
  SPIFFS.remove(SSIDS_PATH);
  SPIFFS.rename(TMP_PATH, SSIDS_PATH);
  ESP.restart();
  return true;
}

bool Config::initNetwork() {
  File file = SPIFFS.open(SSIDS_PATH, "r");
  if (!file || file.isDirectory()) {
    return false;
  }
  char ssidval[30], passval[40];
  uint8_t c = 0;
  while (file.available()) {
    if (parseSsid(file.readStringUntil('\n').c_str(), ssidval, passval)) {
      strlcpy(ssids[c].ssid, ssidval, 30);
      strlcpy(ssids[c].password, passval, 40);
      ssidsCount++;
      c++;
    }
  }
  file.close();
  return true;
}

void Config::setBrightness(bool dosave){
#if BRIGHTNESS_PIN!=255
  if(!store.dspon && dosave) {
    display.wakeup();
  }
  analogWrite(BRIGHTNESS_PIN, map(store.brightness, 0, 100, 0, 255));
  if(!store.dspon) store.dspon = true;
  if(dosave){
    saveValue(&store.brightness, store.brightness, false, true);
    saveValue(&store.dspon, store.dspon, true, true);
  }
#endif
#ifdef USE_NEXTION
  nextion.wake();
  char cmd[15];
  snprintf(cmd, 15, "dims=%d", store.brightness);
  nextion.putcmd(cmd);
  if(!store.dspon) store.dspon = true;
  if(dosave){
    saveValue(&store.brightness, store.brightness, false, true);
    saveValue(&store.dspon, store.dspon, true, true);
  }
#endif
#if (BRIGHTNESS_PIN==255) && !defined(USE_NEXTION) && (DSP_MODEL==DSP_SSD1322)
  if(!store.dspon && dosave) {
    display.wakeup();
  }
  uint32_t p = store.brightness;
  if (p > 100) p = 100;
  uint32_t p2 = p * p;
  uint8_t c = (uint8_t)((p2 * 255UL + 5000UL) / 10000UL);
  uint8_t m = (uint8_t)((p2 * 15UL + 5000UL) / 10000UL);
  dsp.setContrast(c);
  dsp.setMasterContrast(m);
  if(!store.dspon) store.dspon = true;
  if(dosave){
    saveValue(&store.brightness, store.brightness, false, true);
    saveValue(&store.dspon, store.dspon, true, true);
  }
#endif
}

void Config::setDspOn(bool dspon, bool saveval){
  if(saveval){
    store.dspon = dspon;
    saveValue(&store.dspon, store.dspon, true, true);
  }
#ifdef USE_NEXTION
  if(!dspon) nextion.sleep();
  else nextion.wake();
#endif
  if(!dspon){
#if BRIGHTNESS_PIN!=255
  analogWrite(BRIGHTNESS_PIN, 0);
#endif
    display.deepsleep();
  }else{
    display.wakeup();
#if BRIGHTNESS_PIN!=255
  analogWrite(BRIGHTNESS_PIN, map(store.brightness, 0, 100, 0, 255));
#endif
  }
}

void Config::doSleep(){
  if(BRIGHTNESS_PIN!=255) analogWrite(BRIGHTNESS_PIN, 0);
  display.deepsleep();
#ifdef USE_NEXTION
  nextion.sleep();
#endif
#if !defined(ARDUINO_ESP32C3_DEV)
  if(WAKE_PIN!=255) esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKE_PIN, LOW);
  esp_sleep_enable_timer_wakeup(config.sleepfor * 60 * 1000000ULL);
  esp_deep_sleep_start();
#endif
}

void Config::doSleepW(){
  if(BRIGHTNESS_PIN!=255) analogWrite(BRIGHTNESS_PIN, 0);
  display.deepsleep();
#ifdef USE_NEXTION
  nextion.sleep();
#endif
#if !defined(ARDUINO_ESP32C3_DEV)
  #if TOUCH_PIN != 255
    {
      int tp = digitalPinToTouchChannel(TOUCH_PIN);
      if (tp >= 0) {
        uint16_t s = touchRead(TOUCH_PIN);
        uint16_t thr = s > TOUCH_THRESHOLD ? (uint16_t)(s - TOUCH_THRESHOLD) : 0;
        touchSleepWakeUpEnable((touch_pad_t)tp, thr);
        esp_sleep_enable_touchpad_wakeup();
      } else {
        if(WAKE_PIN!=255) esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKE_PIN, LOW);
      }
    }
  #else
    if(WAKE_PIN!=255) esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKE_PIN, LOW);
  #endif
  esp_deep_sleep_start();
#endif
}

void Config::sleepForAfter(uint16_t sf, uint16_t sa){
  sleepfor = sf;
  if(sa > 0) _sleepTimer.attach(sa * 60, doSleep);
  else doSleep();
}

void Config::bootInfo() {
  BOOTLOG("************************************************");
  BOOTLOG("*               Virtuoso v%s               *", YOVERSION);
  BOOTLOG("************************************************");
  BOOTLOG("------------------------------------------------");
  BOOTLOG("arduino:\t%d", ARDUINO);
  BOOTLOG("compiler:\t%s", __VERSION__);
  BOOTLOG("esp32core:\t%d.%d.%d", ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);
#if defined(ESP32)
  {
    esp_reset_reason_t r = esp_reset_reason();
    const char* rr = "UNKNOWN";
    switch (r) {
      case ESP_RST_POWERON: rr = "POWERON"; break;
      case ESP_RST_EXT: rr = "EXT"; break;
      case ESP_RST_SW: rr = "SW"; break;
      case ESP_RST_PANIC: rr = "PANIC"; break;
      case ESP_RST_INT_WDT: rr = "INT_WDT"; break;
      case ESP_RST_TASK_WDT: rr = "TASK_WDT"; break;
      case ESP_RST_WDT: rr = "WDT"; break;
      case ESP_RST_DEEPSLEEP: rr = "DEEPSLEEP"; break;
      case ESP_RST_BROWNOUT: rr = "BROWNOUT"; break;
      case ESP_RST_SDIO: rr = "SDIO"; break;
#if defined(ESP_RST_USB)
      case ESP_RST_USB: rr = "USB"; break;
#endif
#if defined(ESP_RST_PWR_GLITCH)
      case ESP_RST_PWR_GLITCH: rr = "PWR_GLITCH"; break;
#endif
#if defined(ESP_RST_CPU_LOCKUP)
      case ESP_RST_CPU_LOCKUP: rr = "CPU_LOCKUP"; break;
#endif
      default: break;
    }
    BOOTLOG("reset:\t\tesp=%d | %s", (int)r, rr);
  }
#endif
  uint32_t chipId = 0;
  for(int i=0; i<17; i=i+8) {
    chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
  }
  BOOTLOG("chip:\t\tmodel: %s | rev: %d | id: %d | cores: %d | psram: %d", ESP.getChipModel(), ESP.getChipRevision(), chipId, ESP.getChipCores(), ESP.getPsramSize());
  BOOTLOG("display:\t%d", DSP_MODEL);
  if(VS1053_CS==255) {
    BOOTLOG("audio:\t\t%s (%d, %d, %d)", "I2S", I2S_DOUT, I2S_BCLK, I2S_LRC);
  }else{
    BOOTLOG("audio:\t\t%s (%d, %d, %d, %d, %s)", "VS1053", VS1053_CS, VS1053_DCS, VS1053_DREQ, VS1053_RST, VS_HSPI?"true":"false");
  }
  BOOTLOG("audioinfo:\t%s", store.audioinfo?"true":"false");
  BOOTLOG("smartstart:\t%d", store.smartstart);
  BOOTLOG("vumeter:\t%s", store.vumeter?"true":"false");
  BOOTLOG("softapdelay:\t%d", store.softapdelay);
  BOOTLOG("flipscreen:\t%s", store.flipscreen?"true":"false");
  BOOTLOG("invertdisplay:\t%s", store.invertdisplay?"true":"false");
  BOOTLOG("showweather:\t%s", store.showweather?"true":"false");
  BOOTLOG("buttons:\tleft=%d, center=%d, right=%d, up=%d, down=%d, mode=%d, pullup=%s", 
          BTN_LEFT, BTN_CENTER, BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_MODE, BTN_INTERNALPULLUP?"true":"false");
  BOOTLOG("encoders:\tl1=%d, b1=%d, r1=%d, pullup=%s, l2=%d, b2=%d, r2=%d, pullup=%s", 
          ENC_BTNL, ENC_BTNB, ENC_BTNR, ENC_INTERNALPULLUP?"true":"false", ENC2_BTNL, ENC2_BTNB, ENC2_BTNR, ENC2_INTERNALPULLUP?"true":"false");
  BOOTLOG("ir:\t\t%d", IR_PIN);
  if(SDC_CS!=255) BOOTLOG("SD:\t\t%d", SDC_CS);
  BOOTLOG("------------------------------------------------");
}

