// Arduino Uno용 엘리베이터 스케치 (3층 간단 시뮬레이터)
// - 외부 호출 버튼 3개 (각 버튼 옆에 호출 표시 LED 포함)
// - 엘리베이터 위치를 표시하는 층 LED 3개
// - 층간 이동을 표시하는 YELLOW LED 4개 (1-2 구간에 2개, 2-3 구간에 2개)
// - 상태 표시용 GREEN LED 1개 (하드웨어에 없으면 1층 LED 재사용)
// 버튼은 INPUT_PULLUP 방식(액티브 LOW)으로 연결되어 있다고 가정합니다.

// 핀 매핑 (배선이 다르면 이 부분을 수정하세요)
const int BTN_PINS[3]     = {11, 12, 13};    // call buttons for floor 1,2,3
const int BTN_LED[3]      = {8, 9, 10};    // indicator LEDs showing button was pressed
const int FLOOR_LED[3]    = {A0, 4, 7};   // RED LEDs indicating elevator is at floor 1,2,3
const int BETWEEN_LED[4]  = {2, 3, 5, 6}; // yellow LEDs: two for 1->2, two for 2->3
// No separate STATUS LED in hardware; reuse floor-1 LED pin (A0) as status indicator in code
const int STATUS_LED      = A0;         // green/status tied to floor 1 LED pin

// 타이밍(밀리초)
const unsigned long DEBOUNCE_MS = 50;
const unsigned long MOVE_STEP_MS = 350; // time per between-floor yellow LED step
const unsigned long DOOR_OPEN_MS = 1000;

// 엘리베이터 상태
int currentFloor = 0; // 0 기반 인덱스 (0 -> 1층)
bool requested[3] = {false, false, false};

// 입력 디바운스 처리
unsigned long lastBtnTime[3] = {0,0,0};
int lastBtnState[3] = {HIGH, HIGH, HIGH};
// 긴 누름(호출 취소) 판정용 누름 시작 시간
unsigned long pressStartMillis[3] = {0,0,0};
const unsigned long LONG_PRESS_MS = 1000; // >=1000ms 누르면 취소로 판정
bool canceledWhileHold[3] = {false, false, false};
// 취소 보호시간(글리치 방지): 취소 직후 짧게 재등록을 차단
const unsigned long CANCEL_PROTECT_MS = 500; // 밀리초
unsigned long cancelProtectUntil[3] = {0,0,0};
// UX: 취소 시 버튼 LED 깜빡임 피드백
const unsigned long BLINK_INTERVAL_MS = 100; // 깜빡임 온/오프 간격 (ms)
const int BLINK_PHASES = 4; // 깜빡임 단계 수 (2회 깜빡임 = 4 단계)
bool blinkActive[3] = {false, false, false};
unsigned long blinkToggleNext[3] = {0,0,0};
int blinkPhase[3] = {0,0,0};

// 이동 상태 머신
enum MoveState { IDLE, MOVING_UP, MOVING_DOWN, ARRIVING };
MoveState moveState = IDLE;
//int targetFloor = -1;
int moveStepIndex = 0; // 0..1 for each inter-floor (2 steps)
unsigned long moveStepMillis = 0;
unsigned long arrivedMillis = 0;
MoveState lastMoveDirection = IDLE; // remembers direction while arriving

void setup() {
  // 핀 초기화
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
  // 초기 층 표시
  updateFloorLeds();
}

void loop() {
  readButtons();
  updateBlinkStates();
  scheduleNextTargetIfIdle();
  runMovement();
}

// 버튼 LED 깜빡임 상태 머신 업데이트 (비차단)
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

// 버튼 읽기(디바운스 적용), 액티브 LOW
// 짧게 누름(빠르게 뗌) -> 호출 등록 (버튼 LED ON)
// 길게 누름(>= LONG_PRESS_MS) -> 호출 취소 (버튼 LED 깜빡임 후 OFF)
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

// 대기 상태(IDLE)인 경우 다음 이동 목표 선택 (간단한 정책: 가장 가까운 층, 동률이면 위 방향 우선)
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

// 엘리베이터 이동 처리 (비차단 상태머신)
void runMovement(){
  if (moveState == IDLE) return;

  unsigned long now = millis();

  if (moveState == MOVING_UP){
  // 사용할 층간 LED 인덱스 결정
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
    // 도착: 도어 열림 대기 시간 처리
    if (now - arrivedMillis >= DOOR_OPEN_MS){
      // finish arrival
      digitalWrite(STATUS_LED, LOW);
      // 도착 처리: 요청 소거 및 버튼 표시 LED 끄기
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

// 도착 처리: 층 LED 갱신, STATUS LED ON (문 열림 상태), ARRIVING 상태로 전환
void arriveAtFloor(){
  updateFloorLeds();
  digitalWrite(STATUS_LED, HIGH); // green on while doors open
  arrivedMillis = millis();
  moveState = ARRIVING;
  Serial.print("Arrived at floor "); Serial.println(currentFloor+1);
}

// 층 LED 상태 갱신: 현재층만 ON
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

