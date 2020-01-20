/*
     eightGate by fuzzySynths
     converts top row of Arturia Beatstep Pro to gate outputs, for a modular synth
     also metronome output, producing gate signal on bar & beats
     MIDI_decoder for ATtiny84

     code adapted from https://arduino.stackexchange.com/questions/14054/interfacing-an-attiny85-with-midi-via-software-serial


  v10 - doesn't trigger some modules (prok drums). MIDI notes too long so ? overlap?
  v11 - change to 1ms triggers.
  v12 - triggers a bit short - around 200usecs. not all coming through - might be too quick to see on scope
  v13 - see what 2000usecs does. try port manipulation?
  v14 - that didn't work. horrible. working now.
  v15 - yes PB2 working, no missed notes. better but MIDI notes too long - beats OK, notes are not. change timers...
  v15 still - gates too long on MIDI notes, but no bar & beats. weird. tried changing timers, using external crystal, didn't help.
  v17 - jumped v16. no good, timing wrong. try changing to Uno / 328 for now so can use serial
  v18 - using a Uno for serial. is it a SoftSerial thing?
  v19 - try hardware serial port on uno. YES! this works. some triggers are 4ms, some 32ms. try using port manipulation again.
  v20 - move to proMini. because I have one.
          no. needs a separate programmer.
  v30 - change to ATtiny4313. lack of hardware UART was problem. this has more pins so LEDs can have their own - useful to show status (eg uploading sysex)
          this obviates need for 74HC04 (to slow pulses down for LED). I'd also used it to launder incoming MIDI signals but only because spare 2 pins.
  v31 - changed to AT4313. program using AT tiny core -
  v32 - working!
  v33 - duff, not sure why
  v34 - port manipulation
  v35 - MIDI notes 1ms, but up to 70ms when played - so not killing after gate time? 


  also try direct port manipulation (quicker)

  PORTB |= (1 << PB3) sets bit 3 (|= is OR)
  PORTB &= ~(1 << PB3) clears bit 3 (& is AND, then ~ is NOT so inverts)



  ATMEL ATTINY 24/44/84
         Dpin       +-\/-+
              V+   1|    |14  gnd
         10   PB0  2|    |13  PA0    0
         9    PB1  3|    |12  PA1    1
   RESET      PB3  4|    |11  PA2    2
   (pwm) 8    PB2  5|    |10  PA3    3
   (pwm) 7    PA7  6|    |9   PA4    4   SCK
   MOSI  6    PA6  7|    |8   PA5    5   MISO (pwm)
                    +----+

  // to upload via arudino Uno, connect RESET to pin 10, MOSI to 11, MISO to 12, SCK to 13

   ATMEL ATTINY 25/45/85 / ARDUINO
   Pin 1 is /RESET

                    +-\/-+
   Ain0 (D 5) PB5  1|    |8  Vcc
   Ain3 (D 3) PB3  2|    |7  PB2 (D 2) Ain1
   Ain2 (D 4) PB4  3|    |6  PB1 (D 1) pwm1
              GND  4|    |5  PB0 (D 0) pwm0
                    +----+

  // ATMEL ATTINY 4313
   arduino gate         +-\/-+
      17   PA2   RESET 1|    |20  Vcc
      0    Rx     PD0  2|    |19  PB7    SCK   16
      1   (Tx)    PD1  3|    |18  PB6    MISO  15
      2    9      PA1  4|    |17  PB5    MOSI  14
      3    8      PA0  5|    |16  PB4    LED_0 13   pwm 0C1B
      4    7      PD2  6|    |15  PB3    LED_1 12   pwm 0C1A
      5    6      PD3  7|    |14  PB2    0     11   pwm 0C0A
      6    5      PD4  8|    |13  PB1    1     10
      7    4      PD5  9|    |12  PB0    2     9
                  gnd 10|    |11  PD6    3     8
                        +----+

  program via Uno - pins 13 SCK, 12 MISO, 11 MOSI, 10 RESET
*/

// #include <SoftwareSerial.h>
#include <elapsedMillis.h>

// preferences
const unsigned long GATE_TIME = 1000; // gate time, in microseconds, default is 1ms (1000us) [1ms is what Grids uses)
const unsigned long LED_TIME = 100000; // microsecs LED is lit for

// MIDI
const byte RECEIVE_CHANNEL = 10; // MIDI rcv channel
const byte KEY[] = {44, 45, 46, 43, 36, 38, 42, 60}; // MIDI keys to translate into gate signals
const byte METRONOME_KEY = 37;                       // most metronomes use C#1 = 37
const byte VEL_BREAK = 80;                           // beatstep uses vel of 127 for bars, 80 for beats

