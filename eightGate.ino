//  MIDI_decoder for ATtiny84
//  eightGate by fuzzySynths
//  converts top row of Arturia Beatstep Pro MIDI notes to gate outputs, for a modular synth
//  also metronome output, producing gate signal on bar & beats

//  code adapted from https://arduino.stackexchange.com/questions/14054/interfacing-an-attiny85-with-midi-via-software-serial


// ATMEL ATTINY 25/45/85 / ARDUINO
// Pin 1 is /RESET
//
//                  +-\/-+
// Ain0 (D 5) PB5  1|    |8  Vcc
// Ain3 (D 3) PB3  2|    |7  PB2 (D 2) Ain1
// Ain2 (D 4) PB4  3|    |6  PB1 (D 1) pwm1
//            GND  4|    |5  PB0 (D 0) pwm0
//                  +----+


// ATMEL ATTINY 24/44/84
//       Dpin       +-\/-+
//            V+   1|    |14  gnd
//       10   PB0  2|    |13  PA0    0
//       9    PB1  3|    |12  PA1    1
// RESET      PB3  4|    |11  PA2    2
// (pwm) 8    PB2  5|    |10  PA3    3
// (pwm) 7    PA7  6|    |9   PA4    4   SCK
// MOSI  6    PA6  7|    |8   PA5    5   MISO (pwm)
//                  +----+

// to upload via arudino Uno, connect RESET to pin 10, MOSI to 11, MISO to 12, SCK to 13


#include <SoftwareSerial.h>

const byte RECEIVE_CHANNEL = 10;
const byte KEY[] = {44, 45, 46, 43, 36, 38, 42, 60}; // MIDI keys to translate into gate signals
const byte METRONOME_KEY = 37;                       // most metronomes use C#1 = 37
const byte VEL_BREAK = 80;                           // beatstep uses vel of 127 for bars, 80 for beats
const byte OUT_PIN[] = {0, 1, 2, 3, 4, 5, 6, 7};
const byte BEAT_PIN = 8;
const byte BAR_PIN = 9;
const byte TOTAL_KEYS = 8;


SoftwareSerial midi (10, 11);  // Rx, Tx - no need to transmit, so doesn't matter there's no pin 11

const int noRunningStatus = -1;
volatile byte note;

int runningStatus;
unsigned long lastRead;
byte lastCommand;

void setup() {
  //  Set MIDI baud rate:
  midi.begin(31250);
  runningStatus = noRunningStatus;

  // setup pins
  for (int i = 0; i < TOTAL_KEYS; i ++) {
    pinMode (OUT_PIN[i], OUTPUT);
    digitalWrite (OUT_PIN[i], LOW);
  }
  pinMode (BEAT_PIN, OUTPUT);
  pinMode (BAR_PIN, OUTPUT);
  digitalWrite (BEAT_PIN, LOW);
  digitalWrite (BAR_PIN, LOW);
} // end of setup

void RealTimeMessage (const byte msg)
{
  // ignore realtime messages
} // end of RealTimeMessage


// get next byte from serial (blocks)
int getNext ()
{

  if (runningStatus != noRunningStatus)
  {
    int c = runningStatus;
    // finished with look ahead
    runningStatus = noRunningStatus;
    return c;
  }

  while (true)
  {
    while (midi.available () == 0)
    {}
    byte c = midi.read ();
    if (c >= 0xF8)  // RealTime messages
      RealTimeMessage (c);
    else
      return c;
  }
} // end of getNext

// const char * notes [] = { "C ", "C#", "D ", "D#", "E ", "F ", "F#", "G ", "G#", "A ", "A#", "B " };
// byte octave;

byte velocity;

// interpret a note in terms of note name and octave
void getNote ()
{
  note = getNext ();

}  // end of getNote

void getVelocity ()
{
  velocity = getNext ();
}

// show a control message
void showControlMessage ()
{
  byte message =  getNext () & 0x7F;
  byte param = getNext ();
}  // end of showControlMessage

// read a system exclusive message
void showSystemExclusive ()
{
  int count = 0;
  while (true)
  {
    while (midi.available () == 0)
    {}
    byte c = midi.read ();
    if (c >= 0x80)
    {
      runningStatus = c;
      return;
    }

  } // end of reading until all system exclusive done
}  // end of showSystemExclusive

void loop()
{
  byte c = getNext ();
  unsigned int parameter;

  if (((c & 0x80) == 0) && (lastCommand & 0x80))
  {
    runningStatus = c;
    c = lastCommand;
  }

  // channel is in low order bits
  int channel = (c & 0x0F) + 1;

  // messages start with high-order bit set
  if (c & 0x80)
  {
    lastCommand = c;
    switch ((c >> 4) & 0x07)
    {
      case 0:   // Note off
        getNote ();
        getVelocity ();

        // turn off notes once note off received
        for (int i = 0; i < TOTAL_KEYS; i ++) {
          if ((channel == RECEIVE_CHANNEL) && (note == KEY[i])) {
            digitalWrite (OUT_PIN[i], LOW);
          }
        }
        if ((channel == RECEIVE_CHANNEL) && (note == METRONOME_KEY)) {
          digitalWrite (BEAT_PIN, LOW);
          digitalWrite (BAR_PIN, LOW);
        }
        break;

      case 1:   // Note on
        getNote ();
        getVelocity ();
//        if (velocity == 0) {
//           digitalWrite (LED, LOW);
//        }
        for (int i = 0; i < TOTAL_KEYS; i ++) {     // checks if any of keys above received, sends gate signal if so
          if ((channel == RECEIVE_CHANNEL) && (note == KEY[i]) && (velocity > 0)) {
            digitalWrite (OUT_PIN[i], HIGH);
          }
        }
        if ((channel == RECEIVE_CHANNEL) && (note == METRONOME_KEY) && (velocity > 0)) {
          digitalWrite (BEAT_PIN, HIGH);
        }
        if ((channel == RECEIVE_CHANNEL) && (note == METRONOME_KEY) && (velocity > VEL_BREAK)) {
          digitalWrite (BAR_PIN, HIGH);
        }

        break;

        // rest of this not used, but left here in case you need to hack it!

      case 2:  // Polyphonic pressure
        getNote ();
        parameter = getNext ();  // pressure
        break;

      case 3: // Control change
        showControlMessage ();
        break;

      case 4:  // Program change
        parameter = getNext ();  // program
        break;

      case 5: // After-touch pressure
        parameter = getNext (); // value
        break;

      case 6: // Pitch wheel change
        parameter = getNext () |  getNext () << 7;
        break;

      case 7:  // system message
        {
          lastCommand = 0;           // these won't repeat I don't think
          switch (c & 0x0F)
          {
            case 0: // Exclusive
              parameter = getNext (); // vendor ID
              showSystemExclusive ();
              break;

            case 1: // Time code
              parameter = getNext () ;
              break;

            case 2:  // Song position
              parameter = getNext () |  getNext () << 7;
              break;

            case 3: // Song select
              parameter = getNext () ;  // song
              break;

            case 4:    // reserved
            case 5:    // reserved
            case 6:    // tune request
            case 7:    // end of exclusive
            case 8:    // timing clock
            case 9:    // reserved
            case 10:   // start
            case 11:   // continue
            case 12:   // stop
            case 13:   // reserved
            case 14:   // active sensing
            case 15:   // reset
              break;

          }  // end of switch on system message

        }  // end system message
        break;
    }  // end of switch
  }  // end of if
  else
  {
    // unexpected, ignore
  }

}  // end of loop
