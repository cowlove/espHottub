#include "jimlib.h"
#include "RollingLeastSquares.h"

JStuff j;

struct {
	int tempSense = 35;
	int tempAdjust = 18;
	int i2cTemp = 27;
} pins;

void tempAdjust(int percent);

Timer minute(60000), blink(100);

TempSensor tempSense(pins.i2cTemp);

void setup() {
	j.begin();
	j.cli.on("MINUTE", [](){ minute.alarmNow(); });
}

CLI_VARIABLE_FLOAT(setTemp, 40.5);
CLI_VARIABLE_FLOAT(hist, 0.15);
CLI_VARIABLE_STRING(test, "test");

TwoStageRollingAverage<float,300,10> avg1;
int heat = 0;

void loop() {
	j.run();

	if (blink.tick()) { 
		avg1.add(avgAnalogRead(pins.tempSense));	
	}	
	
	if (minute.tick()) {
		float temp = tempSense.readTemp();
	
		if (temp > setTemp) { 
			heat = 0;
		} else if (temp > 20 && temp < setTemp - hist) { 
			heat = 1;
		}
		tempAdjust(heat ? 65 : -1);
		j.led.setPattern(500, heat ? 0x6 : 0x4, 1.4);

		OUT("instADC: %6.1f avgADC: %6.1f setTemp: %6.2f temp: %6.2f heat: %d", avgAnalogRead(pins.tempSense), 
			avg1.average(), (float)setTemp, temp, heat);
	
	}	

	if (1) { // little csim test  
		pinMode(17, INPUT);
		if (digitalRead(17) == 0 && (String)test != String("")) {
			String s = test; 
			OUT("test value is %s", s.c_str());
			test = "";
		}
	}

	delay(10);
}

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
