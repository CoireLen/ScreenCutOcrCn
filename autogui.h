#ifndef SCREEN_CUT_OCR_AUTOGUI_H
#define SCREEN_CUT_OCR_AUTOGUI_H

#include <windows.h>

#include <random>
#include <string>
#include <vector>

struct ScreenZoom
{
    double x;
    double y;
};

class clipboard
{
public:
    // 文本按 UTF-8 解码后写入 CF_UNICODETEXT
    bool setvalue(const char* str);
    bool setvalue(const std::wstring& str);
    // img 为自上而下的 32 位 BGRA/BGRX 数据，step 为每行字节数
    bool setimg(const unsigned char* img, int width, int height, size_t step);
};

class autogui
{
private:
    std::default_random_engine RandEngine;
    std::uniform_int_distribution<unsigned> RandValue10_30;
    ScreenZoom Zoom={0,0};
    void MouseMove(int x,int y); //将鼠标移动到(x,y)
    void MouseLeftButtonClick(); //点击鼠标左键
    POINT MouseLocation(); //取得鼠标位置

public:
    clipboard clipboard;
    std::vector<HWND> FindWindowByName(const char* classname,const char* windowname); //取得所有匹配的窗口句柄
    RECT GetRect(HWND hwnd);
    std::vector<RECT> GetRect(std::vector<HWND> hwnd);
    POINT ScreenSize={0,0};
    void click(int x,int y,float time=0); //移动并点击到x,y; 移动到x,y所用的时间(秒)
    autogui();
    ~autogui();
};

#endif  // SCREEN_CUT_OCR_AUTOGUI_H
