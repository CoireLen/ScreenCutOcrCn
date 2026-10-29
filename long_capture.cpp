#include "long_capture.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <utility>
#include <vector>

#include <QEventLoop>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <opencv2/imgproc.hpp>

#include "screen_capture.h"

#ifdef _WIN32
#include <windows.h>
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#endif

namespace {

constexpr int kMaxLongSide = 20000;
constexpr int kCaptureIntervalMs = 320;
constexpr double kMinMatchScore = 0.75;

cv::Mat qimage_to_bgra(const QImage& image) {
    const QImage source = image.convertToFormat(QImage::Format_ARGB32);
    cv::Mat mat(source.height(), source.width(), CV_8UC4);
    for (int y = 0; y < source.height(); ++y) {
        std::memcpy(mat.ptr(y), source.constScanLine(y),
                    static_cast<size_t>(source.width()) * 4);
    }
    return mat;
}

cv::Rect scale_rect(const cv::Rect& rect, const cv::Size& from, const cv::Size& to) {
    if (from == to || from.width <= 0 || from.height <= 0) {
        return rect;
    }
    const double scale_x = static_cast<double>(to.width) / from.width;
    const double scale_y = static_cast<double>(to.height) / from.height;
    const cv::Rect scaled(cvRound(rect.x * scale_x), cvRound(rect.y * scale_y),
                          cvRound(rect.width * scale_x), cvRound(rect.height * scale_y));
    return scaled & cv::Rect(0, 0, to.width, to.height);
}

bool is_blackish(const cv::Mat& bgr) {
    if (bgr.empty()) {
        return true;
    }
    const cv::Scalar mean = cv::mean(bgr);
    return mean[0] + mean[1] + mean[2] < 9.0;
}

struct FeedResult {
    bool appended = false;
    int shift = 0;
    bool still = false;
    QString note;
};

// 用上一帧中部的一条模板在新帧中匹配，得到内容移动的像素数
FeedResult feed_frame(const cv::Mat& prev, const cv::Mat& frame, bool horizontal) {
    FeedResult result;
    if (prev.size() != frame.size() || prev.type() != frame.type()) {
        result.note = QStringLiteral("取帧尺寸发生变化，已跳过该帧");
        return result;
    }
    cv::Mat prev_gray;
    cv::Mat frame_gray;
    cv::cvtColor(prev, prev_gray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(frame, frame_gray, cv::COLOR_BGR2GRAY);

    cv::Mat templ;
    cv::Mat search;
    int reference = 0;
    if (!horizontal) {
        const int height = prev.rows;
        const int width = prev.cols;
        const int thickness = std::max(16, height / 5);
        const int margin = std::max(2, width / 20);
        const int inner_width = width - 2 * margin;
        const int y_reference = std::max(0, (height - thickness) / 2);
        if (inner_width < 8 || thickness < 8) {
            result.note = QStringLiteral("选区过小，无法拼接");
            return result;
        }
        templ = prev_gray(cv::Rect(margin, y_reference, inner_width, thickness));
        search = frame_gray(cv::Rect(margin, 0, inner_width, height));
        reference = y_reference;
    } else {
        const int height = prev.rows;
        const int width = prev.cols;
        const int thickness = std::max(16, width / 5);
        const int margin = std::max(2, height / 20);
        const int inner_height = height - 2 * margin;
        const int x_reference = std::max(0, (width - thickness) / 2);
        if (inner_height < 8 || thickness < 8) {
            result.note = QStringLiteral("选区过小，无法拼接");
            return result;
        }
        templ = prev_gray(cv::Rect(x_reference, margin, thickness, inner_height));
        search = frame_gray(cv::Rect(0, margin, width, inner_height));
        reference = x_reference;
    }

    // 纯色选区没有可匹配的特征，直接跳过，避免无效匹配
    cv::Scalar mean;
    cv::Scalar stddev;
    cv::meanStdDev(templ, mean, stddev);
    if (stddev[0] < 1.0) {
        result.still = true;
        result.note = QStringLiteral("画面缺少可匹配的内容");
        return result;
    }

    cv::Mat scores;
    cv::matchTemplate(search, templ, scores, cv::TM_CCOEFF_NORMED);
    double min_value = 0.0;
    double max_value = 0.0;
    cv::Point min_location;
    cv::Point max_location;
    cv::minMaxLoc(scores, &min_value, &max_value, &min_location, &max_location);
    if (max_value < kMinMatchScore) {
        result.note = QStringLiteral("未匹配到重叠区域（滚动过快或内容变化过大）");
        return result;
    }

    const int offset = horizontal ? max_location.x : max_location.y;
    const int shift = reference - offset;
    const int span = horizontal ? prev.cols : prev.rows;
    if (std::abs(shift) <= 1) {
        result.still = true;
        return result;
    }
    if (shift < 0) {
        result.note = QStringLiteral("检测到反向滚动，已忽略");
        return result;
    }
    if (shift >= static_cast<int>(span * 0.85)) {
        result.note = QStringLiteral("滚动距离过大，请放慢滚动速度");
        return result;
    }

    result.appended = true;
    result.shift = shift;
    return result;
}

void place_bar(QWidget* bar, const cv::Rect& region, const cv::Size& image_size,
               QScreen* screen) {
    if (bar == nullptr || screen == nullptr) {
        return;
    }
    const QRect available = screen->availableGeometry();
    const QRect screen_geometry = screen->geometry();
    const double scale_x =
        screen_geometry.width() > 0 ? static_cast<double>(image_size.width) / screen_geometry.width() : 1.0;
    const double scale_y =
        screen_geometry.height() > 0 ? static_cast<double>(image_size.height) / screen_geometry.height() : 1.0;
    const QRect selection(
        screen_geometry.left() + qRound(region.x / std::max(scale_x, 0.01)),
        screen_geometry.top() + qRound(region.y / std::max(scale_y, 0.01)),
        qRound(region.width / std::max(scale_x, 0.01)),
        qRound(region.height / std::max(scale_y, 0.01)));

    bar->adjustSize();
    const int x = available.left() + (available.width() - bar->width()) / 2;
    int y = available.top() + 24;
    if (selection.top() < y + bar->height() + 16) {
        y = available.bottom() - bar->height() - 24;
    }
    bar->move(x, y);
}

class LongCaptureBar : public QWidget {
public:
    explicit LongCaptureBar(bool horizontal, QWidget* parent = nullptr)
        : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
          horizontal_(horizontal) {
        setWindowTitle(horizontal ? QStringLiteral("横向长截图") : QStringLiteral("竖向长截图"));

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(14, 10, 14, 10);
        layout->setSpacing(6);

        auto* title = new QLabel(horizontal ? QStringLiteral("横向长截图")
                                            : QStringLiteral("竖向长截图"));
        QFont title_font = title->font();
        title_font.setBold(true);
        title_font.setPointSizeF(title_font.pointSizeF() + 1);
        title->setFont(title_font);

        hint_ = new QLabel(horizontal
                               ? QStringLiteral("在选区内按住 Shift 滚动或拖动横向滚动条，程序自动拼接")
                               : QStringLiteral("将鼠标放在选区内滚动页面，程序自动拼接"),
                           this);
        hint_->setWordWrap(true);
        hint_->setStyleSheet(QStringLiteral("color:#b8b8b8;"));

        status_ = new QLabel(this);
        warning_ = new QLabel(this);
        warning_->setWordWrap(true);
        warning_->setStyleSheet(QStringLiteral("color:#ff8a80;"));
        warning_->hide();

        auto* buttons = new QHBoxLayout;
        auto* done = new QPushButton(QStringLiteral("完成"), this);
        auto* cancel = new QPushButton(QStringLiteral("取消"), this);
        buttons->addStretch();
        buttons->addWidget(done);
        buttons->addWidget(cancel);

        layout->addWidget(title);
        layout->addWidget(hint_);
        layout->addWidget(status_);
        layout->addWidget(warning_);
        layout->addLayout(buttons);

        setObjectName(QStringLiteral("longCaptureBar"));
        setStyleSheet(QStringLiteral(
            "QWidget#longCaptureBar { background:#202020; border:1px solid #3d3d3d; }"
            "QLabel { color:#f0f0f0; background:transparent; }"
            "QPushButton { background:#3a3a3a; color:#f0f0f0; padding:4px 18px;"
            " border:1px solid #4a4a4a; border-radius:4px; }"
            "QPushButton:hover { background:#4a4a4a; }"));
        setFixedWidth(400);

        connect(done, &QPushButton::clicked, this, [this] { finish(true); });
        connect(cancel, &QPushButton::clicked, this, [this] { finish(false); });
        adjustSize();
    }

    std::function<void(bool)> on_finish;

    void update_status(int total, int frames) {
        status_->setText(QStringLiteral("已拼接：%1 px · %2 帧").arg(total).arg(frames));
    }

    void show_warning(const QString& text) {
        if (warning_->text() == text && warning_->isVisible()) {
            return;
        }
        warning_->setText(text);
        warning_->show();
        adjustSize();
    }

    void clear_warning() {
        if (warning_->isHidden()) {
            return;
        }
        warning_->clear();
        warning_->hide();
        adjustSize();
    }

protected:
    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            finish(true);
            return;
        }
        if (event->key() == Qt::Key_Escape) {
            finish(false);
            return;
        }
        QWidget::keyPressEvent(event);
    }

private:
    void finish(bool accepted) {
        if (finished_) {
            return;
        }
        finished_ = true;
        hide();
        if (on_finish) {
            on_finish(accepted);
        }
    }

