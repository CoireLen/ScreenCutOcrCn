#include "screen_capture.h"

#include <cstring>

#include <QDialog>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QScreen>
#include <QShowEvent>
#include <QTimer>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {

constexpr int kMinSelection = 3;
const QColor kAccent(0, 174, 255);

QImage grab_screen_gdi(const QRect& native_rect) {
    if (native_rect.isEmpty()) {
        return {};
    }
    HDC screen_dc = GetDC(nullptr);
    if (screen_dc == nullptr) {
        return {};
    }
    HDC memory_dc = CreateCompatibleDC(screen_dc);
    HBITMAP bitmap = CreateCompatibleBitmap(screen_dc, native_rect.width(), native_rect.height());
    if (memory_dc == nullptr || bitmap == nullptr) {
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        if (memory_dc != nullptr) {
            DeleteDC(memory_dc);
        }
        ReleaseDC(nullptr, screen_dc);
        return {};
    }

    HGDIOBJ previous = SelectObject(memory_dc, bitmap);
    const BOOL copied = BitBlt(memory_dc, 0, 0, native_rect.width(), native_rect.height(),
                               screen_dc, native_rect.x(), native_rect.y(), SRCCOPY | CAPTUREBLT);
    QImage image;
    if (copied) {
        image = QImage(native_rect.size(), QImage::Format_RGB32);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = native_rect.width();
        info.bmiHeader.biHeight = -native_rect.height();  // 负值表示自上而下
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        if (GetDIBits(memory_dc, bitmap, 0, native_rect.height(), image.bits(), &info,
                      DIB_RGB_COLORS) == 0) {
            image = QImage();
        }
    }

    SelectObject(memory_dc, previous);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(nullptr, screen_dc);
    return image;
}

bool is_uniform_black(const QImage& image) {
    if (image.isNull()) {
        return true;
    }
    const QImage thumb = image.scaled(32, 32, Qt::IgnoreAspectRatio, Qt::FastTransformation)
                             .convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < thumb.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(thumb.constScanLine(y));
        for (int x = 0; x < thumb.width(); ++x) {
            if (qRed(row[x]) > 8 || qGreen(row[x]) > 8 || qBlue(row[x]) > 8) {
                return false;
            }
        }
    }
    return true;
}

// Qt 的 geometry() 与窗口实际像素在部分 DPI 配置下不一致，
// 统一用 Win32 监视器信息取主屏的物理区域
QRect primary_native_rect() {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    const POINT origin{0, 0};
    if (GetMonitorInfo(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &info) != 0) {
        return QRect(info.rcMonitor.left, info.rcMonitor.top,
                     info.rcMonitor.right - info.rcMonitor.left,
                     info.rcMonitor.bottom - info.rcMonitor.top);
    }
    return {};
}

// QScreen::grabWindow 必须在 GUI 线程调用；个别显卡/远程桌面下仍会返回全黑，
// 此时退回 GDI 逐次新建 DC 抓取，避免复用旧位图导致的长期黑屏。
QImage grab_screen(QScreen* screen) {
    const QPixmap pixmap = screen->grabWindow(0);
    if (!pixmap.isNull()) {
        const QImage image = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
        if (!is_uniform_black(image)) {
            return image;
        }
    }
    QImage fallback = grab_screen_gdi(primary_native_rect());
    return fallback;
}

// QImage -> cv::Mat（BGRA，逐行拷贝以正确处理 stride）
cv::Mat qimage_to_bgra(const QImage& image) {
    const QImage source = image.convertToFormat(QImage::Format_ARGB32);
    cv::Mat mat(source.height(), source.width(), CV_8UC4);
    for (int y = 0; y < source.height(); ++y) {
        std::memcpy(mat.ptr(y), source.constScanLine(y), static_cast<size_t>(source.width()) * 4);
    }
    return mat;
}

