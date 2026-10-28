#ifndef SCREEN_CAPTURE_H
#define SCREEN_CAPTURE_H

#include <optional>

#include <opencv2/core.hpp>

#include <QString>

struct ScreenSelection {
    // 全屏截图，CV_8UC4（BGRA，Qt 的原生 32 位像素顺序）
    cv::Mat image;
    // 选区，单位为截图的原图像素
    cv::Rect region;
};

/// @brief 抓取主屏并弹出全屏选区遮罩。
/// @note 必须在 GUI 线程调用（内部使用 QPixmap 与 QDialog）。
/// @param error 非空时在失败时写入原因；用户主动取消时不写入
/// @return 取消或截图失败时返回 std::nullopt
std::optional<ScreenSelection> capture_and_select(QString* error = nullptr);

/// @brief 在屏幕上方短暂显示一条状态提示
void show_toast(const QString& text);

#endif  // SCREEN_CAPTURE_H
