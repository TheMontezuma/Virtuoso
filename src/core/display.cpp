#include "options.h"

#include "WiFi.h"
#include "time.h"
#include "display.h"
#include "player.h"
#include "network.h"


Display display;
#ifdef USE_NEXTION
Nextion nextion;
#endif

#ifndef DUMMYDISPLAY
//============================================================================================================================
DspCore dsp;

Page *pages[] = { new Page(), new Page(), new Page(), new Page() };

#ifndef DSQ_SEND_DELAY
#define DSQ_SEND_DELAY portMAX_DELAY
#endif

#ifndef CORE_STACK_SIZE
#define CORE_STACK_SIZE 1024 * 3
#endif
#ifndef DSP_TASK_DELAY
#define DSP_TASK_DELAY pdMS_TO_TICKS(10)
#endif
#ifndef DSP_TASK_PRIO
#define DSP_TASK_PRIO 1
#endif
#ifndef DSP_TASK_CORE
#define DSP_TASK_CORE xPortGetCoreID()
#endif
// Wyłączone wymuszanie BITRATE_FULL = false, używamy ustawienia z myoptions.h
/*
#if !((DSP_MODEL == DSP_ST7735 && DTYPE == INITR_BLACKTAB) || DSP_MODEL == DSP_SSD1322 || DSP_MODEL == DSP_ST7789 || DSP_MODEL == DSP_ST7796 || DSP_MODEL == DSP_ILI9488 || DSP_MODEL == DSP_ILI9486 || DSP_MODEL == DSP_ILI9341 || DSP_MODEL == DSP_ILI9225)
#undef BITRATE_FULL
#define BITRATE_FULL false
#endif
*/
TaskHandle_t DspTask;
QueueHandle_t displayQueue;

void returnPlayer() {
  if (display.mode() == NUMBERS && display.numOfNextStation > 0) {
    player.sendCommand({ PR_PLAY, display.numOfNextStation });
    display.numOfNextStation = 0;
  }
  if (display.mode() == STATIONS && display.currentPlItem > 0) {
    player.sendCommand({ PR_PLAY, display.currentPlItem });
  }
  display.putRequest(NEWMODE, PLAYER);
}

void Display::_createDspTask() {
  xTaskCreatePinnedToCore(loopDspTask, "DspTask", CORE_STACK_SIZE, NULL, DSP_TASK_PRIO, &DspTask, DSP_TASK_CORE);
}

void loopDspTask(void *pvParameters) {
  while (true) {
    if (displayQueue == NULL) break;
    display.loop();
    vTaskDelay(DSP_TASK_DELAY);
  }
  vTaskDelete(NULL);
  DspTask = NULL;
}

void Display::init() {
  Serial.print("##[BOOT]#\tdisplay.init\t");
#ifdef USE_NEXTION
  nextion.begin();
#endif
#if LIGHT_SENSOR != 255
  analogSetAttenuation(ADC_0db);
#endif
  _bootStep = 0;
  dsp.initDisplay();
  displayQueue = NULL;
  displayQueue = xQueueCreate(5, sizeof(requestParams_t));
  while (displayQueue == NULL) { ; }
  _createDspTask();
  while (_bootStep == 0) { delay(10); }
  //_pager.begin();
  //_bootScreen();
  Serial.println("gotowe");
}

void Display::_bootScreen() {
  _boot = new Page();
  _boot->addWidget(new ProgressWidget(bootWdtConf, bootPrgConf, BOOT_PRG_COLOR, 0));
  _bootstring = NULL;
  _pager.addPage(_boot);
  _pager.setPage(_boot, true);
  dsp.drawLogo(bootLogoTop);
  _bootStep = 1;
}

