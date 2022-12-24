#include <FB_Const.h>
#include <FB_Error.h>
#include <FB_Network.h>
#include <FB_Utils.h>
#include <Firebase.h>
#include <FirebaseFS.h>
#include <Firebase_ESP_Client.h>
#include <MB_NTP.h>
#include <ESP8266WiFi.h>
#include "KittyDoorNew.h"

// Firebase Globals
FirebaseConfig fbConfig;		// Configuration for connecting to firebase
FirebaseAuth fbAuth;			  // Utilizes the email and pass
FirebaseData fbRecvData;		// Data object used to recieve commands from firebase
FirebaseData fbSendData;		// Data object used to send status/debug messages to firebase

// Door Globals
String command = NONE;          // The next command the door should perform

KittyDoorOptions options;       // All values pertaining to door options (i.e. triggering light levels, etc)
KittyDoorValues values;         // All values pertaining to current door status (i.e. current light level, state, etc)

/*
  'status.current' is used to keep track of the doors current state. This should strictly follow the current mode of the door
  motors when in a resting state (i.e. closed/opened) and will follow the transient state based on the desired state.
  
  'status.lastAutoMode' is used to keep track of which state the auto mode last knew about. This is used to ensure the auto
  mode does not re-trigger if the door is tampered with, and will only trigger an open/close action when the light level
  state changes.
*/
DoorStatus status = { NONE, /* current */ STATUS_OPEN, /* desired */ NONE /* lastAutoMode */ };

void setup() 
{
  Serial.begin(9600);

  set_pin_modes();

  set_default_door_options();

  read_initial_values();

  connect_to_wifi();

  connect_to_firebase();

  fetch_initial_options();

  delay(500);

  begin_streaming();
}

