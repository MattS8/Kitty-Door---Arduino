/*
 Name:		KittyDoor.ino
 Created:	12/27/2021 3:47:29 PM
 Author:	Matthew Steinhardt
*/

#include <ESP8266WiFi.h>

#include "KittyDoor.h"
#include "Credentials.h"
#include <Firebase_ESP_Client.h>

#pragma region BUILD TYPES
	#define LEGACY_TOKEN
	#define REMOTE_DEBUGGING
#pragma endregion


/*
    'status.old' and 'status.current' are used to create one-shot events needed for updating firebase.
    'status.desired' is used to create one-shot events to trigger door changes.
*/
DoorStatus status = { NONE, NONE, NONE };

String command = NONE;          // The next command the door should perform

KittyDoorOptions options;       // All values pertaining to door options (i.e. triggering light levels, etc)
KittyDoorValues values;         // All values pertaining to current door status (i.e. current light level, state, etc)
DebugValues	debugValues;		// All metadata needed for remote debugging

FirebaseConfig fbConfig;		// Configuration for connecting to firebase
FirebaseAuth fbAuth;			// Utilizes the email and pass set up in Credentials.h
FirebaseData fbRecvData;		// Data object used to recieve commands from firebase
FirebaseData fbSendData;		// Data object used to send status/debug messages to firebase

void setup() 
{
	Serial.begin(9600);

    set_pin_modes();

    set_default_door_options();

	connect_to_wifi();

	connect_to_firebase();

	setup_remote_debugging();

	setup_door_options();

	begin_streaming();
}


void loop() 
{
    read_hardware_override();

    read_door_values();

    /*
    * Set old status to current before reading the new status of the door.
    * This will allow us to determine if the door status has changed, and thus
    * whether or not we should send a status update to firebase.
    */
    status.old = status.current;

    /*
    * 'command' will be changed whenever the command endpoint is changed on firebase.
    * After 'run_new_command' is finished, 'command' will return to NONE. This 
    * allows us to only react to database changes once.
    */
    if (command != NONE)
        run_new_command();

    /*
    * Check the current status of the door and what it should be doing. Make sure
    * the open/close motors are not running if the door is fully opened/closed
    * respectively. Also make sure to check the hardware override first, ignoring
    * any firebase commands/statuses when in override mode.
    */
    operate_on_door();

    /*
    * Check to see if the status has changed after operating on the door.
    * If so, a status update is sent to firebase.
    */
    check_status_update();
}

#pragma region Setup Functions
void set_default_door_options()
{
    options.openLightLevel = 190;
    options.closeLightLevel = 40;
    options.delayOpening = false;
    options.delayOpeningVal = 0;
    options.delayClosing = false;
    options.delayClosingVal = 0;

    values.delayOpening = -1;
    values.delayClosing = -1;
}

void set_pin_modes()
{
    pinMode(PIN_LIGHT_SENSOR, INPUT);       // Ambient light from photo sensor
    pinMode(PIN_UP_SENSE, INPUT_PULLUP);    // Low when door is fully open
    pinMode(PIN_DOWN_SENSE, INPUT_PULLUP);  // Low when door is fully closed
    pinMode(PIN_FORCE_OPEN, INPUT_PULLUP);  // If low, force door to open
    pinMode(PIN_FORCE_CLOSE, INPUT_PULLUP); // If low, force door to close

    pinMode(PIN_OPEN_MOTOR, OUTPUT);  // If HIGH, door will open
    pinMode(PIN_CLOSE_MOTOR, OUTPUT); // If HIGH, door will close

    digitalWrite(PIN_OPEN_MOTOR, LOW);  // Don't close door
    digitalWrite(PIN_CLOSE_MOTOR, LOW); // Don't open door
}

void connect_to_wifi()
{
	WiFi.begin(WIFI_AP_NAME, WIFI_AP_PASS);

	while (WiFi.status() != WL_CONNECTED)
	{
		delay(500);
		Serial.print(".");
	}

	Serial.print("\nConnected with IP: "); Serial.println(WiFi.localIP()); Serial.println("");
}

