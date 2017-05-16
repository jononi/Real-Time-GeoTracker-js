/*
* Project asset real time tracker and logger
* Description: first iteration to interact with neom8n gnss module
* Author: jaafar benabdallah 2017
* Date: May 2017
* v 0.15
* based on original work by: Rick K (rickkas7) https://github.com/rickkas7/AssetTrackerRK/tree/master/examples/2_GPSCellularOnOff
*/

#include "TinyGPSPlus.h"
// #include "Ubidots.h"

// #define serial_debug

#define	GNSS	Serial1
// #define ubiToken "your_token_goes_here"
// #define DATA_SOURCE_NAME "electrongnss"

#ifdef serial_debug
// SerialLogHandler logHandler(9600);
SerialLogHandler logHandler(LOG_LEVEL_WARN, {
	{ "app", LOG_LEVEL_INFO },
	{ "app.custom", LOG_LEVEL_INFO }
});
#endif

SYSTEM_MODE(SEMI_AUTOMATIC);
SYSTEM_THREAD(ENABLED);

const unsigned long SERIAL_PERIOD = 5000;
unsigned long PUBLISH_PERIOD = 120000;

enum State {CONNECT_WAIT_STATE, IDLE_CONNECTED_STATE, TRACKING_ON_STATE, TRACKING_OFF_STATE };

TinyGPSPlus gps;
FuelGauge batt_soc;

// Ubidots ubidots(ubiToken);

unsigned long lastSerial = 0;
unsigned long lastPublish = 0;

// initial connection state
bool tracking_on = true;
State state;

// multipurpose buffer
char buffer[256];

uint32_t  session_id;

void setup()
{
	/*#ifdef serial_debug
	Serial.begin(9600);
	ubidots.setDebug(true);
	#endif
	*/

	// Uncomment when using Google Fi Sim
	// Particle.keepAlive(240);

	System.on(button_click, buttonHandler);

	// The GPS module is connected to Serial1 (pins TX,RX on electron)
	// by default the Baud rate is 9600 bps. Will be adjusted for faster transmission in the future
	GNSS.begin(9600);

	Particle.function("updateFreq", updatePeriod);
	Particle.function("battery", batteryStatus);
	Particle.function("getLocation", gpsPublish);
	Particle.function("tracking", getState);

	// ubidots.setMethod(TYPE_UDP); // UDP saves on data vs. TCP
	// ubidots.setDatasourceName(DATA_SOURCE_NAME);

	// set time zone
	Time.zone(-4);

session_id = Time.local();

#ifdef serial_debug
delay(2000);
const char *time_c = Time.format(session_id, TIME_FORMAT_DEFAULT).c_str();
Log.info("Local time: %s \n", time_c);
#endif

Cellular.on();
Particle.connect();
// initial state after reset
state = CONNECT_WAIT_STATE;

#ifdef serial_debug
Log.info("connecting %d... \n", Cellular.connecting());
#endif
}


void loop()
{
	while (GNSS.available() > 0) {
		if (gps.encode(GNSS.read())) {
			displayInfo();
		}
	}

	// FSM definition
	switch(state) {
		case CONNECT_WAIT_STATE:
		if (Particle.connected()) {
			#ifdef serial_debug
			Log.info("connected!");
			#endif
			state = IDLE_CONNECTED_STATE;
		}
		break;

		case IDLE_CONNECTED_STATE:
		if (!Particle.connected()) {
			// cloud connection lost
			#ifdef serial_debug
			Log.info("retrying to connect...");
			#endif
			state = CONNECT_WAIT_STATE;
		}
		if (tracking_on) {
			#ifdef serial_debug
			Log.info("tracking on");
			#endif
			state = TRACKING_ON_STATE;
		}
		else {
			#ifdef serial_debug
			Log.info("tracking off");
			#endif
			state = TRACKING_OFF_STATE;
		}
		break;

		case TRACKING_ON_STATE:
		if (!Particle.connected()) {
			// cloud connection lost
			#ifdef serial_debug
			Log.info("retrying to connect...");
			#endif
			state = CONNECT_WAIT_STATE;
		}
		if (!tracking_on) {
			#ifdef serial_debug
			Log.info("switch tracking off");
			#endif
			state = TRACKING_OFF_STATE;
			blink_on_switch();
		}
		break;

		case TRACKING_OFF_STATE:
		if (!Particle.connected()) {
			// cloud connection lost
			#ifdef serial_debug
			Log.info("retrying to connect...");
			#endif
			state = CONNECT_WAIT_STATE;
		}
		if (tracking_on) {
			#ifdef serial_debug
			Log.info("switch tracking on");
			#endif
			state = TRACKING_ON_STATE;
			blink_on_switch();
		}
		break;
	}
}


