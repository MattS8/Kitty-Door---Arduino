#pragma once

// DON'T UPLOAD VALUES
#define FIREBASE_HOST "ENTER_HOST_URL_HERE"         // Can be found on your RTDB page in firebase console
#define FIREBASE_API "ENTER_API_KEY_HERE"           // Can be found in the project settings in firebase console
#define FIREBASE_USERNAME "ENTER_USER_EMAIL_HERE"   // The authorized email account linked to your firebase project (can be found in authentication tab in firebase console)
#define FIREBASE_PASS "ENTER_USER_PASS_HERE"        // The authorized email password linked to your firebase project (can be found in authentication tab in firebase console)
static const String WIFI_AP_NAME = "WIFI_HERE";     // WiFi network to connect to
static const String WIFI_AP_PASS = "PASS_HERE";     // WiFi password

///////////////////////////////////
///// PINS
//////////////////////////////////
#pragma region Pins
  const int PIN_LIGHT_SENSOR = A0;
  const int PIN_UP_SENSE = 13;
  const int PIN_DOWN_SENSE = 12;
  const int PIN_FORCE_OPEN = 14;
  const int PIN_FORCE_CLOSE = 2;
  const int PIN_OPEN_MOTOR = 5;
  const int PIN_CLOSE_MOTOR = 4;
#pragma endregion

///////////////////////////////////
///// FIREBASE PATHS
//////////////////////////////////
#pragma region Firebase Paths
  static const String PATH_STATUS_DOOR = "status/kitty_door/";
  static const String PATH_STATUS_HW_OVERRIDE = "status/kitty_door_hw_override";
  static const String PATH_STATUS_LIGHT_LEVEL = "status/kitty_door_light_level";
  static const String PATH_STREAM = "systems/kitty_door";
#pragma endregion

///////////////////////////////////
///// ARDUINO CONSTANTS
//////////////////////////////////
#pragma region Arduino Constants
  static const String STATUS_OPEN = "OPEN";
  static const String STATUS_CLOSED = "CLOSED";
  static const String STATUS_OPENING = "OPENING";
  static const String STATUS_CLOSING = "CLOSING";

  static const String NONE = "_none_";

  static const String COMMAND_OPEN = "openKittyDoor";
  static const String COMMAND_CLOSE = "closeKittyDoor";
  static const String COMMAND_READ_LIGHT_LEVEL = "readLightLevel";

  const int MAX_LIGHT_LEVEL = 1023;
  const int MIN_LIGHT_LEVEL = 0;
#pragma endregion

///////////////////////////////////
///// STRUCTS
//////////////////////////////////
#pragma region Structures
  typedef struct DoorStatus 
  {
    String desired;
    String current;
    String lastAutoMode;
  } DoorStatus;

  typedef struct KittyDoorValues 
  {
      int lightLevel;
      int upSense;
      int downSense;
      int forceOpen;
      int forceClose;
      long delayClosing;
      long delayOpening;
  } KittyDoorValues;

  typedef struct KittyDoorOptions 
  {
      int openLightLevel;
      int closeLightLevel;
      int delayOpeningVal;
      int delayClosingVal;
      bool delayOpening;
      bool delayClosing;
      bool overrideAuto;
  } KittyDoorOptions;
#pragma endregion