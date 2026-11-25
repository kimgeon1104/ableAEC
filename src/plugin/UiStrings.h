#pragma once
// User-interface text in English and Korean.
//
// The Korean UI font embedded in the plugin (resources/fonts/AbleAECUiKR-*.ttf) only contains the
// characters used here. After adding or changing Korean text, run
//     python3 tools/make_ui_font.py
// to rebuild it (CI runs `tools/make_ui_font.py --check` and fails if a character is missing).
// This file must stay UTF-8.

#include <juce_core/juce_core.h>

namespace ui
{
enum class Lang { en = 0, ko = 1 };

enum class Str
{
    subtitle,
    knobFilterLen, knobStep, knobRefDelay, knobResAmount, knobResFloor,
    freeze, aecOff, resetFilter, guide, close, language,
    modeClean, modeLinear, modeEcho, modeRaw,
    latQuality, latLow,
    refStereo, refMono,
    meterSpeaker, meterMic, meterOut,
    stSidechain, stErle, stDelay, stState, stLatency,
    scNotConnected, scNoSignal, scStereo, scMono,
    stateConverged, stateLearning, stateWaiting,
    samples,
    tipFilterLen, tipStep, tipRefDelay, tipResAmount, tipResFloor,
    tipMode, tipLatency, tipRefMode, tipFreeze, tipAecOff, tipReset, tipGuide, tipLanguage,
    guideTitle,
    count
};

struct Entry { const char* en; const char* ko; };

inline const Entry& entry (Str s)
{
    static const Entry table[] = {
        { "Acoustic Echo Canceller  |  mic on input, speaker on sidechain",
          "음향 에코 제거  |  마이크는 입력, 스피커는 사이드체인" },
        { "Filter Length", "필터 길이" },
        { "Adapt Speed", "학습 속도" },
        { "Ref Delay", "참조 지연" },
        { "Residual Supp.", "잔여 억제" },
        { "Supp. Floor", "억제 한계" },
        { "Freeze", "학습 멈춤" },
        { "AEC Off", "AEC 끄기" },
        { "Reset Filter", "필터 초기화" },
        { "Guide", "설명서" },
        { "Close", "닫기" },
        { "Language", "언어" },
        { "Clean", "깨끗하게 (기본)" },
        { "Linear AEC only", "선형 AEC만" },
        { "Echo estimate", "추정 에코 듣기" },
        { "Raw mic", "원본 마이크" },
        { "Quality (~21 ms)", "품질 우선 (~21 ms)" },
        { "Low (~11 ms)", "저지연 (~11 ms)" },
        { "Stereo (L/R)", "스테레오 (L/R)" },
        { "Mono (L+R)", "모노 (L+R)" },
        { "Speaker", "스피커" },
        { "Mic in", "마이크 입력" },
        { "Output", "출력" },
        { "Sidechain", "사이드체인" },
        { "Echo reduction", "에코 감쇄" },
        { "Echo delay", "에코 지연" },
        { "State", "상태" },
        { "Latency", "지연" },
        { "not connected", "연결 안 됨" },
        { "connected, no signal", "연결됨, 신호 없음" },
        { "receiving (stereo)", "수신 중 (스테레오)" },
        { "receiving (mono)", "수신 중 (모노)" },
        { "converged", "학습 완료" },
        { "learning...", "학습 중..." },
        { "waiting for speaker signal", "스피커 신호 대기 중" },
        { "smp", "샘플" },
        // tooltips
        { "How long an echo the filter can remove: speaker-to-mic delay plus the room's reverb. "
          "Use 300-500 ms in large or echoey rooms. Longer costs a little more CPU; no latency change.",
          "지울 수 있는 에코의 길이입니다. 스피커에서 마이크까지의 지연과 방의 울림을 합한 것보다 길어야 합니다. "
          "넓거나 울리는 방은 300-500 ms. 길수록 CPU가 조금 늘고, 지연은 그대로입니다." },
        { "How fast the filter learns. Higher learns faster but wobbles more while you sing.",
          "필터가 배우는 속도입니다. 높이면 빨리 배우지만 노래하는 동안 흔들림이 커집니다." },
        { "Delays the speaker signal before it is compared with the mic. Only needed for very large delays "
          "(Bluetooth speakers, external processing).",
          "스피커 신호를 미리 늦춥니다. 블루투스 스피커나 외부 처리처럼 지연이 아주 클 때만 사용합니다." },
        { "Extra removal of what the filter could not cancel. Higher removes more but can soften the voice "
          "while you sing. 0 % turns it off.",
          "필터가 못 지운 나머지를 추가로 줄입니다. 높이면 더 지우지만 노래할 때 목소리가 약간 뭉개질 수 있습니다. "
          "0 %면 꺼집니다. CPU는 거의 쓰지 않습니다." },
        { "The most the residual suppressor may turn a frequency down.",
          "잔여 억제가 한 주파수를 최대로 줄이는 양입니다." },
        { "What the plugin outputs. Clean = echo cancelled and residual suppressed (recommended). "
          "Echo estimate lets you hear what is being removed.",
          "출력할 소리입니다. 깨끗하게 = 에코 제거 + 잔여 억제(권장). 추정 에코 듣기로 지워지는 소리를 확인할 수 있습니다." },
        { "Quality: most stable, ~21 ms latency. Low: half the latency, but weaker on piano-like music and uses more CPU. "
          "Changing it resets the filter.",
          "품질 우선: 가장 안정적, 지연 약 21 ms. 저지연: 지연이 절반이지만 피아노류 음악에 약하고 CPU를 더 씁니다. "
          "바꾸면 필터가 초기화됩니다." },
        { "Stereo uses the left and right speaker signals separately (needed with two speakers). "
          "Mono sums them (one speaker, about 35 % less CPU).",
          "스테레오는 왼쪽/오른쪽 스피커 신호를 따로 씁니다(스피커 2대일 때 필요). 모노는 합쳐서 씁니다(스피커 1대, CPU 절약)." },
        { "Stop learning and keep the current filter (halves the CPU).", "학습을 멈추고 지금 필터를 유지합니다(CPU 절반)." },
        { "Pass the mic through untouched (same latency).", "처리하지 않고 원본 마이크를 그대로 내보냅니다(지연은 같음)." },
        { "Forget what the filter has learned, e.g. after moving the mic or speakers.",
          "학습한 내용을 지웁니다. 마이크나 스피커 위치를 크게 바꾼 뒤에 누르세요." },
        { "Open the user guide.", "사용 설명서를 엽니다." },
        { "Interface language.", "화면 언어를 바꿉니다." },
        { "AbleAEC user guide", "AbleAEC 사용 설명서" },
    };
    static_assert (sizeof (table) / sizeof (table[0]) == (size_t) Str::count, "missing UI string");
    return table[(size_t) s];
}

inline juce::String tr (Str s, Lang l)
{
    const auto& e = entry (s);
    return juce::String::fromUTF8 (l == Lang::ko ? e.ko : e.en);
}

// User guide shown inside the plugin. Lines starting with "# " are headings, "- " are list items.
inline juce::String guide (Lang l)
{
    if (l == Lang::ko)
        return juce::String::fromUTF8 (
            "# 이 플러그인이 하는 일\n"
            "마이크에는 내 목소리와 스피커에서 나온 소리(반주, 상대방 목소리 등)가 함께 들어갑니다. "
            "AbleAEC는 사이드체인으로 받은 스피커 소리를 기준으로, 마이크에 다시 들어온 스피커 소리(에코)를 지우고 내 목소리만 남깁니다.\n"
            "\n"
            "# 연결하기\n"
            "- 이 플러그인을 마이크 트랙에 넣습니다. 마이크 트랙의 Monitor는 In으로 둡니다.\n"
            "- 디바이스 제목줄의 펼치기 버튼을 눌러 Sidechain을 켭니다.\n"
            "- Audio From을 스피커로 나가는 트랙으로, 탭 위치를 Post Mixer로 지정합니다.\n"
            "- 오른쪽 아래 상태의 사이드체인이 '수신 중'이 되면 연결된 것입니다.\n"
            "- Master를 사이드체인으로 쓰지 마세요. 마이크가 Master로 가면 내 목소리까지 지우려 합니다.\n"
            "- 스피커와 마이크는 같은 오디오 인터페이스를 쓰세요. 장치가 다르면 시간이 어긋나 잘 지워지지 않습니다.\n"
            "\n"
            "# 쓰는 순서\n"
            "- 노래하기 전에 스피커 소리를 10-20초 먼저 틀어 두세요. 필터가 방의 울림을 배우는 시간입니다.\n"
            "- 상태가 '학습 완료'가 되고 에코 감쇄가 15 dB 이상이면 녹음을 시작해도 좋습니다.\n"
            "- 노래하는 동안에도 필터는 망가지지 않고 계속 배웁니다.\n"
            "- 스피커 소리가 마이크에 안 들어가면 목소리를 건드리지 않습니다.\n"
            "- 마이크나 스피커 위치를 크게 바꾸면 '필터 초기화'를 누르세요.\n"
            "\n"
            "# 조절기: 역할과 비용\n"
            "CPU는 마이크 채널당, 기본 설정(품질 우선, 스테레오, 250 ms) 약 8 % 기준입니다. 지연이 바뀌는 것은 지연 모드뿐입니다.\n"
            "- 필터 길이 (기본 250 ms): 지울 수 있는 에코의 길이. 넓거나 울리는 방은 300-500 ms. "
            "비용: 길수록 CPU 증가(100 ms 약 7 %, 500 ms 약 10 %)와 처음 학습이 조금 느려짐. 짧으면 울림 끝부분이 남음. 지연 변화 없음.\n"
            "- 학습 속도 (기본 50 %): 높이면 빨리 배우지만 노래할 때 흔들림이 커지고, 낮추면 안정적이지만 느리게 배움. CPU, 지연 변화 없음.\n"
            "- 참조 지연 (기본 0 ms): 스피커 신호를 미리 늦춤. 블루투스 스피커처럼 지연이 아주 클 때만 사용. "
            "비용: 출력 지연은 늘지 않음. 실제 에코 지연보다 크게 잡으면 에코를 못 지움.\n"
            "- 잔여 억제 (기본 20 %): 남은 에코를 더 줄임. 비용: CPU 거의 없음(약 0.4 %). 높이면 노래할 때 목소리가 약간 뭉개질 수 있음. 지연 변화 없음.\n"
            "- 억제 한계 (기본 -18 dB): 잔여 억제가 줄일 수 있는 최대량. 낮출수록 더 지우지만 목소리 손상 가능성 증가.\n"
            "- 출력: 깨끗하게(권장) / 선형 AEC만 / 추정 에코 듣기 / 원본 마이크. 모두 같은 지연이라 바꿔도 박자가 어긋나지 않음.\n"
            "- 지연 모드: 품질 우선(지연 1024 샘플, 약 21 ms) / 저지연(512 샘플, 약 11 ms). "
            "비용: 저지연은 CPU 약 2배(약 15 %), 피아노류 음악에서 성능 저하. 바꾸면 필터 초기화.\n"
            "- 참조: 스테레오(스피커 2대일 때 필요) / 모노(스피커 1대). 비용: 모노가 CPU 약 35 % 적음(약 5 %). 스피커 2대에 모노를 쓰면 에코가 덜 지워짐.\n"
            "- 학습 멈춤: 지금 필터를 유지. 비용: CPU 절반(약 4 %). 마이크/스피커가 움직이면 따라가지 못함.\n"
            "- AEC 끄기: 원본 그대로 출력(지연은 같음). 필터 초기화: 학습 내용 삭제, 다시 배우는 동안(수 초) 에코가 남음.\n"
            "\n"
            "# GPU를 쓰나요?\n"
            "아니요. 여기서 '학습'은 딥러닝 학습이 아니라, 매 순간 방의 반향을 따라가는 작은 계산(적응 필터)입니다. "
            "5-10 ms마다 바로 결과를 내야 해서 GPU로 데이터를 보내고 받는 시간이 계산보다 오히려 깁니다. CPU 한 코어의 약 8 %면 충분합니다.\n"
            "\n"
            "# 화면 읽기\n"
            "- 스피커 / 마이크 입력 / 출력: 각 신호의 크기.\n"
            "- 에코 감쇄: 스피커 소리만 날 때 마이크보다 출력이 얼마나 작아졌는지. 15 dB 이상 양호, 25 dB 이상 매우 좋음. 노래할 때는 낮게 보이는 것이 정상입니다.\n"
            "- 에코 지연: 스피커에서 마이크까지 걸리는 시간 추정값.\n"
            "- 지연: 이 플러그인이 더하는 지연. 녹음은 Live가 자동으로 맞춰 줍니다.\n"
            "\n"
            "# 문제 해결\n"
            "- 사이드체인 '연결 안 됨': 디바이스를 펼쳐 Sidechain을 켜고 Audio From을 지정하세요.\n"
            "- '연결됨, 신호 없음': 스피커 트랙이 재생 중인지, 페이더가 내려가 있지 않은지 확인하세요.\n"
            "- 에코 감쇄가 계속 낮음: 지연 모드가 품질 우선인지, 참조가 스테레오인지 확인하고 필터 길이를 늘려 보세요.\n"
            "- 목소리가 뭉개짐: 잔여 억제를 낮추거나 억제 한계를 -12 dB 정도로 올리세요.\n"
            "- 반주가 조금 남음: 잔여 억제를 올리세요. 스피커 볼륨이 너무 커서 소리가 찌그러지면 볼륨을 낮추세요.\n"
            "- 모니터링이 늦게 들림: 오디오 인터페이스의 다이렉트 모니터링을 쓰거나 저지연 모드를 쓰세요.\n");

    return juce::String::fromUTF8 (
        "# What this plugin does\n"
        "Your mic picks up your voice and whatever the speakers play (a backing track, a remote caller). "
        "AbleAEC uses the speaker signal it receives on the sidechain to remove that sound (the echo) from the mic, "
        "so only your voice is left.\n"
        "\n"
        "# Connecting\n"
        "- Put the plugin on the mic track. Set the track's Monitor to In.\n"
        "- Open the device (the arrow in its title bar) and turn Sidechain on.\n"
        "- Set Audio From to the track that feeds the speakers, tap point Post Mixer.\n"
        "- The status panel shows 'receiving' once the sidechain arrives.\n"
        "- Do not use the Master as the sidechain if the mic also goes to the Master: the plugin would try to remove your voice.\n"
        "- Use the same audio interface for speakers and mic. Different devices drift apart and the echo is not removed well.\n"
        "\n"
        "# Using it\n"
        "- Play the speaker sound for 10-20 seconds before you start singing, so the filter can learn the room.\n"
        "- Start recording once State shows 'converged' and Echo reduction is above 15 dB.\n"
        "- The filter keeps learning while you sing and is not thrown off by your voice.\n"
        "- If no speaker sound reaches the mic, your voice is left untouched.\n"
        "- After moving the mic or speakers a lot, press Reset Filter.\n"
        "\n"
        "# Controls: what they do and what they cost\n"
        "CPU is per mic channel; the default (Quality, Stereo, 250 ms) uses about 8 %. Only Latency changes the plugin's latency.\n"
        "- Filter Length (250 ms): how long an echo can be removed; 300-500 ms in large or echoey rooms. "
        "Cost: longer uses more CPU (100 ms about 7 %, 500 ms about 10 %) and learns a little slower at first. Too short leaves the end of the reverb. No latency change.\n"
        "- Adapt Speed (50 %): higher learns faster but wobbles more while you sing; lower is steadier but slower. No CPU or latency change.\n"
        "- Ref Delay (0 ms): delays the speaker signal, only for very large delays such as Bluetooth speakers. "
        "Cost: no added output latency, but set larger than the real echo delay and the echo cannot be removed.\n"
        "- Residual Supp. (20 %): removes more of what is left. Cost: almost no CPU (about 0.4 %); higher can soften the voice while you sing. No latency change.\n"
        "- Supp. Floor (-18 dB): the most the residual suppressor may turn a frequency down; lower removes more but risks the voice.\n"
        "- Output: Clean (recommended) / Linear AEC only / Echo estimate / Raw mic. All have the same latency, so switching never shifts timing.\n"
        "- Latency: Quality (1024 samples, ~21 ms) / Low (512 samples, ~11 ms). "
        "Cost: Low uses about twice the CPU (about 15 %) and is weaker on piano-like music. Changing it resets the filter.\n"
        "- Reference: Stereo (needed with two speakers) / Mono (one speaker). Cost: Mono uses about 35 % less CPU (about 5 %); with two speakers it removes less echo.\n"
        "- Freeze keeps the current filter. Cost: halves the CPU (about 4 %), but it cannot follow a moved mic or speaker.\n"
        "- AEC Off passes the mic through (same latency). Reset Filter forgets what was learned; the echo comes back for a few seconds while it re-learns.\n"
        "\n"
        "# Does it use the GPU?\n"
        "No. 'Learning' here is not deep-learning training but a small calculation (an adaptive filter) that follows the room's echo moment by moment. "
        "It must answer every 5-10 ms, and moving audio to a GPU and back would take longer than the calculation itself. About 8 % of one CPU core is enough.\n"
        "\n"
        "# Reading the display\n"
        "- Speaker / Mic in / Output: signal levels.\n"
        "- Echo reduction: how much quieter the output is than the mic while only the speakers play. "
        "Above 15 dB is good, above 25 dB very good. It reads low while you sing; that is normal.\n"
        "- Echo delay: estimated time from speaker to mic.\n"
        "- Latency: what the plugin adds. Live compensates it when recording.\n"
        "\n"
        "# Troubleshooting\n"
        "- Sidechain 'not connected': open the device, turn Sidechain on and choose Audio From.\n"
        "- 'connected, no signal': check that the speaker track is playing and its fader is up.\n"
        "- Echo reduction stays low: check Latency is Quality and Reference is Stereo, try a longer Filter Length.\n"
        "- Voice sounds smeared: lower Residual Supp. or raise Supp. Floor to about -12 dB.\n"
        "- A little backing track remains: raise Residual Supp. Turn the speakers down if they distort.\n"
        "- Monitoring feels late: use your interface's direct monitoring, or the Low latency mode.\n");
}
} // namespace ui