void Display::_buildPager() {
  _meta.init("*", metaConf, config.theme.meta, config.theme.metabg);
  _title1.init("*", title1Conf, config.theme.title1, config.theme.background);
  _clock.init(clockConf, 0, 0);
#if DSP_MODEL == DSP_NOKIA5110
  _plcurrent.init("*", playlistConf, 0, 1);
#else
  _plcurrent.init("*", playlistConf, config.theme.plcurrent, config.theme.plcurrentbg);
#endif
#if !defined(DSP_LCD)
  _plcurrent.moveTo({ TFT_FRAMEWDT, (uint16_t)(dsp.plYStart + dsp.plCurrentPos * dsp.plItemHeight), (int16_t)playlistConf.width });
#endif
#ifndef HIDE_TITLE2
  _title2 = new ScrollWidget("*", title2Conf, config.theme.title2, config.theme.background);
#endif
#ifdef HAS_CALENDAR_CONF
  _cal1 = new ScrollWidget("*", calendar1Conf, config.theme.weather, config.theme.background);
  _cal2 = new ScrollWidget("*", calendar2Conf, config.theme.weather, config.theme.background);
#endif
#if !defined(DSP_LCD) && DSP_MODEL != DSP_NOKIA5110
#if DSP_INVERT_TITLE || defined(DSP_OLED)
  _metabackground = new FillWidget(metaBGConf, config.theme.metafill);
#else
  _metabackground = new FillWidget(metaBGConfInv, config.theme.metafill);
#endif
#endif
#if DSP_MODEL == DSP_NOKIA5110
  _plbackground = new FillWidget(playlBGConf, 1);
  //_metabackground = new FillWidget(metaBGConf, 1);
#endif
#ifndef HIDE_VU
  _vuwidget = new VuWidget(vuConf, bandsConf, config.theme.vumax, config.theme.vumin, config.theme.background);
#endif
#ifndef HIDE_VOLBAR
  _volbar = new SliderWidget(volbarConf, config.theme.volbarin, config.theme.background, 254, config.theme.volbarout);
#endif
#ifndef HIDE_HEAPBAR
  _heapbar = new SliderWidget(heapbarConf, config.theme.buffer, config.theme.background, psramInit() ? 300000 : 1600 * AUDIOBUFFER_MULTIPLIER2);
#endif
#ifndef HIDE_VOL
  _voltxt = new TextWidget(voltxtConf, 10, false, config.theme.vol, config.theme.background);
#endif
#ifndef HIDE_IP
  _volip = new TextWidget(iptxtConf, 30, false, config.theme.ip, config.theme.background);
#endif
#ifndef HIDE_RSSI
  _rssi = new RssiWidget(rssiConf, 20, false, config.theme.rssi, config.theme.background);
#endif
  _nums.init(numConf, 10, false, config.theme.digit, config.theme.background);
#ifndef HIDE_WEATHER
  _weather = new ScrollWidget("\007", weatherConf, config.theme.weather, config.theme.background);
  _sstext = new ScrollWidget("*", weatherConf, config.theme.weather, config.theme.background);
#endif

  if (_volbar) _footer.addWidget(_volbar);
  if (_voltxt) _footer.addWidget(_voltxt);
  if (_heapbar) _footer.addWidget(_heapbar);

  if (_metabackground) pages[PG_PLAYER]->addWidget(_metabackground);
  pages[PG_PLAYER]->addWidget(&_meta);
  pages[PG_PLAYER]->addWidget(&_title1);
  if (_title2) pages[PG_PLAYER]->addWidget(_title2);
  if (_weather) pages[PG_SCREENSAVER]->addWidget(_weather);
#ifndef HIDE_WEATHER
  if (_sstext) pages[PG_SCREENSAVER]->addWidget(_sstext);
#endif
#ifdef HAS_CALENDAR_CONF
  if (_cal1) pages[PG_SCREENSAVER]->addWidget(_cal1);
  if (_cal2) pages[PG_SCREENSAVER]->addWidget(_cal2);
#endif

#if BITRATE_FULL
  _fullbitrate = new BitrateWidget(fullbitrateConf, config.theme.bitrate, config.theme.background);
  pages[PG_PLAYER]->addWidget(_fullbitrate);
#else
  _bitrate = new TextWidget(bitrateConf, 30, false, config.theme.bitrate, config.theme.background);
  pages[PG_PLAYER]->addWidget(_bitrate);
#endif

  if (_vuwidget) pages[PG_PLAYER]->addWidget(_vuwidget);
  pages[PG_PLAYER]->addWidget(&_clock);
  pages[PG_SCREENSAVER]->addWidget(&_clock);
  pages[PG_PLAYER]->addPage(&_footer);
  if (_rssi) pages[PG_PLAYER]->addWidget(_rssi);  //------------ tu dopiero wyswietlam rssi -----------

  if (_metabackground) pages[PG_DIALOG]->addWidget(_metabackground);
  pages[PG_DIALOG]->addWidget(&_meta);
  pages[PG_DIALOG]->addWidget(&_nums);

#if !defined(DSP_LCD) && DSP_MODEL != DSP_NOKIA5110
  pages[PG_DIALOG]->addPage(&_footer);
#endif
#if !defined(DSP_LCD)
  if (_plbackground) {
    pages[PG_PLAYLIST]->addWidget(_plbackground);
    _plbackground->setHeight(dsp.plItemHeight);
    _plbackground->moveTo({ 0, (uint16_t)(dsp.plYStart + dsp.plCurrentPos * dsp.plItemHeight - playlistConf.widget.textsize * 2), (int16_t)playlBGConf.width });
  }
#endif
  pages[PG_PLAYLIST]->addWidget(&_plcurrent);

  for (const auto &p : pages) _pager.addPage(p);
}

