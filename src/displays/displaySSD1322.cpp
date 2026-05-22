#include "../core/options.h"
#if DSP_MODEL==DSP_SSD1322

#include "displaySSD1322.h"
#include "../core/config.h"
#include "../core/network.h"
#include "fonts/TinyFont5.h"
#include "fonts/TinyFont6.h"

#define LOGO_WIDTH 200//21
#define LOGO_HEIGHT 32


#ifndef DEF_SPI_FREQ
  #define DEF_SPI_FREQ        16000000UL      /*  set it to 0 for system default */
#endif
#ifndef SSD1322_GRAYSCALE
  #define SSD1322_GRAYSCALE   false
#endif
const unsigned char logo [] PROGMEM=
{
	// 'logo, 200x32px VIRTUOSO
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x00, 0x08, 0x04, 
	0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x00, 0x1c, 0x0e, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x00, 
	0x1c, 0x0e, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x00, 0x3c, 0x0e, 0x00, 0x00, 0x00, 0x38, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x38, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 
	0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x1c, 0x00, 0x78, 0x00, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1c, 0x00, 0x70, 0x00, 0x00, 
	0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0xf0, 
	0x06, 0x03, 0xff, 0x80, 0xff, 0xc0, 0x20, 0x06, 0x00, 0x1f, 0x00, 0x03, 0xf0, 0x00, 0x07, 0xc0, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0xe0, 0x0e, 0x07, 0xff, 0x81, 0xff, 0xe0, 0x70, 
	0x07, 0x00, 0x7f, 0xc0, 0x07, 0xfe, 0x00, 0x1f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 
	0x00, 0xe0, 0x0e, 0x07, 0xff, 0x80, 0xff, 0xc0, 0x70, 0x07, 0x00, 0xff, 0xe0, 0x0f, 0xff, 0x00, 
	0x3f, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x01, 0xe0, 0x0e, 0x07, 0x03, 0x80, 0x38, 
	0x00, 0x70, 0x07, 0x01, 0xe1, 0xf0, 0x1e, 0x0f, 0x00, 0x78, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x07, 0x01, 0xc0, 0x0e, 0x07, 0x03, 0x80, 0x38, 0x00, 0x70, 0x07, 0x01, 0xc0, 0x70, 0x1c, 
	0x00, 0x00, 0xf0, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x81, 0xc0, 0x0e, 0x07, 0x01, 
	0x80, 0x38, 0x00, 0x70, 0x07, 0x03, 0x80, 0x70, 0x1c, 0x00, 0x00, 0xe0, 0x1c, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x03, 0x83, 0xc0, 0x0e, 0x07, 0x00, 0x00, 0x38, 0x00, 0x70, 0x07, 0x03, 0x80, 
	0x38, 0x1c, 0x00, 0x00, 0xe0, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x83, 0x80, 0x0e, 
	0x07, 0x00, 0x00, 0x38, 0x00, 0x70, 0x07, 0x03, 0x80, 0x38, 0x1f, 0x00, 0x00, 0xe0, 0x0e, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xc3, 0x80, 0x0e, 0x07, 0x00, 0x00, 0x38, 0x00, 0x70, 0x07, 
	0x03, 0x80, 0x38, 0x0f, 0xc0, 0x00, 0xe0, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xc7, 
	0x80, 0x0e, 0x07, 0x00, 0x00, 0x38, 0x00, 0x70, 0x07, 0x03, 0x80, 0x38, 0x07, 0xf0, 0x00, 0xe0, 
	0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xc7, 0x00, 0x0e, 0x07, 0x00, 0x00, 0x38, 0x00, 
	0x70, 0x07, 0x03, 0x80, 0x38, 0x01, 0xfc, 0x00, 0xe0, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0xe7, 0x00, 0x0e, 0x07, 0x00, 0x00, 0x38, 0x00, 0x70, 0x07, 0x03, 0x80, 0x38, 0x00, 0x7f, 
	0x00, 0xe0, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xef, 0x00, 0x0e, 0x07, 0x00, 0x00, 
	0x38, 0x00, 0x70, 0x07, 0x03, 0x80, 0x38, 0x00, 0x1f, 0x80, 0xe0, 0x0e, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0xee, 0x00, 0x0e, 0x07, 0x00, 0x00, 0x38, 0x00, 0x70, 0x07, 0x03, 0x80, 0x38, 
	0x00, 0x07, 0x80, 0xe0, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x00, 0x0e, 0x07, 
	0x00, 0x00, 0x38, 0x00, 0x70, 0x07, 0x03, 0x80, 0x38, 0x00, 0x03, 0xc0, 0xe0, 0x1e, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x00, 0x0e, 0x07, 0x00, 0x00, 0x38, 0x00, 0x70, 0x07, 0x03, 
	0x80, 0x70, 0x00, 0x01, 0xc0, 0xe0, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0x00, 
	0x0e, 0x07, 0x00, 0x00, 0x38, 0x00, 0x70, 0x07, 0x01, 0xc0, 0xf0, 0x08, 0x03, 0x80, 0xf0, 0x3c, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x0e, 0x07, 0x00, 0x00, 0x38, 0x00, 0x3c, 
	0x07, 0x01, 0xf1, 0xe0, 0x1f, 0x83, 0x80, 0x7c, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x3c, 0x00, 0x0e, 0x07, 0x00, 0x00, 0x38, 0x00, 0x3f, 0xff, 0x00, 0xff, 0xe0, 0x1f, 0xff, 0x80, 
	0x3f, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x00, 0x0e, 0x07, 0x00, 0x00, 0x38, 
	0x00, 0x1f, 0xff, 0x00, 0x7f, 0x80, 0x07, 0xff, 0x00, 0x1f, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x18, 0x00, 0x04, 0x03, 0x00, 0x00, 0x10, 0x00, 0x07, 0xfe, 0x00, 0x1e, 0x00, 0x00, 
	0x7c, 0x00, 0x07, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};


