const int BTN_PINS[3]     = {11, 12, 13};
const int BTN_LED[3]      = {8, 9, 10};
const int FLOOR_LED[3]    = {A0, 4, 7};
const int BETWEEN_LED[4]  = {2, 3, 5, 6};

const unsigned long MOVE_STEP_MS = 500;   // 층 사이를 이동하는 데 걸리는 시간
const unsigned long DOOR_OPEN_MS = 1500;  // 문이 열려 있는 시간

int currentFloor = 0;  // 현재 엘리베이터 층(0,1,2)
bool requested[3] = {false, false, false};  // 각 층 호출 플래그

int lastBtnState[3] = {LOW, LOW, LOW};              // 버튼의 마지막 입력값(버튼 입력 변화 감지)
unsigned long pressStartMillis[3] = {0,0,0};        // 버튼 누름 시작 시각

const unsigned long LONG_PRESS_MS = 1000;           // 롱프레스(취소) 인식 시간
bool canceledWhileHold[3] = {false, false, false};  // 누름 중 취소 처리 여부(누른후 LONG_PRESS_MS가 지나면, true로 설정되어 중복 취소 방지)
bool pressStartedWithActiveRequest[3] = {false, false, false}; // 누름 시작 시 요청 활성화 여부("요청이 이미 활성화된 상태에서만" 취소가 동작하도록 하기 위해)
const unsigned long CANCEL_PROTECT_MS = 1000;       // 취소 보호 시간(롱프레스로 취소한 뒤, 일정 시간 동안은 같은 버튼에 대해 다시 요청을 등록할 수 없게)
unsigned long cancelProtectUntil[3] = {0,0,0};      // 취소 보호 종료 시각(now + CANCEL_PROTECT_MS)
const unsigned long BLINK_INTERVAL_MS = 100;        // LED 깜빡임 간격(ms)
const int BLINK_PHASES = 4;                         // LED 깜빡임 횟수
bool blinkActive[3] = {false, false, false};        // LED 깜빡임 활성화 여부
unsigned long blinkToggleNext[3] = {0,0,0};         // LED 다음 토글 시각
int blinkPhase[3] = {0,0,0};                        // LED 깜빡임 단계

int lastBtnLedState[3] = {LOW, LOW, LOW};           // 버튼 LED의 마지막 상태
int lastBetweenLedState[4] = {LOW, LOW, LOW, LOW};  // 층간 LED의 마지막 상태


enum MoveState { STOPPED, MOVING_UP, MOVING_DOWN, ARRIVING };  // 상태머신
MoveState moveState = STOPPED;                // 현재 엘리베이터 상태(STOPPED/MOVING/ARRIVING)
int betweenIdx = 0;                           // 층간 이동 애니메이션 단계(BETWEEN_LED[4] = {2, 3, 5, 6} 중에서 0,1,2 단계로 구분)
unsigned long betweenLastMillis = 0;          // 마지막 층간 애니메이션 갱신 시각(현재 시각(millis())과 betweenLastMillis의 차이가 MOVE_STEP_MS 이상이 되면 다음 애니메이션 단계로)
unsigned long arrivedUntil = 0;               // 도착(ARRIVING) 상태 유지 종료 시각(문이 열려 있는 시간(DOOR_OPEN_MS 등)을 계산하여 설정, 현재 시각(millis())이 arrivedUntil에 도달하면 도착 상태를 종료)
MoveState lastMoveDirection = STOPPED;        // 마지막 이동 방향(상/하 결정용)

void arriveAtFloor(unsigned long holdDurationMs, unsigned long extraMs = 0); // 함수선언: 도착 상태(ARRIVING)로 진입시키는 역할, "도착 처리"가 필요할 때마다 간단히 호출만

// 내부 헬퍼: 주어진 방향으로 이동 시작(중복된 초기화/LED 처리 통합)
static void startMovingTowards(int dir){
  moveState = (dir > 0) ? MOVING_UP : MOVING_DOWN;
  digitalWrite(FLOOR_LED[currentFloor], LOW);
  betweenIdx = 0;
  betweenLastMillis = millis();
}

// ARRIVING 상태에서 다시 이동을 시작할 때 쓰는 헬퍼
static void startMovingFromArrive(int dir, unsigned long now){
  moveState = (dir > 0) ? MOVING_UP : MOVING_DOWN;
  digitalWrite(FLOOR_LED[currentFloor], LOW);
  betweenIdx = 0;
  betweenLastMillis = now;
}

