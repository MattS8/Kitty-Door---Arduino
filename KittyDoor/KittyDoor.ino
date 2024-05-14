#include <Arduino.h>
#if defined(ESP32) || defined(ARDUINO_RASPBERRY_PI_PICO_W) || defined(ARDUINO_GIGA)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#elif __has_include(<WiFiNINA.h>) || defined(ARDUINO_NANO_RP2040_CONNECT)
#include <WiFiNINA.h>
#elif __has_include(<WiFi101.h>)
#include <WiFi101.h>
#elif __has_include(<WiFiS3.h>) || defined(ARDUINO_UNOWIFIR4)
#include <WiFiS3.h>
#elif __has_include(<WiFiC3.h>) || defined(ARDUINO_PORTENTA_C33)
#include <WiFiC3.h>
#elif __has_include(<WiFi.h>)
#include <WiFi.h>
#endif

#include <ArduinoJson.h>

#include <FirebaseClient.h>

#include "KittyDoor.h"
#include "Credentials.h"

#define DEBUG_FIREBASE

///////////////////////////////////////////////////////////
// Firebase Globals
///////////////////////////////////////////////////////////

DefaultNetwork network; // initilize with boolean parameter to enable/disable network reconnection

UserAuth user_auth(FIREBASE_API_KEY, USER_EMAIL, USER_PASSWORD);

FirebaseApp app;

#if defined(ESP32) || defined(ESP8266) || defined(ARDUINO_RASPBERRY_PI_PICO_W)
#include <WiFiClientSecure.h>
WiFiClientSecure ssl_client1, ssl_client2;
#elif defined(ARDUINO_ARCH_SAMD) || defined(ARDUINO_UNOWIFIR4) || defined(ARDUINO_GIGA) || defined(ARDUINO_PORTENTA_C33) || defined(ARDUINO_NANO_RP2040_CONNECT)
#include <WiFiSSLClient.h>
WiFiSSLClient ssl_client1, ssl_client2;
#endif

using AsyncClient = AsyncClientClass;

AsyncClient aClient(ssl_client1, getNetwork(network)), aClient2(ssl_client2, getNetwork(network));

RealtimeDatabase Database;

unsigned long ms = 0;

#ifdef DEBUG_FIREBASE
void print_firebase_result(AsyncResult &aResult);
#endif // DEBUG FIREBASE

///////////////////////////////////////////////////////////
// Kitty Door Globals
///////////////////////////////////////////////////////////
String receivedResult = ""; // JSON string populated from stream callback function
String command = NONE;      // The next command the door should perform

KittyDoorOptions options; // All values pertaining to door options (i.e. triggering light levels, etc)
KittyDoorValues values;   // All values pertaining to current door status (i.e. current light level, state, etc)

DoorStatus status = {NONE, NONE};

#ifdef DEBUG_PING
unsigned long dbg_timeSinceLastPing = 0;
#endif

// Firebase Callback
void firebase_callback(AsyncResult &aResult);
#ifdef DEBUG_FIREBASE
// Firebase Debug Print
void print_firebase_result(AsyncResult &aResult);
#endif

