/*
 * Motion-Sensing House-Light System
 * By: Mark Freithaler
 * Authored On: 08/31/2017
 * Last Revised: 09/23/2017
 * Latest Revisions: Sleep Mode
 */
//Zone Indexes
#define ZONE_NONE     -1
#define ZONE_HALL     0
#define ZONE_DINING   1
#define ZONE_KITCHEN  2
#define ZONE_BATH     3
#define ZONE_BED      4
#define NUM_OF_ZONES  5 //Number of zones being used (Assuming that they are sequentially numbered from 0)

//Mode Indexes
#define MODE_SWITCHING  0 //Used to select the other modes
#define MODE_SLEEP      1 //Sleeps the system while the user sleeps. It awakes as soon as the motion in the central zone is detected
#define MODE_LOWPOWER   2 //Sleeps the system until the user awakens it
#define MODE_MANUAL     3 //Lets you manually set the zone by clicking the switch
#define MODE_SENSING    4 //Actively switches zones to follow the user
#define NUM_OF_MODES    5 //Total number of modes (Used for mode selection/cycling)
     
//Color Indexes
#define COLOR_NONE    0     
#define COLOR_RED     1
#define COLOR_ORANGE  2
#define COLOR_YELLOW  3
#define COLOR_GREEN   4
#define COLOR_CYAN    5
#define COLOR_BLUE    6
#define COLOR_VIOLET  7
#define NUM_OF_COLORS 8

//Possible Button States
#define BUTTON_UNPRESSED  0
#define BUTTON_CLICKED    1
#define BUTTON_IS_HELD    2

//Parameters
#define PERIOD_MS       5 //The frequency with which the system updates (Low -> double-triggering, High -> lag)
#define TRANSITION_MS   1000 //The perod after the hall-zone being HIGH, during which zone changes can occur
#define COOLDOWN_MS     7000 //The period after which a formerly active zone has to wait before being reactivated
#define SELECT_MS       2000 //Time spent sitting on a mode selection before it is selected
#define LED_BRIGHTNESS  42 //Scale of 0-42, for the mode indicator light
#define HOLD_THRESH_MS  2000 //Time to hold button before it considers it a hold instead of a single press
#define SLEEP_TIMER_MS  30000 //Time before the lights go out in sleep mode

#include <Adafruit_NeoPixel.h>
#include <LowPower.h>

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(1, A4, NEO_GRB + NEO_KHZ800);

int oldButtonPinState = HIGH;
int newButtonPinState = HIGH;
int switchPinVal = LOW;
int activeZone = -1;
int nextZone = -1;
int senseVal = 0;
int greatest = 0;
int transitionTimer = 0;
int lastZoneState[] = {LOW,LOW,LOW,LOW,LOW,LOW,LOW,LOW};
int currentZoneState[] = {LOW,LOW,LOW,LOW,LOW,LOW,LOW,LOW};
int selectTimer = -1;
int sleepTimer = 0;
int buttonPressTimer = 0;
int buttonState = 0;
bool transitioning = false;
bool selecting = false;
uint8_t nextMode = 0;
uint8_t mode = 0;
unsigned int zoneCooldown[] = {0,0,0,0,0,0,0,0};
unsigned int likelihood[] = {0,0,0,0,0,0,0,0};
static const uint8_t relayPins[] = {4,3,2,1,A0,A1,A2,A3};
static const uint8_t inputPins[] = {12,11,10,9,8,7,6,5};
static const uint8_t colorsRGB[][3] = {{0,0,0},{6,0,0},{4,2,0},{3,3,0},{0,6,0},{0,3,3},{0,0,6},{2,1,3}};

void turnZoneOn(int z){
  if(z < NUM_OF_ZONES && z > -1){
    digitalWrite(relayPins[z],LOW);
  }
  return;
}

void turnZoneOff(int z){
  if(z < NUM_OF_ZONES && z > -1){
    digitalWrite(relayPins[z],HIGH);
  }
  return;
}

void switchToZone(int z){
  if(activeZone != z){
    turnZoneOff(activeZone);
    activeZone = z;
    turnZoneOn(activeZone);
  }
  return;
}

