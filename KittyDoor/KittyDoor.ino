#include <ESP8266WiFi.h>
#include <Firebase_ESP_Client.h>

#include "RTDBHelper.h"
#include "TokenHelper.h"

#include "WifiCreds.h"

#include "KittyDoor.h"

// int restartCount = 0;          // Used as a UID for controller debug statements
String command = NONE;            // Used to process commands from Firebase
String oldDoorStatus = NONE;      // Used to determine status changes
String desiredDoorStatus = NONE;  // Used for remote override

KittyDoorOptions options;         // All values pertaining to door options (i.e. triggering light levels, etc)
KittyDoorValues values;           // All values pertaining to current door status (i.e. current light level, state, etc)

// Firebase Variables
FirebaseData firebaseData;        // FirebaseESP8266 data object
FirebaseData firebaseSendData;    // FirebaseESP8266 data object used to send updates
FirebaseAuth auth;                // FirebaseAuth data for authentication data
FirebaseConfig firebaseConfig;    // FirebaseConfig data for config data

unsigned int firebaseLogCounter;  // Used to log unique messages to firebase

void setup()
{
  Serial.begin(9600);

  //analogReference(INTERNAL);              // 1.1V ref. 1.08mV resolution on analog inputs
  pinMode(PIN_LIGHT_SENSOR, INPUT);         // Ambient light from photo sensor
  pinMode(PIN_UP_SENSE, INPUT_PULLUP);      // Low when door is fully open
  pinMode(PIN_DOWN_SENSE, INPUT_PULLUP);    // Low when door is fully closed
  pinMode(PIN_FORCE_OPEN, INPUT_PULLUP);    // If low, force door to open
  pinMode(PIN_FORCE_CLOSE, INPUT_PULLUP);   // If low, force door to close
  //
  pinMode(PIN_OPEN_MOTOR, OUTPUT);          // If HIGH, door will open
  pinMode(PIN_CLOSE_MOTOR, OUTPUT);         // If HIGH, door will close
  //
  digitalWrite(PIN_OPEN_MOTOR, LOW);        // Don't close door
  digitalWrite(PIN_CLOSE_MOTOR, LOW);       // Don't open door

  firebaseLogCounter = 0;

  options.openLightLevel = 190;
  options.closeLightLevel = 40;
  options.delayOpening = false;
  options.delayOpeningVal = 0;
  options.delayClosing = false;
  options.delayClosingVal = 0;

  values.delayOpening = -1;
  values.delayClosing = -1;

  // Connect to WiFi
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

  // Connect to Firebase
  firebaseConfig.token_status_callback = tokenStatusCallback;
  firebaseConfig.database_url = FIREBASE_HOST;
  firebaseConfig.api_key = FIREBASE_AUTH;

  auth.user.email = FIREBASE_EMAIL;
  auth.user.password = FIREBASE_PASS;

  Firebase.reconnectWiFi(true);
  Firebase.setDoubleDigits(5);

  Firebase.begin(&firebaseConfig, &auth);
  //Recommend for ESP8266 stream, adjust the buffer size to match stream data size
  #if defined(ESP8266)
    firebaseData.setBSSLBufferSize(2048 /* Rx in bytes, 512 - 16384 */, 512 /* Tx in bytes, 512 - 16384 */);
  #endif

  // Get log counter
  Serial.println("Fetching log counter...");
  if (Firebase.RTDB.getInt(&firebaseData, "debug/kitty_door/msg_count"))
  {
    firebaseLogCounter = (unsigned int) firebaseData.to<int>();
    Serial.print("Log counter set to: ");
    Serial.println(firebaseLogCounter);
  }
  else 
  {
    Serial.println("FAILED");
    Serial.println("REASON: " + firebaseData.errorReason());
  }

  // Get Initial Options From Firebase
  Serial.println("Fetching initial options...");
  FirebaseJson initialOptions;
  if (Firebase.RTDB.getJSON(&firebaseData, PATH_OPTIONS, &initialOptions))
  {
    handleNewOptions(&initialOptions);
    sendFirebaseMessage("Initial Firebase Options fetched... current counter: " + firebaseLogCounter);
  }
  else {
    Serial.println("FAILED");
    Serial.println("REASON: " + firebaseData.errorReason());
  }

  // Begin Streaming
  Serial.print("Streaming to: ");
  Serial.println(PATH_OPTIONS);

  if (!Firebase.RTDB.beginStream(&firebaseData, PATH_OPTIONS))
    Serial.printf("stream begin error, %s\n\n", firebaseData.errorReason().c_str());
  Firebase.RTDB.setStreamCallback(&firebaseData, handleDataRecieved, handleTimeout);
}

