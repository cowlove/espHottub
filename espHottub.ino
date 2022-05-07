//#include "jimlib.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

#include <esp_task_wdt.h>
#include <soc/soc.h>
#include <soc/rtc_cntl_reg.h>
#include <Update.h>			

#include "PubSubClient.h"
#include "ArduinoOTA.h"

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



//JimWiFi jw("MOF-Guest", "");
//JimWiFi jw;

struct {
	int led = 5;
	int powerControlPin = 18;
} pins;

#if 1 
void mqttCallback(char* topic, byte* payload, unsigned int length);

class MQTTClient { 
	WiFiClient espClient;
	String topicPrefix, server;
public:
	PubSubClient client;
	MQTTClient(const char *s, const char *t) : server(s), topicPrefix(t), client(espClient) {}
	void publish(const char *suffix, const char *m) { 
		String t = topicPrefix + "/" + suffix;
		client.publish(t.c_str(), m);
	}
	void publish(const char *suffix, const String &m) {
		 publish(suffix, m.c_str()); 
	}
	void reconnect() {
	// Loop until we're reconnected
		if (WiFi.status() != WL_CONNECTED || client.connected()) 
			return;
		
		Serial.print("Attempting MQTT connection...");
		client.setServer(server.c_str(), 1883);
		client.setCallback(mqttCallback);
		if (client.connect(topicPrefix.c_str())) {
			Serial.println("connected");
			// ... and resubscribe
			client.subscribe((topicPrefix + "/in").c_str());
			client.setCallback(mqttCallback);
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


MQTTClient mqtt("192.168.4.1", "lowpow");

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

void setup() {
	Serial.begin(921600, SERIAL_8N1);
	Serial.println("Restart");	
	
	esp_task_wdt_init(60, true);
	esp_task_wdt_add(NULL);

	//pinMode(35, INPUT);
	//analogSetSamples(255);

	pinMode(pins.led, OUTPUT);
	digitalWrite(pins.led, 0);
	pinMode(pins.powerControlPin, INPUT);

	WiFiAutoConnect();
	ArduinoOTA.begin();
}

EggTimer sec(2000), minute(60000);
EggTimer blink(100);
int loopCount = 0;

float avgAnalogRead(int pin) { 
	float bv = 0;
	const int avg = 1024;
	for (int i = 0; i < avg; i++) {
		bv += analogRead(pin);
	}
	return bv / avg;
}

int hex2bin(const char *in, char *out, int l) { 
        for (const char *p = in; p < in + l ; p += 2) { 
                char b[3];
                b[0] = p[0];
                b[1] = p[1];
                b[2] = 0;
                int c;
                sscanf(b, "%x", &c);
                *(out++) = c;
        }
        return l / 2;
}

void webUpgrade(const char *u) {
	WiFiClientSecure wc;
	wc.setInsecure();
	HTTPClient client; 

	int offset = 0;
	int len = 1024 * 16;
	int errors = 0;
 
	Update.begin(UPDATE_SIZE_UNKNOWN);
	Serial.println("Updating firmware...");

	while(true) { 
		String url = String(u) + Sfmt("?len=%d&offset=%d", len, offset);
		dbg("offset %d, len %d, url %s", offset, len, url.c_str());
		client.begin(wc, url);
		int resp = client.GET();
		//dbg("HTTPClient.get() returned %d\n", resp);
		if(resp != 200) {
			dbg("Get failed\n");
			Serial.print(client.getString());
			delay(5000);
			if (++errors > 10) { 
				return;
			}
			continue;
		}
		int currentLength = 0;
		int	totalLength = client.getSize();
		int len = totalLength;
		uint8_t bbuf[128], tbuf[256];
	
		//Serial.printf("FW Size: %u\n",totalLength);
		if (totalLength == 0) { 
			Serial.printf("\nUpdate Success, Total Size: %u\nRebooting...\n", currentLength);
			Update.end(true);
			ESP.restart();
			return;				
		}
		
		
		WiFiClient * stream = client.getStreamPtr();
		while(client.connected() && len > 0) {
			size_t avail = stream->available();
			if(avail) {
				esp_task_wdt_reset();
				int c = stream->readBytes(tbuf, ((avail > sizeof(tbuf)) ? sizeof(tbuf) : avail));
				if (c > 0) {
					hex2bin((const char *)tbuf, (char *)bbuf, c);
					//dbg("Update with %d", c / 2);
					Update.write(bbuf, c / 2);
					if(len > 0) {
						len -= c;
					}
				}
			}
			delay(1);
		}	
		client.end();
		offset += totalLength / 2;
	}
}


int firstLoop = 1;
float bv1, bv2;
void loop() {
	esp_task_wdt_reset();
	//jw.run();
	mqtt.run();
	ArduinoOTA.handle();
    //if (jw.updateInProgress) {
	//		return;
	//}

	if (0 && blink.tick()) { 
		digitalWrite(pins.led, !digitalRead(pins.led));
	}	
	
	if (sec.tick()) {
		if (1) { 
			int p = 25;
			pinMode(p, OUTPUT);
			digitalWrite(p, !digitalRead(p));
			dbg("OUTPUT %d\n", digitalRead(p));
			dbg("adc: %6.1f %6.1f", avgAnalogRead(35), avgAnalogRead(33));
		
            dbg("%d %s    " __BASE_FILE__ "   " __DATE__ "   " __TIME__ "   0x%08x: ", 
			(int)(millis() / 1000), WiFi.localIP().toString().c_str(), /*(int)ESP.getEfuseMac()*/0);
		
			if (0) { 
				delay(1000);
				dbg("SLEEP %d\n", digitalRead(p));
				gpio_hold_en((gpio_num_t)p);
				gpio_deep_sleep_hold_en();
				delay(100);
				esp_sleep_enable_timer_wakeup(3LL * uS_TO_S_FACTOR);
				//esp_light_sleep_start();
				gpio_hold_dis((gpio_num_t)p);
				dbg("WAKE %d\n", digitalRead(p));
			}
			return;
		}		

		int status = 0;
		if (firstLoop) { 
			bv1 = avgAnalogRead(35);
			bv2 = avgAnalogRead(33);
			firstLoop = 0;
			if (bv1 < 2460) {
				pinMode(pins.powerControlPin, OUTPUT);
				digitalWrite(pins.powerControlPin, 1);
			}
		}
		
		if (WiFi.status() == WL_CONNECTED) {

			WiFiClientSecure wc;
			wc.setInsecure();
			//wc.setFingerprint(fingerprint);
			HTTPClient client;
			int r = client.begin(wc, "https://thingproxy.freeboard.io/fetch/https://vheavy.com/log");
			dbg("http.begin() returned %d\n", r);
		
			String mac = WiFi.macAddress();
			mac.replace(":", "");
			String s = Sfmt("{\"GIT_VERSION\":\"%s\",", GIT_VERSION) + 
				Sfmt("\"MAC\":\"%s\",", mac.c_str()) + 
				Sfmt("\"Power12V\":%d,", digitalRead(pins.powerControlPin)) + 
				Sfmt("\"Tiedown.BatteryVoltage1\":%.1f,", bv1) + 
				Sfmt("\"Tiedown.BatteryVoltage2\":%.1f}\n", bv2);

			client.addHeader("Content-Type", "application/json");
			r = client.POST(s.c_str());
			//r = client.GET();
			s =  client.getString();
			client.end();
		
			dbg("http.POST() returned %d and %s\n", r, s.c_str());
		
			StaticJsonDocument<1024> doc;
			DeserializationError error = deserializeJson(doc, s);
			const char *ota_ver = doc["ota_ver"];
			status = doc["status"];

			if (ota_ver != NULL) { 
				if (strcmp(ota_ver, GIT_VERSION) == 0
					// dont update an existing -dirty unless ota_ver is also -dirty  
					|| (strstr(GIT_VERSION, "-dirty") != NULL && strstr(ota_ver, "-dirty") == NULL)
					) {
					dbg("OTA version '%s', local version '%s', no upgrade needed\n", ota_ver, GIT_VERSION);
				} else { 
					dbg("OTA version '%s', local version '%s', upgrading...\n", ota_ver, GIT_VERSION);
					webUpgrade("https://thingproxy.freeboard.io/fetch/https://vheavy.com/ota");
				}	
			}	  

			if (status == 1) {
				if (digitalRead(pins.powerControlPin) == 1) {
					// ESP lipo battery is low, charge it by light sleeping a while with 12V on  
					dbg("SUCCESS, LIGHT SLEEPING");
					//adc_power_off();
					WiFi.disconnect(true);  // Disconnect from the network
					WiFi.mode(WIFI_OFF);    // Switch WiFi off

					if (1) {
						// investigate why light_sleep isn't working 
						for(int i = 0; i < 23 * 60; i++) {
							esp_task_wdt_reset();
							delay(1000);
						}
						ESP.restart();
					}
					gpio_hold_en((gpio_num_t)pins.powerControlPin);
					gpio_deep_sleep_hold_en();
					delay(100);

					esp_sleep_enable_timer_wakeup(23LL * 60 * uS_TO_S_FACTOR);
					esp_deep_sleep_start();
									
					digitalWrite(pins.led, 0);
					gpio_hold_dis((gpio_num_t)pins.powerControlPin);
					pinMode(pins.powerControlPin, INPUT);
					delay(100);
					ESP.restart();					
				} else { 
					dbg("SUCCESS, DEEP SLEEPING");
					digitalWrite(pins.led, 0);
					pinMode(pins.powerControlPin, INPUT);
					delay(100);
					//esp_sleep_enable_timer_wakeup(53LL * 60 * uS_TO_S_FACTOR);
					esp_sleep_enable_timer_wakeup(23LL * 60 * uS_TO_S_FACTOR);
					esp_deep_sleep_start();
				}
			}
		}

		if(loopCount++ > 30) { 
			if (WiFi.status() != WL_CONNECTED) {
				dbg("NEVER CONNECTED, REBOOTING");
				//ESP.restart();
			}
			dbg("TOO MANY RETRIES, SLEEPING");
			digitalWrite(pins.led, 0);
			pinMode(pins.powerControlPin, INPUT);
			delay(100);
			esp_sleep_enable_timer_wakeup(300LL * uS_TO_S_FACTOR);
			esp_deep_sleep_start();
		}
	}
	delay(50);
}

