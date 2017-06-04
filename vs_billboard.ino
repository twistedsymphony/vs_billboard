/**********************************************************************************************************
 * Sega Vs Billboard Emulator
 * 2016-2017 - Michael Pica (aka twistedsymphony)
 * 
 * This software is designed to smulate the functionality of the billboard hardware
 * found on Sega Versus City and New Versus City cabinets as well as the 
 * optional billboard for the Sega Blast City and Megalo 410.
 * 
 * this software is designed for an Arduino Mega and designed to output to a single 74HC595 for 
 * each 7-Segment display (2 per player). the 8 outputs of the 74HC595 should be mapped in order to each 
 * of the segments in the 7-segement display
 * 
 * this software also requires the ArduinoThread library https://github.com/ivanseidel/ArduinoThread
 * 
***********************************************************************************************************/
#include <Thread.h>
#include <ThreadController.h>

//settings
const boolean invert_disp_output = 0; //0 for common cathode 7-segments, 1 for common annode 7-segments
const boolean invert_wl_output = 0; //allows you to switch for active when high or active when low
const boolean debug_mode = 1; //outputs command information to the serial monitor

//Vs billboard data input pins

//no billboard input pins specified
//billboard hardcoded to Mega Port K (A8-A15) so that we can run a port interrupt and read at once
unsigned long last_interrupted = 0;
const int p1_pin = 5; //jumper for switching player output sides

//specify our 7-Segment output pins
const int clock_pin = 14; //shared by all 4 displays connected to SH_CP (pin 11) of 74HC595
const int data_pin = 15; //shared by all 4 displays connected to DS (pin 14) of 74HC595
int latch_ar_pin = 17; //player 1 right digit connected to ST_CP (pin 12) of 74HC595
int latch_al_pin = 16; //player 1 left digit connected to ST_CP (pin 12) of 74HC595
int latch_br_pin = 19; //player 2 right digit connected to ST_CP (pin 12) of 74HC595
int latch_bl_pin = 18; //player 2 left digit connected to ST_CP (pin 12) of 74HC595

//specify our Winner lamp output pins
int wl_a_pin = 2; //player 1 lamp 21
int wl_b_pin = 3; //player 2 lamp 20


//threading
ThreadController controll = ThreadController();
Thread* wl_a_thread = new Thread();
Thread* wl_b_thread = new Thread();
Thread* animation_a_thread = new Thread();
Thread* animation_b_thread = new Thread();

//digit states
byte al_static = 0x00; //the current non-animation state of player 1 left digit
byte ar_static = 0x00; //the current non-animation state of player 1 right digit
byte bl_static = 0x00; //the current non-animation state of player 2 left digit
byte br_static = 0x00; // the current non-animation state of player 2 right digit
boolean a_animation = 0; //specifies if player 1 is displaying an animation (1) or static (0)
boolean b_animation = 0; //specifies if player 2 is displaying an animation (1) or static (0)

//winner lamp output data vars
int wl_a_action = 7; //on(0), slow blink(1), med blink (2), fast blink (3), 2/5 blink (4), 3/5 blink (5), 4/5 blink (6), off(7)
int wl_b_action = 7; //on(0), slow blink(1), med blink (2), fast blink (3), 2/5 blink (4), 3/5 blink (5), 4/5 blink (6), off(7)
boolean wl_a_state = LOW; //Player 1 current output state
boolean wl_b_state = LOW; //player 2 current output state
int wl_a_step = 0; //player 1 blink step
int wl_b_step = 0; //player 2 blink step

//animation output data vars
byte disp_a_mode = 0; //0=static, 1=animation, 2=demo loop
byte disp_b_mode = 0; //0=static, 1=animation, 2=demo loop
unsigned int disp_a_demo_step = 0; //player 1 step in demo loop
unsigned int disp_b_demo_step = 0; //player 2 step in demo loop
unsigned int disp_a_demo_steps = 0; //player 1 total steps in demo loop
unsigned int disp_b_demo_steps = 0; //player 2 total steps in demo loop
byte animation_a_sequence = 0;
byte animation_b_sequence = 0;
byte disp_a_demo_sequence = 0;
byte disp_b_demo_sequence = 0;
unsigned int animation_a_step = 0; //player 1 step in the animation
unsigned int animation_b_step = 0; //player 2 step in the animation
unsigned int animation_a_steps = 0; //player 1 total steps in animation
unsigned int animation_b_steps = 0; //player 2 total steps in animation
unsigned int animation_a_speed = 32; //player 1 time in ms per step
unsigned int animation_b_speed = 32; //player 2 time in ms per step



//alphanumeric segments to illuminate for a given code 0x00 to 0x7F
//8th bit is always zero as thats the dot segment, not set by these codes
const byte digit_segments[33] = {
   B00111111, //B00000: 0
   B00000110, //B00001: 1
   B01011011, //B00010: 2
   B01001111, //B00011: 3
   B01100110, //B00100: 4
   B01101101, //B00101: 5
   B01111101, //B00110: 6
   B00000111, //B00111: 7
   B01111111, //B01000: 8
   B01101111, //B01001: 9
   B01110111, //B01010: A 10
   B01111100, //B01011: b
   B00111001, //B01100: C
   B01011110, //B01101: d
   B01111001, //B01110: E
   B01110001, //B01111: F 15
   B00111101, //B10000: G
   B01110110, //B10001: H
   B00110000, //B10010: I
   B00011110, //B10011: J
   B01111000, //B10100: k 20
   B00111000, //B10101: L
   B01010100, //B10110: n
   B01011100, //B10111: o
   B01110011, //B11000: P
   B01100111, //B11001: q 25
   B01010000, //B11010: r
   B00110001, //B11011: T
   B00111110, //B11100: U
   B00011100, //B11101: v
   B01101110, //B11110: y 30
   B00000000, //B11111: clear
   B01000000  //B100000: - (not an official code but added for the "rd-4" text scroll
};

