// based on https://github.com/igorantolic/ai-esp32-rotary-encoder code
//
//

#include "esp_log.h"
#define LOG_TAG "yoEncoder"

#include "yoEncoder.h"

#ifndef YOENCODER_DEBOUNCE_US
#define YOENCODER_DEBOUNCE_US 2500
#endif
void IRAM_ATTR yoEncoder::readEncoder_ISR()
{

	uint32_t nowUs = micros();
	if (debounceUs > 0) {
		uint32_t d = nowUs - lastIsrUs;
		if (d < debounceUs) return;
		lastIsrUs = nowUs;
	}
	unsigned long now = millis();
	portENTER_CRITICAL_ISR(&(this->mux));
	if (this->isEnabled)
	{
		// code from https://www.circuitsathome.com/mcu/reading-rotary-encoder-on-arduino/
		/**/
		this->old_AB <<= 2; //remember previous state

		uint8_t s1 = ((digitalRead(this->encoderBPin)) ? (1 << 1) : 0) | ((digitalRead(this->encoderAPin)) ? (1 << 0) : 0);
		int8_t ENC_PORT = (int8_t)s1;
		if ((uint8_t)ENC_PORT == lastState2b) {
			portEXIT_CRITICAL_ISR(&(this->mux));
			return;
		}
		lastState2b = (uint8_t)ENC_PORT;

		this->old_AB |= (ENC_PORT & 0x03); //add current state

		//this->encoder0Pos += ( this->enc_states[( this->old_AB & 0x0f )]);
		int8_t currentDirection = (this->enc_states[(this->old_AB & 0x0f)]); //-1,0 or 1

		if (currentDirection != 0)
		{
			long prevRotaryPosition = this->encoder0Pos / this->encoderSteps;
			this->encoder0Pos += currentDirection;
			long newRotaryPosition = this->encoder0Pos / this->encoderSteps;

			if (newRotaryPosition != prevRotaryPosition && rotaryAccelerationCoef > 1)
			{
				//additional movements cause acceleration?
				// at X ms, there should be no acceleration.
				unsigned long accelerationLongCutoffMillis = 200;
				// at Y ms, we want to have maximum acceleration
				unsigned long accelerationShortCutffMillis = 4;

				// compute linear acceleration
				if (currentDirection == lastMovementDirection &&
					currentDirection != 0 &&
					lastMovementDirection != 0)
				{
					// ... but only of the direction of rotation matched and there
					// actually was a previous rotation.
					unsigned long millisAfterLastMotion = now - lastMovementAt;

					if (millisAfterLastMotion < accelerationLongCutoffMillis)
					{
						if (millisAfterLastMotion < accelerationShortCutffMillis)
						{
							millisAfterLastMotion = accelerationShortCutffMillis; // limit to maximum acceleration
						}
						long extra = (long)(rotaryAccelerationCoef / millisAfterLastMotion);
						long maxExtra = (long)this->encoderSteps * 8;
						if (extra > maxExtra) extra = maxExtra;
						if (currentDirection > 0) this->encoder0Pos += extra;
						else this->encoder0Pos -= extra;
					}
				}
				this->lastMovementAt = now;
				this->lastMovementDirection = currentDirection;
			}

			//respect limits
			if (this->encoder0Pos > (this->_maxEncoderValue))
				this->encoder0Pos = this->_circleValues ? this->_minEncoderValue : this->_maxEncoderValue;
			if (this->encoder0Pos < (this->_minEncoderValue))
				this->encoder0Pos = this->_circleValues ? this->_maxEncoderValue : this->_minEncoderValue;
		}
	}
	portEXIT_CRITICAL_ISR(&(this->mux));
}


yoEncoder::yoEncoder(uint8_t encoder_APin, uint8_t encoder_BPin, uint8_t encoderSteps, bool internalPullup)
{
	this->old_AB = 0;
	this->lastIsrUs = 0;
	this->lastState2b = 0xFF;
	this->debounceUs = YOENCODER_DEBOUNCE_US;

	this->encoderAPin = encoder_APin;
	this->encoderBPin = encoder_BPin;
	this->encoderSteps = encoderSteps;

	pinMode(this->encoderAPin, internalPullup?INPUT_PULLUP:INPUT);
	pinMode(this->encoderBPin, internalPullup?INPUT_PULLUP:INPUT);
}

void yoEncoder::setBoundaries(long minEncoderValue, long maxEncoderValue, bool circleValues)
{
	this->_minEncoderValue = minEncoderValue * this->encoderSteps;
	this->_maxEncoderValue = maxEncoderValue * this->encoderSteps;

	this->_circleValues = circleValues;
}

long yoEncoder::readEncoder()
{
	portENTER_CRITICAL(&(this->mux));
	long p = this->encoder0Pos;
	portEXIT_CRITICAL(&(this->mux));
	return (p / this->encoderSteps);
}

void yoEncoder::setEncoderValue(long newValue)
{
	reset(newValue);
}

long yoEncoder::encoderChanged()
{
	long _encoder0Pos = readEncoder();
	long encoder0Diff = _encoder0Pos - this->lastReadEncoder0Pos;

	this->lastReadEncoder0Pos = _encoder0Pos;

	return encoder0Diff;
}

void yoEncoder::setup(void (*ISR_callback)(void))
{
	attachInterrupt(this->encoderAPin, ISR_callback, CHANGE);
	attachInterrupt(this->encoderBPin, ISR_callback, CHANGE);
}

void yoEncoder::begin()
{
	this->lastReadEncoder0Pos = 0;
}

void yoEncoder::reset(long newValue_)
{
	newValue_ = newValue_ * this->encoderSteps;
	this->encoder0Pos = newValue_;
	this->lastReadEncoder0Pos = this->encoder0Pos;
	if (this->encoder0Pos > this->_maxEncoderValue)
		this->encoder0Pos = this->_circleValues ? this->_minEncoderValue : this->_maxEncoderValue;
	if (this->encoder0Pos < this->_minEncoderValue)
		this->encoder0Pos = this->_circleValues ? this->_maxEncoderValue : this->_minEncoderValue;
}

void yoEncoder::enable()
{
	this->isEnabled = true;
}
void yoEncoder::disable()
{
	this->isEnabled = false;
}
