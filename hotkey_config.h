#ifndef SCREEN_CUT_OCR_HOTKEY_CONFIG_H
#define SCREEN_CUT_OCR_HOTKEY_CONFIG_H

#include <QKeySequence>
#include <QString>
#include <QVector>

struct HotkeyBinding {
    QString id;
    QString title;
    QKeySequence sequence;
};

/// 默认快捷键：截图 Alt+A、识别 Alt+C、竖向长截图 Alt+V、横向长截图 Alt+H
QVector<HotkeyBinding> default_hotkeys();

/// 从用户配置读取快捷键；缺失项使用默认值，空字符串表示禁用
QVector<HotkeyBinding> load_hotkeys();

void save_hotkeys(const QVector<HotkeyBinding>& bindings);

/// 校验快捷键是否可注册（目前仅 Windows 实现），失败时写入原因
bool validate_hotkey(const QKeySequence& sequence, QString* reason);

#ifdef _WIN32
struct NativeHotkey {
    unsigned mods = 0;
    unsigned vk = 0;
};

/// 把 Qt 快捷键转换为 Win32 RegisterHotKey 参数
bool to_native_hotkey(const QKeySequence& sequence, NativeHotkey* out, QString* reason);
#endif

/// 显示用文本，未设置时返回“未设置”
QString hotkey_text(const QKeySequence& sequence);

#endif  // SCREEN_CUT_OCR_HOTKEY_CONFIG_H
