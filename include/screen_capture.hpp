#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace quiklight {
struct MonitorInfo { int index; std::wstring name; std::wstring device_name; long left,top,right,bottom; };
std::vector<MonitorInfo> enumerate_monitors();
class ScreenCapture {
public:
 using Callback=std::function<void(uint32_t,uint32_t,const uint32_t*)>;
 explicit ScreenCapture(int monitor_index,bool verbose=false);
 ~ScreenCapture();
 bool capture(const Callback& cb);
private:
 struct Impl; Impl* p_;
};
}