void Display::_apScreen() {
  if (_boot) _pager.removePage(_boot);
#ifndef DSP_LCD
  _boot = new Page();
#if DSP_MODEL != DSP_NOKIA5110
#if DSP_INVERT_TITLE || defined(DSP_OLED)
  _boot->addWidget(new FillWidget(metaBGConf, config.theme.metafill));
#else
  _boot->addWidget(new FillWidget(metaBGConfInv, config.theme.metafill));
#endif
#endif
  ScrollWidget *bootTitle = (ScrollWidget *)&_boot->addWidget(new ScrollWidget("*", apTitleConf, config.theme.meta, config.theme.metabg));
  bootTitle->setText("TRYB AP");
  uint16_t nameLblW = dsp.textWidth("NAZWA SIECI");
  uint16_t colonW = dsp.textWidth(": ");
  uint16_t nameValW = dsp.textWidth(apSsid);
  uint16_t nameTotalW = nameLblW + colonW + nameValW;
  uint16_t nameLeft = (dsp.width() - nameTotalW) / 2;
  TextWidget *nameLabel = (TextWidget *)&_boot->addWidget(new TextWidget(apNameConf, 30, false, config.theme.title1, config.theme.background));
  nameLabel->setAlign(WA_LEFT);
  nameLabel->moveTo({ (int16_t)nameLeft, apNameConf.top, 0 });
  nameLabel->setText("NAZWA SIECI");
  TextWidget *nameColon = (TextWidget *)&_boot->addWidget(new TextWidget(apNameConf, 30, false, config.theme.title1, config.theme.background));
  nameColon->setAlign(WA_LEFT);
  nameColon->moveTo({ (int16_t)(nameLeft + nameLblW), apNameConf.top, 0 });
  nameColon->setText(": ");
  TextWidget *nameValue = (TextWidget *)&_boot->addWidget(new TextWidget(apNameConf, 30, false, config.theme.clock, config.theme.background));
  nameValue->setAlign(WA_LEFT);
  nameValue->moveTo({ (int16_t)(nameLeft + nameLblW + colonW), apNameConf.top, 0 });
  nameValue->setText(apSsid);

  uint16_t passLblW = dsp.textWidth("HASŁO");
  uint16_t passValW = dsp.textWidth(apPassword);
  uint16_t colonX = nameLeft + nameLblW;
  uint16_t passLeft = colonX - passLblW;
  uint16_t sieciStartX = nameLeft + dsp.textWidth("NAZWA ");
  TextWidget *passLabel = (TextWidget *)&_boot->addWidget(new TextWidget(apPassConf, 30, false, config.theme.title1, config.theme.background));
  passLabel->setAlign(WA_LEFT);
  passLabel->moveTo({ (int16_t)sieciStartX, apPassConf.top, 0 });
  passLabel->setText("HASŁO");
  TextWidget *passColon = (TextWidget *)&_boot->addWidget(new TextWidget(apPassConf, 30, false, config.theme.title1, config.theme.background));
  passColon->setAlign(WA_LEFT);
  passColon->moveTo({ (int16_t)colonX, apPassConf.top, 0 });
  passColon->setText(": ");
  TextWidget *passValue = (TextWidget *)&_boot->addWidget(new TextWidget(apPassConf, 30, false, config.theme.clock, config.theme.background));
  passValue->setAlign(WA_LEFT);
  passValue->moveTo({ (int16_t)(colonX + colonW), apPassConf.top, 0 });
  passValue->setText(apPassword);
  ScrollWidget *bootSett = (ScrollWidget *)&_boot->addWidget(new ScrollWidget("*", apSettConf, config.theme.title2, config.theme.background));
  bootSett->setText(WiFi.softAPIP().toString().c_str(), apSettFmt);
  _pager.addPage(_boot);
  _pager.setPage(_boot);
#else
  dsp.apScreen();
#endif
}

void Display::_start() {
  if (_boot) _pager.removePage(_boot);
#ifdef USE_NEXTION
  nextion.wake();
#endif
  if (network.status != CONNECTED && network.status != SDREADY) {
    _apScreen();
#ifdef USE_NEXTION
    nextion.apScreen();
#endif
    _bootStep = 2;
    return;
  }
#ifdef USE_NEXTION
  //nextion.putcmd("page player");
  nextion.start();
#endif
  _buildPager();
  _mode = PLAYER;
  config.setTitle(const_PlReady);

  if (_heapbar) _heapbar->lock(!config.store.audioinfo);

  if (_weather) _weather->lock(!config.store.showweather);
#ifndef HIDE_WEATHER
  if (_sstext) {
    _sstext->lock(strlen(config.store.screensaverText) == 0);
    if (strlen(config.store.screensaverText) > 0) _sstext->setText(config.store.screensaverText);
  }
#endif

  if (_weather && config.store.showweather) _weather->setText(const_getWeather);

  if (_vuwidget) _vuwidget->lock();

  if (_rssi) _setRSSI(WiFi.RSSI());

  _pager.setPage(pages[PG_PLAYER]);
  _volume();
  _station();
  _time(false);
  _bootStep = 2;
  pm.on_display_player();
}

void Display::_showDialog(const char *title) {
  dsp.setScrollId(NULL);
  _pager.setPage(pages[PG_DIALOG]);
#ifdef META_MOVE
  _meta.moveTo(metaMove);
#endif
  _meta.setAlign(WA_CENTER);
  _meta.setText(title);
}

void Display::_setReturnTicker(uint8_t time_s) {
  _returnTicker.detach();
  _returnTicker.once(time_s, returnPlayer);
}

