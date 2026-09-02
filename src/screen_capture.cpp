#include "screen_capture.hpp"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <stdexcept>
#include <vector>
#include <array>
#include <string>
#include <cstring>
#include <algorithm>
#include <condition_variable>
#include <mutex>

#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"windowsapp.lib")

using Microsoft::WRL::ComPtr;

namespace quiklight {

static BOOL CALLBACK enum_mon_proc(HMONITOR h,HDC,LPRECT,LPARAM lp){
    auto*v=reinterpret_cast<std::vector<MonitorInfo>*>(lp);
    MONITORINFOEXW mi{}; mi.cbSize=sizeof(mi);
    if(!GetMonitorInfoW(h,&mi)) return TRUE;
    MonitorInfo x{};
    x.index=(int)v->size()+1;
    x.name=mi.szDevice;
    x.device_name=mi.szDevice;
    x.left=mi.rcMonitor.left; x.top=mi.rcMonitor.top;
    x.right=mi.rcMonitor.right; x.bottom=mi.rcMonitor.bottom;
    v->push_back(std::move(x));
    return TRUE;
}

std::vector<MonitorInfo> enumerate_monitors(){
    std::vector<MonitorInfo>v;
    EnumDisplayMonitors(nullptr,nullptr,enum_mon_proc,reinterpret_cast<LPARAM>(&v));
    return v;
}

static HMONITOR monitor_from_index(int index){
    HMONITOR target=nullptr;
    struct FindCtx{int idx,cur;HMONITOR h;};
    FindCtx fc{index,0,nullptr};
    EnumDisplayMonitors(nullptr,nullptr,
        [](HMONITOR h,HDC,LPRECT,LPARAM lp)->BOOL{
            auto*f=reinterpret_cast<FindCtx*>(lp);
            if(++f->cur==f->idx){f->h=h;return FALSE;}
            return TRUE;
        },(LPARAM)&fc);
    return fc.h;
}

static void throw_hresult(HRESULT hr,const char* what){
    if(FAILED(hr)){
        char buf[96]{};
        sprintf_s(buf,"%s (HRESULT 0x%08lX)",what,(unsigned long)hr);
        throw std::runtime_error(buf);
    }
}

static void create_d3d11_for_monitor(
    HMONITOR target,
    ComPtr<ID3D11Device>& device,
    ComPtr<ID3D11DeviceContext>& ctx){

    ComPtr<IDXGIFactory1> fac;
    throw_hresult(CreateDXGIFactory1(IID_PPV_ARGS(&fac)),"CreateDXGIFactory1 failed");

    ComPtr<IDXGIAdapter1> chosen;
    ComPtr<IDXGIOutput> output;
    for(UINT ai=0;;++ai){
        chosen.Reset();
        if(fac->EnumAdapters1(ai,&chosen)!=S_OK) break;
        for(UINT oi=0;;++oi){
            ComPtr<IDXGIOutput> o;
            if(chosen->EnumOutputs(oi,&o)!=S_OK) break;
            DXGI_OUTPUT_DESC od{};
            o->GetDesc(&od);
            if(od.Monitor==target){ output=o; break; }
        }
        if(output) break;
    }
    if(!output) throw std::runtime_error("Could not map Windows monitor to a DXGI output");

    D3D_FEATURE_LEVEL fl{};
    const UINT flags=D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    throw_hresult(D3D11CreateDevice(chosen.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,flags,
                                     nullptr,0,D3D11_SDK_VERSION,&device,&fl,&ctx),
                  "D3D11CreateDevice failed");
}

struct ScreenCapture::Impl {
    enum class Backend { Wgc, Dxgi };

    int monitor_index;
    bool verbose;
    Backend backend=Backend::Wgc;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> ctx;
    UINT width=0,height=0;

    struct StagingSlot {
        ComPtr<ID3D11Texture2D> texture;
        ComPtr<ID3D11Query> query;
        bool pending=false;
        uint64_t sequence=0;
    };
    std::array<StagingSlot,3> slots{};
    uint64_t next_sequence=1;

    // WGC state.
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem wgc_item{nullptr};
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool wgc_pool{nullptr};
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession wgc_session{nullptr};
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice wgc_device{nullptr};
    bool winrt_initialized=false;
    std::mutex frame_mutex;
    std::condition_variable frame_cv;
    bool frame_arrived=false;
    winrt::event_token frame_arrived_token{};

    // DXGI fallback state.
    ComPtr<IDXGIOutputDuplication> dupl;

    Impl(int i,bool v):monitor_index(i),verbose(v){
        // This object is created and destroyed on the capture worker thread.
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        winrt_initialized=true;
        init();
    }

    ~Impl(){
        if (wgc_pool && frame_arrived_token.value) {
            try { wgc_pool.FrameArrived(frame_arrived_token); } catch (...) {}
            frame_arrived_token = {};
        }
        { std::lock_guard<std::mutex> lock(frame_mutex); frame_arrived=false; }
        frame_cv.notify_all();
        wgc_session={nullptr};
        wgc_pool={nullptr};
        wgc_item={nullptr};
        wgc_device={nullptr};
        if(winrt_initialized) winrt::uninit_apartment();
    }