#if DSP_HSPI
  DspCore::DspCore(): Jamis_SSD1322(256, 64, &SPI2, TFT_DC, TFT_RST, TFT_CS, DEF_SPI_FREQ) {}
#else
  DspCore::DspCore(): Jamis_SSD1322(256, 64, &SPI, TFT_DC, TFT_RST, TFT_CS, DEF_SPI_FREQ) {}
#endif

#include "tools/utf8PL.h"

static uint16_t _prevClockTop = 0;
static int16_t _clockOnlyTimeY1 = 0;
static uint16_t _clockOnlyTimeH = 0;
static int16_t _clockOnlyColonY1 = 0;
static uint16_t _clockOnlyColonW = 0;
static int16_t _clockOnlyColonShiftX = 0;
static uint8_t _clockOnlyDigitAdvance = 14;
static uint8_t _clockOnlyScale = 2;
static uint16_t _clockOnlyHHW = 0;
static uint16_t _clockOnlyMMW = 0;
static uint8_t _clockOnlyColonPad = 2;
void DspCore::initDisplay() {
#if !SSD1322_GRAYSCALE
  #include "tools/oledcolorfix.h"
#else
    config.theme.background = TFT_BG;
  #if DSP_INVERT_TITLE
    config.theme.meta       = GRAY_9;
    config.theme.metabg     = TFT_BG;
    config.theme.metafill   = TFT_BG;
    config.theme.clock      = GRAY_2;
    config.theme.clockbg    = TFT_BG;


  #else
    config.theme.meta       = TFT_BG;
    config.theme.metabg     = GRAY_9;
    config.theme.metafill   = GRAY_9;
    config.theme.clock      = GRAY_2;
    config.theme.clockbg    = TFT_FG;
  #endif  
   //czarny tlo
    config.theme.weather    = GRAY_3;
    config.theme.title1     = GRAY_3;  // ciemniejszy odcień dla wykonawcy/tytułu (mocny kontrast)
    config.theme.title2     = GRAY_3;
    config.theme.rssi       = config.theme.clock;  // WiFi w kolorze zegara/bitrate
    config.theme.ip         = GRAY_3;
    config.theme.vol        = GRAY_9;
    config.theme.bitrate    = GRAY_2;
    config.theme.digit      = GRAY_9;
    config.theme.buffer     = TFT_FG;
    config.theme.volbarout  = GRAY_9;
    config.theme.volbarin   = GRAY_9;
    config.theme.plcurrent     = GRAY_9;
    config.theme.plcurrentbg   = config.theme.background;
    config.theme.plcurrentfill = config.theme.background;
  
    for(byte i=0;i<5;i++) config.theme.playlist[i] = GRAY_1;
#endif  //!SSD1322_GRAYSCALE

  begin();
  cp437(true);
  flip();
  invert();
  setTextWrap(false);
  
  plItemHeight = playlistConf.widget.textsize*(CHARHEIGHT-1)+playlistConf.widget.textsize*4;
  plTtemsCount = round((float)height()/plItemHeight);
  if(plTtemsCount%2==0) plTtemsCount++;
  plCurrentPos = plTtemsCount/2;
  plYStart = (height() / 2 - plItemHeight / 2) - plItemHeight * (plTtemsCount - 1) / 2 + playlistConf.widget.textsize*2;
}

