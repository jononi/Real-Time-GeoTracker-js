/*
* Project: real time asset tracker and logger
* Description: location/speed tracker and logger based on Particle's Electron and Neo M8 GNSS module.
* Location/speed data can be received by a server running Influx DB to report and save location in real time on a map.
* Author: jaafar benabdallah
* Last updated: March 2018
* v 0.17 -> log location(+geohash)/speed/altitude to influxdb via node.js service
* v 0.18 -> fixed keep alive feature when using Google Fi sim card
* v 0.2 -> added oled display, use of software timers for publishing data and refreshing display
* v 0.3 -> using ubx messaging protocol to communicate with GNSS module. Added software watchdog.
* v 0.31 -> dropped usage of software timers for tracking data publishing and display refreshing. (was not thread safe)
* TODO v 0.4 -> add IMU unit, deep sleep mode with wake on motion detection
* TODO v 0.5 -> add basic dashboard info display: current speed, travel time/distance, current acceleration
* Credits: Rick K (rickkas7) for FSM and cellular location, Ankur Dave for ublox protocol library for the Asset Tracker 2
*/

#if ( PLATFORM_ID == PLATFORM_ELECTRON_PRODUCTION)
#define cellular
#endif

#define serial_debug

#ifdef cellular
#include "cellular_hal.h"
#include "CellularHelper.h"
#endif
#include "Adafruit_GFX.h"
#include "Adafruit_SH1106_Particle.h"
#include "ubx_neom8n.h"


// display control wiring
#define OLED_CS     A2  // /SS 128x64 OLED (in addition to SCK, MOSI on SPI)
#define OLED_DC     A0
#define OLED_RESET  A1

// Startup options configurtion
#ifdef cellular
STARTUP(cellular_credentials_set("h2g2", "", "", NULL));
#endif
SYSTEM_MODE(SEMI_AUTOMATIC);
SYSTEM_THREAD(ENABLED);

// Time related configuration
const unsigned long KEEPALIVE_PERIOD = 60; // in seconds
const unsigned long DISPLAY_PERIOD = 1000; //ms
unsigned long PUBLISH_PERIOD = 20000; //ms
const unsigned long LED_PERIOD = 2000; //ms
const unsigned long CELLULAR_LOC_PERIOD = 30000;

// Serial debug configuration
#ifdef serial_debug
// SerialLogHandler logHandler(115200, LOG_LEVEL_TRACE);
// SerialLogHandler logHandler(115200, LOG_LEVEL_INFO);
SerialLogHandler logHandler(115200, LOG_LEVEL_WARN, {
	{ "app", LOG_LEVEL_INFO },
	{ "app.custom", LOG_LEVEL_INFO }
});
#endif

// tracking variables
uint32_t  session_id;
bool tracking_on_flag = false;
bool assistNow_on_flag = true;
bool cellular_location_on_flag = true;

// timing variables
uint32_t  last_connection_elapsed; // time in ms since last particle (dis)connect event
uint32_t  last_cellular_location_elapsed = 0;
uint32_t	last_display_refresh = 0;
uint32_t	last_tracking_publish = 0;

// gnss stats
uint32_t gnss_begin_ms = 0;
uint32_t ttff = 0;
uint32_t gnss_last_fix_ms = 0;

// gnss data
bool valid_fix_flag = false;
double lat = 0.0, lon = 0.0, alt = 0.0, acc = 0.0, speed_mph = 0.0;
uint8_t nb_sats = 0;
const double mm_per_second_per_mph = 447.04; // mm/s -> mph

// text buffers
char buffer[256];
char button_buffer[32];

// FSM states
enum State {CONNECT_WAIT_STATE, TRACKING_OFF_STATE, TRACKING_ON_STATE};
State state;

// forward declaration required before calling UBX_NEOM8N constructor
void handleGNSSMessage(uint16_t msg_class_id, const ubx_buf_t &buf);

// global instances
UBX_NEOM8N gnss(handleGNSSMessage);
#ifdef cellular
FuelGauge battery;
#endif
Adafruit_SH1106 display(OLED_DC, OLED_RESET, OLED_CS);
//ApplicationWatchdog wd(60000, System.reset);

// software timers
Timer trackingLEDTimer(LED_PERIOD, trackingLEDUpdate);
//Timer watchdogTimer(1000, wd.checkin);


