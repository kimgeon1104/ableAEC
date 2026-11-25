---
title: Ableton Live 설정 가이드
aliases: [Ableton 설정, 사이드체인 설정]
tags: [ableton, setup]
---

# Ableton Live 설정 가이드

[[README|AbleAEC]]를 세션 뷰의 마이크 트랙에 넣고, 스피커로 나가는 트랙을 사이드체인으로 연결하는 방법입니다.
설치는 [[build|빌드 및 설치]]를 참고하세요.

> [!note] 지원 환경
> Ableton Live는 Linux 버전이 없으므로 **Windows / macOS**에서 사용합니다. 서드파티 플러그인 사이드체인은 **Live 11 이상**을 권장합니다 (VST3 / AU 모두 사이드체인 입력이 표시됨).

## 1. 트랙 구성

```mermaid
flowchart TB
    subgraph Live["Ableton Live (Session View)"]
        SPK["스피커 트랙<br/>클립 재생 / 원격 음성 입력"]
        MIC["마이크 트랙<br/>Audio From: 오디오 인터페이스 입력<br/>Monitor: In"]
        AEC["AbleAEC (마이크 트랙 디바이스)"]
        MASTER["Master"]
        SPK --> MASTER
        SPK -. "Sidechain · Post Mixer" .-> AEC
        MIC --> AEC
    end
    MASTER --> SPEAKER["스피커"]
    SPEAKER -->|"공간 반향"| MICIN["마이크"]
    MICIN --> MIC
```

| 트랙 | 설정 |
| --- | --- |
| 스피커 트랙 | 스피커로 내보낼 소리(클립, 원격 통화 음성 등). 출력 → Master |
| 마이크 트랙 | `Audio From` = 마이크가 연결된 인터페이스 입력, `Monitor` = **In** (또는 Auto + Arm) |

## 2. 플러그인 삽입과 사이드체인 연결

1. 브라우저 → **Plug-ins** → `AbleAEC`를 **마이크 트랙**에 끌어다 놓습니다.
2. 디바이스 제목줄의 **펼치기(▼) 버튼**을 눌러 사이드체인 섹션을 엽니다.
3. **Sidechain** 을 켭니다.
4. `Audio From` = **스피커 트랙**, 탭 위치 = **Post Mixer** (볼륨 페이더까지 반영 → 실제 스피커로 나가는 소리와 가장 비슷함).
5. 플러그인 화면 오른쪽 상태창이 `Sidechain: receiving` 이 되는지 확인합니다.
6. 스피커 소리를 재생하고 몇 초 기다리면 `Echo reduction` 값이 올라가고 `State: converged` 가 됩니다.

> [!warning] Master를 사이드체인 소스로 쓰지 마세요 (마이크가 Master로 가는 경우)
> 마이크 트랙이 Master로 출력되면 Master 신호 안에 마이크(근단) 소리도 들어 있습니다. 이를 참조로 쓰면 AEC가 **말소리 자체를 지우려 합니다.** 사이드체인은 반드시 *스피커로 나가는 소리만 있는 트랙*에서 받으세요. 스피커 소스가 여러 트랙이면 그룹 트랙이나 리턴 트랙으로 묶어서 그 트랙을 사이드체인 소스로 지정합니다.

> [!warning] 같은 오디오 인터페이스 사용
> 스피커 출력과 마이크 입력은 **같은 오디오 인터페이스(같은 클럭)** 여야 합니다. 서로 다른 장치(예: 노트북 내장 스피커 + USB 마이크, Live의 Aggregate Device)는 클럭 드리프트 때문에 에코 경로가 계속 변해 제거 성능이 크게 떨어집니다.

## 3. 파라미터