// 用 Win32 物理坐标居中，避开 Qt 逻辑/物理坐标不一致
void place_toast(QLabel* widget) {
    const HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    RECT window{};
    if (hwnd == nullptr || GetWindowRect(hwnd, &window) == 0) {
        return;
    }
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &info) == 0) {
        return;
    }
    const int width = window.right - window.left;
    const int work_width = info.rcWork.right - info.rcWork.left;
    const int work_height = info.rcWork.bottom - info.rcWork.top;
    const int x = info.rcWork.left + (work_width - width) / 2;
    const int y = info.rcWork.top + work_height / 8;
    SetWindowPos(hwnd, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
} 

QRect scale_to_image(const QRect& rect, const QImage& image, const QSize& widget_size) {
    if (widget_size.isEmpty() || image.isNull()) {
        return {};
    }
    const double scale_x = static_cast<double>(image.width()) / widget_size.width();
    const double scale_y = static_cast<double>(image.height()) / widget_size.height();
    return QRect(QPoint(qRound(rect.x() * scale_x), qRound(rect.y() * scale_y)),
                 QSize(qRound(rect.width() * scale_x), qRound(rect.height() * scale_y)));
}

class RegionSelector : public QDialog {
public:
    explicit RegionSelector(const QImage& background, QWidget* parent = nullptr)
        : QDialog(parent), background_(background) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                       Qt::NoDropShadowWindowHint);
        setWindowState(Qt::WindowFullScreen);
        setCursor(Qt::CrossCursor);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
    }

    // 选区在截图原图中的位置
    QRect image_region() const {
        return scale_to_image(selection_, background_, size())
            .intersected(QRect(QPoint(0, 0), background_.size()));
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.drawImage(rect(), background_);

        const QRect selection = selection_in_widget();
        if (selection.isNull()) {
            painter.fillRect(rect(), QColor(0, 0, 0, 90));
            paint_crosshair(painter);
        } else {
            painter.fillRect(rect(), QColor(0, 0, 0, 120));
            painter.drawImage(selection, background_,
                              scale_to_image(selection, background_, size()));
            painter.setPen(QPen(kAccent, 1));
            painter.drawRect(selection.adjusted(0, 0, -1, -1));
            paint_handles(painter, selection);
            paint_size_label(painter, selection);
        }
        paint_hint(painter);
    }

    void showEvent(QShowEvent* event) override {
        QDialog::showEvent(event);
        activateWindow();
        raise();
        setFocus(Qt::ActiveWindowFocusReason);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::RightButton) {
            reject();
            return;
        }
        if (event->button() == Qt::LeftButton) {
            origin_ = event->position().toPoint();
            cursor_ = origin_;
            dragging_ = true;
            update();
        }
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        cursor_ = event->position().toPoint();
        update();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton || !dragging_) {
            return;
        }
        dragging_ = false;
        const QRect selection = QRect(origin_, event->position().toPoint()).normalized();
        if (selection.width() < kMinSelection || selection.height() < kMinSelection) {
            update();
            return;
        }
        selection_ = selection;
        accept();
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape) {
            reject();
            return;
        }
        QDialog::keyPressEvent(event);
    }

