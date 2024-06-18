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

#include <Arduino_JSON.h>
#include <FirebaseClient.h>
#include "Credentials.h"
#include "KittyDoor.h"

void cbSendTask(AsyncResult &aResult);

void cbStreamTask(AsyncResult &aResult);

void printResult(AsyncResult &aResult);

DefaultNetwork network; // initilize with boolean parameter to enable/disable network reconnection

UserAuth user_auth(API_KEY, USER_EMAIL, USER_PASSWORD);

FirebaseApp app;

WiFiClient basic_client1, basic_client2, basic_client3;

// The ESP_SSLClient uses PSRAM by default (if it is available), for PSRAM usage, see https://github.com/mobizt/FirebaseClient#memory-options
// For ESP_SSLClient documentation, see https://github.com/mobizt/ESP_SSLClient
ESP_SSLClient ssl_client1, ssl_client2, ssl_client3;

using AsyncClient = AsyncClientClass;

AsyncClient fbClientStream(ssl_client1, getNetwork(network)), aClient2(ssl_client2, getNetwork(network)), fbClientSend(ssl_client3, getNetwork(network));

RealtimeDatabase Database;

RealtimeDatabaseResult dbResult;

////// ---- Kitty Door Variables ---- //////
KittyDoorValues values; // All values pertaining to current door status (i.e. current light level, state, etc)
DoorState doorstate;
AutoModeState autoMode;
HwOverrideState hwOverride;
String command = NONE;
String desiredState = STATE_OPEN;
bool newDataReceived = false;

void setup()
{
    Serial.begin(115200);
    setPinModes();
    initializeDoor();
    setupFirebase();
}

void loop()
{
    // The async task handler should run inside the main loop
    // without blocking delay or bypassing with millis code blocks.

    app.loop();

    Database.loop();

    // Read Door Values
    readDoorSensors();
    readHardwareOverride();
    readLightLevel();

    // Check for action if in "hw override mode"
    if (isHwForceCloseEnabled())
    {
        desiredState = STATE_CLOSED;
    }
    else if (isHwForceOpenEnabled())
    {
        desiredState = STATE_OPEN;
    }
    // Check for action if in "auto mode"
    else if (autoMode.current)
    {
        // Checks light levels and sets desiredState accordingly
        handleAutoMode();
    }

    // Handle callback for new data from firebase
    if (newDataReceived)
        handleNewFirebaseData();

    // Operate on door based on the current command (determined from actions)
    if (command != NONE)
        handleCommand();

    // Operate on door based on the set desired state
    if (desiredState == STATE_CLOSED)
    {
        closeDoor();
    }
    else
    {
        openDoor();
    }

    // Send any change in hw Override state to firebase
    if (hwOverride.current != hwOverride.previous)
    {
        sendHwOverrideStatus();
    }

    // Send any new door states to firebase
    if (doorstate.previous != doorstate.current)
    {
        sendDoorState();
    }

    // Send any change in autoMode state to firebase
    if (autoMode.current != autoMode.previous)
    {
        sendAutoMode();
    }

#ifdef DEBUG_PRINTS
    debugPrint(debugDoorVars
        + debugHwOverride + (isHwForceCloseEnabled() ? debugForceOpen : isHwForceCloseEnabled() ? debugForceClose : debugDisabled)
        + debugAutoMode + (autoMode.current ? debugEnabled : debugDisabled)
        // + debugAutoModeBuffer + String(values.autoModeBuffer)
        + debugOpenSensor + (isDoorOpen() ? debugTriggered : debugNotTriggered)
        + debugClosedSensor + (isDoorClosed() ? debugTriggered : debugNotTriggered)
        + debugDoorStateCur + doorstate.current
        + debugDoorStatePrev + doorstate.previous
        // + debugLightLevel + String(values.lightLevel) + debugLightLevel1 
        // + String(values.openLightLevel) + debugLightLevel2 + String(values.closeLightLevel) + debugLightLevel3
    );
#endif
}

///////////////////////////////////
///// SETUP FUNCTIONS
//////////////////////////////////
#pragma region SETUP FUNCTIONS