int cycleToNextZone(int fromZone){
  int toZone = (fromZone+1)%NUM_OF_ZONES;
  switchToZone(toZone);
  return toZone;
}

void flashColor(uint8_t color, uint8_t count){ //Flashes the control box's LED (CURRENTLY NOT WORKING)
  color %= NUM_OF_COLORS;
  count %= 10;
  uint8_t r = (colorsRGB[color][0])*LED_BRIGHTNESS;
  uint8_t g = (colorsRGB[color][1])*LED_BRIGHTNESS;
  uint8_t b = (colorsRGB[color][2])*LED_BRIGHTNESS;
  for(int i = 0; i < count; i++){
    pixels.setPixelColor(0, r, g, b);
    pixels.show();
    delay(500);
    pixels.clear();
    pixels.show();
  }
  return;
}

void switchToColor(uint8_t color){ //Changes the color of the control-box's LED
  color %= NUM_OF_COLORS;
  uint8_t r = (colorsRGB[color][0])*LED_BRIGHTNESS;
  uint8_t g = (colorsRGB[color][1])*LED_BRIGHTNESS;
  uint8_t b = (colorsRGB[color][2])*LED_BRIGHTNESS;
  pixels.setPixelColor(0, r, g, b);
  pixels.show();
  return;
}

bool sensingMode(){ //Actively switches zones to follow movement
  //Refresh Inputs
  lastZoneState[0] = currentZoneState[0];
  for (int i = 0; i < NUM_OF_ZONES; i++){
    currentZoneState[i] = digitalRead(inputPins[i]);
    zoneCooldown[i] = decrementToZero(zoneCooldown[i], PERIOD_MS);
  }

  //Reset the choosing process
  if(inputRisingEdge(0) || inputFallingEdge(activeZone)){
    for (int i = 1; i < NUM_OF_ZONES; i++){
      likelihood[i] = 0;
    }
    greatest = 0;
  }
  
  //Update Likelihoods
  for (int i = 1; i < NUM_OF_ZONES; i++){
    if((currentZoneState[i] == HIGH) && (zoneCooldown[i] <= 0)){
      likelihood[i] += 1;
    }      
    if(likelihood[i] > (likelihood[greatest]+1)){ //The +1 s required so that turn-order does not influence the preference
      greatest = i;
    }
  }

  //UpdateTransition
  if(currentZoneState[0] == HIGH){
    transitionTimer = TRANSITION_MS;
  }
  transitionTimer = decrementToZero(transitionTimer, PERIOD_MS);
  
  //Zone Decision
  if((likelihood[greatest] > 3) && (zoneCooldown[greatest] <= 0) && (transitionTimer > 0)){
    zoneCooldown[activeZone] = COOLDOWN_MS;
    switchToZone(greatest);
    return true;
  }
  return false;
}

void sleepMode(){ //Sleeps the system until motion is detected
  int sum = 0;
  switch(activeZone){
    case ZONE_HALL: //Waiting for a timer to expire and then going to sleep
      if(sleepTimer <= 0){
        switchToColor(COLOR_NONE);
        switchToZone(ZONE_NONE);
      }else{
        sleepTimer -= PERIOD_MS;
      }
      break;
    case ZONE_NONE: //In sleep mode
      for(int i = 1; i < NUM_OF_ZONES; i++){ //This extra checking for active zones rules out a flase-triggerng in just ZONE_HALL
        if(digitalRead(inputPins[i]) == LOW){
          sum = 1;
          i = NUM_OF_ZONES;
        }
      }
      while((digitalRead(inputPins[0]) == LOW)&&(sum == 1)){
        LowPower.idle(SLEEP_2S, ADC_OFF, TIMER2_OFF, TIMER1_OFF, TIMER0_OFF, SPI_OFF, USART0_OFF, TWI_OFF);
      }
      mode = MODE_SENSING;
      break;
    default: //Case that starts off the sleep sequence
      sleepTimer = SLEEP_TIMER_MS;
      switchToColor(COLOR_VIOLET);
      switchToZone(ZONE_HALL);
      break;
  }
  return;
}

