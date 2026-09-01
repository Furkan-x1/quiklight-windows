#include "screen_capture.hpp"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <stdexcept>
#include <vector>
#include <string>
#include <cstring>
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")
using Microsoft::WRL::ComPtr;
namespace quiklight {
static BOOL CALLBACK enum_mon_proc(HMONITOR h,HDC,LPRECT rc,LPARAM lp){auto*v=reinterpret_cast<std::vector<MonitorInfo>*>(lp);MONITORINFOEXW mi{};mi.cbSize=sizeof(mi);GetMonitorInfoW(h,&mi);MonitorInfo x{};x.index=(int)v->size()+1;x.name=mi.szDevice;x.device_name=mi.szDevice;x.left=mi.rcMonitor.left;x.top=mi.rcMonitor.top;x.right=mi.rcMonitor.right;x.bottom=mi.rcMonitor.bottom;v->push_back(std::move(x));return TRUE;}
std::vector<MonitorInfo> enumerate_monitors(){std::vector<MonitorInfo>v;EnumDisplayMonitors(nullptr,nullptr,enum_mon_proc,reinterpret_cast<LPARAM>(&v));return v;}
struct ScreenCapture::Impl {
 int monitor_index; bool verbose; ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> ctx; ComPtr<IDXGIOutputDuplication> dupl; ComPtr<ID3D11Texture2D> staging; UINT width=0,height=0; std::vector<uint32_t> pixels; LARGE_INTEGER freq{},last{};
 Impl(int i,bool v):monitor_index(i),verbose(v){init();QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&last);}
 void init(){auto mons=enumerate_monitors();if(monitor_index<1||monitor_index>(int)mons.size())throw std::runtime_error("Invalid monitor index");HMONITOR target=nullptr;struct FindCtx{int idx,cur;HMONITOR h;};FindCtx fc{monitor_index,0,nullptr};EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR h,HDC,LPRECT,LPARAM lp)->BOOL{auto*f=(FindCtx*)lp;if(++f->cur==f->idx){f->h=h;return FALSE;}return TRUE;},(LPARAM)&fc);target=fc.h;if(!target)throw std::runtime_error("Monitor not found");ComPtr<IDXGIFactory1> fac;HRESULT hr=CreateDXGIFactory1(IID_PPV_ARGS(&fac));if(FAILED(hr))throw std::runtime_error("CreateDXGIFactory1 failed");ComPtr<IDXGIAdapter1> chosen;ComPtr<IDXGIOutput> output;for(UINT ai=0;fac->EnumAdapters1(ai,&chosen)==S_OK;ai++){for(UINT oi=0;;oi++){ComPtr<IDXGIOutput> o;if(chosen->EnumOutputs(oi,&o)!=S_OK)break;DXGI_OUTPUT_DESC od{};o->GetDesc(&od);if(od.Monitor==target){output=o;break;}}if(output)break;chosen.Reset();}if(!output)throw std::runtime_error("Could not map Windows monitor to DXGI output");D3D_FEATURE_LEVEL fl;UINT flags=D3D11_CREATE_DEVICE_BGRA_SUPPORT;hr=D3D11CreateDevice(chosen.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,flags,nullptr,0,D3D11_SDK_VERSION,&device,&fl,&ctx);if(FAILED(hr))throw std::runtime_error("D3D11CreateDevice failed");ComPtr<IDXGIOutput1> out1;output.As(&out1);hr=out1->DuplicateOutput(device.Get(),&dupl);if(FAILED(hr))throw std::runtime_error("Desktop Duplication unavailable (try Windows 10/11 desktop session)");DXGI_OUTDUPL_DESC dd{};dupl->GetDesc(&dd);width=dd.ModeDesc.Width;height=dd.ModeDesc.Height;create_staging();}
 void create_staging(){staging.Reset();D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=1;d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;HRESULT hr=device->CreateTexture2D(&d,nullptr,&staging);if(FAILED(hr))throw std::runtime_error("Create staging texture failed");pixels.resize((size_t)width*height);}
 bool frame(const ScreenCapture::Callback&cb){DXGI_OUTDUPL_FRAME_INFO fi{};ComPtr<IDXGIResource> res;HRESULT hr=dupl->AcquireNextFrame(5,&fi,&res);if(hr==DXGI_ERROR_WAIT_TIMEOUT)return true;if(hr==DXGI_ERROR_ACCESS_LOST){init();return false;}if(FAILED(hr))return false;ComPtr<ID3D11Texture2D> tex;res.As(&tex);ctx->CopyResource(staging.Get(),tex.Get());D3D11_MAPPED_SUBRESOURCE m{};hr=ctx->Map(staging.Get(),0,D3D11_MAP_READ,0,&m);if(SUCCEEDED(hr)){for(UINT y=0;y<height;y++)memcpy(pixels.data()+(size_t)y*width,(const uint8_t*)m.pData+(size_t)y*m.RowPitch,(size_t)width*4);ctx->Unmap(staging.Get(),0);cb(width,height,pixels.data());}dupl->ReleaseFrame();return SUCCEEDED(hr);}
};
ScreenCapture::ScreenCapture(int i,bool v):p_(new Impl(i,v)){}
ScreenCapture::~ScreenCapture(){delete p_;}
bool ScreenCapture::capture(const Callback&cb){return p_->frame(cb);}
}
