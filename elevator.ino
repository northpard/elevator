// Arduino Uno용 엘리베이터 스케치 (3층 간단 시뮬레이터)
// - 외부 호출 버튼 3개 (각 버튼 옆에 호출 표시 LED 포함)
// - 엘리베이터 위치를 표시하는 층 LED 3개
// - 층간 이동을 표시하는 YELLOW LED 4개 (1-2 구간에 2개, 2-3 구간에 2개)
// 버튼은 PULLDOWN 방식(외부 풀다운 저항, 액티브 HIGH)으로 연결되어 있다고 가정합니다.

// 핀 매핑 (배선이 바뀌면 이 부분을 수정하세요)
const int BTN_PINS[3]     = {11, 12, 13};    // 1층,2층,3층 호출 버튼 핀
const int BTN_LED[3]      = {8, 9, 10};      // 호출 표시용 LED 핀(각 버튼 옆)
const int FLOOR_LED[3]    = {A0, 4, 7};      // 층 위치 표시 RED LED 핀(1,2,3층)
const int BETWEEN_LED[4]  = {2, 3, 5, 6};    // 층간 YELLOW LED: 1-2 구간 2개, 2-3 구간 2개

// 타이밍(밀리초)
const unsigned long DEBOUNCE_MS = 50;   // 버튼 입력이 안정되었다고 판단하는 최소 시간
const unsigned long MOVE_STEP_MS = 1000; // 층간 YELLOW LED 단계별 시간(ms)
const unsigned long DOOR_OPEN_MS = 1000; // 도착 후 문이 열린 상태로 유지하는 시간
unsigned long currentDoorHoldMs = DOOR_OPEN_MS; // 현재 도어 열림 대기 시간

// (디버그/테스트 코드 제거) 기본 동작에 필요한 상수만 남김

// 엘리베이터 상태
int currentFloor = 0; // 0 기반 인덱스 (0 -> 1층)
bool requested[3] = {false, false, false};

// 입력 디바운스 처리
unsigned long lastBtnTime[3] = {0,0,0};
int lastBtnState[3] = {LOW, LOW, LOW};
// 긴 누름(호출 취소) 판정용 누름 시작 시간
unsigned long pressStartMillis[3] = {0,0,0};
const unsigned long LONG_PRESS_MS = 1000; // >=1000ms 누르면 취소로 판정
bool canceledWhileHold[3] = {false, false, false};
bool pressStartedWithActiveRequest[3] = {false, false, false}; // 눌렀을 때 이미 요청이 있었는지 기록
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
int moveStepIndex = 0; // 각 층 사이를 두 단계(0,1)로 나눈 인덱스
unsigned long moveStepMillis = 0;
unsigned long arrivedMillis = 0;
MoveState lastMoveDirection = IDLE; // 도착 처리 중 기억해 둘 이동 방향