// 핀과 LED 상태 초기화
void setup() {
  for (int i=0;i<3;i++) {
    pinMode(BTN_PINS[i], INPUT);
    pinMode(BTN_LED[i], OUTPUT);
    digitalWrite(BTN_LED[i], LOW);
    pinMode(FLOOR_LED[i], OUTPUT);
    digitalWrite(FLOOR_LED[i], LOW);
  }
  for (int i=0;i<4;i++) {
    pinMode(BETWEEN_LED[i], OUTPUT);
    digitalWrite(BETWEEN_LED[i], LOW);
  }
  updateFloorLeds();
}

// 메인 루프: 입력 처리, LED 업데이트, 이동 스케줄링 및 실행
void loop() {
  readButtons();                // 1. 버튼 입력 및 요청 상태 갱신
  updateButtonLedStates();      // 2. 버튼 LED 상태(요청/블링크) 갱신
  selectNextRequestWhenStopped();   // 3. 멈춤(STOPPED) 상태에서 다음 요청 선택 및 이동 시작
  processMovementState();       // 4. 이동 상태 머신 실행 (상태별 동작)
}

// 모든 상태에서 버튼 LED를 requested[] 및 블링크 상태에 맞게 갱신
void updateButtonLedStates(){
  unsigned long now = millis();
  for (int i=0;i<3;i++){
    int target = LOW;
    if (blinkActive[i]){
      if (now >= blinkToggleNext[i]){
        bool on = (blinkPhase[i] % 2 == 0);
        target = on ? HIGH : LOW;
        blinkPhase[i]++;
        blinkToggleNext[i] = now + BLINK_INTERVAL_MS;
        if (blinkPhase[i] >= BLINK_PHASES){
          blinkActive[i] = false;
          target = LOW;
        }
      } else {
        bool on = (blinkPhase[i] % 2 == 0);
        target = on ? HIGH : LOW;
      }
    } else {
      target = requested[i] ? HIGH : LOW;
    }

    if (target != lastBtnLedState[i]){
      digitalWrite(BTN_LED[i], target);
      lastBtnLedState[i] = target;
    }
  }
}

// 모든 상태에서 버튼 입력을 읽어 requested[] 갱신(요청/취소/롱프레스)
void readButtons(){
  for(int i=0;i<3;i++){
    int reading = digitalRead(BTN_PINS[i]);
    if (reading != lastBtnState[i]){
      lastBtnState[i] = reading;
    }

    if (reading == HIGH){
      if (pressStartMillis[i] == 0){
        pressStartMillis[i] = millis();
        pressStartedWithActiveRequest[i] = requested[i];
        unsigned long now = pressStartMillis[i];
        if (now < cancelProtectUntil[i]){
        } else if (!requested[i]){
          requested[i] = true;
          digitalWrite(BTN_LED[i], HIGH);
        }
      } else {
        unsigned long heldNow = millis() - pressStartMillis[i];
        if (heldNow >= LONG_PRESS_MS && !canceledWhileHold[i] && pressStartedWithActiveRequest[i]){
          if (requested[i]){
            requested[i] = false;
            unsigned long now = millis();
            blinkActive[i] = true;
            blinkPhase[i] = 1;
            digitalWrite(BTN_LED[i], HIGH);
            blinkToggleNext[i] = now + BLINK_INTERVAL_MS;
            cancelProtectUntil[i] = now + CANCEL_PROTECT_MS;
          }
          canceledWhileHold[i] = true;
        }
      }
    } else {
      if (pressStartMillis[i] != 0){
        pressStartMillis[i] = 0;
        canceledWhileHold[i] = false;
        pressStartedWithActiveRequest[i] = false;
      }
    }
  }
}

// 멈춘 상태(STOPPED)이면 가장 가까운 요청을 선택해 이동 시작
void selectNextRequestWhenStopped(){
  if (moveState != STOPPED) return;

  // 현재 층이 요청된 경우 바로 도착 처리
  if (requested[currentFloor]){
    arriveAtFloor(DOOR_OPEN_MS);
    return;
  }

  int upFloor = -1, downFloor = -1;  // upFloor, downFloor: 위쪽/아래쪽에서 요청된 층의 인덱스(없으면 -1)
  int upDist = 100, downDist = 100;  // 현재 층에서 위/아래 요청까지의 거리(몇 층 차이인지)

  // 위쪽/아래쪽에서 가장 가까운 요청을 각각 찾음
  for (int i = 0; i < 3; ++i){
    if (!requested[i]) continue;
    int d = abs(i - currentFloor);
    if (i > currentFloor && d < upDist){ upDist = d; upFloor = i; }
    else if (i < currentFloor && d < downDist){ downDist = d; downFloor = i; }
  }

  // 우선 위쪽(또는 더 가까운 쪽)을 선택, 없으면 아래쪽
  if (upFloor >= 0 || downFloor >= 0){
    if (upFloor >= 0 && (downFloor < 0 || upDist <= downDist)){
      startMovingTowards(+1);
    } else {
      startMovingTowards(-1);
    }
  }
}