void displayInfo() {

	#ifdef serial_debug
	if (millis() - lastSerial >= SERIAL_PERIOD) {
		lastSerial = millis();
		if (gps.location.isValid()) {
			snprintf(buffer, sizeof(buffer), "%f,%f,%.1f", gps.location.lat(), gps.location.lng(), gps.altitude.meters());
		}
		else {
			strcpy(buffer, "no fix");
		}
		Log.info(buffer);
	}
	#endif

	if (state == TRACKING_ON_STATE) {
		if (millis() - lastPublish >= PUBLISH_PERIOD) {
			lastPublish = millis();

			if (gps.location.isValid()) {
				const char *pattern = "{\"s_id\": %d, \"lat\": %f, \"lon\": %f, \"alt\": %f, \"spd\": %f}";
				sprintf(buffer, pattern, session_id, gps.location.lat(), gps.location.lng(), gps.altitude.meters(), gps.speed.mph());
				Particle.publish("GNSS/data", buffer, 60, PRIVATE);
			}
			/*
			// ubidots udp upload: Sends latitude and longitude as context of speed variable
			char ubi_geopoint_context[25];
			sprintf(ubi_geopoint_context, "lat=%.6f$lng=%.6f",gps.location.lat(),gps.location.lng());


			ubidots.add("speed_location", gps.speed.mph(), ubi_geopoint_context);
			ubidots.add("battery_state", batt_soc.getSoC());
			ubidots.sendAll();
			*/

		}
	}
}


// Actively check GPS location. Returns "no fix" event if no fix
int gpsPublish(String command) {
	uint8_t exit_code = 0;

	if (gps.location.isValid()) {
		const char *pattern = "{\"s_id\": %d, \"lat\": %f, \"lon\": %f, \"alt\": %f, \"spd\": %f}";
		sprintf(buffer, pattern, session_id, gps.location.lat(), gps.location.lng(), gps.altitude.meters(), gps.speed.mph());
		Particle.publish("GNSS/data", buffer, 60, PRIVATE);
		exit_code = 1;
	}
	else {
		Particle.publish("GNSS/nofix", Time.timeStr());
	}

	lastPublish = millis();
	return exit_code;
}


int updatePeriod(String command) {
	unsigned long _period = command.toInt();
	if (_period == 0) {
		tracking_on = false;
	}
	else if (_period > 0 ) {
		tracking_on = true;
		PUBLISH_PERIOD = _period*1000;
	}
	else {
		tracking_on = true;
		PUBLISH_PERIOD = 120000;
		return -1;
	}
	return _period;
}


int batteryStatus(String command){
	// Publish the battery percentage remaining
	Particle.publish("GNSS/Bat", String::format("%.2f",batt_soc.getSoC()), 60, PRIVATE);
	// if there's more than 10% of the battery left, then return 1
	if (batt_soc.getSoC()>10)
	{ return 1;}
	// use this one as a onetime alert using ifttt
	else if (batt_soc.getSoC()<10 && batt_soc.getSoC()>9){
		return -1;
	}
	// if you're running out of battery, return 0
	else { return 0;}
}


int getState(String command) {
	char state_label[16];
	int exit_code;
	if (state == TRACKING_ON_STATE) {
		strcpy(state_label, "tracking on");
		exit_code = 1;
	}
	else if (state == TRACKING_OFF_STATE) {
		strcpy(state_label, "tracking off");
		exit_code = 0;
	}
	else {
		return -1;
	}
	Particle.publish("GNSS/State", state_label);
	return exit_code;
}


void buttonHandler(system_event_t event, int data) {
	//int times = system_button_clicks(data);//put here for reference
	tracking_on = !tracking_on;
	blink_on_switch();
	#ifdef serial_debug
	Log.info("tracking status: %d", tracking_on);
	#endif

}


void blink_on_switch() {
	// visual indication on tracking state transition
	RGB.control(true);
	RGB.color(255, 49,  0); //orange
	delay(100);
	RGB.color(0, 0,  0);
	delay(100);
	RGB.color(255, 49,  0);
	RGB.control(false);
}