void lowPowerMode(){ //Mode during which it s leeps the system until the button is pressed
  switchToZone(ZONE_NONE);
  restUntilButton();
  switchToColor(MODE_SENSING);
  delay(1000);
  switchToColor(COLOR_NONE);
  mode = MODE_SENSING;
  return;
}

void manualMode(){ //Mode during which it sits on the first zone triggered
  if(buttonState == BUTTON_CLICKED){
    cycleToNextZone(activeZone);
  }
  /* The old code from when it was "Paused Mode" That stuck to the first zone it transitioned to
  if(sensingMode()){
    restUntilButton();
  }
  */
  return;
}

void switchingMode(){ //Mode during which a new mode can be selected using the button and LED
  switch(buttonState){
    case BUTTON_IS_HELD:
      switchToColor(COLOR_VIOLET);
      selectTimer = SELECT_MS;
      nextMode = 0;
      break;
    case BUTTON_CLICKED:
      nextMode = (nextMode+1)%NUM_OF_MODES;
      if(nextMode == 0){
        nextMode = 1;
      }
      switchToColor(nextMode%NUM_OF_COLORS);
      selectTimer = SELECT_MS;
      break;
    default:
      if(selectTimer > 0){
        selectTimer -= PERIOD_MS;
      }else{
        mode = nextMode;
        selecting = false;
        turnOffLED();
      }
      break;
  }
  return;
}

void restUntilButton(){ //Sleeps the system until the button is pressed
  while(digitalRead(A5) == HIGH){
    LowPower.idle(SLEEP_2S, ADC_OFF, TIMER2_OFF, TIMER1_OFF, TIMER0_OFF, SPI_OFF, USART0_OFF, TWI_OFF);
  }
  delay(500);
  do{
    delay(250);
  }while(digitalRead(A5) == LOW);
  return;
}

void turnOffLED(){
  pixels.clear();
  pixels.show();
  return;
}

bool inputFallingEdge(int zone){
  return (currentZoneState[zone] == LOW) && (lastZoneState[zone] == HIGH);
}

bool inputRisingEdge(int zone){
  return (currentZoneState[zone] == HIGH) && (lastZoneState[zone] == LOW);
}

int decrementToZero(int currentTimeLeft, int amountToDecrement){
  if(currentTimeLeft <= amountToDecrement){
    return 0;
  }else{
    return currentTimeLeft - amountToDecrement;
  }
}

void updateButtonState(){
  oldButtonPinState = newButtonPinState;
  newButtonPinState = digitalRead(A5);
  
  if(newButtonPinState == HIGH){ //The button is currently unpressed
    buttonPressTimer = 0;
    if (oldButtonPinState == LOW){//The button was just released
      oldButtonPinState = HIGH;
      buttonState = BUTTON_CLICKED; //There may be a need to add debouncing, depending on the switch being used and the integrity of its signaling
    }else{
      buttonState = BUTTON_UNPRESSED;
    }
  }else{ //The button is currently being held down
    buttonPressTimer += PERIOD_MS;
    if(buttonPressTimer > HOLD_THRESH_MS){
      buttonState = BUTTON_IS_HELD;
    }
    if (oldButtonPinState == HIGH){//The button was just depressed
      oldButtonPinState = LOW;
    }
  }
  return;
}

void setup(){
  for (int i = 0; i < NUM_OF_ZONES; i++){
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i],HIGH);
  }
  for (int i = 0; i < NUM_OF_ZONES; i++) {
    pinMode(inputPins[i], INPUT);
  }
  pinMode(A5, INPUT_PULLUP);
  pixels.begin();
  switchToZone(1);
  digitalWrite(13,LOW);
}

void loop(){
  switch(mode){
    case 0:
      switchingMode();
      break;
    case 1:
      sleepMode();
      break;
    case 2:
      lowPowerMode();
      break;
    case 3:
      manualMode();
      break;
    case 4:
      sensingMode();
      break;
    default:
      break;
  }

  updateButtonState();
  if(buttonState == BUTTON_IS_HELD){
    mode = MODE_SWITCHING;
  }

  //Period
  delay(PERIOD_MS);
}