    void init(){
        dupl.Reset();
        wgc_session={nullptr}; wgc_pool={nullptr}; wgc_item={nullptr}; wgc_device={nullptr};
        for(auto& s:slots){s.texture.Reset();s.query.Reset();s.pending=false;s.sequence=0;}
        next_sequence=1;

        auto mons=enumerate_monitors();
        if(monitor_index<1||monitor_index>(int)mons.size()) throw std::runtime_error("Invalid monitor index");
        HMONITOR target=monitor_from_index(monitor_index);
        if(!target) throw std::runtime_error("Monitor not found");

        std::string wgc_error;
        try {
            init_wgc(target);
            backend=Backend::Wgc;
        } catch(const std::exception& e){
            wgc_error=e.what();
            if(verbose){ OutputDebugStringA((std::string("Quiklight WGC failed: ")+e.what()+"\n").c_str()); }
            // Keep a known-good fallback. This also handles older/problematic Windows builds.
            init_dxgi(target);
            backend=Backend::Dxgi;
        }

        create_staging();
    }

    void init_wgc(HMONITOR target){
        using namespace winrt::Windows::Graphics::Capture;
        using namespace winrt::Windows::Graphics::DirectX;

        if(!GraphicsCaptureSession::IsSupported())
            throw std::runtime_error("Windows Graphics Capture is not supported");

        create_d3d11_for_monitor(target,device,ctx);

        ComPtr<IDXGIDevice> dxgi_device;
        throw_hresult(device.As(&dxgi_device),"Query IDXGIDevice failed");

        throw_hresult(
            CreateDirect3D11DeviceFromDXGIDevice(
                dxgi_device.Get(),
                reinterpret_cast<::IInspectable**>(winrt::put_abi(wgc_device))),
            "CreateDirect3D11DeviceFromDXGIDevice failed");

        auto factory=winrt::get_activation_factory<GraphicsCaptureItem,IGraphicsCaptureItemInterop>();
        winrt::check_hresult(factory->CreateForMonitor(target,winrt::guid_of<GraphicsCaptureItem>(),winrt::put_abi(wgc_item)));
        if(!wgc_item) throw std::runtime_error("CreateForMonitor returned no capture item");

        const auto size=wgc_item.Size();
        width=(UINT)std::max<int32_t>(1,size.Width);
        height=(UINT)std::max<int32_t>(1,size.Height);

        wgc_pool=Direct3D11CaptureFramePool::CreateFreeThreaded(
            wgc_device,
            DirectXPixelFormat::B8G8R8A8UIntNormalized,
            3,
            { (int)width,(int)height });

        // Do not busy-poll TryGetNextFrame(). The capture system already exposes
        // a worker-thread FrameArrived event; use it to wake our capture thread.
        frame_arrived_token = wgc_pool.FrameArrived(
            [this](auto&&, auto&&) {
                {
                    std::lock_guard<std::mutex> lock(frame_mutex);
                    frame_arrived = true;
                }
                frame_cv.notify_one();
            });

        wgc_session=wgc_pool.CreateCaptureSession(wgc_item);
        try { wgc_session.IsCursorCaptureEnabled(false); } catch(...) {}
        try { wgc_session.IsBorderRequired(false); } catch(...) {}
        wgc_session.StartCapture();
    }

    void init_dxgi(HMONITOR target){
        create_d3d11_for_monitor(target,device,ctx);
        ComPtr<IDXGIOutput> output;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIDevice> dxgiDevice;
        throw_hresult(device.As(&dxgiDevice),"D3D11 device does not expose IDXGIDevice");
        throw_hresult(dxgiDevice->GetAdapter(&adapter),"GetAdapter failed");
        for(UINT oi=0;;++oi){
            ComPtr<IDXGIOutput> o;
            if(adapter->EnumOutputs(oi,&o)!=S_OK) break;
            DXGI_OUTPUT_DESC od{}; o->GetDesc(&od);
            if(od.Monitor==target){output=o;break;}
        }
        if(!output) throw std::runtime_error("Could not locate DXGI output for monitor");
        ComPtr<IDXGIOutput1> out1;
        throw_hresult(output.As(&out1),"IDXGIOutput1 unavailable");
        HRESULT hr=out1->DuplicateOutput(device.Get(),&dupl);
        throw_hresult(hr,"Desktop Duplication unavailable");
        DXGI_OUTDUPL_DESC dd{}; dupl->GetDesc(&dd);
        width=dd.ModeDesc.Width; height=dd.ModeDesc.Height;
    }

    void create_staging(){
        D3D11_TEXTURE2D_DESC d{};
        d.Width=width; d.Height=height;
        d.MipLevels=1; d.ArraySize=1;
        d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count=1;
        d.Usage=D3D11_USAGE_STAGING;
        d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;

        D3D11_QUERY_DESC qd{}; qd.Query=D3D11_QUERY_EVENT;
        for(auto& s:slots){
            throw_hresult(device->CreateTexture2D(&d,nullptr,&s.texture),"Create staging texture failed");
            throw_hresult(device->CreateQuery(&qd,&s.query),"Create GPU completion query failed");
        }
    }

