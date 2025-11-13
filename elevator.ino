// Elevator sketch for Arduino Uno (3-floor simple simulator)
// - 3 call buttons (with per-button LED)
// - 3 floor LEDs (indicate elevator at that floor)
// - 4 between-floor YELLOW LEDs (2 between 1-2, 2 between 2-3)
// - 1 status GREEN LED
// Button wiring assumed with INPUT_PULLUP (active LOW)

// Pin mapping (change if your wiring differs)
const int BTN_PINS[3]     = {2, 3, 4};    // call buttons for floor 1,2,3
const int BTN_LED[3]      = {5, 6, 7};    // indicator LEDs showing button was pressed
const int FLOOR_LED[3]    = {8, 9, 10};   // RED LEDs indicating elevator is at floor 1,2,3
const int BETWEEN_LED[4]  = {11, 12, 13, A0}; // yellow LEDs: two for 1->2, two for 2->3
const int STATUS_LED      = A1;          // green status LED

// Timing (ms)
const unsigned long DEBOUNCE_MS = 50;
const unsigned long MOVE_STEP_MS = 350; // time per between-floor yellow LED step
const unsigned long DOOR_OPEN_MS = 1000;

// elevator state
int currentFloor = 0; // 0-based (0 -> 1st floor)
bool requested[3] = {false, false, false};

// input debouncing
unsigned long lastBtnTime[3] = {0,0,0};
int lastBtnState[3] = {HIGH, HIGH, HIGH};
// press timing for long-press cancel
unsigned long pressStartMillis[3] = {0,0,0};
const unsigned long LONG_PRESS_MS = 1000; // hold 1000ms to cancel
bool canceledWhileHold[3] = {false, false, false};
// cancel protection (glitch prevention): after cancel, block re-registration briefly
const unsigned long CANCEL_PROTECT_MS = 500; // ms
unsigned long cancelProtectUntil[3] = {0,0,0};
// UX: blink feedback when cancel happens
const unsigned long BLINK_INTERVAL_MS = 100; // ms per on/off
const int BLINK_PHASES = 4; // on/off cycles (2 blinks = 4 phases)
bool blinkActive[3] = {false, false, false};
unsigned long blinkToggleNext[3] = {0,0,0};
int blinkPhase[3] = {0,0,0};

// movement state machine
enum MoveState { IDLE, MOVING_UP, MOVING_DOWN, ARRIVING };
MoveState moveState = IDLE;
//int targetFloor = -1;
int moveStepIndex = 0; // 0..1 for each inter-floor (2 steps)
unsigned long moveStepMillis = 0;
unsigned long arrivedMillis = 0;
MoveState lastMoveDirection = IDLE; // remembers direction while arriving

void setup() {
  // pins
  for (int i=0;i<3;i++) {
    pinMode(BTN_PINS[i], INPUT_PULLUP);
    pinMode(BTN_LED[i], OUTPUT);
    digitalWrite(BTN_LED[i], LOW);
    pinMode(FLOOR_LED[i], OUTPUT);
    digitalWrite(FLOOR_LED[i], LOW);
  }
  for (int i=0;i<4;i++) {
    pinMode(BETWEEN_LED[i], OUTPUT);
    digitalWrite(BETWEEN_LED[i], LOW);
  }
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);

  Serial.begin(9600);
  // show initial floor
  updateFloorLeds();
}

void loop() {
  readButtons();
  updateBlinkStates();
  scheduleNextTargetIfIdle();
  runMovement();
}

// Update blinking state machine for button LEDs (non-blocking)
void updateBlinkStates(){
  unsigned long now = millis();
  for (int i=0;i<3;i++){
    if (!blinkActive[i]) continue;
    if (now >= blinkToggleNext[i]){
      // toggle LED state
      bool on = (blinkPhase[i] % 2 == 0);
      digitalWrite(BTN_LED[i], on ? HIGH : LOW);
      blinkPhase[i]++;
      blinkToggleNext[i] = now + BLINK_INTERVAL_MS;
      if (blinkPhase[i] >= BLINK_PHASES){
        // end blinking, ensure LED off
        blinkActive[i] = false;
        digitalWrite(BTN_LED[i], LOW);
      }
    }
  }
}