// 층간 LED 모두 끄기 (중복 쓰기 방지를 위해 최적화됨)
void clearBetweenLeds(){
  for (int i=0;i<4;i++){
    if (lastBetweenLedState[i] != LOW){
      digitalWrite(BETWEEN_LED[i], LOW);
      lastBetweenLedState[i] = LOW;
    }
  }
}

// 주어진 방향으로 이동 애니메이션과 층 전환을 처리
// dir: +1 = 위로, -1 = 아래로
void handleMoving(int dir, unsigned long now){
  int firstIdx = (dir > 0) ? ((currentFloor==0) ? 0 : 2) : ((currentFloor==2) ? 2 : 0);
  if (now - betweenLastMillis < MOVE_STEP_MS) return;
  betweenLastMillis = now;

  if (betweenIdx >= 2){
    currentFloor += dir;
    if (currentFloor < 0) currentFloor = 0;
    if (currentFloor > 2) currentFloor = 2;
    clearBetweenLeds();
    lastMoveDirection = (dir > 0) ? MOVING_UP : MOVING_DOWN;
    if (requested[currentFloor]){
      arriveAtFloor(DOOR_OPEN_MS, MOVE_STEP_MS);
      return;
    }
    bool above = pendingAbove();
    bool below = pendingBelow();
    if (!above && !below){
      moveState = STOPPED;
      updateFloorLeds();
    } else {
      betweenIdx = 0;
      betweenLastMillis = now;
    }
  } else {
    clearBetweenLeds();
    int idx = (dir > 0) ? (firstIdx + betweenIdx) : (firstIdx + (1 - betweenIdx));
    if (lastBetweenLedState[idx] != HIGH) {
      digitalWrite(BETWEEN_LED[idx], HIGH);
      lastBetweenLedState[idx] = HIGH;
    }
    betweenIdx++;
  }
}

// ARRIVING(도착/문 열림) 상태에서의 처리 로직
void handleArriving(unsigned long now){
  if (now < arrivedUntil) return;
  if (currentFloor >=0 && currentFloor < 3){
    requested[currentFloor] = false;
    digitalWrite(BTN_LED[currentFloor], LOW);
  }
  updateFloorLeds();
  bool above = pendingAbove();
  bool below = pendingBelow();
  if (lastMoveDirection == MOVING_UP){
    if (above){ startMovingFromArrive(+1, now); }
    else if (below){ startMovingFromArrive(-1, now); }
    else { moveState = STOPPED; }
  } else if (lastMoveDirection == MOVING_DOWN){
    if (below){ startMovingFromArrive(-1, now); }
    else if (above){ startMovingFromArrive(+1, now); }
    else { moveState = STOPPED; }
  } else {
    if (above){ startMovingFromArrive(+1, now); }
    else if (below){ startMovingFromArrive(-1, now); }
    else { moveState = STOPPED; }
  }
}

// 현재 moveState에 따라 이동 핸들러 호출
void processMovementState(){
  if (moveState == STOPPED) return;
  unsigned long now = millis();
  if (moveState == MOVING_UP) handleMoving(+1, now);
  else if (moveState == MOVING_DOWN) handleMoving(-1, now);
  else if (moveState == ARRIVING) handleArriving(now);
}

// ARRIVING 상태 진입: 층 LED 표시 및 도착 종료 시간 설정
void arriveAtFloor(unsigned long holdDurationMs, unsigned long extraMs){
  updateFloorLeds();
  digitalWrite(BTN_LED[currentFloor], LOW); // 도착 즉시 호출 LED 끔
  arrivedUntil = millis() + holdDurationMs + extraMs;
  (void)holdDurationMs;
  (void)extraMs;
  moveState = ARRIVING;
}

int lastFloorLedState[3] = {LOW, LOW, LOW};
// 층 표시 LED 업데이트; 중복 쓰기 방지
void updateFloorLeds(){
  for (int i=0;i<3;i++){
    int target = (i==currentFloor) ? HIGH : LOW;
    if (target != lastFloorLedState[i]){
      digitalWrite(FLOOR_LED[i], target);
      lastFloorLedState[i] = target;
    }
  }
}

// 현재 층 위에 남은 호출이 있는지 확인
bool pendingAbove(){
  for (int i=currentFloor+1;i<3;i++) if (requested[i]) return true;
  return false;
}

// 현재 층 아래에 남은 호출이 있는지 확인
bool pendingBelow(){
  for (int i=0;i<currentFloor;i++) if (requested[i]) return true;
  return false;
}
