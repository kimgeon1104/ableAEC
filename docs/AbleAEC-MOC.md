---
title: AbleAEC 문서 목차
aliases: [MOC, AbleAEC MOC]
tags: [moc]
---

# AbleAEC 문서 목차

[[README|AbleAEC]] 플러그인 관련 문서 모음입니다.

## 사용

- [[user-manual|사용 설명서]] — 설치, 연결, 조절기, 화면 읽기, 문제 해결 (플러그인 안 설명서와 같은 내용)
- [[ableton-setup|Ableton Live 설정 가이드]] — 마이크 트랙 삽입, 사이드체인 연결, 파라미터, 문제 해결

## 개발

- [[build|빌드 및 설치]] — Linux / Windows / macOS 빌드, 테스트, 설치 경로
- [[dsp-design|DSP 설계]] — 칼만 적응 필터, 스테레오 참조, 2-경로 구조, 잔여 에코 억제, 지연 구조
- [[realistic-test|실환경 시뮬레이션 테스트]] — 음악·백색잡음·말소리 × 조건 17개 시나리오, 들어볼 수 있는 결과 오디오

## 코드 위치

| 모듈 | 경로 | 관련 문서 |
| --- | --- | --- |
| AEC 코어 | `src/dsp/AecCore.h`, `src/dsp/AecCore.cpp` | [[dsp-design]] |
| FFT | `src/dsp/Fft.h` | [[dsp-design]] |
| 플러그인 프로세서 | `src/plugin/PluginProcessor.*` | [[ableton-setup]], [[dsp-design#지연(latency) 구조]] |
| 에디터(UI) | `src/plugin/PluginEditor.*` | [[user-manual#5. 화면 구성]] |
| UI 문구 (한/영), 설명서 내용 | `src/plugin/UiStrings.h` | [[user-manual]] |
| UI 글꼴 | `resources/fonts/`, `tools/make_ui_font.py` | [[build#UI 글꼴]] |
| 테스트 | `tests/` | [[build#테스트]], [[realistic-test]] |
| 오프라인 WAV 처리 | `tools/aec_wav.cpp` | [[realistic-test#내 녹음으로 확인하기 (`aec_wav`)]] |
