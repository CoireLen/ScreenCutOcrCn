#ifndef SCREEN_CUT_OCR_MAIN_WINDOW_H
#define SCREEN_CUT_OCR_MAIN_WINDOW_H

#include <QImage>
#include <QMainWindow>

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
    void addRecognition(const QString& text);
    void log(const QString& message);

signals:
    void screenshotRequested();
    void ocrRequested();

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildUi();
    void buildTray();
    QIcon makeAppIcon() const;

    void copySelectedScreenshot();
    void copySelectedRecognition();
    void clearScreenshots();
    void clearRecognitions();
    void updateScreenshotPreview();
    void quitApplication();

    QListWidget* shot_list_ = nullptr;
    QLabel* shot_preview_ = nullptr;
    QListWidget* text_list_ = nullptr;
    QPlainTextEdit* text_preview_ = nullptr;
    QPlainTextEdit* log_view_ = nullptr;
    QSystemTrayIcon* tray_ = nullptr;
    QImage preview_image_;
    bool quitting_ = false;
};

#endif  // SCREEN_CUT_OCR_MAIN_WINDOW_H
