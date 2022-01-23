/**
  ESP32 state monitor / controller firmware for the Open eXtensible Rack System
  
  See https://oxrs.io/docs/firmware/state-io-esp32.html for documentation.
  
  Compile options:
    ESP32

  External dependencies. Install using the Arduino library manager:
    "Adafruit_MCP23017"
    "OXRS-SHA-Rack32-ESP32-LIB" by SuperHouse Automation Pty
    "OXRS-SHA-IOHandler-ESP32-LIB" by SuperHouse Automation Pty

  Compatible with the Light Switch Controller hardware found here:
    www.superhouse.tv/lightswitch
    
  Compatible with the multi-channel relay driver hardware found here:
    https://www.superhouse.tv/product/8-channel-relay-driver-shield/
    https://bmdesigns.com.au/shop/relay16-16-channel-relay-driver/

  GitHub repository:
    https://github.com/SuperHouse/OXRS-SHA-StateIO-ESP32-FW

  Bugs/Features:
    See GitHub issues list
  
  Copyright 2019-2022 SuperHouse Automation Pty Ltd
*/


/*--------------------------- Firmware -----------------------------------*/
#define FW_NAME       "OXRS-SHA-StateIO-ESP32-FW"
#define FW_SHORT_NAME "State I/O"
#define FW_MAKER      "SuperHouse Automation"
#define FW_VERSION    "0.0.5"

/*--------------------------- Libraries ----------------------------------*/
#include <Adafruit_MCP23X17.h>        // For MCP23017 I/O buffers
#include <OXRS_Rack32.h>              // Rack32 support
#include <OXRS_Input.h>               // For input handling
#include <OXRS_Output.h>              // For output handling
#include "logo.h"                     // Embedded maker logo

/*--------------------------- Constants ----------------------------------*/
// Serial
#define       SERIAL_BAUD_RATE      115200

// Can have up to 8x MCP23017s on a single I2C bus
const byte    MCP_I2C_ADDRESS[]     = { 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27 };
const uint8_t MCP_COUNT             = sizeof(MCP_I2C_ADDRESS);

// Each MCP23017 has 16 I/O pins
#define       MCP_PIN_COUNT         16

// Set false for breakout boards with external pull-ups
#define       MCP_INTERNAL_PULLUPS  true

// Speed up the I2C bus to get faster event handling
#define       I2C_CLOCK_SPEED       400000L

/*--------------------------- Global Variables ---------------------------*/
// Each bit corresponds to an MCP found on the IC2 bus
uint8_t g_mcps_found = 0;

// How many pins on each MCP are we controlling (defaults to all 16)
// Set via "outputsPerMcp" integer config option - should be set via
// the REST API so it is persisted to SPIFFS and loaded early enough
// in the boot sequence to configure the LCD and adoption payloads
uint8_t g_mcp_output_pins = MCP_PIN_COUNT;

// This is the only variable that needs to be set to define the partitions for INP and OUTP ports
// Set via "ioConfig" enum (1 out of 5) config option - should be set via
// the REST API so it is persisted to SPIFFS and loaded early enough
// in the boot sequence to configure the MCP io-expander, LCD and adoption payloads
//
// I2C_adr [0 to (mcp_output_start-1)] = IN; 
// I2C_adr [mcp_output_start to 7] = OUT
// for mcp_output_start only 0, 2, 4, 6 and 8 are supported  
// *  0 -> 0 INP / 8 OUTP ; PORT_LAYOUT_OUTPUT_AUTO (output only)
// *  2 -> 2 INP / 6 OUTP ; PORT_LAYOUT_IO_32_96
// *  4 -> 4 INP / 4 OUTP ; PORT_LAYOUT_IO_64_64
// *  6 -> 6 INP / 2 OUTP ; PORT_LAYOUT_IO_96_32
// *  8 -> 8 INP / 0 OUTP ; PORT_LAYOUT_INPUT_AUTO  (input only)
uint8_t g_mcp_output_start = MCP_COUNT;

/*--------------------------- Global Objects -----------------------------*/
// Rack32 handler
OXRS_Rack32 rack32(FW_NAME, FW_SHORT_NAME, FW_MAKER, FW_VERSION, FW_LOGO);