void setup() {

  #ifdef cellular
  battery.wakeup();
  battery.quickStart();
  #endif

  #ifdef serial_debug
  Serial.begin(115200);
	delay(5000);
	#endif

  gnss_begin_ms = millis();
	gnss_last_fix_ms = millis();
  resetGNSSInfo();
  gnss.start(&Serial1, 9600);

  //system events
  // register SETUP button event handler
  System.on(button_final_click, buttonHandler);
  // register cloud event handler
  System.on(cloud_status, cloud_statusHandler);
  // blue LED on D7 is used to indicate tracking is on
  pinMode(D7, OUTPUT);
  digitalWrite(D7, LOW);

  // display setting up
  display.begin(SH1106_SWITCHCAPVCC);
  display.display(); //shows splash screen

	// start connecting to the internet and cloud
	#ifdef cellular
	Cellular.on();
	#else
	WiFi.on();
	#endif
	Particle.connect();
	delay(2000); // give some time for cloud connection
	// adjust local time
	Time.zone(-5);
	Time.setDSTOffset(1.0);
	Time.beginDST(); // uncomment when it's daylight saving time

	// startup display
	display.clearDisplay();
	display.setTextSize(1);
	display.setTextColor(WHITE);
	display.setCursor(48,0);
	display.printf("%02d:%02d", Time.hour(), Time.minute());
  #ifdef cellular
  display.setCursor(104, 0);
	display.printf("%.0f%%", battery.getSoC());
  #endif
	display.drawFastHLine(0, 9, display.width(), WHITE);
	display.setCursor(6, 10);
	display.print("waiting for fix ...");
	display.display();


	Particle.function("Set_Tracking", startTracking);
	Particle.function("Get_Battery", batteryStatus);
	Particle.function("Get_Status", getStatus);
	Particle.function("Get_Location", gnssPublish);

  session_id = Time.local(); // this is UTC...

	// initial state after reset
	state = CONNECT_WAIT_STATE;

  //watchdogTimer.start();
	last_tracking_publish = millis();
	last_display_refresh = millis();
}


void loop() {
	// FSM transitions
	switch(state) {
		case CONNECT_WAIT_STATE:
		if (Particle.connected()) {
      if (tracking_on_flag) {
        state = TRACKING_ON_STATE;
        trackingLEDTimer.start();
      } else {
        state = TRACKING_OFF_STATE;
      }
		} else {
			// keep retrying
      #ifdef cellular
			if (Cellular.ready()) {
				Particle.connect();
			} else {
				Cellular.connect();
			}
			#else
			if (WiFi.ready()) {
				Particle.connect();
			} else {
				WiFi.connect();
			}
      #endif
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
      trackingLEDTimer.start();
		  //blink_on_switch();
		}
		break;

		case TRACKING_ON_STATE:
		if (!Particle.connected()) {
			// cloud connection lost
			#ifdef serial_debug
			Log.info("retrying to connect...");
			#endif
			state = CONNECT_WAIT_STATE;
      trackingLEDTimer.stop();
			digitalWrite(D7, LOW);
		}
		if (!tracking_on_flag) {
			#ifdef serial_debug
			Log.info("switch tracking off");
			#endif
			state = TRACKING_OFF_STATE;
      trackingLEDTimer.stop();
      digitalWrite(D7, LOW);
			//blink_on_switch();
		}
		break;
	} //FSM transitions

  // one time actions that need to run after cloud  connected
	static bool keepAlive_wait = false;
	static bool keepAlive_set = false;
	static uint32_t first_time_connected = 0;

	if (!keepAlive_wait && Particle.connected()) {
		keepAlive_wait = true;
		first_time_connected = millis(); // don't change keepalive immediately after cloud connect...

    if (Time.isDST()) {
			#ifdef serial_debug
			Log.info("Day light saving time detected");
			Log.info(Time.timeStr());
			#endif
		}

    // update this value while at it
    last_connection_elapsed = millis();

		#ifdef serial_debug
		Log.info("started the wait to update keepalive...");
		#endif
	}
	// but wait for cloud connection to settle down to make sure the change is registered
	if (!keepAlive_set && (millis() - first_time_connected) > 3000) {
    #ifdef cellular
		Particle.keepAlive(KEEPALIVE_PERIOD);
    #endif
		keepAlive_set = true;
		#ifdef serial_debug
		Log.info("keepalive period updated to %d seconds.", KEEPALIVE_PERIOD);
		#endif
	}

	// in Tracking On mode, check if it's time to publish GNSS data
	if (tracking_on_flag && millis() > (last_tracking_publish +  PUBLISH_PERIOD)) {
		trackingOnPublish();
	 	last_tracking_publish = millis();
	}

	// check if it's time to refresh display info
	if (millis() > (last_display_refresh + DISPLAY_PERIOD)) {
		display_update();
		last_display_refresh = millis();
	}

  /* get assist now data provided by u-blox. Requires an HTTP request and a valid token provided by u-blox
  try it every 10 sec while waiting for a fix */
	/*
	if (!valid_fix_flag && assistNow_on_flag) {
		gnss.assist(); //blocks for up to 10 sec, needs cloud connection
  }
	*/

  /* get location information provided by cellular network.
  *  Not available for WiFi devices

  #ifdef cellular
  if (!valid_fix_flag && cellular_location_on_flag) {
    if (millis() - last_cellular_location_elapsed > CELLULAR_LOC_PERIOD) {
      // Get cellular location while waiting for GPS
      CellularHelperLocationResponse cell_loc = CellularHelper.getLocation();
      if (cell_loc.valid) {
				const char *pattern = "{\"s_id\": %d, \"lat\": %f, \"lon\": %f, \"alt\": %f, \"spd\": %f, \"acc\": %d}";
				sprintf(buffer, pattern, session_id, cell_loc.lat, cell_loc.lon, cell_loc.alt, 0.0, cell_loc.uncertainty); //no speed data for cellular location
        Log.info(buffer);
				// log this location only if it's within 4 km accurate
        if (tracking_on_flag && cell_loc.uncertainty < 4000) {
			    Particle.publish("GNSS/data", buffer, 60, PRIVATE);
        }
      } else {
        Log.info("Failed to get cellular location.");
      }
    }
  }
  #endif
	not ready for this yet */
}//loop


