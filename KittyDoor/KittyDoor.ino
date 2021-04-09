#include "KittyDoor.h"


// int restartCount = 0;           // Used as a UID for controller debug statements
String command = NONE;          // Used to process commands from Firebase
String oldDoorStatus = "";      // Used to determine status changes

KittyDoorOptions options;       // All values pertaining to door options (i.e. triggering light levels, etc)
KittyDoorStatus status;         // All values pertaining to current door status (i.e. current light level, state, etc)

// Firebase Variables
FirebaseData firebaseData;      // FirebaseESP8266 data object
FirebaseData firebaseSendData;  // FirebaseESP8266 data object used to send updates

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

  options.openLightLevel = 800;
  options.closeLightLevel = 350;
  options.delayOpening = false;
  options.delayOpeningVal = 0;
  options.delayClosing = false;
  options.delayClosingVal = 0;

  status.delayOpening = -1;
  status.delayClosing = -1;

  // open_threshold = 800;                 // Open door if ambient light level > this
  // close_threshold = 350;                // Close door if ambient light level < this
  // light_above_open_value = 0;           // Initialize light threshold variables
  // light_below_close_value = 0;          // Initialize light threshold variables

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

  // Fetch Restart Count
  // if (Firebase.get(firebaseData, PATH_RESTART_COUNT))
  // {
  //   Serial.print("Current restartCount: ");
  //   Serial.println(firebase.intData());
  //   Serial.print("Current restartCount: ");
  //   Serial.println(firebaseData.intData());
  //   restartCount = firebaseData.intData() + 1;

  //   // Set Restart Count
  //   Firebase.setInt(firebaseSendData, PATH_RESTART_COUNT, restartCount);
  //   delay(500);
  // }
  // else
  // {
  //   Serial.println("Failed to get restart_count");
  // }

  // // Fetch Door Options
  FirebaseJson jsonObject;
  if (Firebase.get(firebaseData, PATH_OPTIONS))
  {
    jsonObject.setJsonData(firebaseData.jsonString());
    handleNewOptions(&jsonObject);
  }
  else
  {
    Serial.println("Unable to fetch initial Options...");
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
        options.openLightLevel = temp;
      }
      else if (key == "delayClosingVal")
      {
        temp = value.toInt();
        if (temp >= 0)
        {
          options.delayClosingVal = temp;
          status.delayClosing = -1;
        }
      }
      else if (key == "delayOpeningVal")
      {
        temp = value.toInt();
        if (temp >= 0)
        {
          options.delayOpeningVal = temp;
          status.delayOpening = -1;
        }
      }
      else if (key == "delayOpening")
      {
        Serial.print("delayOpening: ");
        Serial.println(value);
        options.delayOpening = value == "true";
        if (!options.delayOpening)
        {
          status.delayOpening = -1;
        }
      }
      else if (key == "delayClosing")
      {
        Serial.print("delayClosing: ");
        Serial.println(value);
        options.delayClosing = value == "true";
        if (!options.delayClosing)
        {
          status.delayClosing = -1;
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
    Serial.println("STREAM PATH: " + data.streamPath());
    Serial.println("EVENT PATH: " + data.dataPath());
    Serial.println("DATA TYPE: " + data.dataType());
    Serial.println("EVENT TYPE: " + data.eventType());
    Serial.print("VALUE: ");
    printResult(data);
    Serial.println();

    FirebaseJson *json = data.jsonObjectPtr();
    handleNewOptions(json);
  }
}

void writeDoorStatusToFirebase()
{
  FirebaseJson json;
  json.add("l_timestamp", String(millis()));
  json.add("type", status.door);

  Firebase.set(firebaseSendData, PATH_STATUS_DOOR, json);
}

// 0 = no force open/close; 1 = force open; 2 = force close
void writeHWOverrideToFirebase()
{
  FirebaseJson json;
  json.add("hw_timestamp", String(millis()));
  json.add("type", status.forceOpen == HIGH && status.forceClose == HIGH ? 0 : status.forceOpen == HIGH ? 1
                                                                                                        : 2);

  Firebase.set(firebaseSendData, PATH_STATUS_HW_OVERRIDE, json);
}

void writeDoorLightLevelToFirebase()
{
  FirebaseJson json;
  json.add("ll_timestamp", String(millis()));
  json.add("level", status.lightLevel);
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
  json.add("command", NONE);

  Firebase.set(firebaseSendData, PATH_OPTIONS, json);
}

void checkHardwareOverride()
{
  int newForceClose = digitalRead(PIN_FORCE_CLOSE);
  int newForceOpen = digitalRead(PIN_FORCE_OPEN);

  if (newForceOpen != status.forceOpen || newForceClose != status.forceClose)
  {
    status.forceOpen = newForceOpen;
    status.forceClose = newForceClose;

    writeHWOverrideToFirebase();
  }
}

