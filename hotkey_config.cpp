#include "hotkey_config.h"

#include <QSettings>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

const QString kOrganization = QStringLiteral("CoireLen");
const QString kApplication = QStringLiteral("ScreenCutOcrCn");
const QString kGroup = QStringLiteral("hotkeys");

QSettings make_settings() {
    return QSettings(QSettings::IniFormat, QSettings::UserScope, kOrganization, kApplication);
}

QKeySequence parse_sequence(const QString& text) {
    return QKeySequence::fromString(text, QKeySequence::PortableText);
}

}  // namespace

QVector<HotkeyBinding> default_hotkeys() {
    return {
        {QStringLiteral("shot"), QStringLiteral("截图 (复制图片)"),
         parse_sequence(QStringLiteral("Alt+A"))},
        {QStringLiteral("ocr"), QStringLiteral("识别 (复制文字)"),
         parse_sequence(QStringLiteral("Alt+C"))},
        {QStringLiteral("long_vertical"), QStringLiteral("竖向长截图"),
         parse_sequence(QStringLiteral("Alt+V"))},
        {QStringLiteral("long_horizontal"), QStringLiteral("横向长截图"),
         parse_sequence(QStringLiteral("Alt+H"))},
    };
}

QVector<HotkeyBinding> load_hotkeys() {
    QVector<HotkeyBinding> bindings = default_hotkeys();
    QSettings settings = make_settings();
    settings.beginGroup(kGroup);
    for (HotkeyBinding& binding : bindings) {
        if (settings.contains(binding.id)) {
            binding.sequence = parse_sequence(settings.value(binding.id).toString());
        }
    }
    settings.endGroup();
    return bindings;
}

void save_hotkeys(const QVector<HotkeyBinding>& bindings) {
    QSettings settings = make_settings();
    settings.beginGroup(kGroup);
    for (const HotkeyBinding& binding : bindings) {
        settings.setValue(binding.id, binding.sequence.toString(QKeySequence::PortableText));
    }
    settings.endGroup();
    settings.sync();
}

QString hotkey_text(const QKeySequence& sequence) {
    if (sequence.isEmpty()) {
        return QStringLiteral("未设置");
    }
    return sequence.toString(QKeySequence::NativeText);
}

#ifdef _WIN32

bool to_native_hotkey(const QKeySequence& sequence, NativeHotkey* out, QString* reason) {
    const auto fail = [reason](const QString& message) {
        if (reason != nullptr) {
            *reason = message;
        }
        return false;
    };
    if (out == nullptr) {
        return fail(QStringLiteral("内部错误：缺少输出参数"));
    }
    if (sequence.isEmpty()) {
        return fail(QStringLiteral("请先按下快捷键"));
    }

    const QKeyCombination combination = sequence[0];
    const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
    UINT native_mods = MOD_NOREPEAT;
    if (modifiers.testFlag(Qt::ControlModifier)) {
        native_mods |= MOD_CONTROL;
    }
    if (modifiers.testFlag(Qt::AltModifier)) {
        native_mods |= MOD_ALT;
    }
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        native_mods |= MOD_SHIFT;
    }
    if (modifiers.testFlag(Qt::MetaModifier)) {
        native_mods |= MOD_WIN;
    }
    if ((native_mods & (MOD_CONTROL | MOD_ALT | MOD_WIN)) == 0) {
        return fail(QStringLiteral("快捷键需包含 Ctrl、Alt 或 Win"));
    }

    const int key = combination.key();
    UINT vk = 0;
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        vk = static_cast<UINT>('A' + (key - Qt::Key_A));
    } else if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        vk = static_cast<UINT>('0' + (key - Qt::Key_0));
    } else if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        vk = static_cast<UINT>(VK_F1 + (key - Qt::Key_F1));
    } else {
        switch (key) {
            case Qt::Key_Space: vk = VK_SPACE; break;
            case Qt::Key_Tab: vk = VK_TAB; break;
            case Qt::Key_Return:
            case Qt::Key_Enter: vk = VK_RETURN; break;
            case Qt::Key_Escape: vk = VK_ESCAPE; break;
            case Qt::Key_Backspace: vk = VK_BACK; break;
            case Qt::Key_Insert: vk = VK_INSERT; break;
            case Qt::Key_Delete: vk = VK_DELETE; break;
            case Qt::Key_Home: vk = VK_HOME; break;
            case Qt::Key_End: vk = VK_END; break;
            case Qt::Key_PageUp: vk = VK_PRIOR; break;
            case Qt::Key_PageDown: vk = VK_NEXT; break;
            case Qt::Key_Left: vk = VK_LEFT; break;
            case Qt::Key_Up: vk = VK_UP; break;
            case Qt::Key_Right: vk = VK_RIGHT; break;
            case Qt::Key_Down: vk = VK_DOWN; break;
            case Qt::Key_Print: vk = VK_SNAPSHOT; break;
            case Qt::Key_ScrollLock: vk = VK_SCROLL; break;
            case Qt::Key_Pause: vk = VK_PAUSE; break;
            default: break;
        }
    }
    if (vk == 0) {
        return fail(QStringLiteral("暂不支持该按键，请改用字母、数字、F1–F24 或方向键"));
    }

    out->mods = native_mods;
    out->vk = vk;
    return true;
}

bool validate_hotkey(const QKeySequence& sequence, QString* reason) {
    if (reason != nullptr) {
        reason->clear();
    }
    if (sequence.isEmpty()) {
        return true;
    }
    NativeHotkey native;
    return to_native_hotkey(sequence, &native, reason);
}

#else

bool validate_hotkey(const QKeySequence&, QString* reason) {
    if (reason != nullptr) {
        reason->clear();
    }
    return true;
}

#endif