// I/O buffers
Adafruit_MCP23X17 mcp23017[MCP_COUNT];

// Input handler
OXRS_Input oxrsInput[MCP_COUNT];

// Output handlers
OXRS_Output oxrsOutput[MCP_COUNT];

/*--------------------------- Program ------------------------------------*/
/**
  Setup
*/
void setup()
{
  // Startup logging to serial
  Serial.begin(SERIAL_BAUD_RATE);
  Serial.println();
  Serial.println(F("========================================"));
  Serial.print  (F("FIRMWARE: ")); Serial.println(FW_NAME);
  Serial.print  (F("MAKER:    ")); Serial.println(FW_MAKER);
  Serial.print  (F("VERSION:  ")); Serial.println(FW_VERSION);
  Serial.println(F("========================================"));
  
  // Start the I2C bus
  Wire.begin();

  // Scan the I2C bus
  scanI2CBus();

  // Start Rack32 hardware
  rack32.begin(jsonConfig, jsonCommand);

  // set up I2C-I/O buffers
  configureI2CBus();
  
  // Speed up I2C clock for faster scan rate (after bus scan)
  Wire.setClock(I2C_CLOCK_SPEED);

  // Set up port display (depends on g_mcp_output_start)
  bool err_output_start = false;
  if (g_mcp_output_pins == 8)
  {
    switch (g_mcp_output_start) 
    {
      case 0: rack32.setDisplayPorts(g_mcps_found, PORT_LAYOUT_OUTPUT_AUTO_8);  break;
      case 2: rack32.setDisplayPorts(g_mcps_found, PORT_LAYOUT_IO_32_96_8);     break;
      case 4: rack32.setDisplayPorts(g_mcps_found, PORT_LAYOUT_IO_64_64_8);     break;
      case 6: rack32.setDisplayPorts(g_mcps_found, PORT_LAYOUT_IO_96_32_8);     break;
      case 8: rack32.setDisplayPorts(g_mcps_found, PORT_LAYOUT_INPUT_AUTO);     break;
      default: err_output_start = true;
    }
  }
  else
  {
    switch (g_mcp_output_start) 
    {
      case 0: rack32.setDisplayPorts(g_mcps_found, PORT_LAYOUT_OUTPUT_AUTO);  break;
      case 2: rack32.setDisplayPorts(g_mcps_found, PORT_LAYOUT_IO_32_96);     break;
      case 4: rack32.setDisplayPorts(g_mcps_found, PORT_LAYOUT_IO_64_64);     break;
      case 6: rack32.setDisplayPorts(g_mcps_found, PORT_LAYOUT_IO_96_32);     break;
      case 8: rack32.setDisplayPorts(g_mcps_found, PORT_LAYOUT_INPUT_AUTO);   break;
      default: err_output_start = true;
    }
  }
  if (err_output_start)
  {
    Serial.print(F("[stio] invalid g_mcp_output_start: "));
    Serial.println(g_mcp_output_start);
  }

  // Set up config/command schema (for self-discovery and adoption)
  setConfigSchema();
  setCommandSchema();
}

/**
  Main processing loop
*/
void loop()
{
  // Iterate through each of the MCP23017s
  for (uint8_t mcp = 0; mcp < MCP_COUNT; mcp++)
  {
    if (bitRead(g_mcps_found, mcp) == 0)
      continue;

    // Check for any output events
    if (isOutputMcp(mcp))
    {
      oxrsOutput[mcp].process();
    }

     // Read the values for all 16 pins on this MCP
    uint16_t io_value = mcp23017[mcp].readGPIOAB();

    // Show port animations
    rack32.updateDisplayPorts(mcp, io_value);

    // Check for any input events
    if (isInputMcp(mcp))
    {
      // Check for any input events
      oxrsInput[mcp].process(mcp, io_value);
    }
  }

  // Let Rack32 hardware handle any events etc
  rack32.loop();
}

/**
  Config handler
 */
