#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#ifdef KREMA_LOCAL_ABI
#include "win_abi.h"
#else
#include <windows.h>
#include <shellapi.h>
#include <mmsystem.h>
#endif
#include <stdint.h>
#include <string.h>
#include "pet.h"
#include "art.h"

enum { TRAY_MESSAGE=WM_APP+1, SHOW_MESSAGE=WM_APP+2, ID_SHOW=101, ID_SOUND, ID_FOLLOW, ID_CORNER, ID_EXIT };
static const WCHAR CLASS_NAME[]=L"Pativukat.Krema.Desktop.3";
static struct {
    HWND window;
    HINSTANCE instance;
    HANDLE mutex;
    HDC dc;
    HBITMAP bitmap;
    HGDIOBJ stock_bitmap;
    uint32_t *pixels;
    uint8_t *touch_map;
    POINT drawn_at;
    TouchZone press_zone;
    int w,h,visible,muted,tray_ok,dragging,moved,menu_open,interval;
    UINT taskbar_created;
    POINT press_point;
    float press_x,press_y;
    NOTIFYICONDATAW tray;
    Pet pet;
} app;

static double seconds(void) { return (double)GetTickCount64()*.001; }
static POINT pointer(void)
{
    POINT p;
    if (!GetCursorPos(&p)) { p.x=app.pet.cursor_x; p.y=app.pet.cursor_y; }
    return p;
}
static WorkArea work_area(POINT p)
{
    MONITORINFO m={0};
    m.cbSize=sizeof m;
    if (GetMonitorInfoW(MonitorFromPoint(p,MONITOR_DEFAULTTONEAREST),&m))
        return (WorkArea){m.rcWork.left,m.rcWork.top,m.rcWork.right,m.rcWork.bottom};
    return (WorkArea){0,0,GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN)};
}
static void error_box(const WCHAR *message)
{
    MessageBoxW(app.window,message,L"Krema Masaüstü",MB_OK|MB_ICONINFORMATION);
}
static int set_input(int enable)
{
    RAWINPUTDEVICE devices[2];
    for (int i=0;i<2;i++) {
        devices[i].usUsagePage=1;
        devices[i].usUsage=i==0 ? 6 : 2;
        devices[i].dwFlags=enable ? RIDEV_INPUTSINK : RIDEV_REMOVE;
        devices[i].hwndTarget=enable ? app.window : NULL;
    }
    return RegisterRawInputDevices(devices,2,sizeof devices[0]);
}
static void read_input(HRAWINPUT handle)
{
    RAWINPUT data;
    UINT size=sizeof data;
    if (!app.visible || GetRawInputData(handle,RID_INPUT,&data,&size,sizeof(RAWINPUTHEADER))==(UINT)-1) return;
    double now=seconds();
    if (data.header.dwType==RIM_TYPEKEYBOARD) {
        RAWKEYBOARD *k=&data.data.keyboard;
        if (!(k->Flags & RI_KEY_BREAK) && k->VKey!=255)
            pet_key(&app.pet,k->MakeCode,(k->Flags&RI_KEY_E0)!=0,now);
    } else if (data.header.dwType==RIM_TYPEMOUSE) {
        USHORT f=data.data.mouse.usButtonFlags;
        if (f&RI_MOUSE_LEFT_BUTTON_DOWN) pet_button(&app.pet,1,1,now);
        if (f&RI_MOUSE_LEFT_BUTTON_UP) pet_button(&app.pet,1,0,now);
        if (f&RI_MOUSE_RIGHT_BUTTON_DOWN) pet_button(&app.pet,2,1,now);
        if (f&RI_MOUSE_RIGHT_BUTTON_UP) pet_button(&app.pet,2,0,now);
        if (f&RI_MOUSE_MIDDLE_BUTTON_DOWN) pet_button(&app.pet,3,1,now);
        if (f&RI_MOUSE_MIDDLE_BUTTON_UP) pet_button(&app.pet,3,0,now);
        if (f&(RI_MOUSE_WHEEL|RI_MOUSE_HWHEEL)) pet_button(&app.pet,4,1,now);
    }
}
static int resize_bitmap(int w,int h)
{
    if (app.bitmap && w==app.w && h==app.h) return 1;
    BITMAPINFO bi={0};
    bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth=w; bi.bmiHeader.biHeight=-h;
    bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32;
    bi.bmiHeader.biCompression=BI_RGB;
    void *bits=NULL;
    HBITMAP bitmap=CreateDIBSection(app.dc,&bi,DIB_RGB_COLORS,&bits,NULL,0);
    if (!bitmap || !bits) { if (bitmap) DeleteObject(bitmap); return 0; }
    uint8_t *zones=HeapAlloc(GetProcessHeap(),0,(size_t)w*(size_t)h);
    if (!zones) { DeleteObject(bitmap); return 0; }
    HGDIOBJ previous=SelectObject(app.dc,bitmap);
    if (!previous || previous==HGDI_ERROR) { HeapFree(GetProcessHeap(),0,zones); DeleteObject(bitmap); return 0; }
    if (app.bitmap) DeleteObject(previous);
    else app.stock_bitmap=previous;
    if (app.touch_map) HeapFree(GetProcessHeap(),0,app.touch_map);
    app.touch_map=zones;
    app.bitmap=bitmap; app.pixels=(uint32_t *)bits; app.w=w; app.h=h;
    return 1;
}
static void timer_interval(int ms)
{
    if (app.interval!=ms) {
        if (SetTimer(app.window,1,(UINT)ms,NULL)) app.interval=ms;
    }
}
static void drag_to(POINT pt)
{
    int dx=pt.x-app.press_point.x,dy=pt.y-app.press_point.y;
    if (!app.moved) {
        int threshold=(int)(4*app.pet.dpi_scale+.5f);
        app.moved=dx>threshold || dx< -threshold || dy>threshold || dy< -threshold;
    }
    if (!app.moved) return;
    WorkArea a=work_area(pt);
    /* Leave enough fur on screen to pick the pet up after a drag to an edge. */
    float x=app.press_x+dx,y=app.press_y+dy,s=app.pet.dpi_scale;
    if (x<a.left-95*s)x=a.left-95*s;
    if (x>a.right-180*s)x=a.right-180*s;
    if (y<a.top-30*s)y=a.top-30*s;
    if (y>a.bottom-85*s)y=a.bottom-85*s;
    app.pet.x=x; app.pet.y=y;
}
static TouchZone touch_at(POINT point)
{
    int x=point.x-app.drawn_at.x,y=point.y-app.drawn_at.y;
    if (!app.touch_map || x<0 || y<0 || x>=app.w || y>=app.h) return NO_TOUCH;
    return (TouchZone)app.touch_map[y*app.w+x];
}
static void play_cue(SoundCue cue)
{
    if (cue==QUIET) return;
    if (cue==STOP_AUDIO || app.muted || !app.visible) {
        PlaySoundW(NULL,NULL,0);
        return;
    }
    DWORD flags=SND_MEMORY|SND_ASYNC|SND_NODEFAULT;
    uint32_t offset;
    switch (cue) {
    case PLAY_STROKE: offset=STROKE_OFFSET; break;
    case PLAY_HISS: offset=HISS_OFFSET; break;
    case PLAY_SLEEP: offset=SLEEP_OFFSET; flags|=SND_LOOP; break;
    default:return;
    }
    PlaySoundW((const WCHAR *)(const void *)(krema_asset_bytes+offset),NULL,flags);
}
static int frame(void)
{
    if (!app.visible) return 1;
    double now=seconds();
    POINT pt=pointer();
    if (app.dragging) drag_to(pt);
    if (app.dragging || app.menu_open) {
        app.pet.contact=NO_TOUCH; app.pet.stroke_distance=0;
    } else if (app.pet.have_pointer && (pt.x!=app.pet.cursor_x || pt.y!=app.pet.cursor_y)) {
        float scale=app.pet.dpi_scale;
        pet_touch(&app.pet,touch_at(pt),(pt.x-app.pet.cursor_x)/scale,(pt.y-app.pet.cursor_y)/scale,now,0);
    }
    pet_step(&app.pet,now,pt.x,pt.y,work_area(pt),app.dragging||app.menu_open);
    play_cue(pet_take_sound(&app.pet));
    float scale=app.pet.dpi_scale;
    int width=(int)(PET_WIDTH*scale+.5f),height=(int)(PET_HEIGHT*scale+.5f);
    if (!resize_bitmap(width,height)) return 0;
    pet_render(&app.pet,app.pixels,app.touch_map,width,height);
    POINT position={(LONG)app.pet.x,(LONG)app.pet.y},src={0,0};
    SIZE size={width,height};
    BLENDFUNCTION blend={AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    int ok=UpdateLayeredWindow(app.window,NULL,&position,&size,app.dc,&src,0,&blend,ULW_ALPHA);
    if (ok) app.drawn_at=position;
    int interval=app.pet.pose==ASLEEP ? 85 : (now-app.pet.last_input<2 || app.pet.pose==WALKING ? 30 : 49);
    timer_interval(interval);
    return ok;
}
static void finish_drag(void)
{
    if (!app.dragging) return;
    drag_to(pointer());
    app.dragging=0;
    ReleaseCapture();
    app.pet.hold_position_until=seconds()+.85;
    if (!app.moved) pet_touch(&app.pet,app.press_zone,0,0,seconds(),1);
    frame();
}
static void add_tray(void)
{
    app.tray_ok=Shell_NotifyIconW(NIM_ADD,&app.tray) != 0;
}
static void toggle_visible(void)
{
    if (app.visible) {
        if (!app.tray_ok) add_tray();
        if (!app.tray_ok) { error_box(L"Bildirim alanı hazır olunca Krema’yı gizleyebilirsin."); return; }
        app.visible=0;
        play_cue(STOP_AUDIO);
        app.dragging=0;
        if (GetCapture()==app.window)ReleaseCapture();
        KillTimer(app.window,1); app.interval=0;
        set_input(0);
        ShowWindow(app.window,SW_HIDE);
        return;
    }
    if (!set_input(1)) {error_box(L"Klavye ve fare girişi açılamadı."); return;}
    app.visible=1;
    app.pet.held_buttons=0;
    app.pet.have_pointer=0; app.pet.contact=NO_TOUCH; app.pet.stroke_distance=0;
    app.pet.cue=QUIET;
    pet_wake(&app.pet,seconds());
    app.pet.last_tick=seconds();
    frame();
    ShowWindow(app.window,SW_SHOWNOACTIVATE);
}
static void menu_at(POINT point)
{
    if (app.menu_open) return;
    HMENU menu=CreatePopupMenu();
    if (!menu) return;
    app.menu_open=1;
    AppendMenuW(menu,MF_STRING,ID_FOLLOW,L"İmlecin peşinden yürü");
    AppendMenuW(menu,MF_STRING,ID_SOUND,L"Sesi kapat");
    if (app.pet.following) CheckMenuItem(menu,ID_FOLLOW,MF_BYCOMMAND|MF_CHECKED);
    if (app.muted) CheckMenuItem(menu,ID_SOUND,MF_BYCOMMAND|MF_CHECKED);
    AppendMenuW(menu,MF_SEPARATOR,0,NULL);
    AppendMenuW(menu,MF_STRING,ID_CORNER,L"Köşeye taşı");
    AppendMenuW(menu,MF_STRING,ID_SHOW,app.visible ? L"Krema’yı gizle" : L"Krema’yı göster");
    AppendMenuW(menu,MF_STRING,ID_EXIT,L"Çıkış");
    SetForegroundWindow(app.window);
    int command=TrackPopupMenu(menu,TPM_RIGHTBUTTON|TPM_RETURNCMD|TPM_NONOTIFY,
                               point.x,point.y,0,app.window,NULL);
    DestroyMenu(menu);
    app.menu_open=0;
    PostMessageW(app.window,WM_NULL,0,0);
    switch (command) {
    case ID_FOLLOW:
        app.pet.following=!app.pet.following;
        app.pet.walking=0;
        pet_wake(&app.pet,seconds());
        break;
    case ID_SOUND:
        app.muted=!app.muted;
        if (app.muted) play_cue(STOP_AUDIO);
        else if (app.visible && app.pet.pose==ASLEEP) play_cue(PLAY_SLEEP);
        break;
    case ID_CORNER:
        app.pet.following=0;
        pet_home(&app.pet,work_area(point));
        if (!app.visible)toggle_visible();
        frame();
        break;
    case ID_SHOW:toggle_visible();break;
    case ID_EXIT:DestroyWindow(app.window);break;
    }
}
static LRESULT CALLBACK window_proc(HWND hwnd,UINT message,WPARAM w,LPARAM l)
{
    if (app.taskbar_created && message==app.taskbar_created) {
        add_tray(); return 0;
    }
    switch (message) {
    case WM_INPUT:
        read_input((HRAWINPUT)l);
        return DefWindowProcW(hwnd,message,w,l);
    case WM_TIMER:
        if (w==1) frame();
        return 0;
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_LBUTTONDOWN:
        app.dragging=1; app.moved=0;
        app.press_point=pointer(); app.press_zone=touch_at(app.press_point); app.press_x=app.pet.x; app.press_y=app.pet.y;
        SetCapture(hwnd);
        return 0;
    case WM_MOUSEMOVE:
        if (app.dragging) drag_to(pointer());
        return 0;
    case WM_LBUTTONUP: finish_drag(); return 0;
    case WM_CAPTURECHANGED:
        if ((HWND)l!=hwnd) app.dragging=0;
        return 0;
    case WM_RBUTTONUP: menu_at(pointer());return 0;
    case TRAY_MESSAGE:
        if ((UINT)l==WM_RBUTTONUP || (UINT)l==WM_CONTEXTMENU)menu_at(pointer());
        else if ((UINT)l==WM_LBUTTONDBLCLK)toggle_visible();
        return 0;
    case SHOW_MESSAGE:
        if (!app.visible) toggle_visible();
        return 0;
    case WM_DPICHANGED:
        if (app.window) {
            const RECT *r=(const RECT *)l;
            app.pet.dpi_scale=(float)(w&0xffff)/96.f;
            app.pet.x=(float)r->left; app.pet.y=(float)r->top;
        }
        return 0;
    case WM_DISPLAYCHANGE:
        if (app.window) {
            RECT r;
            if (GetWindowRect(hwnd,&r) && !MonitorFromRect(&r,MONITOR_DEFAULTTONULL))
                pet_home(&app.pet,work_area(pointer()));
        }
        return 0;
    case WM_CLOSE:DestroyWindow(hwnd);return 0;
    case WM_DESTROY:
        KillTimer(hwnd,1);
        set_input(0);
        if (app.tray_ok)Shell_NotifyIconW(NIM_DELETE,&app.tray);
        PlaySoundW(NULL,NULL,0);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,message,w,l);
}
static int run(void)
{
    app.mutex=CreateMutexW(NULL,FALSE,L"Local\\Pativukat.Krema.Masaustu");
    if (!app.mutex) {error_box(L"Krema başlatılamadı."); return 1;}
    if (GetLastError()==ERROR_ALREADY_EXISTS) {
        HWND existing=FindWindowW(CLASS_NAME,NULL);
        if (existing) PostMessageW(existing,SHOW_MESSAGE,0,0);
        else error_box(L"Önce açık Krema uygulamasında sağ tık menüsünden Çıkış’ı seç.");
        return 0;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    app.instance=GetModuleHandleW(NULL);
    static WNDCLASSEXW wc={
        .cbSize=sizeof(WNDCLASSEXW), .lpfnWndProc=window_proc, .lpszClassName=CLASS_NAME
    };
    wc.hInstance=app.instance;
    wc.hIcon=LoadIconW(app.instance,MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon=LoadIconW(NULL,IDI_APPLICATION);
    wc.hCursor=LoadCursorW(NULL,IDC_ARROW); wc.hIconSm=wc.hIcon;
    if (!RegisterClassExW(&wc))return 1;
    POINT pt=pointer(); WorkArea a=work_area(pt);
    pet_init(&app.pet,seconds(),a,1);
    app.window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE,
        CLASS_NAME,L"Krema Masaüstü",WS_POPUP,(int)app.pet.x,(int)app.pet.y,PET_WIDTH,PET_HEIGHT,
        NULL,NULL,app.instance,NULL);
    if (!app.window) {error_box(L"Krema’nın penceresi açılamadı."); return 1;}
    UINT dpi=GetDpiForWindow(app.window);
    app.pet.dpi_scale=dpi ? dpi/96.f : 1.f;
    pet_home(&app.pet,a);
    app.dc=CreateCompatibleDC(NULL);
    if (!app.dc || !set_input(1)) {error_box(L"Krema’nın ekranı veya giriş aygıtları açılamadı."); return 1;}
    app.taskbar_created=RegisterWindowMessageW(L"TaskbarCreated");
    app.tray.cbSize=sizeof app.tray; app.tray.hWnd=app.window; app.tray.uID=1;
    app.tray.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP; app.tray.uCallbackMessage=TRAY_MESSAGE;
    app.tray.hIcon=wc.hIcon;
    const WCHAR tip[]=L"Krema Masaüstü";
    memcpy(app.tray.szTip,tip,sizeof tip);
    add_tray();
    app.visible=1;
    if (!frame()) {error_box(L"Krema’nın görüntüsü çizilemedi."); return 1;}
    ShowWindow(app.window,SW_SHOWNOACTIVATE);
    MSG msg;
    int result;
    while ((result=GetMessageW(&msg,NULL,0,0))>0) {
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    return result<0 ? 1 : 0;
}
#ifdef KREMA_LOCAL_ABI
void WINAPI WinMainCRTStartup(void)
#else
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,PWSTR command_line,int show)
#endif
{
#ifndef KREMA_LOCAL_ABI
    (void)instance; (void)previous; (void)command_line; (void)show;
#endif
    int result=run();
    if (app.dc) {
        if (app.stock_bitmap)SelectObject(app.dc,app.stock_bitmap);
        if (app.bitmap)DeleteObject(app.bitmap);
        DeleteDC(app.dc);
    }
    if (app.touch_map) HeapFree(GetProcessHeap(),0,app.touch_map);
    if (app.mutex)CloseHandle(app.mutex);
#ifdef KREMA_LOCAL_ABI
    ExitProcess((UINT)result);
#else
    return result;
#endif
}