void Display::_swichMode(displayMode_e newmode) {
#ifdef USE_NEXTION
  //nextion.swichMode(newmode);
  nextion.putRequest({ NEWMODE, newmode });
#endif
  if (config.getMode() == PM_BLUETOOTH && (newmode == SCREENSAVER || newmode == SCREENBLANK)) return;
  if (newmode == _mode || (network.status != CONNECTED && network.status != SDREADY)) return;
  displayMode_e prevMode = _mode;
  _mode = newmode;
  dsp.setScrollId(NULL);
  if (prevMode == SLEEPING && newmode != SLEEPING) {
    config.clockOnly = false;
    if (_weather) _weather->unlock();
#ifndef HIDE_WEATHER
    if (_sstext) _sstext->unlock();
#endif
#ifdef HAS_CALENDAR_CONF
    if (_cal1) _cal1->unlock();
    if (_cal2) _cal2->unlock();
#endif
    dsp.clearDsp();
    _meta.setText("");
    _title1.setText("");
    if (_title2) _title2->setText("");
    if (_bitrate) _bitrate->setText("");
    if (_rssi) _rssi->setText("");
    _nums.setText("");
  }
  if (newmode == PLAYER) {
    if (config.getMode() == PM_BLUETOOTH) {
      dsp.clearDsp();
      numOfNextStation = 0;
      _returnTicker.detach();
#ifdef META_MOVE
      _meta.moveBack();
#endif
      _meta.setTextSize(metaConf.widget.textsize);
      _meta.setAlign(WA_CENTER);
      _meta.setText("Bluetooth");
      _nums.setText("");
      _title1.setTextSize(title1Conf.widget.textsize);
      _title1.setAlign(WA_CENTER);
      {
        uint8_t _cw;
        uint16_t metaH;
        dsp.charSize(metaConf.widget.textsize, _cw, metaH);
        uint16_t secondLineTop = (uint16_t)(metaConf.widget.top + metaH + 6);
        _title1.moveTo({ 0, secondLineTop, (int16_t)dsp.width() });
      }
      _title1.setText("Aktywne");
      if (_title2) _title2->setText("");
      if (_bitrate) _bitrate->setText("");
      if (_fullbitrate) {
        _fullbitrate->setBitrate(0);
        _fullbitrate->setFormat(BF_UNCNOWN);
      }
      if (_vuwidget) _vuwidget->lock(true);
      dsp.fillRect(vuConf.left, vuConf.top, (int16_t)dsp.width(), (int16_t)(bandsConf.height * 2 + 6), config.theme.background);
      if (_weather) _weather->lock(true);
#ifndef HIDE_WEATHER
      if (_sstext) _sstext->lock(true);
#endif
#ifdef HAS_CALENDAR_CONF
      if (_cal1) _cal1->lock(true);
      if (_cal2) _cal2->lock(true);
#endif
      if (_volbar) _volbar->lock(true);
      if (_heapbar) _heapbar->lock(true);
      if (_voltxt) _voltxt->lock(true);
      if (_rssi) _rssi->lock(true);
      if (_bitrate) _bitrate->lock(true);
      if (_fullbitrate) _fullbitrate->lock(true);
      config.isScreensaver = false;
      config.clockOnly = false;
      _pager.setPage(pages[PG_PLAYER]);
      _layoutChange(false);
      config.setDspOn(config.store.dspon, false);
      pm.on_display_player();
      return;
    }
    _clock.unlock();
    if (_vuwidget) _vuwidget->lock(!config.store.vumeter);
    if (_weather) _weather->lock(!config.store.showweather);
#ifdef HAS_CALENDAR_CONF
    if (_cal1) _cal1->lock(!config.store.showcalendar);
    if (_cal2) _cal2->lock(!config.store.showcalendar);
#endif
    if (_volbar) _volbar->unlock();
    if (_heapbar) _heapbar->lock(!config.store.audioinfo);
    if (_voltxt) _voltxt->unlock();
    if (_rssi) _rssi->unlock();
    if (_bitrate) _bitrate->unlock();
    if (_fullbitrate) _fullbitrate->unlock();
    if (player.isRunning())
      _clock.moveTo(clockMove);
    else
      _clock.moveBack();
#ifdef DSP_LCD
    dsp.clearDsp();
#endif
    numOfNextStation = 0;
    _returnTicker.detach();
#ifdef META_MOVE
    _meta.moveBack();
#endif
    _nums.setText("");
    _nums.setText(config.lastStation(), "%d");
    config.isScreensaver = false;
    config.clockOnly = false;
    _pager.setPage(pages[PG_PLAYER]);
    _layoutChange(player.isRunning());
    _station();
    config.setDspOn(config.store.dspon, false);
    pm.on_display_player();
  }
  if (newmode == SCREENSAVER || newmode == SCREENBLANK) {
    config.isScreensaver = true;
    config.clockOnly = false;
    _pager.setPage(pages[PG_SCREENSAVER]);
    if (newmode == SCREENBLANK) {
      _clock.moveTo(clockMove);
      config.setDspOn(true, false);
      putRequest(CLOCK, 0);
    } else { // SCREENSAVER
      dsp.clearDsp();
      uint8_t _cw;
      uint16_t timeH, dateH, weatherH = 0;
      dsp.charSize(2, _cw, timeH);
      dsp.charSize(1, _cw, dateH);
#ifndef HIDE_WEATHER
      dsp.charSize(weatherConf.widget.textsize, _cw, weatherH);
      const bool showWeatherLine = config.store.showweather;
      const bool showTextLine = strlen(config.store.screensaverText) > 0;
      if (_weather) _weather->lock(!showWeatherLine);
      if (_sstext) {
        _sstext->lock(!showTextLine);
        if (showTextLine) _sstext->setText(config.store.screensaverText);
      }
#else
      const bool showWeatherLine = false;
      const bool showTextLine = false;
#endif
      uint16_t cal1H = 0, cal2H = 0;
#ifdef HAS_CALENDAR_CONF
      dsp.charSize(calendar1Conf.widget.textsize, _cw, cal1H);
      dsp.charSize(calendar2Conf.widget.textsize, _cw, cal2H);
#endif
      uint16_t totalH = timeH + 2 + dateH + 2;
#ifndef HIDE_WEATHER
      if (showWeatherLine && _weather) totalH += weatherH + 8;
      if (showTextLine && _sstext) totalH += weatherH + 8;
#endif
#ifdef HAS_CALENDAR_CONF
      if (_cal1) totalH += cal1H + 2;
      if (_cal2) totalH += cal2H;
#endif

      int16_t blockTop = (int16_t)((dsp.height() - totalH) / 2);
      if (blockTop < 0) blockTop = 0;

      _clock.moveTo({ clockConf.left, (uint16_t)blockTop, 0 });
      uint16_t y = (uint16_t)blockTop + timeH + 2 + dateH + 2;
#ifndef HIDE_WEATHER
      if (showWeatherLine && _weather) { _weather->moveTo({ weatherConf.widget.left, y, (int16_t)weatherConf.width }); y += weatherH + 8; }
      if (showTextLine && _sstext) {
        uint16_t textTop = (uint16_t)(y + 3);
        if (dsp.height() > 0 && textTop + weatherH > dsp.height()) {
          textTop = (uint16_t)max<int16_t>(0, (int16_t)dsp.height() - (int16_t)weatherH);
        }
        _sstext->moveTo({ weatherConf.widget.left, textTop, (int16_t)weatherConf.width });
        y += weatherH + 8;
      }
#endif
#ifdef HAS_CALENDAR_CONF
      if (_cal1) { _cal1->moveTo({ calendar1Conf.widget.left, y, (int16_t)calendar1Conf.width }); y += (_cal1 ? cal1H : 0) + 2; }
      if (_cal2) { _cal2->moveTo({ calendar2Conf.widget.left, y, (int16_t)calendar2Conf.width }); }
#endif
      putRequest(CLOCK, 0);
    }
  } else {
    config.screensaverTicks = SCREENSAVERSTARTUPDELAY;
    config.screensaverPlayingTicks = SCREENSAVERSTARTUPDELAY;
    config.isScreensaver = false;
  }
  /*-------------------------------------------------*/
  /*----------------- MODE VOLUME -------------------*/
  if (newmode == VOL) {
    _showDialog(const_DlgVolume);
    //_nums.setText(config.store.volume, numtxtFmt);
    uint8_t volPercent = (config.store.volume * 100 + 127) / 254;  // zmiana 08.05 wyswietlania volume z 1-254 na 1-100
    _nums.setText(volPercent, "%d");


    if (_volip) {
      _volip->setText(WiFi.localIP().toString().c_str(), iptxtFmt);
      pages[PG_DIALOG]->addWidget(_volip);  // dodanie tylko do dialogu
    }
  }
  /*------------- END MODE VOLUME -------------------*/
  /*-------------------------------------------------*/
  if (newmode == LOST) _showDialog(const_DlgLost);
  if (newmode == UPDATING) _showDialog(const_DlgUpdate);
  if (newmode == SLEEPING) {
    dsp.clearDsp();
    config.isScreensaver = true;
    if (_weather) _weather->lock(true);
#ifndef HIDE_WEATHER
    if (_sstext) _sstext->lock(true);
#endif
#ifdef HAS_CALENDAR_CONF
    if (_cal1) _cal1->lock(true);
    if (_cal2) _cal2->lock(true);
#endif
    config.clockOnly = true;
    config.clockOnlyOffsetX = -37;
    _pager.setPage(pages[PG_SCREENSAVER]);
    config.setDspOn(true, false);
    putRequest(CLOCK, 1);
  }
  if (newmode == SDCHANGE) _showDialog(const_waitForSD);
  if (newmode == INFO || newmode == SETTINGS || newmode == TIMEZONE || newmode == WIFI) _showDialog(const_DlgNextion);
  if (newmode == NUMBERS) _showDialog("");
  if (newmode == STATIONS) {
    _pager.setPage(pages[PG_PLAYLIST]);
    _plcurrent.setText("");
    currentPlItem = config.lastStation();
    _drawPlaylist();
  }
}