void handleGNSSMessage(uint16_t msg_class_id, const ubx_buf_t &buf) {
    switch (msg_class_id) {
    case UBX_MSG_NAV_PVT:
			valid_fix_flag = ((buf.payload_rx_nav_pvt.flags & UBX_RX_NAV_PVT_FLAGS_GNSSFIXOK) == 1);
			lat = (double)buf.payload_rx_nav_pvt.lat * 1e-7;
			lon	= (double)buf.payload_rx_nav_pvt.lon * 1e-7;
	    alt = (double)buf.payload_rx_nav_pvt.height * 1e-3;
	    acc = (double)buf.payload_rx_nav_pvt.hAcc * 1e-3;
	    nb_sats = buf.payload_rx_nav_pvt.numSV;
	    speed_mph = (double)buf.payload_rx_nav_pvt.gSpeed / mm_per_second_per_mph;
	    // update time of latest update
	    if (valid_fix_flag) {
				gnss_last_fix_ms = millis();// keep it up to date unless fix is lost
			}
	    // time to first fix
	    if (ttff == 0 && valid_fix_flag) {
	      ttff = millis() - gnss_begin_ms;
				Log.info("ttff=%d ms", ttff);
	    }
	    break;
		default:
			valid_fix_flag = 0; // reset flag if no successful nav_pvt message parsing
    }
}

/* this runs in between calls to loop() to
* feed gnss parser with available data on the UART */
void serialEvent1() {
  gnss.update();
}

void resetGNSSInfo() {
    valid_fix_flag = false;
    lat = lon = alt = acc = speed_mph = 0.0;
    nb_sats = 0;
    ttff = 0;
}

// flash blue led if tracking is on
void trackingLEDUpdate() {
if (state == TRACKING_ON_STATE) {
	digitalWrite(D7, HIGH);
	delay(50);
	digitalWrite(D7, LOW);
	}
}

/* this is called from loop() */
void trackingOnPublish() {
	#ifdef serial_debug
	Log.info("publish gnss data called");
	Log.info("fix age: %d ms, validity: %d", millis() - gnss_last_fix_ms, valid_fix_flag);
	#endif

  if (valid_fix_flag && (millis() - gnss_last_fix_ms) < 10000) {
		const char *pattern = "{\"s_id\": %d, \"age\": %d, \"spd\": %f, \"lat\": %f, \"lon\": %f, \"alt\": %f}";
		snprintf(buffer, sizeof(buffer), pattern, session_id, millis() - gnss_last_fix_ms, speed_mph, lat, lon, alt);
		Particle.publish("GNSS/data",buffer, 60, PRIVATE);
	}
	if (valid_fix_flag && (millis() - gnss_last_fix_ms) >= 10000) {
		snprintf(buffer, sizeof(buffer), "fix age: %d ms", millis() - gnss_last_fix_ms);
		Particle.publish("GNSS/obsolete", buffer, 60, PRIVATE);
	}
  if (!valid_fix_flag) {
    snprintf(buffer, sizeof(buffer), "no fix: %d s", (int)(millis() - gnss_begin_ms) / 1000);
    Particle.publish("GNSS/no fix", buffer, 60, PRIVATE);
  }
}