//chars releated to the output codes (for use in debug output)
const char digit_val[] = "0123456789AbCdEFGHIJkLnoPqrTUvy ";


/* animations
 * the even bytes are the left display while the odd bytes are the right display
 * each bit corrospodes to a display segment similar to the character codes
 */
 
//" SEGA " text scroll ~480ms per frame
const unsigned int animation_00_speed = 512;
const unsigned int animation_00_steps = 12;
const byte animation_00[animation_00_steps]={
  B00000000, B01101101,
  B01101101, B01111001,
  B01111001, B00111101,
  B00111101, B01110111,
  B01110111, B00000000,
  B00000000, B00000000
}; 

//"Spinning Wheels" ~96ms per frame
const unsigned int animation_01_speed = 102;
const unsigned int animation_01_steps = 12;
const byte animation_01[animation_01_steps] ={
  B00000001, B00000001,
  B00000010, B00000010,
  B00000100, B00000100,
  B00001000, B00001000,
  B00010000, B00010000,
  B00100000, B00100000
};

//"The Wave" ~192ms per frame
const unsigned int animation_02_speed = 205;
const unsigned int animation_02_steps = 14;
const byte animation_02[animation_02_steps] ={
  B00000000, B00001000,
  B00001000, B01010100,
  B01010100, B00110111,
  B00110111, B00110111,
  B00110111, B01010100,
  B01010100, B00001000,
  B00001000, B00000000
};

//"Square Chase" ~96ms per frame
const unsigned int animation_03_speed = 102;
const unsigned int animation_03_steps = 8;
const byte animation_03[animation_03_steps] ={
  B00000000, B01011100,
  B01011100, B00000000,
  B01100011, B00000000,
  B00000000, B01100011
};

//"C Chase" ~192ms per frame
const unsigned int animation_04_speed = 205;
const unsigned int animation_04_steps = 8;
const byte animation_04[animation_04_steps] ={
  B01100001, B01001100,
  B01001100, B01100001,
  B00011100, B00100011,
  B01100010, B01010100
};

//"The Fountain" ~96ms per frame
const unsigned int animation_05_speed = 95;
const unsigned int animation_05_steps = 12;
const byte animation_05[animation_05_steps] ={
  B00000001, B00000001,
  B00100000, B00000010,
  B00010000, B00000100,
  B00001000, B00001000,
  B00000100, B00010000,
  B00000010, B00100000
};

//"The Snake" ~32ms per frame
const unsigned int animation_06_speed = 52;
const unsigned int animation_06_steps = 24;
const byte animation_06[animation_06_steps] ={
  B00000001, B00000001,
  B00000000, B00000011,
  B00000000, B01000010,
  B01000000, B01000000,
  B01010000, B00000000,
  B00011000, B00000000,
  B00001000, B00001000,
  B00000000, B00001100,
  B00000000, B01000100,
  B01000000, B01000000,
  B01100000, B00000000,
  B00100001, B00000000
};

//"Line Chase" ~96ms per frame
const unsigned int animation_07_speed = 102;
const unsigned int animation_07_steps = 8;
const byte animation_07[animation_07_steps] ={
  B00001000, B00000001,
  B00010000, B00000010,
  B00100000, B00000100,
  B00000001, B00001000
};

//"SEGA  VERSUS CITY  ADVANCED BATTLE CABINET  " ~384ms per frame
const unsigned int animation_10_speed = 448;
const unsigned int animation_10_steps = 88;
const byte animation_10[animation_10_steps]= {
  B01101101, B01111001,
  B01111001, B00111101,
  B00111101, B01110111,
  B01110111, B00000000,  
  B00000000, B00000000, 
  B00000000, B00011100, 
  B00011100, B01111001, 
  B01111001, B01010000, 
  B01010000, B01101101, 
  B01101101, B00111110, 
  B00111110, B01101101, 
  B01101101, B00000000, 
  B00000000, B00111001, 
  B00111001, B00110000, 
  B00110000, B00110001, 
  B00110001, B01101110, 
  B01101110, B00000000, 
  B00000000, B00000000, 
  B00000000, B01110111, 
  B01110111, B01011110, 
  B01011110, B00011100, 
  B00011100, B01110111, 
  B01110111, B01010100, 
  B01010100, B00111001, 
  B00111001, B01111001, 
  B01111001, B01011110, 
  B01011110, B00000000, 
  B00000000, B01111100, 
  B01111100, B01110111, 
  B01110111, B00110001, 
  B00110001, B00110001, 
  B00110001, B00111000, 
  B00111000, B01111001, 
  B01111001, B00000000, 
  B00000000, B00111001, 
  B00111001, B01110111, 
  B01110111, B01111100, 
  B01111100, B00110000, 
  B00110000, B01010100, 
  B01010100, B01111001, 
  B01111001, B00110001, 
  B00110001, B00000000,
  B00000000, B00000000,
  B00000000, B01101101
};

