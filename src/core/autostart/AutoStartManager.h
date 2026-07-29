// AutoStartManager - Windows 开机启动注册表联动 + 启动时校准（phase 5）。
//
// 写 HKCU\Software\Microsoft\Windows\CurrentVersion\Run\XTranslate，命令行
// 形如 `"<exe>" --minimized`，让登录后无窗口驻留到托盘。
//
// 校准语义：启动时对比 config.autostart 与注册表实际状态，不一致以 config
// 为准（防用户手动删注册表或第三方工具清理后出现"配置开了但实际没注册"
// 的悬空状态）。配置 autostart 字段变化时同步写/删注册表。

#pragma once

#include <QObject>

class AutoStartManager : public QObject
{
    Q_OBJECT
public:
    static AutoStartManager &instance();

    // 启动时调用一次：把注册表同步到 config.autostart 状态。
    void alignFromConfig();

    // 把注册表写入/删除同步到 on；同时把 config.autostart 置为 on。
    // 失败返回 false（如注册表被组策略禁写）。
    bool setEnabled(bool on);

    // 注册表当前是否真的写了 XTranslate 项（与 config 解耦，用于校准比对）。
    bool isRegistered() const;

private:
    AutoStartManager();
};
