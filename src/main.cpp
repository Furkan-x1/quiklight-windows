#include "quiklight_layout.hpp"
#include "quiklight_hid.hpp"
#include "screen_capture.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

using namespace quiklight;

namespace {

struct AppSettings {
    int monitor = 1;
    int brightness = 255;
    int fps = 60;
    float smoothing = 0.35f;
    bool verbose = false;
    MappingConfig mapping{};
    ColorEnhancementConfig color{};
    EffectConfig effect{};
};

static std::wstring exe_directory() {
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (!n || n >= MAX_PATH) return L".";
    std::wstring p(buf, n);
    auto slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : p.substr(0, slash);
}

static std::wstring config_path() { return exe_directory() + L"\\quiklight.ini"; }

static std::string narrow(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n, nullptr, nullptr);
    return out;
}

static std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), n);
    return out;
}

static std::map<std::string, std::string> read_ini(const std::wstring& path) {
    std::ifstream f(narrow(path));
    std::map<std::string, std::string> m;
    std::string line;
    while (std::getline(f, line)) {
        auto p = line.find('#');
        if (p != std::string::npos) line.resize(p);
        p = line.find('=');
        if (p == std::string::npos) continue;
        auto k = line.substr(0, p), v = line.substr(p + 1);
        auto trim = [](std::string& s) {
            while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
            while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
        };
        trim(k); trim(v);
        if (!k.empty()) m[k] = v;
    }
    return m;
}

static int geti(const std::map<std::string, std::string>& m, const char* k, int d) {
    auto it = m.find(k); if (it == m.end()) return d;
    try { return std::stoi(it->second); } catch (...) { return d; }
}
static float getf(const std::map<std::string, std::string>& m, const char* k, float d) {
    auto it = m.find(k); if (it == m.end()) return d;
    try { return std::stof(it->second); } catch (...) { return d; }
}
static bool getb(const std::map<std::string, std::string>& m, const char* k, bool d) {
    return geti(m, k, d ? 1 : 0) != 0;
}

static AppSettings load_settings() {
    AppSettings s;
    auto m = read_ini(config_path());
    s.monitor = geti(m, "monitor", s.monitor);
    s.brightness = geti(m, "brightness", s.brightness);
    s.fps = geti(m, "fps", s.fps);
    s.smoothing = getf(m, "smoothing", s.smoothing);
    s.verbose = getb(m, "verbose", s.verbose);
    s.mapping.reverse_top = getb(m, "reverse_top", s.mapping.reverse_top);
    s.mapping.reverse_right = getb(m, "reverse_right", s.mapping.reverse_right);
    s.mapping.reverse_left = getb(m, "reverse_left", s.mapping.reverse_left);
    s.mapping.swap_left_right = getb(m, "swap_left_right", s.mapping.swap_left_right);
    s.mapping.top_offset = geti(m, "top_offset", 0);
    s.mapping.right_offset = geti(m, "right_offset", 0);
    s.mapping.left_offset = geti(m, "left_offset", 0);
    s.color.saturation_boost = getf(m, "saturation_boost", s.color.saturation_boost);
    s.color.value_boost = getf(m, "value_boost", s.color.value_boost);
    s.color.gamma = getf(m, "gamma", s.color.gamma);
    s.color.min_saturation = getf(m, "min_saturation", s.color.min_saturation);
    s.color.hue_shift_degrees = getf(m, "hue_shift", s.color.hue_shift_degrees);
    s.color.capture_depth_percent = getf(m, "capture_depth_percent", s.color.capture_depth_percent);
    s.effect.mode = (LightMode)std::clamp(geti(m, "mode", 0), 0, 15);
    s.effect.speed = getf(m, "effect_speed", s.effect.speed);
    s.effect.manual.r = (uint8_t)std::clamp(geti(m, "manual_r", s.effect.manual.r), 0, 255);
    s.effect.manual.g = (uint8_t)std::clamp(geti(m, "manual_g", s.effect.manual.g), 0, 255);
    s.effect.manual.b = (uint8_t)std::clamp(geti(m, "manual_b", s.effect.manual.b), 0, 255);
    return s;
}