/* HIDDEN Animations:
 *  These are animations used by the demo loops
 *  that can't be called directly by a command
 */

//"Digit Fill" ~192ms per frame
const unsigned int animation_20_speed = 205;
const unsigned int animation_20_steps = 34;
const byte animation_20[animation_20_steps] ={
  B00000000, B00000001,
  B00000000, B00000011,
  B00000000, B00000111,
  B00000000, B00001111,
  B00000000, B00011111,
  B00000000, B00111111,
  B00000000, B01111111,
  B00000000, B11111111,
  B00000001, B11111111,
  B00000011, B11111111,
  B00000111, B11111111,
  B00001111, B11111111,
  B00011111, B11111111,
  B00111111, B11111111,
  B01111111, B11111111,
  B11111111, B11111111,
  B00000000, B00000000
};

//"P1" text Blink ~480ms per frame
const unsigned int animation_21_speed = 512;
const unsigned int animation_21_steps = 4;
const byte animation_21[animation_21_steps]={
  B00000110, B01110011,
  B00000000, B00000000
};

//"P2" text Blink ~480ms per frame
const unsigned int animation_22_speed = 512;
const unsigned int animation_22_steps = 4;
const byte animation_22[animation_22_steps]={
  B01011011, B01110011,
  B00000000, B00000000
};

//"blank for 480ms
const unsigned int animation_23_speed = 480;
const unsigned int animation_23_steps = 2;
const byte animation_23[animation_23_steps]={
  B00000000, B00000000
};

//" SEGA rd-4 " text scroll ~480ms per frame
const unsigned int animation_24_speed = 512;
const unsigned int animation_24_steps = 20;
const byte animation_24[animation_24_steps]={
  B00000000, B01101101,
  B01101101, B01111001,
  B01111001, B00111101,
  B00111101, B01110111,
  B01110111, B00000000,
  B00000000, B01010000,
  B01010000, B01011110,
  B01011110, B01000000,
  B01000000, B01100110,
  B01100110, B00000000
};



//"eyes" varying timing
const unsigned int animation_25_speed = 512;
const unsigned int animation_25_steps = 0;
const byte animation_25[animation_25_steps] = {
};

//Demo Loop 0 (attract mode 1)
const unsigned int demo_0_steps = 1;
const byte demo_0[demo_0_steps] ={
  21
};
 
//Demo Loop 1
const unsigned int demo_1_steps = 11;
const byte demo_1[demo_1_steps] ={
  20, //fill
  21, //P1 Blink
  21, //P1 Blink
  21, //P1 Blink
  21, //P1 Blink
  21, //P1 Blink
  01, //Spinning Wheels
  01, //Spinning Wheels
  01, //Spinning Wheels
  24, //SEGA rd-4 Scroll
  23  //Blank
};

//Demo Loop 2
//the same as demo 1 except flashes 2P
const unsigned int demo_2_steps = 11;
const byte demo_2[demo_2_steps] ={
  20, //fill
  22, //P2 Blink
  22, //P2 Blink
  22, //P2 Blink
  22, //P2 Blink
  22, //P2 Blink
  01, //Spinning Wheels
  01, //Spinning Wheels
  01, //Spinning Wheels
  24, //SEGA rd-4 Scroll
  23  //Blank
};

//Demo Loop 3 (attract mode 2)
//the same as demo 1 except it flashes 1p or 2p depending on side
//it also prefixes animation 1 with a few seconds of the winner lamp on
//then switches to a slow blink at the the start, then switches the
// wl off after the 3rd 1P flash
const unsigned int demo_3_steps = 11;
const byte demo_3[demo_3_steps] ={
  20, //fill
  21, //P1 Blink
  21, //P1 Blink
  21, //P1 Blink
  21, //P1 Blink
  21, //P1 Blink
  01, //Spinning Wheels
  01, //Spinning Wheels
  01, //Spinning Wheels
  24, //SEGA rd-4 Scroll
  23  //Blank
};