    bool poll_ready(const ScreenCapture::Callback& cb){
        int best=-1; uint64_t best_seq=0;
        for(int i=0;i<(int)slots.size();++i){
            auto&s=slots[i]; if(!s.pending) continue;
            HRESULT qr=ctx->GetData(s.query.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(qr==S_FALSE) continue;
            if(FAILED(qr)){s.pending=false;continue;}
            // The GPU query has completed, so this staging resource is safe to
            // map and, after unmapping, safe to reuse. Prefer the newest completed
            // frame to keep latency low.
            if(s.sequence>=best_seq){best=i;best_seq=s.sequence;}
        }
        if(best<0) return false;
        auto&s=slots[best];
        D3D11_MAPPED_SUBRESOURCE m{};
        if(FAILED(ctx->Map(s.texture.Get(),0,D3D11_MAP_READ,0,&m))){s.pending=false;return false;}
        cb(width,height,static_cast<const uint32_t*>(m.pData),m.RowPitch/sizeof(uint32_t));
        ctx->Unmap(s.texture.Get(),0);
        s.pending=false;

        // Do not mark older *unfinished* copies as free. Reusing a staging
        // texture while the GPU still owns the previous CopyResource can cause
        // an implicit GPU/CPU synchronization and is a major source of stalls.
        // Completed older slots, however, can be discarded immediately.
        for(auto&x:slots){
            if(x.pending && x.sequence<best_seq){
                HRESULT qr=ctx->GetData(x.query.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH);
                if(qr!=S_FALSE) x.pending=false;
            }
        }
        return true;
    }

    bool submit_texture(ID3D11Texture2D* texture){
        int free_slot=-1;
        // Prefer an unused slot. If all are busy, drop the new frame instead
        // of blocking or building a latency backlog.
        for(int i=0;i<(int)slots.size();++i){ if(!slots[i].pending){free_slot=i;break;} }
        if(free_slot<0) return false;
        auto&s=slots[free_slot];
        ctx->CopyResource(s.texture.Get(),texture);
        ctx->End(s.query.Get());
        s.pending=true; s.sequence=next_sequence++;
        return true;
    }

    bool frame_wgc(const ScreenCapture::Callback&cb){
        // Wait for the capture system instead of spinning at 1000+ Hz when no
        // frame is available. This also avoids competing with a game that is
        // trying to present frames on the same GPU.
        {
            std::unique_lock<std::mutex> lock(frame_mutex);
            frame_cv.wait_for(lock, std::chrono::milliseconds(8), [&]{ return frame_arrived; });
            frame_arrived=false;
        }

        bool delivered=poll_ready(cb);
        try {
            winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame latest{nullptr};
            for(int guard=0; guard<8; ++guard){
                auto next=wgc_pool.TryGetNextFrame();
                if(!next) break;
                latest=next;
            }
            if(latest){
                auto surface=latest.Surface();
                winrt::com_ptr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access=
                    surface.as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
                winrt::com_ptr<ID3D11Texture2D> texture;
                winrt::check_hresult(access->GetInterface(winrt::guid_of<ID3D11Texture2D>(),texture.put_void()));
                submit_texture(texture.get());
            }
        } catch(const winrt::hresult_error& e){
            if(verbose){
                std::string msg="Quiklight WGC frame error: "+winrt::to_string(e.message())+"\n";
                OutputDebugStringA(msg.c_str());
            }
        } catch(...){ }
        if(!delivered) delivered=poll_ready(cb);
        return delivered;
    }

    bool frame_dxgi(const ScreenCapture::Callback&cb){
        bool delivered=poll_ready(cb);
        DXGI_OUTDUPL_FRAME_INFO fi{}; ComPtr<IDXGIResource> res;
        HRESULT hr=dupl->AcquireNextFrame(0,&fi,&res);
        if(hr==DXGI_ERROR_WAIT_TIMEOUT) return delivered;
        if(hr==DXGI_ERROR_ACCESS_LOST){ init(); return delivered; }
        if(FAILED(hr)) return delivered;
        ComPtr<ID3D11Texture2D> tex;
        if(SUCCEEDED(res.As(&tex))) submit_texture(tex.Get());
        dupl->ReleaseFrame();
        if(!delivered) delivered=poll_ready(cb);
        return delivered;
    }

    bool frame(const ScreenCapture::Callback&cb){
        return backend==Backend::Wgc ? frame_wgc(cb) : frame_dxgi(cb);
    }

    const char* backend_name() const { return backend==Backend::Wgc ? "Windows Graphics Capture" : "Desktop Duplication"; }
};

ScreenCapture::ScreenCapture(int i,bool v):p_(new Impl(i,v)){}
ScreenCapture::~ScreenCapture(){delete p_;}
bool ScreenCapture::capture(const Callback&cb){return p_->frame(cb);}
const char* ScreenCapture::backend_name() const{return p_->backend_name();}

}
