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

## 특징

- VST3 (Windows / macOS), AU (macOS) — 사이드체인 입력 버스 지원
- 2-경로(foreground/background) **주파수 영역 분할 적응 필터(PBFDAF)** — 더블토크(양쪽 동시 발화)에 강하고, 마이크/스피커 위치가 바뀌어도 빠르게 재수렴
- **잔여 에코 억제기(RES)** — 스피커 왜곡 등 선형 필터로 못 지운 잔여 에코 추가 제거
- 에코 감쇄량(ERLE), 추정 에코 지연, 사이드체인 연결 상태 실시간 표시
- 지연(latency) 512 샘플 고정 — 호스트에 보고되므로 Live가 자동 보정

## 빠른 시작

1. [[docs/build|빌드 가이드]]대로 빌드하거나, GitHub Actions 빌드 결과물(`AbleAEC-windows`, `AbleAEC-macos`)을 내려받습니다.
2. `AbleAEC.vst3`를 VST3 폴더에 복사하고 Live에서 플러그인을 다시 검색합니다.
3. [[docs/ableton-setup|Ableton 설정 가이드]]대로 마이크 트랙에 넣고 사이드체인을 스피커 트랙으로 지정합니다.

## 문서

- [[docs/AbleAEC-MOC|문서 목차 (MOC)]]
- [[docs/ableton-setup|Ableton Live 설정 가이드]]
- [[docs/build|빌드 및 설치]]
- [[docs/dsp-design|DSP 설계]]

## 저장소 구조

```
src/dsp/        AEC 알고리즘 코어 (JUCE 의존성 없음)
src/plugin/     JUCE 플러그인 래퍼 (사이드체인 버스, 파라미터, UI)
tests/          오프라인 시뮬레이션 테스트, VST3 호스팅 종단 테스트
docs/           문서
```