void loop() 
{
  /*
  * Read the hardware override pins and report any change to firebase. The desired
  * status might change based on the results of this function.
  */
  read_hardware_override();

  /* 
  * Read new door values and report any changes to the door's state to firebase. If
  * the door is found to be in a new resting state, the desired status will be changed
  * to NONE.
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
  if (!options.overrideAuto && values.forceOpen == HIGH && values.forceClose == HIGH)
  {
    // Delayed opening is in effect
    if (values.delayOpening > 0)
    {
      check_delayed_opening();
    }
    // Delayed closing is in effect
    else if (values.delayClosing > 0)
    {
      check_delayed_closing();
    }
    // Bright enough to open the door 
    else if (values.lightLevel >= options.openLightLevel)
    {
      // Auto mode has not tried opening the door yet
      if (status.lastAutoMode != STATUS_OPEN)
      {
        auto_open_door();
      }
      else
      {
        debug_print("Auto Mode has already opened the door.");
      }
      
    }
    // Dark enough to close the door 
    else if (values.lightLevel <= options.closeLightLevel)
    {
      // Auto mode has not tried closing the door yet
      if (status.lastAutoMode != STATUS_CLOSED)
      {
        auto_close_door();
      }
      else
      {
        debug_print("Auto Mode has already closed the door.");
      }
    }
  }

  delay(2000);
}

///////////////////////////////////
///// AUTO MODE FUNCTIONS
//////////////////////////////////
#pragma region AUTO MODE FUNCTIONS

// Sets the proper logic in motion to close the door whenever enough darkness
//  triggers an auto-close event.
void auto_close_door()
{
  // Do nothing if the door is already closed
  if (values.downSense == LOW)
  {
    status.desired = NONE;
    stop_door_motors();
    debug_print("Auto Close Door: Door is already closed!");
    return;
  }

  // Delayed closing -> set delay
  if (options.delayClosing)
  {
    values.delayClosing = millis() + options.delayClosingVal;
    debug_print("Auto Close Door: Dark enough to close!\n\tStarting delay of " + String(options.delayClosingVal) + " milliseconds...");
  }
  // Immediately close door
  else
  {
    status.desired = STATUS_CLOSED;
    status.lastAutoMode = STATUS_CLOSED;
    close_door();
    debug_print("Auto Close Door: Dark enough to close!\n\tImmediately closing door...");
  }
}

// Sets the proper logic in motion to open the door whenever enough light
//  triggers an auto-open event.
void auto_open_door()
{
  // Do nothing if the door is already open
  if (values.upSense == LOW)
  {
    stop_door_motors();
    debug_print("Check Light Level: Door is already open!");
    return;
  }

  // Delayed opening -> set delay
  if (options.delayOpening)
  {
    values.delayOpening = millis() + options.delayOpeningVal;
    debug_print("Check Light Level: Bright enough to open!\n\tStarting delay of " + String(options.delayOpeningVal) + " milliseconds...");
  }
  // Immediately open door
  else 
  {
    status.desired = STATUS_OPEN;
    status.lastAutoMode = STATUS_OPEN;
    open_door();
    debug_print("Check Light Level: Bright enough to open!\n\tImmediately opening door...");
  }
}

// Checks the current delay and either opens the door once the timer has
//  expired, or clears the delay and does nothing if conditions are no longer
//  met to open the door.
void check_delayed_opening()
{
  // Not time yet!
  if (millis() < values.delayOpening)
  {
    debug_print("Check Delayed Opening: Waiting for delay to pass...");
    return;
  }

  // It got too dark while waiting to open!
  if (values.lightLevel < options.openLightLevel)
  {
    debug_print("Check Delayed Opening: Too dark to open now!");
    values.delayOpening = -1;
    return;
  }

  // Door is already open!
  if (values.upSense == LOW)
  {
    debug_print("Check Delayed Opening: Door is already open!");
    values.delayOpening = -1;
    return;
  }

  debug_print("Check Delayed Opening: Opening door!");
  status.desired = STATUS_OPEN;
  status.lastAutoMode = STATUS_OPEN;
  open_door();
}

// Checks the current delay and either closes the door once the timer has
//  expired, or clears the delay and does nothing if conditions are no longer
//  met to close the door.
void check_delayed_closing()
{
  // Not time yet!
  if (millis() < values.delayClosing)
  {
    debug_print("Check Delayed Closing: Waiting for delay to pass...");
    return;
  }

  // It got too bright while waiting to close!
  if (values.lightLevel > options.closeLightLevel)
  {
    debug_print("Check Delayed Closing: Too light to close now!");
    values.delayClosing = -1;
    return;
  }

  // Door is already closed!
  if (values.downSense == LOW)
  {
    debug_print("Check Delayed Closing: Door is already closed!");
    values.delayClosing = -1;
    return;
  }

  debug_print("Check Delayed Closing: Closing door!");
  status.desired = STATUS_CLOSED;
  status.lastAutoMode = STATUS_CLOSED;
  close_door();
}
#pragma endregion

///////////////////////////////////
///// FIREBASE COMMANDS
//////////////////////////////////
#pragma region FIREBASE COMMANDS

// Forces the door open as long as the door is not in hardware override mode and is not
//  already open.
void handle_open_command()
{
  if (values.upSense == LOW)
  {
    debug_print("Handle Open Command: Command received, however door is already openend!");
    return;
  }

  if (values.forceClose == LOW || values.forceOpen == LOW)
  {
    debug_print("Handle Open Command: Command received, however currently in hardware override mode!");
    return;
  }

  // Door is in a proper state to receive OPEN command!
  status.desired = STATUS_OPEN;
  // Clear auto mode as it is now invalidated
  status.lastAutoMode = NONE;
  open_door();
  debug_print("Handle Open Command: Command received!");
}

// Forces the door closed as long as the door is not in hardware override mode and is not
//  already closed.
void handle_close_command()
{
  if (values.downSense == LOW)
  {
    debug_print("Handle Close Command: Command received, however door is already closed!");
    return;
  }

  if (values.forceClose == LOW || values.forceOpen == LOW)
  {
    debug_print("Handle Close Command: Command received, however currently in hardware override mode!");
    return;
  }

  // Door is in a proper state to receive CLOSE command!
  status.desired = STATUS_CLOSED;
  // Clear auto mode as it is now invalidated
  status.lastAutoMode = NONE;
  close_door();
  debug_print("Handle Close Command: Command receieved!");
}

// Top-level function that handles all commands received from firebase.
void process_new_command()
{
  // Change global immediately to consume the command
  String newCommand = command;
  command = NONE;

  // Handle Open Door Command
  if (newCommand == "\""+COMMAND_OPEN+"\"")
  {
    handle_open_command();
  }
  // Handle Close Door Command
  else if (newCommand == "\""+COMMAND_CLOSE+"\"")
  {
    handle_close_command();
  }
  // Handle Read Light Sensor
  else if (newCommand == "\""+COMMAND_READ_LIGHT_LEVEL+"\"")
  {
    debug_print("Process New Command: Sending ligth level (" + String(values.lightLevel) + ") to firebse!");
    fb_send_light_level();
  }
  // None Command is usually from a clear -> do nothing
  else if (newCommand == "\""+NONE+"\"")
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
  fb_send_options();
}
#pragma endregion

///////////////////////////////////
///// DOOR OPERATORS
//////////////////////////////////
#pragma region DOOR OPERATORS
void stop_door_motors()
{
  digitalWrite(PIN_OPEN_MOTOR, LOW);
  digitalWrite(PIN_CLOSE_MOTOR, LOW);
}

void open_door()
{
  digitalWrite(PIN_OPEN_MOTOR, HIGH);
  digitalWrite(PIN_CLOSE_MOTOR, LOW);
  values.delayOpening = -1;

  status.current = STATUS_OPENING;
  fb_send_door_state();
}

void close_door()
{
  digitalWrite(PIN_CLOSE_MOTOR, HIGH);
  digitalWrite(PIN_OPEN_MOTOR, LOW);
  values.delayClosing = -1;

  status.current = STATUS_CLOSING;
  
  fb_send_door_state();
}
#pragma endregion

///////////////////////////////////
///// VALUE READERS
//////////////////////////////////
#pragma region VALUE READERS

/**
  * Reads the hardware override switch and reports back to firebase whenever
  * a change has occured.
*/
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

      // Only start closing the door if force closing and not already closed
      if (values.forceClose == LOW && values.downSense == HIGH)
      {
        status.desired = STATUS_CLOSED;

        // Begin closing the door immediately
        close_door();
      }

      // Only start opening the door if force opening and not already open
      if (values.forceOpen == LOW && values.upSense == HIGH)
      {
        status.desired = STATUS_OPEN;
        
        // Begin opening the door immediately
        open_door();
      }

      // A change in override state invalidates last auto mode status.
      //  Clear and let auto mode logic take over
      status.lastAutoMode = NONE;  

      // Send new state to firebase
      fb_send_hardware_override_status();

      debug_print("Hardware Override Detected! \n\tForce Open: " + String(values.forceOpen) 
        + "\n\tForce Close: " + String(values.forceClose) + "\n\tDesired Status: " + status.desired);
    }
}