private:
    QRect selection_in_widget() const {
        return dragging_ ? QRect(origin_, cursor_).normalized() : QRect();
    }

    void paint_crosshair(QPainter& painter) {
        if (cursor_.x() < 0 || cursor_.y() < 0) {
            return;
        }
        painter.setPen(QPen(QColor(255, 255, 255, 90), 1));
        painter.drawLine(0, cursor_.y(), width(), cursor_.y());
        painter.drawLine(cursor_.x(), 0, cursor_.x(), height());
    }

    void paint_handles(QPainter& painter, const QRect& selection) {
        constexpr int size = 6;
        painter.setPen(QPen(kAccent, 1));
        painter.setBrush(Qt::white);
        const QPoint corners[4] = {selection.topLeft(), selection.topRight(),
                                   selection.bottomLeft(), selection.bottomRight()};
        for (const QPoint& corner : corners) {
            painter.drawRect(QRect(corner - QPoint(size / 2, size / 2), QSize(size, size)));
        }
        painter.setBrush(Qt::NoBrush);
    }

    void paint_size_label(QPainter& painter, const QRect& selection) {
        const QString text =
            QStringLiteral("%1 × %2").arg(selection.width()).arg(selection.height());
        QFont label_font = font();
        label_font.setBold(true);
        painter.setFont(label_font);
        const QFontMetrics metrics(label_font);
        const QRect box(QPoint(0, 0), metrics.boundingRect(text).size() + QSize(16, 8));
        QPoint position = cursor_ + QPoint(16, 20);
        if (position.x() + box.width() > width()) {
            position.setX(width() - box.width() - 4);
        }
        if (position.y() + box.height() > height()) {
            position.setY(cursor_.y() - box.height() - 8);
        }
        const QRect target(position, box.size());
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(20, 20, 20, 210));
        painter.drawRoundedRect(target, 4, 4);
        painter.setPen(QColor(240, 240, 240));
        painter.drawText(target, Qt::AlignCenter, text);
        painter.setBrush(Qt::NoBrush);
    }

    void paint_hint(QPainter& painter) {
        const QString hint = QStringLiteral("拖拽选择区域    Esc / 右键取消");
        QFont hint_font = font();
        hint_font.setPointSizeF(hint_font.pointSizeF() + 1);
        painter.setFont(hint_font);
        const QFontMetrics metrics(hint_font);
        const QSize box_size(metrics.horizontalAdvance(hint) + 32, metrics.height() + 16);
        const QRect target(QPoint((width() - box_size.width()) / 2, 24), box_size);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(20, 20, 20, 200));
        painter.drawRoundedRect(target, 8, 8);
        painter.setPen(QColor(235, 235, 235));
        painter.drawText(target, Qt::AlignCenter, hint);
        painter.setBrush(Qt::NoBrush);
    }

    QImage background_;
    QPoint origin_;
    QPoint cursor_ = QPoint(-1, -1);
    QRect selection_;
    bool dragging_ = false;
};

}  // namespace

std::optional<ScreenSelection> capture_and_select(QString* error) {
    const auto fail = [error](const QString& reason) -> std::optional<ScreenSelection> {
        if (error != nullptr) {
            *error = reason;
        }
        return std::nullopt;
    };

    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        return fail(QStringLiteral("未检测到主屏幕"));
    }
    const QImage image = grab_screen(screen);
    if (image.isNull()) {
        return fail(QStringLiteral("屏幕抓取失败（Qt 与 GDI 均未返回图像）"));
    }

    RegionSelector selector(image);
    selector.setScreen(screen);
    selector.showFullScreen();
    if (selector.exec() != QDialog::Accepted) {
        return std::nullopt;  // 用户取消，不算错误
    }

    const QRect region = selector.image_region();
    if (region.isEmpty()) {
        return fail(QStringLiteral("选区无效"));
    }
    return ScreenSelection{ qimage_to_bgra(image),
                            cv::Rect(region.x(), region.y(), region.width(), region.height()) };
}

void show_toast(const QString& text) {
    static QPointer<QLabel> current;
    if (!current.isNull()) {
        current->close();  // 上一个提示由 WA_DeleteOnClose 接管释放
    }

    auto* label = new QLabel;
    label->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    label->setAttribute(Qt::WA_ShowWithoutActivating);
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    label->setAttribute(Qt::WA_DeleteOnClose);
    label->setAlignment(Qt::AlignCenter);
    // OCR 文本或错误信息可能含尖括号，必须按纯文本显示
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setStyleSheet(QStringLiteral(
        "background-color: rgba(24, 24, 24, 215); color: #f0f0f0;"
        "padding: 10px 18px; border-radius: 8px; font-size: 14px;"));

    int max_width = 640;
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        max_width = qMax(240, screen->availableGeometry().width() * 3 / 4);
    }
    label->setMaximumWidth(max_width);
    label->setText(text);
    label->adjustSize();

    current = label;
    label->show();
    place_toast(label);
    label->raise();
    QTimer::singleShot(2600, label, &QWidget::close);
}
