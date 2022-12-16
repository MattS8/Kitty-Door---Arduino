#pragma once
#define FIREBASE_HOST "home-controller-286c0.firebaseio.com"
#define FIREBASE_AUTH "ldaTh4Iy2foRFvAPfdlib5kPMyYchWKF1RKdUGDQ"

#pragma region Pins
    const int PIN_LIGHT_SENSOR = A0;
    const int PIN_UP_SENSE = 13;
    const int PIN_DOWN_SENSE = 12;
    const int PIN_FORCE_OPEN = 14;
    const int PIN_FORCE_CLOSE = 2;
    const int PIN_OPEN_MOTOR = 5;
    const int PIN_CLOSE_MOTOR = 4;
#pragma endregion

#pragma region Firebase Paths
    static const String PATH_STATUS_DOOR = "status/kitty_door/";
    static const String PATH_STATUS_HW_OVERRIDE = "status/kitty_door_hw_override";
    static const String PATH_STATUS_LIGHT_LEVEL = "status/kitty_door_light_level";
    static const String PATH_OPTIONS = "systems/kitty_door";
    static const String PATH_RESTART_COUNT = "debug/kitty_door/restart_count";
    static const String PATH_DEBUG_MESSAGE = "debug/kitty_door/messages/";
#pragma endregion

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


#pragma region Structures
    typedef struct DoorStatus {
        String old;
        String desired;
        String current;
    } DoorStatus;

    typedef struct DebugValues {
        unsigned int logCounter;
    } DebugValues;

    typedef struct KittyDoorValues {
        int lightLevel;
        int hwOverride;
        int upSense;
        int downSense;
        int forceOpen;
        int forceClose;
        long delayClosing;
        long delayOpening;
    } KittyDoorValues;

    typedef struct KittyDoorOptions {
        int openLightLevel;
        int closeLightLevel;
        int delayOpeningVal;
        int delayClosingVal;
        bool delayOpening;
        bool delayClosing;
        bool overrideAuto;
    } KittyDoorOptions;
#pragma endregion