static void save_settings(const AppSettings& s) {
    std::ofstream f(narrow(config_path()), std::ios::trunc);
    if (!f) return;
    f << "# Quiklight Windows configuration\n";
    f << "monitor=" << s.monitor << "\n";
    f << "brightness=" << s.brightness << "\n";
    f << "fps=" << s.fps << "\n";
    f << std::fixed << std::setprecision(3);
    f << "smoothing=" << s.smoothing << "\n";
    f << "mode=" << (int)s.effect.mode << "\n";
    f << "effect_speed=" << s.effect.speed << "\n";
    f << "manual_r=" << (int)s.effect.manual.r << "\n";
    f << "manual_g=" << (int)s.effect.manual.g << "\n";
    f << "manual_b=" << (int)s.effect.manual.b << "\n\n";
    f << "capture_depth_percent=" << s.color.capture_depth_percent << "\n";
    f << "saturation_boost=" << s.color.saturation_boost << "\n";
    f << "value_boost=" << s.color.value_boost << "\n";
    f << "gamma=" << s.color.gamma << "\n";
    f << "min_saturation=" << s.color.min_saturation << "\n";
    f << "hue_shift=" << s.color.hue_shift_degrees << "\n\n";
    f << "reverse_top=" << (s.mapping.reverse_top ? 1 : 0) << "\n";
    f << "reverse_right=" << (s.mapping.reverse_right ? 1 : 0) << "\n";
    f << "reverse_left=" << (s.mapping.reverse_left ? 1 : 0) << "\n";
    f << "swap_left_right=" << (s.mapping.swap_left_right ? 1 : 0) << "\n";
    f << "top_offset=" << s.mapping.top_offset << "\n";
    f << "right_offset=" << s.mapping.right_offset << "\n";
    f << "left_offset=" << s.mapping.left_offset << "\n";
    f << "verbose=" << (s.verbose ? 1 : 0) << "\n";
}

static LedFrame random_frame(uint32_t seed) {
    std::mt19937 r(seed);
    const ColorRgb pal[] = {{255,0,0},{0,255,0},{0,0,255},{255,255,0},{0,255,255},{255,0,255},{255,255,255},{255,128,0},{255,105,180},{128,255,0}};
    std::uniform_int_distribution<int> d(0, 9);
    LedFrame f{};
    for (auto& c : f) c = pal[d(r)];
    return f;
}

class Engine {
public:
    Engine() : settings_(load_settings()) { set_color_enhancement_config(settings_.color); }
    ~Engine() { stop(); }

    AppSettings get() const { std::lock_guard<std::mutex> lock(m_); return settings_; }
    void update(const AppSettings& s) {
        bool monitorChanged;
        {
            std::lock_guard<std::mutex> lock(m_);
            monitorChanged = s.monitor != settings_.monitor;
            settings_ = s;
            set_color_enhancement_config(settings_.color);
            if (monitorChanged) restart_capture_ = true;
        }
        save_settings(s);
    }
    bool running() const { return running_; }
    std::wstring status() const { std::lock_guard<std::mutex> lock(m_); return status_; }

    void start() {
        if (running_) return;
        { std::lock_guard<std::mutex> lock(m_); set_color_enhancement_config(settings_.color); status_ = L"Starting..."; }
        running_ = true;
        worker_ = std::thread([this] { run(); });
    }
    void stop() {
        running_ = false;
        if (worker_.joinable()) worker_.join();
        hid_.reset(); capture_.reset();
        { std::lock_guard<std::mutex> lock(m_); status_ = L"Stopped"; }
    }

    bool test_colors() {
        if (running_) { set_status(L"Stop the current mode before running LED test"); return false; }
        std::thread([this] {
            try {
                auto s = get();
                QuiklightHid hid(0x1A86, 0xFE07, (uint8_t)std::clamp(s.brightness, 0, 255));
                auto frame = random_frame(12345);
                for (int i=0;i<24;++i) {
                    if (!hid.sendFrame(frame)) throw std::runtime_error("HID write failed");
                    std::rotate(frame.begin(), frame.begin()+1, frame.end());
                    std::this_thread::sleep_for(std::chrono::milliseconds(120));
                }
                LedFrame black{}; hid.sendFrame(black); set_status(L"LED test finished");
            } catch (const std::exception& e) { set_status(widen(std::string("Test failed: ") + e.what())); }
        }).detach();
        return true;
    }

private:
    void set_status(const std::wstring& s) { std::lock_guard<std::mutex> lock(m_); status_ = s; }

