#include "autogui.h"

#include <cstring>
#include <iostream>
#include <string.h>

namespace {

// 其他程序可能短暂占用剪贴板，重试几次再放弃
bool open_clipboard() {
    constexpr int kMaxAttempts = 5;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        if (OpenClipboard(nullptr)) {
            return true;
        }
        Sleep(20);
    }
    std::cerr << "打开剪贴板失败，错误码: " << GetLastError() << std::endl;
    return false;
}

bool set_clipboard_text(UINT format, const void* data, size_t bytes) {
    if (data == nullptr || bytes == 0) {
        std::cerr << "剪贴板数据为空" << std::endl;
        return false;
    }
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (handle == nullptr) {
        std::cerr << "剪贴板内存分配失败，错误码: " << GetLastError() << std::endl;
        return false;
    }
    void* locked = GlobalLock(handle);
    if (locked == nullptr) {
        std::cerr << "剪贴板内存锁定失败，错误码: " << GetLastError() << std::endl;
        GlobalFree(handle);
        return false;
    }
    std::memcpy(locked, data, bytes);
    GlobalUnlock(handle);

    if (!open_clipboard()) {
        GlobalFree(handle);
        return false;
    }
    EmptyClipboard();
    if (SetClipboardData(format, handle) == nullptr) {
        std::cerr << "写入剪贴板失败，错误码: " << GetLastError() << std::endl;
        GlobalFree(handle);  // 失败时所有权未转移
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}

}  // namespace

autogui::autogui()
{
    RandValue10_30=std::uniform_int_distribution<unsigned> (10,30);

    int cx = GetSystemMetrics(SM_CXSCREEN);
    int cy = GetSystemMetrics(SM_CYSCREEN);

    DEVMODE dm{};
    dm.dmSize = sizeof(dm);
    MONITORINFOEX miex{};
    miex.cbSize = sizeof(miex);
    const HMONITOR hMonitor = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
    if (GetMonitorInfo(hMonitor, &miex) != 0 &&
        EnumDisplaySettings(miex.szDevice, ENUM_CURRENT_SETTINGS, &dm) != 0) {
        cx = static_cast<int>(dm.dmPelsWidth);
        cy = static_cast<int>(dm.dmPelsHeight);
    }
    if (cx <= 0 || cy <= 0) {
        std::cerr << "无法获取屏幕尺寸，鼠标点击功能不可用" << std::endl;
        return;
    }
    ScreenSize={cx,cy};
    Zoom={65535.0/cx,65535.0/cy};
}

autogui::~autogui()
{
}
void autogui::click(int x,int y,float time){
    if (time<=0){
        MouseMove(x,y);
        MouseLeftButtonClick();
    }
}
void  autogui::MouseMove(int x,int y){
    mouse_event(MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_MOVE,x*Zoom.x,y*Zoom.y,0,NULL);
}
void autogui::MouseLeftButtonClick(){
    //Sleep((DWORD)RandValue10_30(RandEngine));
    mouse_event(MOUSEEVENTF_LEFTDOWN,0,0,0,NULL);
    Sleep((DWORD)RandValue10_30(RandEngine));//点击释放间隔
    mouse_event(MOUSEEVENTF_LEFTUP,0,0,0,NULL);
}
POINT autogui::MouseLocation(){
    POINT p;
    GetCursorPos(&p);
    return p;
}
std::vector<HWND> autogui::FindWindowByName(const char *classname,const char* windowname){
    std::vector<HWND> vHWND;
    HWND hwnd=FindWindowA(classname,windowname);
    if (hwnd==NULL){
        return vHWND;
    }
    vHWND.push_back(hwnd);
    while((hwnd=FindWindowExA(NULL,hwnd,classname,windowname))!=NULL){
        vHWND.push_back(hwnd);
    }
    return vHWND;
}
RECT autogui::GetRect(HWND hwnd){
    RECT rt={0,0,0,0};
    if (GetWindowRect(hwnd,&rt)==0)
    {
        std::cerr << "GetWindowRect 失败，错误码: " << GetLastError() << std::endl;
    }
    return rt;
}
std::vector<RECT> autogui::GetRect(std::vector<HWND> hwnd){
    std::vector<RECT> vRECT;
    for (HWND i:hwnd){
        vRECT.push_back(GetRect(i));
    }
    return vRECT;
}

bool clipboard::setvalue(const char * str){
    if (str == nullptr) {
        std::cerr << "剪贴板文本为空指针" << std::endl;
        return false;
    }
    // Win32 剪贴板只接受 UTF-16，先按 UTF-8 解码
    const int length = MultiByteToWideChar(CP_UTF8, 0, str, -1, nullptr, 0);
    if (length <= 0) {
        std::cerr << "剪贴板文本不是合法的 UTF-8，错误码: " << GetLastError() << std::endl;
        return false;
    }
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str, -1, wide.data(), length);
    wide.resize(static_cast<size_t>(length - 1));
    return setvalue(wide);
}

bool clipboard::setvalue(const std::wstring& str){
    const size_t bytes = (str.size() + 1) * sizeof(wchar_t);
    return set_clipboard_text(CF_UNICODETEXT, str.c_str(), bytes);
}

bool clipboard::setimg(const unsigned char * img,int width,int height,size_t step){
    if (img == nullptr || width <= 0 || height <= 0 ||
        step < static_cast<size_t>(width) * 4) {
        std::cerr << "剪贴板图片参数非法: " << width << "x" << height
                  << " step=" << step << std::endl;
        return false;
    }

    const size_t row_bytes = static_cast<size_t>(width) * 4;
    const size_t image_bytes = row_bytes * static_cast<size_t>(height);
    constexpr size_t kHeaderBytes = sizeof(BITMAPINFOHEADER);

    // 写入标准 CF_DIB（自底向上 32 位），各程序兼容性最好，Windows 可据此合成 CF_BITMAP
    HGLOBAL dib = GlobalAlloc(GMEM_MOVEABLE, kHeaderBytes + image_bytes);
    if (dib == nullptr) {
        std::cerr << "剪贴板图片内存分配失败，错误码: " << GetLastError() << std::endl;
        return false;
    }
    auto* header = static_cast<BITMAPINFOHEADER*>(GlobalLock(dib));
    if (header == nullptr) {
        std::cerr << "剪贴板图片内存锁定失败，错误码: " << GetLastError() << std::endl;
        GlobalFree(dib);
        return false;
    }
    *header = {};
    header->biSize = sizeof(BITMAPINFOHEADER);
    header->biWidth = width;
    header->biHeight = height;  // 正值 = 自底向上，兼容性最好
    header->biPlanes = 1;
    header->biBitCount = 32;
    header->biCompression = BI_RGB;
    header->biSizeImage = static_cast<DWORD>(image_bytes);
    auto* destination = reinterpret_cast<unsigned char*>(header) + kHeaderBytes;
    for (int y = 0; y < height; ++y) {
        const unsigned char* source_row =
            img + static_cast<size_t>(height - 1 - y) * step;
        std::memcpy(destination + static_cast<size_t>(y) * row_bytes,
                    source_row, row_bytes);
    }
    GlobalUnlock(dib);

    if (!open_clipboard()) {
        GlobalFree(dib);
        return false;
    }
    EmptyClipboard();
    if (SetClipboardData(CF_DIB, dib) == nullptr) {
        std::cerr << "写入剪贴板图片失败，错误码: " << GetLastError() << std::endl;
        GlobalFree(dib);  // 失败时所有权未转移
        CloseClipboard();
        return false;
    }
    CloseClipboard();
    return true;
}
