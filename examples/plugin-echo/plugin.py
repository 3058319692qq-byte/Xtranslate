#!/usr/bin/env python3
"""plugin-echo: XTranslate 翻译插件示例（phase 5）。

协议（v1，JSON over stdio）：
  - 主进程启动本插件后，按行读取 stdin（每行一个 JSON 请求）；
  - 主进程期待每条请求都得到一行 JSON 响应（stdout，末尾换行）；
  - 进程长驻，直到 stdin 关闭（readline 返回空串）再退出。

请求类型：
  {"op":"describe"}
    -> {"name":"echo","version":"0.0.1"}
  {"op":"translate","text":"...","from":"...","to":"..."}
    -> {"text":"[echo] ...","provider":"plugin:echo"}
  其它 op
    -> {"error":"unknown op: <op>"}

注意：name 字段必须等于插件子目录名（这里是 "echo"），否则 XTranslate 探测失败。
"""

import json
import sys


def main() -> None:
    while True:
        line = sys.stdin.readline()
        if not line:
            break
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except Exception as exc:  # noqa: BLE001 - 任意 JSON 解析错误都要回包
            sys.stdout.write(json.dumps({"error": "bad json: %s" % exc}) + "\n")
            sys.stdout.flush()
            continue

        op = req.get("op")
        if op == "describe":
            sys.stdout.write(json.dumps({"name": "echo", "version": "0.0.1"}) + "\n")
            sys.stdout.flush()
        elif op == "translate":
            text = req.get("text", "")
            sys.stdout.write(
                json.dumps({"text": "[echo] " + text, "provider": "plugin:echo"}) + "\n"
            )
            sys.stdout.flush()
        else:
            sys.stdout.write(json.dumps({"error": "unknown op: %s" % op}) + "\n")
            sys.stdout.flush()


if __name__ == "__main__":
    main()