    void run() {
        try {
            auto s = get();
            hid_ = std::make_unique<QuiklightHid>(0x1A86,0xFE07,(uint8_t)std::clamp(s.brightness,0,255));
            int lastBrightness = s.brightness;
            LightMode lastMode = s.effect.mode;
            capture_ = (s.effect.mode == LightMode::Capture) ? std::make_unique<ScreenCapture>(s.monitor,s.verbose) : nullptr;
            if (capture_) set_status(std::wstring(L"Running: Capture (" ) + widen(capture_->backend_name()) + L")");
            else set_status(std::wstring(L"Running: ") + light_mode_name(s.effect.mode));

            LedFrame raw{}, mapped{}, output{}, smooth{}, last{};
            bool have=false, sent=false;
            auto modeStart = std::chrono::steady_clock::now();

            while (running_) {
                s = get();
                set_color_enhancement_config(s.color);
                if (s.brightness != lastBrightness) {
                    hid_->setBrightness((uint8_t)std::clamp(s.brightness,0,255));
                    lastBrightness = s.brightness;
                }
                if (s.effect.mode != lastMode) {
                    lastMode = s.effect.mode;
                    modeStart = std::chrono::steady_clock::now();
                    have=false; sent=false;
                    if (s.effect.mode == LightMode::Capture) {
                        capture_ = std::make_unique<ScreenCapture>(s.monitor,s.verbose);
                    } else capture_.reset();
                    if (capture_) set_status(std::wstring(L"Running: Capture (" ) + widen(capture_->backend_name()) + L")");
                    else set_status(std::wstring(L"Running: ") + light_mode_name(s.effect.mode));
                }
                if (s.effect.mode == LightMode::Capture && restart_capture_) {
                    restart_capture_ = false; capture_.reset();
                    capture_ = std::make_unique<ScreenCapture>(s.monitor,s.verbose);
                    have=false; sent=false;
                }

                auto start = std::chrono::steady_clock::now();
                if (s.effect.mode == LightMode::Capture) {
                    bool ok = capture_ && capture_->capture([&](uint32_t w,uint32_t h,const uint32_t* p){
                        compute_colors(w,h,p,raw); mapped=remap_frame(raw,s.mapping);
                    });
                    if (ok) output = mapped;
                } else {
                    double t = std::chrono::duration<double>(start-modeStart).count();
                    output = remap_frame(effect_frame(s.effect,t),s.mapping);
                }

                smooth = have ? smooth_frame(smooth,output,s.smoothing) : output;
                have=true;
                if (!sent || !frames_equal(smooth,last)) {
                    if (!hid_->sendFrame(smooth)) throw std::runtime_error("HID write failed");
                    last=smooth; sent=true;
                }
                int targetFps = (s.effect.mode == LightMode::Capture) ? s.fps : std::clamp((int)(30.f*s.effect.speed),10,120);
                auto frameMs=std::chrono::milliseconds(std::max(1,1000/std::max(1,targetFps)));
                auto elapsed=std::chrono::steady_clock::now()-start;
                auto sleepFor=frameMs-std::chrono::duration_cast<std::chrono::milliseconds>(elapsed);
                if (sleepFor.count()>0) std::this_thread::sleep_for(sleepFor);
            }
            if (hid_) { LedFrame black{}; hid_->sendFrame(black); }
        } catch(const std::exception& e) { set_status(widen(std::string("Error: ") + e.what())); }
        hid_.reset(); capture_.reset(); running_=false;
    }

    mutable std::mutex m_;
    AppSettings settings_;
    std::wstring status_=L"Stopped";
    std::atomic_bool running_{false};
    std::atomic_bool restart_capture_{false};
    std::thread worker_;
    std::unique_ptr<QuiklightHid> hid_;
    std::unique_ptr<ScreenCapture> capture_;
};

constexpr int WMAPP_TRAY=WM_APP+11;
constexpr UINT ID_TRAY=1001;
constexpr int APP_START=2001,APP_TEST=2002,APP_MONITOR=2003,APP_BRIGHTNESS=2004,APP_FPS=2005,APP_SMOOTHING=2006,
              APP_SAT=2007,APP_VALUE=2008,APP_GAMMA=2009,APP_MINSAT=2010,APP_HUE=2011,APP_REVT=2012,APP_REVR=2013,
              APP_REVL=2014,APP_SWAP=2015,APP_TOFF=2016,APP_ROFF=2017,APP_LOFF=2018,APP_AUTOSTART=2019,APP_SAVE=2020,
              APP_STATUS=2021,APP_MODE=2022,APP_SPEED=2023,APP_RED=2024,APP_GREEN=2025,APP_BLUE=2026,APP_DEPTH=2027,
              APP_HELP=2028,APP_BRIGHTNESS_VAL=2029,APP_SPEED_VAL=2030,APP_DEPTH_VAL=2031,APP_RGB_PREVIEW=2032;

class Gui {
public:
    explicit Gui(HINSTANCE h):hinst_(h){}
    ~Gui(){if(timer_)KillTimer(hwnd_,timer_);remove_tray();}