/* run once at start up*/
void setup() {
  //set our billboard pins to input mode
  pinMode(p1_pin, INPUT);
  
  //set our 7-segment pins to output mode
  pinMode(clock_pin, OUTPUT);
  pinMode(data_pin, OUTPUT);
  pinMode(latch_ar_pin, OUTPUT);
  pinMode(latch_al_pin, OUTPUT);
  pinMode(latch_br_pin, OUTPUT);
  pinMode(latch_bl_pin, OUTPUT);
    
  //set our winner lamp pins to output mode
  pinMode(wl_a_pin, OUTPUT);
  pinMode(wl_b_pin, OUTPUT);

  //check for player inversion
  if(digitalRead(p1_pin) == HIGH){
    //swap winner lamp pins
    int temp = wl_a_pin;
    wl_a_pin = wl_b_pin;
    wl_b_pin = temp;

    //swap right digit latch pins
    temp = latch_ar_pin;
    latch_ar_pin = latch_br_pin;
    latch_br_pin = temp;
    
    //swap left digit latch pins
    temp = latch_al_pin;
    latch_al_pin = latch_bl_pin;
    latch_bl_pin = temp;
  }

  //setup winner lamp psudothreads
  wl_a_thread->enabled = false;
  wl_a_thread->onRun(wl_a_callback);
  wl_a_thread->setInterval(500);
  controll.add(wl_a_thread);

  wl_b_thread->enabled = false;
  wl_b_thread->onRun(wl_b_callback);
  wl_b_thread->setInterval(500);
  controll.add(wl_b_thread); 

  //setup animation psudothreads
  animation_a_thread->enabled = false;
  animation_a_thread->onRun(animation_a_callback);
  animation_a_thread->setInterval(100);
  controll.add(animation_a_thread);

  animation_b_thread->enabled = false;
  animation_b_thread->onRun(animation_b_callback);
  animation_b_thread->setInterval(100);
  controll.add(animation_b_thread);
  
  if(debug_mode == 1){
    //start the serial output 
    Serial.begin(9600);

    //write out a start message to the console, so we know it's working
    Serial.println("Start\n");
  }

  //setup port interrupts
  DDRK = B00000000; //set Port K as input
  cli(); // switch interrupts off while messing with their settings
  PCICR = 0b00000100; // Enable PCINT2 interrupt (Port K on Mega 2560)
  PCMSK2 = 0b11111111; // Enabling interrupt on pins A8 to A15 (PCINT16:23 on Mega 2560)
  sei(); // turn interrupts back on
}


/* Interrupt service routine for PCINT2 (Port K on Mega 2560) */
ISR(PCINT2_vect) {
 /* bool interrupted = false;
  if(micros() - last_interrupted < 400){
    interrupted = true;
  }
  last_interrupted = micros();
  if(!interrupted){
    delayMicroseconds(260);*/
    command_handler(PINK); //read Port K directly into the command handler
  //}
}


/* the main loop */
void loop() {
  controll.run(); //check psudothread timers
}


/* routes the command to the correct handler */
void command_handler(byte command){
  noInterrupts(); //disable interrupts while we process the command
  switch(command >> 4) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
      static_letter(command);
      break;
    case 8:
      static_dots(command);
      break;
    case 11:
    case 12:
    case 13:
    case 14:
      static_segment(command);
      break;
    case 9:
      set_wl(command);
      break;
    case 10:
    case 15:
      decode_animation_command(command);
      break;
  }
  interrupts(); //re-enable interrupts
  
  if(debug_mode == 1){
    debug_output(command);
  }
  return;
}

void return_to_static(boolean side){
  if(side == 0){ //side A
    if(disp_a_mode != 0){
      animation_a_thread->enabled = false;
      disp_a_mode = 0;
      shift_out(latch_al_pin, al_static);
      shift_out(latch_ar_pin, ar_static);
    }
  } else { //side B
    if(disp_b_mode != 0){
      animation_b_thread->enabled = false;
      disp_b_mode = 0;
      shift_out(latch_bl_pin, bl_static);
      shift_out(latch_br_pin, br_static);
    }
  }
}


/* handles a static letter change command */
void static_letter(byte command){
  byte data_out = digit_segments[command & B00011111];
  int latch_pin;
  byte current_mode;
  boolean side;
  switch(command >> 5) {
    case 0: //disp A Right
      latch_pin = latch_ar_pin;
      data_out |= (ar_static & B10000000); //maintain the current decimal if set
      ar_static = data_out;
      current_mode = disp_a_mode;
      side = 0;
      break;
    case 1: //disp A Left
      latch_pin = latch_al_pin;
      data_out |= (al_static & B10000000); 
      al_static = data_out;
      current_mode = disp_a_mode;
      side = 0;
      break;
    case 2: //disp B Right
      latch_pin = latch_br_pin;
      data_out |= (br_static & B10000000);
      br_static = data_out;
      current_mode = disp_b_mode;
      side = 1;
      break;
    case 3: //disp B Left
      latch_pin = latch_bl_pin;
      data_out |= (bl_static & B10000000);
      bl_static = data_out;
      current_mode = disp_b_mode;
      side = 1;
      break;
    default:
      return;
  }

  if(current_mode != 0){
    return_to_static(side);
  } else {
    shift_out(latch_pin, data_out);
  }
  return;
}


/* handles a static dot change command */
void static_dots(byte command){
  command = ~command;
  byte ar_dot = ((command & B00000001)<<7);//move command dot for AR to most significant bit
  byte al_dot = ((command & B00000010)<<6);//move command dot for AL to most significant bit
  byte br_dot = ((command & B00000100)<<5);//move command dot for BR to most significant bit
  byte bl_dot = ((command & B00001000)<<4);//move command dot for BL to most significant bit
  
  if(ar_dot != (ar_static & B10000000)){ //check if dot changed
    ar_dot |= (ar_static & B01111111); //change just the dot but leave everything else
    shift_out(latch_ar_pin, ar_dot);
    ar_static = ar_dot;
  }
  
  if(al_dot != (al_static & B10000000)){ //check if dot changed
    al_dot |= (al_static & B01111111); //change just the dot but leave everything else
    shift_out(latch_al_pin, al_dot);
    al_static = al_dot;
  }

  if(br_dot != (br_static & B10000000)){ //check if dot changed
    br_dot |= (br_static & B01111111); //change just the dot but leave everything else
    shift_out(latch_br_pin, br_dot);
    br_static = br_dot;
  }  
  
  if(bl_dot != (bl_static & B10000000)){ //check if dot changed
    bl_dot |= (bl_static & B01111111); //change just the dot but leave everything else
    shift_out(latch_bl_pin, bl_dot);
    bl_static = bl_dot;
  }
  
  return;
}




