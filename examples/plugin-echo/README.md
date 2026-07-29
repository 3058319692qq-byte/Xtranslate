# plugin-echo

XTranslate 翻译插件示例（phase 5）。把 stdin 收到的文本加 `[echo] ` 前缀原样返回，验证插件协议链路通畅。

## 协议（v1，JSON over stdio）

主进程启动插件后按行交互：每行一个 JSON 请求，每行一个 JSON 响应。进程长驻，stdin 关闭后退出。

### 请求 / 响应

| op | 请求 | 响应 |
| --- | --- | --- |
| `describe` | `{"op":"describe"}` | `{"name":"echo","version":"0.0.1"}` |
| `translate` | `{"op":"translate","text":"...","from":"...","to":"..."}` | `{"text":"[echo] ...","provider":"plugin:echo"}` |
| 其它 | 任意 | `{"error":"unknown op: <op>"}` |

`describe` 响应中的 `name` 必须等于插件子目录名，否则 XTranslate 会跳过该插件。

## 安装

把整个 `plugin-echo/` 目录复制到 XTranslate 的插件根目录下：

```
%APPDATA%\XTranslate\plugins\plugin-echo\plugin.py
```

或者用环境变量 `XTRANSLATE_PLUGINS_DIR` 指定自定义插件根目录（存在则完全取代默认路径，不合并）：

```
set XTRANSLATE_PLUGINS_DIR=D:\my_plugins
copy /s plugin-echo D:\my_plugins\plugin-echo
```

XTranslate 启动时和「设置 → 插件 → 重新扫描」时会枚举 `plugins/*/plugin.{exe|py|bat}`，按子目录名识别插件。

## 依赖

需要系统 PATH 中可用 `python`（Python 3.6+）。若希望脱离 Python 直接运行，把入口改写成 `plugin.exe` 或 `plugin.bat`（含 `python plugin.py %*`）即可。

## 自测

XTranslate 内置 selftest：

```
XTranslate.exe --selftest plugin
```

会临时构造一个等价的 echo 插件目录并验证 `describe` + `translate` 完整链路。无 python 解释器时如实上报 `echo_ok=false` 也算 pass。
