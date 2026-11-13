/*
 * aoc_touch_sound.ino
 *
 * Four doors, each with a QT1010 touch panel behind it. Touching a panel
 * while its door is open starts that door's sound looping on a YX5300 MP3
 * module. With several doors open, the most recently touched sound plays;
 * when its door closes, the previously touched (still open) door's sound
 * plays again. When no touched door is open, the player is stopped.
 *
 * Target:  ATtiny84 using ATTinyCore (Tools > Board > ATtinyCore >
 *          ATtiny24/44/84(a) (No bootloader), Chip: ATtiny84,
 *          Clock: 8 MHz (internal), Pin Mapping: Clockwise).
 *          Run "Burn Bootloader" once to set the 8 MHz fuse.
 *
 * Wiring:
 *   PA0..PA3  Door switches A..D to GND (internal pull-ups).
 *             Switch closed (LOW) = door closed, open (HIGH) = door open.
 *   PA4..PA7  QT1010 OUT pins A..D (active HIGH while touched).
 *   PB0       Serial TX  -> YX5300 RX
 *   PB1       Serial RX  <- YX5300 TX (optional; not used by this sketch)
 *
 * SD card: put the sounds in folder 01 on the card, named 001xxx.mp3
 * (door A), 002xxx.mp3 (door B), 003xxx.mp3 (door C), 004xxx.mp3 (door D).
 */

#include <SoftwareSerial.h>

const uint8_t NUM_DOORS = 4;

const uint8_t DOOR_PINS[NUM_DOORS]  = { PIN_PA0, PIN_PA1, PIN_PA2, PIN_PA3 };
const uint8_t TOUCH_PINS[NUM_DOORS] = { PIN_PA4, PIN_PA5, PIN_PA6, PIN_PA7 };

const uint8_t YX_RX_PIN = PIN_PB1;
const uint8_t YX_TX_PIN = PIN_PB0;

/* YX5300 command codes */
const uint8_t YX_CMD_PLAY_FOLDER_FILE = 0x0F;
const uint8_t YX_CMD_SINGLE_CYCLE     = 0x19;
const uint8_t YX_CMD_SELECT_DEVICE    = 0x09;
const uint8_t YX_CMD_STOP             = 0x16;
const uint8_t YX_DEVICE_TF_CARD       = 0x02;
const uint8_t YX_SOUND_FOLDER         = 0x01;

const unsigned long LOOP_DELAY_MS   = 20;   /* also serves as the debounce time */
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
 * handlePanelTouched
 *
 * Moves a touched door's sound to the top of the play stack and plays it.
 *
 * door  - index of the panel that was just touched (its door is open)
 */
void handlePanelTouched(uint8_t door)
{
  removeFromStack(door);
  playStack[playDepth] = door;
  playDepth++;
  playSoundLooped(door);
}


/*
 * setup
 *
 * Configures the pins, starts the serial link and gets the YX5300 ready,
 * reading from the SD card with nothing playing.
 */
void setup()
{
  uint8_t door;

  for (door = 0; door < NUM_DOORS; door++)
  {
    pinMode(DOOR_PINS[door], INPUT_PULLUP);
    pinMode(TOUCH_PINS[door], INPUT);
  }

  yxSerial.begin(9600);
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
 * closed and panels that have just been touched (rising edge) while their
 * door is open.
 */
void loop()
{
  uint8_t openMask    = readOpenDoors();
  uint8_t touchMask   = readTouches();
  uint8_t closedMask  = prevOpenMask & ~openMask;
  uint8_t pressedMask = touchMask & ~prevTouchMask & openMask;
  uint8_t door;
  uint8_t bit;

  for (door = 0; door < NUM_DOORS; door++)
  {
    bit = (1 << door);

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
