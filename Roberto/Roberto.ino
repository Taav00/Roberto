/***************************************************************************************
 *
 * Title:       Roberto, the pastis serving robot
 * Version:     v1.0
 * Date:        2014-05-27
 * Author:      Karl Kangur <karl.kangur@gmail.com>
 * Website:     https://github.com/Robopoly/Roberto
 * Licence:     LGPL
 *
 ***************************************************************************************/
#include <Servo.h>
#include <Wire.h>
#include <EEPROM.h>
#include "Roberto.h"
#include <robopolyLCD.h>

Servo servoEarLeft;
Servo servoEarRight;
Servo servoBowTie;
Servo servoArmLeft;
Servo servoArmRight;

// buffer for user input
char buffer[16];

// transition structure
struct transition
{
    enum state_codes state_source;
    enum return_codes return_code;
    enum state_codes state_destination;
};

// state functions and codes must be in sync
return_codes (*state[])(void) = {
  stateWaitingCup,
  stateMovingArmsIn,
  statePouring,
  stateMovingRightArmOut,
  stateRemoveCup,
  stateMovingArmsOut
};

struct transition state_transitions[] = {
  {STATE_WAITING_CUP,          OK,     STATE_MOVING_ARMS_IN},
  {STATE_WAITING_CUP,          REPEAT, STATE_WAITING_CUP},
  {STATE_MOVING_ARMS_IN,       OK,     STATE_POURING},
  {STATE_MOVING_ARMS_IN,       FAIL,   STATE_MOVING_ARMS_OUT},
  {STATE_POURING,              OK,     STATE_MOVING_RIGHT_ARM_OUT},
  {STATE_POURING,              FAIL,   STATE_MOVING_ARMS_OUT},
  {STATE_MOVING_RIGHT_ARM_OUT, OK,     STATE_REMOVE_CUP},
  {STATE_REMOVE_CUP,           OK    , STATE_MOVING_ARMS_OUT},
  {STATE_REMOVE_CUP,           REPEAT, STATE_REMOVE_CUP},
  {STATE_MOVING_ARMS_OUT,      OK,     STATE_WAITING_CUP}
};

enum state_codes currentState;
enum return_codes returnCode;

// pointer to the current called function in the state machine
return_codes (*stateFunction)(void);

// time from start of liquid pouring in milliseconds
uint16_t pouringTime;
uint8_t liquidType;
uint8_t servingMode;
uint8_t debugMode;

void setup()
{
  // light up the eyes in red (debug)
  eyes(1, 0, 0);
  
  // set up serial communication for debug and configuration
  Serial.begin(9600);
  
  // configure servo motors
  servoArmLeft.attach(4);
  servoArmRight.attach(5);
  servoBowTie.attach(6);
  servoEarRight.attach(8);
  servoEarLeft.attach(7);
  
  // initialise servo positions
  servoArmLeft.write(SERVO_ARM_LEFT_OPEN);
  servoArmRight.write(SERVO_ARM_RIGHT_OPEN);
  servoBowTie.write(SERVO_BOWTIE_CENTER);
  servoEarRight.write(SERVO_EAR_RIGHT_CENTER);
  servoEarLeft.write(SERVO_EAR_LEFT_CENTER);
  
  // set port modes
  pinMode(LED, OUTPUT);
  pinMode(OUTPUT_PUMP, OUTPUT);
  pinMode(OUTPUT_EYE_LEFT_RED, OUTPUT);
  pinMode(OUTPUT_EYE_LEFT_GREEN, OUTPUT);
  pinMode(OUTPUT_EYE_LEFT_BLUE, OUTPUT);
  pinMode(OUTPUT_EYE_RIGHT_RED, OUTPUT);
  pinMode(OUTPUT_EYE_RIGHT_GREEN, OUTPUT);
  pinMode(OUTPUT_EYE_RIGHT_BLUE, OUTPUT);
  
  // load data from memory
  pouringTime = (EEPROM.read(EEPROM_POURING_TIME) << 8) | EEPROM.read(EEPROM_POURING_TIME + 1);
  liquidType = EEPROM.read(EEPROM_TYPE);
  servingMode = EEPROM.read(EEPROM_MODE);
  
  // start i2c bus for LCD screen
  Wire.begin();
  
  // initialise LCD screen
  lcd("Initialising", "systems...");
  
  // initialise robot state
  currentState = ENTRY_STATE;
  
  // no debug mode
  debugMode = 0;
  
  // normal eye color
  eyes(0, 1, 1);
}