void DspCore::drawLogo(uint16_t top) {
  clearDisplay();
  setTextColor(TFT_LOGO, TFT_BG);
  setFont(&FreeSans9pt7b);
  const char* t = "Virtuoso";
  int16_t x1, y1; uint16_t w, h;
  getTextBounds(t, 0, 0, &x1, &y1, &w, &h);
  int16_t x = (width() - (int16_t)w) / 2;
  int16_t y = (height() - (int16_t)h) / 2;
  setCursor(x, y + h);
  print(t);
  display();
}


void DspCore::printPLitem(uint8_t pos, const char* item, ScrollWidget& current){
  setTextSize(playlistConf.widget.textsize);
  if (pos == plCurrentPos) {
    current.setText(item);
  } else {
    uint8_t plColor = (abs(pos - plCurrentPos)-1)>4?4:abs(pos - plCurrentPos)-1;
    setTextColor(config.theme.playlist[plColor], config.theme.background);
    setCursor(TFT_FRAMEWDT, plYStart + pos * plItemHeight);
    fillRect(0, plYStart + pos * plItemHeight - 1, width(), plItemHeight - 2, config.theme.background);
    print(utf8PL(item, true));
  }
}

void DspCore::drawPlaylist(uint16_t currentItem) {
  uint8_t lastPos = config.fillPlMenu(currentItem - plCurrentPos, plTtemsCount);
  if(lastPos<plTtemsCount){
    fillRect(0, lastPos*plItemHeight+plYStart, width(), height()/2, config.theme.background);
  }
}

void DspCore::clearDsp(bool black) {
  //fillScreen(TFT_BG);
  clearDisplay();
  _oldtimeleft = 0;
  _olddateleft = 0;
  _oldtimewidth = 0;
  _olddatewidth = 0;
  _oldTimeBuf[0] = 0;
  _oldDateBuf[0] = 0;
  _prevClockTop = 0;
}

GFXglyph *pgm_read_glyph_ptr(const GFXfont *gfxFont, uint8_t c) {
  return gfxFont->glyph + c;
}
//------------------------funkcja wyliczania wielkosci czcionki -----------------------
uint8_t DspCore::_charWidth(unsigned char c){
  return CHARWIDTH;
}

uint16_t DspCore::textWidth(const char *txt){
  uint16_t w = 0, l=strlen(txt);
  for(uint16_t c=0;c<l;c++) w+=_charWidth(txt[c]);
  return w;
}