void setConfigSchema()
{
  // Define our config schema
  StaticJsonDocument<2048> json;
  JsonVariant config = json.as<JsonVariant>();
  
  JsonObject ioConfig = json.createNestedObject("ioConfig");
  ioConfig["type"] = "string";
  ioConfig["description"] = "Hint ! Restart your device when changed to make effective!";
  JsonArray ioConfigEnum = ioConfig.createNestedArray("enum");
  ioConfigEnum.add("io_128_0");
  ioConfigEnum.add("io_96_32");
  ioConfigEnum.add("io_64_64");
  ioConfigEnum.add("io_32_96");
  ioConfigEnum.add("io_0_128");

  JsonObject outputsPerMcp = json.createNestedObject("outputsPerMcp");
  outputsPerMcp["type"] = "integer";
  outputsPerMcp["description"] = "Hint ! Restart your device when changed to make effective!";
  outputsPerMcp["minimum"] = 8;
  outputsPerMcp["maximum"] = MCP_PIN_COUNT;
  outputsPerMcp["multipleOf"] = 8;

  // Do we have any input MCPs?
  if (isInputMcp(0))
  {
    inputConfigSchema(config);
  }
  
  // Do we have any output MCPs?
  if (isOutputMcp(MCP_COUNT - 1))
  {
    outputConfigSchema(config);
  }

  // Pass our config schema down to the Rack32 library
  rack32.setConfigSchema(config);
}

void inputConfigSchema(JsonVariant json)
{
  JsonObject inputs = json.createNestedObject("inputs");
  inputs["type"] = "array";
  
  JsonObject items = inputs.createNestedObject("items");
  items["type"] = "object";

  JsonObject properties = items.createNestedObject("properties");

  JsonObject index = properties.createNestedObject("index");
  index["type"] = "integer";
  index["minimum"] = getMinInputIndex();;
  index["maximum"] = getMaxInputIndex();

  JsonObject type = properties.createNestedObject("type");
  JsonArray typeEnum = type.createNestedArray("enum");
  typeEnum.add("button");
  typeEnum.add("contact");
  typeEnum.add("rotary");
  typeEnum.add("switch");
  typeEnum.add("toggle");

  JsonObject invert = properties.createNestedObject("invert");
  invert["type"] = "boolean";

  JsonArray required = items.createNestedArray("required");
  required.add("index"); 
}

void outputConfigSchema(JsonVariant json)
{
  JsonObject outputs = json.createNestedObject("outputs");
  outputs["type"] = "array";
  
  JsonObject items = outputs.createNestedObject("items");
  items["type"] = "object";

  JsonObject properties = items.createNestedObject("properties");

  JsonObject index = properties.createNestedObject("index");
  index["type"] = "integer";
  index["minimum"] = getMinOutputIndex();;
  index["maximum"] = getMaxOutputIndex();

  JsonObject type = properties.createNestedObject("type");
  JsonArray typeEnum = type.createNestedArray("enum");
  typeEnum.add("relay");
  typeEnum.add("motor");
  typeEnum.add("timer");

  JsonObject timerSeconds = properties.createNestedObject("timerSeconds");
  timerSeconds["type"] = "integer";
  timerSeconds["minimum"] = 1;

  JsonObject interlockIndex = properties.createNestedObject("interlockIndex");
  interlockIndex["type"] = "integer";
  interlockIndex["minimum"] = getMinOutputIndex();
  interlockIndex["maximum"] = getMaxOutputIndex();

  JsonArray required = items.createNestedArray("required");
  required.add("index");
}

void jsonConfig(JsonVariant json)
{
  if (json.containsKey("ioConfig"))
  {
    jsonIoConfig(json["ioConfig"]);
  }
  
  if (json.containsKey("outputsPerMcp"))
  {
    g_mcp_output_pins = json["outputsPerMcp"].as<uint8_t>();
  }

  if (json.containsKey("inputs"))
  {
    for (JsonVariant input : json["inputs"].as<JsonArray>())
    {
      jsonInputConfig(input);    
    }
  }

  if (json.containsKey("outputs"))
  {
    for (JsonVariant output : json["outputs"].as<JsonArray>())
    {
      jsonOutputConfig(output);
    }
  }  
}