// pins
const byte OUT_PIN[] = {11, 10, 9, 8, 7, 6, 5, 4, 3, 2};  // ATtiny4313
const byte LED_PIN[] = {13, 12} ; // both PWM pins
const byte MIDI_IN_PIN = 0; // hardware UART

// don't change these
const byte TOTAL_KEYS = 8;
const byte BEAT_CH = 8; // TOTAL_KEYS + 1;
const byte BAR_CH = 9; // TOTAL_KEYS + 2;
byte velocity;

bool noteOn[10] = {false, false, false, false, false, false, false, false, false, false};
bool gateOn[10] = {false, false, false, false, false, false, false, false, false, false};
bool LED_on[2] = {false, false};
unsigned long startTime[10];


elapsedMicros sinceGateStart0;
elapsedMicros sinceGateStart1;
elapsedMicros sinceGateStart2;
elapsedMicros sinceGateStart3;
elapsedMicros sinceGateStart4;
elapsedMicros sinceGateStart5;
elapsedMicros sinceGateStart6;
elapsedMicros sinceGateStart7;
elapsedMicros sinceGateStart8;
elapsedMicros sinceGateStart9;


const int noRunningStatus = -1;
volatile byte note;

int runningStatus;
unsigned long lastRead;
byte lastCommand;

void setup() {
  //  Set MIDI baud rate:
  Serial.begin(31250);
  runningStatus = noRunningStatus;

  // setup pins
  pinMode (MIDI_IN_PIN, INPUT); // input is default, so not needed
  for (int i = 0; i < (TOTAL_KEYS + 2); i ++) {
    pinMode (OUT_PIN[i], OUTPUT);
    digitalWrite (OUT_PIN[i], LOW);
  }
  pinMode (LED_PIN[0], OUTPUT);
  pinMode (LED_PIN[1], OUTPUT);
  digitalWrite (LED_PIN[0], LOW);
  digitalWrite (LED_PIN[1], LOW);
} // end of setup

// outputs

void setLED(int LEDch) {
  if (LEDch == 0) {
    PORTB |= (1 << PB4); //  LED_0
    LED_on[0] = true;
  }
  if (LEDch == 1) {
    PORTB |= (1 << PB3); //  LED_1
    LED_on[1] = true;
  }
}

void clearLED(int LEDch) {
//  digitalWrite(LED_PIN[LEDch], LOW);
  if (LEDch == 0) {
    PORTB &= ~(1 << PB4);   //  LED_0
  }
  if (LEDch == 1) {
    PORTB &= ~(1 << PB3);   //  LED_1
  }
}




void turnGatesOff() {

  if ((gateOn[0]) && (sinceGateStart0 > GATE_TIME)) {
    clearGate(0);
    // PORTB &= ~(1 << PB2); // clears gate bit
    // gateOn[0] = false;
  }

  if ((gateOn[1]) && (sinceGateStart1 > GATE_TIME)) {
        clearGate(1);
    //PORTB &= ~(1 << PB1);
    //gateOn[1] = false;
  }

  if ((gateOn[2]) && (sinceGateStart2 > GATE_TIME)) {
        clearGate(2);
   // PORTB &= ~(1 << PB0);
   // gateOn[2] = false;
  }

  if ((gateOn[3]) && (sinceGateStart3 > GATE_TIME)) {
        clearGate(3);
  //  PORTD &= ~(1 << PD6);
   // gateOn[3] = false;
  }

  if ((gateOn[4]) && (sinceGateStart4 > GATE_TIME)) {
        clearGate(4);
   // PORTD &= ~(1 << PD5);
   // gateOn[4] = false;
  }

  if ((gateOn[5]) && (sinceGateStart5 > GATE_TIME)) {
        clearGate(5);
  //  PORTD &= ~(1 << PD4);
  //  gateOn[5] = false;
  }

  if ((gateOn[6]) && (sinceGateStart6 > GATE_TIME)) {
        clearGate(6);
   // PORTD &= ~(1 << PD3);
  //  gateOn[6] = false;
  }

  if ((gateOn[7]) && (sinceGateStart7 > GATE_TIME)) {
        clearGate(7);
   // PORTD &= ~(1 << PD2);
   // gateOn[7] = false;
  }

  if ((gateOn[BAR_CH]) && sinceGateStart8 > GATE_TIME) {
        clearGate(8);
    //PORTA &= ~(1 << PA0);
    //gateOn[BAR_CH] = false;
  }

  if ((gateOn[BEAT_CH]) && sinceGateStart9 > GATE_TIME) {
        clearGate(9);
    //PORTA &= ~(1 << PA1);
    //gateOn[BEAT_CH] = false;
  }

  if ((LED_on[0]) && (sinceGateStart8 > LED_TIME)) {
    clearLED(0);
  }

  if ((LED_on[1]) && (sinceGateStart9 > LED_TIME)) {
    clearLED(1);
  }
}



