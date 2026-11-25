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
| `plugin_host_test` | 빌드된 VST3를 DAW처럼 로드 → 사이드체인 버스 활성화, 두 Latency 모드의 보고 지연과 실제 출력 정렬 일치, L≠R 스테레오 사이드체인에서 플러그인 경유 에코 제거 (스테레오 vs 모노 참조 비교), 사이드체인 없을 때 통과 동작 |
| `tests/realistic/run_realistic.py` | 내 목소리(TTS) + 스피커 소리 6종(음악 3장르·백색잡음·말소리 2종) × 조건(스피커 1/2대, 왜곡, 큰 번짐, 약한 에코, 에코 없음, 에코 켜짐/꺼짐) 17개 시나리오를 가상 방에서 측정. CI(Linux)에서 `--check` 로 20개 회귀 기준치 검사. 자세한 내용: [[realistic-test]] |

Linux에서 측정한 결과 예시:

```
aec_sim_test      [LinearOnly] ERLE single-talk 23.3 dB, after path change 24.3 dB, double-talk SER -5.1 → 18.4 dB, delay 35.0 ms (true 35.0)
                  [Clean]      ERLE single-talk 38.1 dB, after path change 41.0 dB
plugin_host_test  latency Quality 1024 / Low 512 samples, ERLE stereo reference 64.1 dB vs mono sum 6.2 dB
realistic         after singing (linear / clean): pop 27.1 / 39.5, EDM 26.7 / 38.8, piano 17.5 / 21.4,
                  white noise 33.9 / 48.2, speech 25.3 / 31.9 dB; no echo: vocal untouched (46.3 → 46.3 dB)
```

CPU 사용량은 [[dsp-design#CPU (48 kHz, 마이크 채널당, 단일 코어)|DSP 설계]] 참고.

### 오프라인 WAV 처리 도구

`aec_wav` 는 플러그인과 같은 알고리즘으로 WAV 파일을 처리합니다. 사용법은 [[realistic-test#내 녹음으로 확인하기 (`aec_wav`)|실환경 테스트 문서]] 참고.

## 관련 문서

- [[AbleAEC-MOC|문서 목차]]
- [[dsp-design|DSP 설계]]
- [[realistic-test|실환경 시뮬레이션 테스트]]