void attempToOpenDoor()
{
  if (options.delayOpening) // Haven't started delay yet, do so now!
  {
    if (status.delayOpening < 0)
    {
      status.delayOpening = millis() + options.delayOpeningVal;
    }
    else if (millis() > status.delayOpening)
    {
      if (status.upSense == HIGH)
      {
        openDoor();
      }
      status.delayOpening = -1;
    }
  }
  else if (status.upSense == HIGH)
  {
    openDoor();
  }
}

void openDoor()
{
  digitalWrite(PIN_OPEN_MOTOR, HIGH);
  digitalWrite(PIN_CLOSE_MOTOR, LOW);
  status.door = STATUS_OPENING;
  status.delayOpening = -1;
}

void closeDoor()
{
    digitalWrite(PIN_CLOSE_MOTOR, HIGH);
    digitalWrite(PIN_OPEN_MOTOR, LOW);
    status.door = STATUS_CLOSING;
    status.delayClosing = -1;
}

void handleNewCommand()
{
  String newCommand = command;
  command = NONE;
  oldDoorStatus = status.door;

  if (newCommand == COMMAND_OPEN)
  {
    if (status.upSense == HIGH)
    {
      openDoor();
    }
  }
  else if (newCommand == COMMAND_CLOSE)
  {
    if (status.downSense == HIGH)
    {
      closeDoor();
    }
  }
  else if (newCommand == COMMAND_READ_LIGHT_LEVEL)
  {
    writeDoorLightLevelToFirebase();
  }

  if (oldDoorStatus != status.door)
  {
    writeDoorStatusToFirebase();
  }

  writeOptionsToFirebase();
}

void doorHasOpened()
{
  digitalWrite(PIN_OPEN_MOTOR, LOW);
  digitalWrite(PIN_CLOSE_MOTOR, LOW);
  status.door = STATUS_OPEN;
}

void doorHasClosed()
{
  digitalWrite(PIN_OPEN_MOTOR, LOW);
  digitalWrite(PIN_CLOSE_MOTOR, LOW);
  status.door = STATUS_CLOSED;
}

void loop()
{
  // Read new values
  checkHardwareOverride();
  status.lightLevel = analogRead(PIN_LIGHT_SENSOR);
  status.upSense = analogRead(PIN_UP_SENSE);
  status.downSense = analogRead(PIN_DOWN_SENSE);
  oldDoorStatus = status.door;

  // Handle any incoming commands/option changes from firebase
  if (command != NONE)
  {
    handleNewCommand();
  }

  // Check if hardware force open is enabled
  // If enabled and door is not up, open door
  if (status.forceOpen == LOW)
  {
    if (status.upSense == HIGH)
    {
      openDoor();
    }
    else
    { // Door is already open
      doorHasOpened();
    }
  }
  // Else, check if hardware force close is enabled
  // If enabled and door is not down, close door
  else if (status.forceClose == LOW)
  {
    if (status.downSense == HIGH)
    {
      closeDoor();
    }
    else 
    { // Door is already closed
      doorHasClosed();
    }
  }
  // Else, check if a delay to open is in effect
  // If in effect, check to see if it is time to open the door
  else if (status.delayOpening > 0)
  {
    if (millis() > status.delayOpening)
    {
      if (status.lightLevel >= options.openLightLevel)
      {
        openDoor();
      }
      else 
      { // It got dark before delay time was over!
        status.delayOpening = -1;
      }
    }
  }
  // Else, check if a delay to close is in effect
  // If in effect, check to see if it is time to close the door
  else if (status.delayClosing > 0)
  {
    if (millis() > status.delayClosing)
    {
      if (status.lightLevel <= options.closeLightLevel)
      {
        closeDoor();
      }
      else 
      { // It got light before delay time was over!
        status.delayOpening = -1;
      }
    }
  } 
  // Else, check if light level is high enough to open
  else if (status.lightLevel >= options.openLightLevel)
  {
    if (status.upSense == HIGH)
    {
      openDoor();
    }
    else 
    {
      doorHasOpened();
    }
  }
  // Else, check if light level is low enough to close
  else if (status.lightLevel <= options.closeLightLevel)
  {
    if (status.downSense == HIGH)
    {
      closeDoor();
    }
    else 
    {
      doorHasClosed();
    }
  }
  // Else, make sure the door is doing nothing
  else 
  {
    digitalWrite(PIN_CLOSE_MOTOR, LOW);
    digitalWrite(PIN_OPEN_MOTOR, LOW);
  }

  if (oldDoorStatus != status.door)
  {
    writeDoorStatusToFirebase();
  }

  Serial.print("Sensor Values:");
  Serial.print("  -> Light Level: ");Serial.print(status.lightLevel);
  Serial.print("  -> Up Sense: ");Serial.print(status.upSense);
  Serial.print("  -> Down Sense: ");Serial.print(status.downSense);
  Serial.print("  -> Force Open: ");Serial.print(status.forceOpen);
  Serial.print("  -> Force Close: ");Serial.println(status.forceClose);

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
