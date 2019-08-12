#include <EEPROM.h>

// PIN
const int pinSpeedToneControl = 6;
const int pinKeyDit = 5;
const int pinKeyDah = 4;

const int pinStatusLed = 13;
const int pinMosfet = 8;
const int pinSpeaker = 9;

// STATES
const int stateIdle = 0;
const int stateSettingSpeed = 1;
const int stateSettingTone = 2;

// SYMBOLS
const int symDit = 1;
const int symDah = 2;

// SAVED STATE
int toneFreq = 800;
int ditMillis = 60;

// RUN STATE
int currState = stateIdle;
int prevSymbol = 0; // 0=none, 1=dit, 2=dah
unsigned long whenStartedPress;
int currStorageOffset = 0;

void dumpSettingsToStorage();

void saveStorageEmptyPacket(int type) {
	if (currStorageOffset + 1 >= storageSize) {
		dumpSettingsToStorage();
		return;
	}

	EEPROM.write(currStorageOffset++, type);
	EEPROM.write(currStorageOffset, packetTypeEnd);
}

void saveStorageInt(int type, int value) {
	if (currStorageOffset + 1 + 2 >= storageSize) {
		dumpSettingsToStorage();
		return;
	}
	EEPROM.write(currStorageOffset++, type);
	EEPROM.write(currStorageOffset++, (value >> 8) & 0xFF);
	EEPROM.write(currStorageOffset++, value & 0xFF);
	EEPROM.write(currStorageOffset, packetTypeEnd);
}

void dumpSettingsToStorage() {
	currStorageOffset = 2;
	saveStorageInt(packetTypeSpeed, ditMillis);
	saveStorageInt(packetTypeFreq, toneFreq);
}

int delayInterruptable(int ms, int *pins, int *conditions, size_t numPins) {
	unsigned long finish = millis() + ms;

	while (1) {
		if (ms != -1 && millis() > finish)
			return -1;

		for (size_t i = 0; i < numPins; i++)
			if (digitalRead(pins[i]) == conditions[i])
				return pins[i];
	}
}

void waitPin(int pin, int condition) {
	int pins[1] = { pin };
	int conditions[1] = { condition };
	delayInterruptable(-1, pins, conditions, 1);
	delay(250); // debounce
}

void playSym(int sym) {
	playSymInterruptableVec(sym, NULL, NULL, 0);
}

int playSymInterruptable(int sym, int transmit, int pin, int condition) {
	int pins[1] = { pin };
	int conditions[1] = { condition };
	return playSymInterruptableVec(sym, transmit, pins, conditions, 1);
}

int playSymInterruptableVec(int sym, int *pins, int *conditions, size_t numPins) {
	prevSymbol = sym;

	tone(pinSpeaker, toneFreq);
	digitalWrite(pinStatusLed, HIGH);

	int ret = delayInterruptable(ditMillis * (sym == symDit ? 1 : 3), pins, conditions, numPins);

	noTone(pinSpeaker);
	digitalWrite(pinStatusLed, LOW);

	if (ret != -1)
		return ret;

	ret = delayInterruptable(ditMillis, pins, conditions, numPins);
	if (ret != -1)
		return ret;

	return -1;
}

int scaleDown(int orig, double factor, int lowerLimit) {
	int scaled = (int)((double)orig * factor);
	if (scaled == orig)
		scaled--;
	if (scaled < lowerLimit)
		scaled = lowerLimit;
	return scaled;
}

int scaleUp(int orig, double factor, int upperLimit) {
	int scaled = (int)((double)orig * factor);
	if (scaled == orig)
		scaled++;
	if (scaled > upperLimit)
		scaled = upperLimit;
	return scaled;
}

void factoryReset() {
	if (EEPROM.read(0) != storageMagic1)
		EEPROM.write(0, storageMagic1);
	if (EEPROM.read(1) != storageMagic2)
		EEPROM.write(1, storageMagic2);
	if (EEPROM.read(2) != packetTypeEnd)
		EEPROM.write(2, packetTypeEnd);

	currStorageOffset = 2;

	tone(pinSpeaker, 900);
	delay(300);
	tone(pinSpeaker, 600);
	delay(300);
	tone(pinSpeaker, 1500);
	delay(900);
	noTone(pinSpeaker);
}

void loadStorage() {
	currStorageOffset = 2;

	while (1) {
		int packetType = EEPROM.read(currStorageOffset);
		if (packetType == packetTypeEnd)
			break;
		else if (packetType == packetTypeSpeed) {
			ditMillis = (EEPROM.read(currStorageOffset + 1) << 8) | EEPROM.read(currStorageOffset + 2);
			currStorageOffset += 2;
		}
	else if (packetType == packetTypeFreq) {
			toneFreq = (EEPROM.read(currStorageOffset + 1) << 8) | EEPROM.read(currStorageOffset + 2);
			currStorageOffset += 2;
		}
		currStorageOffset++; // packet type byte
	}
}

void setup() {
	pinMode(pinSpeedToneControl, INPUT_PULLUP);
	pinMode(pinKeyDit, INPUT);
	pinMode(pinKeyDah, INPUT);

	pinMode(pinStatusLed, OUTPUT);
	pinMode(pinSpeaker, OUTPUT);

	loadStorage();
}

void loop() {
	int ditPressed = (digitalRead(pinKeyDit) == HIGH);
	int dahPressed = (digitalRead(pinKeyDah) == HIGH);

	if (currState == stateIdle) {
		if (ditPressed && dahPressed) {
			if (prevSymbol == symDah)
		playSym(symDit, 1);
			else
		playSym(symDah, 1);
		}
		else if (dahPressed)
			playSym(symDah, 1);
		else if (ditPressed)
			playSym(symDit, 1);
		else
			prevSymbol = 0;

		if (digitalRead(pinSpeedToneControl) == LOW) {
			unsigned long whenStartedPress = millis();
			int nextState = stateSettingSpeed;

			delay(5);

			while (digitalRead(pinSpeedToneControl) == LOW) {
				if (millis() > whenStartedPress + 1000) {
					digitalWrite(pinStatusLed, HIGH);
					nextState = stateSettingTone;
				}
			}

			digitalWrite(pinStatusLed, LOW);
			currState = nextState;

			delay(50);
		}
	}
	else if (currState == stateSettingSpeed) {
		if (playSymInterruptable(symDit, 0, pinSpeedToneControl, LOW) != -1) {
			currState = stateIdle;
			waitPin(pinSpeedToneControl, HIGH);
			return;
		}
		if (ditPressed)
			ditMillis = scaleDown(ditMillis, 1 / 1.05, 20);
		if (dahPressed)
			ditMillis = scaleUp(ditMillis, 1.05, 800);
		saveStorageInt(packetTypeSpeed, ditMillis);
	}
}