// Actively check GPS location using cloud function. Returns "no fix" event if no fix
int gnssPublish(String command) {
	int exit_code =  -1;
	#ifdef serial_debug
	Log.info("publish gnss data called");
  snprintf(buffer, sizeof(buffer), "fix age: %d ms", millis() - gnss_last_fix_ms);
	Log.info(buffer);
	#endif

  if (valid_fix_flag && (millis() - gnss_last_fix_ms) < 10000) {
    const char *pattern = "{\"s_id\": %d, \"lat\": %f, \"lon\": %f, \"alt\": %f, \"spd\": %f}";
    sprintf(buffer, pattern, session_id, lat, lon, alt, speed_mph);
    Particle.publish("GNSS/data", buffer, 60, PRIVATE);
		exit_code =  millis() - gnss_last_fix_ms;
	}

  if (!valid_fix_flag) {
    snprintf(buffer, sizeof(buffer), "no fix: %d s", (int)((millis() - gnss_begin_ms) / 1000));
    Particle.publish("GNSS/no fix", Time.timeStr(), 60, PRIVATE);
    return exit_code; //don't do next test
  }

	if (millis() - gnss_last_fix_ms > 10000) {
		Particle.publish("GNSS/obsolete", Time.timeStr(), 60, PRIVATE);
		exit_code =  millis() - gnss_last_fix_ms;
	}

	return exit_code;
}


/* remotely start/stop tracking and tracking period
* command = 0..10 --> tracking off
* command >= 10 and < 1800 --> tracking on, published every command seconds
*/
int startTracking(String command) {
	unsigned long _period = command.toInt();
	if (_period >= 10 && _period < 1800) {
		tracking_on_flag = true;
		PUBLISH_PERIOD = _period*1000;
	}
	else {
		// any other value will stop real time tracking and logging
		tracking_on_flag = false;
		PUBLISH_PERIOD = 20000; // for serial debug
	}
	return _period;
}


int batteryStatus(String command){
	// Publish the battery percentage remaining
  #ifdef cellular
	float batterySoc = battery.getSoC();
  #else
  float batterySoc = 100.0;
  #endif
	Particle.publish("GNSS/Bat", String::format("%.2f", batterySoc), 60, PRIVATE);
	// if there's more than 10% of the battery left, then return 1
	if (batterySoc < 10 && batterySoc > 9) {
		return -1;
	}
	else {
		return (int) batterySoc;
	}
}


int getStatus(String command) {
	char state_label[16];
	int exit_code;
	if (state == TRACKING_ON_STATE) {
		strcpy(state_label, "tracking on");
		exit_code = (int)(PUBLISH_PERIOD / 1000);
	}
	else if (state == TRACKING_OFF_STATE) {
		strcpy(state_label, "tracking off");
		exit_code = 0;
	}
	else {
		return -1;
	}
	Particle.publish("GNSS/Status", state_label, 60, PRIVATE);
	return exit_code;
}


void buttonHandler(system_event_t event, int data) {
	int nb_clicks = system_button_clicks(data);
	sprintf(button_buffer, "%d clicks in %d ms", nb_clicks, data);
	if (nb_clicks == 2) {
		// turn off display and GNSS module before Electron enters sleep mode
		display.SH1106_command(SH1106_DISPLAYOFF);
		//shutdown GNSS/oled power supply
    gnss.stop(); // takes care of shutting down Pololu S7V8F3 voltage regulator
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
	if (data == cloud_status_connected) last_connection_elapsed = 0;
	#ifdef serial_debug
	switch(data) {
		case cloud_status_connecting:
		Log.info("connecting to cloud");
		break;

		case cloud_status_connected:
		Log.info("connected to cloud");
		break;

		case cloud_status_disconnected:
		Log.info("disconnected from cloud");
		break;
	}
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
    #ifdef cellular
		if(Cellular.connecting()) display.print("--");
		if(Cellular.ready()) display.print("Fi");
    #else
    if(WiFi.connecting()) display.print("--");
		if(WiFi.ready()) display.print("Fi");
    #endif
	}
	// gnss fix status indicator
	display.setCursor(20, 0);
	display.printf("%s", valid_fix_flag?"*":"~");
	// tracking status indicator
	display.setCursor(32, 0);
	display.printf("%s", tracking_on_flag?"T":" ");
	// current local time
	display.setCursor(48, 0);
	display.printf("%02d:%02d%s", Time.hourFormat12(), Time.minute(), Time.isAM()?"AM":"PM");
	// battery state of charge
  #ifdef cellular
	display.setCursor(104, 0);
	display.printf("%.0f%%", battery.getSoC());
  #endif
}

void display_update() {
	display.clearDisplay();
	status_bar_update();
	// last fix age
	display.setCursor(0, 11);
	if (valid_fix_flag) {
		display.printf("fix age: %d ms", millis() - gnss_last_fix_ms);
	} else {
		display.print("waiting for fix...\n");
	}
	// last fix accuracy
	display.setCursor(0, 19);
	if (valid_fix_flag) {
		display.printf("hAcc: %.3f", acc);
	} else {
		display.printf("hAcc: --");
	}
	// number of satellites
	display.setCursor(0, 27);
	if (valid_fix_flag) {
		display.printf("nb sats: %d", nb_sats) ;
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
