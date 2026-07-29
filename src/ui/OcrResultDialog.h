// OcrResultDialog - editable result window for the "截图OCR" action.
//
// Shows the recognized lines joined by '\n' in an editable QPlainTextEdit.
// Buttons: 复制全部 | 翻译 (sends the current text back to the main window's
// source box via translateRequested) | 关闭.

#pragma once

#include <QDialog>

class QPlainTextEdit;

class OcrResultDialog : public QDialog
{
    Q_OBJECT
public:
    explicit OcrResultDialog(const QString &text, QWidget *parent = nullptr);

    QString currentText() const;

signals:
    void translateRequested(const QString &text);

protected:
    void showEvent(QShowEvent *event) override;

private:
    // Phase 7: 应用/刷新顶层窗口 DWM backdrop（与 MainWindow 同策略）。
    void applyBackdrop();
    // Phase 7-fix1：从 ConfigManager 读 ui.font 应用到 m_edit。
    // 字号用 result_pt；颜色 result_color=theme 时用 textOnGlass，否则自定义。
    void applyFontConfig();

    QPlainTextEdit *m_edit = nullptr;
};
