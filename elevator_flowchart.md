# 엘리베이터 순서도 (Mermaid)

아래 Mermaid 다이어그램은 `elevator.ino`의 주요 동작 흐름을 간단히 표현합니다.
- 사용법: `mermaid.live` 또는 VS Code의 Mermaid Preview에서 붙여넣어 렌더링 후 이미지로 내보내세요.

---

```mermaid
flowchart TD
  Start([Start]) --> Init
  Init["setup(): 핀 초기화\nupdateFloorLeds()"] --> Loop["loop() 반복"]

  subgraph MainLoop [loop]
    Loop --> ReadButtons["readButtons()\n- debounce\n- 짧은 누름 -> 등록\n- 긴 누름 -> 취소"]
    ReadButtons --> UpdateBlink["updateBlinkStates()"]
    UpdateBlink --> SyncLED["syncRequestLedStates()"]
    SyncLED --> Schedule["scheduleNextTargetIfIdle()\n- IDLE이면 다음 목표 선택"]
    Schedule --> RunMove["runMovement()\n- MOVING_UP / MOVING_DOWN / ARRIVING"]
    RunMove --> Loop
  end

  subgraph Movement
    RunMove --> IsIdle{moveState == IDLE?}
    IsIdle -- true --> Loop
    IsIdle -- false --> MovingUp{moveState == MOVING_UP?}
    MovingUp -- true --> MoveStepUp["층간 LED 애니메이션\nmoveStepIndex 증가\n도착 시 -> arriveAtFloor()"]
    MovingUp -- false --> MovingDown{moveState == MOVING_DOWN?}
    MovingDown -- true --> MoveStepDown["층간 LED 역순 애니메이션\nmoveStepIndex 증가\n도착 시 -> arriveAtFloor()"]
    MovingDown -- false --> Arriving{moveState == ARRIVING}
    Arriving -- true --> ArriveProc["arriveAtFloor():\n- updateFloorLeds()\n- arrivedMillis = millis()\n- currentDoorHoldMs 설정"]
    ArriveProc --> ArrivingWait["도어 열림 대기\n(now - arrivedMillis >= currentDoorHoldMs?)"]
    ArrivingWait -- false --> Loop
    ArrivingWait -- true --> ClearRequest["도착층 요청 클리어\nrequested[floor]=false"]
    ClearRequest --> UpdateFloorLEDs["updateFloorLeds()"]
    UpdateFloorLEDs --> DecideNext["같은 방향 우선(pendingAbove()/pendingBelow())\n-> moveState 설정 또는 IDLE"]
    DecideNext --> Loop
  end

```

---

간단 설명
- readButtons(): 디바운스 후 짧은 누름은 즉시 요청 등록, 길게 누르면 요청 취소(깜빡임 피드백).
- scheduleNextTargetIfIdle(): IDLE일 때 가장 가까운 요청을 선택(거리가 같으면 위쪽 우선).
- runMovement(): 층간 LED로 애니메이션을 보여주며 층에 도착하면 `arriveAtFloor()`로 전환.
- arriveAtFloor(): 도착 처리(층 LED ON, 도어 열림 시간 대기, 요청 클리어) 후 동일 방향 우선으로 다음 행동 결정.

사용 팁
- Mermaid를 렌더링해 Slides에 쓰려면 mermaid.live에 붙여넣고 PNG로 내보내세요.
- 필요하면 더 상세한 분기(예: pressStartMillis/취소 보호/깜빡임 타이밍)를 추가해 드리겠습니다.
