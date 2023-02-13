#include "KittyDoor.h"

#define DEFAULT_LOOP_DELAY 50
#define RESTING_LOOP_DELAY 500
unsigned long gLoopDelay = DEFAULT_LOOP_DELAY;
// int restartCount = 0;           // Used as a UID for controller debug statements
String gCommand = NONE;            // Used to process commands from Firebase
DoorStatus gDoorStatus;            // Keeps track of door state

KittyDoorOptions gOptions;         // All values pertaining to door options (i.e. triggering light levels, etc)
KittyDoorValues gValues;           // All values pertaining to current door status (i.e. current light level, state, etc)

// Firebase Variables
FirebaseData gFirebaseData;        // FirebaseESP8266 data object
FirebaseData gFirebaseSendData;    // FirebaseESP8266 data object used to send updates
FirebaseAuth gFirebaseAuth;        // FirebaseAuth data for authentication data
FirebaseConfig gFirebaseConfig;    // FirebaseConfig data for config data

///////////////////////////////////
///// SETUP FUNCTIONS
//////////////////////////////////
#pragma region Setup

// Pin values can be found in KittyDoor.h
void initialize_pins()
{
  //analogReference(INTERNAL);              // 1.1V ref. 1.08mV resolution on analog inputs
  pinMode(PIN_LIGHT_SENSOR, INPUT);         // Ambient light from photo sensor
  pinMode(PIN_UP_SENSE, INPUT_PULLUP);      // Low when door is fully open
  pinMode(PIN_DOWN_SENSE, INPUT_PULLUP);    // Low when door is fully closed
  pinMode(PIN_FORCE_OPEN, INPUT_PULLUP);    // If low, force door to open
  pinMode(PIN_FORCE_CLOSE, INPUT_PULLUP);   // If low, force door to close
  
  pinMode(PIN_OPEN_MOTOR, OUTPUT);          // If HIGH, door will open under normal circumstances
  pinMode(PIN_CLOSE_MOTOR, OUTPUT);         // If HIGH, door will close under normal circumstances
  
  digitalWrite(PIN_OPEN_MOTOR, LOW);        // Don't close door
  digitalWrite(PIN_CLOSE_MOTOR, LOW);       // Don't open door
}

// If we can't connect to firebase, ensure some default options are set so that the door
//  will work on boot-up.
void initialize_structs()
{
  gOptions.openLightLevel = 190;
  gOptions.closeLightLevel = 40;
  gOptions.delayOpening = false;
  gOptions.delayOpeningVal = 0;
  gOptions.delayClosing = false;
  gOptions.delayClosingVal = 0;

  gValues.delayOpening = -1;
  gValues.delayClosing = -1;
  gValues.forceClose = -1;
  gValues.forceOpen = -1;

  // After initial calibration, the door should be in the OPEN state
  gDoorStatus.current = STATUS_OPEN;
  gDoorStatus.desired = STATUS_OPEN;
}

// Ensure the door is in the up/open state.
void reset_door()
{
  open_door();

  // Wait until door is completely open
  while (digitalRead(PIN_UP_SENSE) != LOW)
    delay(10);

  stop_door_motors();
}

// Wifi credentials are stored in KittyDoor.h
void initialize_wifi()
{
  WiFi.begin(WIFI_AP_NAME, WIFI_AP_PASS);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();
}

// Set up the config, auth, and connection properties here.
void initialize_firebase()
{
  gFirebaseConfig.token_status_callback = tokenStatusCallback;
  gFirebaseConfig.host = FIREBASE_HOST;
  gFirebaseConfig.api_key = FIREBASE_API;

  gFirebaseAuth.user.email = FIREBASE_EMAIL;
  gFirebaseAuth.user.password = FIREBASE_PASS;

  Firebase.begin(&gFirebaseConfig, &gFirebaseAuth);

  Firebase.reconnectWiFi(true);
  Firebase.RTDB.setMaxRetry(&gFirebaseData, 5);
  Firebase.RTDB.setMaxErrorQueue(&gFirebaseData, 30);

  //Recommend for ESP8266 stream, adjust the buffer size to match stream data size
  #if defined(ESP8266)
    gFirebaseData.setBSSLBufferSize(1024 /* Rx in bytes */, 1024 /* Tx in bytes */);
  #endif
}

// These should be fetched first-thing to overwrite the initalized struct values.
void fetch_initial_options()
{
  debug_print("Fetching initial options...");
  FirebaseJson initialOptions;
  if (Firebase.RTDB.getJSON(&gFirebaseData, PATH_STREAM, &initialOptions))
  {
    parse_firebase_data(&initialOptions);
    gCommand = NONE;
  }
  else {
    debug_print("FAILED\n\tREASON: " + gFirebaseData.errorReason());
  }
}