    bool create(){
        INITCOMMONCONTROLSEX icc{sizeof(icc),ICC_STANDARD_CLASSES|ICC_BAR_CLASSES}; InitCommonControlsEx(&icc);
        WNDCLASSW wc{}; wc.lpfnWndProc=&Gui::wnd_proc_static; wc.hInstance=hinst_; wc.lpszClassName=L"QuiklightWindowsClass";
        wc.hCursor=nullptr; wc.hbrBackground=(HBRUSH)(COLOR_WINDOW+1); wc.hIcon=nullptr;
        if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
        hwnd_=CreateWindowExW(0,wc.lpszClassName,L"Quiklight Windows",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1100,1000,nullptr,nullptr,hinst_,this);
        if(!hwnd_)return false;
        build_controls(); load_ui_from_settings(); timer_=SetTimer(hwnd_,1,250,nullptr); add_tray(); ShowWindow(hwnd_,SW_SHOW); UpdateWindow(hwnd_); return true;
    }
    int run(){MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return (int)msg.wParam;}

private:
    static LRESULT CALLBACK wnd_proc_static(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
        Gui*self=(Gui*)GetWindowLongPtrW(hwnd,GWLP_USERDATA);
        if(msg==WM_NCCREATE){auto*cs=(CREATESTRUCTW*)lp;self=(Gui*)cs->lpCreateParams;SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)self);self->hwnd_=hwnd;}
        return self?self->wnd_proc(msg,wp,lp):DefWindowProcW(hwnd,msg,wp,lp);
    }
    LRESULT wnd_proc(UINT msg,WPARAM wp,LPARAM lp){
        switch(msg){
            case WM_COMMAND: {
                int id=LOWORD(wp), code=HIWORD(wp);
                if(id==APP_START&&code==BN_CLICKED)toggle_start();
                else if(id==APP_TEST&&code==BN_CLICKED)engine_.test_colors();
                else if(id==APP_AUTOSTART&&code==BN_CLICKED)install_autostart();
                else if(id==APP_SAVE&&code==BN_CLICKED)save_ui();
                else if(id==APP_MODE&&code==CBN_SELCHANGE){save_ui();update_help();}
                else if(id==APP_HELP&&code==EN_SETFOCUS){update_help();}
                return 0;
            }
            case WM_HSCROLL: save_ui(); update_dynamic_values(); return 0;
            case WM_TIMER: if(wp==1){refresh_status();update_dynamic_values();} return 0;
            case WM_CLOSE: ShowWindow(hwnd_,SW_HIDE); return 0;
            case WMAPP_TRAY:
                if(lp==WM_LBUTTONUP){ShowWindow(hwnd_,SW_SHOW);SetForegroundWindow(hwnd_);} else if(lp==WM_RBUTTONUP)show_tray_menu(); return 0;
            case WM_DESTROY: engine_.stop(); remove_tray(); PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(hwnd_,msg,wp,lp);
    }

    HWND make_group(const wchar_t*t,int x,int y,int w,int h){return CreateWindowW(L"BUTTON",t,WS_CHILD|WS_VISIBLE|BS_GROUPBOX,x,y,w,h,hwnd_,nullptr,hinst_,nullptr);}
    HWND add_label(const wchar_t*t,int x,int y,int w,int h){return CreateWindowW(L"STATIC",t,WS_CHILD|WS_VISIBLE,x,y,w,h,hwnd_,nullptr,hinst_,nullptr);}
    HWND add_edit(int id,int x,int y,int w=75){HWND h=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,x,y,w,24,hwnd_,(HMENU)(INT_PTR)id,hinst_,nullptr);SendMessageW(h,EM_SETLIMITTEXT,16,0);return h;}
    HWND add_check(int id,const wchar_t*t,int x,int y,int w=160){return CreateWindowW(L"BUTTON",t,WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,x,y,w,22,hwnd_,(HMENU)(INT_PTR)id,hinst_,nullptr);}
    HWND add_track(int id,int x,int y,int w,int lo,int hi){HWND h=CreateWindowW(TRACKBAR_CLASSW,L"",WS_CHILD|WS_VISIBLE|TBS_AUTOTICKS,x,y,w,30,hwnd_,(HMENU)(INT_PTR)id,hinst_,nullptr);SendMessageW(h,TBM_SETRANGE,TRUE,MAKELONG(lo,hi));return h;}

    void build_controls(){
        HFONT font=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
        auto F=[&](HWND h){if(h)SendMessageW(h,WM_SETFONT,(WPARAM)font,TRUE);};
        title_=add_label(L"Quiklight Windows",24,18,400,30);F(title_);
        add_label(L"Lighting mode",24,55,120,22);F(mode_label_=add_label(L"Mode",24,55,120,22));
        mode_=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|CBS_NOINTEGRALHEIGHT|WS_VSCROLL,145,51,330,250,hwnd_,(HMENU)APP_MODE,hinst_,nullptr);F(mode_);
        for(int i=0;i<=15;i++)SendMessageW(mode_,CB_ADDSTRING,0,(LPARAM)light_mode_name((LightMode)i)); SendMessageW(mode_,CB_SETMINVISIBLE,10,0);
        add_label(L"Effect speed",500,55,95,22);speed_=add_track(APP_SPEED,595,51,280,5,500);speed_val_=add_label(L"1.00x",885,55,65,22);F(speed_);F(speed_val_);

        make_group(L"Playback / capture",18,88,475,250);
        add_label(L"Monitor",35,118,80,22);monitor_=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|CBS_NOINTEGRALHEIGHT|WS_VSCROLL,120,114,350,220,hwnd_,(HMENU)APP_MONITOR,hinst_,nullptr);F(monitor_);
        add_label(L"Brightness",35,156,80,22);brightness_=add_track(APP_BRIGHTNESS,120,152,280,0,255);brightness_val_=add_label(L"255",410,156,45,22);F(brightness_);F(brightness_val_);
        add_label(L"Capture FPS",35,198,80,22);fps_=add_edit(APP_FPS,120,194,65);F(fps_);
        add_label(L"Smoothing",220,198,75,22);smoothing_=add_edit(APP_SMOOTHING,295,194,65);F(smoothing_);
        F(add_label(L"Higher values reduce flicker but add response delay. 0 = instant.",35,226,430,20));
        F(add_label(L"Capture uses a non-blocking low-latency path and may drop busy GPU frames.",35,246,430,20));
        add_label(L"Edge capture depth",35,278,110,22);depth_=add_track(APP_DEPTH,150,274,255,5,250);depth_val_=add_label(L"2.0%",410,278,55,22);F(depth_);F(depth_val_);
        F(add_label(L"Percent of the screen width/height sampled inward from the edges.",35,302,430,20));

        make_group(L"Color processing (Capture only)",510,88,475,250);
        add_label(L"Saturation",530,118,90,22);sat_=add_edit(APP_SAT,625,114,70);F(sat_);F(add_label(L"Color intensity multiplier",705,118,240,22));
        add_label(L"Value",530,160,90,22);value_=add_edit(APP_VALUE,625,156,70);F(value_);F(add_label(L"Captured brightness multiplier",705,160,240,22));
        add_label(L"Gamma",530,202,90,22);gamma_=add_edit(APP_GAMMA,625,198,70);F(gamma_);F(add_label(L"Changes midtone brightness",705,202,240,22));
        add_label(L"Min saturation",530,244,90,22);minsat_=add_edit(APP_MINSAT,625,240,70);F(minsat_);F(add_label(L"Prevents near-gray colors",705,244,240,22));
        add_label(L"Hue shift",530,286,90,22);hue_=add_edit(APP_HUE,625,282,70);F(hue_);F(add_label(L"Rotates captured colors",705,286,240,22));
        F(add_label(L"These settings affect Capture mode only.",530,316,420,18));

        make_group(L"Manual color / effects",18,355,475,265);
        add_label(L"Red",35,385,45,22);red_=add_track(APP_RED,85,381,300,0,255);red_val_=add_label(L"255",395,385,45,22);F(red_);F(red_val_);
        add_label(L"Green",35,425,45,22);green_=add_track(APP_GREEN,85,421,300,0,255);green_val_=add_label(L"64",395,425,45,22);F(green_);F(green_val_);
        add_label(L"Blue",35,465,45,22);blue_=add_track(APP_BLUE,85,461,300,0,255);blue_val_=add_label(L"0",395,465,45,22);F(blue_);F(blue_val_);
        F(add_label(L"Manual RGB is used by Static, Breathing, Wave and Strobe effects.",35,505,425,22));
        F(add_label(L"Rainbow / Fire / Ocean / Forest / Aurora and other modes use their own palettes.",35,535,425,36));

        make_group(L"LED mapping",510,355,475,265);
        revt_=add_check(APP_REVT,L"Reverse top",530,385);revr_=add_check(APP_REVR,L"Reverse right",700,385);F(revt_);F(revr_);
        revl_=add_check(APP_REVL,L"Reverse left",530,420);swap_=add_check(APP_SWAP,L"Swap left/right",700,420);F(revl_);F(swap_);
        add_label(L"Top offset",530,460,80,22);toff_=add_edit(APP_TOFF,615,456,60);F(toff_);
        add_label(L"Right offset",700,460,85,22);roff_=add_edit(APP_ROFF,790,456,60);F(roff_);
        add_label(L"Left offset",530,500,80,22);loff_=add_edit(APP_LOFF,615,496,60);F(loff_);
        F(add_label(L"Mapping fixes physical strip direction/orientation without changing capture.",530,535,420,40));

        make_group(L"What the settings do",18,635,967,165);
        help_=CreateWindowW(L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,32,664,935,118,hwnd_,(HMENU)APP_HELP,hinst_,nullptr);F(help_);

        start_=CreateWindowW(L"BUTTON",L"Start",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,30,820,150,40,hwnd_,(HMENU)APP_START,hinst_,nullptr);F(start_);
        test_=CreateWindowW(L"BUTTON",L"Test LEDs",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,195,820,150,40,hwnd_,(HMENU)APP_TEST,hinst_,nullptr);F(test_);
        save_=CreateWindowW(L"BUTTON",L"Save settings",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,360,820,150,40,hwnd_,(HMENU)APP_SAVE,hinst_,nullptr);F(save_);
        autostart_=CreateWindowW(L"BUTTON",L"Start with Windows",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,525,820,175,40,hwnd_,(HMENU)APP_AUTOSTART,hinst_,nullptr);F(autostart_);
        add_label(L"Status:",720,825,55,22);status_=add_label(L"Stopped",780,825,190,22);F(status_);

        all_controls_font(font);
    }

    void all_controls_font(HFONT font){
        HWND hs[]={title_,mode_,monitor_,brightness_,brightness_val_,fps_,smoothing_,depth_,depth_val_,sat_,value_,gamma_,minsat_,hue_,revt_,revr_,revl_,swap_,toff_,roff_,loff_,start_,test_,save_,autostart_,status_,speed_,speed_val_,red_,red_val_,green_,green_val_,blue_,blue_val_,help_};
        for(HWND h:hs)if(h)SendMessageW(h,WM_SETFONT,(WPARAM)font,TRUE);
    }

    static std::wstring text(HWND h){int n=GetWindowTextLengthW(h);std::wstring s(n,L'\0');if(n)GetWindowTextW(h,s.data(),n+1);return s;}
    static int to_int(HWND h,int fallback){try{return std::stoi(text(h));}catch(...){return fallback;}}
    static float to_float(HWND h,float fallback){try{return std::stof(text(h));}catch(...){return fallback;}}
    static void set_text(HWND h,const std::wstring&s){SetWindowTextW(h,s.c_str());}
    static void set_float(HWND h,float x){std::wostringstream o;o<<std::fixed<<std::setprecision(2)<<x;set_text(h,o.str());}

    void populate_monitors(){SendMessageW(monitor_,CB_RESETCONTENT,0,0);for(auto&m:enumerate_monitors()){std::wostringstream o;o<<m.index<<L": "<<m.name<<L"  ["<<m.right-m.left<<L"x"<<m.bottom-m.top<<L"]";SendMessageW(monitor_,CB_ADDSTRING,0,(LPARAM)o.str().c_str());}}

    void load_ui_from_settings(){
        populate_monitors(); auto s=engine_.get();
        SendMessageW(monitor_,CB_SETCURSEL,std::max(0,s.monitor-1),0);SendMessageW(mode_,CB_SETCURSEL,(int)s.effect.mode,0);
        SendMessageW(brightness_,TBM_SETPOS,TRUE,s.brightness);set_text(fps_,std::to_wstring(s.fps));set_float(smoothing_,s.smoothing);
        set_float(sat_,s.color.saturation_boost);set_float(value_,s.color.value_boost);set_float(gamma_,s.color.gamma);set_float(minsat_,s.color.min_saturation);set_float(hue_,s.color.hue_shift_degrees);
        SendMessageW(depth_,TBM_SETPOS,TRUE,(LPARAM)std::clamp((int)std::lround(s.color.capture_depth_percent*10.f),5,250));
        SendMessageW(speed_,TBM_SETPOS,TRUE,(LPARAM)std::clamp((int)std::lround(s.effect.speed*100.f),5,500));
        SendMessageW(red_,TBM_SETPOS,TRUE,s.effect.manual.r);SendMessageW(green_,TBM_SETPOS,TRUE,s.effect.manual.g);SendMessageW(blue_,TBM_SETPOS,TRUE,s.effect.manual.b);
        SendMessageW(revt_,BM_SETCHECK,s.mapping.reverse_top?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(revr_,BM_SETCHECK,s.mapping.reverse_right?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(revl_,BM_SETCHECK,s.mapping.reverse_left?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(swap_,BM_SETCHECK,s.mapping.swap_left_right?BST_CHECKED:BST_UNCHECKED,0);
        set_text(toff_,std::to_wstring(s.mapping.top_offset));set_text(roff_,std::to_wstring(s.mapping.right_offset));set_text(loff_,std::to_wstring(s.mapping.left_offset));update_dynamic_values();update_help();refresh_status();
    }

    AppSettings read_ui() const {
        AppSettings s=engine_.get();int sel=(int)SendMessageW(monitor_,CB_GETCURSEL,0,0);if(sel>=0)s.monitor=sel+1;
        int mode=(int)SendMessageW(mode_,CB_GETCURSEL,0,0);if(mode>=0)s.effect.mode=(LightMode)mode;
        s.brightness=(int)SendMessageW(brightness_,TBM_GETPOS,0,0);s.fps=std::clamp(to_int(fps_,s.fps),1,120);s.smoothing=std::clamp(to_float(smoothing_,s.smoothing),0.f,.99f);
        s.effect.speed=std::clamp((float)SendMessageW(speed_,TBM_GETPOS,0,0)/100.f,.05f,5.f);
        s.effect.manual={(uint8_t)SendMessageW(red_,TBM_GETPOS,0,0),(uint8_t)SendMessageW(green_,TBM_GETPOS,0,0),(uint8_t)SendMessageW(blue_,TBM_GETPOS,0,0)};
        s.color.capture_depth_percent=std::clamp((float)SendMessageW(depth_,TBM_GETPOS,0,0)/10.f,.5f,25.f);
        s.color.saturation_boost=std::clamp(to_float(sat_,s.color.saturation_boost),0.f,4.f);s.color.value_boost=std::clamp(to_float(value_,s.color.value_boost),0.f,4.f);s.color.gamma=std::clamp(to_float(gamma_,s.color.gamma),.1f,3.f);s.color.min_saturation=std::clamp(to_float(minsat_,s.color.min_saturation),0.f,1.f);s.color.hue_shift_degrees=std::clamp(to_float(hue_,s.color.hue_shift_degrees),-180.f,180.f);
        s.mapping.reverse_top=SendMessageW(revt_,BM_GETCHECK,0,0)==BST_CHECKED;s.mapping.reverse_right=SendMessageW(revr_,BM_GETCHECK,0,0)==BST_CHECKED;s.mapping.reverse_left=SendMessageW(revl_,BM_GETCHECK,0,0)==BST_CHECKED;s.mapping.swap_left_right=SendMessageW(swap_,BM_GETCHECK,0,0)==BST_CHECKED;
        s.mapping.top_offset=to_int(toff_,s.mapping.top_offset);s.mapping.right_offset=to_int(roff_,s.mapping.right_offset);s.mapping.left_offset=to_int(loff_,s.mapping.left_offset);return s;
    }

    void save_ui(){auto s=read_ui();engine_.update(s);update_dynamic_values();}
    void toggle_start(){save_ui();if(engine_.running()){engine_.stop();SetWindowTextW(start_,L"Start");}else{engine_.start();SetWindowTextW(start_,L"Stop");}refresh_status();}
    void refresh_status(){set_text(status_,engine_.status());SetWindowTextW(start_,engine_.running()?L"Stop":L"Start");}
    void update_dynamic_values(){
        set_text(brightness_val_,std::to_wstring((int)SendMessageW(brightness_,TBM_GETPOS,0,0)));
        float sp=(float)SendMessageW(speed_,TBM_GETPOS,0,0)/100.f;set_float(speed_val_,sp);
        float dep=(float)SendMessageW(depth_,TBM_GETPOS,0,0)/10.f;std::wostringstream d;d<<std::fixed<<std::setprecision(1)<<dep<<L"%";set_text(depth_val_,d.str());
        set_text(red_val_,std::to_wstring((int)SendMessageW(red_,TBM_GETPOS,0,0)));set_text(green_val_,std::to_wstring((int)SendMessageW(green_,TBM_GETPOS,0,0)));set_text(blue_val_,std::to_wstring((int)SendMessageW(blue_,TBM_GETPOS,0,0)));
    }

    void update_help(){int idx=(int)SendMessageW(mode_,CB_GETCURSEL,0,0);LightMode m=idx>=0?(LightMode)idx:LightMode::Capture;std::wstring s;
        switch(m){
            case LightMode::Capture:s=L"Capture: samples color from the chosen monitor edges and maps it to the LEDs. Edge capture depth controls how far inward the sampled strip reaches: 1% is very close to the border; 10-25% reaches deeper into the picture. Start around 2-5% for a typical monitor.";break;
            case LightMode::Static:s=L"Static / Manual: keeps every LED at the manual RGB color. Use the Red/Green/Blue sliders below to choose the color.";break;
            case LightMode::Rainbow:s=L"Rainbow: continuously moves a full-spectrum rainbow around the strip. Effect speed controls how quickly the colors travel.";break;
            case LightMode::ColorCycle:s=L"Color cycle: the whole strip smoothly cycles through hues together. Good for a simple idle/background animation.";break;
            case LightMode::Breathing:s=L"Breathing: your manual color fades in and out. Higher smoothing makes the change softer; effect speed controls the breathing rate.";break;
            case LightMode::Wave:s=L"Wave: your manual color forms a moving brightness wave around the strip. Effect speed controls the wave movement.";break;
            case LightMode::Strobe:s=L"Strobe: your manual color flashes on briefly, then turns off. Use lower brightness for comfort; effect speed increases the flash rate.";break;
            case LightMode::Fire:s=L"Fire: animated red/orange/yellow fire-like movement. It ignores the manual RGB color and uses its own palette.";break;
            case LightMode::Ocean:s=L"Ocean: slow blue/cyan water-like animation. It ignores the manual RGB color.";break;
            case LightMode::Forest:s=L"Forest: animated green tones with gentle variation. It ignores the manual RGB color.";break;
            case LightMode::Aurora:s=L"Aurora: moving green/teal/blue spectral gradients. It ignores the manual RGB color.";break;
            case LightMode::Twinkle:s=L"Twinkle: your manual color sits dimly on the strip while random-looking LEDs brighten and fade like stars.";break;
            case LightMode::Police:s=L"Police: alternating red and blue halves. Effect speed controls the alternation rate.";break;
            case LightMode::Warm:s=L"Warm white: fixed warm-white lighting, useful as a neutral ambient light for a room.";break;
            case LightMode::Cool:s=L"Cool white: fixed cool-white lighting, closer to a daylight/cool lamp appearance.";break;
            case LightMode::White:s=L"White: fixed maximum white on the strip. Overall brightness still controls the final intensity.";break;
        }
        s+=L"\r\n\r\nSaturation increases or decreases color strength; Value changes captured brightness; Gamma changes the middle tones; Min saturation prevents weak colors from looking gray; Hue shift rotates captured colors. Mapping controls are for fixing the physical LED order/orientation.";
        SetWindowTextW(help_,s.c_str());
    }

    void add_tray(){NOTIFYICONDATAW nid{sizeof(nid)};nid.hWnd=hwnd_;nid.uID=ID_TRAY;nid.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;nid.uCallbackMessage=WMAPP_TRAY;nid.hIcon=nullptr;wcscpy_s(nid.szTip,L"Quiklight Windows");Shell_NotifyIconW(NIM_ADD,&nid);}
    void remove_tray(){if(!hwnd_)return;NOTIFYICONDATAW nid{sizeof(nid)};nid.hWnd=hwnd_;nid.uID=ID_TRAY;Shell_NotifyIconW(NIM_DELETE,&nid);}
    void show_tray_menu(){POINT pt;GetCursorPos(&pt);HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,engine_.running()?L"Stop":L"Start");AppendMenuW(menu,MF_STRING,2,L"Open");AppendMenuW(menu,MF_STRING,3,L"Exit");SetForegroundWindow(hwnd_);UINT c=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,pt.x,pt.y,0,hwnd_,nullptr);DestroyMenu(menu);if(c==1)toggle_start();else if(c==2){ShowWindow(hwnd_,SW_SHOW);SetForegroundWindow(hwnd_);}else if(c==3)DestroyWindow(hwnd_);}
    void install_autostart(){wchar_t startup[MAX_PATH]{};if(FAILED(SHGetFolderPathW(nullptr,CSIDL_STARTUP,nullptr,SHGFP_TYPE_CURRENT,startup)))return;std::wstring shortcutPath=std::wstring(startup)+L"\\Quiklight Windows.lnk";HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);bool needUninit=SUCCEEDED(hr);IShellLinkW*link=nullptr;if(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link)))){link->SetPath((exe_directory()+L"\\QuiklightWindows.exe").c_str());link->SetDescription(L"Quiklight Windows ambient LED service");IPersistFile*pf=nullptr;if(SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(&pf)))){pf->Save(shortcutPath.c_str(),TRUE);pf->Release();}link->Release();}if(needUninit)CoUninitialize();MessageBoxW(hwnd_,L"A startup shortcut was created in your user Startup folder.",L"Quiklight Windows",MB_OK|MB_ICONINFORMATION);}

    HINSTANCE hinst_{};HWND hwnd_{};UINT_PTR timer_{};Engine engine_;
    HWND title_{},mode_{},mode_label_{},monitor_{},brightness_{},brightness_val_{},fps_{},smoothing_{},depth_{},depth_val_{},sat_{},value_{},gamma_{},minsat_{},hue_{};
    HWND revt_{},revr_{},revl_{},swap_{},toff_{},roff_{},loff_{},start_{},test_{},save_{},autostart_{},status_{},help_{};
    HWND speed_{},speed_val_{},red_{},red_val_{},green_{},green_val_{},blue_{},blue_val_{};
};

