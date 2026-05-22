#ifndef AUDIOHANDLERS_H
#define AUDIOHANDLERS_H

//=============================================//
//              Audio handlers                 //
//=============================================//

void audio_info(const char *info) {
  if(player.lockOutput) return;
  if(config.store.audioinfo) telnet.printf("##AUDIO.INFO#: %s\n", info);
  #ifdef USE_NEXTION
    nextion.audioinfo(info);
  #endif
  static int s_nominalKbps = 0;
  static BitrateFormat s_nominalFmt = BF_UNCNOWN;
  static uint32_t s_nominalStartMs = 0;
  auto resetNominal = [&](){
    s_nominalKbps = 0;
    s_nominalFmt = config.configFmt;
    s_nominalStartMs = millis();
  };
  if (strstr(info, "format is aac")  != NULL) { config.setBitrateFormat(BF_AAC); resetNominal(); display.putRequest(DBITRATE); }
  if (strstr(info, "format is flac") != NULL) { config.setBitrateFormat(BF_FLAC); resetNominal(); display.putRequest(DBITRATE); }
  if (strstr(info, "format is mp3")  != NULL) { config.setBitrateFormat(BF_MP3); resetNominal(); display.putRequest(DBITRATE); }
  if (strstr(info, "format is wav")  != NULL) { config.setBitrateFormat(BF_WAV); resetNominal(); display.putRequest(DBITRATE); }
  if (strstr(info, "skip metadata") != NULL) config.setTitle(config.station.name);
  if (strstr(info, "Account already in use") != NULL || strstr(info, "HTTP/1.0 401") != NULL) {
    player.setError(info);
    
  }
  char* ici; char b[20]={0};
  if ((ici = strstr(info, "BitRate: ")) != NULL) {
    strlcpy(b, ici + 9, 50);
    if(b[0] >= '0' && b[0] <= '9') {
      int kbpsRaw = atoi(b) / 1000;
      if(kbpsRaw < 0) kbpsRaw = 0;
      if(config.configFmt != s_nominalFmt){
        s_nominalKbps = 0;
        s_nominalFmt = config.configFmt;
        s_nominalStartMs = millis();
      }
      if(s_nominalKbps == 0 && (config.configFmt == BF_AAC || config.configFmt == BF_FLAC)){
        uint32_t age = millis() - s_nominalStartMs;
        if(config.configFmt == BF_AAC){
          if(kbpsRaw >= 24 && kbpsRaw <= 512 && (kbpsRaw % 8) == 0) s_nominalKbps = kbpsRaw;
          else if(age > 3000 && kbpsRaw > 0) s_nominalKbps = ((kbpsRaw + 8) / 16) * 16;
        }else{
          if(kbpsRaw >= 200 && kbpsRaw <= 3000 && (kbpsRaw % 10) == 0) s_nominalKbps = kbpsRaw;
          else if(age > 3000 && kbpsRaw > 0) s_nominalKbps = ((kbpsRaw + 25) / 50) * 50;
        }
      }
      if(s_nominalKbps > 0 && (config.configFmt == BF_AAC || config.configFmt == BF_FLAC)){
        int bps = s_nominalKbps * 1000;
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", bps);
        audio_bitrate(buf);
      }else{
        audio_bitrate(b);
      }
    }
  }
}

void audio_bitrate(const char *info)
{
  int kbps = atoi(info) / 1000;
  if(kbps < 0) kbps = 0;
  config.station.bitrate = kbps;

  static uint32_t lastSentMs = 0;
  static int lastSentKbps = -1;

  uint32_t now = millis();
  uint32_t minIntervalMs = 1200;
  int minDiffKbps = 8;
  if (config.configFmt == BF_MP3) { minIntervalMs = 800; minDiffKbps = 1; }
  if (config.configFmt == BF_AAC) { minIntervalMs = 8000; minDiffKbps = 32; }
  if (config.configFmt == BF_FLAC) { minIntervalMs = 12000; minDiffKbps = 64; }

  int diff = lastSentKbps < 0 ? 9999 : abs(kbps - lastSentKbps);
  bool timeOk = (uint32_t)(now - lastSentMs) >= minIntervalMs;
  bool changeBig = diff >= minDiffKbps;
  if(lastSentKbps >= 0 && !timeOk && !changeBig) return;

  lastSentMs = now;
  lastSentKbps = kbps;

  if(config.store.audioinfo) telnet.printf("%s %s\n", "##AUDIO.BITRATE#:", info);
  display.putRequest(DBITRATE);
  #ifdef USE_NEXTION
    nextion.bitrate(config.station.bitrate);
  #endif
  netserver.requestOnChange(BITRATE, 0);
}

bool printable(const char *info) {
  // Always allow for Polish UTF-8 (and all characters generally)
  return true;
}

void audio_showstation(const char *info) {
  bool p = printable(info) && (strlen(info) > 0);(void)p;
  //config.setTitle(p?info:config.station.name);
  if(player.remoteStationName){
    config.setStation(p?info:config.station.name);
    display.putRequest(NEWSTATION);
    netserver.requestOnChange(STATION, 0);
  }
}

void audio_showstreamtitle(const char *info) {
  DBGH();
  if (strstr(info, "Account already in use") != NULL || strstr(info, "HTTP/1.0 401") != NULL) player.setError(info);
  bool p = printable(info) && (strlen(info) > 0);
  #ifdef DEBUG_TITLES
    config.setTitle(DEBUG_TITLES);
  #else
    config.setTitle(p?info:config.station.name);
  #endif
}

void audio_error(const char *info) {
  //config.setTitle(info);
  player.setError(info);
  telnet.printf("##ERROR#:\t%s\n", info);
}

void audio_id3artist(const char *info){
  if(printable(info)) config.setStation(info);
  display.putRequest(NEWSTATION);
  netserver.requestOnChange(STATION, 0);
}

void audio_id3album(const char *info){
  if(player.lockOutput) return;
  if(printable(info)){
    if(strlen(config.station.title)==0){
      config.setTitle(info);
    }else{
      char out[BUFLEN]= {0};
      strlcat(out, config.station.title, BUFLEN);
      strlcat(out, " - ", BUFLEN);
      strlcat(out, info, BUFLEN);
      config.setTitle(out);
    }
  }
}

void audio_id3title(const char *info){
  audio_id3album(info);
}

void audio_beginSDread(){
  config.setTitle("");
}

void audio_id3data(const char *info){  //id3 metadata
    if(player.lockOutput) return;
    telnet.printf("##AUDIO.ID3#: %s\n", info);
}

void audio_eof_mp3(const char *info){  //end of file
    config.sdResumePos = 0;
    player.next();
}

void audio_eof_stream(const char *info){
  player.sendCommand({PR_STOP, 0});
  if(!player.resumeAfterUrl) return;
  if (config.getMode()==PM_WEB){
    player.sendCommand({PR_PLAY, config.lastStation()});
  }else{
    player.setResumeFilePos( config.sdResumePos==0?0:config.sdResumePos-player.sd_min);
    player.sendCommand({PR_PLAY, config.lastStation()});
  }
}

void audio_progress(uint32_t startpos, uint32_t endpos){
  player.sd_min = startpos;
  player.sd_max = endpos;
  netserver.requestOnChange(SDLEN, 0);
}

#endif
