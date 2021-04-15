#include "KittyDoor.h"


// int restartCount = 0;           // Used as a UID for controller debug statements
String command = NONE;            // Used to process commands from Firebase
String oldDoorStatus = NONE;      // Used to determine status changes
String desiredDoorStatus = NONE;  // Used for remote override

KittyDoorOptions options;       // All values pertaining to door options (i.e. triggering light levels, etc)
KittyDoorValues values;         // All values pertaining to current door status (i.e. current light level, state, etc)

// Firebase Variables
FirebaseData firebaseData;      // FirebaseESP8266 data object
FirebaseData firebaseSendData;  // FirebaseESP8266 data object used to send updates

unsigned int firebaseLogCounter;    // Used to log unique messages to firebase

void setup()
{
  // put your setup code here, to run once:
  Serial.begin(9600);
  //analogReference(INTERNAL);              // 1.1V ref. 1.08mV resolution on analog inputs
  pinMode(PIN_LIGHT_SENSOR, INPUT);       // Ambient light from photo sensor
  pinMode(PIN_UP_SENSE, INPUT_PULLUP);    // Low when door is fully open
  pinMode(PIN_DOWN_SENSE, INPUT_PULLUP);  // Low when door is fully closed
  pinMode(PIN_FORCE_OPEN, INPUT_PULLUP);  // If low, force door to open
  pinMode(PIN_FORCE_CLOSE, INPUT_PULLUP); // If low, force door to close
  //
  pinMode(PIN_OPEN_MOTOR, OUTPUT);  // If HIGH, door will open
  pinMode(PIN_CLOSE_MOTOR, OUTPUT); // If HIGH, door will close
  //
  digitalWrite(PIN_OPEN_MOTOR, LOW);  // Don't close door
  digitalWrite(PIN_CLOSE_MOTOR, LOW); // Don't open door

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
  Firebase.begin(FIREBASE_HOST, FIREBASE_AUTH);
  Firebase.reconnectWiFi(true);
  Firebase.setMaxRetry(firebaseData, 4);
  firebaseData.setResponseSize(1024);

  // Get log counter
  Serial.println("Fetching log counter...");
  if (Firebase.get(firebaseData, "debug/kitty_door/msg_count"))
  {
    firebaseLogCounter = (unsigned int) firebaseDat.intData();
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
  if (Firebase.get(firebaseData, PATH_OPTIONS))
  {
    handleNewOptions(firebaseData.jsonData());
    sendFirebaseMessage("Initial Firebase Options fetched... current counter: " + firebaseLogCounter);
  }
  else {
    Serial.println("FAILED");
    Serial.println("REASON: " + firebaseData.errorReason());
  }


  // Begin Streaming
  Serial.print("Streaming to: ");
  Serial.println(PATH_OPTIONS);

  if (!Firebase.beginStream(firebaseData, PATH_OPTIONS))
  {
    Serial.println("------------------------------------");
    Serial.println("Can't begin stream connection...");
    Serial.println("REASON: " + firebaseData.errorReason());
    Serial.println("------------------------------------");
    Serial.println();
  }

  Firebase.setStreamCallback(firebaseData, handleDataRecieved, handleTimeout);
}

void handleTimeout(bool timeout)
{
  if (timeout)
  {
    Serial.println();
    Serial.println("Stream timeout, resume streaming...");
    Serial.println();
  }
}

void sendFirebaseMessage(String message)
{
  FirebaseJson json;
  json.add("count", String(++firebaseLogCounter));
  json.add("message", message);

  Firebase.set(firebaseSendData, PATH_DEBUG_MESSAGE + firebaseLogCounter, json);
}

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

void handleDataRecieved(StreamData data)
{
  if (data.dataType() == "json")
  {
    Serial.println("Stream data available...");
    // Serial.println("STREAM PATH: " + data.streamPath());
    // Serial.println("EVENT PATH: " + data.dataPath());
    // Serial.println("DATA TYPE: " + data.dataType());
    // Serial.println("EVENT TYPE: " + data.eventType());
    // Serial.print("VALUE: ");
    // printResult(data);
    // Serial.println();

    FirebaseJson *json = data.jsonObjectPtr();
    handleNewOptions(json);
  }
}

void writeDoorStatusToFirebase()
{
  FirebaseJson json;
  json.add("l_timestamp", String(millis()));
  json.add("type", doorStatus);

  Firebase.set(firebaseSendData, PATH_STATUS_DOOR, json);
}

// 0 = no force open/close; 1 = force open; 2 = force close
void writeHWOverrideToFirebase()
{
  FirebaseJson json;
  json.add("hw_timestamp", String(millis()));
  json.add("type", values.forceOpen == HIGH && values.forceClose == HIGH ? 0 : values.forceOpen == HIGH ? 1
                                                                                                        : 2);

  Firebase.set(firebaseSendData, PATH_STATUS_HW_OVERRIDE, json);
}

void writeDoorLightLevelToFirebase()
{
  FirebaseJson json;
  json.add("ll_timestamp", String(millis()));
  json.add("level", values.lightLevel);
  Firebase.set(firebaseSendData, PATH_STATUS_LIGHT_LEVEL, json);
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

  Firebase.set(firebaseSendData, PATH_OPTIONS, json);
}

void checkHardwareOverride()
{
  int newForceClose = digitalRead(PIN_FORCE_CLOSE);
  int newForceOpen = digitalRead(PIN_FORCE_OPEN);

  if (newForceOpen != values.forceOpen || newForceClose != values.forceClose)
  {
    values.forceOpen = newForceOpen;
    values.forceClose = newForceClose;
    desiredDoorStatus = NONE;
    options.overrideAuto = false;

    writeHWOverrideToFirebase();
  }
}

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

void doorHasOpened()
{
  digitalWrite(PIN_OPEN_MOTOR, LOW);
  digitalWrite(PIN_CLOSE_MOTOR, LOW);
  doorStatus = STATUS_OPEN;
}

void doorHasClosed()
{
  digitalWrite(PIN_OPEN_MOTOR, LOW);
  digitalWrite(PIN_CLOSE_MOTOR, LOW);
  doorStatus = STATUS_CLOSED;
}

void loop()
{
  // Read new values
  checkHardwareOverride();
  values.lightLevel = analogRead(PIN_LIGHT_SENSOR);
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
    if (values.upSense == HIGH)
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
    if (values.downSense == HIGH)
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
    { // Bad state, should never reach this
      Serial.print("===== ERROR: remote override enabled, but desired status was in an unexpected state (");
      Serial.print(desiredDoorStatus);
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
        if (values.downSense == HIGH)
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

// DEBUG
void printResult(StreamData &data)
{

  if (data.dataType() == "int")
    Serial.println(data.intData());
  else if (data.dataType() == "float")
    Serial.println(data.floatData(), 5);
  else if (data.dataType() == "double")
    printf("%.9lf\n", data.doubleData());
  else if (data.dataType() == "boolean")
    Serial.println(data.boolData() == 1 ? "true" : "false");
  else if (data.dataType() == "string" || data.dataType() == "null")
    Serial.println(data.stringData());
  else if (data.dataType() == "json")
  {
    Serial.println();
    FirebaseJson *json = data.jsonObjectPtr();
    //Print all object data
    Serial.println("Pretty printed JSON data:");
    String jsonStr;
    json->toString(jsonStr, true);
    Serial.println(jsonStr);
    Serial.println();
    Serial.println("Iterate JSON data:");
    Serial.println();
    size_t len = json->iteratorBegin();
    String key, value = "";
    int type = 0;
    for (size_t i = 0; i < len; i++)
    {
      json->iteratorGet(i, type, key, value);
      Serial.print(i);
      Serial.print(", ");
      Serial.print("Type: ");
      Serial.print(type == FirebaseJson::JSON_OBJECT ? "object" : "array");
      if (type == FirebaseJson::JSON_OBJECT)
      {
        Serial.print(", Key: ");
        Serial.print(key);
      }
      Serial.print(", Value: ");
      Serial.println(value);
    }
    json->iteratorEnd();
  }
  else if (data.dataType() == "array")
  {
    Serial.println();
    //get array data from FirebaseData using FirebaseJsonArray object
    FirebaseJsonArray *arr = data.jsonArrayPtr();
    //Print all array values
    Serial.println("Pretty printed Array:");
    String arrStr;
    arr->toString(arrStr, true);
    Serial.println(arrStr);
    Serial.println();
    Serial.println("Iterate array values:");
    Serial.println();

    for (size_t i = 0; i < arr->size(); i++)
    {
      Serial.print(i);
      Serial.print(", Value: ");

      FirebaseJsonData *jsonData = data.jsonDataPtr();
      //Get the result data from FirebaseJsonArray object
      arr->get(*jsonData, i);
      if (jsonData->typeNum == FirebaseJson::JSON_BOOL)
        Serial.println(jsonData->boolValue ? "true" : "false");
      else if (jsonData->typeNum == FirebaseJson::JSON_INT)
        Serial.println(jsonData->intValue);
      else if (jsonData->typeNum == FirebaseJson::JSON_FLOAT)
        Serial.println(jsonData->floatValue);
      else if (jsonData->typeNum == FirebaseJson::JSON_DOUBLE)
        printf("%.9lf\n", jsonData->doubleValue);
      else if (jsonData->typeNum == FirebaseJson::JSON_STRING ||
               jsonData->typeNum == FirebaseJson::JSON_NULL ||
               jsonData->typeNum == FirebaseJson::JSON_OBJECT ||
               jsonData->typeNum == FirebaseJson::JSON_ARRAY)
        Serial.println(jsonData->stringValue);
    }
  }
  else if (data.dataType() == "blob")
  {

    Serial.println();

    for (int i = 0; i < data.blobData().size(); i++)
    {
      if (i > 0 && i % 16 == 0)
        Serial.println();

      if (i < 16)
        Serial.print("0");

      Serial.print(data.blobData()[i], HEX);
      Serial.print(" ");
    }
    Serial.println();
  }
  else if (data.dataType() == "file")
  {

    Serial.println();

    File file = data.fileStream();
    int i = 0;

    while (file.available())
    {
      if (i > 0 && i % 16 == 0)
        Serial.println();

      int v = file.read();

      if (v < 16)
        Serial.print("0");

      Serial.print(v, HEX);
      Serial.print(" ");
      i++;
    }
    Serial.println();
    file.close();
  }
}