// MIDI inputs

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
    // while (midi.available () == 0)
    while (Serial.available () == 0)
    {}
    // byte c = midi.read ();
    byte c = Serial.read ();
    if (c >= 0xF8)  // RealTime messages
      RealTimeMessage (c);
    else
      return c;
  }
} // end of getNext




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
    //  while (midi.available () == 0)
    while (Serial.available () == 0)
    {}
    //  byte c = midi.read ();
    byte c = Serial.read ();
    if (c >= 0x80)
    {
      runningStatus = c;
      return;
    }

  } // end of reading until all system exclusive done
}  // end of showSystemExclusive

void clearGate(int ch) {
  gateOn[ch] = false;
  switch (ch) {
    case 0:
      PORTB &= ~(1 << PB2);
      break;
    case 1:
      PORTB &= ~(1 << PB1);
      break;
    case 2:
      PORTB &= ~(1 << PB0);
      break;
    case 3:
      PORTD &= ~(1 << PD6);
      break;
    case 4:
      PORTD &= ~(1 << PD5);
      break;
    case 5:
      PORTD &= ~(1 << PD4);
      break;
    case 6:
      PORTD &= ~(1 << PD3);
      break;
    case 7:
      PORTD &= ~(1 << PD2);
      break;
    case 8:
      PORTA &= ~(1 << PA0);
      break;
    case 9:
      PORTA &= ~(1 << PA1);
      break;
  }

}


void loop() {
  turnGatesOff();

  // checks for incoming MIDI
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
            noteOn[i] = false;
            clearGate(i);
          }
        }
        if ((channel == RECEIVE_CHANNEL) && (note == METRONOME_KEY)) {
          noteOn[BEAT_CH] = false;
          noteOn[BAR_CH] = false;
          clearGate(8);
          clearGate(9);
        }


        break;

      case 1:   // Note on
        getNote ();
        getVelocity ();

        for (int i = 0; i < TOTAL_KEYS; i ++) {     // checks if any of keys above received, sends gate signal if not already on
          if ((channel == RECEIVE_CHANNEL) && (note == KEY[i]) && (velocity > 0) && (!noteOn[i])) {
            noteOn[i] = true;                                                                                 // sets noteOn flag
            gateOn[i] = true;

            switch (i) {
              case 0:
                PORTB |= (1 << PB2);  // digitalWrite (OUT_PIN[i], HIGH);
                sinceGateStart0 = 0;
                break;
              case 1:
                PORTB |= (1 << PB1);  // digitalWrite (OUT_PIN[i], HIGH);
                sinceGateStart1 = 0;
                break;
              case 2:
                PORTB |= (1 << PB0);  // digitalWrite (OUT_PIN[i], HIGH);
                sinceGateStart2 = 0;
                break;
              case 3:
                PORTD |= (1 << PD6);  // digitalWrite (OUT_PIN[i], HIGH);
                sinceGateStart3 = 0;
                break;
              case 4:
                PORTD |= (1 << PD5);  // digitalWrite (OUT_PIN[i], HIGH);
                sinceGateStart4 = 0;
                break;
              case 5:
                PORTD |= (1 << PD4);  // digitalWrite (OUT_PIN[i], HIGH);
                sinceGateStart5 = 0;
                break;
              case 6:
                PORTD |= (1 << PD3);  //digitalWrite (OUT_PIN[i], HIGH);
                sinceGateStart6 = 0;
                break;
              case 7:
                PORTD |= (1 << PD2);  //digitalWrite (OUT_PIN[i], HIGH);
                sinceGateStart7 = 0;
                break;
            }
          }
        }
        if ((channel == RECEIVE_CHANNEL) && (note == METRONOME_KEY) && (velocity > 0) && (!noteOn[BEAT_CH]) && (!gateOn[BEAT_CH])) {
          noteOn[BEAT_CH] = true;
          gateOn[BEAT_CH] = true;
          sinceGateStart9 = 0;
          setLED(1);
          PORTA |= (1 << PA1); // set beat gate 9
          
          if (velocity > VEL_BREAK) { // it's a high signal, so a bar marker
            noteOn[BAR_CH] = true;
            gateOn[BAR_CH] = true;
            sinceGateStart8 = 0;
            setLED(0);
            PORTA |= (1 << PA0); // set beat gate 8
          }
        }


        break;

      // rest of this not used, but left here in case you need to hack it

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