void connect_to_firebase()
{
#ifdef LEGACY_TOKEN
	fbConfig.database_url = FIREBASE_HOST;
	fbConfig.signer.tokens.legacy_token = FIREBASE_SECRET;
#else
	fbConfig.host = FIREBASE_HOST;
	fbConfig.api_key = FIREBASE_API;

	fbAuth.user.email = FIREBASE_USERNAME;
	fbAuth.user.password = FIREBASE_PASS;
#endif // LEGACY_TOKEN

	Firebase.begin(&fbConfig, &fbAuth);

	Firebase.reconnectWiFi(true);
	Firebase.RTDB.setMaxRetry(&fbRecvData, 4);
	Firebase.RTDB.setMaxErrorQueue(&fbRecvData, 30);

	fbRecvData.setResponseSize(1024);
}

void setup_remote_debugging()
{
	Serial.println("Fetching log counter...");
	while (!Firebase.ready())
		;

	if (Firebase.RTDB.get(&fbRecvData, "debug/kitty_door/msg_count"))
	{
		debugValues.logCounter = (unsigned int)fbRecvData.to<int>();
		Serial.print("Log counter set to: "); Serial.println(debugValues.logCounter);
	}
	else
	{
		Serial.println("FAILED");
		Serial.println("REASON: " + fbRecvData.errorReason());
	}
}

void setup_door_options()
{
	Serial.println("Fetching door options...");
	if (Firebase.RTDB.get(&fbRecvData, PATH_OPTIONS))
	{
		handleNewOptions(fbRecvData.to<FirebaseJson*>());
	}
	else 
	{
		Serial.println("FAILED");
		Serial.println("REASON: " + fbRecvData.errorReason());
	}
}

void begin_streaming()
{
    Serial.print("Streaming to: "); Serial.println(PATH_OPTIONS);

    if (!Firebase.RTDB.beginStream(&fbRecvData, PATH_OPTIONS))
    {
        Serial.println("------------------------------------");
        Serial.println("Can't begin stream connection...");
        Serial.println("REASON: " + fbRecvData.errorReason());
        Serial.println("------------------------------------");
        Serial.println();
    }

    Firebase.RTDB.setStreamCallback(&fbRecvData, handleDataRecieved, handleTimeout);
}

#pragma endregion

#pragma region Firebase Data Handlers
void handleTimeout(bool timeout)
{
    if (timeout)
        Serial.println("\nStream timeout, resume streaming...\n");
}

void handleDataRecieved(FirebaseStream data)
{
    if (data.dataType() == "json")
    {
        Serial.println("Stream data available...");
        // Serial.println("STREAM PATH: " + data.streamPath());
        // Serial.println("EVENT PATH: " + data.dataPath());
        // Serial.println("DATA TYPE: " + data.dataType());
        // Serial.println("EVENT TYPE: " + data.eventType());
        // Serial.print("VALUE: ");
        // printResult(data);       // NOTE: Need to copy over printResult() function
        // Serial.println();

        FirebaseJson* json = data.jsonObjectPtr();
        handleNewOptions(json);
    }
}

