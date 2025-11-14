# 엘리베이터 순서도 (Mermaid, 한글화)

아래는 `elevator.ino`의 동작 흐름을 한글 레이블로 정리한 Mermaid 다이어그램입니다.
- mermaid.live 또는 VS Code Mermaid Preview에 붙여넣어 렌더링하세요.

---

```mermaid
flowchart TD
  Start([시작]) --> Init
  Init["setup(): 핀 초기화\n층 LED 갱신"] --> Loop["루프 반복"]

  subgraph MainLoop [메인 루프]
  Loop --> ReadButtons["버튼 읽기\n- 디바운스\n- 짧게 누름 -> 호출 등록\n- 길게 누름 -> 취소"]
  ReadButtons --> UpdateBlink["깜빡임 상태 갱신"]
  UpdateBlink --> SyncLED["요청-LED 동기화"]
  SyncLED --> Schedule["대기 시 다음 목표 선택\n- 대기 상태일 때 다음 목표 선택"]
  Schedule --> RunMove["이동 처리\n- 상승 중 / 하강 중 / 도착 처리 중"]
    RunMove --> Loop
  end

  subgraph Movement [이동 처리]
  RunMove --> IsIdle{이동상태가 대기인가?}
  IsIdle -- true --> Loop
  IsIdle -- false --> MovingUp{이동상태가 상승중인가?}
  MovingUp -- true --> MoveStepUp["층간 LED 애니메이션\n단계 인덱스 증가\n도착 시 -> 층 도착 처리"]
  MovingUp -- false --> MovingDown{이동상태가 하강중인가?}
  MovingDown -- true --> MoveStepDown["층간 LED 역순 애니메이션\n단계 인덱스 증가\n도착 시 -> 층 도착 처리"]
  MovingDown -- false --> Arriving{이동상태가 도착 처리 중인가?}
  Arriving -- true --> ArriveProc["층 도착 처리:\n- 층 LED 갱신\n- 도착 시각 기록\n- 도어 열림 시간 설정"]
  ArriveProc --> ArrivingWait["도어 열림 대기\n(도착 시각이 도어 열림 시간만큼 지났는가?)"]
    ArrivingWait -- false --> Loop
  ArrivingWait -- true --> ClearRequest["도착층 요청 클리어"]
  ClearRequest --> UpdateFloorLEDs["층 LED 갱신"]
  UpdateFloorLEDs --> DecideNext["같은 방향 우선(위쪽/아래쪽 대기 판정)\n-> 이동상태 설정 또는 대기"]
    DecideNext --> Loop
  end

```

---

설명
- 이 다이어그램은 화면에 바로 붙여넣어 사용할 수 있도록 모든 표시 텍스트를 한국어로 작성했습니다.
- 함수 이름 같은 코드는 괄호 안에 그대로 두어 원본 코드와 매핑하기 쉽도록 했습니다.

원하시면 이 Mermaid를 PNG로 렌더해 워크스페이스에 `elevator_flowchart.png`로 저장해 드리겠습니다. (mermaid-cli 또는 웹 렌더러 사용)