/* handles a static change to a single segment */
void static_segment(byte command){
  byte segment = (command & B00000111);
  byte data_out = 1<<segment;
  byte off = ((command & B00001000)>>3);
  int latch_pin;
  byte current_mode;
  boolean side;
  switch(command >> 4) {
    case 11: //disp A Right
      latch_pin = latch_ar_pin;
      if(off == 1){
        data_out = ~data_out & ar_static;
      } else {
        data_out |= ar_static;
      }
      ar_static = data_out;
      current_mode = disp_a_mode;
      side = 0;
      break;
    case 12: //disp A Left
      latch_pin = latch_al_pin;
      if(off == 1){
        data_out = ~data_out & al_static;
      } else {
        data_out |= al_static;
      }
      al_static = data_out;
      current_mode = disp_a_mode;
      side = 0;
      break;
    case 13: //disp B Right
      latch_pin = latch_br_pin;
      if(off == 1){
        data_out = ~data_out & br_static;
      } else {
        data_out |= br_static;
      }
      br_static = data_out;
      current_mode = disp_b_mode;
      side = 1;
      break;
    case 14: //disp B Left
      latch_pin = latch_bl_pin;
      if(off == 1){
        data_out = ~data_out & bl_static;
      } else {
        data_out |= bl_static;
      }
      bl_static = data_out;
      current_mode = disp_b_mode;
      side = 1;
      break;
    default:
      return;
  }

  if(current_mode != 0){
    return_to_static(side);
  } else {
    shift_out(latch_pin, data_out);
  }
  return;
}




/* handles a winner lamp command */
void set_wl(byte command){
  byte side = (command & B00001000)>>3; //0 = a lamp, 1 = b lamp
  command &= B00000111; //strip it down to lamp command 0-7
  unsigned int blink_rate = 0; //time in ms, 0 = don't change
  int wl_step = 0;
  switch(command){
    case 0: //on
      blink_rate = 0;
      break;
    case 1: //slow blink
      blink_rate = 512;
      break;
    case 2: //med blink
      blink_rate = 256;
      break;
    case 3: //fast blink
      blink_rate = 128;
      wl_step = 1;
      break;
    case 4: //blink 2, pause 3
      blink_rate = 205;
      break;
    case 5: //blink 3, pause 2
    case 6: //blink 4, pause 1
      wl_step = 9;
      blink_rate = 205;
      break;
    case 7:
      blink_rate = 0;
      break;
    default:
      return;
      break;
  }
  
  if(side == 0){ //a lamp
    if (wl_a_action == command){ //if this is already the currently running command
      return; //do nothing
    }
    wl_a_action = command;
    wl_a_step = wl_step;
    if(blink_rate == 0){
      wl_a_thread->enabled = false;  
    } else {
      wl_a_thread->setInterval(blink_rate);
      wl_a_thread->enabled = true;
    }
    wl_a_callback();
  } else { //b lamp
    if (wl_b_action == command){ //if this is already the currently running command
      return; //do nothing
    }
    wl_b_action = command;
    wl_b_step = wl_step;
    if(blink_rate == 0){
      wl_b_thread->enabled = false;
    } else {
      wl_b_thread->setInterval(blink_rate);
      wl_b_thread->enabled = true;
    }
    wl_b_callback();
  }

  return;
}


/* changes the A side winner lamp state to the next step */
void wl_a_callback(){
  boolean new_state = 0;
  
  //blink patters have a 10 step loop, so start over if we're at 10
  if(wl_a_step > 9){
    wl_a_step = 0;
  }
  
  switch(wl_a_action){
    case 0: //wl on
      wl_a_thread->enabled = false; 
      new_state = 1;
      break;
    case 7: //wl off
      wl_a_thread->enabled = false; 
      new_state = 0;
      break;
    case 1: //500ms blink
    case 2: //250ms blink
    case 3: //125ms blink
      new_state = 0;
      if(wl_a_state == 0){//toggle state
        new_state = 1;
      }
      break;
    default: // blink patterns
      //we set state by step
      switch(wl_a_step){
        case 0: //first blink
        case 2: //second blink
          new_state = 1;
          break;
        case 4:
          new_state = 1;
          if(wl_a_action == 4){
            new_state = 0;
          }
          break;
        case 6:
          new_state = 1;
          if(wl_a_action != 6){
            new_state = 0;
          }
          break;
        default: //odd steps and step 8 is always skipped
          new_state = 0;
          break;
      }
      break;
  }

  if(new_state != wl_a_state){ //skip this if no change
    wl_a_state = new_state;
    if(invert_wl_output == 1){
      new_state = ~new_state;
    }
    digitalWrite(wl_a_pin, new_state);
  }
  wl_a_step++; //incriment the step
}