    bool horizontal_ = false;
    bool finished_ = false;
    QLabel* hint_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* warning_ = nullptr;
};

}  // namespace

std::optional<LongCaptureResult> capture_long_screenshot(bool horizontal, QString* error) {
    const auto fail = [error](const QString& reason) -> std::optional<LongCaptureResult> {
        if (error != nullptr) {
            *error = reason;
        }
        return std::nullopt;
    };

    QString select_error;
    const auto selection = capture_and_select(&select_error);
    if (!selection) {
        if (!select_error.isEmpty()) {
            return fail(select_error);
        }
        return std::nullopt;  // 用户取消
    }
    if (selection->region.width < 8 || selection->region.height < 8) {
        return fail(QStringLiteral("选区过小"));
    }

    const cv::Size reference_size = selection->image.size();
    const cv::Rect region = selection->region;

    cv::Mat prev;
    cv::cvtColor(selection->image(region).clone(), prev, cv::COLOR_BGRA2BGR);
    if (prev.empty()) {
        return fail(QStringLiteral("选区内容为空"));
    }

    std::vector<cv::Mat> pieces;
    pieces.push_back(prev.clone());
    int total = horizontal ? prev.cols : prev.rows;
    int frames = 1;

    LongCaptureBar bar(horizontal);
    place_bar(&bar, region, reference_size, QGuiApplication::primaryScreen());
    bar.update_status(total, frames);
    bar.show();
    bar.raise();
#ifdef _WIN32
    // 控制条不参与屏幕捕获，避免被抓进拼接结果
    SetWindowDisplayAffinity(reinterpret_cast<HWND>(bar.winId()), WDA_EXCLUDEFROMCAPTURE);
#endif
    bar.activateWindow();
    bar.setFocus(Qt::ActiveWindowFocusReason);

    QEventLoop loop;
    bool accepted = false;
    bar.on_finish = [&accepted, &loop](bool ok) {
        accepted = ok;
        loop.quit();
    };

    QTimer timer;
    timer.setInterval(kCaptureIntervalMs);
    QObject::connect(&timer, &QTimer::timeout, &bar, [&] {
        const QImage full = grab_primary_screen();
        if (full.isNull()) {
            return;
        }
        const cv::Mat full_bgra = qimage_to_bgra(full);
        const cv::Rect rect = scale_rect(region, reference_size, full_bgra.size());
        if (rect.width < 8 || rect.height < 8) {
            return;
        }
        cv::Mat frame;
        cv::cvtColor(full_bgra(rect), frame, cv::COLOR_BGRA2BGR);
        if (is_blackish(frame)) {
            return;
        }

        const FeedResult feed = feed_frame(prev, frame, horizontal);
        if (feed.appended) {
            if (total + feed.shift > kMaxLongSide) {
                bar.show_warning(
                    QStringLiteral("已达到最大长度 %1 px，请点击“完成”").arg(kMaxLongSide));
                return;
            }
            const int span = horizontal ? frame.cols : frame.rows;
            if (horizontal) {
                pieces.push_back(frame.colRange(span - feed.shift, span).clone());
            } else {
                pieces.push_back(frame.rowRange(span - feed.shift, span).clone());
            }
            prev = frame;
            total += feed.shift;
            ++frames;
            bar.clear_warning();
            bar.update_status(total, frames);
        } else if (!feed.note.isEmpty() && !feed.still) {
            bar.show_warning(feed.note);
        }
    });
    timer.start();
    loop.exec();
    timer.stop();
    bar.hide();

    if (!accepted) {
        return std::nullopt;
    }
    if (pieces.empty()) {
        return fail(QStringLiteral("长截图没有可用画面"));
    }

    cv::Mat canvas;
    if (horizontal) {
        cv::hconcat(pieces, canvas);
    } else {
        cv::vconcat(pieces, canvas);
    }
    if (canvas.empty()) {
        return fail(QStringLiteral("长截图拼接失败"));
    }

    LongCaptureResult result;
    result.image = std::move(canvas);
    result.frames = frames;
    return result;
}
