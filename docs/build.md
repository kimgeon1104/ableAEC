---
title: 빌드 및 설치
aliases: [빌드 가이드, Build]
tags: [build, cmake, juce]
---

# 빌드 및 설치

[[README|AbleAEC]]는 CMake + JUCE 8.0.4 로 빌드합니다. JUCE는 configure 시 자동으로 내려받습니다 (기존 체크아웃이 있으면 `-DJUCE_DIR=<경로>`).

## 요구 사항

- CMake 3.22 이상, C++17 컴파일러
- Git (JUCE 자동 다운로드용)

## Linux

Ableton Live는 Linux를 지원하지 않으므로, Linux 빌드는 테스트 및 Linux DAW(Bitwig, REAPER 등)용입니다.

```bash
sudo apt-get install -y ninja-build libasound2-dev libx11-dev libxrandr-dev \
  libxinerama-dev libxcursor-dev libxext-dev libfreetype-dev libfontconfig1-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Windows

Visual Studio 2022 이상 (C++ 데스크톱 개발 워크로드) 필요.

```powershell
cmake -S . -B build -A x64   # 설치된 최신 Visual Studio를 자동 선택
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

결과물: `build\AbleAEC_artefacts\Release\VST3\AbleAEC.vst3` → `C:\Program Files\Common Files\VST3\` 에 복사

## macOS

Xcode 필요.

```bash
cmake -S . -B build -G Xcode "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

결과물 및 설치 위치:

| 형식 | 빌드 결과 | 설치 위치 |
| --- | --- | --- |
| VST3 | `build/AbleAEC_artefacts/Release/VST3/AbleAEC.vst3` | `~/Library/Audio/Plug-Ins/VST3/` |
| AU | `build/AbleAEC_artefacts/Release/AU/AbleAEC.component` | `~/Library/Audio/Plug-Ins/Components/` |

GitHub Actions 결과물처럼 인터넷에서 받은 서명되지 않은 플러그인은 격리 속성을 지워야 Live가 로드합니다.

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/AbleAEC.vst3
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/AbleAEC.component
```

## Ableton Live에서 인식시키기

1. `Preferences → Plug-ins` 에서 **Use VST3 Plug-in System Folders** 켜기 (macOS는 AU도 켜기)
2. **Rescan** 클릭
3. 브라우저 `Plug-ins` 아래 `ableAEC → AbleAEC` 확인

이후 설정은 [[ableton-setup|Ableton 설정 가이드]] 참고.

## CI 빌드

`.github/workflows/build.yml` 이 push / PR 마다 Linux · Windows · macOS 에서 빌드하고 테스트를 돌립니다. 각 실행의 **Artifacts** 에서 `AbleAEC-windows`, `AbleAEC-macos` 를 받아 바로 설치할 수 있습니다.

## 테스트

| 테스트 | 내용 |
| --- | --- |
| `aec_sim_test` | 합성 음성 + 가상 방 응답으로 AEC 코어 검증: 단일 발화 ERLE, 더블토크 시 근단 음성 보존, 에코 경로 변화 후 재수렴, 지연 추정, 참조 없을 때 투명성 |
| `plugin_host_test` | 빌드된 VST3를 DAW처럼 로드 → 사이드체인 버스 활성화, 보고된 지연과 실제 출력 정렬 일치, 플러그인 경유 에코 제거, 사이드체인 없을 때 통과 동작 |

Linux에서 측정한 결과 예시:

```
[LinearOnly] ERLE single-talk 23.8 dB, after path change 24.9 dB, double-talk SER -5.1 → 20.2 dB, delay 35.0 ms (true 35.0)
[Clean]      ERLE single-talk 41.2 dB, after path change 43.8 dB
plugin_host_test: latency 512 samples, ERLE through plugin 74.3 dB
```

CPU 사용량 (48 kHz, 채널당, 단일 코어 기준): 필터 200 ms ≈ 2.3 %, 500 ms ≈ 6.7 %.

## 관련 문서

- [[AbleAEC-MOC|문서 목차]]
- [[dsp-design|DSP 설계]]