void initializeDoor()
{
    readDoorSensors();
    readLightLevel();
    values.hwForceClose = digitalRead(PIN_FORCE_CLOSE);
    values.hwForceOpen = digitalRead(PIN_FORCE_OPEN);

    values.openLightLevel = 190;
    values.closeLightLevel = 40;
    values.autoModeBuffer = 0;
    doorstate.current = STATE_OPEN;
    doorstate.previous = NONE;
    autoMode.current = true;
    autoMode.previous = true;
    hwOverride.current = HWO_DISABLED;
    hwOverride.previous = -1;

    /*
     * Force the open motor to run. If the door is in a mismatched state, it will begin to close,
     * then loop back around until it is in the proper open state.
     */
    digitalWrite(PIN_CLOSE_MOTOR, LOW);
    digitalWrite(PIN_OPEN_MOTOR, HIGH);

    // A slight delay allows the door to get off the sensor before checking!
    delay(250);

    while (!isDoorOpen())
    {
        // This runs the proper checking logic to stop the door once it is open.
        readDoorSensors();
        openDoor();
        delay(50);
    }

    // This ensures the motors are stopped (isDoorOpen() may immediately return without stopping the motors)
    stopDoorMotors();
}

void setPinModes()
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

void setupFirebase()
{
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    Serial.print("Connecting to Wi-Fi");
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

    ssl_client1.setClient(&basic_client1);
    ssl_client2.setClient(&basic_client2);
    ssl_client3.setClient(&basic_client3);

    ssl_client1.setInsecure();
    ssl_client2.setInsecure();
    ssl_client3.setInsecure();

    // ssl_client1.setBufferSizes(2048, 1024);
    // ssl_client2.setBufferSizes(2048, 1024);
    // ssl_client3.setBufferSizes(2048, 1024);

    // In case using ESP8266 without PSRAM and you want to reduce the memory usage,
    // you can use WiFiClientSecure instead of ESP_SSLClient with minimum receive and transmit buffer size setting as following.
    ssl_client1.setBufferSizes(1024, 512);
    ssl_client2.setBufferSizes(1024, 512);
    ssl_client3.setBufferSizes(1024, 512);
    // Note that, because the receive buffer size was set to minimum safe value, 1024, the large server response may not be able to handle.
    // The WiFiClientSecure uses 1k less memory than ESP_SSLClient.

    // ssl_client1.setDebugLevel(1);
    // ssl_client2.setDebugLevel(1);
    // ssl_client3.setDebugLevel(1);

    initializeApp(fbClientSend, app, getAuth(user_auth), cbAuthTask, "AUTH");

    // Binding the FirebaseApp for authentication handler.
    // To unbind, use Database.resetApp();
    app.getApp<RealtimeDatabase>(Database);

    Database.url(DATABASE_URL);

    // Since v1.2.1, in SSE mode (HTTP Streaming) task, you can filter the Stream events by using RealtimeDatabase::setSSEFilters(<keywords>),
    // which the <keywords> is the comma separated events.
    // The event keywords supported are:
    // get - To allow the http get response (first put event since stream connected).
    // put - To allow the put event.
    // patch - To allow the patch event.
    // keep-alive - To allow the keep-alive event.
    // cancel - To allow the cancel event.
    // auth_revoked - To allow the auth_revoked event.
    // To clear all prevousely set filter to allow all Stream events, use RealtimeDatabase::setSSEFilters().
    Database.setSSEFilters("get,put,patch,keep-alive,cancel,auth_revoked");

    // The "unauthenticate" error can be occurred in this case because we don't wait
    // the app to be authenticated before connecting the stream.
    // This is ok as stream task will be reconnected automatically when the app is authenticated.

    Database.get(fbClientStream, "/systems/kitty_door/controller/action", cbStreamTask, true /* SSE mode (HTTP Streaming) */, "ACTION");
}

#pragma endregion

///////////////////////////////////
///// VALUE READERS
//////////////////////////////////
#pragma region VALUE READERS

void readHardwareOverride()
{
    values.hwForceClose = digitalRead(PIN_FORCE_CLOSE);
    values.hwForceOpen = digitalRead(PIN_FORCE_OPEN);
    hwOverride.current = !isHwForceOpenEnabled() && !isHwForceCloseEnabled()
                             ? HWO_DISABLED
                         : isHwForceOpenEnabled()
                             ? HWO_FORCE_OPEN
                             : HWO_FORCE_CLOSE;
}

void readLightLevel() 
{ 
  values.lightLevel = analogRead(PIN_LIGHT_SENSOR); 
  delay(10);
}