void handleNewOptions(FirebaseJson* json)
{
	size_t len = json->iteratorBegin();
    String key, value = "";
    int type = 0;
    int temp = -1;

    for (size_t i = 0; i < len; i++)
    {
        json->iteratorGet(i, type, key, value);
        if (key == "closeLightLevel")
        {
            temp = value.toInt();
            if (temp < MIN_LIGHT_LEVEL)
                temp = MIN_LIGHT_LEVEL;
            else if (temp > MAX_LIGHT_LEVEL)
                temp = MAX_LIGHT_LEVEL;
            options.closeLightLevel = temp;
        }
        else if (key == "openLightLevel")
        {
            temp = value.toInt();
            if (temp < MIN_LIGHT_LEVEL)
                temp = MIN_LIGHT_LEVEL;
            else if (temp > MAX_LIGHT_LEVEL)
                temp = MAX_LIGHT_LEVEL;
            options.openLightLevel = temp;
        }
        else if (key == "delayClosingVal")
        {
            temp = value.toInt();
            if (temp >= 0)
            {
                options.delayClosingVal = temp;
                values.delayClosing = -1;
            }
        }
        else if (key == "delayOpeningVal")
        {
            temp = value.toInt();
            if (temp >= 0)
            {
                options.delayOpeningVal = temp;
                values.delayOpening = -1;
            }
        }
        else if (key == "delayOpening")
        {
            options.delayOpening = value == "true";
            if (!options.delayOpening)
            {
                values.delayOpening = -1;
            }
        }
        else if (key == "delayClosing")
        {
            options.delayClosing = value == "true";
            if (!options.delayClosing)
            {
                values.delayClosing = -1;
            }
        }
        else if (key == "overrideAuto")
        {
            options.overrideAuto = value == "true";
            if (!options.overrideAuto)
            {
                Serial.println("Remote override DISABLED!");
                status.desired = NONE;
            }
        }
        else if (key == "command")
        {
            if (value == COMMAND_CLOSE)
            {
                command = COMMAND_CLOSE;
            }
            else if (value == COMMAND_OPEN)
            {
                command = COMMAND_OPEN;
            }
            else if (value == COMMAND_READ_LIGHT_LEVEL)
            {
                command = COMMAND_READ_LIGHT_LEVEL;
            }
            else
                command = NONE;
        }
    }
}

#pragma endregion

#pragma region Firebase Send Functions
void send_debugging_message(String message)
{
    FirebaseJson json;
    json.add("count", String(++debugValues.logCounter));
    json.add("device_time", String(millis() + " milliseconds"));
    json.add("message", message);
    json.add("Ts/.sv", "timestamp");

    String debugPath = String(PATH_DEBUG_MESSAGE + debugValues.logCounter);
    Firebase.RTDB.setJSON(&fbSendData, debugPath, &json);
}

void send_light_level()
{
    FirebaseJson json;
    json.add("ll_timestamp", String(millis()));
    json.add("level", values.lightLevel);
    Firebase.RTDB.setJSON(&fbSendData, PATH_STATUS_LIGHT_LEVEL, &json);
}

void send_door_status()
{
    FirebaseJson json;
    json.add("l_timestamp", String(millis()));
    json.add("type", status.current);

    Firebase.RTDB.setJSON(&fbSendData, PATH_STATUS_DOOR, &json);

#ifdef REMOTE_DEBUGGING
    delay(500);
    send_debugging_message("Door Status: " + status.current + ", Light Level: " + values.lightLevel);
#endif
}

void send_options()
{
    FirebaseJson json;
    json.add("closeLightLevel", options.closeLightLevel);
    json.add("openLightLevel", options.openLightLevel);
    json.add("delayOpening", options.delayOpening);
    json.add("delayClosing", options.delayClosing);
    json.add("delayOpeningVal", options.delayOpeningVal);
    json.add("delayClosingVal", options.delayClosingVal);
    json.add("o_timestamp", String(millis()));
    json.add("autoOverride", options.overrideAuto);
    json.add("command", NONE);

    Firebase.RTDB.setJSON(&fbSendData, PATH_OPTIONS, &json);
}

void send_hardware_override_status()
{
    FirebaseJson json;
    json.add("hw_timestamp", String(millis()));
    json.add("type", values.forceOpen == HIGH && values.forceClose == HIGH 
        ? 0 
        : values.forceOpen == HIGH 
            ? 1
            : 2
    );

    Firebase.RTDB.setJSON(&fbSendData, PATH_STATUS_HW_OVERRIDE, &json);
}
#pragma endregion

#pragma region Sensor Readers
void read_hardware_override()
{
    int newForceClose = digitalRead(PIN_FORCE_CLOSE);
    int newForceOpen = digitalRead(PIN_FORCE_OPEN);

    if (newForceOpen != values.forceOpen || newForceClose != values.forceClose)
    {
        values.forceOpen = newForceOpen;
        values.forceClose = newForceClose;
        status.desired = NONE;
        options.overrideAuto = false;

        send_hardware_override_status();
    }
}

void read_door_values()
{
    values.lightLevel = analogRead(PIN_LIGHT_SENSOR);
    values.upSense = digitalRead(PIN_UP_SENSE);
    values.downSense = digitalRead(PIN_DOWN_SENSE);
}
#pragma endregion

