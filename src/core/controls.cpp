#include "Arduino.h"
#include "controls.h"
#include "options.h"
#include "config.h"
#include "player.h"
#include "display.h"
#include "network.h"
#include "netserver.h"

long encOldPosition = 0;
long enc2OldPosition = 0;
int lpId = -1;

#ifndef BT_KEY_PIN
#define BT_KEY_PIN 255
#endif
#ifndef BT_KEY_ACTIVE_LOW
#define BT_KEY_ACTIVE_LOW false
#endif
#ifndef BT_KEY_CLICK_MS
#define BT_KEY_CLICK_MS 200
#endif

static inline void _setBtKey(bool pressed) {
#if BT_KEY_PIN != 255
  uint8_t v = pressed ? (BT_KEY_ACTIVE_LOW ? LOW : HIGH) : (BT_KEY_ACTIVE_LOW ? HIGH : LOW);
  digitalWrite(BT_KEY_PIN, v);
#else
  (void)pressed;
#endif
}

static inline void _initBtKeyPin() {
#if BT_KEY_PIN != 255
  pinMode(BT_KEY_PIN, OUTPUT);
  _setBtKey(false);
#endif
}

static inline void _btKeyClick() {
#if BT_KEY_PIN != 255
  _setBtKey(true);
  delay(BT_KEY_CLICK_MS);
  _setBtKey(false);
#endif
}
#if TOUCH_PIN != 255
static uint16_t touchBaseline = 0;
static bool touchPressed = false;
#endif

#define ISPUSHBUTTONS BTN_LEFT != 255 || BTN_CENTER != 255 || BTN_RIGHT != 255 || ENC_BTNB != 255 || BTN_UP != 255 || BTN_DOWN != 255 || ENC2_BTNB != 255 || BTN_MODE != 255
#if ISPUSHBUTTONS
#include "../OneButton/OneButton.h"
OneButton button[]{ { BTN_LEFT, true, BTN_INTERNALPULLUP }, { BTN_CENTER, true, BTN_INTERNALPULLUP }, { BTN_RIGHT, true, BTN_INTERNALPULLUP }, { ENC_BTNB, true, ENC_INTERNALPULLUP }, { BTN_UP, true, BTN_INTERNALPULLUP }, { BTN_DOWN, true, BTN_INTERNALPULLUP }, { ENC2_BTNB, true, ENC2_INTERNALPULLUP }, { BTN_MODE, true, BTN_INTERNALPULLUP } };
constexpr uint8_t nrOfButtons = sizeof(button) / sizeof(button[0]);
#endif

#if ENC_HALFQUARD == false
#define ENCODER_STEPS 4
#elif ENC_HALFQUARD == true
#define ENCODER_STEPS 2
#elif ENC_HALFQUARD == 255
#define ENCODER_STEPS 1
#endif

#if ENC2_HALFQUARD == false
#define ENCODER2_STEPS 4
#elif ENC2_HALFQUARD == true
#define ENCODER2_STEPS 2
#elif ENC2_HALFQUARD == 255
#define ENCODER2_STEPS 1
#endif

//#if (ENC_BTNL != 255 && ENC_BTNR != 255) || (ENC2_BTNL != 255 && ENC2_BTNR != 255)
#if (ENC_BTNL != 255 && ENC_BTNR != 255)
yoEncoder encoder = yoEncoder(ENC_BTNL, ENC_BTNR, ENCODER_STEPS, ENC_INTERNALPULLUP);
#endif

#if (ENC2_BTNL != 255 && ENC2_BTNR != 255)
yoEncoder encoder2 = yoEncoder(ENC2_BTNL, ENC2_BTNR, ENCODER2_STEPS, ENC2_INTERNALPULLUP);
#endif
//#endif

#if (TS_MODEL != TS_MODEL_UNDEFINED) && (DSP_MODEL != DSP_DUMMY)
#include "touchscreen.h"
TouchScreen touchscreen;
#endif

#if IR_PIN != 255
#include <assert.h>

#include "../IRremoteESP8266/IRrecv.h"
#include "../IRremoteESP8266/IRremoteESP8266.h"
#include "../IRremoteESP8266/IRac.h"
#include "../IRremoteESP8266/IRtext.h"
#include "../IRremoteESP8266/IRutils.h"

