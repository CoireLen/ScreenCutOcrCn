#include "hotkey_service.h"

#include "hotkey.hpp"

HotkeyService::HotkeyService() = default;
HotkeyService::~HotkeyService() = default;

void HotkeyService::clear() {
    hotkey_.reset();
}

void HotkeyService::apply(const QVector<HotkeyBinding>& bindings,
                          const QHash<QString, Handler>& handlers, QStringList* failures) {
    clear();
    hotkey_ = std::make_unique<HotKey>();
    for (const HotkeyBinding& binding : bindings) {
        if (binding.sequence.isEmpty()) {
            continue;
        }
        const auto handler = handlers.find(binding.id);
        if (handler == handlers.end()) {
            continue;
        }
        NativeHotkey native;
        QString reason;
        if (!to_native_hotkey(binding.sequence, &native, &reason)) {
            if (failures != nullptr) {
                *failures << QStringLiteral("%1（%2）：%3")
                                 .arg(binding.title, hotkey_text(binding.sequence), reason);
            }
            continue;
        }
        Handler callback = handler.value();
        if (!hotkey_->Register(native.mods, native.vk, [callback] { callback(); })) {
            if (failures != nullptr) {
                *failures << QStringLiteral("%1（%2）：注册失败，可能已被其他程序占用")
                                 .arg(binding.title, hotkey_text(binding.sequence));
            }
        }
    }
}
