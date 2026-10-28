#ifndef SCREEN_CUT_OCR_HOTKEY_H
#define SCREEN_CUT_OCR_HOTKEY_H

#include <windows.h>

#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#define WM_ADD_HOTKEY (WM_USER + 0x0001)
#define WM_DEL_HOTKEY (WM_USER + 0x0002)
#define WM_END (WM_USER + 0x0003)

namespace hotkey_detail {

struct HotKeyInfo {
    UINT fsModifiers = 0;
    UINT vk = 0;
    int id = 0;
    bool active = false;
    std::function<void()> func;
    // 注册结果，用于同步 Register 的返回值
    std::shared_ptr<std::promise<bool>> ready;
};

inline std::string getmodstr(UINT mod) {
    std::string modstr;
    if ((mod & MOD_ALT) == MOD_ALT) modstr.append("ALT ");
    if ((mod & MOD_CONTROL) == MOD_CONTROL) modstr.append("CONTROL ");
    if ((mod & MOD_NOREPEAT) == MOD_NOREPEAT) modstr.append("NOREPEAT ");
    if ((mod & MOD_SHIFT) == MOD_SHIFT) modstr.append("SHIFT ");
    if ((mod & MOD_WIN) == MOD_WIN) modstr.append("WIN ");
    return modstr;
}

inline int getmodbin(UINT mod) {
    int bin = 0;
    if ((mod & MOD_ALT) == MOD_ALT) bin |= MOD_ALT;
    if ((mod & MOD_CONTROL) == MOD_CONTROL) bin |= MOD_CONTROL;
    if ((mod & MOD_SHIFT) == MOD_SHIFT) bin |= MOD_SHIFT;
    if ((mod & MOD_WIN) == MOD_WIN) bin |= MOD_WIN;
    return bin;
}

inline DWORD WINAPI thread_func(LPVOID param) {
    HANDLE ready_event = static_cast<HANDLE>(param);
    std::vector<HotKeyInfo> hotkeys;
    int next_id = 1;

    // 先触碰一次消息队列，保证 PostThreadMessage 不会因队列未建立而失败
    MSG msg = {};
    PeekMessage(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    if (ready_event != nullptr) {
        SetEvent(ready_event);
    }

    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        switch (msg.message) {
            case WM_HOTKEY: {
                const UINT hotkey_id = static_cast<UINT>(msg.wParam);
                const UINT mod = static_cast<UINT>(msg.lParam);
                std::cout << "WM_HOTKEY received: " << getmodstr(mod) << "id=" << hotkey_id
                          << std::endl;
                for (HotKeyInfo& hotkey : hotkeys) {
                    if (!hotkey.active || hotkey.id != static_cast<int>(hotkey_id)) {
                        continue;
                    }
                    if (getmodbin(hotkey.fsModifiers) != getmodbin(mod)) {
                        continue;
                    }
                    try {
                        hotkey.func();
                    } catch (const std::exception& e) {
                        std::cerr << "快捷键处理异常: " << e.what() << std::endl;
                    } catch (...) {
                        std::cerr << "快捷键处理异常: 未知错误" << std::endl;
                    }
                }
                break;
            }
            case WM_ADD_HOTKEY: {
                std::unique_ptr<HotKeyInfo> info(reinterpret_cast<HotKeyInfo*>(msg.wParam));
                const int id = next_id++;
                const bool registered =
                    RegisterHotKey(nullptr, id, info->fsModifiers, info->vk) != 0;
                info->id = registered ? id : 0;
                info->active = registered;
                if (!registered) {
                    std::cerr << "注册快捷键失败: " << getmodstr(info->fsModifiers)
                              << static_cast<char>(info->vk) << "，错误码 " << GetLastError()
                              << std::endl;
                }
                if (info->ready) {
                    info->ready->set_value(registered);
                    info->ready.reset();
                }
                hotkeys.push_back(std::move(*info));
                break;
            }
            case WM_DEL_HOTKEY: {
                std::unique_ptr<HotKeyInfo> info(reinterpret_cast<HotKeyInfo*>(msg.wParam));
                for (HotKeyInfo& hotkey : hotkeys) {
                    if (!hotkey.active) {
                        continue;
                    }
                    if (hotkey.fsModifiers == info->fsModifiers && hotkey.vk == info->vk) {
                        UnregisterHotKey(nullptr, hotkey.id);
                        hotkey.active = false;
                    }
                }
                break;
            }
            case WM_END: {
                for (HotKeyInfo& hotkey : hotkeys) {
                    if (hotkey.active) {
                        UnregisterHotKey(nullptr, hotkey.id);
                        hotkey.active = false;
                    }
                }
                return 0;
            }
            default:
                break;
        }
    }
    return 0;
}

}  // namespace hotkey_detail

class HotKey {
public:
    HotKey() {
        HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (ready == nullptr) {
            std::cerr << "创建快捷键线程同步事件失败: " << GetLastError() << std::endl;
            return;
        }
        thread_ = CreateThread(nullptr, 0, hotkey_detail::thread_func, ready, 0, &thread_id_);
        if (thread_ == nullptr) {
            std::cerr << "创建快捷键线程失败: " << GetLastError() << std::endl;
            CloseHandle(ready);
            return;
        }
        WaitForSingleObject(ready, 2000);
        CloseHandle(ready);
    }

    ~HotKey() {
        if (thread_ != nullptr) {
            PostThreadMessage(thread_id_, WM_END, 0, 0);
            WaitForSingleObject(thread_, INFINITE);
            CloseHandle(thread_);
            thread_ = nullptr;
        }
    }

    HotKey(const HotKey&) = delete;
    HotKey& operator=(const HotKey&) = delete;

    /// @brief 注册全局快捷键并等待注册结果
    /// @return 线程不可用、超时或快捷键被占用时返回 false
    bool Register(UINT mod, UINT key, std::function<void()> func) {
        if (thread_ == nullptr) {
            return false;
        }
        auto ready = std::make_shared<std::promise<bool>>();
        std::future<bool> result = ready->get_future();
        auto* info = new hotkey_detail::HotKeyInfo();
        info->fsModifiers = mod;
        info->vk = key;
        info->func = std::move(func);
        info->ready = ready;
        if (!PostThreadMessage(thread_id_, WM_ADD_HOTKEY, reinterpret_cast<WPARAM>(info), 0)) {
            std::cerr << "投递快捷键注册消息失败: " << GetLastError() << std::endl;
            delete info;
            return false;
        }
        if (result.wait_for(std::chrono::seconds(2)) != std::future_status::ready) {
            std::cerr << "等待快捷键注册结果超时" << std::endl;
            return false;
        }
        return result.get();
    }

    void Unregister(UINT mod, UINT key) {
        if (thread_ == nullptr) {
            return;
        }
        auto* info = new hotkey_detail::HotKeyInfo();
        info->fsModifiers = mod;
        info->vk = key;
        if (!PostThreadMessage(thread_id_, WM_DEL_HOTKEY, reinterpret_cast<WPARAM>(info), 0)) {
            delete info;
        }
    }

private:
    HANDLE thread_ = nullptr;
    DWORD thread_id_ = 0;
};

#endif  // SCREEN_CUT_OCR_HOTKEY_H