void loop()
{
  // debug mode shows the sensor values on lcd
  if(debugMode)
  {
    if(debugMode == 1)
    {
      // detach servo motors so that they could be moved freely
      servoArmLeft.detach();
      servoArmRight.detach();
      servoBowTie.detach();
      servoEarRight.detach();
      servoEarLeft.detach();
  
      debugMode = 2;
    }
    
    if(Serial.available())
    {
      char c = Serial.read();
      while(Serial.available())
      {
        Serial.read();
      }
      switch(c)
      {
        case '1':
          Serial.println("Pump on");
          MACRO_PUMP_ON;
          break;
        case '2':
          Serial.println("Pump off");
          MACRO_PUMP_OFF;
          break;
        case '3':
          Serial.println("LEDs on");
          eyes(1, 1, 1);
          break;
        case '4':
          Serial.println("LEDs off");
          eyes(0, 0, 0);
          break;
        default:
          Serial.println("Debug mode:\n1: pump on\n2: pump off\n3: leds on\n4: leds off\nReset MCU to quit");
      }
    }
    
    String line1 = "CUP: ";
    line1 += digitalRead(INPUT_CUP);
    line1 += ", FLOW: ";
    line1 += digitalRead(INPUT_FLOW);
    
    String line2 = "BTN: ";
    line2 += digitalRead(INPUT_BUTTON);
    line2 += ", TOUCH: ";
    line2 += digitalRead(INPUT_ARM_CONTACT);
    
    lcd(line1.c_str(), line2.c_str());
    delay(200);
    return;
  }
  // current function to call according to the state machine
  stateFunction = state[currentState];
  // actually call the function
  returnCode = stateFunction();
  // fetch next state
  currentState = lookupTransitions(currentState, returnCode);
  
  if(Serial.available())
  {
    char c = Serial.read();
    while(Serial.available())
    {
      Serial.read();
    }
    switch(c)
    {
      // clear input (typically the remaining "\n" character)
      case '1':
        // clear the input buffer
        memset(buffer, 0, sizeof(buffer));
        // wait for user input, 16 character or 15 and a "\n" termination character
        Serial.setTimeout(5000);
        Serial.println("Enter new pouring time in milliseconds");
        Serial.readBytesUntil('\n', buffer, 15);
        pouringTime = atoi(buffer);
        EEPROM.write(EEPROM_POURING_TIME, pouringTime >> 8);
        EEPROM.write(EEPROM_POURING_TIME + 1, pouringTime & 0xff);
        Serial.print("New pouring time set ");
        Serial.print(pouringTime);
        Serial.println("ms");
        Serial.setTimeout(1000);
        break;
      case '2':
        Serial.print("Current pouring time: ");
        Serial.print((EEPROM.read(EEPROM_POURING_TIME) << 8) | EEPROM.read(EEPROM_POURING_TIME + 1));
        Serial.println("ms");
        Serial.print("Serving mode: ");
        Serial.println(EEPROM.read(EEPROM_MODE));
        Serial.print("Serving type: ");
        Serial.println(EEPROM.read(EEPROM_TYPE));
        break;
      case '3':
        Serial.print("The pump has been activated ");
        Serial.print((EEPROM.read(EEPROM_PUMP_ACTIVATION) << 8) | EEPROM.read(EEPROM_PUMP_ACTIVATION + 1));
        Serial.println(" times");
        break;
      case '4':
        Serial.print("Sensors: CUP=");
        Serial.print(digitalRead(INPUT_CUP));
        Serial.print(", ARM_CONTACT=");
        Serial.print(digitalRead(INPUT_ARM_CONTACT));
        Serial.print(", FLOW=");
        Serial.println(digitalRead(INPUT_FLOW));
        break;
      case '5':
        Serial.println("Change serving mode: 0 = manual (must click on button), 1 = automatic");
        memset(buffer, 0, sizeof(buffer));
        Serial.setTimeout(5000);
        Serial.readBytesUntil('\n', buffer, 15);
        servingMode = atoi(buffer);
        EEPROM.write(EEPROM_MODE, servingMode);
        Serial.println("New serving mode set");
        Serial.setTimeout(1000);
        break;
      case '6':
        Serial.println("Change liquid type: 0 = Ricard, 1 = syrup");
        memset(buffer, 0, sizeof(buffer));
        Serial.setTimeout(5000);
        Serial.readBytesUntil('\n', buffer, 15);
        liquidType = atoi(buffer);
        EEPROM.write(EEPROM_TYPE, liquidType);
        Serial.println("New liquid type set");
        Serial.setTimeout(1000);
        break;
      case '7':
        Serial.println("Entering debug mode, reset MCU to quit.");
        debugMode = 1;
        break;
      default:
        Serial.println("Roberto configuration:\n1: set new pouring time\n2: check configuration\n3: read how many times the pump has been activated\n4: show sensor values\n5: change mode (automatic/manual)\n6: change type (Ricard, syrup)\n7: Enable debug mode");
    }
  }
}

