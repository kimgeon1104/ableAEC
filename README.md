---
title: AbleAEC
aliases: [ableAEC, 에이블톤 AEC 플러그인]
tags: [ableton, aec, vst3, juce]
---

# AbleAEC

에이블톤 라이브(Ableton Live)용 **AEC(Acoustic Echo Cancellation, 음향 반향 제거) 플러그인**입니다.
마이크 트랙에 넣고, 스피커로 나가는 트랙을 **사이드체인**으로 받으면 마이크에 다시 들어온 스피커 소리(에코)를 제거합니다.

```mermaid
flowchart LR
    SPK["스피커 트랙<br/>(Session 클립 / 원격 음성)"] -->|Master → 스피커| ROOM(("방 / 공간"))
    ROOM -->|에코| MIC["마이크 트랙 입력"]
    SPK -.->|Sidechain (Post Mixer)| AEC
    MIC --> AEC["AbleAEC<br/>(마이크 트랙에 삽입)"]
    AEC --> OUT["에코가 제거된 마이크 신호"]
```

## 화면

![[docs/plugin-ui-ko.png]]

한국어 / English 화면을 오른쪽 위에서 바꿀 수 있고(프로젝트에 저장), **설명서** 버튼으로 플러그인 안에서 사용법을 볼 수 있습니다. 각 조절기에 마우스를 올리면 설명이 나옵니다. 한글 글꼴(나눔고딕 기반, SIL OFL)이 플러그인에 들어 있어 어느 시스템에서나 같은 모습으로 보입니다. (스크린샷은 스피커 신호와 그 지연된 에코를 넣어 학습시킨 상태를 Linux에서 렌더링한 것)

## 특징

- VST3 (Windows / macOS), AU (macOS) — 사이드체인 입력 버스 지원
- **스테레오 참조** — 스피커 2대(L/R 다른 소리)도 각각의 경로를 모델링
- **GMDF 필터 + 주파수 영역 칼만 학습** — 23 Hz 분해능으로 서브베이스·순음까지 처리하면서 지연은 짧게 유지, 노래하는 동안(더블토크)에도 필터가 망가지지 않으며, 마이크/스피커 위치가 바뀌면 다시 학습
- 스피커 소리가 마이크에 안 들어오면 목소리를 건드리지 않음
- **자동(Auto) 모드** — 울리는 방에서는 필터를 자동으로 늘리고(테스트 +8 dB), 마이크 속 에코가 적으면 억제를 약하게 해서 목소리 보호
- **잔여 에코 억제기(RES)** — 선형 필터로 못 지운 잔여 반주 추가 제거
- 에코 감쇄량(ERLE), 추정 에코 지연, 사이드체인 연결 상태 실시간 표시
- Latency 모드: Quality(약 21 ms, 기본) / Low(약 11 ms) — 호스트에 보고되므로 Live가 자동 보정
- 음악(팝·피아노·EDM)·백색잡음·말소리 × 다양한 조건 17개 시나리오로 검증 ([[docs/realistic-test|결과와 들어볼 수 있는 오디오]])

## 빠른 시작

1. [[docs/build|빌드 가이드]]대로 빌드하거나, GitHub Actions 빌드 결과물(`AbleAEC-windows`, `AbleAEC-macos`)을 내려받습니다.
2. `AbleAEC.vst3`를 VST3 폴더에 복사하고 Live에서 플러그인을 다시 검색합니다.
3. [[docs/user-manual|사용 설명서]]대로 마이크 트랙에 넣고 사이드체인을 스피커 트랙으로 지정합니다.

## 문서

- [[docs/user-manual|사용 설명서]]
- [[docs/AbleAEC-MOC|문서 목차 (MOC)]]
- [[docs/ableton-setup|Ableton Live 설정 가이드]]
- [[docs/build|빌드 및 설치]]
- [[docs/dsp-design|DSP 설계]]
- [[docs/realistic-test|실환경 시뮬레이션 테스트]]

## 저장소 구조

```
src/dsp/        AEC 알고리즘 코어 (JUCE 의존성 없음)
src/plugin/     JUCE 플러그인 래퍼 (사이드체인 버스, 파라미터, UI, 한/영 문구 UiStrings.h)
resources/      UI 글꼴 (나눔고딕 서브셋, OFL)
tools/          aec_wav: WAV 파일 오프라인 AEC 처리 도구, make_ui_font.py: UI 글꼴 생성
tests/          오프라인 시뮬레이션 테스트, VST3 호스팅 종단 테스트, 실환경(보컬+반주) 테스트
docs/           문서
```
