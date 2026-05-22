#ifndef optionschecker_h
#define optionschecker_h

#if REAL_LEDBUILTIN==TFT_RST
#  error LED_BUILTIN JEST TAKI SAM JAK TFT_RST. Sprawdź w myoptions.h
#endif

#if REAL_LEDBUILTIN==VS1053_RST
#  error LED_BUILTIN JEST TAKI SAM JAK VS1053_RST. Sprawdź w myoptions.h
#endif

#if (I2S_DOUT!=255) && (VS1053_CS!=255)
#  error MUSISZ WYBRAĆ MIĘDZY I2S DAC A VS1053, WYŁĄCZAJĄC DRUGI MODUŁ W myoptions.h
#endif

#if !(defined(ARDUINO_ESP32_DEV) || defined(ARDUINO_ESP32S3_DEV) || defined(ARDUINO_ESP32C3_DEV))
#  error OBSŁUGIWANE SĄ TYLKO PŁYTKI "ESP32 Dev Module", "ESP32 Wrover Module" ORAZ "ESP32 S3 Dev Module". Wybierz jedną w MENU: NARZĘDZIA >> PŁYTKA
#endif

#endif

