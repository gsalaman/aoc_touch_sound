/*
 * aoc_touch_sound.ino
 *
 * Four doors, each with a QT1010 touch panel behind it. Touching a panel
 * while its door is open starts that door's sound looping on a YX5300 MP3
 * module. With several doors open, the most recently touched sound plays;
 * when its door closes, the previously touched (still open) door's sound
 * plays again. When no touched door is open, the player is stopped.
 *
 * Target:  Arduino Uno (Tools > Board > Arduino AVR Boards > Arduino Uno).
 *          Debug messages go out the USB serial port at 115200 baud
 *          (Tools > Serial Monitor, set to 115200).
 *
 * Wiring:
 *   D0, D1    Reserved for USB serial debug (Serial).
 *   D2        Serial RX  <- YX5300 TX (optional; not used by this sketch)
 *   D3        Serial TX  -> YX5300 RX
 *   D4..D7    Door switches A..D to GND (internal pull-ups).
 *             Switch closed (LOW) = door closed, open (HIGH) = door open.
 *   D8..D11   QT1010 OUT pins A..D (active HIGH while touched).
 *
 * SD card: put the sounds in folder 01 on the card, named 001xxx.mp3
 * (door A), 002xxx.mp3 (door B), 003xxx.mp3 (door C), 004xxx.mp3 (door D).
 */

#include <SoftwareSerial.h>

const uint8_t NUM_DOORS = 4;

const uint8_t DOOR_PINS[NUM_DOORS]  = { 4, 5, 6, 7 };
const uint8_t TOUCH_PINS[NUM_DOORS] = { 8, 9, 10, 11 };

const uint8_t YX_RX_PIN = 2;
const uint8_t YX_TX_PIN = 3;

const char DOOR_NAMES[NUM_DOORS] = { 'A', 'B', 'C', 'D' };

/* YX5300 command codes */
const uint8_t YX_CMD_PLAY_FOLDER_FILE = 0x0F;
const uint8_t YX_CMD_SINGLE_CYCLE     = 0x19;
const uint8_t YX_CMD_SELECT_DEVICE    = 0x09;
const uint8_t YX_CMD_STOP             = 0x16;
const uint8_t YX_DEVICE_TF_CARD       = 0x02;
const uint8_t YX_SOUND_FOLDER         = 0x01;

const unsigned long DEBUG_BAUD       = 115200;
const unsigned long YX_BAUD          = 9600;
const unsigned long LOOP_DELAY_MS    = 20;    /* also serves as the debounce time */
const unsigned long YX_BOOT_DELAY_MS = 500;

SoftwareSerial yxSerial(YX_RX_PIN, YX_TX_PIN);

/* Doors whose sounds have been touched, oldest first; the last entry is playing. */
uint8_t playStack[NUM_DOORS];
uint8_t playDepth = 0;

uint8_t prevTouchMask = 0;
uint8_t prevOpenMask  = 0;


/*
 * yxSendCommand
 *
 * Sends one 8-byte command frame to the YX5300:
 *   7E FF 06 <cmd> 00 <dataHigh> <dataLow> EF
 *
 * cmd       - YX5300 command code
 * dataHigh  - high byte of the command argument
 * dataLow   - low byte of the command argument
 */
void yxSendCommand(uint8_t cmd, uint8_t dataHigh, uint8_t dataLow)
{
  const uint8_t frame[8] = { 0x7E, 0xFF, 0x06, cmd, 0x00, dataHigh, dataLow, 0xEF };

  yxSerial.write(frame, sizeof(frame));
  delay(20);   /* give the module time to process the command */
}


/*
 * playSoundLooped
 *
 * Starts the given door's sound and sets the YX5300 to repeat it until
 * told otherwise.
 *
 * door  - door index 0..NUM_DOORS-1 (plays file number door + 1)
 */
void playSoundLooped(uint8_t door)
{
  Serial.print(F("Playing sound "));
  Serial.println(DOOR_NAMES[door]);

  yxSendCommand(YX_CMD_PLAY_FOLDER_FILE, YX_SOUND_FOLDER, door + 1);
  yxSendCommand(YX_CMD_SINGLE_CYCLE, 0x00, 0x00);   /* 0x00 = repeat current track */
}


/*
 * stopSound
 *
 * Stops playback on the YX5300.
 */
void stopSound()
{
  Serial.println(F("Stopping sound"));

  yxSendCommand(YX_CMD_STOP, 0x00, 0x00);
}


/*
 * removeFromStack
 *
 * Removes a door from the play stack if it is present, keeping the order
 * of the remaining entries.
 *
 * door  - door index to remove
 */
void removeFromStack(uint8_t door)
{
  uint8_t writeIndex = 0;
  uint8_t readIndex;

  for (readIndex = 0; readIndex < playDepth; readIndex++)
  {
    if (playStack[readIndex] != door)
    {
      playStack[writeIndex] = playStack[readIndex];
      writeIndex++;
    }
  }
  playDepth = writeIndex;
}