/**
 * Reads the values of the light sensor and the open/close state indicator pins.
 * If a new state is detected, it is reported to firebase.
*/
void read_door_values()
{
  values.lightLevel = analogRead(PIN_LIGHT_SENSOR);

  int newUpSense = digitalRead(PIN_UP_SENSE);
  int newDownSense = digitalRead(PIN_DOWN_SENSE);

  // New door state found -> report to firebase
  if (newUpSense != values.upSense || newDownSense != values.downSense)
  {
    // Set new values
    values.upSense = digitalRead(PIN_UP_SENSE);
    values.downSense = digitalRead(PIN_DOWN_SENSE);

    // Door is now open
    if (values.upSense == LOW)
    {
      stop_door_motors();

      // Error Check: Door should never read both opened and closed at the same time!
      if (values.downSense == LOW)
      {
        debug_print("ERROR (Read Door Values): Both upSense and downSense were LOW!");
        return;
      }

      status.current = STATUS_OPEN;
      status.desired = NONE;
    }
    // Door is now closed
    else if (values.downSense == LOW)
    {
      stop_door_motors();

      // Error Check: Door should never read both opened and closed at the same time!
      if (values.upSense == LOW)
      {
        debug_print("ERROR (Read Door Values): Both upSense and downSense were LOW!");
        return;
      }

      status.current = STATUS_CLOSED;
      status.desired = NONE;
    }
    // Door is in transient state
    else
    {
      // Error Check: Desired status should be either OPEN or CLOSED while in transient state
      if (status.desired != STATUS_OPEN && status.desired != STATUS_CLOSED)
      {
        debug_print("ERROR (Read Door Values): Desired status should not be " + status.desired + " while door is in a transient state!");
        return;
      }

      status.current = status.desired == STATUS_OPEN ? STATUS_OPENING : STATUS_CLOSING;
    }

    // Send new door state to firebase
    debug_print("Read Door Values: Sending door state to firebase (" + status.current + ")");
    fb_send_door_state();
  }
}
#pragma endregion

///////////////////////////////////
///// FIREBASE SEND FUNCTIONS
//////////////////////////////////
#pragma region FIREBASE SEND FUNCTIONS

void fb_send_options()
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
  }
}

void fb_send_light_level()
{
  FirebaseJson json;
  json.add("ll_timestamp", String(millis()));
  json.add("level", values.lightLevel);

  if(!Firebase.RTDB.setJSON(&fbSendData, PATH_STATUS_LIGHT_LEVEL, &json))
  {
    debug_print("ERROR (Send Light Level):\n\t" + fbSendData.errorReason());
  }  
}

void fb_send_hardware_override_status()
{
  FirebaseJson json;
  json.add("hw_timestamp", String(millis()));
  json.add("type", values.forceOpen == HIGH && values.forceClose == HIGH 
      ? 0 
      : values.forceOpen == HIGH 
          ? 1
          : 2
  );

  if (!Firebase.RTDB.setJSON(&fbSendData, PATH_STATUS_HW_OVERRIDE, &json))
  {
    debug_print("ERROR (Send Hardware Override Status):\n\t" + fbSendData.errorReason());
  }
}

