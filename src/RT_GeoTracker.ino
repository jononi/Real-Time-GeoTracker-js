/*
* Project: real time asset tracker and logger
* Description: location/speed tracker and logger based on Particle's Electron and Neo M8 GNSS module. 
* Location/speed data can be received by a server running Influx DB to report and save location in real time on a map.
* Author: jaafar benabdallah
* Last updated: March 2018
* v 0.17 -> log location(+geohash)/speed/altitude to influxdb via node.js service
* v 0.18 -> fixed keep alive feature when using Google Fi sim card
* v 0.2 -> added oled display, use of software timers for publishing data and refreshing display
* TODO v 0.3 -> add IMU unit, deep sleep mode and wake on motion detection
* TODO v 0.4 -> add basic dashboard info display: current speed, travel time/distance, current acceleration
* Credits: Rick K (rickkas7) for the Asset Tracker code and Mikal Hart for the TinyGPSPlus+ library and examples
*/

#include "cellular_hal.h"
#include "Serial5/Serial5.h"
#include "Adafruit_GFX.h"
#include "Adafruit_SH1106_Particle.h"
#include "TinyGPSPlus.h" //updated to v1.0.2

#define serial_debug

#define	GNSS			Serial5		// GNSS serial
#define OLED_CS     A2  // /SS 128x64 OLED (in addition to SCK, MOSI on SPI)
#define OLED_DC     A0
#define OLED_RESET  A1

// Startup configurtion options
STARTUP(cellular_credentials_set("h2g2", "", "", NULL));
SYSTEM_MODE(SEMI_AUTOMATIC);
SYSTEM_THREAD(ENABLED);

// Global variables
const unsigned long KEEPALIVE_PERIOD = 60; // in seconds
const unsigned long DISPLAY_PERIOD = 1000; //ms
unsigned long PUBLISH_PERIOD = 60000; //ms
const unsigned long LED_PERIOD = 2000; //ms

#ifdef serial_debug
// SerialLogHandler logHandler(115200, LOG_LEVEL_TRACE);
SerialLogHandler logHandler(115200, LOG_LEVEL_INFO);
/*
SerialLogHandler logHandler(LOG_LEVEL_WARN, {
	{ "app", LOG_LEVEL_INFO },
	{ "app.custom", LOG_LEVEL_INFO }s
});*/
#endif

bool tracking_on_flag = false;
uint32_t  session_id;
uint32_t last_connection_elapsed; // time in ms since last particle (dis)connect event

// text buffers
char sentenceBuf[256];
char buffer[256];
char button_buffer[32];

size_t sentenceBufOffset = 0;

// FSM states
enum State {CONNECT_WAIT_STATE, TRACKING_OFF_STATE, TRACKING_ON_STATE};
State state;

// global instances
TinyGPSPlus gps;
FuelGauge battery;
Adafruit_SH1106 display(OLED_DC, OLED_RESET, OLED_CS);
Timer displayTimer(DISPLAY_PERIOD, display_update);
Timer publishTimer(PUBLISH_PERIOD, publishData);
Timer trackingLEDTimer(LED_PERIOD, trackingLEDUpdate);


void setup() {
	Cellular.on();
	Particle.connect();

	#ifdef serial_debug
	const char *time_c = Time.format(session_id, TIME_FORMAT_DEFAULT).c_str();
	if (Cellular.connecting()) Log.info("%s: connecting on... \n", time_c );
	#endif

	// display setting up
	display.begin(SH1106_SWITCHCAPVCC);
	display.display(); //shows splash screen

	// register SETUP button event handler
	//System.on(button_click, buttonHandler);
	System.on(button_final_click, buttonHandler);

	// register cloud event handler
	System.on(cloud_status, cloud_statusHandler);

	// use D7 LED to indicate tracking is on
	pinMode(D7, OUTPUT);

	// set time zone
	Time.zone(-5);
	// account for daylight saving time if it's active:
	if (Time.isDST()) Time.beginDST();

	// initial state after reset
	state = CONNECT_WAIT_STATE;

	session_id = Time.local(); //it's still UTC...

	// pre-gps fix screen
	display.clearDisplay();
	display.setTextSize(1);
	display.setTextColor(WHITE);
	display.setCursor(48,0);
	display.printf("%02d:%02d", Time.hour(), Time.minute());
	display.setCursor(104, 0);
	display.printf("%.0f%%", battery.getSoC());
	display.drawFastHLine(0, 9, display.width(), WHITE);
	display.setCursor(6, 10);
	display.print("waiting for fix ...");
	display.display();
	displayTimer.start();

	publishTimer.start();

	Particle.function("Set_Period", updatePeriod);
	Particle.function("Get_Battery", batteryStatus);
	Particle.function("Get_Status", getState);
	Particle.function("Get_Location", gpsPublish);

	delay(2000);

	// The GPS module is connected to GNSS UART (Serial5)
	GNSS.begin(9600);
}