void readDoorSensors()
{
    values.upSense = digitalRead(PIN_UP_SENSE);
    values.downSense = digitalRead(PIN_DOWN_SENSE);
    doorstate.current = isDoorOpen() ? STATE_OPEN
        : isDoorClosed() ? STATE_CLOSED
            : desiredState == STATE_OPEN ? STATE_OPENING
            : STATE_CLOSING;
}

bool isHwForceCloseEnabled() { return values.hwForceClose == LOW; }

bool isHwForceOpenEnabled() { return values.hwForceOpen == LOW; }

bool isDoorOpen() { return values.upSense == LOW; }

bool isDoorClosed() { return values.downSense == LOW; }

#pragma endregion // VALUE READERS

///////////////////////////////////
///// DOOR OPERATORS
//////////////////////////////////
#pragma region DOOR OPERATORS

void stopDoorMotors()
{
    digitalWrite(PIN_OPEN_MOTOR, LOW);
    digitalWrite(PIN_CLOSE_MOTOR, LOW);
}

void openDoor()
{
    desiredState = STATE_OPEN;
    if (isDoorOpen())
    {
        stopDoorMotors();
    }
    else
    {
        digitalWrite(PIN_CLOSE_MOTOR, LOW);
        digitalWrite(PIN_OPEN_MOTOR, HIGH);
    }
}

void closeDoor()
{
    desiredState = STATE_CLOSED;
    if (isDoorClosed())
    {
        stopDoorMotors();
    }
    else
    {
        digitalWrite(PIN_CLOSE_MOTOR, HIGH);
        digitalWrite(PIN_OPEN_MOTOR, LOW);
    }
}

#pragma endregion

///////////////////////////////////
///// FB SEND FUNCTIONS
//////////////////////////////////
#pragma region FB SEND FUNCTIONS

void sendAlive()
{
    if (app.ready())
    {
        JsonWriter writer;
        object_t json, obj1, obj2;
        writer.create(obj1, "status", millis());
        writer.create(obj2, "timestamp", String(millis()));
        writer.join(json, 2, obj1, obj2);

        Database.set<object_t>(fbClientSend, "/systems/kitty_door/status/online_check", json, cbSendTask, "SEND");
    }
}

void sendLightLevel()
{
    if (app.ready())
    {
        JsonWriter writer;
        object_t json, obj1, obj2;
        writer.create(obj1, "status", values.lightLevel);
        writer.create(obj2, "timestamp", String(millis()));
        writer.join(json, 2, obj1, obj2);

        Database.set<object_t>(fbClientSend, "/systems/kitty_door/status/light_level", json, cbSendTask, "SEND");
    }
}

void sendHwOverrideStatus()
{
    if (app.ready())
    {
        JsonWriter writer;
        object_t json, obj1, obj2;
        writer.create(obj1, "status", hwOverride.current);
        writer.create(obj2, "timestamp", String(millis()));
        writer.join(json, 2, obj1, obj2);

        Database.set<object_t>(fbClientSend, "/systems/kitty_door/status/hw_override", json, cbSendTask, "SEND");
        hwOverride.previous = hwOverride.current;
    }
}

void sendAutoMode()
{
    if (app.ready())
    {
        JsonWriter writer;
        object_t json, obj1, obj2;
        writer.create(obj1, "status", !autoMode.current); // Backend checks if "auto-mode is overridden" so need to negate (silly)
        writer.create(obj2, "timestamp", String(millis()));
        writer.join(json, 2, obj1, obj2);

        Database.set<object_t>(fbClientSend, "/systems/kitty_door/status/override_auto", json, cbSendTask, "SEND");
        autoMode.previous = autoMode.current;
    }
}

void sendDoorState()
{
    if (app.ready())
    {
        JsonWriter writer;
        object_t json, obj1, obj2;
        writer.create(obj1, "status", doorstate.current);
        writer.create(obj2, "timestamp", String(millis()));
        writer.join(json, 2, obj1, obj2);

        Database.set<object_t>(fbClientSend, "/systems/kitty_door/status/door_state", json, cbSendTask, "SEND");
        doorstate.previous = doorstate.current;
    }
}
#pragma endregion

