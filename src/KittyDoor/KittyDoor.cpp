#include <ESP8266WiFi.h>
#include <Firebase_ESP_Client.h>
#include "KittyDoor.h"

////////////////////////////////
// DEBUG #DEFINES
////////////////////////////////
#define DEBUG_PRINTS
#define DEBUG_PING

#define MAX_OPERATION_TIME 5000 // 5 second timeout for door operations

// Firebase Globals
FirebaseConfig fbConfig; // Configuration for connecting to firebase
FirebaseAuth fbAuth;     // Utilizes the email and pass
FirebaseData fbRecvData; // Data object used to recieve commands from firebase
FirebaseData fbSendData; // Data object used to send status/debug messages to firebase

// Door Globals
String command = NONE; // The next command the door should perform

KittyDoorOptions options; // All values pertaining to door options (i.e. triggering light levels, etc)
KittyDoorValues values;   // All values pertaining to current door status (i.e. current light level, state, etc)

DoorStatus status = {NONE, NONE};

#ifdef DEBUG_PING
unsigned long dbg_timeSinceLastPing = 0;
#endif

void setup()
{
    Serial.begin(115200);

    set_pin_modes();

    set_default_door_options();

    initialize_door();

    connect_to_wifi();

    connect_to_firebase();

    fetch_initial_options();

    delay(500);

    begin_streaming();

    debug_ping();
}

void loop()
{
    /*
     * Reads the hardware override pins and acts on any changes. This includes opening/closing
     * the door and sending the proper statuses to firebase.
     */
    read_hardware_override();

    /*
     * This simply reads in the light level now. No reporting is done in this function.
     */
    read_door_values();

    /*
     * 'command' will be set whenever the firebase callback received a new command value.
     * After process_new_command runs, command will return to NONE, effectively creating
     * a one-shot system to handle requests from the phone.
     */
    if (command != NONE)
        process_new_command();

    /*
     * If the door is not in an override mode from hardware switch or firebase, the door should be in
     * auto mode. This means it will determine whether to open/close the door based on the light
     * level and the options provided from firebase.
     */
    if (!is_force_close_enabled() && !is_force_open_enabled() && !options.overrideAuto)
    {
        // Note: currently, delayed opening/closing is NYI

        // Bright enough to open the door
        if (values.lightLevel >= options.openLightLevel)
        {
            // Auto mode has not tried opening the door yet
            if (status.lastAutoMode != STATUS_OPEN)
            {
                status.lastAutoMode = STATUS_OPEN;
                open_door();
            }
            else
            {
                debug_print("Auto Mode has already opened the door.");
            }
        }
        else if (values.lightLevel <= options.closeLightLevel)
        {
            // Auto mode has not tried closing the door yet
            if (status.lastAutoMode != STATUS_CLOSED)
            {
                status.lastAutoMode = STATUS_CLOSED;
                close_door();
            }
            else
            {
                debug_print("Auto Mode has already closed the door.");
            }
        }
    }

#ifdef DEBUG_PING
    if (millis() - dbg_timeSinceLastPing > DEBUG_PING_INTERVAL)
        debug_ping();
#endif

    delay(25);
}

///////////////////////////////////
///// VALUE READERS
//////////////////////////////////
#pragma region VALUE READERS
void read_hardware_override()
{
    int newForceClose = digitalRead(PIN_FORCE_CLOSE);
    int newForceOpen = digitalRead(PIN_FORCE_OPEN);

    // New mode found -> report to firebase
    if (newForceClose != values.forceClose || newForceOpen != values.forceOpen)
    {
        // Set new values
        values.forceClose = newForceClose;
        values.forceOpen = newForceOpen;
        options.overrideAuto = false;

        if (is_force_close_enabled())
        {
            debug_print("Force close (Hardware) enabled!");
            close_door();
        }

        if (is_force_open_enabled())
        {
            debug_print("Force open (Hardware) enabled!");
            open_door();
        }

        // A change in override state invalidates last auto mode status.
        //  Clear and let auto mode logic take over
        status.lastAutoMode = NONE;

        // Send new state to firebase
        send_hardware_override_status();
    }
}