/* changes the B side winner lamp state to the next step */
void wl_b_callback(){
   boolean new_state = 0;

  //blink patters have a 10 step loop, so start over if we're at 10
  if(wl_b_step > 9){
    wl_b_step = 0;
  }
  
  switch(wl_b_action){
    case 0: //wl on
      wl_b_thread->enabled = false; 
      new_state = 1;
      break;
    case 7: //wl off
      wl_b_thread->enabled = false; 
      new_state = 0;
      break;
    case 1: //500ms blink
    case 2: //250ms blink
    case 3: //125ms blink
      new_state = ~wl_b_state; //toggle state
      break;
    default: // blink patterns
      //we set state by step
      switch(wl_b_step){
        case 0: //first blink
        case 2: //second blink
          new_state = 1;
          break;
        case 4:
          new_state = 1;
          if(wl_b_action == 4){
            new_state = 0;
          }
          break;
        case 6:
          new_state = 1;
          if(wl_b_action != 6){
            new_state = 0;
          }
          break;
        default: //odd steps and step 8 is always skipped
          new_state = 0;
          break;
      }
      break;
  }

  if(new_state != wl_b_state){ //skip this if no change
    wl_b_state = new_state;
    if(invert_wl_output == 1){
      new_state = ~new_state;
    }
    digitalWrite(wl_b_pin, new_state);
  }
  wl_b_step++; //incriment the step
}

void decode_animation_command(byte command){
  byte side = (command & B00001000)>>3; //0 = a side, 1 = b side
  byte animation_sequence = 0;
  if((command>>4)==15){
    animation_sequence = 8;
  }
  animation_sequence += command&B00000111;
  config_animation(animation_sequence, side, 0);
}

void config_animation(byte animation_sequence, byte side, boolean demo_step){
  byte mode = 0;
  boolean start_demo = 0;
  unsigned int animation_speed = 0; //time in ms, 0 = don't change
  unsigned int animation_steps = 0;

  switch(animation_sequence){
    case 0: //" SEGA text scroll"
      mode = 1;
      animation_speed = animation_00_speed;
      animation_steps = animation_00_steps;
      break;
    case 1: //" Spinning Wheels"
      mode = 1;
      animation_speed = animation_01_speed;
      animation_steps = animation_01_steps;
      break;
    case 2: //" The Wave"
      mode = 1;
      animation_speed = animation_02_speed;
      animation_steps = animation_02_steps;
      break;
    case 3: //" Square Chase"
      mode = 1;
      animation_speed = animation_03_speed;
      animation_steps = animation_03_steps;
      break;
    case 4: //" C Chase"
      mode = 1;
      animation_speed = animation_04_speed;
      animation_steps = animation_04_steps;
      break;
    case 5: //" The Fountain"
      mode = 1;
      animation_speed = animation_05_speed;
      animation_steps = animation_05_steps;
      break;
    case 6: //" The Snake"
      mode = 1;
      animation_speed = animation_06_speed;
      animation_steps = animation_06_steps;
      break;
    case 7: //" Line Chase"
      mode = 1;
      animation_speed = animation_07_speed;
      animation_steps = animation_07_steps;
      break;
    case 8: //" Demo Loop 1 (P1)"
      mode = 2;
      start_demo = 1;
      animation_sequence = 1; //demo sequence
      animation_steps = demo_1_steps; //demo steps
      break;
    case 9: //" Demo Loop 1 (P2)"
      mode = 2;
      start_demo = 1;
      animation_sequence = 2; //demo sequence
      animation_steps = demo_2_steps; //demo steps
      break;
    case 10: //" SEGA VERSUS CITY ADVANCED BATTLE CABINET text scroll"
      mode = 1;
      animation_speed = animation_10_speed;
      animation_steps = animation_10_steps;
      break;
      
    //HIDDEN ANIMATIONS
    case 20: //fill
      mode = 1;
      animation_speed = animation_20_speed;
      animation_steps = animation_20_steps;
      break;
    case 21: //P1 Blink
      mode = 1;
      animation_speed = animation_21_speed;
      animation_steps = animation_21_steps;
      break;
    case 22: //P2 Blink
      mode = 1;
      animation_speed = animation_22_speed;
      animation_steps = animation_22_steps;
      break;
    case 23: //blank
      mode = 1;
      animation_speed = animation_23_speed;
      animation_steps = animation_23_steps;
      break;
    case 24: //sega rd-4 text scroll
      mode = 1;
      animation_speed = animation_24_speed;
      animation_steps = animation_24_steps;
      break;

    //ATTRACT MODES  
    case 98: //attract mode 1
      mode = 2;
      start_demo = 1;
      animation_sequence = 3; //demo sequence
      animation_steps = demo_3_steps; //demo steps
      break;
    case 99: //attract mode 0
      mode = 2;
      start_demo = 1;
      animation_sequence = 0; //demo sequence
      animation_steps = demo_0_steps; //demo steps
      break;
    default: //error mode
      //hault animations but don't return to static because that's what the real billboard does
      if(side == 0){
        animation_a_thread->enabled = false;
      } else {
        animation_b_thread->enabled = false;
      }
      return;
      break;
  }
  
  //if this is the next step in a demo then we force mode to 2
  if(demo_step == 1){
    mode = 2;
  }

  if(side == 0){ //side a
    if(start_demo == 1){
      disp_a_mode = mode;
      disp_a_demo_steps = animation_steps;
      disp_a_demo_sequence = animation_sequence;
      disp_a_demo_step = animation_steps;
      increment_demo_step(0);
    } else if(disp_a_mode != mode || animation_a_sequence != animation_sequence){
      disp_a_mode = mode;
      set_animation(side, animation_sequence, animation_steps, animation_speed);
    }
  } else { //side b
    if(start_demo == 1){
      disp_b_mode = mode;
      disp_b_demo_steps = animation_steps;
      disp_b_demo_sequence = animation_sequence;
      disp_a_demo_step = animation_steps;
      increment_demo_step(1);
    } else if(disp_b_mode != mode || animation_b_sequence != animation_sequence){
      disp_b_mode = mode;
      set_animation(side, animation_sequence, animation_steps, animation_speed);
    }
  }
  
}