#pragma region Door Operators
void stop_door_motors(bool isOpen)
{
    digitalWrite(PIN_OPEN_MOTOR, LOW);
    digitalWrite(PIN_CLOSE_MOTOR, LOW);
    status.current = isOpen ? STATUS_OPEN : STATUS_CLOSED;
}

void open_door()
{
    digitalWrite(PIN_OPEN_MOTOR, HIGH);
    digitalWrite(PIN_CLOSE_MOTOR, LOW);
    status.current = STATUS_OPENING;
    values.delayOpening = -1;
}

void close_door()
{
    digitalWrite(PIN_CLOSE_MOTOR, HIGH);
    digitalWrite(PIN_OPEN_MOTOR, LOW);
    status.current = STATUS_CLOSING;
    values.delayClosing = -1;
}

void run_new_command()
{
    String newCommand = command;
    command = NONE;
    Serial.print("Handling new command: ");Serial.println(newCommand);

    if (newCommand == COMMAND_OPEN)
    {
        if (values.upSense == HIGH)
        {
            if (values.forceOpen == HIGH && values.forceClose == HIGH)
            {
                Serial.println("   opening door!");
                status.desired = STATUS_OPEN;
                open_door();
            }
            else
                Serial.println("   attempted to open door, but hardware override is enabled!");
        }
        else
            Serial.println("   door is already open!");
    }
    else if (newCommand == COMMAND_CLOSE)
    {
        if (values.downSense == HIGH)
        {
            if (values.forceClose == HIGH && values.forceOpen == HIGH)
            {
                Serial.println("   closing door!");
                status.desired = STATUS_CLOSED;
                close_door();
            }
            else
                Serial.println("   attempted to close door, but hardware override is enabled!");
        }
        else
            Serial.println("   door is already closed!");
    }
    else if (newCommand == COMMAND_READ_LIGHT_LEVEL)
    {
        Serial.print("   writing light level of "); Serial.print(values.lightLevel); Serial.println(" to firebase!");
        send_light_level();
    }

    check_status_update();

    send_options();
}