void DspCore::_getTimeBounds() {
  if (config.clockOnly) {
#if CLOCKFONT_MONO
    setFont(&DS_DIGI15pt7b);
#else
    setFont(&DS_DIGI15pt7b);
#endif
    setTextSize(_clockOnlyScale);
    _clockOnlyDigitAdvance = pgm_read_byte(&DS_DIGI15pt7bGlyphs['0' - 0x20].xAdvance);
    int16_t x1, y1;
    uint16_t w, h;
    int16_t minY = 32767;
    int16_t maxY = -32768;
    const char testChars[] = "0123456789:";
    for (uint8_t i = 0; testChars[i] != 0; i++) {
      char s[2] = { testChars[i], 0 };
      getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
      if (y1 < minY) minY = y1;
      int16_t y2 = (int16_t)(y1 + (int16_t)h);
      if (y2 > maxY) maxY = y2;
    }
    if (minY == 32767 || maxY == -32768) {
      minY = 0;
      maxY = 0;
    }
    _clockOnlyTimeY1 = minY;
    _clockOnlyTimeH = (uint16_t)max<int16_t>(0, maxY - minY);

    getTextBounds(":", 0, 0, &x1, &y1, &w, &h);
    _clockOnlyColonY1 = y1;
    uint8_t colonAdvance = pgm_read_byte(&DS_DIGI15pt7bGlyphs[':' - 0x20].xAdvance);
    _clockOnlyColonW = (uint16_t)colonAdvance * _clockOnlyScale;
    _clockOnlyColonShiftX = 0;
    uint16_t digitStep = (uint16_t)_clockOnlyDigitAdvance * _clockOnlyScale;
    _clockOnlyHHW = digitStep * 2;
    _clockOnlyMMW = digitStep * 2;
    // visual centering of colon
    uint16_t colonPad = (uint16_t)_clockOnlyColonPad * _clockOnlyScale;
    _dotsLeft = _clockOnlyHHW + colonPad + _clockOnlyColonW + colonPad; 
    _timewidth = _dotsLeft + _clockOnlyMMW;
    return;
  }
  uint8_t scale = config.isScreensaver ? 2 : 1;
  _timewidth = strlen(_timeBuf) * (CHARWIDTH * scale);
  char buf[4];
  strftime(buf, 4, "%H:", &network.timeinfo);
  _dotsLeft = strlen(buf) * (CHARWIDTH * scale);
}

void DspCore::_clockSeconds(){
  uint8_t scale = config.clockOnly ? _clockOnlyScale : (config.isScreensaver ? 2 : 1);
  if (config.clockOnly) {
#if CLOCKFONT_MONO
    setFont(&DS_DIGI15pt7b);
#else
    setFont(&DS_DIGI15pt7b);
#endif
    setTextSize(_clockOnlyScale);
  } else {
    setTextSize(scale);
    setFont();
  }
  bool even = (network.timeinfo.tm_sec % 2 == 0);
  if (config.clockOnly) {
    setTextColor(even ? config.theme.clock : config.theme.background, config.theme.background);
  } else {
    setTextColor(even ? config.theme.clock : config.theme.clockbg, config.theme.clockbg);
  }
  if (config.clockOnly) {
    uint16_t colonPad = (uint16_t)_clockOnlyColonPad * _clockOnlyScale;
    setCursor(_timeleft + _clockOnlyHHW + colonPad + 1, clockTop - _clockOnlyTimeY1);
  } else {
    setCursor(_timeleft + _dotsLeft - (CHARWIDTH * scale), clockTop);
  }
  print(":");
}

void DspCore::_clockDate(){
  //uint8_t scale = config.isScreensaver ? 2 : 1;
  uint8_t scale = 1;
  int16_t dateTop = clockTop + (CHARHEIGHT * (config.isScreensaver ? 2 : 1)) + 2;
  if (config.isScreensaver) {
    dsp.fillRect(0, dateTop - 1, width(), CHARHEIGHT + 2, config.theme.background);
  } else if (_olddateleft > 0) {
    dsp.fillRect(_olddateleft, dateTop, _olddatewidth + 8, CHARHEIGHT, config.theme.background);
  }
  setTextColor(config.theme.clock, config.theme.background);
  setCursor(_dateleft, dateTop);
  setTextSize(scale);
  if(config.isScreensaver) print(_dateBuf);
  strlcpy(_oldDateBuf, _dateBuf, sizeof(_dateBuf));
  _olddatewidth = _datewidth;
  _olddateleft = _dateleft;
}

