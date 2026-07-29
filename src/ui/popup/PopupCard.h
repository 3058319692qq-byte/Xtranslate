// PopupCard - the 划词翻译 result card (phase 3).
//
// Frameless rounded card with a soft drop shadow, always on top, shown near
// the mouse cursor (popupAt flips towards the opposite side when the card
// would leave the screen). Content: elided source text (max 3 lines) +
// translated text + buttons [复制译文 | 朗读 | 关闭].
//
// The card kicks off TranslationManager as soon as it is constructed and
// shows a loading state until the result arrives. Closes on Esc, on losing
// activation (click elsewhere) and via the 关闭 button; WA_DeleteOnClose.

#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QPropertyAnimation;

class PopupCard : public QWidget
{
    Q_OBJECT
public:
    explicit PopupCard(const QString &sourceText, QWidget *parent = nullptr);

    // Shows the card near `globalPos` (typically QCursor::pos()).
    void popupAt(const QPoint &globalPos);

protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void startTranslation();
    // Phase 7: 应用 Acrylic popup backdrop；失败回退 qss 假玻璃（不变契约）。
    void applyBackdrop();
    // Phase 7: 玻璃淡入动效（windowOpacity 0→1，180ms）。
    void startFadeIn();
    // Phase 7-fix1：从 ConfigManager 读 ui.font 应用到 resultLabel。
    // 字号用 result_pt；颜色 result_color=theme 时用 textOnGlass，否则自定义。
    void applyFontConfig();

    QString m_sourceText;
    QString m_targetLang;       // heuristic: CJK source -> en, else zh-CN
    QLabel *m_sourceLabel = nullptr;
    QLabel *m_resultLabel = nullptr;
    QPushButton *m_copyButton = nullptr;
    QPushButton *m_speakButton = nullptr;
    QString m_translatedText;
    bool m_ready = false;       // guards against closing during first show
    QPropertyAnimation *m_fadeIn = nullptr;
};