uint8_t irVolRepeat = 0;
const uint16_t kCaptureBufferSize = 1024;
const uint8_t kTimeout = IR_TIMEOUT;
const uint16_t kMinUnknownSize = 12;
#define LEGACY_TIMING_INFO false

IRrecv irrecv(IR_PIN, kCaptureBufferSize, kTimeout, true);
decode_results irResults;
static bool irPwrResumeAfter = false;
static bool irMuted = false;
static uint8_t irMuteSavedVolume = 0;
static bool irDimmed = false;
static uint8_t irBrightnessSaved = 100;
#endif
//----------------------------------------------------------------
#if ENC_BTNL != 255
void IRAM_ATTR readEncoderISR() {
  if ((SDC_CS == 255 && display.mode() == LOST) || display.mode() == UPDATING) return;
  encoder.readEncoder_ISR();
}
#endif

#if ENC2_BTNL != 255
void IRAM_ATTR readEncoder2ISR() {
  if ((SDC_CS == 255 && display.mode() == LOST) || display.mode() == UPDATING) return;
  encoder2.readEncoder_ISR();
}
#endif
//----------------------------------------------------------------
void initControls() {
  _initBtKeyPin();

#if ENC_BTNL != 255
  encoder.begin();
  encoder.setup(readEncoderISR);
  encoder.setBoundaries(0, 254, true);
  encoder.setAcceleration(config.store.encacc);
#endif

#if ENC2_BTNL != 255
  encoder2.begin();
  encoder2.setup(readEncoder2ISR);
  encoder2.setBoundaries(0, 254, true);
  encoder2.setAcceleration(config.store.encacc);
#endif

#if ISPUSHBUTTONS
  for (int i = 0; i < nrOfButtons; i++) {
    if ((i == 0 && BTN_LEFT == 255) || (i == 1 && BTN_CENTER == 255) || (i == 2 && BTN_RIGHT == 255) || (i == 3 && ENC_BTNB == 255) || (i == 4 && BTN_UP == 255) || (i == 5 && BTN_DOWN == 255) || (i == 6 && ENC2_BTNB == 255) || (i == 7 && BTN_MODE == 255)) continue;
    button[i].attachClick([](void* p) {
      onBtnClick((int)p);
    },
                          (void*)i);
    button[i].attachDoubleClick([](void* p) {
      onBtnDoubleClick((int)p);
    },
                                (void*)i);
    button[i].attachLongPressStart([](void* p) {
      onBtnLongPressStart((int)p);
    },
                                   (void*)i);
    button[i].attachLongPressStop([](void* p) {
      onBtnLongPressStop((int)p);
    },
                                  (void*)i);
    button[i].setClickTicks(BTN_CLICK_TICKS);
    uint16_t pressTicks = BTN_PRESS_TICKS;
    if (i == 3 || i == 6) pressTicks = 3000;
    button[i].setPressTicks(pressTicks);
  }
#endif

#if (TS_MODEL != TS_MODEL_UNDEFINED) && (DSP_MODEL != DSP_DUMMY)
  touchscreen.init();
#endif

#if IR_PIN != 255
  pinMode(IR_PIN, INPUT);
  assert(irutils::lowLevelSanityCheck() == 0);
#if DECODE_HASH
  irrecv.setUnknownThreshold(kMinUnknownSize);
#endif  // DECODE_HASH
  irrecv.setTolerance(config.store.irtlp);
  irrecv.enableIRIn();
#endif  // IR_PIN!=255
#if TOUCH_PIN != 255
  {
    uint32_t acc = 0;
    const uint8_t n = 16;
    for (uint8_t i = 0; i < n; i++) {
      acc += touchRead(TOUCH_PIN);
      delay(5);
    }
    touchBaseline = acc / n;
  }
#endif
}
//----------------------------------------------------------------
void loopControls() {
  if (display.mode() == UPDATING || display.mode() == SDCHANGE) return;
  if (SDC_CS == 255 && display.mode() == LOST) return;
  if (ctrls_on_loop) ctrls_on_loop();

#if ENC_BTNL != 255
  encoder1Loop();
#endif

#if ENC2_BTNL != 255
  encoder2Loop();
#endif

#if ISPUSHBUTTONS
  for (unsigned i = 0; i < nrOfButtons; i++) {
    if ((i == 0 && BTN_LEFT == 255) || (i == 1 && BTN_CENTER == 255) || (i == 2 && BTN_RIGHT == 255) || (i == 3 && ENC_BTNB == 255) || (i == 4 && BTN_UP == 255) || (i == 5 && BTN_DOWN == 255) || (i == 6 && ENC2_BTNB == 255)) continue;
    button[i].tick();
    if (lpId >= 0) {
      if (DSP_MODEL == DSP_DUMMY && (lpId == 4 || lpId == 5)) continue;
      onBtnDuringLongPress(lpId);
    }
  }
#endif

#if IR_PIN != 255
  irLoop();
#endif
#if (TS_MODEL != TS_MODEL_UNDEFINED) && (DSP_MODEL != DSP_DUMMY)
  if (network.status == CONNECTED || network.status == SDREADY) touchscreen.loop();
#endif
#if TOUCH_PIN != 255
  {
    uint16_t v = touchRead(TOUCH_PIN);
    bool active = (v + TOUCH_THRESHOLD) < touchBaseline;
    if (!active) {
      touchBaseline = (touchBaseline * 7 + v) / 8;
    }
    if (!touchPressed && active) {
      touchPressed = true;
      onBtnLongPressStart(EVT_BTNMODE);
    } else if (touchPressed && !active) {
      touchPressed = false;
      onBtnLongPressStop(EVT_BTNMODE);
    }
  }
#endif

}
//----------------------------------------------------------------
#if ENC_BTNL != 255 || ENC2_BTNL != 255
void encodersLoop(yoEncoder* enc, bool first) {
  if (network.status != CONNECTED && network.status != SDREADY) return;
  if (display.mode() == LOST) return;
  int8_t encoderDelta = enc->encoderChanged();
  if (encoderDelta != 0) {
#if defined(DUMMYDISPLAY) && !defined(USE_NEXTION)
    uint8_t encBtnState = digitalRead(first ? ENC_BTNB : ENC2_BTNB);
    first = first ? (first && encBtnState) : (!encBtnState);
    if (first) {
      int nv = config.store.volume + encoderDelta;
      if (nv < 0) nv = 0;
      if (nv > 254) nv = 254;
      player.setVol((uint8_t)nv);
    } else {
      if (encoderDelta > 0) player.next();
      else player.prev();
    }
#else
    if (display.mode() == STATIONS) {
      controlsEvent(encoderDelta > 0);
    } else {
      if (display.mode() == PLAYER) {
        display.putRequest(NEWMODE, STATIONS);
      } else {
        controlsEvent(encoderDelta > 0, encoderDelta);
      }
    }
#endif
  }
}
#endif
//----------------------------------------------------------------
#if ENC_BTNL != 255
void encoder1Loop() {
  encodersLoop(&encoder, true);
}
#endif
//----------------------------------------------------------------
#if ENC2_BTNL != 255
void encoder2Loop() {
  encodersLoop(&encoder2, false);
}
#endif
//----------------------------------------------------------------
#if IR_PIN != 255
void irBlink() {
  if (REAL_LEDBUILTIN == 255) return;
  if (player.status() == STOPPED) {
    for (uint8_t i = 0; i < 7; i++) {
      digitalWrite(REAL_LEDBUILTIN, !digitalRead(REAL_LEDBUILTIN));
      delay(100);
    }
  }
}
//----------------------------------------------------------------
void irNumber(uint8_t num) {
  uint16_t s;
  if (display.numOfNextStation == 0 && num == 0) return;
  display.putRequest(NEWMODE, NUMBERS);
  if (display.numOfNextStation > UINT16_MAX / 10) return;
  s = display.numOfNextStation * 10 + num;
  if (s > config.store.countStation) return;
  display.numOfNextStation = s;
  display.putRequest(NEXTSTATION, s);
}
//----------------------------------------------------------------
void irLoop() {
  if (irrecv.decode(&irResults)) {
    // Ignoruj szum (UNKNOWN protocol)
    if (irResults.decode_type == decode_type_t::UNKNOWN) return;
    
    static const uint64_t IR_REPEAT_VALUE = 0xFFFFFFFFULL;
    static uint64_t lastNonVolValue = 0;
    static uint32_t lastNonVolMs = 0;
    if (netserver.irRecordEnable) {
      Serial.print(resultToHumanReadableBasic(&irResults));
      Serial.println("--------------------------");
      if (!irResults.repeat && irResults.value != 0 && irResults.value != IR_REPEAT_VALUE) {
        for (int t = 0; t < IR_SLOTS_TOTAL; t++) {
          for (int k = 0; k < 3; k++) {
            if (t == config.irindex && k == config.irchck) continue;
            if (config.getIRVal(t, k) == irResults.value) config.setIRVal(t, k, 0);
          }
        }
        config.setIRVal(config.irindex, config.irchck, irResults.value);
        config.saveIR();
        netserver.irValsToWs();
        netserver.irToWs(typeToString(irResults.decode_type, irResults.repeat).c_str(), irResults.value);
      }
      return;
    }
    if (irResults.value == 0) return;
    if (!irResults.repeat /* && irResults.command!=0*/) {
      irVolRepeat = 0;
    }
    switch (irVolRepeat) {
      case 1:
        {
          controlsEvent(display.mode() == STATIONS ? false : true);
          break;
        }
      case 2:
        {
          controlsEvent(display.mode() == STATIONS ? true : false);
          break;
        }
    }
    if (irResults.value == IR_REPEAT_VALUE) return;
    if (irResults.repeat && irVolRepeat == 0) {
      uint32_t now = millis();
      if (irResults.value == lastNonVolValue && (now - lastNonVolMs) < 400) return;
    }
    for (int target = 0; target < IR_SLOTS_TOTAL; target++) {
      for (int j = 0; j < 3; j++) {
        if (config.getIRVal(target, j) == irResults.value) {
          if (network.status != CONNECTED && network.status != SDREADY && target != IR_AST && target != IR_PWR && target != IR_HOME && target != IR_BACK && target != IR_MUTE && target != IR_HASH && target != IR_MENU) return;
          if (target != IR_AST && target != IR_HOME && target != IR_BACK && target != IR_MUTE && target != IR_HASH && target != IR_MENU && display.mode() == LOST) return;
          if ((display.mode() == SCREENSAVER || display.mode() == SCREENBLANK) && target != IR_PWR) {
            display.putRequest(NEWMODE, PLAYER);
          }
          if (target != IR_UP && target != IR_DOWN) {
            lastNonVolValue = irResults.value;
            lastNonVolMs = millis();
          }
          switch (target) {
            case IR_PLAY:
              {
                irBlink();
                if (display.mode() == NUMBERS) {
                  display.putRequest(NEWMODE, PLAYER);
                  player.sendCommand({ PR_PLAY, display.numOfNextStation });
                  display.numOfNextStation = 0;
                  break;
                }
                onBtnClick(1);
                break;
              }
            case IR_PWR:
              {
                irBlink();
                if (!config.store.dspon || display.mode() == SLEEPING) {
                  config.setDspOn(true);
                  config.screensaverTicks = SCREENSAVERSTARTUPDELAY;
                  config.screensaverPlayingTicks = SCREENSAVERSTARTUPDELAY;
                  config.isScreensaver = false;
                  display.putRequest(NEWMODE, CLEAR);
                  display.putRequest(NEWMODE, PLAYER);
                  if (irPwrResumeAfter) {
                    player.sendCommand({ PR_PLAY, config.lastStation() });
                  }
                  return;
                }
                irPwrResumeAfter = player.isRunning();
                player.sendCommand({ PR_STOP, 0 });
                display.putRequest(NEWMODE, SLEEPING);
                return;
              }
            case IR_PREV:
              {
                player.prev();
                irVolRepeat = 0;
                break;
              }
            case IR_NEXT:
              {
                player.next();
                irVolRepeat = 0;
                break;
              }
            case IR_UP:
              {
                controlsEvent(display.mode() == STATIONS ? false : true);
                irVolRepeat = 1;
                break;
              }
            case IR_DOWN:
              {
                controlsEvent(display.mode() == STATIONS ? true : false);
                irVolRepeat = 2;
                break;
              }
            case IR_MENU:
              {
                if (display.mode() == NUMBERS) {
                  display.putRequest(NEWMODE, PLAYER);
                  display.numOfNextStation = 0;
                  break;
                }
                display.putRequest(NEWMODE, display.mode() == PLAYER ? STATIONS : PLAYER);
                break;
              }
            case IR_HASH:
              {
                if (!config.store.dspon) config.setDspOn(true);
                uint8_t current = config.store.brightness;
                if (!irDimmed) {
                  irBrightnessSaved = current;
                  irDimmed = true;
                  uint8_t dim = current / 2;
                  if (dim == 0 && current > 0) dim = 1;
                  config.store.brightness = dim;
                  config.setBrightness(false);
                } else {
                  irDimmed = false;
                  uint8_t restore = irBrightnessSaved == 0 ? 100 : irBrightnessSaved;
                  config.store.brightness = restore;
                  config.setBrightness(false);
                }
                break;
              }
            case IR_HOME:
              {
                display.numOfNextStation = 0;
                display.putRequest(NEWMODE, CLEAR);
                display.putRequest(NEWMODE, PLAYER);
                break;
              }
            case IR_BACK:
              {
                display.numOfNextStation = 0;
                if (display.mode() != PLAYER) {
                  display.putRequest(NEWMODE, CLEAR);
                  display.putRequest(NEWMODE, PLAYER);
                }
                break;
              }
            case IR_MUTE:
              {
                if (!irMuted) {
                  irMuteSavedVolume = config.store.volume;
                  irMuted = true;
                  config.setVolume(0);
                  player.setVolume(player.volToI2S(0));
                } else {
                  irMuted = false;
                  config.setVolume(irMuteSavedVolume);
                  player.setVolume(player.volToI2S(irMuteSavedVolume));
                }
                break;
              }
            case IR_0:
              {
                irNumber(0);
                break;
              }
            case IR_1:
              {
                irNumber(1);
                break;
              }
            case IR_2:
              {
                irNumber(2);
                break;
              }
            case IR_3:
              {
                irNumber(3);
                break;
              }
            case IR_4:
              {
                irNumber(4);
                break;
              }
            case IR_5:
              {
                irNumber(5);
                break;
              }
            case IR_6:
              {
                irNumber(6);
                break;
              }
            case IR_7:
              {
                irNumber(7);
                break;
              }
            case IR_8:
              {
                irNumber(8);
                break;
              }
            case IR_9:
              {
                irNumber(9);
                break;
              }
            case IR_AST:
              {
                //ESP.restart();
                onBtnClick(EVT_BTNMODE);
                break;
              }
          } /* switch (target) */
          target = IR_SLOTS_TOTAL;
          break;
        } /* if(config.ircodes.irVals[target][j]==irResults.value) */
      }   /* for(int j=0; j<3; j++) */
    }     /* for(int target=0; target<16; target++) */
  }       /* if (irrecv.decode(&irResults)) */
}
#endif  // ------if IR_PIN!=255 ----------
//----------------------------------------------------------------
void onBtnLongPressStart(int id) {
  switch ((controlEvt_e)id) {
    case EVT_BTNLEFT:
    case EVT_BTNRIGHT:
    case EVT_BTNUP:
    case EVT_BTNDOWN:
      {
        lpId = id;
        break;
      }
    case EVT_BTNCENTER:
      {
#if defined(DUMMYDISPLAY) && !defined(USE_NEXTION)
        break;
#endif
        display.putRequest(NEWMODE, display.mode() == PLAYER ? STATIONS : PLAYER);
        break;
      }
    case EVT_ENCBTNB:
    case EVT_ENC2BTNB:
      {
#if defined(DUMMYDISPLAY) && !defined(USE_NEXTION)
        break;
#endif
        if (display.mode() == PLAYER) {
          display.putRequest(NEWMODE, VOL);
          display.putRequest(DRAWVOL);
        }
        break;
      }
    case EVT_BTNMODE:
      {
        //config.doSleepW();
        display.putRequest(NEWMODE, SLEEPING);
        break;
      }
    default: break;
  }
}
//----------------------------------------------------------------
void onBtnLongPressStop(int id) {
  switch ((controlEvt_e)id) {
    case EVT_BTNLEFT:
    case EVT_BTNRIGHT:
    case EVT_BTNUP:
    case EVT_BTNDOWN:
      {
        lpId = -1;
        break;
      }
    case EVT_ENCBTNB:
    case EVT_ENC2BTNB:
      {
#if BT_KEY_PIN != 255
        if (config.getMode() == PM_BLUETOOTH) _setBtKey(false);
#endif
        break;
      }
    case EVT_BTNMODE:
      {
        config.doSleepW();
        break;
      }
    default:
      break;
  }
}
//----------------------------------------------------------------
unsigned long lpdelay;
boolean checklpdelay(int m, unsigned long& tstamp) {
  if (millis() - tstamp > m) {
    tstamp = millis();
    return true;
  } else {
    return false;
  }
}
//----------------------------------------------------------------
void onBtnDuringLongPress(int id) {
  if (network.status != CONNECTED && network.status != SDREADY) return;
  if (checklpdelay(BTN_LONGPRESS_LOOP_DELAY, lpdelay)) {
    switch ((controlEvt_e)id) {
      case EVT_BTNLEFT:
        {
          controlsEvent(false);
          break;
        }
      case EVT_BTNRIGHT:
        {
          controlsEvent(true);
          break;
        }
      case EVT_BTNUP:
      case EVT_BTNDOWN:
        {
          if (display.mode() == PLAYER) {
            display.putRequest(NEWMODE, STATIONS);
          }
          if (display.mode() == STATIONS) {
            controlsEvent(id == EVT_BTNDOWN);
          }
          break;
        }
      default:
        break;
    }
  }
}
//----------------------------------------------------------------
void controlsEvent(bool toRight, int8_t volDelta) {
  if (display.mode() == NUMBERS) {
    display.numOfNextStation = 0;
    display.putRequest(NEWMODE, PLAYER);
  }
  if (display.mode() != STATIONS) {
#if !defined(DUMMYDISPLAY) || defined(USE_NEXTION)
    display.putRequest(NEWMODE, VOL);
#endif
    if (volDelta != 0) {
      int nv = config.store.volume + volDelta;
      if (nv < 0) nv = 0;
      if (nv > 254) nv = 254;
      player.setVol((uint8_t)nv);
    } else {
      player.stepVol(toRight);
    }
  }
  if (display.mode() == STATIONS) {
    display.resetQueue();
    int p = toRight ? display.currentPlItem + 1 : display.currentPlItem - 1;
    if (p < 1) p = config.store.countStation;
    if (p > config.store.countStation) p = 1;
    display.currentPlItem = p;
    display.putRequest(DRAWPLAYLIST, p);
  }
}
//----------------------------------------------------------------
void onBtnClick(int id) {
  bool passBnCenter = (controlEvt_e)id == EVT_BTNCENTER || (controlEvt_e)id == EVT_ENCBTNB || (controlEvt_e)id == EVT_ENC2BTNB;
  controlEvt_e btnid = static_cast<controlEvt_e>(id);
  pm.on_btn_click(btnid);
  if (network.status != CONNECTED && network.status != SDREADY && (controlEvt_e)id != EVT_BTNMODE && !passBnCenter) return;
  switch (btnid) {
    case EVT_BTNLEFT:
      {
        controlsEvent(false);
        break;
      }
    case EVT_BTNCENTER:
      {
#if BT_KEY_PIN != 255
        if (btnid == EVT_ENC2BTNB && config.getMode() == PM_BLUETOOTH && display.mode() == PLAYER) {
          _btKeyClick();
          break;
        }
#endif
        if (display.mode() == NUMBERS) {
          display.numOfNextStation = 0;
          display.putRequest(NEWMODE, PLAYER);
        }
        if (display.mode() == PLAYER) {
          player.toggle();
        }
        if (display.mode() == SCREENSAVER || display.mode() == SCREENBLANK) {
          display.putRequest(NEWMODE, PLAYER);
#ifdef DSP_LCD
          delay(50);//bylo 00
#endif
        }
        if (display.mode() == STATIONS) {
          display.putRequest(NEWMODE, PLAYER);
#ifdef DSP_LCD
          delay(50);//bylo 200
#endif
          player.sendCommand({ PR_PLAY, display.currentPlItem });
        }
        if (network.status == SOFT_AP || display.mode() == LOST) {
          config.changeMode();
        }
        break;
      }
    case EVT_ENCBTNB:
    case EVT_ENC2BTNB:
      {
#if BT_KEY_PIN != 255
        if (btnid == EVT_ENC2BTNB && config.getMode() == PM_BLUETOOTH && display.mode() == PLAYER) {
          _btKeyClick();
          break;
        }
#endif
        if (display.mode() == NUMBERS) {
          display.numOfNextStation = 0;
          display.putRequest(NEWMODE, PLAYER);
        }
        if (display.mode() == SCREENSAVER || display.mode() == SCREENBLANK) {
          display.putRequest(NEWMODE, PLAYER);
#ifdef DSP_LCD
          delay(50);
#endif
        }
        if (display.mode() == PLAYER) {
          player.toggle();
          break;
        }
        if (display.mode() == STATIONS) {
          display.putRequest(NEWMODE, PLAYER);
#ifdef DSP_LCD
          delay(50);
#endif
          player.sendCommand({ PR_PLAY, display.currentPlItem });
          break;
        }
        if (network.status == SOFT_AP || display.mode() == LOST) {
          config.changeMode();
        }
        break;
      }
    case EVT_BTNRIGHT:
      {
        controlsEvent(true);
        break;
      }
    case EVT_BTNUP:
    case EVT_BTNDOWN:
      {
        if (DSP_MODEL == DSP_DUMMY) {
          if (id == EVT_BTNUP) {
            player.next();
          } else {
            player.prev();
          }
        } else {
          if (display.mode() == PLAYER) {
            if (config.store.skipPlaylistUpDown || ENC2_BTNL != 255) {
              if (id == EVT_BTNUP) {
                player.prev();
              } else {
                player.next();
              }
            } else {
              display.putRequest(NEWMODE, STATIONS);
            }
          }
          if (display.mode() == STATIONS) {
            controlsEvent(id == EVT_BTNDOWN);
          }
        }
        break;
      }
    case EVT_BTNMODE:
      {
        config.changeMode();
        break;
      }
    default: break;
  }
}
//----------------------------------------------------------------
void onBtnDoubleClick(int id) {
  if (display.mode() == SCREENSAVER || display.mode() == SCREENBLANK) {
    display.putRequest(NEWMODE, PLAYER);
    return;
  }
  switch ((controlEvt_e)id) {
    case EVT_BTNLEFT:
      {
        if (display.mode() != PLAYER) return;
        if (network.status != CONNECTED && network.status != SDREADY) return;
        player.prev();
        break;
      }
    case EVT_BTNCENTER:
    case EVT_ENCBTNB:
    case EVT_ENC2BTNB:
      {
        //display.putRequest(NEWMODE, display.mode() == PLAYER ? VOL : PLAYER);
        onBtnClick(EVT_BTNMODE);
        break;
      }
    case EVT_BTNRIGHT:
      {
        if (display.mode() != PLAYER) return;
        if (network.status != CONNECTED && network.status != SDREADY) return;
        player.next();
        break;
      }
    default:
      break;
  }
}
//----------------------------------------------------------------
void setIRTolerance(uint8_t tl) {
  config.saveValue(&config.store.irtlp, tl);
#if IR_PIN != 255
  irrecv.setTolerance(config.store.irtlp);
#endif
}
//----------------------------------------------------------------
void setEncAcceleration(uint16_t acc) {
  config.saveValue(&config.store.encacc, acc);
#if ENC_BTNL != 255
  encoder.setAcceleration(config.store.encacc);
#endif
#if ENC2_BTNL != 255
  encoder2.setAcceleration(config.store.encacc);
#endif
}
//----------------------------------------------------------------
void flipTS() {
#if (TS_MODEL != TS_MODEL_UNDEFINED) && (DSP_MODEL != DSP_DUMMY)
  touchscreen.flip();
#endif
}
