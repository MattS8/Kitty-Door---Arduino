#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "FirebaseESP8266.h"
// #include <FirebaseJson.h>
#include "WifiCreds.h"


static const String FIREBASE_HOST = "home-controller-286c0.firebaseio.com";
static const String FIREBASE_AUTH = "ldaTh4Iy2foRFvAPfdlib5kPMyYchWKF1RKdUGDQ";

/* -------------------- Arduino Constants -------------------- */
static const String STATUS_OPEN = "OPEN";
static const String STATUS_CLOSED = "CLOSED";
static const String STATUS_OPENING = "OPENING";
static const String STATUS_CLOSING = "CLOSING";

static const String NONE = "_none_";

static const String COMMAND_OPEN = "openKittyDoor";
static const String COMMAND_CLOSE = "closeKittyDoor";
static const String COMMAND_READ_LIGHT_LEVEL = "readLightLevel";

const int MAX_LIGHT_LEVEL = 1500;
const int MIN_LIGHT_LEVEL = 0;

/* -------------------- Pin Values -------------------- */
const int PIN_LIGHT_SENSOR = A0;
const int PIN_UP_SENSE = 2;
const int PIN_DOWN_SENSE = 12;
const int PIN_FORCE_OPEN = 14;
const int PIN_FORCE_CLOSE = 13;

// ??
const int PIN_OPEN_MOTOR = 5;
const int PIN_CLOSE_MOTOR = 4;

/* -------------------- Firebase Paths -------------------- */
static const String PATH_STATUS_DOOR = "status/kitty_door/";
static const String PATH_STATUS_HW_OVERRIDE = "status/kitty_door_hw_override";
static const String PATH_STATUS_LIGHT_LEVEL = "statis/kitty_door_light_level";
static const String PATH_OPTIONS = "systems/kitty_door";
static const String PATH_RESTART_COUNT = "debug/kitty_door/restart_count";

// CUSTOME FIREBASE DEFINES
#define FIREBASE_PIN_A0 88
typedef struct KittyDoorStatus {
    String door;
    int lightLevel;
    int hwOverride;
    int upSense;
    int downSense;
    int forceOpen;
    int forceClose;
    long delayClosing;
    long delayOpening;
} KittyDoorStatus;

typedef struct KittyDoorOptions {
    int openLightLevel;
    int closeLightLevel;
    int delayOpeningVal;
    int delayClosingVal;
    bool delayOpening;
    bool delayClosing;
} KittyDoorOptions;

