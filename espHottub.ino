#include "jimlib.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

#include <esp_task_wdt.h>
#include <soc/soc.h>
#include <soc/rtc_cntl_reg.h>
#include <Update.h>			

#include "PubSubClient.h"
#include "ArduinoOTA.h"
#include "RollingLeastSquares.h"
#include "OneWireNg_CurrentPlatform.h"

#if NOMORE
String Sfmt(const char *format, ...) { 
    va_list args;
    va_start(args, format);
	char buf[256];
	vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
	return String(buf);
}



class EggTimer {
	uint64_t last;
	uint64_t interval; 
	bool first = true;
public:
	EggTimer(float ms) : interval(ms * 1000), last(0) { reset(); }
	bool tick() { 
		uint64_t now = micros();
		if (now - last >= interval) { 
			last += interval;
			// reset last to now if more than one full interval behind 
			if (now - last >= interval) 
				last = now;
			return true;
		} 
		return false;
	}
	void reset() { 
		last = micros();
	}
	void alarmNow() { 
		last = 0;
	}
};

#endif

//JimWiFi jw("MOF-Guest", "");
//JimWiFi jw;

struct {
	int led = 5;
	int relay = 25;
	int tempSense = 35;
	int tempAdjust = 18;
//	int i2cTemp = 26;
} pins;

#if 1 
void mqttCallback(char* topic, byte* payload, unsigned int length);

String getMacAddress() {
	uint8_t baseMac[6];
	// Get MAC address for WiFi station
	esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
	char baseMacChr[18] = {0};
	sprintf(baseMacChr, "%02X:%02X:%02X:%02X:%02X:%02X", baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
	return String(baseMacChr);
}

class MQTTClient { 
	WiFiClient espClient;
	String topicPrefix, server;
public:
	PubSubClient client;
	MQTTClient(const char *s, const char *t) : topicPrefix(t), server(s), client(espClient) {}
	void publish(const char *suffix, const char *m) { 
		String t = topicPrefix + "/" + suffix;
		client.publish(t.c_str(), m);
	}
	void publish(const char *suffix, const String &m) {
		 publish(suffix, m.c_str()); 
	}
	void reconnect() {
		if (WiFi.status() != WL_CONNECTED || client.connected()) 
			return;
		
		Serial.printf("Attempting MQTT connection...client.connected()=%d\n", client.connected());
		client.setServer(server.c_str(), 1883);
		client.setCallback(mqttCallback);
		String user = topicPrefix + getMacAddress();
		if (client.connect(user.c_str())) {
			// ... and resubscribe
			client.subscribe((topicPrefix + "/in").c_str());
			client.setCallback(mqttCallback);
			Serial.printf("Connected! client.connected()=%d\n", client.connected());
		} else {
			Serial.print("failed, rc=");
			Serial.print(client.state());
		}
	}
	void dprintf(const char *format, ...) { 
		va_list args;
		va_start(args, format);
        char buf[256];
        vsnprintf(buf, sizeof(buf), format, args);
	    va_end(args);
		client.publish((topicPrefix + "/debug").c_str(), buf);
	}
	void run() { 
		client.loop();
		reconnect();
	}
 };



MQTTClient mqtt("192.168.4.1", "hottub");


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

void mqttCallback(char* topic, byte* payload, unsigned int length) {
	Serial.print("Message arrived [");
	Serial.print(topic);
	Serial.print("] ");
	std::string p;
	for (int i = 0; i < length; i++) {
		Serial.print((char)payload[i]);
		p += (char)payload[i];
	}
	mqtt.publish("heater/out", "got mqtt message");
	Serial.println();

	int pct = -1;
	sscanf(p.c_str(), "tempadj %d", &pct);
	tempAdjust(pct);
}

#endif

void dbg(const char *format, ...) { 
	va_list args;
	va_start(args, format);
	char buf[256];
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);
	mqtt.publish("debug", buf);
	//jw.udpDebug(buf);
	Serial.println(buf);
}
	
#define uS_TO_S_FACTOR 1000000LL  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP 30


void WiFiAutoConnect() { 
	const struct {
		const char *name;
		const char *pass;
	} aps[] = {	{"MOF-Guest", ""}, 
				{"ChloeNet", "niftyprairie7"},
				{"Team America", "51a52b5354"},  
				{"TUK-FIRE", "FD priv n3t 20 q4"}};
	WiFi.disconnect(true);
	WiFi.mode(WIFI_STA);
	WiFi.setSleep(false);

	int bestMatch = -1;

	int n = WiFi.scanNetworks();
	Serial.println("scan done");
	
	if (n == 0) {
		Serial.println("no networks found");
	} else {
		Serial.printf("%d networks found\n", n);
		for (int i = 0; i < n; ++i) {
		// Print SSID and RSSI for each network found
			Serial.printf("%3d: %s (%d)\n", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI());
			for (int j = 0; j < sizeof(aps)/sizeof(aps[0]); j++) { 				
				if (strcmp(WiFi.SSID(i).c_str(), aps[j].name) == 0) { 
					if (bestMatch == -1 || j < bestMatch) {
						bestMatch = j;
					}
				}
			}	
		}
	}
	if (bestMatch == -1) {
		bestMatch = 0;
	}
	Serial.printf("Using WiFi AP '%s'...\n", aps[bestMatch].name);
	WiFi.begin(aps[bestMatch].name, aps[bestMatch].pass);
}


#define OWNG OneWireNg_CurrentPlatform 
OWNG *ow; 

void setup() {
	Serial.begin(921600, SERIAL_8N1);
	Serial.println("Restart");	
	
	esp_task_wdt_init(60, true);
	esp_task_wdt_add(NULL);

	pinMode(pins.led, OUTPUT);
	pinMode(pins.relay, OUTPUT);
	digitalWrite(pins.led, 0);
	digitalWrite(pins.relay, 0);

	ow = new OWNG(27, true);

	WiFiAutoConnect();
	ArduinoOTA.begin();
}

int loopCount = 0;

float avgAnalogRead(int pin) { 
	float bv = 0;
	const int avg = 1024;
	for (int i = 0; i < avg; i++) {
		delayMicroseconds(10);
		bv += analogRead(pin);
	}
	return bv / avg;
}

float readTemp() { 
	std::vector<DsTempData> t = readTemps(ow);
	return t.size() > 0 ? t[0].degC : 0;
}

TwoStageRollingAverage<float,300,10> avg1;
//RollingAverage<float,300> avg1;
EggTimer sec(60000), minute(60000);
EggTimer blink(100);

int firstLoop = 1;
float bv1, bv2;
int heat = 0;
void loop() {
	esp_task_wdt_reset();
	mqtt.run();
	ArduinoOTA.handle();

	if (blink.tick()) { 
		digitalWrite(pins.led, !digitalRead(pins.led));
		avg1.add(avgAnalogRead(pins.tempSense));	
	}	
	
	if (sec.tick()) {
		float temp = readTemp();
	
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

		if (temp > 40.00) { 
			heat = 0;
		}
		if (temp > 20 && temp < 39.00) { 
			heat = 1;
		}
		tempAdjust(heat ? 65 : 50);

		//dbg("OUTPUT %d", digitalRead(p));
		dbg("instADC: %6.1f avgADC: %6.1f temp: %6.2f heat: %d", avgAnalogRead(pins.tempSense), 
			avg1.average(), temp, heat);
	
		//dbg("%d %s    " __BASE_FILE__ "   " __DATE__ "   " __TIME__, (int)(millis() / 1000), WiFi.localIP().toString().c_str());
	
	}	
	delay(50);
}

