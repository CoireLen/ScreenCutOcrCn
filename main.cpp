#include <iostream>
#include <algorithm>
#include "autogui.h"
#include "cnocr.h"
#include "hotkey.hpp"
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <opencv2/highgui.hpp>
void runscreencutandocr();
void runscreencut();
cnocr* pocr;
autogui* pag;
std::vector<std::thread> vt;
u_char *g_img=NULL;//全局img缓存
int main(int argc , char ** argv)
{
    cnocr ocr;
    pocr=&ocr;
    autogui ag;
    pag=&ag;
    HotKey hk;
    hk.Register(MOD_ALT|MOD_NOREPEAT,0x43, runscreencutandocr);
    hk.Register(MOD_ALT|MOD_NOREPEAT,0x41, runscreencut);
    Fl_Window *window;
    Fl_Box *box;
    window = new Fl_Window(300, 180);
    window->label("Ocr Tool");
    box = new Fl_Box(20, 40, 260, 100, "Alt_C");
    box->box(FL_UP_BOX);
    box->labelsize(36);
    box->labelfont(FL_BOLD + FL_ITALIC);
    (FL_SHADOW_LABEL);
    window->end();
    window->show(argc, argv);
    int ret= Fl::run();
    for(auto i_t=0;i_t<vt.size();i_t++){
        vt[i_t].join();
    }
    if (g_img!=NULL)
        free( g_img);
    return ret;
}


struct cvmousedrawrectdata
{
    cv::Mat img;
    cv::Mat temp1;
    bool  drawrect=false;
    cv::Point leftpoint=cv::Point(-1,-1);
    cv::Point mousepoint=cv::Point(-1,-1);
    int *rect;
    bool *whiledo;
};
void on_mouse(int event, int x, int y, int flags, void *ustc);
void screencutandocr();
void runscreencutandocr(){
    vt.push_back(std::thread(screencutandocr));
}
void screencutandocr()
{
    if (g_img==NULL){
        g_img=(u_char *)malloc(pag->ScreenSize.x*pag->ScreenSize.y*sizeof(u_char)*4);
    }
    pag->screen.capture(g_img);
    unsigned char * img=g_img;
    cv::Mat matimg(pag->ScreenSize.y,pag->ScreenSize.x,CV_8UC4,img);
    cv::namedWindow("select rect",cv::WINDOW_NORMAL);
    cv::setWindowProperty("select rect",cv::WND_PROP_FULLSCREEN,cv::WINDOW_FULLSCREEN );//CV_WND_PROP_FULLSCREEN，CV_WINDOW_FULLSCREEN
    struct cvmousedrawrectdata mddate;
    int rect[4]={0};
    mddate.img=matimg.clone();
    mddate.temp1=matimg.clone();
    mddate.rect=rect;
    bool wdo=true;
    mddate.whiledo=&wdo;
    while ((cv::waitKey(20) != 27)&&wdo){
        cv::setMouseCallback("select rect", on_mouse, (void*)&mddate);
        cv::imshow("select rect",mddate.temp1);
    }
    cv::destroyWindow("select rect");
    for (auto i :rect){
        if (i==0)
        {
            wdo=true;
            break;
        }
    }
    if (wdo!=true)
    {
        auto imgrect = matimg(cv::Rect(rect[0], rect[1], rect[2] - rect[0], rect[3] - rect[1]));
        cv::UMat umat=imgrect.getUMat(cv::ACCESS_READ);
        auto qout = pocr->ocr(umat);
        std::wstring wstr;
        for (auto ws:qout){
            wstr.append(ws.first);
            wstr.push_back('\n');
        }
        clipboard cb;
        cb.setvalue(wstr);
    }
}


void runscreencut(){
    if (g_img==NULL){
        g_img=(u_char *)malloc(pag->ScreenSize.x*pag->ScreenSize.y*sizeof(u_char)*4);
    }
    pag->screen.capture(g_img);
    auto img=g_img;
    cv::Mat matimg(pag->ScreenSize.y,pag->ScreenSize.x,CV_8UC4,img);
    cv::namedWindow("select rect",cv::WINDOW_NORMAL);
    cv::setWindowProperty("select rect",cv::WND_PROP_FULLSCREEN,cv::WINDOW_FULLSCREEN );//CV_WND_PROP_FULLSCREEN，CV_WINDOW_FULLSCREEN
    struct cvmousedrawrectdata mddate;
    int rect[4]={0};
    mddate.img=matimg.clone();
    mddate.temp1=matimg.clone();
    mddate.rect=rect;
    bool wdo=true;
    mddate.whiledo=&wdo;
    while ((cv::waitKey(20) != 27)&&wdo){
        cv::setMouseCallback("select rect", on_mouse, (void*)&mddate);
        cv::imshow("select rect",mddate.temp1);
    }
    cv::destroyWindow("select rect");
    for (auto i :rect){
        if (i==0)
        {
            wdo=true;
            break;
        }
    }
    if (wdo!=true)
    {
        auto imgrect = matimg(cv::Rect(rect[0], rect[1], rect[2] - rect[0], rect[3] - rect[1]));
        clipboard cb;
        //将图片添加到剪贴板;
        cb.setimg(imgrect.data,imgrect.cols,imgrect.rows,imgrect.step1());
        printf("imgsize=%d,%d",imgrect.cols,imgrect.rows);
    }
}
void on_mouse(int event, int x, int y, int flags, void *ustc){
    struct cvmousedrawrectdata* mddate=(struct cvmousedrawrectdata*)ustc;
    cv::Mat& image = mddate->img;//这样就可以传递Mat信息了
    cv::Mat& temp1=mddate->temp1;
    image.copyTo(temp1);
	char temp[16];
	switch (event) {
	case cv::EVENT_LBUTTONDOWN://按下左键
	{   
		sprintf_s(temp, "(%d,%d)", x, y);
		putText(image, temp, cv::Point(x, y), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 0, 0, 255));
		mddate->drawrect = true;
		mddate->leftpoint= cv::Point(x, y);
        mddate->rect[0]=x;
        mddate->rect[1]=y;
	}	break;
	case cv::EVENT_MOUSEMOVE://移动鼠标
	{
		mddate->mousepoint = cv::Point(x, y);
		if (mddate->drawrect)
		{ 
            cv::rectangle(temp1, mddate->leftpoint, mddate->mousepoint, cv::Scalar(0,255,0,255));
        }
	}break;
	case cv::EVENT_LBUTTONUP:
	{
		mddate->drawrect = false;
		sprintf_s(temp, "(%d,%d)", x, y);
		//调用函数进行绘制
		cv::rectangle(image,mddate->leftpoint, mddate->mousepoint, cv::Scalar(0,255,0,255));
        mddate->rect[2]=x;
        mddate->rect[3]=y;
        *mddate->whiledo=false;
	}break;
    }
}