void jsonIoConfig(const char* ioConfig)
{
  if (strcmp(ioConfig, "io_128_0") == 0)
  {
    g_mcp_output_start = 8;
  }
  else if (strcmp(ioConfig, "io_96_32") == 0)
  {
    g_mcp_output_start = 6;
  }
  else if (strcmp(ioConfig, "io_64_64") == 0)
  {
    g_mcp_output_start = 4;
  }
  else if (strcmp(ioConfig, "io_32_96") == 0)
  {
    g_mcp_output_start = 2;
  }
  else if (strcmp(ioConfig, "io_0_128") == 0)
  {
    g_mcp_output_start = 0;
  }
  else
  {
    Serial.println(F("[stio] invalid ioConfig enum"));
  }
}

void jsonInputConfig(JsonVariant json)
{
  uint8_t index = getInputIndex(json);
  if (index == 0) return;

  // Work out the MCP and pin we are configuring
  int mcp = (index - 1) / MCP_PIN_COUNT;
  int pin = (index - 1) % MCP_PIN_COUNT;
  
  if (json.containsKey("type"))
  {
    if (strcmp(json["type"], "button") == 0)
    {
      oxrsInput[mcp].setType(pin, BUTTON);
    }
    else if (strcmp(json["type"], "contact") == 0)
    {
      oxrsInput[mcp].setType(pin, CONTACT);
    }
    else if (strcmp(json["type"], "rotary") == 0)
    {
      oxrsInput[mcp].setType(pin, ROTARY);
    }
    else if (strcmp(json["type"], "switch") == 0)
    {
      oxrsInput[mcp].setType(pin, SWITCH);
    }
    else if (strcmp(json["type"], "toggle") == 0)
    {
      oxrsInput[mcp].setType(pin, TOGGLE);
    }
    else 
    {
      Serial.println(F("[stio] invalid input type"));
    }
  }
  
  if (json.containsKey("invert"))
  {
    oxrsInput[mcp].setInvert(pin, json["invert"].as<bool>());
  }
}

void jsonOutputConfig(JsonVariant json)
{
  uint8_t index = getOutputIndex(json);
  if (index == 0) return;

  // Work out the MCP and pin we are configuring
  uint8_t mcp = outpIndex2Mcp(index);
  uint8_t pin = outpIndex2Pin(index);
  
  if (json.containsKey("type"))
  {
    if (strcmp(json["type"], "motor") == 0)
    {
      oxrsOutput[mcp].setType(pin, MOTOR);
    }
    else if (strcmp(json["type"], "relay") == 0)
    {
      oxrsOutput[mcp].setType(pin, RELAY);
    }
    else if (strcmp(json["type"], "timer") == 0)
    {
      oxrsOutput[mcp].setType(pin, TIMER);
    }
    else 
    {
      Serial.println(F("[stio] invalid output type"));
    }
  }
  
  if (json.containsKey("timerSeconds"))
  {
    if (json["timerSeconds"].isNull())
    {
      oxrsOutput[mcp].setTimer(pin, DEFAULT_TIMER_SECS);
    }
    else
    {
      oxrsOutput[mcp].setTimer(pin, json["timerSeconds"].as<int>());
    }
  }
  
  if (json.containsKey("interlockIndex"))
  {
    // If an empty message then treat as 'unlocked' - i.e. interlock with ourselves
    if (json["interlockIndex"].isNull())
    {
      oxrsOutput[mcp].setInterlock(pin, pin);
    }
    else
    {
      uint8_t interlock_index = json["interlockIndex"].as<uint8_t>();
     
      uint8_t interlock_mcp = outpIndex2Mcp(interlock_index);
      uint8_t interlock_pin = outpIndex2Pin(interlock_index);
      
      if (interlock_mcp == mcp)
      {
        oxrsOutput[mcp].setInterlock(pin, interlock_pin);
      }
      else
      {
        Serial.println(F("[stio] lock must be with pin on same mcp"));
      }
    }
  }
}

/**
  Command handler
 */
void setCommandSchema()
{
  // Define our config schema
  StaticJsonDocument<2048> json;
  JsonVariant command = json.as<JsonVariant>();

  // Do we have any output MCPs?
  if (isOutputMcp(MCP_COUNT - 1))
  {
    outputCommandSchema(command);
  }

  // Pass our command schema down to the Rack32 library
  rack32.setCommandSchema(command);
}