void read_door_values() { values.lightLevel = analogRead(PIN_LIGHT_SENSOR); }

bool is_force_close_enabled() { return values.forceClose == LOW; }

bool is_force_open_enabled() { return values.forceOpen == LOW; }

bool is_door_open() { return values.upSense == LOW; }

bool is_door_closed() { return values.downSense == LOW; }
#pragma endregion // VALUE READERS

///////////////////////////////////
///// DOOR OPERATORS
//////////////////////////////////
#pragma region DOOR OPERATORS
void stop_door_motors()
{
    digitalWrite(PIN_OPEN_MOTOR, LOW);
    digitalWrite(PIN_CLOSE_MOTOR, LOW);
}

void open_door(bool reportToFirebase)
{
    if (is_door_open())
    {
        debug_print("Tried to open door, but it's already opened!");
        return;
    }
    operate_on_door(LOW, HIGH, STATUS_OPENING, STATUS_OPEN, is_door_open, reportToFirebase);
}

void close_door(bool reportToFirebase)
{
    if (is_door_closed())
    {
        debug_print("Tried to close door, but it's already closed!");
        return;
    }
    operate_on_door(HIGH, LOW, STATUS_CLOSING, STATUS_CLOSED, is_door_closed, reportToFirebase);
}

void operate_on_door(int closeMotor, int openMotor, String transStatus, String finalStatus, bool (*isDoorResting)(), bool reportToFirebase)
{
    status.current = transStatus;
    long timeOperatedOnDoor = 0;

    if (reportToFirebase)
        send_door_state();

    digitalWrite(PIN_CLOSE_MOTOR, closeMotor);
    digitalWrite(PIN_OPEN_MOTOR, openMotor);
    values.delayClosing = -1;

    // Wait until door is closed/opened
    long operationStart = millis();
    while (timeOperatedOnDoor < MAX_OPERATION_TIME && !isDoorResting())
    {
        values.downSense = digitalRead(PIN_DOWN_SENSE);
        values.upSense = digitalRead(PIN_UP_SENSE);
        delay(25); // A small delay stops random crashing (probably due to reading the pin too quickly)
        timeOperatedOnDoor = millis() - operationStart;
    }

    // Door has reached closed state
    stop_door_motors();
    status.current = finalStatus;
    if (reportToFirebase)
        send_door_state();
}
#pragma endregion // DOOR OPERATORS

///////////////////////////////////
///// SETUP FUNCTIONS
//////////////////////////////////
#pragma region SETUP FUNCTIONS
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

void initialize_door()
{
    values.upSense = digitalRead(PIN_UP_SENSE);
    values.downSense = digitalRead(PIN_DOWN_SENSE);
    values.forceClose = digitalRead(PIN_FORCE_CLOSE);
    values.forceOpen = digitalRead(PIN_FORCE_OPEN);

    values.lightLevel = analogRead(PIN_LIGHT_SENSOR);

    /*
     * Force the open motor to run. If the door is in a mismatched state, it will begin to close,
     * then loop back around until it is in the proper open state.
     */
    digitalWrite(PIN_CLOSE_MOTOR, LOW);
    digitalWrite(PIN_OPEN_MOTOR, HIGH);

    // A slight delay allows the door to get off the sensor before checking!
    delay(250);

    // This runs the proper checking logic to stop the door once it is open.
    open_door(false);

    // This ensures the motors are stopped (open_door() may immediately return without stopping the motors)
    stop_door_motors();
}

void connect_to_wifi()
{
    WiFi.begin(WIFI_AP_NAME, WIFI_AP_PASS);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.print("\nConnected with IP: ");
    Serial.println(WiFi.localIP());
    Serial.println("");
}