void handleTimeout(bool timeout)
{
  if (timeout)
  {
    Serial.println();
    Serial.println("Stream timeout, resume streaming...");
    Serial.println();
  }
  if (!firebaseData.httpConnected())
  {
    Serial.printf("error code: %d, reason: %s\n\n", firebaseData.httpCode(), firebaseData.errorReason().c_str());
    //ESP.reset();
  }
}

void sendFirebaseMessage(String message)
{
  FirebaseJson json;
  json.add("count", String(++firebaseLogCounter));
  json.add("message", message);

  Firebase.RTDB.setJSON(&firebaseSendData, PATH_DEBUG_MESSAGE + firebaseLogCounter, &json);
}

/* -------- Firebase Handlers -------- */

void handleNewOptions(FirebaseJson *json)
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
        sendFirebaseMessage("Option Change - openLightLevel set to: " + value.toInt());
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
          desiredDoorStatus = NONE;
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

void handleDataRecieved(FirebaseStream data)
{
  if (data.dataTypeEnum() == fb_esp_rtdb_data_type_json)
  {
    Serial.println("Stream data available...");
    handleNewOptions(data.to<FirebaseJson*>());
  }  
}

// void handleDataRecieved(StreamData data)
// {
//   if (data.dataType() == "json")
//   {
//     Serial.println("Stream data available...");
//     // Serial.println("STREAM PATH: " + data.streamPath());
//     // Serial.println("EVENT PATH: " + data.dataPath());
//     // Serial.println("DATA TYPE: " + data.dataType());
//     // Serial.println("EVENT TYPE: " + data.eventType());
//     // Serial.print("VALUE: ");
//     // printResult(data);
//     // Serial.println();

//     FirebaseJson *json = data.jsonObjectPtr();
//     handleNewOptions(json);
//   }
// }

void handleNewCommand()
{
  String newCommand = command;
  command = NONE;
  oldDoorStatus = doorStatus;

  Serial.print("Handling new command: ");
  Serial.println(newCommand);

  if (newCommand == COMMAND_OPEN)
  {
    if (values.upSense == HIGH)
    {
      if (values.forceOpen == HIGH && values.forceClose == HIGH)
      {
        Serial.println("   opening door!");
        desiredDoorStatus = STATUS_OPEN;
        openDoor();
      }
      else 
      {
        Serial.println("   attempted to open door, but hardware override is enabled!");
      }
    } else {
      Serial.println("   door is already open!");
    }
  }
  else if (newCommand == COMMAND_CLOSE)
  {
    if (values.downSense == HIGH)
    {
      if (values.forceClose == HIGH && values.forceOpen == HIGH)
      {
        Serial.println("   closing door!");
        desiredDoorStatus = STATUS_CLOSED;
        closeDoor();
      }
      else 
      {
        Serial.println("   attempted to close door, but hardware override is enabled!");       
      }
    } else {
      Serial.println("   door is already closed!");
    }
  }
  else if (newCommand == COMMAND_READ_LIGHT_LEVEL)
  {
    Serial.print("   writing light level of ");
    Serial.print(values.lightLevel);
    Serial.println(" to firebase!");
    writeDoorLightLevelToFirebase();
  }

  if (oldDoorStatus != doorStatus)
  {
    Serial.print("   status changed from ");
    Serial.print(oldDoorStatus);
    Serial.print(" to ");
    Serial.println(doorStatus);
    writeDoorStatusToFirebase();
  }

  writeOptionsToFirebase();
}

/* -------- Firebase Write Operations -------- */

void writeDoorStatusToFirebase()
{
  FirebaseJson json;
  json.add("l_timestamp", String(millis()));
  json.add("type", doorStatus);

  Firebase.RTDB.setJSON(&firebaseSendData, PATH_STATUS_DOOR, &json);
}

// 0 = no force open/close; 1 = force open; 2 = force close
void writeHWOverrideToFirebase()
{
  FirebaseJson json;
  json.add("hw_timestamp", String(millis()));
  json.add("type", values.forceOpen == HIGH && values.forceClose == HIGH ? 0 : values.forceOpen == HIGH ? 1
                                                                                                        : 2);

  Firebase.RTDB.setJSON(&firebaseSendData, PATH_STATUS_HW_OVERRIDE, &json);
}

void writeDoorLightLevelToFirebase()
{
  FirebaseJson json;
  json.add("ll_timestamp", String(millis()));
  json.add("level", values.lightLevel);
  Firebase.RTDB.setJSON(&firebaseSendData, PATH_STATUS_LIGHT_LEVEL, &json);
}