// returns the new state according to the current state and the return value
state_codes lookupTransitions(state_codes state, return_codes code)
{
  uint8_t i;
  // default return state is the entry state
  state_codes nextState = ENTRY_STATE;
  // see if a state transition matches and switch to the next state
  for(i = 0; i < sizeof(state_transitions) / sizeof(transition); i++)
  {
    if(state_transitions[i].state_source == state && state_transitions[i].return_code == code)
    {
      nextState = state_transitions[i].state_destination;
      break;
    }
  }
  return nextState;
}

return_codes stateWaitingCup()
{
  // timer to animate the robot while idle
  static uint32_t lcdUpdateTime = 0;
  static uint8_t idleState = 0;
  if(millis() > lcdUpdateTime)
  {
    switch(idleState)
    {
      case 0:
        lcd("Salut, je suis", "Roberto");
        break;
      case 1:
        lcd("Le robot serveur", "de Robopoly");
        break;
      case 2:
        if(liquidType == TYPE_RICARD)
        {
          lcd("Je sers du", "RICARD");
        }
        else if(liquidType == TYPE_SYRUP)
        {
          lcd("Je sers du", "sirop");
        }
        break;
      case 3:
        if(liquidType == TYPE_RICARD)
        {
          lcd("Pour seulement", "7 petits francs");
        }
        else if(liquidType == TYPE_SYRUP)
        {
          lcd("Et c'est", "gratuit");
        }
        break;
    }
    
    Serial.println("State: waiting for cup");
    
    // toggle the next state, modulo 4
    idleState = (idleState + 1) & 0b11;
    
    lcdUpdateTime = millis() + TIME_LCD_UPDATE;
  }
  
  if(COND_ARMS_TOUCH)
  {
    uint32_t configurationTextChangeDelay = 0;
    char message[16];
    uint8_t showConfigState = 0;
    while(COND_ARMS_TOUCH)
    {
      if(millis() > configurationTextChangeDelay)
      {
        switch(showConfigState)
        {
          case 0:
            itoa((EEPROM.read(EEPROM_PUMP_ACTIVATION) << 8) | EEPROM.read(EEPROM_PUMP_ACTIVATION + 1), message, 10);
            lcd(message, "fois servis");
            break;
          case 1:
            itoa(pouringTime, message, 10);
            lcd(message, "temps de debit");
            break;
        }
        showConfigState = (showConfigState + 1) & 1;
        
        configurationTextChangeDelay = millis() + TIME_LCD_UPDATE;
      }
    }
  }
  
  // a human blinks every 2 to 10 seconds and a blink is about 200ms in duration
  static uint32_t nextBlink = millis() + 2000 * random(1, 5);
  if(millis() > nextBlink)
  {
    eyes(0, 0, 0);
    delay(200);
    eyes(0, 1, 1);
    nextBlink = millis() + 2000 * random(1, 5);
  }
  
  static uint32_t ledToggleTime = millis() + TIME_HEARTBEAT;
  if(millis() > ledToggleTime)
  {
    // toggle the onboard LED
    digitalWrite(LED, !digitalRead(LED));
    ledToggleTime = millis() + TIME_HEARTBEAT;
  }
  
  static uint32_t bowTieMoveTime = millis() + TIME_BOWTIEMOVE * random(1, 10);
  if(millis() > bowTieMoveTime)
  {
    // toggle the onboard LED
    servoBowTie.write(random(SERVO_BOWTIE_LEFT, SERVO_BOWTIE_RIGHT));
    bowTieMoveTime = millis() + TIME_BOWTIEMOVE * random(1, 10);
  }
  
  // ear movement
  static uint32_t earMoveTime = millis() + TIME_EAR_MOVE_IDLE * random(1, 10);
  if(millis() > earMoveTime)
  {
    servoEarRight.write(random(min(SERVO_EAR_RIGHT_BACK, SERVO_EAR_RIGHT_FORWARDS), max(SERVO_EAR_RIGHT_BACK, SERVO_EAR_RIGHT_FORWARDS)));
    servoEarLeft.write(random(min(SERVO_EAR_LEFT_BACK, SERVO_EAR_LEFT_FORWARDS), max(SERVO_EAR_LEFT_BACK, SERVO_EAR_LEFT_FORWARDS)));
    
    earMoveTime = millis() + TIME_EAR_MOVE_IDLE * random(1, 10);
  }
  
  // fast disco mode
  static uint32_t discoModeTime = millis() + TIME_DISCO_MODE;
  static uint32_t discoModeDuration = discoModeTime + TIME_DISCO_DURATION;
  if(millis() > discoModeTime)
  {
    if(millis() < discoModeDuration)
    {
      eyesRandom();
    }
    else
    {
      eyes(0, 1, 1);
      discoModeTime = millis() + TIME_DISCO_MODE;
      discoModeDuration = discoModeTime + TIME_DISCO_DURATION;
    }
  }
  
  if(COND_CUP_IS_PRESENT)
  {
    // turn eyes white
    eyes(1, 1, 1);
    
    servoEarLeft.write(SERVO_EAR_LEFT_CENTER);
    servoEarRight.write(SERVO_EAR_RIGHT_CENTER);
    
    if(servingMode == MODE_MANUAL)
    {
      lcd("Goblet insere", "Payer l'humain");
      while(!COND_BUTTON_PRESSED)
      {
        if(!COND_CUP_IS_PRESENT)
        {
          return REPEAT;
        }
      }
      // the cup was removed so simply return to the idle state
      return OK;
    }
    else if(servingMode == MODE_AUTOMATIC)
    {
      lcd("Goblet insere", "");
      return OK;
    }
  }
  return REPEAT;
}

