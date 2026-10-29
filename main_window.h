#ifndef SCREEN_CUT_OCR_MAIN_WINDOW_H
#define SCREEN_CUT_OCR_MAIN_WINDOW_H

#include <QImage>
#include <QMainWindow>

#include "hotkey_config.h"

class QAction;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QSystemTrayIcon;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    void addScreenshot(const QImage& image);
    void addRecognition(const QString& text, const QImage& source = QImage());
    void log(const QString& message);

    const QVector<HotkeyBinding>& hotkeys() const { return hotkeys_; }

signals:
    void screenshotRequested();
    void ocrRequested();
    void longVerticalRequested();
    void longHorizontalRequested();
    // 编辑快捷键期间临时注销全局热键，避免录制时触发已有功能
    void hotkeysEditingStarted();
    void hotkeysEditingFinished();
    // 对历史截图重新发起识别
    void screenshotOcrRequested(const QImage& image);

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildUi();
    void buildTray();
    QIcon makeAppIcon() const;

    void copySelectedImage();
    void copySelectedText();
    void recognizeSelectedImage();
    void showHistoryMenu(const QPoint& pos);
    void clearHistory();
    void updatePreview();
    QListWidgetItem* findItemForImage(const QImage& image) const;
    void applyRecognitionToItem(QListWidgetItem* item, const QString& text);
    void editHotkeys();
    void updateHotkeyLabels();
    void quitApplication();

    QListWidget* history_list_ = nullptr;
    QLabel* image_preview_ = nullptr;
    QPlainTextEdit* text_preview_ = nullptr;
    QPlainTextEdit* log_view_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
    QImage preview_image_;
    QVector<HotkeyBinding> hotkeys_;
    QAction* shot_action_ = nullptr;
    QAction* ocr_action_ = nullptr;
    QAction* long_vertical_action_ = nullptr;
    QAction* long_horizontal_action_ = nullptr;
    bool quitting_ = false;
};

#endif  // SCREEN_CUT_OCR_MAIN_WINDOW_H
