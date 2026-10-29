#ifndef SCREEN_CUT_OCR_LONG_CAPTURE_H
#define SCREEN_CUT_OCR_LONG_CAPTURE_H

#include <optional>

#include <opencv2/core.hpp>

#include <QString>

struct LongCaptureResult {
    // 拼接结果，CV_8UC3（BGR）
    cv::Mat image;
    // 实际拼接的帧数（含首帧）
    int frames = 0;
};

/// @brief 框选后进入长截图模式：用户滚动页面，程序按重叠区域自动拼接。
/// @note 必须在 GUI 线程调用；用户取消时返回 std::nullopt 且不写 error
/// @param horizontal true 为横向长截图，false 为竖向
std::optional<LongCaptureResult> capture_long_screenshot(bool horizontal, QString* error = nullptr);

#endif  // SCREEN_CUT_OCR_LONG_CAPTURE_H
