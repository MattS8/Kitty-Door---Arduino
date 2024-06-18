#define DEBUG_PRINTS

#define HWO_DISABLED 0
#define HWO_FORCE_OPEN 1
#define HWO_FORCE_CLOSE 2
static const String NONE = "_none_";
#define COMMAND_OPEN "OPEN_DOOR"
#define COMMAND_CLOSE "CLOSE_DOOR"
#define COMMAND_READ_LIGHT_LEVEL "READ_LIGHT_LEVEL"
#define COMMAND_SET_TO_AUTO "SET_TO_AUTO"
#define COMMAND_CHECK_ALIVE "CHECK_ALIVE"

static const String STATE_OPEN = "OPEN";
static const String STATE_CLOSED = "CLOSED";
static const String STATE_OPENING = "OPENING";
static const String STATE_CLOSING = "CLOSING";
#define BUFFER_TIME 1000

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
///// FORWARD DECLARATIONS
//////////////////////////////////
#pragma region FORWARD DECLARATIONS
// Setup Functions
void setPinModes();
void setupFirebase();
void setDefaultDoorOptions();
void initializeDoor();

// Send Functions
void sendLightLevel();

// Value Reading Functions
void readHardwareOverride();
void readLightLevel();
void readDoorSensors();
bool isHwForceCloseEnabled(); // Helper Function
bool isHwForceOpenEnabled();  // Helper Function
bool isDoorOpen();            // Helper Function
bool isDoorClosed();          // Helper Function

// Callback Handlers
void handleCommand();

// Debug
void debugKeepAlive();

#pragma endregion

///////////////////////////////////
///// STRUCTS
//////////////////////////////////
#pragma region Structures
typedef struct DoorState
{
  String previous;
  String current;
} DoorState;

typedef struct AutoModeState
{
  bool previous;
  bool current;
} AutoModeState;

typedef struct HwOverrideState
{
  int previous;
  int current;
} HwOverrideState;

typedef struct KittyDoorValues
{
  int lightLevel;
  int upSense;
  int downSense;
  int hwForceOpen;
  int hwForceClose;
  long delayClosing;
  long delayOpening;
  int openLightLevel;
  int closeLightLevel;
  unsigned long autoModeBuffer;
} KittyDoorValues;
#pragma endregion

#ifdef DEBUG_PRINTS
static const String debugLightLevel1 = " (open/close: ";
static const String debugLightLevel2 = ", ";
static const String debugLightLevel3 = ")";
static const String debugNotTriggered = "NOT TRIGGERED";
static const String debugTriggered = "TRIGGERED";
static const String debugEnabled = "ENABLED";
static const String debugDisabled = "DISABLED";
static const String debugForceClose = "FORCE_CLOSE";
static const String debugForceOpen = "FORCE_OPEN";
static const String debugDoorVars = "____DOOR VARIABLES____";
static const String debugHwOverride = "\n\tHwOverride: ";
static const String debugAutoMode = "\n\tAutoMode: ";
static const String debugAutoModeBuffer = "\n\tAutoModeBufferTime: ";
static const String debugOpenSensor = "\n\tOpenSensor: ";
static const String debugClosedSensor = "\n\tClosedSensor: ";
static const String debugDoorStateCur = "\n\tDoorState (Current): ";
static const String debugDoorStatePrev = "\n\tDoorState (previous): ";
static const String debugLightLevel = "\n\tLightLevel: ";
#endif