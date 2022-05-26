#include "jimlib.h"
#include "RollingLeastSquares.h"

JStuff j;

struct {
	int led = 5;
	int relay = 25;
	int tempSense = 35;
	int tempAdjust = 18;
	int i2cTemp = 27;
} pins;

void tempAdjust(int pct) { 
	if (pct > 0 && pct <= 100) { 
		ledcSetup(0, 1000, 16); // channel 1, 50 Hz, 16-bit width
		ledcAttachPin(pins.tempAdjust, 0);   // GPIO 33 assigned to channel 1
		ledcWrite(0, pct * 65535 / 100);
	} else { 
		ledcDetachPin(pins.tempAdjust);
		pinMode(pins.tempAdjust, INPUT);
	}
}

TempSensor tempSense(pins.i2cTemp);

void setup() {
	j.begin();
	pinMode(pins.led, OUTPUT);
	pinMode(pins.relay, OUTPUT);
	digitalWrite(pins.led, 0);
	digitalWrite(pins.relay, 0);
}

int loopCount = 0;
TwoStageRollingAverage<float,300,10> avg1;
Timer sec(60000), minute(60000);
Timer blink(100);

int firstLoop = 1;
float bv1, bv2;
int heat = 0;

CLI_VARIABLE_FLOAT(setTemp, 40.5);
CLI_VARIABLE_FLOAT(hist, 0.15);

void loop() {
	j.run();

	if (blink.tick()) { 
		digitalWrite(pins.led, !digitalRead(pins.led));
		avg1.add(avgAnalogRead(pins.tempSense));	
	}	
	
	if (sec.tick()) {
		float temp = tempSense.readTemp();
	
		if (0) { 
			pinMode(pins.relay, OUTPUT);
			const int setTemp = 1900;
			if (avg1.average() > setTemp) { 
				digitalWrite(pins.relay, 1);
			}
			if (avg1.average() < setTemp - 100) { 
				digitalWrite(pins.relay	, 0);
			}
		}

		if (temp > setTemp) { 
			heat = 0;
		}
		if (temp > 20 && temp < setTemp - hist) { 
			heat = 1;
		}
		tempAdjust(heat ? 65 : 50);

		//dbg("OUTPUT %d", digitalRead(p));
		OUT("instADC: %6.1f avgADC: %6.1f temp: %6.2f heat: %d", avgAnalogRead(pins.tempSense), 
			avg1.average(), temp, heat);
	
		//dbg("%d %s    " __BASE_FILE__ "   " __DATE__ "   " __TIME__, (int)(millis() / 1000), WiFi.localIP().toString().c_str());
	
	}	
	delay(50);
}