// Firebase path can be found in KittyDoor.h
void begin_streaming()
{
  debug_print("Streaming to: " + PATH_STREAM);

  if (!Firebase.RTDB.beginStream(&gFirebaseData, PATH_STREAM))
    Serial.printf("stream begin error, %s\n\n", gFirebaseData.errorReason().c_str());
  Firebase.RTDB.setStreamCallback(&gFirebaseData, handle_data_received, handle_timeout);
}

// Main-level setup function called on boot-up
void setup()
{
  Serial.begin(9600);

  initialize_pins();

  initialize_structs();

  reset_door();

  initialize_wifi();

  initialize_firebase();

  // Get Initial Options From Firebase
  fetch_initial_options();

  begin_streaming();
}
#pragma endregion Setup

///////////////////////////////////
///// FIREBASE HANDLERS
//////////////////////////////////
#pragma region Firebase Handlers

// Should resume if still connected, otherwise we need to force a reconnection any way possible.
void handle_timeout(bool timeout)
{
  if (timeout)
    debug_print("Stream timeout, resume streaming...");

  // Bad solution - Force a reset to attempt setup again
  if (!gFirebaseData.httpConnected())
  {
    Serial.printf("error code: %d, reason: %s\n\n", gFirebaseData.httpCode(), gFirebaseData.errorReason().c_str());
    delay(150);
    ESP.reset();
  }
}
  
void handle_data_received(FirebaseStream data)
{
  if (data.dataType() == "json")
  {
    debug_print(String("Callback received stream data!\nJson Data:")
      + String("\n\tSTREAM PATH: ") + String(data.streamPath())
      + String("\n\tEVENT PATH: ") + String(data.dataPath())
      + String("\n\tDATA TYPE: ") + String(data.dataType())
      + String("\n\tEVENT TYPE: ") + String(data.eventType()));

    parse_firebase_data(data.jsonObjectPtr());
  }
  else
  {
    debug_print("Error: Callback received data of type: " + data.dataType());
  } 
}
#pragma endregion Firebase Handlers

///////////////////////////////////
///// DEBUG FUNCTIONS
//////////////////////////////////
#pragma region Debug

// Ensure that debug messages aren't spammed to the console.
String gLastDebugMessage = "";
void debug_print(String message)
{
  if (gLastDebugMessage != message)
    Serial.println(message);
  
  gLastDebugMessage = message;
}

#pragma endregion Debug

///////////////////////////////////
///// PARSE FUNCTIONS
//////////////////////////////////
#pragma region Parse Functions

void parse_firebase_data(FirebaseJson* json)
{
  size_t len = json->iteratorBegin();
  String key, value = "";
  int type = 0;

  // Parse each option
  for (size_t i = 0; i < len; i++)
  {
    json->iteratorGet(i, type, key, value);
    if (key == "closeLightLevel")
      parse_closeLightLevel(value.toInt());
    else if (key == "openLightLevel")
      parse_openLightLevel(value.toInt());
    else if (key == "delayClosingVal")
      parse_delayClosingVal(value.toInt());
    else if (key == "delayOpeningVal")
      parse_delayOpeningVal(value.toInt());
    else if (key == "delayOpening")
      parse_delayOpening(value);
    else if (key == "delayClosing")
      parse_delayOpening(value);
    else if (key == "overrideAuto")
      parse_overrideAuto(value);
    else if (key == "command")
      gCommand = value;
  }
}

/* Parse Helper Functions */
#pragma region Parse Helper Functions

void send_options_to_firebase()
{
  FirebaseJson json;
  json.add("closeLightLevel", gOptions.closeLightLevel);
  json.add("openLightLevel", gOptions.openLightLevel);
  json.add("delayOpening", gOptions.delayOpening);
  json.add("delayClosing", gOptions.delayClosing);
  json.add("delayOpeningVal", gOptions.delayOpeningVal);
  json.add("delayClosingVal", gOptions.delayClosingVal);
  json.add("o_timestamp", String(millis()));
  json.add("autoOverride", gOptions.overrideAuto);
  json.add("command", NONE);

  if (!Firebase.RTDB.setJSON(&gFirebaseSendData, PATH_STREAM, &json))
  {
    debug_print("(send_options_to_firebase) ERROR: " + gFirebaseSendData.errorReason());
  }  
}

void parse_overrideAuto(String value)
{
  gOptions.overrideAuto = value == "true";
  if (!gOptions.overrideAuto)
  {
    // Invalidate door desired status to force a change if needed
    gDoorStatus.desired = NONE;
  }
}