void set_animation(boolean side, byte animation_sequence, unsigned int animation_steps, unsigned int animation_speed){
  if(side == 0){ //side a
    if(disp_a_mode != 0){
      animation_a_step = 0;
      animation_a_sequence = animation_sequence;
      animation_a_steps = animation_steps;
      animation_a_thread->setInterval(animation_speed);
      animation_a_thread->enabled = true;
      animation_a_callback();
    }
  } else { //side b
    if(disp_b_mode != 0){
      animation_b_step = 0;
      animation_b_sequence = animation_sequence;
      animation_b_steps = animation_steps;
      animation_b_thread->setInterval(animation_speed);
      animation_b_thread->enabled = true;
      animation_b_callback();
    }
  }
}


byte get_animation_step(byte animation_sequence, unsigned int animation_step){
  switch(animation_sequence){
    case 0:
      return animation_00[animation_step];
      break;
    case 1:
      return animation_01[animation_step];
      break;
    case 2:
      return animation_02[animation_step];
      break;
    case 3:
      return animation_03[animation_step];
      break;
    case 4:
      return animation_04[animation_step];
      break;
    case 5:
      return animation_05[animation_step];
      break;
    case 6:
      return animation_06[animation_step];
      break;
    case 7:
      return animation_07[animation_step];
      break;
    case 10:
      return animation_10[animation_step];
      break;
    case 20:
      return animation_20[animation_step];
      break;
    case 21:
      return animation_21[animation_step];
      break;
    case 22:
      return animation_22[animation_step];
      break;
    case 23:
      return animation_23[animation_step];
      break;
    case 24:
      return animation_24[animation_step];
      break;
  }
  return 0; //if invalid step just return blank
}


//increments the next animation in the demo loop
void increment_demo_step(boolean side){
  byte animation_sequence = 0;
  byte demo_sequence = 0;
  unsigned int next_step = 0;
  unsigned int demo_steps = 0;
  
  if(side == 0){ //side a
    next_step = disp_a_demo_step;
    demo_steps = disp_a_demo_steps;
    demo_sequence = disp_a_demo_sequence;
  } else { //side b
    next_step = disp_b_demo_step;
    demo_steps = disp_b_demo_steps;
    demo_sequence = disp_b_demo_sequence;
  }
  
  next_step++;
  if(demo_steps <= next_step){
    next_step = 0;
  }
  
  switch(demo_sequence){
    case 0:
      animation_sequence = demo_0[next_step];
      break;
    case 1:
      animation_sequence = demo_1[next_step];
      break;
    case 2:
      animation_sequence = demo_2[next_step];
      break;
    case 3:
      animation_sequence = demo_3[next_step];
      break;
  }

  if(side == 0){ //side a
    disp_a_demo_step = next_step;
    if(animation_a_sequence == animation_sequence){ //if the same animation is looped again
      animation_a_step = 0;
      animation_a_callback();
    } else { //if its a different animation
      config_animation(animation_sequence, side, 1);    
    }
  } else { //side b
    disp_b_demo_step = next_step;
    if(animation_b_sequence == animation_sequence){
      animation_b_step = 0;
      animation_b_callback();
    } else {
      config_animation(animation_sequence, side, 1);    
    }
  }
  
}


//Runs the next step in the animation
void animation_a_callback(){
  byte data_out_l = 0;
  byte data_out_r = 0;

  switch(disp_a_mode){
    case 0: //static mode, we shouldn't be here so return to static
      return_to_static(0);
      return;
      break;
    case 2: //todo: advance to next demo loop step
      if(animation_a_step >= animation_a_steps){
         increment_demo_step(0);
         return;
      }
      break;
    case 1: //if we've hit the end of the loop, start over
      if(animation_a_step >= animation_a_steps){
        animation_a_step = 0;
      }
      break;
  }

  data_out_l = get_animation_step(animation_a_sequence, animation_a_step);
  animation_a_step++;
  data_out_r = get_animation_step(animation_a_sequence, animation_a_step);
  animation_a_step++;
  shift_out(latch_al_pin, data_out_l);
  shift_out(latch_ar_pin, data_out_r);
}


//Runs the next step in the animation
void animation_b_callback(){
  byte data_out_l = 0;
  byte data_out_r = 0;

  switch(disp_b_mode){
    case 0: //static mode, we shouldn't be here so return to static
      return_to_static(1);
      return;
      break;
    case 2: //todo: advance to next demo loop step
      if(animation_b_step >= animation_b_steps){
         increment_demo_step(1);
         return;
      }
      break;
    case 1: //if we've hit the end of the loop, start over
      if(animation_b_step >= animation_b_steps){
        animation_b_step = 0;
      }
      break;
  }

  data_out_l = get_animation_step(animation_b_sequence, animation_b_step);
  animation_b_step++;
  data_out_r = get_animation_step(animation_b_sequence, animation_b_step);
  animation_b_step++;
  shift_out(latch_bl_pin, data_out_l);
  shift_out(latch_br_pin, data_out_r);
}
  