void writeOptionsToFirebase()
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

  Firebase.RTDB.setJSON(&firebaseSendData, PATH_OPTIONS, &json);
}

/* -------- Sensor Checks -------- */

void checkHardwareOverride()
{
  int newForceClose = digitalRead(PIN_FORCE_CLOSE);
  int newForceOpen = digitalRead(PIN_FORCE_OPEN);

  if (newForceOpen != values.forceOpen || newForceClose != values.forceClose)
  {
    values.forceOpen = newForceOpen;
    values.forceClose = newForceClose;
    desiredDoorStatus = values.forceOpen == LOW 
      ? STATUS_OPEN 
      : values.forceClose == LOW 
        ? STATUS_CLOSED
        : NONE;
    options.overrideAuto = false;

    writeHWOverrideToFirebase();
  }
}

void checkLightLevel()
{
  // Check if light change causes a new desired state
  int oldLightLevel = values.lightLevel;
  values.lightLevel = analogRead(PIN_LIGHT_SENSOR);

  // If in an "override" mode, don't mess with the desiredDoorStatus based on light!
  if (values.forceOpen == LOW || values.forceClose == LOW || options.overrideAuto)
    return;

  // Changing from "too light to close" to "dark enough to close"
  if (oldLightLevel > options.closeLightLevel && values.lightLevel <= options.closeLightLevel)
  {
    desiredDoorStatus = STATUS_CLOSED;
  }
  // Changing from "too dark to open" to "light enough to open"
  else if (oldLightLevel < options.openLightLevel && values.lightLevel >= options.openLightLevel)
  {
    desiredDoorStatus = STATUS_OPEN;
  }
}

/* -------- Door Motor Operations -------- */

void openDoor()
{
  digitalWrite(PIN_OPEN_MOTOR, HIGH);
  digitalWrite(PIN_CLOSE_MOTOR, LOW);
  doorStatus = STATUS_OPENING;
  values.delayOpening = -1;
}

void closeDoor()
{
    digitalWrite(PIN_CLOSE_MOTOR, HIGH);
    digitalWrite(PIN_OPEN_MOTOR, LOW);
    doorStatus = STATUS_CLOSING;
    values.delayClosing = -1;
}

/* -------- Door Motor Events -------- */

void doorHasOpened()
{
  digitalWrite(PIN_OPEN_MOTOR, LOW);
  digitalWrite(PIN_CLOSE_MOTOR, LOW);
  doorStatus = STATUS_OPEN;
  desiredDoorStatus = NONE;
}

void doorHasClosed()
{
  digitalWrite(PIN_OPEN_MOTOR, LOW);
  digitalWrite(PIN_CLOSE_MOTOR, LOW);
  doorStatus = STATUS_CLOSED;
  desiredDoorStatus = NONE;
}

