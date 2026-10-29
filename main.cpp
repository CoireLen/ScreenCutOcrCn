#include <atomic>
#include <chrono>
#include <cstring>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMessageBox>
#include <QMetaObject>
#include <QRunnable>
#include <QString>
#include <QStringList>
#include <QThreadPool>

#include "autogui.h"
#include "cnocr.h"
#include "hotkey_config.h"
#include "hotkey_service.h"
#include "long_capture.h"
#include "main_window.h"
#include "screen_capture.h"

namespace {

std::unique_ptr<cnocr> g_ocr;              // 主线程构造，识别线程只读
std::atomic_bool g_busy{false};            // 同一时刻只允许一个截图/识别流程
std::atomic_bool g_alive{true};            // 退出时阻止工作线程再向 GUI 线程投递
MainWindow* g_window = nullptr;            // 仅在 GUI 线程访问
std::unique_ptr<HotkeyService> g_hotkeys;  // 仅在 GUI 线程访问

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

QImage mat_to_qimage_bgra(const cv::Mat& mat) {
    if (mat.empty() || mat.type() != CV_8UC4) {
        return {};
    }
    const QImage view(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step),
                      QImage::Format_ARGB32);
    return view.copy();
}

cv::Mat qimage_to_bgr(const QImage& image) {
    const QImage source = image.convertToFormat(QImage::Format_ARGB32);
    if (source.isNull()) {
        return {};
    }
    cv::Mat bgra(source.height(), source.width(), CV_8UC4);
    for (int y = 0; y < source.height(); ++y) {
        std::memcpy(bgra.ptr(y), source.constScanLine(y),
                    static_cast<size_t>(source.width()) * 4);
    }
    cv::Mat bgr;
    cv::cvtColor(bgra, bgr, cv::COLOR_BGRA2BGR);
    return bgr;
}

class OcrTask : public QRunnable {
public:
    OcrTask(cv::Mat image, QImage source)
        : image_(std::move(image)), source_(std::move(source)) {}