void operate_on_door()
{
    // Check if hardware force open is enabled
    // If enabled and door is not up, open door
    if (values.forceOpen == LOW)
    {
        Serial.println("Force Open Enabled:");
        if (values.upSense == HIGH)
        {
            Serial.println("   Opening door...");
            open_door();
        }
        else
        { // Door is already open
            Serial.println("   Door is already opened!");
            stop_door_motors(true);
        }
    }
    // Else, check if hardware force close is enabled
    // If enabled and door is not down, close door
    else if (values.forceClose == LOW)
    {
        Serial.println("Force Close Enabled:");
        if (values.downSense == HIGH)
        {
            Serial.println("   Closing door...");
            close_door();
        }
        else
        { // Door is already closed
            Serial.println("   Door is already closed!");
            stop_door_motors(false);
        }
    }
    // Else, check if we are in automatic mode or remote override mode
    else if (options.overrideAuto)
    {
        Serial.println("   Remote Override Mode Enabled!");
        if (status.desired == STATUS_CLOSED)
        { // Remote command to CLOSE door
            if (values.downSense == HIGH)
            { // Door is not closed yet
                Serial.println("   Closing Door...");
                close_door();
            }
            else
            { // Door is already closed
                Serial.print("   Door is closed!");
                stop_door_motors(false);
            }
        }
        else if (status.desired == STATUS_OPEN)
        { // Remote command to OPEN door
            if (values.upSense == HIGH)
            { // Door is not open yet
                Serial.println("   Opening Door...");
                open_door();
            }
            else
            { // Door is already open
                Serial.println("   Door is open!");
                stop_door_motors(true);
            }
        }
        else
        { // Bad state, should never reach this
            Serial.print("===== ERROR: remote override enabled, but desired status was in an unexpected state (");
            Serial.print(status.desired);
            Serial.println(") =====");
            digitalWrite(PIN_CLOSE_MOTOR, LOW);
            digitalWrite(PIN_OPEN_MOTOR, LOW);
        }
    }
    // Else, check if a delay to open is in effect
    // If in effect, check to see if it is time to open the door
    else if (values.delayOpening > 0)
    {
        Serial.println("   Automatic Mode Enabled!");
        Serial.println("      Delay (Open) In Effect:");
        if (millis() > values.delayOpening)
        {
            Serial.println("      Delay time expired!");
            if (values.lightLevel >= options.openLightLevel)
            {
                Serial.println("      Light level premits...");
                if (values.upSense == HIGH)
                {
                    Serial.println("      Opening Door...");
                    open_door();
                }
                else
                {
                    Serial.println("      Door already opened...");
                    stop_door_motors(true);
                }
            }
            else
            { // It got dark before delay time was over!
                Serial.println("      It got too dark!");
                values.delayOpening = -1;
            }
        }
        else
        {
            Serial.print("      Still waiting ");
            Serial.print(values.delayOpening - millis());
            Serial.println(" milliseconds...");
        }
    }
    // Else, check if a delay to close is in effect
    // If in effect, check to see if it is time to close the door
    else if (values.delayClosing > 0)
    {
        Serial.println("   Automatic Mode Enabled!");
        Serial.println("      Delay (Close) In Effect:");
        if (millis() > values.delayClosing)
        {
            Serial.println("      Delay time expired!");
            if (values.lightLevel <= options.closeLightLevel)
            {
                Serial.println("      Light level premits...");
                if (values.downSense == HIGH)
                {
                    Serial.println("      Closing door...");
                    close_door();
                }
                else
                {
                    Serial.println("      Door already closed...");
                    stop_door_motors(false);
                }
            }
            else
            { // It got light before delay time was over!
                Serial.println("      It got too bright!");
                values.delayClosing = -1;
            }
        }
        else
        {
            Serial.print("      Still waiting ");
            Serial.print(values.delayClosing - millis());
            Serial.println(" milliseconds...");
        }
    }
    // Else, check if light level is high enough to open
    else if (values.lightLevel >= options.openLightLevel)
    {
        Serial.println("   Automatic Mode Enabled!");
        Serial.println("      It's bright enough to open up!");
        if (values.upSense == HIGH)
        {
            if (options.delayOpening)
            {
                Serial.print("      Beginning DELAY of ");
                Serial.print(options.delayOpeningVal);
                Serial.println(" milliseconds");
                values.delayOpening = millis() + options.delayOpeningVal;
                digitalWrite(PIN_CLOSE_MOTOR, LOW);
                digitalWrite(PIN_OPEN_MOTOR, LOW);
            }
            else
            {
                Serial.println("      Opening door...");
                open_door();
            }
        }
        else
        {
            Serial.println("      Door already open...");
            stop_door_motors(true);
        }
    }
    // Else, check if light level is low enough to close
    else if (values.lightLevel <= options.closeLightLevel)
    {
        Serial.println("   Automatic Mode Enabled!");
        Serial.println("      It's dark enough to close!");
        if (values.downSense == HIGH)
        {
            if (options.delayClosing)
            {
                Serial.print("      Beginning DELAY of ");
                Serial.print(options.delayClosingVal);
                Serial.println(" milliseconds");
                values.delayClosing = millis() + options.delayClosingVal;
                digitalWrite(PIN_CLOSE_MOTOR, LOW);
                digitalWrite(PIN_OPEN_MOTOR, LOW);
            }
            else
            {
                Serial.println("      Closing door...");
                close_door();
            }
        }
        else
        {
            Serial.println("      Door already closed...");
            stop_door_motors(false);
        }
    }
    // Else, make sure the door is doing nothing
    else
    {
        Serial.print("   Door resting at: ");
        Serial.println(status.current);
        digitalWrite(PIN_CLOSE_MOTOR, LOW);
        digitalWrite(PIN_OPEN_MOTOR, LOW);
    }
}
#pragma endregion

#pragma region Status Check
void check_status_update()
{
    if (status.old != status.current)
    {
        Serial.print("   status changed from "); Serial.print(status.old); Serial.print(" to "); Serial.println(status.current);
        send_door_status();
    }
}
#pragma endregion