return_codes stateMovingArmsIn()
{
  Serial.println("State: moving arms in");
  
  if(liquidType == TYPE_RICARD)
  {
    lcd("Et un Ricard", "un vrai!");
  }
  else if(liquidType == TYPE_SYRUP)
  {
    lcd("Et voila un", "bon sirop");
  }
  
  // do not move right away
  uint32_t waitAfterCut = millis() + TIME_WAIT_AFTER_CUP_INSERT;
  while(waitAfterCut > millis())
  {
    if(!COND_CUP_IS_PRESENT)
    {
      lcd("Erreur: goblet", "enleve");
      Serial.println("Error: cup was removed");
      return FAIL;
    }
  }
  
  // move arms in
  moveArmServo(servoArmLeft, SERVO_ARM_LEFT_CLOSED, SERVO_ARM_SPEED);
  moveArmServo(servoArmRight, SERVO_ARM_RIGHT_CLOSED, SERVO_ARM_SPEED);
  
  uint32_t timeout = millis() + 3000;
  while(!COND_ARMS_TOUCH)
  {
    // arms can be blocked, so a timeout is needed
    if(millis() > timeout)
    {
      lcd("Erreur: capteur", "bras");
      Serial.println("Error: timeout while moving arms in");
      return FAIL;
    }
    
    if(!COND_CUP_IS_PRESENT)
    {
      lcd("Erreur: goblet", "enleve");
      Serial.println("Error: cup was removed");
      return FAIL;
    }
  }
  
  return OK;
}

