#ifndef SCREEN_CUT_OCR_HOTKEY_SERVICE_H
#define SCREEN_CUT_OCR_HOTKEY_SERVICE_H

#include <functional>
#include <memory>

#include <QHash>
#include <QStringList>
#include <QVector>

#include "hotkey_config.h"

class HotKey;

/// 管理全局快捷键的注册与重新注册
class HotkeyService {
public:
    using Handler = std::function<void()>;

    HotkeyService();
    ~HotkeyService();

    HotkeyService(const HotkeyService&) = delete;
    HotkeyService& operator=(const HotkeyService&) = delete;

    /// 清空旧绑定后按配置重新注册，失败的项写入 failures
    void apply(const QVector<HotkeyBinding>& bindings,
               const QHash<QString, Handler>& handlers, QStringList* failures);

    void clear();

private:
    std::unique_ptr<HotKey> hotkey_;
};

#endif  // SCREEN_CUT_OCR_HOTKEY_SERVICE_H