void parse_delayClosing(String value)
{
  gOptions.delayClosing = value == "true";
  if (!gOptions.delayClosing)
  {
    gValues.delayClosing = -1;
  }
}

void parse_delayOpening(String value)
{
  gOptions.delayOpening = value == "true";
  if (!gOptions.delayOpening)
  {
    gValues.delayOpening = -1;
  }
}

void parse_closeLightLevel(int newLevel)
{
  gOptions.closeLightLevel = clamp_int(newLevel, MIN_LIGHT_LEVEL, MAX_LIGHT_LEVEL);
}

void parse_openLightLevel(int newLevel)
{
  gOptions.openLightLevel = clamp_int(newLevel, MIN_LIGHT_LEVEL, MAX_LIGHT_LEVEL);
}

void parse_delayClosingVal(int newVal)
{
  if (newVal >= 0)
  {
    gOptions.delayClosingVal = newVal;
    gValues.delayClosing = -1;
  }
}

void parse_delayOpeningVal(int newVal)
{
    if (newVal >= 0)
  {
    gOptions.delayOpeningVal = newVal;
    gValues.delayOpening = -1;
  }
}
#pragma endregion Parse Helper Functions
#pragma endregion Parse Functions

///////////////////////////////////
///// FIREBASE COMMAND HANDLERS
//////////////////////////////////
#pragma region Command Handlers

/**
 * Top-level function that determines which command to handle. Also clears the command on firebase
 * to allow for new commands to be received.
 */
void process_command()
{
  String newCommand = gCommand;
  gCommand = NONE;

  // None Command is usually from a clear -> do nothing
  if (newCommand == "\""+NONE+"\"" || newCommand == NONE)
    return;
  // Handle Open Door Command
  else if (newCommand == "\""+COMMAND_OPEN+"\"" || newCommand == COMMAND_OPEN)
    handle_open_command();
  // Handle Close Door Command
  else if (newCommand == "\""+COMMAND_CLOSE+"\"" || newCommand == COMMAND_CLOSE)
    handle_close_command();
  // Handle Read Light Sensor
  else if (newCommand == "\""+COMMAND_READ_LIGHT_LEVEL+"\"" || newCommand == COMMAND_READ_LIGHT_LEVEL)
    handle_requested_light_level();
  // Unknown Command
  else 
    debug_print("parse_new_command: Unknown command received -> " + newCommand);

  // Clears the command on firebase's end, signifying that the command has been received
  //  and consumed on the door's end
  send_options_to_firebase();
}

// Called when user requests to force open the door.
void handle_open_command()
{
  // Do nothing if is door already opened
  if (gValues.upSense == LOW)
  {
    debug_print("handle_open_command: Door already opened -> Doing nothig...");
    return;
  }

  // Do nothing if hardware override is enabled
  if (gValues.forceClose == LOW || gValues.forceOpen == LOW)
  {
    debug_print("handle_open_command: Hardware override enabled -> Doing nothig...");
    return;
  }

  gDoorStatus.desired = STATUS_OPEN;
  open_door();
  debug_print("handle_open_command: Door can be opened -> Opening...");
}

// Called when user requests to force close the door.
void handle_close_command()
{
    // Do nothing if is door already closed
  if (gValues.downSense == LOW)
  {
    debug_print("handle_close_command: Door already closed -> Doing nothig...");
    return;
  }

  // Do nothing if hardware override is enabled
  if (gValues.forceClose == LOW || gValues.forceOpen == LOW)
  {
    debug_print("handle_close_command: Hardware override enabled -> Doing nothig...");
    return;
  }

  gDoorStatus.desired = STATUS_CLOSED;
  close_door();
  debug_print("handle_close_command: Door can be opened -> Closing...");
}

// Callend when user requests current light readings from sensor.
void handle_requested_light_level()
{
  // Read light level
  FirebaseJson json;
  json.add("ll_timestamp", String(millis()));
  json.add("level", gValues.lightLevel);

  // Send light level to firebase
  if(!Firebase.RTDB.setJSON(&gFirebaseSendData, PATH_STATUS_LIGHT_LEVEL, &json))
    debug_print("(handle_requested_light_level) ERROR: " + gFirebaseSendData.errorReason()); 
}

#pragma endregion Command Handlers

///////////////////////////////////
///// VALUE READING FUNCTIONS
//////////////////////////////////
#pragma region Value Readers