void outputCommandSchema(JsonVariant json)
{
  JsonObject outputs = json.createNestedObject("outputs");
  outputs["type"] = "array";
  
  JsonObject items = outputs.createNestedObject("items");
  items["type"] = "object";

  JsonObject properties = items.createNestedObject("properties");

  JsonObject index = properties.createNestedObject("index");
  index["type"] = "integer";
  index["minimum"] = getMinOutputIndex();
  index["maximum"] = getMaxOutputIndex();

  JsonObject type = properties.createNestedObject("type");
  JsonArray typeEnum = type.createNestedArray("enum");
  typeEnum.add("relay");
  typeEnum.add("motor");
  typeEnum.add("timer");

  JsonObject command = properties.createNestedObject("command");
  command["type"] = "string";
  JsonArray commandEnum = command.createNestedArray("enum");
  commandEnum.add("query");
  commandEnum.add("on");
  commandEnum.add("off");

  JsonArray required = items.createNestedArray("required");
  required.add("index");
  required.add("command");
}

void jsonCommand(JsonVariant json)
{
  if (json.containsKey("outputs"))
  {
    for (JsonVariant output : json["outputs"].as<JsonArray>())
    {
      jsonOutputCommand(output);
    }
  }
}

void jsonOutputCommand(JsonVariant json)
{
  uint8_t index = getOutputIndex(json);
  if (index == 0) return;

  // Work out the MCP and pin we are processing
  uint8_t mcp = outpIndex2Mcp(index);
  uint8_t pin = outpIndex2Pin(index);
  
  // Get the output type for this pin
  uint8_t type = oxrsOutput[mcp].getType(pin);
  
  if (json.containsKey("type"))
  {
    if ((strcmp(json["type"], "relay") == 0 && type != RELAY) ||
        (strcmp(json["type"], "motor") == 0 && type != MOTOR) ||
        (strcmp(json["type"], "timer") == 0 && type != TIMER))
    {
      Serial.println(F("[stio] command type doesn't match configured type"));
      return;
    }
  }
  
  if (json.containsKey("command"))
  {
    if (json["command"].isNull() || strcmp(json["command"], "query") == 0)
    {
      // Publish a status event with the current state
      uint8_t state = mcp23017[mcp].digitalRead(pin);
      publishOutputEvent(index, type, state);
    }
    else
    {
      // Send this command down to our output handler to process
      if (strcmp(json["command"], "on") == 0)
      {
        oxrsOutput[mcp].handleCommand(mcp, pin, RELAY_ON);
      }
      else if (strcmp(json["command"], "off") == 0)
      {
        oxrsOutput[mcp].handleCommand(mcp, pin, RELAY_OFF);
      }
      else 
      {
        Serial.println(F("[stio] invalid command"));
      }
    }
  }
}

/**
 * Helper functions
 */
bool isInputMcp(uint8_t mcp)
{
  return mcp < g_mcp_output_start;  
}

bool isOutputMcp(uint8_t mcp)
{
  return !isInputMcp(mcp);  
}

uint8_t getMinInputIndex()
{
  // Remember our indexes are 1-based
  return 1; 
}

uint8_t getMaxInputIndex()
{
  // Remember our indexes are 1-based
  // Search for highest input MCP found
  for (int i = g_mcp_output_start - 1; i >= 0; i--)
  {
    if (bitRead(g_mcps_found, i))
    {
        return ((i + 1) * MCP_PIN_COUNT);
    }
  } 
  // No input MCP found
  return getMinInputIndex(); 
}

uint8_t getMinOutputIndex()
{
  // Remember our indexes are 1-based
  return g_mcp_output_start * MCP_PIN_COUNT + 1; 
}

uint8_t getMaxOutputIndex()
{
  // Search for highest MCP found
  for (int i = 7; i >= g_mcp_output_start; i--)
  {
    if (bitRead(g_mcps_found, i))
    {
        return ((i + 1 - g_mcp_output_start) * g_mcp_output_pins + getMinOutputIndex() - 1);
    }
  }
  // No output MCP found
  return getMinOutputIndex(); 
}

uint8_t outpIndex2Mcp(int index)
{
  return ((index - getMinOutputIndex()) / g_mcp_output_pins + g_mcp_output_start);
}

