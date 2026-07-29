#include "core/plugin/PluginTranslator.h"

#include "core/plugin/PluginManager.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPromise>
#include <QtConcurrent>

namespace {
// 单次翻译的进程 IO 超时：插件可能要查本地词典/调远端 API，给足 30s。
constexpr int kTranslateTimeoutMs = 30000;
} // namespace

PluginTranslator::PluginTranslator(const QString &pluginName)
    : m_pluginName(pluginName)
{
}

QString PluginTranslator::name() const
{
    return QStringLiteral("plugin:") + m_pluginName;
}

bool PluginTranslator::isConfigured() const
{
    // 插件存在且 available=true 视为已配置；UI 调度链据此过滤。
    const PluginInfo *info = PluginManager::instance().find(m_pluginName);
    return info && info->available;
}

QFuture<TransResult> PluginTranslator::translate(const QString &text,
                                                 const QString &from,
                                                 const QString &to)
{
    return QtConcurrent::run([this, text, from, to]() {
        TransResult result;
        result.provider = name();

        const PluginInfo *info = PluginManager::instance().find(m_pluginName);
        if (!info || !info->available) {
            result.error = QStringLiteral("plugin unavailable: %1")
                               .arg(info ? info->error : QStringLiteral("not found"));
            return result;
        }

        QProcess proc;
        // 与 PluginManager::probe 同款：插件协议是 UTF-8 JSON 行，钉死
        // python 子进程管道 IO 为 UTF-8（zh-CN 下默认 GBK 会把中文乱码）。
        QProcessEnvironment procEnv = QProcessEnvironment::systemEnvironment();
        procEnv.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
        procEnv.insert(QStringLiteral("PYTHONIOENCODING"),
                       QStringLiteral("utf-8"));
        proc.setProcessEnvironment(procEnv);
        proc.setProgram(info->command);
        proc.setArguments(info->args);
        proc.setWorkingDirectory(info->dir);
        proc.start();
        if (!proc.waitForStarted(kTranslateTimeoutMs)) {
            result.error = QStringLiteral("plugin io error: start: %1")
                               .arg(proc.errorString());
            return result;
        }

        const QByteArray req = QJsonDocument(QJsonObject{
            {QStringLiteral("op"),     QStringLiteral("translate")},
            {QStringLiteral("text"),   text},
            {QStringLiteral("from"),   from},
            {QStringLiteral("to"),     to},
        }).toJson(QJsonDocument::Compact);
        proc.write(req);
        proc.write("\n");
        if (!proc.waitForBytesWritten(kTranslateTimeoutMs)) {
            result.error = QStringLiteral("plugin io error: write: %1")
                               .arg(proc.errorString());
            proc.kill();
            proc.waitForFinished(500);
            return result;
        }
        if (!proc.waitForReadyRead(kTranslateTimeoutMs)) {
            result.error = QStringLiteral("plugin io error: read: %1")
                               .arg(proc.errorString());
            proc.kill();
            proc.waitForFinished(500);
            return result;
        }

        QByteArray line = proc.readAllStandardOutput().trimmed();
        const int lastNl = line.lastIndexOf('\n');
        if (lastNl >= 0)
            line = line.mid(lastNl + 1).trimmed();
        proc.kill();
        proc.waitForFinished(500);

        const QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isObject()) {
            result.error = QStringLiteral("plugin io error: invalid JSON: %1")
                               .arg(QString::fromUtf8(line));
            return result;
        }
        const QJsonObject obj = doc.object();
        result.text = obj.value(QStringLiteral("text")).toString();
        const QString pluginErr = obj.value(QStringLiteral("error")).toString();
        if (!pluginErr.isEmpty()) {
            result.error = pluginErr;
            return result;
        }
        if (result.text.isEmpty()) {
            // text+error 同时为空：协议异常。
            result.error = QStringLiteral("plugin io error: empty text and error");
            return result;
        }
        return result;
    });
}
