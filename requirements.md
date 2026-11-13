# Requirements:

##  Overview
There are 4 touch panels behind 4 doors.  Each touch panel is connected to micro-controller, and triggers a custom sound.  This sound continues to loop until the door is closed.

##  Touch Interface
We'll be using the QT1010 capacitive touch panel connected to a piece of copper tape embedded behind a sticker, inside of the door.

##  Sound Interface
We'll use the YX5300 as a sound controller.  This will be hooked up to the serial interface on our microcontroller.

## Door Interface
We need some way of determining if a given door is open or closed.  Potential ideas are a physical switch or a reed switch with an embedded magnet.

## Operation
If no door is open, the device is silent.

If one door is open, the sound file will start when the corresponding touch panel is pressed.  The sound file will stop when the door is closed.

If multiple doors are open, touching the various panels will trigger their sounds, but only the last sound touched will be playing.  When that door
is closed, it'll go back to playing the previous sound until that door is closed.  Same process until all doors are closed.

Example:

* Door A opened.  No sound.
* Door B opened.  No sound.
* Button B pressed.  Sound B starts to play.
* Button A pressed.  Sound A starts to play.
* Door C opened.  Sound A continues.
* Button C pressed.  Sound C starts.
* Door B closes.   Sound C continues.
* Door C closes.  Sound A now plays, We would play sound B here, but it's door is closed.
* Door A closes.  Sound A stops.
  
##  Power
This project will be powered via a standard wall-wart (110v)