void fb_send_door_state()
{
  FirebaseJson json;
  json.add("l_timestamp", String(millis()));
  json.add("type", status.current);

  if (!Firebase.RTDB.setJSON(&fbSendData, PATH_STATUS_DOOR, &json))
  {
    debug_print("ERROR (Send Door State):\n\t" + fbSendData.errorReason());
  }
}

#pragma endregion

///////////////////////////////////
///// SETUP FUNCTIONS
//////////////////////////////////
#pragma region SETUP FUNCTIONS

void read_initial_values()
{
  values.upSense = digitalRead(PIN_UP_SENSE);
  values.downSense = digitalRead(PIN_DOWN_SENSE);
  values.forceClose = digitalRead(PIN_FORCE_CLOSE);
  values.forceOpen = digitalRead(PIN_FORCE_OPEN);

  values.lightLevel = analogRead(PIN_LIGHT_SENSOR);

  // Safety measure to ensure door is not moving during setup
  close_door();
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

void fetch_initial_options()
{
  debug_print("Fetching initial options...");
  if (Firebase.RTDB.get(&fbRecvData, PATH_STREAM))
  {
    handleNewData(fbRecvData.to<FirebaseJson*>());
  }
  else
  {
    debug_print("ERROR: Failed to fetch initial options\n\tReason: " + fbRecvData.errorReason());
  }
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
	fbConfig.host = FIREBASE_HOST;
	fbConfig.api_key = FIREBASE_API;

	fbAuth.user.email = FIREBASE_USERNAME;
	fbAuth.user.password = FIREBASE_PASS;

  Firebase.begin(&fbConfig, &fbAuth);

  Firebase.reconnectWiFi(true);
	Firebase.RTDB.setMaxRetry(&fbRecvData, 4);
	Firebase.RTDB.setMaxErrorQueue(&fbRecvData, 30);

  fbRecvData.setResponseSize(1024);
}

void begin_streaming()
{
  debug_print("Streaming to: " + PATH_STREAM);

  if (!Firebase.RTDB.beginStream(&fbRecvData, PATH_STREAM))
  {
    debug_print("ERROR: Couldn't begin stream connection...\n\tReason: " + fbRecvData.errorReason());
  }

  Firebase.RTDB.setStreamCallback(&fbRecvData, fbCallback, fbTimeoutCallback);
}
#pragma endregion

///////////////////////////////////
///// FIREBASE CALLBACKS
//////////////////////////////////
#pragma region Firebase Callbacks
void fbCallback(FirebaseStream data)
{
    debug_print("Callback received stream data!");
    if (data.dataType() == "json")
    {
        debug_print("\tJson Data:");
        debug_print("\tSTREAM PATH: " + data.streamPath());
        debug_print("\tEVENT PATH: " + data.dataPath());
        debug_print("\tDATA TYPE: " + data.dataType());
        debug_print("\tEVENT TYPE: " + data.eventType());

        FirebaseJson* jsonData = data.jsonObjectPtr();
        handleNewData(jsonData);
    }
    else
    {
        debug_print("Callback received data of type: " + data.dataType());
    }
}

void fbTimeoutCallback(bool timeout)
{
    debug_print("fbTimeout occured!");
    if (timeout)
        debug_print("\tStream Tiemout");
}

void handleNewData(FirebaseJson* json)
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
      if (!options.overrideAuto)
      {
          debug_print("Handle New Options: REMOTE OVERRIDE DISABLED!");
          status.desired = NONE;
      }
    }
    else if (key == "command")
    {
      command = value;
    }
  }
}
#pragma endregion

///////////////////////////////////
///// DEBUG
//////////////////////////////////
#pragma region debug

// Test out the read functionality of Firebase Library
void read_test_endpoint()
{
  if (Firebase.RTDB.get(&fbRecvData, PATH_STREAM))
  {
    handleNewData(fbRecvData.to<FirebaseJson*>());
  }
  else 
  {
    Serial.println("Read Data FAILED:");
    Serial.println("\tReason: " + fbRecvData.errorReason());
  }
}

// Test out the send functionality of Firebase Library
void send_test_endpoint()
{
  FirebaseJson json;
  json.add("board_time", millis());
  json.add("strVal", "Hello! This is something to write to firebase!");
  //json.add("Ts/.sv", "timestamp");

  if (Firebase.RTDB.setJSON(&fbSendData, "status/debug_test", &json))
  {
    debug_print("Sent test message to firebase!");
  }
  else {
    debug_print("Error: Unable to send test message to firebase!\n\tReason: " + fbSendData.errorReason());
  }
}

// Ensure that debug messages aren't spammed to the console
String dbg_string = "";
void debug_print(String message)
{
  if (dbg_string == message)
    return;

  dbg_string = message;
  Serial.println(dbg_string);
}
#pragma endregion