///////////////////////////////////
///// CALLBACK HANDLERS
//////////////////////////////////
#pragma region CALLBACK HANDLERS
int authErrors = 0;
void cbAuthTask(AsyncResult &aResult)
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
        int errCode = aResult.error().code();
        Firebase.printf("Error task: %s, msg: %s, code: %d\n", aResult.uid().c_str(), aResult.error().message().c_str(), errCode);

        if (errCode == -4) 
        {
            authErrors += 1;
            if (authErrors >= 10)
            {
                authErrors = 0;
                user_auth = UserAuth(API_KEY, USER_EMAIL, USER_PASSWORD);
                deinitializeApp(app);
                Database.resetApp();

                debugPrint("Resetting firebase connection...");
                delay(2000);
                initializeApp(fbClientSend, app, getAuth(user_auth), cbAuthTask, "AUTH");
                app.getApp<RealtimeDatabase>(Database);
                Database.url(DATABASE_URL);
                Database.setSSEFilters("get,put,patch,keep-alive,cancel,auth_revoked");
            }
        }
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

            // The stream event from RealtimeDatabaseResult can be converted to the values as following.
            bool v1 = RTDB.to<bool>();
            int v2 = RTDB.to<int>();
            float v3 = RTDB.to<float>();
            double v4 = RTDB.to<double>();
            String v5 = RTDB.to<String>();
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

void cbSendTask(AsyncResult &aResult)
{
    printResult(aResult);
}

void cbStreamTask(AsyncResult &aResult)
{
    // WARNING!
    // Do not put your codes inside the callback and printResult.

    if (aResult.available())
    {
        RealtimeDatabaseResult &RTDB = aResult.to<RealtimeDatabaseResult>();
        if (RTDB.isStream())
        {
            dbResult = RTDB;
            newDataReceived = true;
        }
    }
}

void handleNewFirebaseData()
{
    newDataReceived = false;
    JSONVar resObj = JSON.parse(dbResult.to<String>());

    // JSON.typeof(jsonVar) can be used to get the type of the variable
    if (JSON.typeof(resObj) == "undefined")
    {
        debugPrint("New Firebase Data: Parsing input failed!");
        return;
    }

    if (resObj.hasOwnProperty("type"))
    {
        String actionType = String((const char *)resObj["type"]);
        debugPrint(String("New Command: ") + actionType);
        if (actionType != NONE)
        {
            command = actionType;
        }
    }
}

void handleAutoMode()
{
    if (values.lightLevel >= values.openLightLevel)
    {
        if (values.autoModeBuffer == 0)
        {
            values.autoModeBuffer = millis() + BUFFER_TIME;
        }
        else if (millis() >= values.autoModeBuffer)
        {
            values.autoModeBuffer = 0;
            desiredState = STATE_OPEN;
        }
    }
    else if (values.lightLevel <= values.closeLightLevel)
    {
        if (values.autoModeBuffer == 0)
        {
            values.autoModeBuffer = millis() + BUFFER_TIME;
        }
        else if (millis() >= values.autoModeBuffer)
        {
            values.autoModeBuffer = 0;
            desiredState = STATE_CLOSED;
        }
    }
}

void handleCommand()
{
    if (command == COMMAND_OPEN)
    {
        if (!isHwForceCloseEnabled() && !isHwForceOpenEnabled())
        {
            desiredState = STATE_OPEN;
            autoMode.current = false;
        }
    }
    else if (command == COMMAND_CLOSE)
    {
        if (!isHwForceCloseEnabled() && !isHwForceOpenEnabled())
        {
            desiredState = STATE_CLOSED;
            autoMode.current = false;
        }
    }
    else if (command == COMMAND_READ_LIGHT_LEVEL)
    {
        sendLightLevel();
    }
    else if (command == COMMAND_SET_TO_AUTO)
    {
        autoMode.current = true;
    }
    else if (command == COMMAND_CHECK_ALIVE)
    {
        sendAlive();
    }
    else if (command != NONE)
    {
        debugPrint("Recieved unknown command: " + command);
    }

    command = NONE;
}

void printResult(AsyncResult &aResult)
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

            // The stream event from RealtimeDatabaseResult can be converted to the values as following.
            bool v1 = RTDB.to<bool>();
            int v2 = RTDB.to<int>();
            float v3 = RTDB.to<float>();
            double v4 = RTDB.to<double>();
            String v5 = RTDB.to<String>();
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
#pragma endregion

///////////////////////////////////
///// DEBUG FUNCTIONS
//////////////////////////////////
#pragma region DEBUG

// Ensure that debug messages aren't spammed to the console
#ifdef DEBUG_PRINTS
String dbg_string = "";
#endif
void debugPrint(String message)
{
#ifdef DEBUG_PRINTS
    if (dbg_string == message)
        return;

    dbg_string = message;
    Serial.println(dbg_string);
#endif
}
#pragma endregion