/*
 * readOpenDoors
 *
 * Reads all door switches.
 *
 * Returns a bit mask with bit n set when door n is open.
 */
uint8_t readOpenDoors()
{
  uint8_t mask = 0;
  uint8_t door;

  for (door = 0; door < NUM_DOORS; door++)
  {
    if (digitalRead(DOOR_PINS[door]) == HIGH)
    {
      mask |= (1 << door);
    }
  }
  return mask;
}


/*
 * readTouches
 *
 * Reads all QT1010 outputs.
 *
 * Returns a bit mask with bit n set when panel n is being touched.
 */
uint8_t readTouches()
{
  uint8_t mask = 0;
  uint8_t door;

  for (door = 0; door < NUM_DOORS; door++)
  {
    if (digitalRead(TOUCH_PINS[door]) == HIGH)
    {
      mask |= (1 << door);
    }
  }
  return mask;
}


/*
 * handleDoorClosed
 *
 * Takes a closed door's sound out of the play stack. If that sound was the
 * one playing, falls back to the previously touched sound, or stops if
 * there is none.
 *
 * door  - index of the door that just closed
 */
void handleDoorClosed(uint8_t door)
{
  bool wasPlaying = (playDepth > 0) && (playStack[playDepth - 1] == door);

  Serial.print(F("Door "));
  Serial.print(DOOR_NAMES[door]);
  Serial.println(F(" closed"));

  removeFromStack(door);

  if (wasPlaying)
  {
    if (playDepth > 0)
    {
      playSoundLooped(playStack[playDepth - 1]);
    }
    else
    {
      stopSound();
    }
  }
}


/*
 * handleDoorOpened
 *
 * Reports a door opening on the debug port. Opening a door does not start
 * a sound by itself; the panel behind it has to be touched.
 *
 * door  - index of the door that just opened
 */
void handleDoorOpened(uint8_t door)
{
  Serial.print(F("Door "));
  Serial.print(DOOR_NAMES[door]);
  Serial.println(F(" opened"));
}


/*
 * handlePanelTouched
 *
 * Moves a touched door's sound to the top of the play stack and plays it.
 * If that door's sound is already the one playing, does nothing, so the
 * sound carries on instead of restarting.
 *
 * door  - index of the panel that was just touched (its door is open)
 */
void handlePanelTouched(uint8_t door)
{
  bool alreadyPlaying = (playDepth > 0) && (playStack[playDepth - 1] == door);

  Serial.print(F("Panel "));
  Serial.print(DOOR_NAMES[door]);
  Serial.println(F(" touched"));

  if (alreadyPlaying)
  {
    Serial.println(F("Already playing; ignored"));
    return;
  }

  /*
   * If this door was touched before, take it out of the stack first (the
   * other entries keep their order). A door never appears twice; touching
   * it again just moves it to the top. This also means the stack can never
   * hold more than NUM_DOORS entries.
   */
  removeFromStack(door);

  /*
   * Push the door onto the top of the stack. playStack lists touched doors
   * oldest to newest, playDepth is how many entries are in use, and the
   * last entry is the sound that is playing.
   */
  playStack[playDepth] = door;
  playDepth++;

  playSoundLooped(door);
}


/*
 * setup
 *
 * Configures the pins, starts the debug and YX5300 serial links, and gets
 * the YX5300 ready, reading from the SD card with nothing playing.
 */
void setup()
{
  uint8_t door;

  for (door = 0; door < NUM_DOORS; door++)
  {
    pinMode(DOOR_PINS[door], INPUT_PULLUP);
    pinMode(TOUCH_PINS[door], INPUT);
  }

  Serial.begin(DEBUG_BAUD);
  Serial.println(F("aoc_touch_sound starting"));

  yxSerial.begin(YX_BAUD);
  delay(YX_BOOT_DELAY_MS);   /* let the YX5300 power up */

  yxSendCommand(YX_CMD_SELECT_DEVICE, 0x00, YX_DEVICE_TF_CARD);
  delay(200);
  stopSound();

  prevOpenMask = readOpenDoors();
}


/*
 * loop
 *
 * Polls the doors and touch panels, reacting to doors that have just
 * opened or closed and panels that have just been touched (rising edge)
 * while their door is open.
 */
void loop()
{
  uint8_t openMask    = readOpenDoors();
  uint8_t touchMask   = readTouches();
  uint8_t openedMask  = openMask & ~prevOpenMask;
  uint8_t closedMask  = prevOpenMask & ~openMask;
  uint8_t pressedMask = touchMask & ~prevTouchMask & openMask;
  uint8_t door;
  uint8_t bit;

  for (door = 0; door < NUM_DOORS; door++)
  {
    bit = (1 << door);

    if (openedMask & bit)
    {
      handleDoorOpened(door);
    }
    if (closedMask & bit)
    {
      handleDoorClosed(door);
    }
    if (pressedMask & bit)
    {
      handlePanelTouched(door);
    }
  }

  prevOpenMask  = openMask;
  prevTouchMask = touchMask;
  delay(LOOP_DELAY_MS);
}