void Display::resetQueue() {
  if (displayQueue != NULL) xQueueReset(displayQueue);
}

void Display::_drawPlaylist() {
  dsp.drawPlaylist(currentPlItem);
  _setReturnTicker(8);  //-------------------------wyjscie do ekranu glownego------------------------------------------
}

void Display::_drawNextStationNum(uint16_t num) {
  _setReturnTicker(3);  //-------------------------wyjscie do ekranu glownego------------------------------------------
  _meta.setAlign(metaConf.widget.align);
  _meta.setText(config.stationByNum(num));
  _nums.setText(num, "%d");
}

void Display::printPLitem(uint8_t pos, const char *item) {
  dsp.printPLitem(pos, item, _plcurrent);
}

void Display::putRequest(displayRequestType_e type, int payload) {
  if (displayQueue == NULL) return;
  requestParams_t request;
  request.type = type;
  request.payload = payload;
  xQueueSend(displayQueue, &request, DSQ_SEND_DELAY);
#ifdef USE_NEXTION
  nextion.putRequest(request);
#endif
}

void Display::_layoutChange(bool played) {
  if (config.getMode() == PM_BLUETOOTH && _mode == PLAYER) {
    _meta.setTextSize(metaConf.widget.textsize);
    _meta.setAlign(WA_CENTER);
    _meta.setText("Bluetooth");
    _title1.setTextSize(title1Conf.widget.textsize);
    _title1.setAlign(WA_CENTER);
    {
      uint8_t _cw;
      uint16_t metaH;
      dsp.charSize(metaConf.widget.textsize, _cw, metaH);
      uint16_t secondLineTop = (uint16_t)(metaConf.widget.top + metaH + 6);
      _title1.moveTo({ 0, secondLineTop, (int16_t)dsp.width() });
    }
    _title1.setText("Aktywne");
    if (_vuwidget) _vuwidget->lock(true);
    dsp.fillRect(vuConf.left, vuConf.top, (int16_t)dsp.width(), (int16_t)(bandsConf.height * 2 + 6), config.theme.background);
    if (_title2) _title2->setText("");
    return;
  }
  if (config.store.vumeter) {
    _meta.setTextSize(metaConf.widget.textsize);
    _title1.setTextSize(title1Conf.widget.textsize);
    _meta.moveBack();
    _title1.moveBack();
    if (played) {
      if (_vuwidget) _vuwidget->unlock();
      _clock.moveTo(clockMove);
      if (_weather) _weather->moveTo(weatherMoveVU);
    } else {
      if (_vuwidget)
        if (!_vuwidget->locked()) _vuwidget->lock();
      _clock.moveBack();
      if (_weather) _weather->moveBack();
    }
  } else {
    _meta.setTextSize(2);
    _title1.setTextSize(title1Conf.widget.textsize);
    _meta.setAlign(WA_CENTER);
    _meta.moveTo({ 0, 17, (int16_t)dsp.width() });
    _title1.moveTo({ 0, 41, (int16_t)dsp.width() });
    if (played) {
      if (_weather) _weather->moveTo(weatherMove);
      _clock.moveBack();
    } else {
      if (_weather) _weather->moveBack();
      _clock.moveBack();
    }
  }
}
#ifndef DSP_QUEUE_TICKS
#define DSP_QUEUE_TICKS 0
#endif
void Display::loop() {
  if (_bootStep == 0) {
    _pager.begin();
    _bootScreen();
    return;
  }
  if (displayQueue == NULL) return;
  _pager.loop();
#ifdef USE_NEXTION
  nextion.loop();
#endif
  requestParams_t request;
  if (xQueueReceive(displayQueue, &request, DSP_QUEUE_TICKS)) {
    bool pm_result = true;
    pm.on_display_queue(request, pm_result);
    if (pm_result)
      switch (request.type) {
        case NEWMODE: _swichMode((displayMode_e)request.payload); break;
        case CLOCK:
          if (_mode == PLAYER || _mode == SCREENSAVER || _mode == SCREENBLANK || _mode == SLEEPING) _time(request.payload != 0);
          /*#ifdef USE_NEXTION
            if(_mode==TIMEZONE) nextion.localTime(network.timeinfo);
            if(_mode==INFO)     nextion.rssi();
          #endif*/
          break;
        case NEWTITLE: _title(); break;
        case NEWSTATION: _station(); break;
        case NEXTSTATION: _drawNextStationNum(request.payload); break;
        case DRAWPLAYLIST: _drawPlaylist(); break;
        case DRAWVOL: _volume(); break;
        case DBITRATE:
          {
            if (config.getMode() == PM_BLUETOOTH) {
              if (_bitrate) _bitrate->setText("");
              if (_fullbitrate) {
                _fullbitrate->setBitrate(0);
                _fullbitrate->setFormat(BF_UNCNOWN);
              }
              break;
            }
            static uint32_t lastBitrate = 0;
            static BitrateFormat lastFormat = BF_UNCNOWN;
            const bool bitrateChanged = (config.station.bitrate != lastBitrate);
            const bool formatChanged = (config.configFmt != lastFormat);
            if (bitrateChanged || formatChanged) {
              lastBitrate = config.station.bitrate;
              lastFormat = config.configFmt;
              char buf[20];
              const char* fmt = "%dkbps";
              switch(config.configFmt){
                case BF_MP3:  fmt = "MP3 %dkbps"; break;
                case BF_AAC:  fmt = "AAC %dkbps"; break;
                case BF_FLAC: fmt = "FLC %dkbps"; break;
                case BF_OGG:  fmt = "OGG %dkbps"; break;
                case BF_WAV:  fmt = "WAV %dkbps"; break;
                default:      fmt = "%dkbps";     break;
              }
              snprintf(buf, 20, fmt, config.station.bitrate);
              if (_bitrate) {
                _bitrate->setText(config.station.bitrate == 0 ? "" : buf);
              }
              if (_fullbitrate) {
                _fullbitrate->setBitrate(config.station.bitrate);
                _fullbitrate->setFormat(config.configFmt);
              }
            }
          }
          break;
        case AUDIOINFO:
          if (config.getMode() == PM_BLUETOOTH) break;
          if (_heapbar) {
            _heapbar->lock(!config.store.audioinfo);
            _heapbar->setValue(player.inBufferFilled());
          }
          break;
        case SHOWVUMETER:
          {
            if (_vuwidget) {
              if (config.getMode() == PM_BLUETOOTH) {
                _vuwidget->lock(true);
              } else {
                _vuwidget->lock(!config.store.vumeter);
              }
              _layoutChange(player.isRunning());
              if (_mode == PLAYER) {
                _station();
                _title();
              }
            }
            break;
          }
        case SHOWWEATHER:
          {
            if (config.getMode() == PM_BLUETOOTH) break;
            if (_mode == SLEEPING) {
              if (_weather) _weather->lock(true);
#ifndef HIDE_WEATHER
              if (_sstext) _sstext->lock(true);
#endif
              break;
            }
            if (_weather) _weather->lock(!config.store.showweather);
#ifndef HIDE_WEATHER
            if (_sstext) {
              const bool haveText = strlen(config.store.screensaverText) > 0;
              _sstext->lock(!haveText);
              if (haveText) _sstext->setText(config.store.screensaverText);
            }
#endif
            if (!config.store.showweather) {
#ifndef HIDE_IP
              if (_volip) _volip->setText(WiFi.localIP().toString().c_str(), iptxtFmt);
#endif
            } else {
              if (_weather) _weather->setText(const_getWeather);
            }
            break;
          }
        case NEWWEATHER:
          {
            if (config.getMode() == PM_BLUETOOTH) break;
            if (_mode == SLEEPING) {
              if (_weather) _weather->lock(true);
#ifndef HIDE_WEATHER
              if (_sstext) _sstext->lock(true);
#endif
              break;
            }
            if (_weather && network.weatherBuf && config.store.showweather) _weather->setText(network.weatherBuf);
#ifndef HIDE_WEATHER
            if (_sstext) {
              const bool haveText = strlen(config.store.screensaverText) > 0;
              _sstext->lock(!haveText);
              if (haveText) _sstext->setText(config.store.screensaverText);
            }
#endif
            break;
          }
        case SHOWCALENDAR:
          {
            if (config.getMode() == PM_BLUETOOTH) break;
#ifdef HAS_CALENDAR_CONF
            if (_cal1) _cal1->lock(!config.store.showcalendar);
            if (_cal2) _cal2->lock(!config.store.showcalendar);
#endif
            break;
          }
        case NEWCALENDAR:
          {
            if (config.getMode() == PM_BLUETOOTH) break;
#ifdef HAS_CALENDAR_CONF
            if (_cal1 && network.calendar1Buf) _cal1->setText(network.calendar1Buf);
            if (_cal2 && network.calendar2Buf) _cal2->setText(network.calendar2Buf);
#endif
            break;
          }
        case BOOTSTRING:
          {
            if (_bootstring) _bootstring->setText(config.ssids[request.payload].ssid, bootstrFmt);
            /*#ifdef USE_NEXTION
            char buf[50];
            snprintf(buf, 50, bootstrFmt, config.ssids[request.payload].ssid);
            nextion.bootString(buf);
          #endif*/
            break;
          }
        case WAITFORSD:
          {
            if (_bootstring) _bootstring->setText(const_waitForSD);
            break;
          }
        case SDFILEINDEX:
          {
            if (_mode == SDCHANGE) _nums.setText(request.payload, "%d");
            break;
          }
        case DSPRSSI:
          if (config.getMode() == PM_BLUETOOTH) break;
          if (_rssi) { _setRSSI(request.payload); }
          if (_heapbar && config.store.audioinfo) _heapbar->setValue(player.isRunning() ? player.inBufferFilled() : 0);
          break;
        case PSTART: _layoutChange(true); break;
        case PSTOP: _layoutChange(false); break;
        case DSP_START: _start(); break;
        case NEWIP:
          {
#ifndef HIDE_IP
            if (_volip) _volip->setText(WiFi.localIP().toString().c_str(), iptxtFmt);
#endif
            break;
          }
        default: break;
      }
  }
  dsp.loop();
#if I2S_DOUT == 255
  player.computeVUlevel();
#endif
}