/* -------- Big Ugly Long Loop Function -------- */
void loop()
{
  // Read new values
  checkHardwareOverride();
  checkLightLevel();
  values.upSense = digitalRead(PIN_UP_SENSE);
  values.downSense = digitalRead(PIN_DOWN_SENSE);
  oldDoorStatus = doorStatus;

  // Handle any incoming commands/option changes from firebase
  if (command != NONE)
  {
    handleNewCommand();
  }

  // Check if hardware force open is enabled
  // If enabled and door is not up, open door
  if (values.forceOpen == LOW)
  {
    Serial.println("Force Open Enabled:");
    if (desiredDoorStatus == STATUS_OPEN && values.upSense == HIGH)
    {
      Serial.println("   Opening door...");
      openDoor();
    }
    else
    { // Door is already open
      Serial.println("   Door is already opened!");
      doorHasOpened();
    }
  }
  // Else, check if hardware force close is enabled
  // If enabled and door is not down, close door
  else if (values.forceClose == LOW)
  {
    Serial.println("Force Close Enabled:");
    if (desiredDoorStatus == STATUS_CLOSED && values.downSense == HIGH)
    {
      Serial.println("   Closing door...");
      closeDoor();
    }
    else 
    { // Door is already closed
      Serial.println("   Door is already closed!");
      doorHasClosed();
    }
  }
  // Else, check if we are in automatic mode or remote override mode
  else if (options.overrideAuto)
  {
    Serial.println("   Remote Override Mode Enabled!");
    if (desiredDoorStatus == STATUS_CLOSED)
    { // Remote command to CLOSE door
      if (values.downSense == HIGH)
      { // Door is not closed yet
        Serial.println("   Closing Door...");
        closeDoor();
      }
      else 
      { // Door is already closed
        Serial.print("   Door is closed!");
        doorHasClosed();
      }
    }
    else if (desiredDoorStatus == STATUS_OPEN)
    { // Remote command to OPEN door
      if (values.upSense == HIGH)
      { // Door is not open yet
        Serial.println("   Opening Door...");
        openDoor();
      }
      else 
      { // Door is already open
        Serial.println("   Door is open!");
        doorHasOpened();
      }
    }
    else 
    { // Door has already been opened/closed based off of the last recieved override. Nothing needs to be done until
      //  the next override command or is set back to auto mode 
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
        if (desiredDoorStatus == STATUS_OPEN && values.upSense == HIGH)
        {
          Serial.println("      Opening Door...");
          openDoor();
        }
        else 
        {
          Serial.println("      Door already opened...");
          doorHasOpened();
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
        if (desiredDoorStatus == STATUS_CLOSED && values.downSense == HIGH)
        {
          Serial.println("      Closing door...");
          closeDoor();  
        }
        else 
        {
          Serial.println("      Door already closed...");
          doorHasClosed();
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
    if (desiredDoorStatus == STATUS_OPEN && values.upSense == HIGH)
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
        openDoor();
      }
    }
    else 
    {
      Serial.println("      Door already open...");  
      doorHasOpened();
    }
  }
  // Else, check if light level is low enough to close
  else if (values.lightLevel <= options.closeLightLevel)
  {
      Serial.println("   Automatic Mode Enabled!");
      Serial.println("      It's dark enough to close!");
    if (desiredDoorStatus == STATUS_CLOSED && values.downSense == HIGH)
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
        closeDoor();
      }
    }
    else 
    {
      Serial.println("      Door already closed...");  
      doorHasClosed();
    }
  }
  // Else, make sure the door is doing nothing
  else 
  {
    Serial.print("   Door resting at: ");
    Serial.println(doorStatus);
    digitalWrite(PIN_CLOSE_MOTOR, LOW);
    digitalWrite(PIN_OPEN_MOTOR, LOW);

    if (desiredDoorStatus != NONE)
    {
      Serial.print("WARNING: desiredDoorStatus was not none, however door is currently resting... (");
      Serial.print(desiredDoorStatus);
      Serial.println(")");
    }
  }

  if (oldDoorStatus != doorStatus)
  {
    Serial.println();
    Serial.print("Door status changed from ");
    Serial.print(oldDoorStatus);
    Serial.print(" to ");
    Serial.print(doorStatus);
    Serial.println();
    writeDoorStatusToFirebase();
  }

  // Serial.print("Sensor Values:");
  // Serial.print("  -> Light Level: ");Serial.print(values.lightLevel);
  // Serial.print("  -> Up Sense: ");Serial.print(values.upSense);
  // Serial.print("  -> Down Sense: ");Serial.print(values.downSense);
  // Serial.print("  -> Force Open: ");Serial.print(values.forceOpen);
  // Serial.print("  -> Force Close: ");Serial.println(values.forceClose);

  //   if (force_open_value == LOW && up_sense_value == HIGH){
  //     digitalWrite (open_door,HIGH);  //Open the door
  //     digitalWrite (close_door,LOW);
  //     light_above_open_value = 0;
  //     light_below_close_value = 0;
  //     goto stop_motor_skip;
  //   }

  //   if (force_close_value == LOW && force_open_value == HIGH && down_sense_value == HIGH){
  //     digitalWrite (open_door,LOW);
  //     digitalWrite (close_door,HIGH); //Close the door
  //     light_above_open_value = 0;
  //     light_below_close_value = 0;
  //     goto stop_motor_skip;
  //   }
  //   if (light_above_open_value == HIGH && up_sense_value == HIGH && force_close_value == HIGH){
  //     digitalWrite (open_door,HIGH);  //Open the door
  //     digitalWrite (close_door,LOW);
  //     goto stop_motor_skip;
  //   }
  //   if (light_below_close_value == HIGH && down_sense_value == HIGH && force_open_value == HIGH){
  //     digitalWrite (open_door,LOW);
  //     digitalWrite (close_door,HIGH); //Close the door
  //     goto stop_motor_skip;
  //   }
  //   digitalWrite (open_door,LOW);  //Stop motor
  //   digitalWrite (close_door,LOW); //Stop motor

  // stop_motor_skip:

  // Serial.print("light_level: ");Serial.print(light_level);
  // Serial.print("    up_sense_value: ");Serial.print(up_sense_value);
  // Serial.print("    down_sense_value: ");Serial.print(down_sense_value);
  // Serial.print("    force_open_value: ");Serial.print(force_open_value);
  // Serial.print("    force_close_value: ");Serial.println(force_close_value);
}