void connect_to_firebase()
{
    fbConfig.host = FIREBASE_HOST;
    fbConfig.database_url = FIREBASE_HOST;
    fbConfig.api_key = FIREBASE_API;
    // fbConfig.token_status_callback = fbTokenStatusCallback;

    fbAuth.user.email = FIREBASE_USERNAME;
    fbAuth.user.password = FIREBASE_PASS;

    Firebase.reconnectWiFi(true);
    Firebase.RTDB.setMaxRetry(&fbRecvData, 4);
    Firebase.setDoubleDigits(5);
    Firebase.RTDB.setMaxErrorQueue(&fbRecvData, 30);

    fbRecvData.setResponseSize(1024);

    Firebase.begin(&fbConfig, &fbAuth);

// Recommend for ESP8266 stream, adjust the buffer size to match stream data size
#if defined(ESP8266)
    fbRecvData.setBSSLBufferSize(2048 /* Rx in bytes, 512 - 16384 */, 512 /* Tx in bytes, 512 - 16384 */);
#endif
}

void fetch_initial_options()
{
    debug_print("Fetching initial options...");
    if (Firebase.RTDB.get(&fbRecvData, PATH_STREAM))
    {
        handle_callback_data(fbRecvData.to<FirebaseJson *>());
    }
    else
    {
        debug_print("ERROR: Failed to fetch initial options\n\tReason: " + fbRecvData.errorReason());
    }
}

void begin_streaming()
{
    debug_print("Streaming to: " + PATH_STREAM);

    if (!Firebase.RTDB.beginStream(&fbRecvData, PATH_STREAM))
    {
        debug_print("ERROR: Couldn't begin stream connection...\n\tReason: " + fbRecvData.errorReason());
    }

    Firebase.RTDB.setStreamCallback(&fbRecvData, firebase_callback, timeout_callback);

    debug_ping();
}
#pragma endregion // SETUP FUNCTIONS

///////////////////////////////////
///// FIREBASE SEND FUNCTIONS
//////////////////////////////////
#pragma region FIREBASE SEND FUNCTIONS
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

    if (!Firebase.RTDB.setJSON(&fbSendData, PATH_STREAM, &json))
    {
        debug_print("ERROR (Send Options):\n\t" + fbSendData.errorReason());
        handle_firebase_stream_failed();
    }
}

void send_light_level()
{
    FirebaseJson json;
    json.add("ll_timestamp", String(millis()));
    json.add("level", values.lightLevel);

    if (!Firebase.RTDB.setJSON(&fbSendData, PATH_STATUS_LIGHT_LEVEL, &json))
    {
        debug_print("ERROR (Send Light Level):\n\t" + fbSendData.errorReason());
        handle_firebase_stream_failed();
    }
}

void send_hardware_override_status()
{
    FirebaseJson json;
    json.add("hw_timestamp", String(millis()));
    json.add("type", !is_force_close_enabled() && !is_force_open_enabled()
                         ? 0
                     : is_force_open_enabled()
                         ? 1
                         : 2);

    if (!Firebase.RTDB.setJSON(&fbSendData, PATH_STATUS_HW_OVERRIDE, &json))
    {
        debug_print("ERROR (Send Hardware Override Status):\n\t" + fbSendData.errorReason());
        handle_firebase_stream_failed();
    }
}

void send_door_state()
{
    FirebaseJson json;
    json.add("l_timestamp", String(millis()));
    json.add("type", status.current);

    if (!Firebase.RTDB.setJSON(&fbSendData, PATH_STATUS_DOOR, &json))
    {
        debug_print("ERROR (Send Door State):\n\t" + fbSendData.errorReason());
        handle_firebase_stream_failed();
    }
}
#pragma endregion // FIREBASE SEND FUNCTIONS

///////////////////////////////////
///// FIREBASE COMMANDS
//////////////////////////////////
#pragma region FIREBASE COMMANDS
void handle_open_command()
{
    debug_print("Open command received!");
    if (is_force_close_enabled() || is_force_open_enabled())
    {
        debug_print("Handle Open Command: Command received, however currently in hardware override mode!");
        return;
    }

    // Clear auto mode as it is now invalidated
    status.lastAutoMode = NONE;
    open_door();
}