| 파라미터 | 기본값 | 설명 |
| --- | --- | --- |
| Filter Length | 250 ms | 적응 필터가 덮는 에코 길이. **에코 지연 + 잔향 시간** 이상이어야 함. 넓은/울리는 방은 300–500 ms |
| Adapt Speed | 50 % | 학습 속도 배율 (칼만 이득). 높을수록 빨리 수렴하지만 노래 중 흔들림이 커짐 |
| Ref Delay | 0 ms | 참조 신호를 미리 지연. 에코 지연이 매우 크면(예: 블루투스 스피커, 외부 처리) 그만큼 지정해서 필터 길이를 아낌 |
| Residual Supp. | 20 % | 잔여 에코 억제 강도. 0 % = 끔. 높이면 반주는 더 줄지만 노래 중 보컬이 약간 뭉개질 수 있음 |
| Supp. Floor | -18 dB | 잔여 에코 억제기의 최대 감쇄량 |
| Output | Clean | `Clean`(AEC+억제), `Linear AEC only`, `Echo estimate`(추정 에코만, 확인용), `Raw mic`(원본, 지연만 맞춤) |
| Latency | Quality (~21 ms) | `Quality`: 블록 512 (48 kHz), 음악에 권장. `Low (~11 ms)`: 블록 256, 음성/모노용 — **음악 반주에서는 성능이 크게 떨어짐** ([[realistic-test#해석|테스트 결과]]). 바꾸면 필터가 초기화됨 |
| Reference | Stereo (L/R) | 스테레오 사이드체인을 L/R 따로 참조 (스피커 2대일 때 필수). `Mono (L+R)` 는 스피커 1대 또는 CPU 절약용 |
| Freeze | off | 학습 정지 (현재 필터 유지) |
| AEC Off | off | 처리 없이 원본 마이크를 내보냄 (지연은 동일하게 유지) |
| Reset Filter | — | 학습된 필터 초기화 (마이크/스피커 위치를 크게 바꾼 뒤) |

> [!tip] 노래 시작 전 반주를 10–20초 먼저 틀어두세요
> 필터가 방의 반향을 배우는 데 반주 기준 수 초가 걸리고, 20초 전후에 가장 좋아집니다. 상태창의 `Echo reduction` 이 15 dB 이상이 된 뒤 녹음을 시작하면 좋습니다. 노래하는 동안에도 필터는 망가지지 않고 계속 학습합니다.

자세한 알고리즘은 [[dsp-design|DSP 설계]] 참고.

## 화면 읽는 법

- **Speaker / Mic in / Output** 미터: 사이드체인 참조, 마이크 입력, 처리 결과 레벨
- **Sidechain**: `not connected`(사이드체인 꺼짐) / `connected, no signal` / `receiving (stereo)` / `receiving (mono)`
- **Echo reduction (ERLE)**: 스피커 소리만 날 때 마이크 대비 출력이 얼마나 줄었는지(선형 필터 기준). 15 dB 이상이면 양호, 25 dB 이상이면 매우 좋음. 말하는 중에는 낮게 표시되는 것이 정상
- **Echo delay**: 스피커 → 마이크 총 지연 추정값 (인터페이스 왕복 지연 + 음향 경로 + Ref Delay)
- **Latency**: 플러그인이 호스트에 보고한 지연 (Quality 1024 샘플, Low 512 샘플 @48 kHz)

## 4. 문제 해결

| 증상 | 원인 / 조치 |
| --- | --- |
| `Sidechain: not connected` | 디바이스를 펼쳐 Sidechain 토글을 켜고 `Audio From`을 지정 |
| `connected, no signal` | 스피커 트랙이 재생 중인지, 탭 위치가 Post Mixer인데 페이더가 내려가 있지 않은지 확인 |
| ERLE가 계속 낮음 (< 6 dB) | 반주를 20초 이상 틀어도 낮으면: Latency가 `Quality` 인지, Reference가 `Stereo` 인지 확인. Echo delay 값이 Filter Length 끝 근처라면 Filter Length를 늘리거나 Ref Delay 지정. 스피커/마이크가 다른 장치인지 확인 |
| 말할 때 목소리가 뭉개짐 | Residual Supp.를 낮추거나 Supp. Floor를 -15 dB 정도로 올림 |
| 에코가 조금 남음 | Residual Supp. 를 높임. 스피커 볼륨이 너무 커서 왜곡되면 볼륨을 낮춤 |
| 모니터링이 약간 늦게 들림 | 플러그인 지연 1024 샘플(Quality, 48 kHz에서 약 21 ms). 녹음은 Live가 자동 보정. 모니터링은 오디오 인터페이스의 다이렉트 모니터링을 쓰는 것을 권장. `Low` 모드는 지연이 절반이지만 음악에서 성능이 크게 떨어짐 |

## 관련 문서

- [[AbleAEC-MOC|문서 목차]]
- [[dsp-design|DSP 설계]]
- [[realistic-test|보컬 + 반주 실환경 시뮬레이션 테스트]]
- [[build|빌드 및 설치]]