return_codes statePouring()
{
  // the liquid has reached the flow sensor, start timing, make sure cup is still there
  uint32_t start = millis();
  uint32_t time = start + pouringTime;
  uint32_t bowTieTime = start + TIME_BOWTIE_TOGGLE;
  uint32_t now;
  uint8_t percent;
  char progress[4];
  
  LED_OFF;
  
  Serial.println("State: pouring");
  eyes(0, 1, 0);
  
  MACRO_PUMP_ON;

  // timeout if there's no liquid after some time
  uint32_t timeout = millis() + TIME_LIQUID_SENSOR_REACH;
  while(!COND_LIQUID_DETECT)
  {
    // arms can be blocked, so a timeout is needed
    if(millis() > timeout)
    {
      eyes(1, 0, 0);
      MACRO_PUMP_OFF;
      lcd("Erreur: timeout", "liquide");
      Serial.println("Error: timeout while pumping liquid");
      return FAIL;
    }
    
    // check for cup presence
    if(!COND_CUP_IS_PRESENT)
    {
      eyes(1, 0, 0);
      MACRO_PUMP_OFF;
      lcd("Erreur: goblet", "enleve");
      Serial.println("Error: cup was removed");
      delay(TIME_EAR_MOVE_CUP_REMOVE);
      return FAIL;
    }
  }
  
  // turn on the LED to indicate when the sensor detected the liquid
  LED_ON;
  
  // actually start pouring
  start = millis();
  time = start + pouringTime;
  bowTieTime = start + TIME_BOWTIE_TOGGLE;
  while((now = millis()) < time)
  {
    // check for cup presence
    if(!COND_CUP_IS_PRESENT)
    {
      eyes(1, 0, 0);
      // stop pump
      MACRO_PUMP_OFF;
      // reset bow tie
      servoBowTie.write(SERVO_BOWTIE_CENTER);
      lcd("Erreur: goblet", "enleve");
      Serial.println("Error: cup was removed");
      // wait for drip
      delay(TIME_EAR_MOVE_CUP_REMOVE);
      return FAIL;
    }
    
    // move the bow tie
    if(now > bowTieTime)
    {
      if(servoBowTie.read() == SERVO_BOWTIE_RIGHT)
      {
        servoBowTie.write(SERVO_BOWTIE_LEFT);
      }
      else
      {
        servoBowTie.write(SERVO_BOWTIE_RIGHT);
      }
      
      // display feedback on lcd: in % when pouring and timeout in case of fail
      percent = 100 * (now - start) / pouringTime;
      memset(progress, 0, sizeof(progress));
      itoa(percent, progress, 10);
      if(percent < 10)
      {
        progress[1] = '%';
      }
      else if(percent < 100)
      {
        progress[2] = '%';
      }
      else
      {
        progress[3] = '%';
      }
      
      lcd("Preparation", progress);
      Serial.print("Info: ");
      Serial.println(progress);
      
      // also toggle the led
      digitalWrite(LED, !digitalRead(LED));
      bowTieTime = millis() + TIME_BOWTIE_TOGGLE;
    }
  }
  
  lcd("Preparation", "100%");
  
  // shut off the pump
  MACRO_PUMP_OFF;
  
  // turn the LED off to show when exactly the pump was turned off
  LED_OFF;
  
  // put bow tie back to center
  servoBowTie.write(SERVO_BOWTIE_CENTER);
  
  // increment poured times counter
  uint16_t pouredTimes = (EEPROM.read(EEPROM_PUMP_ACTIVATION) << 8) | EEPROM.read(EEPROM_PUMP_ACTIVATION + 1);
  pouredTimes++;
  EEPROM.write(EEPROM_PUMP_ACTIVATION, pouredTimes >> 8);
  EEPROM.write(EEPROM_PUMP_ACTIVATION + 1, pouredTimes & 0xff);
  
  // wait a little bit for the drip  uint32_t start = millis();
  lcd("Attends un peu", "...");
  delay(TIME_DRIP_WAIT);
  
  return OK;
}

return_codes stateMovingRightArmOut()
{
  eyes(0, 0, 1);
  Serial.println("State: moving right arm out");
  
  // display information on the lcd
  lcd("C'est pret!", "...");
  
  moveArmServo(servoArmLeft, SERVO_ARM_LEFT_HALF_OPEN, SERVO_ARM_SPEED);
  moveArmServo(servoArmRight, SERVO_ARM_RIGHT_OPEN, SERVO_ARM_SPEED);
  
  return OK;
}