/**
 * Reads the values of the light sensor and the open/close state indicator pins.
 * If a new state is detected, it is reported to firebase.
*/
void read_door_state()
{
  gValues.lightLevel = analogRead(PIN_LIGHT_SENSOR);

  int newUpSense = digitalRead(PIN_UP_SENSE);
  int newDownSense = digitalRead(PIN_DOWN_SENSE);

  // New door state found -> report to firebase
  if (newUpSense != gValues.upSense || newDownSense != gValues.downSense)
  {
    // Set new values
    gValues.upSense = newUpSense;
    gValues.downSense = newDownSense;

    // Set current state
    if (gValues.upSense == LOW)
    {
      gDoorStatus.current = STATUS_OPEN;
      gDoorStatus.desired = NONE; // Resting state signifies desired state achieved
      stop_door_motors();
      gLoopDelay = RESTING_LOOP_DELAY;  // Increases delay to prevent "bouncing" on the sensor
    }
    else if (gValues.downSense == LOW)
    {
      gDoorStatus.current = STATUS_CLOSED;
      gDoorStatus.desired = NONE; // Resting state signifies desired state achieved
      stop_door_motors();
      gLoopDelay = RESTING_LOOP_DELAY;  // Increases delay to prevent "bouncing" on the sensor
    }
    else
    {
      // Desired status signifies which action the door will try to take, putting the
      //  door in a transient state.
      gDoorStatus.current = gDoorStatus.desired == STATUS_OPEN
        ? STATUS_OPENING
        : STATUS_CLOSING;

      // Assuming that door is in transient state, reduce loop delay to catch the door hitting
      //  a resting state quickly. Too long of a delay could cause the door to "miss" reading
      //  the open/closed sensor and cause desync
      gLoopDelay = DEFAULT_LOOP_DELAY;
    }

    // Any change in door state should be reported to firebase
    send_door_state_to_firebase();    
  }
}

/**
  * Sends the latest state of the door to firebase. This should be called whenever 
  * the door sensors read something different.
*/
void send_door_state_to_firebase()
{
  FirebaseJson json;
  json.add("l_timestamp", String(millis()));
  json.add("type", gDoorStatus.current);

  if (!Firebase.RTDB.setJSON(&gFirebaseSendData, PATH_STATUS_DOOR, &json))
    debug_print("(send_door_state_to_firebase) ERROR: " + gFirebaseSendData.errorReason());
}

/**
  * Reads the hardware override switch and reports back to firebase whenever
  * a change has occured.
*/
void read_hardware_override()
{
  int newForceClose = digitalRead(PIN_FORCE_CLOSE);
  int newForceOpen = digitalRead(PIN_FORCE_OPEN);

  // New mode found -> report to firebase
  if (newForceClose != gValues.forceClose || newForceOpen != gValues.forceOpen)
  {
    gValues.forceClose = newForceClose;
    gValues.forceOpen = newForceOpen;

    // Any change to the hardware switch will invalidate whatever remote command was previously
    //  given. Setting these will allow the automatic mode to determine the state of the
    //  door until another remote command is given.
    gOptions.overrideAuto = false;

    // Trigger an action from the door to get it in the proper state
    gDoorStatus.desired = gValues.forceOpen == LOW 
      ? STATUS_OPEN 
      : gValues.forceClose == LOW
        ? STATUS_CLOSED
        : NONE;

    // Send hardware override status to firebase
    send_hw_override_to_firebase();
  }
}

/**
  * Sends the latest status of whether or not the door is being overriden by the 
  * hardware override switch.
*/
void send_hw_override_to_firebase()
{
  FirebaseJson json;
  json.add("hw_timestamp", String(millis()));
  json.add("type", gValues.forceOpen == HIGH && gValues.forceClose == HIGH 
    ? 0 /* OFF */
    : gValues.forceOpen == HIGH 
      ? 1  /* FORCE CLOSE */
      : 2  /* FORCE OPEN */
    );

  if (!Firebase.RTDB.setJSON(&gFirebaseSendData, PATH_STATUS_HW_OVERRIDE, &json))
    debug_print("(send_hw_override_to_firebase) ERROR: " + gFirebaseSendData.errorReason());
}

#pragma endregion Value Readers

///////////////////////////////////
///// DOOR OPERATIONS
//////////////////////////////////
#pragma region Door Operations

void open_door()
{
  digitalWrite(PIN_OPEN_MOTOR, HIGH);
  digitalWrite(PIN_CLOSE_MOTOR, LOW);
  delay(DOOR_OPERATION_DELAY);  // Defined in KittyDoor.h

  // We always want to quickly catch when the door hits a resting state after
  //  opening the door
  gLoopDelay = DEFAULT_LOOP_DELAY;
}