uint8_t outpIndex2Pin(int index)
{
  return ((index - getMinOutputIndex()) % g_mcp_output_pins);
}



uint8_t getInputIndex(JsonVariant json)
{
  if (!json.containsKey("index"))
  {
    Serial.println(F("[stio] missing input index"));
    return 0;
  }
  
  uint8_t index = json["index"].as<uint8_t>();

  // Check the index is valid for this device
  if (index < getMinInputIndex() || index > getMaxInputIndex())
  {
    Serial.println(F("[stio] invalid input index"));
    return 0;
  }

  return index;
}

uint8_t getOutputIndex(JsonVariant json)
{
  if (!json.containsKey("index"))
  {
    Serial.println(F("[stio] missing output index"));
    return 0;
  }
  
  uint8_t index = json["index"].as<uint8_t>();

  // Check the index is valid for this device
  if (index < getMinOutputIndex() || index > getMaxOutputIndex())
  {
    Serial.println(F("[stio] invalid output index"));
    return 0;
  }

  return index;
}

void publishInputEvent(uint8_t index, uint8_t type, uint8_t state)
{
  char inputType[8];
  getInputType(inputType, type);
  char eventType[7];
  getInputEventType(eventType, type, state);

  StaticJsonDocument<64> json;
  json["index"] = index;
  json["type"] = inputType;
  json["event"] = eventType;

  if (!rack32.publishStatus(json.as<JsonVariant>()))
  {
    Serial.print(F("[stio] [failover] "));
    serializeJson(json, Serial);
    Serial.println();

    // TODO: add failover handling code here
  }
}

void publishOutputEvent(uint8_t index, uint8_t type, uint8_t state)
{
  char outputType[8];
  getOutputType(outputType, type);
  char eventType[7];
  getOutputEventType(eventType, type, state);

  StaticJsonDocument<64> json;
  json["index"] = index;
  json["type"] = outputType;
  json["event"] = eventType;
  
  if (!rack32.publishStatus(json.as<JsonVariant>()))
  {
    Serial.print(F("[stio] [failover] "));
    serializeJson(json, Serial);
    Serial.println();

    // TODO: add failover handling code here
  }
}

void getInputType(char inputType[], uint8_t type)
{
  // Determine what type of input we have
  sprintf_P(inputType, PSTR("error"));
  switch (type)
  {
    case BUTTON:
      sprintf_P(inputType, PSTR("button"));
      break;
    case CONTACT:
      sprintf_P(inputType, PSTR("contact"));
      break;
    case ROTARY:
      sprintf_P(inputType, PSTR("rotary"));
      break;
    case SWITCH:
      sprintf_P(inputType, PSTR("switch"));
      break;
    case TOGGLE:
      sprintf_P(inputType, PSTR("toggle"));
      break;
  }
}
void getInputEventType(char eventType[], uint8_t type, uint8_t state)
{
  // Determine what event we need to publish
  sprintf_P(eventType, PSTR("error"));
  switch (type)
  {
    case BUTTON:
      switch (state)
      {
        case HOLD_EVENT:
          sprintf_P(eventType, PSTR("hold"));
          break;
        case 1:
          sprintf_P(eventType, PSTR("single"));
          break;
        case 2:
          sprintf_P(eventType, PSTR("double"));
          break;
        case 3:
          sprintf_P(eventType, PSTR("triple"));
          break;
        case 4:
          sprintf_P(eventType, PSTR("quad"));
          break;
        case 5:
          sprintf_P(eventType, PSTR("penta"));
          break;
      }
      break;
    case CONTACT:
      switch (state)
      {
        case LOW_EVENT:
          sprintf_P(eventType, PSTR("closed"));
          break;
        case HIGH_EVENT:
          sprintf_P(eventType, PSTR("open"));
          break;
      }
      break;
    case ROTARY:
      switch (state)
      {
        case LOW_EVENT:
          sprintf_P(eventType, PSTR("up"));
          break;
        case HIGH_EVENT:
          sprintf_P(eventType, PSTR("down"));
          break;
      }
      break;
    case SWITCH:
      switch (state)
      {
        case LOW_EVENT:
          sprintf_P(eventType, PSTR("on"));
          break;
        case HIGH_EVENT:
          sprintf_P(eventType, PSTR("off"));
          break;
      }
      break;
    case TOGGLE:
      sprintf_P(eventType, PSTR("toggle"));
      break;
  }
}