void setup()
{
    Serial.begin(115200);

///////////////////////////////////////////////////////////
// Setup Pin Modes
///////////////////////////////////////////////////////////
#pragma region Setup Pin Modes
    pinMode(PIN_LIGHT_SENSOR, INPUT);       // Ambient light from photo sensor
    pinMode(PIN_UP_SENSE, INPUT_PULLUP);    // Low when door is fully open
    pinMode(PIN_DOWN_SENSE, INPUT_PULLUP);  // Low when door is fully closed
    pinMode(PIN_FORCE_OPEN, INPUT_PULLUP);  // If low, force door to open
    pinMode(PIN_FORCE_CLOSE, INPUT_PULLUP); // If low, force door to close

    pinMode(PIN_OPEN_MOTOR, OUTPUT);  // If HIGH, door will open
    pinMode(PIN_CLOSE_MOTOR, OUTPUT); // If HIGH, door will close

    digitalWrite(PIN_OPEN_MOTOR, LOW);  // Don't close door
    digitalWrite(PIN_CLOSE_MOTOR, LOW); // Don't open door
#pragma endregion                       // Setup Pin Modes

///////////////////////////////////////////////////////////
// Set Default Door Options
///////////////////////////////////////////////////////////
#pragma region Setup Default Door Options
    options.openLightLevel = 190;
    options.closeLightLevel = 40;
    options.delayOpening = false;
    options.delayOpeningVal = 0;
    options.delayClosing = false;
    options.delayClosingVal = 0;

    values.delayOpening = -1;
    values.delayClosing = -1;
#pragma endregion

///////////////////////////////////////////////////////////
// Set Default Door Options
///////////////////////////////////////////////////////////
#pragma region Set Defaul Door Options
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
#pragma endregion

///////////////////////////////////////////////////////////
// Setup WiFi & Firebase
///////////////////////////////////////////////////////////
#pragma region Setup Firebase
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to Wi-Fi");
    unsigned long ms = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print(".");
        delay(300);
    }
    Serial.println();
    Serial.print("Connected with IP: ");
    Serial.println(WiFi.localIP());
    Serial.println();

    Firebase.printf("Firebase Client v%s\n", FIREBASE_CLIENT_VERSION);

    Serial.println("Initializing app...");
    ssl_client1.setInsecure();
    ssl_client2.setInsecure();
    ssl_client1.setBufferSizes(4096, 1024);
    ssl_client2.setBufferSizes(4096, 1024);

    // In case using ESP8266 without PSRAM and you want to reduce the memory usage, you can use WiFiClientSecure instead of ESP_SSLClient (see examples/RealtimeDatabase/StreamConcurentcy/StreamConcurentcy.ino)
    // with minimum receive and transmit buffer size setting as following.
    // ssl_client1.setBufferSizes(1024, 512);
    // ssl_client2.setBufferSizes(1024, 512);
    // Note that, because the receive buffer size was set to minimum safe value, 1024, the large server response may not be able to handle.
    // The WiFiClientSecure uses 1k less memory than ESP_SSLClient.

    initializeApp(aClient2, app, getAuth(user_auth), firebase_callback, "authTask");

    app.getApp<RealtimeDatabase>(Database);

    Database.url(DATABASE_URL);

    // The "unauthenticate" error can be occurred in this case because we don't wait
    // the app to be authenticated before connecting the stream.
    // This is ok as stream task will be reconnected automatically when the app is authenticated.
    Database.get(aClient, PATH_BASE + PATH_DEBUG_PING, firebase_callback, true /* SSE mode (HTTP Streaming) */, "streamTask");
#pragma endregion // Setup Firebase
}

void loop()
{
    app.loop();
    Database.loop();

    if (receivedResult != "")
    {
        handleStreamResult();
    }
}

///////////////////////////////////////////////////////////
///// FIREBASE HANDLERS
///////////////////////////////////////////////////////////
#pragma region FIREBASE HANDLERS
void firebase_callback(AsyncResult &aResult)
{
    debug_print("Callback received stream data!");

#ifdef DEBUG_FIREBASE
    print_firebase_result(aResult);
#endif

    if (aResult.available())
    {
        RealtimeDatabaseResult &RTDB = aResult.to<RealtimeDatabaseResult>();
        if (RTDB.isStream())
        {
            receivedResult = RTDB.to<String>();
        }
    }
}

void handleStreamResult()
{
    JsonDocument doc;
    deserializeJson(doc, receivedResult);
}
#pragma endregion // FIREBASE HANDLERS

///////////////////////////////////////////////////////////
///// VALUE READERS
///////////////////////////////////////////////////////////
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

///////////////////////////////////////////////////////////
///// DOOR OPERATORS
///////////////////////////////////////////////////////////
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

///////////////////////////////////////////////////////////
///// FIREBASE COMMANDS
///////////////////////////////////////////////////////////
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

///////////////////////////////////////////////////////////
///// DEBUG
///////////////////////////////////////////////////////////
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

#ifdef DEBUG_FIREBASE
void print_firebase_result(AsyncResult &aResult)
{
    if (aResult.isEvent())
    {
        Firebase.printf("Event task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.appEvent().message().c_str(), aResult.appEvent().code());
    }

    if (aResult.isDebug())
    {
        Firebase.printf("Debug task: %s, msg: %s\n", aResult.uid().c_str(), aResult.debug().c_str());
    }

    if (aResult.isError())
    {
        Firebase.printf("Error task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.error().message().c_str(), aResult.error().code());
    }

    if (aResult.available())
    {
        RealtimeDatabaseResult &RTDB = aResult.to<RealtimeDatabaseResult>();
        if (RTDB.isStream())
        {
            Serial.println("----------------------------");
            Firebase.printf("task: %s\n", aResult.uid().c_str());
            Firebase.printf("event: %s\n", RTDB.event().c_str());
            Firebase.printf("path: %s\n", RTDB.dataPath().c_str());
            Firebase.printf("data: %s\n", RTDB.to<const char *>());
            Firebase.printf("type: %d\n", RTDB.type());
        }
        else
        {
            Serial.println("----------------------------");
            Firebase.printf("task: %s, payload: %s\n", aResult.uid().c_str(), aResult.c_str());
        }

#if defined(ESP32) || defined(ESP8266)
        Firebase.printf("Free Heap: %d\n", ESP.getFreeHeap());
#elif defined(ARDUINO_RASPBERRY_PI_PICO_W)
        Firebase.printf("Free Heap: %d\n", rp2040.getFreeHeap());
#endif
    }
}
#endif
#pragma endregion // DEBUG