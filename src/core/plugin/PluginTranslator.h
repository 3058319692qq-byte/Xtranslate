// PluginTranslator - 把进程外插件包装成 Translator 接口（phase 5）。
//
// 每次翻译翻译调用启动一次插件进程，发 translate 请求，读取响应后销毁进程。
// 这样实现简单且与插件崩溃隔离；性能敏感场景可在后续迭代改为长驻池。
//
// 错误语义：
//   - 插件不可用（available=false）-> 返回 error="plugin unavailable: ..."
//   - 进程启动/读写失败 -> 返回 error="plugin io error: ..."
//   - 插件返回非空 error -> 透传该 error
//   - 插件返回合法 text -> 成功，provider="plugin:<name>"

#pragma once

#include "core/translate/Translator.h"

class PluginTranslator : public Translator
{
public:
    explicit PluginTranslator(const QString &pluginName);

    QString name() const override;
    bool requiresKey() const override { return false; }
    bool isConfigured() const override;

    QFuture<TransResult> translate(const QString &text,
                                   const QString &from,
                                   const QString &to) override;

private:
    QString m_pluginName;
};