void close_door()
{
  digitalWrite(PIN_CLOSE_MOTOR, HIGH);
  digitalWrite(PIN_OPEN_MOTOR, LOW);
  delay(DOOR_OPERATION_DELAY);  // Defined in KittyDoor.h

  // We always want to quickly catch when the door hits a resting state after
  //  closing the door
  gLoopDelay = DEFAULT_LOOP_DELAY;
}

void stop_door_motors()
{
  digitalWrite(PIN_OPEN_MOTOR, LOW);
  digitalWrite(PIN_CLOSE_MOTOR, LOW);
}

/**
 * Determines if the light level is at a point where the door should automatically open/close itself.
 * In order to prevent this from continuously triggering, gDoorStatus.desired is used to keep track of the
 * last action while in auto mode and prevents subsequent calls until the state has changed.
 *
 * For example:
 * If the light level triggers the door to open, gDoorStatus.desired will be set to STATUS_OPEN
 * and will prevent auto mode from triggering more open events until that status is cleared (i.e. via
 * an override command from remote/hardware switch or light level drops low enough to trigger a close
 * event.)
 */
void run_auto_mode()
{
  // Auto Open Door
  if (gValues.lightLevel >= gOptions.openLightLevel && gDoorStatus.desired != STATUS_OPEN)
  {
    // Door might already be in open state if a remote command toggled auto mode while the door is in
    //  the proper position.
    if (gValues.upSense != LOW)
    {
      // Do nothing while door is in transient state - this assumes up motor is running
      if (gDoorStatus.current != STATUS_OPENING)
      {
        debug_print("AUTO MODE: Opening door!");

        // Update desired status to match what state auto mode thinks the door should be in
        gDoorStatus.desired = STATUS_OPEN;
        open_door();
      }
    }
    else
      debug_print("AUTO MODE: Door already open.");
  }
  // Auto Close Door
  else if (gValues.lightLevel <= gOptions.closeLightLevel && gDoorStatus.desired != STATUS_CLOSED)
  {
    // Door might already be in closed state if a remote command toggled auto mode while the door is in
    //  the proper position.
    if (gValues.downSense != LOW)
    {
      if (gDoorStatus.current != STATUS_CLOSING)
      {
        debug_print("AUTO MODE: Closing door!");    

        // Update desired status to match what state auto mode thinks the door should be in
        gDoorStatus.desired = STATUS_CLOSED;  
        close_door();
      }
    }
  }
}

/**
 * Forces the door closed unless the sensor already says the door is closed. Clearing the desired status
 * might not be needed here, as the door shouldn't operate based on this value. It is merely used to
 * determine transient states.
 */
void run_force_close()
{
  if (gValues.downSense == LOW)
  {
    debug_print("Hardware Force Close has shut the door.");
    gDoorStatus.desired = NONE;
  }
  else
    close_door();
}

/**
 * Forces the door open unless the sensor already says the door is opened. Clearing the desired status
 * might not be needed here, as the door shouldn't operate based on this value. It is merely used to
 * determine transient states.
 */
void run_force_open()
{
  if (gValues.upSense == LOW)
  {
    debug_print("Hardware Force open has opened the door.");
    gDoorStatus.desired = NONE;
  }
  else
    open_door();
}
#pragma endregion Door Operations

///////////////////////////////////
///// GENERIC HELPER FUNCTIONS
//////////////////////////////////
#pragma region Generic Helper Functions

bool is_in_auto_mode()
{
  return !gOptions.overrideAuto && gValues.forceClose == HIGH && gValues.forceOpen == HIGH;
}

int clamp_int(int value, int min, int max)
{
  return value < MIN_LIGHT_LEVEL
    ? MIN_LIGHT_LEVEL
    : value > MAX_LIGHT_LEVEL
      ? MAX_LIGHT_LEVEL
      : value;
}
#pragma endregion Generic Helper Functions


void loop()
{
  // Read Door State - Desired Status can be cleared here!
  read_door_state();

  // Read hardware override - Desired Status can change here!
  read_hardware_override();

  // Read any new command from firebase
  process_command();

  // Door is in force close mode and an action is required
  if (gValues.forceClose == LOW && gDoorStatus.desired == STATUS_CLOSED)
  {
    run_force_close();
  }
  // Door is in force open mode and an action is required
  else if (gValues.forceOpen == LOW && gDoorStatus.desired == STATUS_OPEN)
  {
    run_force_open();
  }
  // Door should determine its state from light level
  else if (is_in_auto_mode())
  {
    run_auto_mode();
  }

  // This delay varies based on if a resting state has previously been recorded
  delay(gLoopDelay);
}