static void usage(){std::wcout<<L"Quiklight Windows\n\nWith no arguments, the GUI starts.\n\nCLI:\n  --list-devices\n  --list-monitors\n  --help\n";}

static int cli(int argc,wchar_t**argv){
    bool listd=false,listm=false;for(int i=1;i<argc;i++){std::wstring a=argv[i];if(a==L"--help"||a==L"-h"){usage();return 0;}if(a==L"--list-devices")listd=true;if(a==L"--list-monitors")listm=true;}
    if(listd){auto ds=QuiklightHid::listDevices();for(auto&d:ds)std::wcout<<L"VID=0x"<<std::hex<<d.vendor_id<<L" PID=0x"<<d.product_id<<std::dec<<L" "<<d.manufacturer<<L" "<<d.product<<L"\n"<<d.path<<L"\n";return 0;}
    if(listm){for(auto&m:enumerate_monitors())std::wcout<<m.index<<L": "<<m.name<<L" ["<<m.left<<L","<<m.top<<L" - "<<m.right<<L","<<m.bottom<<L"]\n";return 0;}
    return 0;
}

} // namespace

int APIENTRY wWinMain(HINSTANCE h,HINSTANCE,PWSTR,int){
    int argc=0;wchar_t** argv=CommandLineToArgvW(GetCommandLineW(),&argc);bool cliMode=false;for(int i=1;i<argc;i++){std::wstring a=argv[i];if(a==L"--cli"||a==L"--list-devices"||a==L"--list-monitors"||a==L"--help")cliMode=true;}int result=0;
    if(cliMode)result=cli(argc,argv);else{Gui gui(h);if(!gui.create())return 1;result=gui.run();}
    LocalFree(argv);return result;
}
