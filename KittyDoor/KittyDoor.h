#pragma once
///////////////////////////////////
///// FORWARD DECLARATIONS
//////////////////////////////////
#pragma region FORWARD DECLARATIONS

// Debug
void debug_prin(String message, bool debugEnabled);

/**
 * @brief Parses stream result string and performs an action.
 * Actions are either firebase commands or send functions.
 * 
 */
void handleStreamResult();

// Firebase Send Functions
#pragma region Firebase Send Functions
void send_options();
void send_light_level();
void send_hardware_override_status();
void send_door_state();
#pragma endregion //Firebase Send Functions

// Firebase Commands Functions
#pragma region Firebase Command Functions
/**
 * @brief Forces the door open as long as the door is not in hardware override mode and is not
 * already open.
 * 
 */
void handle_open_command();

/**
 * @brief Forces the door closed as long as the door is not in hardware override mode and is not
 * already closed.
 */
void handle_close_command();
#pragma endregion // Firebase Send Functions

// Door Functions
#pragma region Door Functions
/**
 * @brief Forces both door motors to stop immediately.
 * 
 */
void stop_door_motors();
void open_door(bool reportToFirebase = true);
void close_door(bool reportToFirebase = true);
void operate_on_door(int closeMotor, int openMotor, String transStatus, String finalStatus, bool (*isDoorResting)(), bool reportToFirebase);
#pragma endregion //Door Functions

// Value-Reading Functions
#pragma region Value-Reading Functions
/**
 * @brief Reads the hardware override switch and reports back to firebase whenever
 * a change has occured.
 * 
 */
void read_hardware_override();

/**
 * @brief Currently only reads the light level. The opening/closing values are now handled via the
 * synchronous functions open_door() and close_door()!
 * 
 */
void read_door_values();

bool is_force_close_enabled();  // Helper Function
bool is_force_open_enabled();   // Helper Function
bool is_door_open();            // Helper Function
bool is_door_closed();          // Helper Function
#pragma endregion //Value-Reading Functions

// Debug Functions
#pragma region Debug Functions
/**
 * @brief Only prints messages to the terminal if DEBUG_PRINTS
 * is defined and @link message isn't the last debug string printed to
 * the terminal.
 * 
 * @param message The unique debug string to print to terminal
 */
void debug_print(String message);

/**
 * @brief Pings the server with the time the arduino device has
 * been alive.
 * 
 */
void debug_ping();

#pragma endregion //Debug Functions

#pragma endregion // FORWARD DECLARATIONS

#define MAX_OPERATION_TIME 5000 // 5 second timeout for door operations

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
  static const String PATH_BASE = "systems/kitty_door/";
  static const String PATH_STATUS_DOOR = "status/door_state";
  static const String PATH_STATUS_HW_OVERRIDE = "status/hw_Override";
  static const String PATH_STATUS_LIGHT_LEVEL = "status/light_level";
  static const String PATH_STREAM = "controller";
  static const String PATH_DEBUG_PING = "debug/ping";
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

///////////////////////////////////
///// STRUCTS
//////////////////////////////////
#pragma region Structures
  typedef struct DoorStatus 
  {
    String lastAutoMode;
    String current;
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

///////////////////////////////////
///// DEBUG CONSTANTS
//////////////////////////////////
#pragma region Debug Constants
const unsigned long DEBUG_PING_INTERVAL = 1000 * 60 * 10; // Every 10 minutes
#pragma endregion // Debug Constants