    void run() override {
        const auto start = std::chrono::steady_clock::now();
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
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - start)
                                    .count();
        g_busy.store(false);
        if (!g_alive.load()) {
            return;
        }
        QMetaObject::invokeMethod(
            qApp,
            [text = std::move(text), error = std::move(error), line_count, elapsed_ms,
             source = source_]() {
                if (!error.empty()) {
                    const QString message = QString::fromUtf8(error.c_str());
                    show_toast(QStringLiteral("识别失败: %1").arg(message));
                    if (g_window != nullptr) {
                        g_window->log(QStringLiteral("识别失败: %1").arg(message));
                    }
                    return;
                }
                QString content = QString::fromStdWString(text);
                if (content.endsWith(QLatin1Char('\n'))) {
                    content.chop(1);
                }
                clipboard cb;
                const bool copied = cb.setvalue(content.toStdWString());
                if (g_window != nullptr) {
                    g_window->addRecognition(content, source);
                    g_window->log(QStringLiteral("识别完成：%1 行，耗时 %2 ms")
                                      .arg(line_count)
                                      .arg(elapsed_ms));
                    if (!copied) {
                        g_window->log(QStringLiteral("写入剪贴板失败，剪贴板可能被占用"));
                    }
                }
                if (!copied) {
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
    QImage source_;
};

void note(const QString& message) {
    if (g_window != nullptr) {
        g_window->log(message);
    }
}

void capture_and_ocr() {
    if (g_busy.exchange(true)) {
        show_toast(QStringLiteral("正在处理中，请稍候"));
        note(QStringLiteral("忽略触发：上一个截图/识别任务尚未完成"));
        return;
    }
    note(QStringLiteral("开始框选（识别流程）"));
    QString error;
    const auto selection = capture_and_select(&error);
    if (!selection) {
        g_busy.store(false);
        if (!error.isEmpty()) {
            show_toast(QStringLiteral("截图失败: %1").arg(error));
            note(QStringLiteral("截图失败: %1").arg(error));
        } else {
            note(QStringLiteral("已取消框选"));
        }
        return;
    }

    const cv::Mat region = selection->image(selection->region);
    const QImage shot = mat_to_qimage_bgra(region);
    if (g_window != nullptr) {
        g_window->addScreenshot(shot);
    }

    cv::Mat bgr;
    try {
        cv::cvtColor(region, bgr, cv::COLOR_BGRA2BGR);
    } catch (const cv::Exception& e) {
        g_busy.store(false);
        const QString message = QString::fromUtf8(e.what());
        show_toast(QStringLiteral("截图处理失败: %1").arg(message));
        note(QStringLiteral("截图处理失败: %1").arg(message));
        return;
    }
    QThreadPool::globalInstance()->start(new OcrTask(std::move(bgr), shot));
}

void ocr_existing_image(const QImage& image) {
    if (image.isNull()) {
        show_toast(QStringLiteral("截图数据已失效，无法识别"));
        note(QStringLiteral("忽略识别：截图数据已失效"));
        return;
    }
    if (g_busy.exchange(true)) {
        show_toast(QStringLiteral("正在处理中，请稍候"));
        note(QStringLiteral("忽略触发：上一个截图/识别任务尚未完成"));
        return;
    }
    note(QStringLiteral("开始识别历史截图 %1 × %2").arg(image.width()).arg(image.height()));
    cv::Mat bgr;
    try {
        bgr = qimage_to_bgr(image);
    } catch (const cv::Exception& e) {
        g_busy.store(false);
        const QString message = QString::fromUtf8(e.what());
        show_toast(QStringLiteral("识别失败: %1").arg(message));
        note(QStringLiteral("识别失败: %1").arg(message));
        return;
    }
    if (bgr.empty()) {
        g_busy.store(false);
        show_toast(QStringLiteral("识别失败: 截图数据无效"));
        note(QStringLiteral("识别失败: 截图数据无效"));
        return;
    }
    QThreadPool::globalInstance()->start(new OcrTask(std::move(bgr), image));
}

void capture_to_clipboard() {
    if (g_busy.exchange(true)) {
        show_toast(QStringLiteral("正在处理中，请稍候"));
        note(QStringLiteral("忽略触发：上一个截图/识别任务尚未完成"));
        return;
    }
    note(QStringLiteral("开始框选（截图流程）"));
    QString error;
    const auto selection = capture_and_select(&error);
    if (!selection) {
        g_busy.store(false);
        if (!error.isEmpty()) {
            show_toast(QStringLiteral("截图失败: %1").arg(error));
            note(QStringLiteral("截图失败: %1").arg(error));
        } else {
            note(QStringLiteral("已取消框选"));
        }
        return;
    }
    // clone 保证位图数据连续（setimg 用 step 描述每行字节数）
    const cv::Mat region = selection->image(selection->region).clone();
    if (g_window != nullptr) {
        g_window->addScreenshot(mat_to_qimage_bgra(region));
    }
    clipboard cb;
    const bool copied = cb.setimg(region.data, region.cols, region.rows, region.step1());
    g_busy.store(false);
    if (copied) {
        show_toast(QStringLiteral("截图已复制到剪贴板"));
        note(QStringLiteral("截图 %1 × %2 已复制到剪贴板")
                 .arg(region.cols)
                 .arg(region.rows));
    } else {
        show_toast(QStringLiteral("复制到剪贴板失败，剪贴板可能被占用"));
        note(QStringLiteral("复制截图到剪贴板失败"));
    }
}

void capture_long(bool horizontal) {
    if (g_busy.exchange(true)) {
        show_toast(QStringLiteral("正在处理中，请稍候"));
        note(QStringLiteral("忽略触发：上一个截图/识别任务尚未完成"));
        return;
    }
    note(horizontal ? QStringLiteral("开始框选（横向长截图）")
                    : QStringLiteral("开始框选（竖向长截图）"));
    QString error;
    const auto result = capture_long_screenshot(horizontal, &error);
    if (!result) {
        g_busy.store(false);
        if (!error.isEmpty()) {
            show_toast(QStringLiteral("长截图失败: %1").arg(error));
            note(QStringLiteral("长截图失败: %1").arg(error));
        } else {
            note(QStringLiteral("已取消长截图"));
        }
        return;
    }
    if (result->image.empty()) {
        g_busy.store(false);
        show_toast(QStringLiteral("长截图失败: 拼接结果为空"));
        note(QStringLiteral("长截图失败: 拼接结果为空"));
        return;
    }

    cv::Mat bgra;
    try {
        cv::cvtColor(result->image, bgra, cv::COLOR_BGR2BGRA);
    } catch (const cv::Exception& e) {
        g_busy.store(false);
        const QString message = QString::fromUtf8(e.what());
        show_toast(QStringLiteral("长截图处理失败: %1").arg(message));
        note(QStringLiteral("长截图处理失败: %1").arg(message));
        return;
    }

    if (g_window != nullptr) {
        g_window->addScreenshot(mat_to_qimage_bgra(bgra));
    }
    // cvtColor 的输出连续，可直接交给 setimg
    clipboard cb;
    const bool copied = cb.setimg(bgra.data, bgra.cols, bgra.rows, bgra.step1());
    g_busy.store(false);
    if (copied) {
        show_toast(QStringLiteral("长截图已复制到剪贴板"));
        note(QStringLiteral("长截图 %1 × %2（%3 帧）已复制到剪贴板")
                 .arg(bgra.cols)
                 .arg(bgra.rows)
                 .arg(result->frames));
    } else {
        show_toast(QStringLiteral("长截图完成，但复制到剪贴板失败"));
        note(QStringLiteral("长截图 %1 × %2（%3 帧），复制到剪贴板失败")
                 .arg(bgra.cols)
                 .arg(bgra.rows)
                 .arg(result->frames));
    }
}

void apply_hotkeys(const QVector<HotkeyBinding>& bindings) {
    if (g_hotkeys == nullptr) {
        return;
    }
    // 热键回调在消息线程，必须排队回 GUI 线程执行
    auto post = [](std::function<void()> action) {
        return [action = std::move(action)] {
            QMetaObject::invokeMethod(qApp, action, Qt::QueuedConnection);
        };
    };
    QHash<QString, HotkeyService::Handler> handlers;
    handlers.insert(QStringLiteral("shot"), post([] { capture_to_clipboard(); }));
    handlers.insert(QStringLiteral("ocr"), post([] { capture_and_ocr(); }));
    handlers.insert(QStringLiteral("long_vertical"), post([] { capture_long(false); }));
    handlers.insert(QStringLiteral("long_horizontal"), post([] { capture_long(true); }));

    QStringList failures;
    g_hotkeys->apply(bindings, handlers, &failures);
    if (failures.isEmpty()) {
        QStringList parts;
        for (const HotkeyBinding& binding : bindings) {
            if (!binding.sequence.isEmpty()) {
                parts << QStringLiteral("%1 %2").arg(hotkey_text(binding.sequence), binding.title);
            }
        }
        note(parts.isEmpty() ? QStringLiteral("全局快捷键已全部禁用")
                             : QStringLiteral("全局快捷键已生效：%1").arg(parts.join(QStringLiteral("，"))));
        return;
    }
    for (const QString& failure : failures) {
        note(QStringLiteral("快捷键注册失败：%1").arg(failure));
    }
    show_toast(QStringLiteral("有 %1 个快捷键注册失败，详见日志").arg(failures.size()));
}

}  // namespace

int main(int argc, char** argv) {
    // 控制台按 UTF-8 输出，避免中文日志乱码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ScreenCutOcrCn"));
    QApplication::setOrganizationName(QStringLiteral("CoireLen"));
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

    MainWindow window;
    g_window = &window;
    QObject::connect(&window, &MainWindow::screenshotRequested, &window,
                     [] { capture_to_clipboard(); });
    QObject::connect(&window, &MainWindow::ocrRequested, &window,
                     [] { capture_and_ocr(); });
    QObject::connect(&window, &MainWindow::longVerticalRequested, &window,
                     [] { capture_long(false); });
    QObject::connect(&window, &MainWindow::longHorizontalRequested, &window,
                     [] { capture_long(true); });
    QObject::connect(&window, &MainWindow::hotkeysEditingStarted, &window,
                     [] { g_hotkeys->clear(); });
    QObject::connect(&window, &MainWindow::hotkeysEditingFinished, &window,
                     [] { apply_hotkeys(g_window->hotkeys()); });
    QObject::connect(&window, &MainWindow::screenshotOcrRequested, &window,
                     [](const QImage& image) { ocr_existing_image(image); });
    window.log(QStringLiteral("ScreenCutOcrCn 启动，OCR 引擎加载完成"));
    window.log(QStringLiteral("工作目录: %1").arg(QDir::currentPath()));
    window.show();

    g_hotkeys = std::make_unique<HotkeyService>();
    apply_hotkeys(window.hotkeys());

    const int code = app.exec();
    g_alive.store(false);
    g_hotkeys->clear();
    // 等待识别线程结束，避免退出后仍访问 OCR 引擎
    QThreadPool::globalInstance()->waitForDone();
    g_hotkeys.reset();
    g_window = nullptr;
    return code;
}