void getOutputType(char outputType[], uint8_t type)
{
  // Determine what type of output we have
  sprintf_P(outputType, PSTR("error"));
  switch (type)
  {
    case MOTOR:
      sprintf_P(outputType, PSTR("motor"));
      break;
    case RELAY:
      sprintf_P(outputType, PSTR("relay"));
      break;
    case TIMER:
      sprintf_P(outputType, PSTR("timer"));
      break;
  }
}

void getOutputEventType(char eventType[], uint8_t type, uint8_t state)
{
  // Determine what event we need to publish
  sprintf_P(eventType, PSTR("error"));
  switch (state)
  {
    case RELAY_ON:
      sprintf_P(eventType, PSTR("on"));
      break;
    case RELAY_OFF:
      sprintf_P(eventType, PSTR("off"));
      break;
  }
}


/**
  Event handlers
*/
void inputEvent(uint8_t id, uint8_t input, uint8_t type, uint8_t state)
{
  // Determine the index for this input event (1-based)
  uint8_t mcp = id;
  uint8_t index = (MCP_PIN_COUNT * mcp) + input + 1;

  // Publish the event
  publishInputEvent(index, type, state);
}

void outputEvent(uint8_t id, uint8_t output, uint8_t type, uint8_t state)
{
  // Determine the index (1-based)
  uint8_t mcp = id;
  uint8_t pin = output;
  uint8_t raw_index = (mcp - g_mcp_output_start) * g_mcp_output_pins + getMinOutputIndex() - 1 + pin;
  uint8_t index = raw_index + 1;
  
  // Update the MCP pin - i.e. turn the relay on/off (LOW/HIGH)
  mcp23017[mcp].digitalWrite(pin, state);

  // Publish the event
  publishOutputEvent(index, type, state);
}

/**
  I2C
 */
void scanI2CBus()
{
  Serial.println(F("[stio] scanning for I/O buffers..."));

  for (uint8_t mcp = 0; mcp < MCP_COUNT; mcp++)
  {
    // Check if there is anything responding on this address
    Wire.beginTransmission(MCP_I2C_ADDRESS[mcp]);
    if (Wire.endTransmission() == 0)
    {
      bitWrite(g_mcps_found, mcp, 1);
    }
    
    // Initialise input handlers (default to TOGGLE)
    oxrsInput[mcp].begin(inputEvent, TOGGLE);
    
    // Initialise output handlers
    oxrsOutput[mcp].begin(outputEvent);
  }
}

void configureI2CBus()
{
  Serial.println(F("[stio] configuring I/O buffers..."));

  for (uint8_t mcp = 0; mcp < MCP_COUNT; mcp++)
  {
    Serial.print(F(" - 0x"));
    Serial.print(MCP_I2C_ADDRESS[mcp], HEX);
    Serial.print(F("..."));

    // Check if an MCP was found on this address
    if (bitRead(g_mcps_found, mcp))
    {
      // If an MCP23017 was found then initialise
      mcp23017[mcp].begin_I2C(MCP_I2C_ADDRESS[mcp]);

      if (isInputMcp(mcp))
      {     
        // Configure input devices
        for (uint8_t pin = 0; pin < MCP_PIN_COUNT; pin++)
        {
          mcp23017[mcp].pinMode(pin, MCP_INTERNAL_PULLUPS ? INPUT_PULLUP : INPUT);
        }
        Serial.print(F("MCP23017 [input]"));
        if (MCP_INTERNAL_PULLUPS) { Serial.print(F(" (internal pullups)")); }
        Serial.println();
      }
      else
      {
        // Configure output devices
        for (uint8_t pin = 0; pin < MCP_PIN_COUNT; pin++)
        {
          mcp23017[mcp].pinMode(pin, OUTPUT);
          mcp23017[mcp].digitalWrite(pin, RELAY_OFF);
        }
         Serial.println(F("MCP23017 [output]"));
      }
    }
    else
    {
      Serial.println(F("empty"));
    }
  }
}
