// PluginManager - 进程外翻译插件发现与生命周期管理（phase 5）。
//
// 插件协议（v1，JSON over stdio）：
//   启动插件进程 -> stdin 发请求 JSON -> stdout 收一行响应 JSON -> 进程长驻。
//   请求 {"op":"describe"}                          -> {"name":..,"version":..}
//   请求 {"op":"translate","text":..,"from":..,"to":..} -> {"text":..,"provider":..,"error":..}
//
// 插件目录解析顺序：
//   1) 环境变量 XTRANSLATE_PLUGINS_DIR（存在则完全取代默认路径，不合并）；
//   2) %APPDATA%\XTranslate\plugins\。
//
// 插件目录布局：每个子目录视为一个插件，主入口文件名固定 plugin.[exe|py|bat]，
// 优先级 exe > py > bat；子目录名即插件 name。子目录不存在主入口则跳过。

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

struct PluginInfo {
    QString name;       // 子目录名
    QString dir;        // 子目录绝对路径
    QString command;    // 主入口可执行文件绝对路径
    QStringList args;   // 启动参数（py 走 python 解释器时填脚本路径）
    bool available = false;     // describe 探测成功
    QString version;    // describe 返回的 version（探测成功才填）
    QString error;      // describe 失败/超时原因
};

class PluginManager : public QObject
{
    Q_OBJECT
public:
    static PluginManager &instance();

    // 重新扫描插件目录；返回当前已发现的 PluginInfo 列表。
    // 同步调用 describe 探测每个插件，可能耗时若干秒。
    QVector<PluginInfo> rescan();

    // 最近一次 rescan 的结果快照（不触发探测）。
    QVector<PluginInfo> plugins() const { return m_plugins; }

    // 当前生效的插件根目录（环境变量优先）。
    static QString pluginsDir();

    // 按 name 查找一个插件；找不到返回 nullptr。
    const PluginInfo *find(const QString &name) const;

signals:
    // rescan 完成后发一次，UI 据此刷新插件列表。
    void pluginsChanged();

private:
    PluginManager();

    // 探测单个插件：启动进程 -> 发 describe -> 读响应（带超时）。
    PluginInfo probePlugin(const QString &name, const QString &dir);

    QVector<PluginInfo> m_plugins;
};