void DspCore::_clockTime(){
  uint8_t scale = config.clockOnly ? _clockOnlyScale : (config.isScreensaver ? 2 : 1);
  if(_oldtimeleft>0 && !CLOCKFONT_MONO){
    uint16_t pad = config.clockOnly ? (uint16_t)_clockOnlyDigitAdvance * _clockOnlyScale : (CHARWIDTH * scale * 2 + 2);
    dsp.fillRect(_oldtimeleft, _prevClockTop, _oldtimewidth + pad, clockTimeHeight, config.theme.background);
  }
  if(config.isScreensaver){
    //_timeleft = (width() - _timewidth) / 2;
    //_timeleft = width() - _timewidth - clockRightSpace - 2;
    _timeleft = (width() - _timewidth) / 2;
  }else{
    _timeleft = width() - _timewidth - clockRightSpace - 2;
  }
  if (config.clockOnly) {
    _timeleft += config.clockOnlyOffsetX;
  }
  if (config.clockOnly) {
#if CLOCKFONT_MONO
    setFont(&DS_DIGI15pt7b);
#else
    setFont(&DS_DIGI15pt7b);
#endif
    setTextSize(_clockOnlyScale);
  } else {
    setTextSize(scale);
  }
  
  if(CLOCKFONT_MONO && config.clockOnly) {
    setCursor(_timeleft, clockTop - _clockOnlyTimeY1);
    setTextColor(config.theme.clockbg, config.theme.background);
    print("88 88");
  }
  clearClock();
  if (!config.clockOnly) setFont();
  setTextColor(config.theme.clock, config.theme.background);
  if (config.clockOnly) {
    char hh[3], mm[3];
    strftime(hh, sizeof(hh), "%H", &network.timeinfo);
    strftime(mm, sizeof(mm), "%M", &network.timeinfo);
    uint16_t digitStep = (uint16_t)_clockOnlyDigitAdvance * _clockOnlyScale;
    int16_t y = clockTop - _clockOnlyTimeY1;
    int16_t minX = (int16_t)((int32_t)_timeleft + (int32_t)_dotsLeft);
    if (minX < 0) minX = 0;
    if ((int32_t)minX + (int32_t)digitStep * 2 > (int32_t)width()) {
      minX = (int16_t)max<int32_t>(0, (int32_t)width() - (int32_t)digitStep * 2);
    }
    setCursor(_timeleft, y);
    print(hh[0]);
    setCursor(_timeleft + digitStep, y);
    print(hh[1]);
    setCursor(minX, y);
    print(mm[0]);
    setCursor((int16_t)(minX + (int16_t)digitStep), y);
    print(mm[1]);
  } else {
    setCursor(_timeleft, clockTop);
    print(_timeBuf);
  }
  setFont();

  strlcpy(_oldTimeBuf, _timeBuf, sizeof(_timeBuf));
  _oldtimewidth = _timewidth;
  _oldtimeleft = _timeleft;

  snprintf(_buffordate, sizeof(_buffordate), "%2d %s %d", network.timeinfo.tm_mday, mnths[network.timeinfo.tm_mon], network.timeinfo.tm_year+1900);
  strlcpy(_dateBuf, utf8PL(_buffordate, true), sizeof(_dateBuf));
  {
    //uint8_t scale2 = config.isScreensaver ? 2 : 1;
    uint8_t scale2 = 1;
    _datewidth = strlen(_dateBuf) * CHARWIDTH * scale2;
  }
  _dateleft = config.isScreensaver ? (_timeleft + (_timewidth - _datewidth) / 2) : (width() - clockRightSpace - _datewidth);

}

void DspCore::printClock(uint16_t top, uint16_t rightspace, uint16_t timeheight, bool redraw){
  uint16_t prevTop = clockTop;
  clockRightSpace = rightspace;
  clockTimeHeight = timeheight;
  strftime(_timeBuf, sizeof(_timeBuf), "%H:%M", &network.timeinfo);
  bool force = redraw;
  bool needBounds = (strcmp(_oldTimeBuf, _timeBuf) != 0) || force;
  if (config.clockOnly && _clockOnlyTimeH == 0) needBounds = true;
  if (needBounds) {
    _getTimeBounds();
  }
  uint16_t desiredTop = top;
  if (config.clockOnly) {
    uint16_t h = (uint16_t)_clockOnlyTimeH;
    uint16_t bottomPad = (uint16_t)(2 * _clockOnlyScale);
    uint16_t drawH = h + bottomPad + 2;
    uint16_t screenH = height();
    desiredTop = screenH > drawH ? (uint16_t)((screenH - drawH) / 2) : 0;
    if (desiredTop + h + bottomPad > screenH) desiredTop = screenH > (uint16_t)(h + bottomPad) ? (uint16_t)(screenH - (h + bottomPad)) : 0;
    uint16_t minH = h + bottomPad + 2;
    if (clockTimeHeight < minH) clockTimeHeight = minH;
  }
  _prevClockTop = prevTop;
  clockTop = desiredTop;
  if (_prevClockTop != clockTop) {
    if (config.clockOnly) {
      clearDisplay();
      _oldtimeleft = 0;
      _olddateleft = 0;
      _oldTimeBuf[0] = 0;
      _oldDateBuf[0] = 0;
      force = true;
    } else {
      force = true;
    }
  }
  if(strcmp(_oldTimeBuf, _timeBuf)!=0 || force){
    _getTimeBounds();
    if (config.clockOnly) {
      uint16_t bottomPad = (uint16_t)(2 * _clockOnlyScale);
      uint16_t minH = _clockOnlyTimeH + bottomPad + 2;
      if (clockTimeHeight < minH) clockTimeHeight = minH;
    }
    _clockTime();
    if(config.isScreensaver && !config.clockOnly){
      if(strcmp(_oldDateBuf, _dateBuf)!=0 || force) _clockDate();
    }
  }
  _clockSeconds();
}