// Read buttons with debounce; active LOW
// Short press (release quickly) => register request (turn on button LED)
// Long press (hold >= LONG_PRESS_MS then release) => cancel request (turn off button LED)
void readButtons(){
  for(int i=0;i<3;i++){
    int reading = digitalRead(BTN_PINS[i]);
    if (reading != lastBtnState[i]){
      lastBtnTime[i] = millis();
      lastBtnState[i] = reading;
    }
    if ((millis() - lastBtnTime[i]) > DEBOUNCE_MS){
      // stable state
      if (reading == LOW){
        // button currently pressed (held)
        if (pressStartMillis[i] == 0) pressStartMillis[i] = millis();
        // if held long enough, cancel immediately (once)
        unsigned long heldNow = millis() - pressStartMillis[i];
        if (heldNow >= LONG_PRESS_MS && !canceledWhileHold[i]){
          // perform cancel while still holding
          if (requested[i]){
            requested[i] = false;
            // start blink feedback sequence then ensure off
            unsigned long now = millis();
            blinkActive[i] = true;
            blinkPhase[i] = 1; // we set LED on immediately, so phase=1 means next toggle will turn it off
            digitalWrite(BTN_LED[i], HIGH);
            blinkToggleNext[i] = now + BLINK_INTERVAL_MS;
            // set protection window to avoid immediate re-registration
            cancelProtectUntil[i] = now + CANCEL_PROTECT_MS;
            Serial.print("Cancelled request while holding, floor "); Serial.println(i+1);
          } else {
            Serial.print("Hold detected but no active request to cancel on floor "); Serial.println(i+1);
          }
          canceledWhileHold[i] = true; // avoid repeating
        }
      } else {
        // button released
        if (pressStartMillis[i] != 0){
          unsigned long held = millis() - pressStartMillis[i];
          if (held < LONG_PRESS_MS){
            // short press -> set request (if not already requested)
            unsigned long now = millis();
            if (now < cancelProtectUntil[i]){
              Serial.print("Ignored quick re-press due to cancel-protect on floor "); Serial.println(i+1);
            } else {
              if (!requested[i]){
                requested[i] = true;
                digitalWrite(BTN_LED[i], HIGH); // indicate request
                Serial.print("Button pressed, floor "); Serial.println(i+1);
              } else {
                Serial.print("Button pressed but request already active for floor "); Serial.println(i+1);
              }
            }
          } else {
            // long press that already canceled while holding -> nothing to do on release
            if (canceledWhileHold[i]){
              // already handled
            } else {
              // edge: long press but cancel didn't trigger earlier (very unlikely) -> handle now
              if (requested[i]){
                requested[i] = false;
                unsigned long now = millis();
                blinkActive[i] = true;
                blinkPhase[i] = 1;
                digitalWrite(BTN_LED[i], HIGH);
                blinkToggleNext[i] = now + BLINK_INTERVAL_MS;
                cancelProtectUntil[i] = now + CANCEL_PROTECT_MS;
                Serial.print("Cancelled request on release (long press), floor "); Serial.println(i+1);
              }
            }
          }
          // reset press tracking
          pressStartMillis[i] = 0;
          canceledWhileHold[i] = false;
        }
      }
    }
  }
}

// If idle, pick next requested floor (simple policy: nearest, prefer up if tie)
void scheduleNextTargetIfIdle(){
  if (moveState != IDLE) return;
  int best = -1;
  int bestDist = 100;
  for (int i=0;i<3;i++){
    if (requested[i]){
      int d = abs(i - currentFloor);
      if (d < bestDist){ bestDist = d; best = i; }
    }
  }
  if (best >= 0){
    // choose direction: prefer moving toward the chosen floor
    if (best > currentFloor) moveState = MOVING_UP;
    else if (best < currentFloor) moveState = MOVING_DOWN;
    else {
      arriveAtFloor();
    }
    moveStepIndex = 0;
    moveStepMillis = millis();
    Serial.print("Starting move (from idle) towards floor "); Serial.println(best+1);
  }
}