/* data shift out, based on the arduino shift out example */
void shift_out(int latch_pin, byte data_out) {
  // This shifts 8 bits out MSB first, 
  //on the rising edge of the clock,
  //clock idles low

  //internal function setup
  int i=0;
  int pin_state;

  //invert the output if necessary
  if(invert_disp_output == 1){
    data_out = ~data_out;
  }

  //clear everything out just in case to
  //prepare shift register for bit shifting
  digitalWrite(data_pin, 0);
  digitalWrite(clock_pin, 0);

  //activate latch
  digitalWrite(latch_pin, 0);

  //for each bit in the byte myDataOut?
  //NOTICE THAT WE ARE COUNTING DOWN in our for loop
  //This means that %00000001 or "1" will go through such
  //that it will be pin Q0 that lights. 
  for (i=7; i>=0; i--)  {
    digitalWrite(clock_pin, 0);

    //if the value passed to myDataOut and a bitmask result 
    // true then... so if we are at i=6 and our value is
    // %11010100 it would the code compares it to %01000000 
    // and proceeds to set pinState to 1.
    if ( data_out & (1<<i) ) {
      pin_state= 1;
    } else {
      pin_state= 0;
    }

    //Sets the pin to HIGH or LOW depending on pinState
    digitalWrite(data_pin, pin_state);
    //register shifts bits on upstroke of clock pin  
    digitalWrite(clock_pin, 1);
    //zero the data pin after shift to prevent bleed through
    digitalWrite(data_pin, 0);
  }

  //stop shifting
  digitalWrite(clock_pin, 0);
  digitalWrite(latch_pin, 1);
}


void debug_output(byte command){
  Serial.print(millis(), DEC);  //write out the time stamp
  Serial.print("|");
  Serial.print(command, BIN); //write out the binary command
  Serial.print("|");
  byte type = 0;
  switch(command >> 4) {
    case 0:
    case 1:
      Serial.write("P1 Right Char");
      type = 0;
      break;
    case 2:
    case 3:
      Serial.write("P1 Left Char");
      type = 0;
      break;
    case 4:
    case 5:
      Serial.write("P2 Right Char");
      type = 0;
      break;
    case 6:
    case 7:
      Serial.write("P2 Left Char");
      type = 0;
      break;
    case 8:
      Serial.write("Dots");
      type = 1;
      break;
    case 11:
      Serial.write("P1 Right Segment");
      type = 2;
      break;
    case 12:
      Serial.write("P1 Left Segment");
      type = 2;
      break;
    case 13:
      Serial.write("P2 Right Segment");
      type = 2;
      break;
    case 14:
      Serial.write("P2 Left Segment");
      type = 2;
      break;
    case 9:
      type = 3;
      break;
    case 10:
    case 15:
      type = 4;
      break;
    default:
      Serial.write("WTF!?\n");
      return;
  }

  
  switch (type){
    case 0: //character
      Serial.write(": ");
      Serial.write(digit_val[command & B00011111]);
      break;
    case 1: //dots
      Serial.write(": ");
      Serial.print((command&B00001111), BIN);
      break;
    case 2: //segments
      Serial.print((command&B00000111)+1, DEC);
      if((command&B00001000) == 0){
        Serial.write(": On");
      } else {
        Serial.write(": Off");
      }
      break;
    case 3: //winner lamps
      if((command&B00001000)==0){
        Serial.write("P1 WL: ");
      } else {
        Serial.write("P2 WL: ");
      }
      switch(command&B00000111){
        case 0:
          Serial.write(" On");
          break;
        case 1:
          Serial.write(" 512ms blink");
          break;
        case 2:
          Serial.write(" 256ms blink");
          break;
        case 3:
          Serial.write(" 128ms blink");
          break;
        case 4:
          Serial.write(" 205ms 2/5blink");
          break;
        case 5:
          Serial.write(" 205ms 3/5blink");
          break;
        case 6:
          Serial.write(" 205ms 4/5blink");
          break;
        case 7:
          Serial.write(" Off");
          break;
      }
      break;
    case 4: //animations
      if((command&B00001000)==0){
        Serial.write("P1 Animation: ");
      } else {
        Serial.write("P2 Animation: ");
      }
      if((command>>4)==15){
        switch(command&B00000111){
          case 0:
            Serial.write(" Demo Loop 1 (P1)");
            break;
          case 1:
            Serial.write(" Demo Loop 1 (P2)");
            break;
          case 2:
            Serial.write(" SEGA VERSUS CITY ADVANCED BATTLE CABINET text scroll");
            break;
          default:
            Serial.write(" HAULT");
            break;
        }
      } else {
        switch(command&B00000111){
          case 0:
            Serial.write(" SEGA text scroll");
            break;
          case 1:
            Serial.write(" Spinning Wheels");
            break;
          case 2:
            Serial.write(" The Wave");
            break;
          case 3:
            Serial.write(" Square Chase");
            break;
          case 4:
            Serial.write(" C Chase");
            break;
          case 5:
            Serial.write(" The Fountain");
            break;
          case 6:
            Serial.write(" The Snake");
            break;
          case 7:
            Serial.write(" Line Chase");
            break;
        }
        break;
     }
  }
  
  Serial.write("\n");
  return;
}