void DspCore::clearClock(){
  uint8_t scale = config.clockOnly ? _clockOnlyScale : (config.isScreensaver ? 2 : 1);
  uint16_t pad = config.clockOnly ? (uint16_t)_clockOnlyDigitAdvance * _clockOnlyScale : (CHARWIDTH * scale);
  int16_t x = (int16_t)_oldtimeleft - (int16_t)pad;
  int16_t y = (int16_t)_prevClockTop;
  int16_t w = (int16_t)_oldtimewidth + (int16_t)pad * 2 + 2;
  int16_t h = (int16_t)clockTimeHeight;
  int16_t maxW = (int16_t)width();
  int16_t maxH = (int16_t)height();
  if (w <= 0 || h <= 0) return;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x >= maxW || y >= maxH) return;
  if (x + w > maxW) w = maxW - x;
  if (y + h > maxH) h = maxH - y;
  if (w <= 0 || h <= 0) return;
  dsp.fillRect(x, y, w, h, config.theme.background);
}

void DspCore::startWrite(void) {
  //TAKE_MUTEX();
  Jamis_SSD1322::startWrite();
}

void DspCore::endWrite(void) {
  Jamis_SSD1322::endWrite();
  //GIVE_MUTEX();
}

void DspCore::loop(bool force) {
  display();
  delay(5);
}

void DspCore::charSize(uint8_t textsize, uint8_t& width, uint16_t& height){
  width = textsize * CHARWIDTH;
  height = textsize * CHARHEIGHT;
}

void DspCore::setTextSize(uint8_t s){
  Adafruit_GFX::setTextSize(s);
}

void DspCore::flip(){
  setRotation(config.store.flipscreen?2:0);
}

void DspCore::invert(){
  invertDisplay(config.store.invertdisplay);
}

void DspCore::sleep(void) { oled_command(SSD1322_DISPLAYOFF); }
void DspCore::wake(void) { oled_command(SSD1322_DISPLAYON); }

void DspCore::writePixel(int16_t x, int16_t y, uint16_t color) {
  if(_clipping){
    if ((x < _cliparea.left) || (x >= _cliparea.left + _cliparea.width) || (y < _cliparea.top) || (y >= _cliparea.top + _cliparea.height)) return;
  }
  Jamis_SSD1322::writePixel(x, y, color);
}

void DspCore::writeFillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  if (w <= 0 || h <= 0) return;
  if (_clipping) {
    int16_t cx0 = _cliparea.left;
    int16_t cy0 = _cliparea.top;
    int16_t cx1 = _cliparea.left + _cliparea.width;
    int16_t cy1 = _cliparea.top + _cliparea.height;

    int16_t x0 = x < cx0 ? cx0 : x;
    int16_t y0 = y < cy0 ? cy0 : y;
    int16_t x1 = (int16_t)(x + w);
    int16_t y1 = (int16_t)(y + h);
    if (x1 > cx1) x1 = cx1;
    if (y1 > cy1) y1 = cy1;

    int16_t nw = (int16_t)(x1 - x0);
    int16_t nh = (int16_t)(y1 - y0);
    if (nw <= 0 || nh <= 0) return;
    Jamis_SSD1322::writeFillRect(x0, y0, nw, nh, color);
    return;
  }
  Jamis_SSD1322::writeFillRect(x, y, w, h, color);
}

void DspCore::setClipping(clipArea ca){
  _cliparea = ca;
  _clipping = true;
}

void DspCore::clearClipping(){
  _clipping = false;
}

void DspCore::setNumFont(){
  setTextSize(1);
}

#endif