void runMovement(){
  if (moveState == IDLE) return;

  unsigned long now = millis();

  if (moveState == MOVING_UP){
    // determine which between-LEDs to use
    int firstIdx = (currentFloor==0) ? 0 : 2; // 0..1 for 0->1, 2..3 for 1->2
    if (now - moveStepMillis >= MOVE_STEP_MS){
      // advance step
      // clear previous
      for (int i=0;i<4;i++) digitalWrite(BETWEEN_LED[i], LOW);
      // light current step
      digitalWrite(BETWEEN_LED[firstIdx + moveStepIndex], HIGH);
      moveStepIndex++;
      moveStepMillis = now;
      if (moveStepIndex >= 2){
        // arrived at next floor
        currentFloor += 1;
        // clear between leds
        for (int i=0;i<4;i++) digitalWrite(BETWEEN_LED[i], LOW);
        lastMoveDirection = MOVING_UP;
        arriveAtFloor();
      }
    }
  } else if (moveState == MOVING_DOWN){
    int firstIdx = (currentFloor==2) ? 2 : 0; // moving down uses same segments but in reverse
    if (now - moveStepMillis >= MOVE_STEP_MS){
      for (int i=0;i<4;i++) digitalWrite(BETWEEN_LED[i], LOW);
      // light step in reverse order for appearance
      int idx = firstIdx + (1 - moveStepIndex);
      digitalWrite(BETWEEN_LED[idx], HIGH);
      moveStepIndex++;
      moveStepMillis = now;
      if (moveStepIndex >= 2){
        currentFloor -= 1;
        for (int i=0;i<4;i++) digitalWrite(BETWEEN_LED[i], LOW);
        lastMoveDirection = MOVING_DOWN;
        arriveAtFloor();
      }
    }
  } else if (moveState == ARRIVING){
    // waiting for door open time
    if (now - arrivedMillis >= DOOR_OPEN_MS){
      // finish arrival
      digitalWrite(STATUS_LED, LOW);
      // clear floor LED after short blink
      // turn off request and its indicator
      // clear the request for this floor (if any)
      if (currentFloor >=0 && currentFloor < 3){
        requested[currentFloor] = false;
        digitalWrite(BTN_LED[currentFloor], LOW);
      }
      updateFloorLeds();

      // Decide next action: same-direction first (SCAN-like)
      bool above = pendingAbove();
      bool below = pendingBelow();
      if (lastMoveDirection == MOVING_UP){
        if (above){
          moveState = MOVING_UP;
          moveStepIndex = 0;
          moveStepMillis = now;
          Serial.println("Continuing UP to next request");
        } else if (below){
          moveState = MOVING_DOWN;
          moveStepIndex = 0;
          moveStepMillis = now;
          Serial.println("Reversing to DOWN to serve requests");
        } else {
          moveState = IDLE;
          Serial.print("Idle at floor "); Serial.println(currentFloor+1);
        }
      } else if (lastMoveDirection == MOVING_DOWN){
        if (below){
          moveState = MOVING_DOWN;
          moveStepIndex = 0;
          moveStepMillis = now;
          Serial.println("Continuing DOWN to next request");
        } else if (above){
          moveState = MOVING_UP;
          moveStepIndex = 0;
          moveStepMillis = now;
          Serial.println("Reversing to UP to serve requests");
        } else {
          moveState = IDLE;
          Serial.print("Idle at floor "); Serial.println(currentFloor+1);
        }
      } else {
        // if we were arriving without a prior direction (edge), pick nearest
        if (above){ moveState = MOVING_UP; moveStepIndex = 0; moveStepMillis = now; }
        else if (below){ moveState = MOVING_DOWN; moveStepIndex = 0; moveStepMillis = now; }
        else { moveState = IDLE; Serial.print("Idle at floor "); Serial.println(currentFloor+1); }
      }
    }
  }
}

void arriveAtFloor(){
  // indicate arrival
  updateFloorLeds();
  digitalWrite(STATUS_LED, HIGH); // green on while doors open
  arrivedMillis = millis();
  moveState = ARRIVING;
  Serial.print("Arrived at floor "); Serial.println(currentFloor+1);
}

void updateFloorLeds(){
  for (int i=0;i<3;i++){
    digitalWrite(FLOOR_LED[i], (i==currentFloor) ? HIGH : LOW);
  }
}

// helpers to check pending requests above/below currentFloor
bool pendingAbove(){
  for (int i=currentFloor+1;i<3;i++) if (requested[i]) return true;
  return false;
}

bool pendingBelow(){
  for (int i=0;i<currentFloor;i++) if (requested[i]) return true;
  return false;
}

