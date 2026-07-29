// SelfTest - "--selftest <mode>" implementations (phase 1).
//
// Contract: print exactly one JSON line on stdout; exit code 0 = pass.

#pragma once

#include <QStringList>

namespace selftest {

// --selftest env  (phase 0, unchanged contract)
int runEnv();

// --selftest ocr [imagePath] [--debug]
//   no arg : built-in 800x200 image "Hello World 你好世界"; pass when the
//            merged recognized text contains both "Hello" and "你好".
//   path   : recognize that file; pass when the pipeline reports no error.
//   --debug: additionally dump det prob/binary/box-overlay PNGs and every
//            rec crop strip next to the input image (cwd for built-in).
int runOcr(const QStringList &args);

// --selftest translate "<text>" [from] [to]
//   Full TranslationManager chain; pass when any provider (incl. mock)
//   returns a result.
int runTranslate(const QStringList &args);

// --selftest capture [--debug]  (phase 2)
//   Headless-automatic: shows a 640x240 test window with two painted text
//   lines, grabs its global rect via ScreenCapturer, runs OCR. JSON
//   {"lines":N,"texts":[..],"match":bool,"elapsedMs":..}; pass when the
//   merged text contains both "Capture" and "截图".
//   --debug: taller probe with a tight 2-line paragraph to exercise line
//   grouping; adds "groups" to the JSON and dumps capture_probe_* pipeline
//   images + capture_groups.png (group boxes overlay) into the cwd.
int runCapture(const QStringList &args);

// --selftest overlay  (phase 2)
//   Shows an OverlayWindow with two fake translated groups, reads back the
//   Win32 extended styles (WS_EX_TRANSPARENT|WS_EX_LAYERED|WS_EX_NOACTIVATE).
//   JSON {"styles_ok":bool,"groups":2}; auto-closes after 800 ms.
int runOverlay();

// --selftest hotkey  (phase 3; F-1 修正于 phase 5)
//   读取 ConfigManager 当前绑定值注册六动作（不再恒用 defaultActions 硬编码），
//   报告 {"registered":N,"failed":[..],"bindings":{actionId:keySeq}}，
//   注销全部。Pass when N==6, or N>=3 with the conflicts listed truthfully
//   (environment-occupied keys).
int runHotkey();

// --selftest tts  (phase 3)
//   JSON {"available":bool,"voices":N,"zh_voice":..,"en_voice":..}. When a
//   voice exists, speak("test") live and pass unless it throws; available =
//   false is also a pass (no engine on this machine, reported truthfully).
int runTts();

// --selftest selection  (phase 3)
//   Puts a sentinel on the clipboard, opens a QLineEdit window with preset
//   pre-selected text, runs the full SelectionGrabber chain (Ctrl+C inject ->
//   sequence poll -> restore). JSON {"text":..,"source":..,
//   "clipboard_restored":bool,"copy_latency_ms":..}; pass when the grabbed
//   text equals the preset AND the sentinel survived the round trip.
int runSelection();

// --selftest config  (phase 4)
//   QTemporaryDir isolation (never touches the real %APPDATA%): defaults
//   written -> 3 values changed -> re-read verified -> corrupt bytes written
//   -> reload falls back to defaults with config.json.bak preserved.
int runConfig();

// --selftest db  (phase 4; 迁移用例于 phase 5)
//   Temp SQLite database: one row per scene inserted -> queried -> favorite
//   set -> trim (limit 3, 4th insert drops the oldest non-favorite row).
//   Phase 5 追加：构造 v0 老库（旧 CHECK，3 条+1 收藏）→ 打开触发迁移 →
//   校验数据完整 + 收藏保留 + scene='replace' 可写入 + user_version=1 +
//   .bak_v0 备份存在。
int runDb();

// --selftest providers  (phase 4)
//   JSON {"providers":[{name,requires_key,configured}..],"sign_ok":bool};
//   pass when the registry holds >= 10 providers and every signature
//   known-vector check (baidu MD5 doc vector, sha256/HMAC RFC vectors,
//   youdao input truncation, TC3 derivation shape) succeeds.
int runProviders();

// --selftest theme  (phase 4)
//   Loads light.qss/dark.qss from the QRC (non-empty) and applies both
//   through ThemeManager. JSON {"themes":["light","dark"],"qss_ok":true}.
int runTheme();

// --selftest replace  (phase 5)
//   弹出一个预选中文本的 QLineEdit，触发 TextReplacer（走 mock 翻译链）：
//   校验 QLineEdit 文本变为译文 + 剪贴板最终还原成 sentinel 备份。
//   JSON {"original":..,"translated":..,"replaced":bool,
//         "clipboard_restored":bool,"provider":..}；pass 当 replaced=true
//   且 clipboard_restored=true。
int runReplace();

// --selftest systemocr  (phase 5)
//   调用 SystemOcrEngine（C++/WinRT）识别内置 800x200 测试图。
//   JSON {"available":bool,"lang_tag":..,"lines":N,"merged":..,
//         "elapsedMs":..,"error":..}；available=false 也算 pass（环境无
//   WinRT 或无 OCR 语言包时如实上报）。available=true 时 pass 要求 lines>0
//   且 merged 同时包含 "Hello" 与 "你好"。
int runSystemOcr();

// --selftest plugin  (phase 5)
//   构造临时插件目录（XTRANSLATE_PLUGINS_DIR），写一个 plugin.py 的 echo
//   插件 -> rescan -> 走 PluginTranslator 翻译一条 -> 校验返回文本。
//   JSON {"plugins":[{name,available,version}..],"echo_ok":bool,
//         "provider":..,"translated":..}；无 python 解释器时 echo_ok=false
//   也算 pass（如实上报环境）。
int runPlugin();

} // namespace selftest
