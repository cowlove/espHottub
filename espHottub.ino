#include "jimlib.h"
#include "RollingLeastSquares.h"
#include "confPanel.h"

JStuff j;

struct {
	int tempSense = 35;
	int tempAdjust = 18;
	int i2cTemp = 25;
} pins;

void tempAdjust(int percent);

Timer minute(60000), blink(100);

TempSensor tempSense(pins.i2cTemp);

float setTempA = 106.1, setTempB = 0, setTempC = 10, enumB = 2, counter = 0, counterMode = 3, abLock = 0;
ConfPanelClient cpc(0);
ConfPanelParamFloat p1(&cpc, &setTempA, "Set Temp A", 0.1);
ConfPanelParamFloat p5(&cpc, &setTempB, "Set Temp B", 0.1);
ConfPanelParamEnum p2(&cpc, &enumB, "Mode", "AUTO/MANUAL/OFF");
ConfPanelParamFloat p3(&cpc, &counter, "Counter", 0.1);
ConfPanelParamEnum p4(&cpc, &counterMode, "Counter Mode", "UP/DOWN/OFF");
ConfPanelParamEnum p6(&cpc, &abLock, "A/B Locked", "OFF/ON");

ConfPanelClient cpc1(1);
ConfPanelParamFloat p9(&cpc1, &setTempC, "Set Temp C", 0.1);
vector<ConfPanelClient *> clients;
WiFiUDP udp;

void setup() {
	j.begin();
	j.cli.on("MINUTE", [](){ minute.alarmNow(); });
	j.run();
	udp.begin(4444);
	cpc.addFloat(&setTempA, "TempA2");
	clients.push_back(&cpc);
	clients.push_back(&cpc1);
}

CLI_VARIABLE_FLOAT(setTemp, 41);
CLI_VARIABLE_FLOAT(hist, 0.1);
CLI_VARIABLE_FLOAT(reportTime, 60);
CLI_VARIABLE_STRING(test, "test");

TwoStageRollingAverage<float,300,10> avg1;
int heat = 0;

void loop() {
	j.run();
   if (counterMode == 0) 
      counter += 1;
    if (counterMode == 1)
      counter -= 1;
    if (abLock) 
      setTempB = setTempA; 

	string s;
    for (auto c : clients) 
      s += c->readData();

    if (s.length() > 0) {
      udp.beginPacket("255.255.255.255", 4444);
      udp.write((uint8_t *)s.c_str(), s.length());
      udp.endPacket();
    }

    if (udp.parsePacket() > 0) {
      char buf[1024];
      static LineBuffer lb;
      int n = udp.read((uint8_t *)buf, sizeof(buf));
      lb.add((char *)buf, n, [](const char *line) { 
        for (auto p : clients) { 
          p->onRecv(line);
        }
      }); 
	}
   

	if (blink.tick()) { 
		avg1.add(avgAnalogRead(pins.tempSense));	
	}	
	
	if (j.secTick(reportTime)) {
		float temp = tempSense.readTemp();
	
		if (temp > setTemp) { 
			heat = 0;
		} else if (temp > 20 && temp < setTemp - hist) { 
			heat = 1;
		}
		tempAdjust(heat ? 65 : -1);
		//j.led.setPattern(500, heat ? 0x6 : 0x4, 1.4);

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
