#include "core/plugin/PluginManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTimer>

namespace {
// describe 探测超时：长驻进程冷启动 + 解释器初始化可能要 1~2s。
constexpr int kProbeTimeoutMs = 4000;
} // namespace

PluginManager &PluginManager::instance()
{
    static PluginManager mgr;
    return mgr;
}

PluginManager::PluginManager() = default;

QString PluginManager::pluginsDir()
{
    // 环境变量存在则完全取代默认路径（语义简单：不合并、不叠加）。
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString envDir = env.value(QStringLiteral("XTRANSLATE_PLUGINS_DIR")).trimmed();
    if (!envDir.isEmpty())
        return envDir;
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/plugins");
}

const PluginInfo *PluginManager::find(const QString &name) const
{
    for (const auto &p : m_plugins)
        if (p.name == name)
            return &p;
    return nullptr;
}

QVector<PluginInfo> PluginManager::rescan()
{
    m_plugins.clear();

    const QString root = pluginsDir();
    QDir dir(root);
    if (!dir.exists()) {
        emit pluginsChanged();
        return m_plugins;
    }

    const QStringList subs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &sub : subs) {
        const QString subDir = dir.filePath(sub);
        m_plugins.append(probePlugin(sub, subDir));
    }
    emit pluginsChanged();
    return m_plugins;
}

PluginInfo PluginManager::probePlugin(const QString &name, const QString &dir)
{
    PluginInfo info;
    info.name = name;
    info.dir = dir;

    // 主入口优先级：plugin.exe > plugin.py > plugin.bat。
    // py 走系统 python 解释器（部署文档要求用户保证 PATH 中有 python）。
    const QString exePath = QDir(dir).filePath(QStringLiteral("plugin.exe"));
    const QString pyPath = QDir(dir).filePath(QStringLiteral("plugin.py"));
    const QString batPath = QDir(dir).filePath(QStringLiteral("plugin.bat"));

    if (QFileInfo::exists(exePath)) {
        info.command = exePath;
    } else if (QFileInfo::exists(pyPath)) {
        info.command = QStringLiteral("python");
        info.args = QStringList{ pyPath };
    } else if (QFileInfo::exists(batPath)) {
        info.command = batPath;
    } else {
        info.error = QStringLiteral("no plugin entry (plugin.exe|plugin.py|plugin.bat)");
        return info;
    }

    // describe 探测：启动 -> 写一行 JSON -> 读一行响应 -> 关闭。
    // 插件协议是 UTF-8 JSON 行；Windows 上 python 对管道 stdio 默认用
    // locale 编码（zh-CN 为 GBK），中文经 stdin 往返必乱码。钉死
    // PYTHONUTF8/PYTHONIOENCODING 让插件子进程 IO 恒为 UTF-8（对
    // plugin.exe/.bat 无害，多余环境变量被忽略）。
    QProcess proc;
    QProcessEnvironment procEnv = QProcessEnvironment::systemEnvironment();
    procEnv.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    procEnv.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    proc.setProcessEnvironment(procEnv);
    proc.setProgram(info.command);
    proc.setArguments(info.args);
    proc.setWorkingDirectory(dir);
    proc.start();
    if (!proc.waitForStarted(kProbeTimeoutMs)) {
        info.error = QStringLiteral("start failed: %1").arg(proc.errorString());
        return info;
    }

    const QByteArray req = QJsonDocument(QJsonObject{
        {QStringLiteral("op"), QStringLiteral("describe")}
    }).toJson(QJsonDocument::Compact);
    proc.write(req);
    proc.write("\n");
    if (!proc.waitForBytesWritten(kProbeTimeoutMs)) {
        info.error = QStringLiteral("write timeout");
        proc.kill();
        proc.waitForFinished(500);
        return info;
    }

    if (!proc.waitForReadyRead(kProbeTimeoutMs)) {
        info.error = QStringLiteral("read timeout");
        proc.kill();
        proc.waitForFinished(500);
        return info;
    }

    QByteArray line = proc.readAllStandardOutput().trimmed();
    // 多余输出（启动 banner 等）容错：取最后一行非空内容。
    const int lastNl = line.lastIndexOf('\n');
    if (lastNl >= 0)
        line = line.mid(lastNl + 1).trimmed();

    proc.kill();
    proc.waitForFinished(500);

    const QJsonDocument doc = QJsonDocument::fromJson(line);
    if (!doc.isObject()) {
        info.error = QStringLiteral("invalid JSON: %1").arg(QString::fromUtf8(line));
        return info;
    }
    const QJsonObject obj = doc.object();
    if (obj.value(QStringLiteral("name")).toString() != name) {
        info.error = QStringLiteral("name mismatch: got '%1'")
                         .arg(obj.value(QStringLiteral("name")).toString());
        return info;
    }
    info.version = obj.value(QStringLiteral("version")).toString();
    info.available = true;
    return info;
}