void setup() {
  // 핀 초기화
  for (int i=0;i<3;i++) {
    // pinMode(BTN_PINS[i], INPUT_PULLUP);
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
  // 초기 층 표시
  updateFloorLeds();
}

void loop() {
  readButtons();
  updateBlinkStates();
  syncRequestLedStates(); // 요청 배열과 버튼 LED 상태를 동기화
  scheduleNextTargetIfIdle();
  runMovement();
}

// 요청 상태와 버튼 LED 상태를 일치시킴 (깜빡임 동작 중엔 간섭하지 않음)
void syncRequestLedStates(){
  for (int i=0;i<3;i++){
    if (blinkActive[i]) continue;
    digitalWrite(BTN_LED[i], requested[i] ? HIGH : LOW);
  }
}

// 버튼 LED 깜빡임 상태 머신 업데이트 (비차단)
void updateBlinkStates(){
  unsigned long now = millis();
  for (int i=0;i<3;i++){
    if (!blinkActive[i]) continue;
    if (now >= blinkToggleNext[i]){
      // LED 상태를 토글
      bool on = (blinkPhase[i] % 2 == 0);
      digitalWrite(BTN_LED[i], on ? HIGH : LOW);
      blinkPhase[i]++;
      blinkToggleNext[i] = now + BLINK_INTERVAL_MS;
      if (blinkPhase[i] >= BLINK_PHASES){
        // 깜빡임 종료 후 LED를 확실히 끔
        blinkActive[i] = false;
        digitalWrite(BTN_LED[i], LOW);
      }
    }
  }
}

// 버튼 읽기(디바운스 적용), 액티브 HIGH (pulldown wiring)
// 짧게 누르면 즉시 호출 등록, 길게 누르면 취소
void readButtons(){
  unsigned long now = millis();

  for(int i=0;i<3;i++){
    int reading = digitalRead(BTN_PINS[i]);
    if (reading != lastBtnState[i]){
      lastBtnTime[i] = millis();
      lastBtnState[i] = reading;
    }
    if ((millis() - lastBtnTime[i]) > DEBOUNCE_MS){
      // 상태가 안정된 구간
      if (reading == HIGH){
        // 버튼이 눌린 상태(홀드) -- pulldown: 눌림 == HIGH
        if (pressStartMillis[i] == 0){
          pressStartMillis[i] = millis();
          pressStartedWithActiveRequest[i] = requested[i]; // 초기 상태 저장
          unsigned long now = pressStartMillis[i];
          if (now < cancelProtectUntil[i]){
            // 취소 보호시간 내 재입력 무시
          } else if (!requested[i]){
            requested[i] = true;
            digitalWrite(BTN_LED[i], HIGH); // 호출 등록 상태를 LED로 표시
          } else {
            // 이미 등록된 상태
          }
        }
        if (pressStartMillis[i] != 0){
          unsigned long heldNow = millis() - pressStartMillis[i];
          if (heldNow >= LONG_PRESS_MS && !canceledWhileHold[i] && pressStartedWithActiveRequest[i]){
            // 누르는 중에 바로 취소 처리
            if (requested[i]){
              requested[i] = false;
              // 깜빡임 피드백을 시작하고 종료 시 LED를 끔
              unsigned long now = millis();
              blinkActive[i] = true;
              blinkPhase[i] = 1; // LED를 즉시 켰으므로 다음 토글에서 꺼지도록 1단계로 설정
              digitalWrite(BTN_LED[i], HIGH);
              blinkToggleNext[i] = now + BLINK_INTERVAL_MS;
              // 즉시 재등록을 방지하기 위한 보호 시간 설정
              cancelProtectUntil[i] = now + CANCEL_PROTECT_MS;
            } else {
              // 취소할 호출 없음
            }
            canceledWhileHold[i] = true; // 같은 취소 메시지가 반복되지 않도록 플래그
          }
        }
      } else {
        // 버튼을 뗀 순간
        if (pressStartMillis[i] != 0){
          unsigned long held = millis() - pressStartMillis[i];
          // 릴리스 시점에서는 긴 누름에 의한 취소 처리는 이미 홀드 중에 수행되었어야 함.
          // 따라서 릴리스 시점의 추가 취소 로직은 제거하여 짧은 더블-프레스가 취소로 해석되는
          // 경우를 방지한다.
          // 누름 관련 상태 초기화
          pressStartMillis[i] = 0;
          canceledWhileHold[i] = false;
          pressStartedWithActiveRequest[i] = false;
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
    // 선택된 층 방향으로 이동하도록 방향을 결정
    if (best > currentFloor) {
      moveState = MOVING_UP;
      digitalWrite(FLOOR_LED[currentFloor], LOW); // 출발 층을 떠났으므로 LED를 끔
    } else if (best < currentFloor) {
      moveState = MOVING_DOWN;
      digitalWrite(FLOOR_LED[currentFloor], LOW); // 출발 층을 떠났으므로 LED를 끔
    } else {
      arriveAtFloor(DOOR_OPEN_MS);
    }
  moveStepIndex = 0;
  moveStepMillis = millis();
  }
}

// 엘리베이터 이동 처리 (비차단 상태머신)
void runMovement(){
  if (moveState == IDLE) return;

  unsigned long now = millis();

  if (moveState == MOVING_UP){
  // 사용할 층간 LED 인덱스 결정
    int firstIdx = (currentFloor==0) ? 0 : 2; // 0,1은 0->1 / 2,3은 1->2 구간에 대응
    if (now - moveStepMillis >= MOVE_STEP_MS){
      moveStepMillis = now;
      if (moveStepIndex >= 2){
        // 다음 층에 도착
        currentFloor += 1;
        // 층간 LED 모두 끔
        for (int i=0;i<4;i++) digitalWrite(BETWEEN_LED[i], LOW);
        lastMoveDirection = MOVING_UP;
        unsigned long holdMs = requested[currentFloor] ? DOOR_OPEN_MS : MOVE_STEP_MS;
        arriveAtFloor(holdMs);
      } else {
        // 다음 단계로 진행
        for (int i=0;i<4;i++) digitalWrite(BETWEEN_LED[i], LOW);
        digitalWrite(BETWEEN_LED[firstIdx + moveStepIndex], HIGH);
        moveStepIndex++;
      }
    }
  } else if (moveState == MOVING_DOWN){
    int firstIdx = (currentFloor==2) ? 2 : 0; // 내려갈 때도 같은 구간을 반대 순서로 사용
    if (now - moveStepMillis >= MOVE_STEP_MS){
      moveStepMillis = now;
      if (moveStepIndex >= 2){
        currentFloor -= 1;
        for (int i=0;i<4;i++) digitalWrite(BETWEEN_LED[i], LOW);
        lastMoveDirection = MOVING_DOWN;
        unsigned long holdMs = requested[currentFloor] ? DOOR_OPEN_MS : MOVE_STEP_MS;
        arriveAtFloor(holdMs);
      } else {
        for (int i=0;i<4;i++) digitalWrite(BETWEEN_LED[i], LOW);
        // 내려가는 느낌을 주기 위해 역순으로 LED를 켬
        int idx = firstIdx + (1 - moveStepIndex);
        digitalWrite(BETWEEN_LED[idx], HIGH);
        moveStepIndex++;
      }
    }
  } else if (moveState == ARRIVING){
    // 도착 처리: 도어 열림 대기 시간
    if (now - arrivedMillis >= currentDoorHoldMs){
      // 도착 루틴 종료
      // 도착 층의 요청을 제거하고 버튼 LED 끄기
      if (currentFloor >=0 && currentFloor < 3){
        requested[currentFloor] = false;
        digitalWrite(BTN_LED[currentFloor], LOW);
      }
      updateFloorLeds();

      // 다음 행동 결정: 같은 방향 우선(SCAN 유사 정책)
      bool above = pendingAbove();
      bool below = pendingBelow();
      if (lastMoveDirection == MOVING_UP){
        if (above){
          moveState = MOVING_UP;
          moveStepIndex = 0;
          moveStepMillis = now;
          digitalWrite(FLOOR_LED[currentFloor], LOW); // 다시 출발하므로 현재 층 LED 끔
        } else if (below){
          moveState = MOVING_DOWN;
          moveStepIndex = 0;
          moveStepMillis = now;
          digitalWrite(FLOOR_LED[currentFloor], LOW);
        } else {
          moveState = IDLE;
        }
      } else if (lastMoveDirection == MOVING_DOWN){
        if (below){
          moveState = MOVING_DOWN;
          moveStepIndex = 0;
          moveStepMillis = now;
          digitalWrite(FLOOR_LED[currentFloor], LOW);
        } else if (above){
          moveState = MOVING_UP;
          moveStepIndex = 0;
          moveStepMillis = now;
          digitalWrite(FLOOR_LED[currentFloor], LOW);
        } else {
          moveState = IDLE;
        }
      } else {
        // 직전 방향 정보가 없다면 가까운 요청 방향을 선택
        if (above){
          moveState = MOVING_UP;
          moveStepIndex = 0;
          moveStepMillis = now;
          digitalWrite(FLOOR_LED[currentFloor], LOW);
        } else if (below){
          moveState = MOVING_DOWN;
          moveStepIndex = 0;
          moveStepMillis = now;
          digitalWrite(FLOOR_LED[currentFloor], LOW);
        } else {
          moveState = IDLE;
        }
      }
    }
  }
}

// 도착 처리: 층 LED 갱신 후 ARRIVING 상태로 전환 (문 열림 상태를 가정)
void arriveAtFloor(unsigned long holdDurationMs){
  updateFloorLeds();
  arrivedMillis = millis();
  currentDoorHoldMs = holdDurationMs;
  moveState = ARRIVING;
}

// 층 LED 상태 갱신: 현재층만 ON
void updateFloorLeds(){
  for (int i=0;i<3;i++){
    digitalWrite(FLOOR_LED[i], (i==currentFloor) ? HIGH : LOW);
  }
}

// 현재층 위/아래에 대기 중인 호출이 있는지 확인하는 보조 함수들
bool pendingAbove(){
  for (int i=currentFloor+1;i<3;i++) if (requested[i]) return true;
  return false;
}

bool pendingBelow(){
  for (int i=0;i<currentFloor;i++) if (requested[i]) return true;
  return false;
}
