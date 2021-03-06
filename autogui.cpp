#include "autogui.h"
#include <string.h>

autogui::autogui(/* args */)
{
    RandValue10_30=std::uniform_int_distribution<unsigned> (10,30);
    DEVMODE dm;
    dm.dmSize = sizeof(dm);
    dm.dmDriverExtra = 0;
    MONITORINFOEX miex;
    HWND hWnd = GetDesktopWindow();
    HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    miex.cbSize = sizeof(miex);
    GetMonitorInfo(hMonitor, &miex);
    EnumDisplaySettings(miex.szDevice, ENUM_CURRENT_SETTINGS, &dm);
    int cx= dm.dmPelsWidth;
    int cy = dm.dmPelsHeight;
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
std::vector<HWND> autogui::FindWindowByName(char *classname,char * windowname){
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
        printf("GetWindowRect Fail\n");
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


clipboard::clipboard(/* args */)
{
}

clipboard::~clipboard()
{
}
void clipboard::setvalue(char * str){
    if (OpenClipboard(GetActiveWindow())){
        EmptyClipboard();
        int b = strnlen(str,1000);
        int size =sizeof(char)* (b+1);
        LPWSTR pData = (LPWSTR)GlobalAlloc(GMEM_MOVEABLE,size);
        if (pData ==NULL){
            printf("clipboard alloc mem fail");
            return;
        }
        auto hData=GlobalLock(pData);
        if (hData==NULL){
            printf("clipboard lock mem fail");
            return;
        }
        if (size < 1000)
            memcpy(hData,(void *)str, size);
        GlobalUnlock(hData);
        if (SetClipboardData(CF_TEXT,hData)==NULL){
            CloseClipboard();
            printf("set clipboard data fail");
            return;
        }
        CloseClipboard();
    }
    else{
        printf("opencliboard fail");
    }
}
void clipboard::setvalue(std::wstring str){
    if (OpenClipboard(GetActiveWindow())){
        EmptyClipboard();
        int b = str.length();
        int size =sizeof(str[0])* (b+1);
        LPWSTR pData = (LPWSTR)GlobalAlloc(GMEM_MOVEABLE,size);
        if (pData ==NULL){
            printf("clipboard alloc mem fail");
            return;
        }
        auto hData=GlobalLock(pData);
        if (hData==NULL){
            printf("clipboard lock mem fail");
            return;
        }
        if (size < 1000)
            memcpy(hData,(void *)str.data(), size);
        GlobalUnlock(hData);
        if (SetClipboardData(CF_UNICODETEXT,hData)==NULL){
            CloseClipboard();
            printf("set clipboard data fail");
            return;
        }
        CloseClipboard();
    }
    else{
        printf("opencliboard fail");
    }
}
void rotateImg(unsigned char *pImgData,int iWidth,int iHeight)
{
	unsigned int* pImg32 = (unsigned int*)pImgData;
	int iCount = iWidth * iHeight;
	unsigned int* pRotateImg32 = new unsigned int[iCount];
	for (int i = 0; i < iCount; ++ i) {
		pRotateImg32[i] = pImg32[iCount - i - 1];
	}
 
	unsigned int* pRotateImg32_r = (unsigned int*)pImgData;
	for (int i = 0; i < iHeight; ++ i) {
		for (int j = 0; j < iWidth; ++ j) {
			pRotateImg32_r[j + i * iWidth] = pRotateImg32[iWidth - j - 1 + i * iWidth];
		}
	}
	delete [] pRotateImg32;
}
void clipboard::setimg(unsigned char * img,u_int width,u_int height,size_t step1){
    if (OpenClipboard(GetActiveWindow())){
        EmptyClipboard();
        BITMAP bm={0};
        bm.bmWidth=width;
        bm.bmHeight=height;
        bm.bmBits=img;
        bm.bmPlanes=1;
        bm.bmWidthBytes=step1;
        bm.bmBitsPixel=32;
        auto hbm=CreateBitmapIndirect(&bm);
        if (hbm==NULL){
             printf("CreateBitmap fail");
            return;
        }
        if (SetClipboardData(CF_BITMAP,hbm)==NULL){
            CloseClipboard();
            printf("set clipboard data fail");
            return;
        }
        CloseClipboard();
    }
    else{
        printf("opencliboard fail");
    }
}