void handle_close_command()
{
    debug_print("Close command received!");

    if (is_force_close_enabled() || is_force_open_enabled())
    {
        debug_print("Handle Close Command: Command received, however currently in hardware override mode!");
        return;
    }

    // Clear auto mode as it is now invalidated
    status.lastAutoMode = NONE;
    close_door();
}

void process_new_command()
{
    // Change global immediately to consume the command
    String newCommand = command;
    command = NONE;

    // Handle Open Door Command
    if (newCommand == "\"" + COMMAND_OPEN + "\"")
    {
        handle_open_command();
    }
    // Handle Close Door Command
    else if (newCommand == "\"" + COMMAND_CLOSE + "\"")
    {
        handle_close_command();
    }
    // Handle Read Light Sensor
    else if (newCommand == "\"" + COMMAND_READ_LIGHT_LEVEL + "\"")
    {
        debug_print("Process New Command: Sending ligth level (" + String(values.lightLevel) + ") to firebse!");
        send_light_level();
    }
    // None Command is usually from a clear -> do nothing
    else if (newCommand == "\"" + NONE + "\"")
    {
        debug_print("Process New Command: NONE command -> Doing nothing.");
        return;
    }
    // Unknown Command
    else
    {
        debug_print("Process New Command: Unknown command received -> " + newCommand);
    }

    // Clears the command on firebase's end, signifying that the command has been received
    //  and consumed on the door's end
    send_options();
}
#pragma endregion // FIREBASE COMMANDS

///////////////////////////////////
///// FIREBASE CALLBACKS
//////////////////////////////////
#pragma region Firebase Callbacks

void firebase_callback(FirebaseStream data)
{
    debug_print("Callback received stream data!");
    if (data.dataType() == "json")
    {
        debug_print("\tJson Data:");
        debug_print("\tSTREAM PATH: " + data.streamPath());
        debug_print("\tEVENT PATH: " + data.dataPath());
        debug_print("\tDATA TYPE: " + data.dataType());
        debug_print("\tEVENT TYPE: " + data.eventType());

        FirebaseJson *jsonData = data.jsonObjectPtr();
        handle_callback_data(jsonData);
    }
    else
    {
        debug_print("Callback received data of type: " + data.dataType());
    }
}

void timeout_callback(bool connectionCut)
{
    debug_print("fbTimeout occured!");
    if (connectionCut)
    {
        debug_print("\tStream Tiemout");
        begin_streaming();
    }
}

void handle_callback_data(FirebaseJson *json)
{
    size_t len = json->iteratorBegin();
    String key, value = "";
    int type = 0;
    int temp = -1;

    for (size_t i = 0; i < len; i++)
    {
        debug_print("Json Iterator[" + String(i) + "]: " + key + " -> " + value);
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
        }
        else if (key == "command")
        {
            command = value;
        }
    }
}

void handle_firebase_stream_failed()
{
    debug_print("Attempting to reconnect to firebase...");
    Firebase.reset(&fbConfig);
    debug_print("\tReset!");
    delay(3000);
    debug_print("\tRunning connection routine again!");
    connect_to_firebase();
    debug_print("\tBeginning stream...");
    begin_streaming();
    debug_print("Done!");
}
#pragma endregion // FIREBASE CALLBACKS

///////////////////////////////////
///// DEBUG
//////////////////////////////////
#pragma region debug
// Ensure that debug messages aren't spammed to the console
#ifdef DEBUG_PRINTS
String dbg_string = "";
#endif
void debug_print(String message)
{
#ifdef DEBUG_PRINTS
    if (dbg_string == message)
        return;

    dbg_string = message;
    Serial.println(dbg_string);
#endif
}

#ifdef DEBUG_PING

#endif

void debug_ping()
{
#ifdef DEBUG_PING
    static unsigned long dbg_alive_count = 0;
    dbg_timeSinceLastPing = millis();
    FirebaseJson pingJson;
    pingJson.add("time_alive", String(millis()));
    pingJson.add("count", ++dbg_alive_count);
    if (!Firebase.RTDB.setJSON(&fbSendData, PATH_DEBUG_PING, &pingJson))
        handle_firebase_stream_failed();

#endif
}
#pragma endregion // DEBUG