return_codes stateRemoveCup()
{
  eyes(0, 1, 1);
  Serial.println("State: remove cup");
  // flash eyes and display information on lcd for the customer
  
  // sometimes the user can drop the cup back in the hand, so a timeout is needed
  uint32_t timeout = millis() + TIME_WAIT_AFTER_CUP_REMOVE;
  uint32_t lcdUpdateTime = millis() + TIME_LCD_UPDATE;
  uint32_t ledDiscoTime = millis() + TIME_LED_DISCO;
  uint8_t idleState = 0;
  
  uint32_t moveEars = 0;
  uint32_t moveEarsStep = 0;
  uint8_t moveEarsState = 0;
    
  while(timeout > millis())
  {
    // cup is still in the robot hand
    if(COND_CUP_IS_PRESENT)
    {
      timeout = millis() + TIME_WAIT_AFTER_CUP_REMOVE;
    }
    
    // update the lcd
    if(lcdUpdateTime < millis())
    {
      switch(idleState)
      {
        case 0:
          lcd("Tu peux prendre", "le verre");
          break;
        case 1:
          lcd("Merci et SANTE!", ":)");
          break;
      }
      
      eyesRandom();
      
      // toggle the next state, modulo 4
      idleState = (idleState + 1) & 0b1;
      
      lcdUpdateTime = millis() + TIME_LCD_UPDATE;
    }
    
    if(millis() > moveEars)
    {
      if(millis() > moveEarsStep)
      {
        switch(moveEarsState)
        {
          case 0:
            servoEarLeft.write(SERVO_EAR_LEFT_FORWARDS);
            servoEarRight.write(SERVO_EAR_RIGHT_FORWARDS);
            moveEarsStep = millis() + TIME_EAR_MOVE_CUP_REMOVE_STEP;
            moveEarsState++;
            break;
          case 1:
            servoEarLeft.write(SERVO_EAR_LEFT_CENTER);
            servoEarRight.write(SERVO_EAR_RIGHT_CENTER);
            moveEarsStep = millis() + TIME_EAR_MOVE_CUP_REMOVE_STEP;
            moveEarsState++;
            break;
          case 2:
            servoEarLeft.write(SERVO_EAR_LEFT_FORWARDS);
            servoEarRight.write(SERVO_EAR_RIGHT_FORWARDS);
            moveEarsStep = millis() + TIME_EAR_MOVE_CUP_REMOVE_STEP;
            moveEarsState++;
            break;
          case 3:
            servoEarLeft.write(SERVO_EAR_LEFT_CENTER);
            servoEarRight.write(SERVO_EAR_RIGHT_CENTER);
            moveEarsState = 0;
            moveEars = millis() + TIME_EAR_MOVE_CUP_REMOVE;
            moveEarsStep = 0;
            break;
        }
      }
    }
    
    if(ledDiscoTime < millis())
    {
      eyesRandom();
      ledDiscoTime = millis() + TIME_LED_DISCO;
    }
  }
  
  // display remaining time for next state on lcd (arm moving away)
  
  return OK;
}

return_codes stateMovingArmsOut()
{
  eyes(1, 1, 0);
  lcd("Beep, boop", "...");
  Serial.println("State: moving arms out");
  
  // move both arms to idle position
  moveArmServo(servoArmRight, SERVO_ARM_RIGHT_OPEN, SERVO_ARM_SPEED);
  moveArmServo(servoArmLeft, SERVO_ARM_LEFT_OPEN, SERVO_ARM_SPEED);
  
  return OK;
}

void lcd(const char* firstLine, const char* secondLine)
{
  // the LCD functions need a small delay or it won't work
  clearLCD();
  printLCD((char*)firstLine, START);
  delay(1);
  printLCD((char*)secondLine, START_2ND_LINE);
  delay(1);
}

void eyes(uint8_t red, uint8_t green, uint8_t blue)
{
  digitalWrite(OUTPUT_EYE_LEFT_RED, red);
  digitalWrite(OUTPUT_EYE_LEFT_GREEN, green);
  digitalWrite(OUTPUT_EYE_LEFT_BLUE, blue);
  digitalWrite(OUTPUT_EYE_RIGHT_RED, red);
  digitalWrite(OUTPUT_EYE_RIGHT_GREEN, green);
  digitalWrite(OUTPUT_EYE_RIGHT_BLUE, blue);
}

void eyesRandom()
{
  uint8_t rnd = random(0, 255);
  digitalWrite(OUTPUT_EYE_LEFT_RED, (rnd >> 0) & 1);
  digitalWrite(OUTPUT_EYE_LEFT_GREEN, (rnd >> 1) & 1);
  digitalWrite(OUTPUT_EYE_LEFT_BLUE, (rnd >> 2) & 1);
  digitalWrite(OUTPUT_EYE_RIGHT_RED, (rnd >> 3) & 1);
  digitalWrite(OUTPUT_EYE_RIGHT_GREEN, (rnd >> 4) & 1);
  digitalWrite(OUTPUT_EYE_RIGHT_BLUE, (rnd >> 5) & 1);
}

void moveArmServo(Servo servo, uint8_t angle, uint8_t speed)
{
  uint8_t newAngle;
  int8_t dir = angle - servo.read() > 0 ? 1 : -1;
  for(newAngle = servo.read(); newAngle != angle; newAngle += dir)
  {
    servo.write(newAngle);
    delay(speed);
  }
}

