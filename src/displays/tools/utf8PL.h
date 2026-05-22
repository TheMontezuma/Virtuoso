#ifndef utf8PLGFX_h
#define utf8PLGFX_h

// Polish chars: ąćęłńóśźż ĄĆĘŁŃÓŚŹŻ

char* DspCore::utf8PL(const char* str, bool uppercase) {
 int index = 0;
  static char strn[BUFLEN];
  strlcpy(strn, str, BUFLEN); 
 
  if (uppercase) {
    for (char *iter = strn; *iter != '\0'; ++iter) {
      unsigned char ch = (unsigned char)*iter;
      if (ch >= 'a' && ch <= 'z') *iter = (char)(ch - 32);
    }
  }

  while (strn[index])
  { 
    if ((uint8_t)strn[index] == 0xC2) {
      uint8_t b = (uint8_t)strn[index + 1];
      bool mapped = true;
      switch (b) {
        case 0xB1: strn[index] = uppercase ? 0xB7 : 0xB8; break;
        case 0xA1: strn[index] = 0xB7; break;
        case 0xB9: strn[index] = uppercase ? 0xB7 : 0xB8; break;
        case 0xA5: strn[index] = 0xB7; break;
        case 0xE6: strn[index] = uppercase ? 0xC4 : 0xBD; break;
        case 0xC6: strn[index] = 0xC4; break;
        case 0xEA: strn[index] = uppercase ? 0xD7 : 0xD6; break;
        case 0xCA: strn[index] = 0xD7; break;
        case 0xB3: strn[index] = uppercase ? 0xD0 : 0xCF; break;
        case 0xA3: strn[index] = 0xD0; break;
        case 0xF1: strn[index] = uppercase ? 0xC1 : 0xC0; break;
        case 0xD1: strn[index] = 0xC1; break;
        case 0xF3: strn[index] = uppercase ? 0xBF : 0xBE; break;
        case 0xD3: strn[index] = 0xBF; break;
        case 0xB6: strn[index] = uppercase ? 0xCC : 0xCB; break;
        case 0xA6: strn[index] = 0xCC; break;
        case 0x9C: strn[index] = uppercase ? 0xCC : 0xCB; break;
        case 0x8C: strn[index] = 0xCC; break;
        case 0xBC: strn[index] = uppercase ? 0xBC : 0xBB; break;
        case 0xAC: strn[index] = 0xBC; break;
        case 0x9F: strn[index] = uppercase ? 0xBC : 0xBB; break;
        case 0x8F: strn[index] = 0xBC; break;
        case 0xBF: strn[index] = uppercase ? 0xBA : 0xB9; break;
        case 0xAF: strn[index] = 0xBA; break;
        default: mapped = false; break;
      }
      if (mapped) {
        int sind = index + 2;
        while (strn[sind]) {
          strn[sind - 1] = strn[sind];
          sind++;
        }
        strn[sind - 1] = 0;
      }
    }
    switch ((uint8_t)strn[index]) {
      case 0xB1: strn[index] = uppercase ? 0xB7 : 0xB8; break;
      case 0xA1: strn[index] = 0xB7; break;
      case 0xB9: strn[index] = uppercase ? 0xB7 : 0xB8; break;
      case 0xA5: strn[index] = 0xB7; break;
      case 0xE6: strn[index] = uppercase ? 0xC4 : 0xBD; break;
      case 0xC6: strn[index] = 0xC4; break;
      case 0xEA: strn[index] = uppercase ? 0xD7 : 0xD6; break;
      case 0xCA: strn[index] = 0xD7; break;
      case 0xB3: strn[index] = uppercase ? 0xD0 : 0xCF; break;
      case 0xA3: strn[index] = 0xD0; break;
      case 0xF1: strn[index] = uppercase ? 0xC1 : 0xC0; break;
      case 0xD1: strn[index] = 0xC1; break;
      case 0xF3: strn[index] = uppercase ? 0xBF : 0xBE; break;
      case 0xD3: strn[index] = 0xBF; break;
      case 0xB6: strn[index] = uppercase ? 0xCC : 0xCB; break;
      case 0xA6: strn[index] = 0xCC; break;
      case 0x9C: strn[index] = uppercase ? 0xCC : 0xCB; break;
      case 0x8C: strn[index] = 0xCC; break;
      case 0xBC: strn[index] = uppercase ? 0xBC : 0xBB; break;
      case 0xAC: strn[index] = 0xBC; break;
      case 0x9F: strn[index] = uppercase ? 0xBC : 0xBB; break;
      case 0x8F: strn[index] = 0xBC; break;
      case 0xBF: strn[index] = uppercase ? 0xBA : 0xB9; break;
      case 0xAF: strn[index] = 0xBA; break;
    }
    if ((uint8_t)strn[index] == 0xC5)
    {
      switch ((uint8_t)strn[index + 1]) {
        case 0x82: {    
			if (!uppercase){ 
			strn[index] = 0xCf;} // *ł
			else {
			strn[index] = 0xD0;} // *Ł
            break;
        }
			case 0x81: { 
			strn[index] = 0xD0; // *Ł
            break;
            } 

		case 0x84: {
			if (!uppercase){ 
			strn[index] = 0xC0;} // *ń
			else {
			strn[index] = 0xC1;} // *Ń
              break;
		}
			case 0x83: { 
			strn[index] = 0xC1; // *Ń
            break;
            } 
			
		case 0x9B: { 
			if (!uppercase){ 
			strn[index] = 0xCB;} // *ś
			else {
			strn[index] = 0xCC;} // *Ś
            break;
        }
			case 0x9A: { 
			strn[index] = 0xCC; // *Ś
            break;
            } 
			
		case 0xBA: { 
			if (!uppercase){ 
			strn[index] = 0xBB;} // *ź
			else {
			strn[index] = 0xBC;} // *Ź
            break;
        }
			case 0xB9: { 
			strn[index] = 0xBC; // *Ź
            break;
            } 

		case 0xBC: { 
			if (!uppercase){ 
			strn[index] = 0xB9;} // *ż
			else {
			strn[index] = 0xBA;} // *Ż
            break;
        }
			case 0xBB: { 
			strn[index] = 0xBA; // *Ż
            break;
            } 
//slovakia
        case 0x88: {    
			if (!uppercase){ 
			strn[index] = 0xB4;} // *ň
			else {
			strn[index] = 0xB3;} // *Ň
            break;
        }
			case 0x87: { 
			strn[index] = 0xB3; // *Ň
            break;
            } 

		case 0x95: {
			if (!uppercase){ 
			strn[index] = 0xB6;} // *ř
			else {
			strn[index] = 0xB5;} // *Ŕ
              break;
		}
			case 0x94: { 
			strn[index] = 0xB5; // *Ŕ
            break;
            } 
			
		case 0xA1: { 
			if (!uppercase){ 
			strn[index] = 0xC3;} // *š
			else {
			strn[index] = 0xC2;} // *Š
            break;
        }
			case 0xA0: { 
			strn[index] = 0xC2; // *Š
            break;
            } 
			
		case 0xA5: { 
			if (!uppercase){ 
			strn[index] = 0xC6;} // *ť
			else {
			strn[index] = 0xC5;} // *Ť
            break;
        }
			case 0xA4: { 
			strn[index] = 0xC5; // *Ť
            break;
            } 

		case 0xBE: { 
			if (!uppercase){ 
			strn[index] = 0xC8;} // *ž
			else {
			strn[index] = 0xC7;} // *Ž
            break;
        }
			case 0xBD: { 
			strn[index] = 0xC7; // *Ž
            break;
            } 

		case 0xAE: { 
			if (!uppercase){ 
			strn[index] = 0xE8;} // *ů
			else {
			strn[index] = 0x9D;} // *Ů
            break;
        }
		    case 0xAF: { 
			strn[index] = 0x9D; // *Ů
            break;
            } 
			 	
	  }
		int sind = index + 2;
		while (strn[sind]) {
        strn[sind - 1] = strn[sind];
        sind++;
      }
    strn[sind - 1] = 0;
	}

if ((uint8_t)strn[index] == 0xC4)
    {
	  switch ((uint8_t)strn[index + 1]) {

		case 0x85: {
			if (!uppercase){ 
			strn[index] = 0xB8;} // *ą
			else {
			strn[index] = 0xB7;} // *Ą
              break;
		}
			case 0x84	: { 
			strn[index] = 0xB7; // *Ą
            break;
            } 
		
		case 0x87: {
			if (!uppercase){ 
			strn[index] = 0xBD;} // *ć
			else {
			strn[index] = 0xC4;} // *Ć
              break;
		}
			case 0x86: { 
			strn[index] = 0xC4; // *Ć
            break;
            } 

    case 0x99: {
			if (!uppercase){ 
			strn[index] = 0xD6;} // *ę
			else {
			strn[index] = 0xD7;} // *Ę
              break;
		}
			case 0x98: { 
			strn[index] = 0xD7; // *Ę
            break;
            } 
// Slovakia chars:
        case 0x8D: {
			if (!uppercase){ 
			strn[index] = 0xCA;} // *č
			else {
			strn[index] = 0xC9;} // *Č
              break;
		}
			case 0x8C: { 
			strn[index] = 0xC9; // *Č
            break;
            } 

		case 0x8E: {
			if (!uppercase){ 
			strn[index] = 0xD1;} // *ď
			else {
			strn[index] = 0xCE;} // *Ď
              break;
		}
			case 0x8F: { 
			strn[index] = 0xCE; // *Ď
            break;
            } 	

		case 0xBA: {
			if (!uppercase){ 
			strn[index] = 0xD3;} // *ĺ
			else {
			strn[index] = 0xD2;} // *Ĺ 
              break;
		}
			case 0xB9: { 
			strn[index] = 0xD2; // *Ĺ 
            break;
            } 

		case 0xBE: {
			if (!uppercase){ 
			strn[index] = 0xD5;} // *ľ
			else {
			strn[index] = 0xD4;} // *Ľ 
              break;
		}
			case 0xBD: { 
			strn[index] = 0xD4; // *Ľ
            break;
            }

      } 

		int sind = index + 2;
		while (strn[sind]) {
        strn[sind - 1] = strn[sind];
        sind++;
      }
      strn[sind - 1] = 0;
    
	}

if ((uint8_t)strn[index] == 0xC3)
    {
	  switch ((uint8_t)strn[index + 1]) {

		case 0xB3: {
			if (!uppercase){ 
			strn[index] = 0xBE;} // *ó
			else {
			strn[index] = 0xBF;} // *Ó
              break;
		}
			case 0x93: { 
			strn[index] = 0xBF; // *Ó
            break;
            } 
// deutschland chars: äöü ÄÖÜ ß é
    
	        case 0xA4: {
		if (!uppercase){					// ä 
			strn[index] = 0x84;}
		  else {
			strn[index] = 0x8E;}			// Ä 
              break;
            }
		case 0xB6: { 
		if (!uppercase){					// ö 
			strn[index] = 0x94;}
		  else {
			strn[index] = 0x99;}			// Ö 
              break;
			}
		case 0xBC: {  
		if (!uppercase){					// ü 
			  strn[index] = 0x81;}
		  else {
			  strn[index] = 0x9A;}			// Ü 
              break;
            }			
		case 0x84: {  						// Ä
              strn[index] = 0x8E;
              break;
            }
		case 0x96: {  						// Ö
              strn[index] = 0x99;
              break;
            }
		case 0x9C: {  						// Ü
              strn[index] = 0x9A;
              break;
            }
		case 0x9F: {  						// ß
              strn[index] = 0xE1;
              break;
            }		       
         
// Slovakia

        case 0xA1: {
			if (!uppercase){ 
			strn[index] = 0xD9;} // *á
			else {
			strn[index] = 0xD8;} // *Á
              break;
		}
			case 0x81: { 
			strn[index] = 0xD8; // *Á
            break;
            } 

		 
		case 0xA9: {
			if (!uppercase){ 
			strn[index] = 0x82;} // *é
			else {
			strn[index] = 0x90;} // *É
              break;
		}
			case 0x89: { 
			strn[index] = 0x90; // *É
            break;
            } 

		case 0xAD: {
			if (!uppercase){ 
			strn[index] = 0xDB;} // *í
			else {
			strn[index] = 0xDA;} // *Í
              break;
		}
			case 0x8D: { 
			strn[index] = 0xDA; // *Í
            break;
            } 	

		case 0xB4: {
			if (!uppercase){ 
			strn[index] = 0xDD;} // *ô
			else {
			strn[index] = 0xDC;} // *Ô
              break;
		}
			case 0x94: { 
			strn[index] = 0xDC; // *Ô
            break;
            } 	

		case 0xBA: {
			if (!uppercase){ 
			strn[index] = 0xDF;} // *ú
			else {
			strn[index] = 0xDE;} // *Ú
              break;
		}
			case 0x9A: { 
			strn[index] = 0xDE; // *Ú
            break;
            } 

		case 0xBD: {
			if (!uppercase){ 
			strn[index] = 0xE3;} // *ý
			else {
			strn[index] = 0xE2;} // *Ý
              break;
		}
			case 0x9D: { 
			strn[index] = 0xE2; // *Ý
            break;
            } 

          } 

		int sind = index + 2;
		while (strn[sind]) {
        strn[sind - 1] = strn[sind];
        sind++;
      }
      strn[sind - 1] = 0;
    
	}

// Wstaw tutaj swoją korektę na dalsze czcionki...


    index++;
  }
return strn;
}
#endif
