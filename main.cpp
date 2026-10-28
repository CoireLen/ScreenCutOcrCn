#include <atomic>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QMetaObject>
#include <QRunnable>
#include <QString>
#include <QStringList>
#include <QThreadPool>

#include "autogui.h"
#include "cnocr.h"
#include "hotkey.hpp"
#include "screen_capture.h"

namespace {

std::unique_ptr<cnocr> g_ocr;    // 主线程构造，识别线程只读
std::atomic_bool g_busy{false};  // 同一时刻只允许一个截图/识别流程
std::atomic_bool g_alive{true};  // 退出时阻止工作线程再向 GUI 线程投递

// 模型与字典按相对路径加载，启动前检查，缺失时给出绝对路径便于排查
bool check_models(QString* missing) {
    const QStringList required = {
        QStringLiteral("modle/cnocr136fc.onnx"),
        QStringLiteral("modle/ch_PP-OCRv3_det_infer.onnx"),
        QStringLiteral("dict/label_cn.txt"),
    };
    QStringList absent;
    for (const QString& path : required) {
        const QFileInfo info(path);
        if (!info.exists()) {
            absent << info.absoluteFilePath();
        }
    }
    if (absent.isEmpty()) {
        return true;
    }
    *missing = absent.join(QLatin1Char('\n'));
    return false;
}

class OcrTask : public QRunnable {
public:
    explicit OcrTask(cv::Mat image) : image_(std::move(image)) {}

    void run() override {
        std::wstring text;
        std::string error;
        int line_count = 0;
        try {
            if (g_ocr == nullptr) {
                throw std::runtime_error("OCR 引擎未初始化");
            }
            for (const auto& line : g_ocr->ocr(image_)) {
                text.append(line.first);
                text.push_back(L'\n');
                ++line_count;
            }
        } catch (const std::exception& e) {
            error = e.what();
        } catch (...) {
            error = "未知异常";
        }
        g_busy.store(false);
        if (!g_alive.load()) {
            return;
        }
        QMetaObject::invokeMethod(
            qApp,
            [text = std::move(text), error = std::move(error), line_count]() {
                if (!error.empty()) {
                    show_toast(
                        QStringLiteral("识别失败: %1").arg(QString::fromUtf8(error.c_str())));
                    return;
                }
                clipboard cb;
                if (!cb.setvalue(text)) {
                    show_toast(QStringLiteral("识别完成，但写入剪贴板失败，剪贴板可能被占用"));
                    return;
                }
                show_toast(line_count > 0
                               ? QStringLiteral("已识别 %1 行，已复制到剪贴板").arg(line_count)
                               : QStringLiteral("未识别到文字"));
            },
            Qt::QueuedConnection);
    }

private:
    cv::Mat image_;
};

void capture_and_ocr() {
    if (g_busy.exchange(true)) {
        show_toast(QStringLiteral("正在处理中，请稍候"));
        return;
    }
    QString error;
    const auto selection = capture_and_select(&error);
    if (!selection) {
        g_busy.store(false);
        if (!error.isEmpty()) {
            show_toast(QStringLiteral("截图失败: %1").arg(error));
        }
        return;
    }

    cv::Mat bgr;
    try {
        cv::cvtColor(selection->image(selection->region), bgr, cv::COLOR_BGRA2BGR);
    } catch (const cv::Exception& e) {
        g_busy.store(false);
        show_toast(QStringLiteral("截图处理失败: %1")
                       .arg(QString::fromUtf8(e.what())));
        return;
    }
    QThreadPool::globalInstance()->start(new OcrTask(std::move(bgr)));
}

void capture_to_clipboard() {
    if (g_busy.exchange(true)) {
        show_toast(QStringLiteral("正在处理中，请稍候"));
        return;
    }
    QString error;
    const auto selection = capture_and_select(&error);
    if (!selection) {
        g_busy.store(false);
        if (!error.isEmpty()) {
            show_toast(QStringLiteral("截图失败: %1").arg(error));
        }
        return;
    }
    // clone 保证位图数据连续（setimg 用 step 描述每行字节数）
    const cv::Mat region = selection->image(selection->region).clone();
    clipboard cb;
    const bool copied = cb.setimg(region.data, region.cols, region.rows, region.step1());
    g_busy.store(false);
    show_toast(copied ? QStringLiteral("截图已复制到剪贴板")
                      : QStringLiteral("复制到剪贴板失败，剪贴板可能被占用"));
}

}  // namespace

int main(int argc, char** argv) {
    // 控制台按 UTF-8 输出，避免中文日志乱码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);

    QString missing;
    if (!check_models(&missing)) {
        const QString message =
            QStringLiteral("缺少识别所需的模型/字典文件:\n%1\n\n当前工作目录:\n%2\n\n"
                           "请将 modle/ 与 dict/ 放在当前工作目录下，或从发布包重新解压。")
                .arg(missing, QDir::currentPath());
        std::cerr << message.toStdString() << std::endl;
        QMessageBox::critical(nullptr, QStringLiteral("ScreenCutOcrCn"), message);
        return 1;
    }

    try {
        g_ocr = std::make_unique<cnocr>();
    } catch (const std::exception& e) {
        const QString message =
            QStringLiteral("OCR 初始化失败:\n%1\n\n当前工作目录:\n%2\n\n"
                           "请确认模型文件完整、onnxruntime 与 VC++ 运行库已部署。")
                .arg(QString::fromUtf8(e.what()), QDir::currentPath());
        std::cerr << message.toStdString() << std::endl;
        QMessageBox::critical(nullptr, QStringLiteral("ScreenCutOcrCn"), message);
        return 1;
    } catch (...) {
        std::cerr << "OCR 初始化失败: 未知异常" << std::endl;
        QMessageBox::critical(nullptr, QStringLiteral("ScreenCutOcrCn"),
                              QStringLiteral("OCR 初始化失败: 未知异常"));
        return 1;
    }

    // 热键回调在独立线程，截图/UI 必须排队回 GUI 线程执行
    HotKey hotkey;
    QStringList failed_hotkeys;
    if (!hotkey.Register(MOD_ALT | MOD_NOREPEAT, 'C', [] {
            QMetaObject::invokeMethod(qApp, [] { capture_and_ocr(); }, Qt::QueuedConnection);
        })) {
        failed_hotkeys << QStringLiteral("Alt+C（识别并复制文字）");
    }
    if (!hotkey.Register(MOD_ALT | MOD_NOREPEAT, 'A', [] {
            QMetaObject::invokeMethod(qApp, [] { capture_to_clipboard(); }, Qt::QueuedConnection);
        })) {
        failed_hotkeys << QStringLiteral("Alt+A（截图并复制图片）");
    }
    if (!failed_hotkeys.isEmpty()) {
        const QString message =
            QStringLiteral("以下热键注册失败，可能已被其他程序占用:\n%1\n\n"
                           "程序仍会继续运行，但对应的功能无法通过热键触发。")
                .arg(failed_hotkeys.join(QLatin1Char('\n')));
        std::cerr << message.toStdString() << std::endl;
        QMessageBox::warning(nullptr, QStringLiteral("ScreenCutOcrCn"), message);
    }

    const int code = app.exec();
    g_alive.store(false);
    // 等待识别线程结束，避免退出后仍访问 OCR 引擎
    QThreadPool::globalInstance()->waitForDone();
    return code;
}