void Display::_setRSSI(int rssi) {
  if (!_rssi) return;
#if RSSI_DIGIT
  _rssi->setText(rssi, rssiFmt);
  return;
#endif
  int rssi_steps[] = { RSSI_STEPS };
  uint8_t level;
  if (rssi >= rssi_steps[0]) level = 4;
  else if (rssi >= rssi_steps[1]) level = 3;
  else if (rssi >= rssi_steps[2]) level = 2;
  else if (rssi >= rssi_steps[3]) level = 1;
  else level = 0;
  char rssiG[3] = { (char)('0' + level), ' ', 0 };
  _rssi->setText(rssiG);
}

void Display::_station() {
  if (config.getMode() == PM_BLUETOOTH) {
    _meta.setAlign(WA_CENTER);
    _meta.setText("Bluetooth");
    return;
  }

  _meta.setAlign(metaConf.widget.align);
  const char* name = config.station.name;
  char filtered[BUFLEN];
  strlcpy(filtered, name ? name : "", sizeof(filtered));
  size_t _tl = strlen(filtered);
  char _lb[BUFLEN];
  size_t max_i = (_tl < (sizeof(_lb) - 1)) ? _tl : (sizeof(_lb) - 1);
  for (size_t i = 0; i < max_i; i++) {
    char ch = filtered[i];
    _lb[i] = (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
  }
  _lb[max_i] = 0;
  if (strstr(_lb, "connect") != NULL || strstr(_lb, "conect") != NULL) filtered[0] = 0;
  _meta.setText(filtered);

  /*#ifdef USE_NEXTION
  nextion.newNameset(config.station.name);
  nextion.bitrate(config.station.bitrate);
  nextion.bitratePic(ICON_NA);
#endif*/
}

char *split(char *str, const char *delim) {
  char *dmp = strstr(str, delim);
  if (dmp == NULL) return NULL;
  *dmp = '\0';
  return dmp + strlen(delim);
}

void Display::_title() {
  if (config.getMode() == PM_BLUETOOTH) {
    _layoutChange(false);
    if (_title2) _title2->setText("");
    return;
  }
  _layoutChange(player.isRunning());
  if (strlen(config.station.title) > 0) {
    char titleBuf[BUFLEN];
    strlcpy(titleBuf, config.station.title, sizeof(titleBuf));
    size_t _tl = strlen(titleBuf);
    char _lb[BUFLEN];
    size_t max_i = (_tl < (sizeof(_lb) - 1)) ? _tl : (sizeof(_lb) - 1);
    for (size_t i = 0; i < max_i; i++) {
      char ch = titleBuf[i];
      _lb[i] = (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
    }
    _lb[max_i] = 0;
    if (strcmp(config.station.title, const_PlConnect) == 0 || strstr(_lb, "connect") != NULL || strstr(_lb, "conect") != NULL) {
      _title1.setText("");
      if (_title2) _title2->setText("");
      _title1.moveTo({ title1Conf.widget.left, (uint16_t)(dsp.height() + 1), (int16_t)title1Conf.width });
      return;
    }
    char tmpbuf[BUFLEN];
    strlcpy(tmpbuf, config.station.title, sizeof(tmpbuf));
    char *stitle = split(tmpbuf, (const char *)" - ");
    if (stitle) {
      if (_title2) {
        _title1.setText(tmpbuf);
        _title2->setText(stitle);
      } else {
        _title1.setText(titleBuf);
      }
    } else {
      _title1.setText(config.station.title);
      if (_title2) _title2->setText("");
    }
    /*#ifdef USE_NEXTION
      nextion.newTitle(config.station.title);
    #endif*/

  } else {
    _title1.setText("");
    if (_title2) _title2->setText("");
  }
  if (player_on_track_change) player_on_track_change();
  pm.on_track_change();
}

void Display::_time(bool redraw) {
#if LIGHT_SENSOR != 255
  if (config.store.dspon) {
    config.store.brightness = AUTOBACKLIGHT(analogRead(LIGHT_SENSOR));
    config.setBrightness();
  }
#endif
  bool forceRedraw = redraw;
  if (_mode == SLEEPING) {
    config.clockOnly = true;
    config.isScreensaver = true;
    config.clockOnlyOffsetX = -37;
    forceRedraw = true;
  }
  _clock.draw(forceRedraw);
  /*#ifdef USE_NEXTION
    nextion.printClock(network.timeinfo);
  #endif*/
}

void Display::_volume() {
  if (_volbar) _volbar->setValue(config.store.volume);
#ifndef HIDE_VOL
  if (_voltxt) _voltxt->setText(config.store.volume, voltxtFmt);
#endif
  if (_mode == VOL) {
    _setReturnTicker(3);
    //_nums.setText(config.store.volume, numtxtFmt);
    uint8_t volPercent = (config.store.volume * 100 + 127) / 254;  // zmiana 08.05 wyswietlania volume z 1-254 na 1-100
    _nums.setText(volPercent, "%d");
  }
  /*#ifdef USE_NEXTION
    nextion.setVol(config.store.volume, _mode == VOL);
  #endif*/
}

void Display::flip() {
  dsp.flip();
}

void Display::invert() {
  dsp.invert();
}

void Display::setContrast() {
#if DSP_MODEL == DSP_NOKIA5110
  dsp.setContrast(config.store.contrast);
#endif
}

bool Display::deepsleep() {
#if defined(LCD_I2C) || defined(DSP_OLED) || BRIGHTNESS_PIN != 255
  dsp.sleep();
  return true;
#endif
  return false;
}

void Display::wakeup() {
#if defined(LCD_I2C) || defined(DSP_OLED) || BRIGHTNESS_PIN != 255
  dsp.wake();
#endif
}
//============================================================================================================================
#else  // !DUMMYDISPLAY
//============================================================================================================================
void Display::init() {
#ifdef USE_NEXTION
  nextion.begin(true);
#endif
}
void Display::_start() {
#ifdef USE_NEXTION
  //nextion.putcmd("page player");
  nextion.start();
#endif
  config.setTitle(const_PlReady);
}
void Display::putRequest(displayRequestType_e type, int payload) {
  if (type == DSP_START) _start();
#ifdef USE_NEXTION
  requestParams_t request;
  request.type = type;
  request.payload = payload;
  nextion.putRequest(request);
#else
  if (type == NEWMODE) mode((displayMode_e)payload);
#endif
}
//============================================================================================================================
#endif  // DUMMYDISPLAY