void loop() {
	// FSM transitions
	switch(state) {
		case CONNECT_WAIT_STATE:
		if (Particle.connected()) {
			#ifdef serial_debug
			Log.info("connected to Particle Cloud!");
			#endif
			state = tracking_on_flag?TRACKING_ON_STATE:TRACKING_OFF_STATE;
		} else {
			// keep retrying
			if (Cellular.ready()) {
				Particle.connect();
			} else {
				Cellular.connect();
			}
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
		if (tracking_on_flag) {
			#ifdef serial_debug
			Log.info("switch tracking on");
			#endif
			state = TRACKING_ON_STATE;
		  blink_on_switch();
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
		if (!tracking_on_flag) {
			#ifdef serial_debug
			Log.info("switch tracking off");
			#endif
			state = TRACKING_OFF_STATE;
			blink_on_switch();
		}
		break;
	} //FSM transitions

	/* keepalive setting change steps while in SEMI_AUTO and Threaded mode
	https://community.particle.io/t/event-loop-error-3-and-keepalive-keep-alive-times-solved/37062/4
	*/
	static bool keepAlive_wait = false;
	static bool keepAlive_set = false;
	static uint32_t first_time_connected = 0;

	if (!keepAlive_wait && Particle.connected()) {
		keepAlive_wait = true;
		first_time_connected = millis(); // don't change keepalive immediately after cloud connect...
		#ifdef serial_debug
		Log.info("started the wait to update keepalive...");
		#endif
	}
	// but wait for cloud connection to settle down to make sure the change is registered
	if (!keepAlive_set && (millis() - first_time_connected) > 3000) {
		Particle.keepAlive(KEEPALIVE_PERIOD);
		keepAlive_set = true;
		#ifdef serial_debug
		Log.info("keepalive period updated to %d seconds.", KEEPALIVE_PERIOD);
		#endif
	}

}//loop

// this runs in between calls to loop (in its own thread?, not in interrupt context anyway)
// feed gps parser with available data on the UART
void serialEvent5() {
	while (GNSS.available() > 0) {
		gps.encode(GNSS.read());
	}
}


/* this is called by a software timer every PUBLISH_PERIOD ms */
void publishData() {

	#ifdef serial_debug
	if (gps.location.isValid()) {
		snprintf(buffer, sizeof(buffer), "%f,%f,%.1f", gps.location.lat(), gps.location.lng(), gps.altitude.meters());
	}
	else {
		strcpy(buffer, "no fix");
	}

	Log.info(buffer);
	Log.info("fix age: %d ms", gps.location.age());

	if (Cellular.connecting()) Log.info("%02d:%02d:%02d : connecting...\n", Time.hour(), Time.minute(), Time.second());
	if (Cellular.ready())	Log.info("%02d:%02d:%02d : connected.\n", Time.hour(), Time.minute(), Time.second());
	#endif

	if (state == TRACKING_ON_STATE) {
		if (gps.location.isValid() && gps.location.age() < 10000) {
			const char *pattern = "{\"s_id\": %d, \"lat\": %f, \"lon\": %f, \"alt\": %f, \"spd\": %f}";
			sprintf(buffer, pattern, session_id, gps.location.lat(), gps.location.lng(), gps.altitude.meters(), gps.speed.mph());
			Particle.publish("GNSS/data", buffer, 60, PRIVATE);
		} else {
			// this will only be using during debug phase
			sprintf(buffer, "last update was %d ms ago", gps.location.age());
			Particle.publish("GNSS/data/obsolete", buffer, 60, PRIVATE);
		}
	}
}


// Actively check GPS location. Returns "no fix" event if no fix
int gpsPublish(String command) {
	int exit_code =  -1;
	#ifdef serial_debug
	Log.info("publish called");
	Log.info("fix age: %d", gps.location.age());
	Log.info("fix updated? %d", gps.location.isUpdated());
	#endif

	if (gps.location.isUpdated() && gps.location.age() < 60000) {
		const char *pattern = "{\"s_id\": %d, \"lat\": %f, \"lon\": %f, \"alt\": %f, \"spd\": %f}";
		sprintf(buffer, pattern, session_id, gps.location.lat(), gps.location.lng(), gps.altitude.meters(), gps.speed.mph());
		Particle.publish("GNSS/data", buffer, 60, PRIVATE);
		exit_code =  gps.location.age();
	}

	if (gps.location.age() > 60000) {
		Particle.publish("GNSS/obsolete", Time.timeStr(), 60, PRIVATE);
		exit_code =  gps.location.age();
	}

	if (!gps.location.isValid()) {
		Particle.publish("GNSS/no fix", Time.timeStr(), 60, PRIVATE);
	}

	return exit_code;
}


int updatePeriod(String command) {
	unsigned long _period = command.toInt();
	if (_period > 0 && _period < 1800) {
		tracking_on_flag = true;
		PUBLISH_PERIOD = _period*1000;
		publishTimer.changePeriod(PUBLISH_PERIOD);
	}
	else {
		// any other value will stop real time tracking and logging
		tracking_on_flag = false;
		PUBLISH_PERIOD = 30000; // for serial debug
		publishTimer.changePeriod(PUBLISH_PERIOD);
		_period = 0;
	}
	return _period;
}


int batteryStatus(String command){
	// Publish the battery percentage remaining
	float batterySoc = battery.getSoC();
	Particle.publish("GNSS/Bat", String::format("%.2f", batterySoc), 60, PRIVATE);
	// if there's more than 10% of the battery left, then return 1
	if (batterySoc < 10 && batterySoc > 9) {
		return -1;
	}
	else {
		return (int) batterySoc;
	}
}


int getState(String command) {
	char state_label[16];
	int exit_code;
	if (state == TRACKING_ON_STATE) {
		strcpy(state_label, "tracking on");
		exit_code = PUBLISH_PERIOD / 1000;
	}
	else if (state == TRACKING_OFF_STATE) {
		strcpy(state_label, "tracking off");
		exit_code = 0;
	}
	else {
		return -1;
	}
	Particle.publish("GNSS/State", state_label, 60, PRIVATE);
	return exit_code;
}


void buttonHandler(system_event_t event, int data) {
	int nb_clicks = system_button_clicks(data);
	sprintf(button_buffer, "%d clicks in %d ms", nb_clicks, data);
	if (nb_clicks == 2) {
		// turn off display and GNSS module before Electron enters sleep mode
		display.SH1106_command(SH1106_DISPLAYOFF);
		//TODO: shutdown GNSS/oled power supply
		#ifdef serial_debug
		Log.info(button_buffer);
		#endif
	}
	if (nb_clicks == 1) {
		tracking_on_flag = !tracking_on_flag;
		#ifdef serial_debug
		Log.info("tracking status: %s",tracking_on_flag?"on":"off");
		#endif
	}
}


void cloud_statusHandler(system_event_t event, int data) {
	#ifdef serial_debug
	Log.info("cloud status: %d\n", data);
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


void status_bar_update() {
	display.drawFastHLine(0, 9, display.width(), WHITE);
	// cellular network cloud status indicator
	display.setCursor(0, 0);
	if(Particle.connected()) {
		display.print("CC");
	} else {
		if(Cellular.connecting()) display.print("--");
		if(Cellular.ready()) display.print("Fi");
	}
	// gnss fix status indicator
	display.setCursor(20, 0);
	display.printf("%s", gps.location.isUpdated()?"*":"~");
	// tracking status indicator
	display.setCursor(32, 0);
	display.printf("%s", tracking_on_flag?"T":" ");
	// current local time
	display.setCursor(48, 0);
	display.printf("%02d:%02d%s", Time.hourFormat12(), Time.minute(), Time.isAM()?"AM":"PM");
	// battery state of charge
	display.setCursor(104, 0);
	display.printf("%.0f%%", battery.getSoC());
}

void display_update() {
	display.clearDisplay();
	status_bar_update();
	// last fix age
	display.setCursor(0, 11);
	if (gps.location.isValid()) {
		display.printf("fix age: %d ms", gps.location.age());
	} else {
		display.print("waiting for fix...\n");
	}
	// last fix hdop
	display.setCursor(0, 19);
	if (gps.location.isValid()) {
		display.printf("hdop: %.3f", gps.hdop.hdop());
	} else {
		display.printf("hdop: --");
	}
	// number of satellites
	display.setCursor(0, 27);
	if (gps.satellites.isValid()) {
		display.printf("nb sats: %d", gps.satellites.value()) ;
	} else {
		display.printf("nb sats: --");
	}
	// minutes since last (dis)connect event
	uint16_t minutes = round((millis() - last_connection_elapsed) / 60000);
	display.setCursor(0, 35);
	display.printf(" %d min", minutes);

	// button data if it was pressed
	display.setCursor(0, 43);
	if (strlen(button_buffer) > 0) {
		display.print(button_buffer);
	}
	// consume the button button buffer
	button_buffer[0]='\0';

	display.display();
}

// flash blue led if tracking is on
void trackingLEDUpdate() {
if (state == TRACKING_ON_STATE) {
	digitalWrite(D7, HIGH);
	delay(50);
	digitalWrite(D7, LOW);
	}
}


