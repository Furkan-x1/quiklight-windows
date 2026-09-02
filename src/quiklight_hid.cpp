#include "quiklight_hid.hpp"
#include <setupapi.h>
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <iomanip>
#include <cwctype>
#pragma comment(lib,"setupapi.lib")
#pragma comment(lib,"hid.lib")
namespace quiklight {
static uint8_t checksum8(const std::vector<uint8_t>&d){uint32_t s=0;for(auto b:d)s+=b;return(uint8_t)s;}
static std::wstring getstr(HANDLE h, DWORD which){WCHAR b[256]{};BOOLEAN ok=FALSE;if(which==0)ok=HidD_GetManufacturerString(h,b,sizeof(b));else if(which==1)ok=HidD_GetProductString(h,b,sizeof(b));else ok=HidD_GetSerialNumberString(h,b,sizeof(b));return ok?std::wstring(b):L"";}
std::vector<HidDeviceInfo> QuiklightHid::listDevices(){
 std::vector<HidDeviceInfo> out;
 GUID g{}; HidD_GetHidGuid(&g);
 HDEVINFO info=SetupDiGetClassDevsW(&g,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
 if(info==INVALID_HANDLE_VALUE) return out;
 SP_DEVICE_INTERFACE_DATA id{}; id.cbSize=sizeof(id);
 for(DWORD i=0;SetupDiEnumDeviceInterfaces(info,nullptr,&g,i,&id);++i){
  DWORD need=0; SetupDiGetDeviceInterfaceDetailW(info,&id,nullptr,0,&need,nullptr);
  if(!need) continue;
  std::vector<BYTE> buf(need);
  auto detail=reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buf.data());
  detail->cbSize=sizeof(*detail);
  if(!SetupDiGetDeviceInterfaceDetailW(info,&id,detail,need,nullptr,nullptr)) continue;
  std::wstring path=detail->DevicePath;
  std::wstring lower=path;
  std::transform(lower.begin(),lower.end(),lower.begin(),[](wchar_t c){return (wchar_t)towlower(c);});
  if(lower.find(L"vid_1a86") == std::wstring::npos || lower.find(L"pid_fe07") == std::wstring::npos) continue;

  HidDeviceInfo d{}; d.path=path; d.vendor_id=0x1A86; d.product_id=0xFE07; d.output_report_len=65;
  HANDLE h=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
  if(h==INVALID_HANDLE_VALUE)
    h=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
  if(h!=INVALID_HANDLE_VALUE){
   HIDD_ATTRIBUTES a{}; a.Size=sizeof(a);
   if(HidD_GetAttributes(h,&a)){ d.vendor_id=a.VendorID; d.product_id=a.ProductID; }
   d.manufacturer=getstr(h,0); d.product=getstr(h,1); d.serial=getstr(h,2);
   PHIDP_PREPARSED_DATA pp=nullptr; HIDP_CAPS caps{};
   if(HidD_GetPreparsedData(h,&pp)){
    if(HidP_GetCaps(pp,&caps)==HIDP_STATUS_SUCCESS && caps.OutputReportByteLength) d.output_report_len=caps.OutputReportByteLength;
    HidD_FreePreparsedData(pp);
   }
   CloseHandle(h);
  }
  out.push_back(std::move(d));
 }
 SetupDiDestroyDeviceInfoList(info); return out;
}
QuiklightHid::QuiklightHid(uint16_t vid,uint16_t pid,uint8_t b,const std::wstring&p):vid_(vid),pid_(pid),brightness_(b),forced_path_(p){open();initialize();}
QuiklightHid::~QuiklightHid(){if(dev_!=INVALID_HANDLE_VALUE)CloseHandle(dev_);}
void QuiklightHid::open(){
 std::vector<HidDeviceInfo> candidates;
 if(!forced_path_.empty()) candidates.push_back({forced_path_,vid_,pid_,L"",L"",L"",65});
 else {
  for(const auto& d:listDevices()) if(d.vendor_id==vid_ && d.product_id==pid_) candidates.push_back(d);
 }
 if(candidates.empty()) throw std::runtime_error("Quiklight HID device not found (VID=1A86 PID=FE07)");
 std::stable_sort(candidates.begin(), candidates.end(), [](const HidDeviceInfo& a, const HidDeviceInfo& b){
  auto score = [](const std::wstring& path){
   std::wstring p=path;
   std::transform(p.begin(),p.end(),p.begin(),[](wchar_t c){return (wchar_t)towlower(c);});
   if(p.find(L"&mi_00#")!=std::wstring::npos) return 0;
   if(p.find(L"&mi_01#")!=std::wstring::npos) return 100;
   return 10;
  };
  return score(a.path) < score(b.path);
 });
 DWORD lastErr=ERROR_FILE_NOT_FOUND; std::wstring lastPath;
 for(const auto& d:candidates){
  lastPath=d.path;
  HANDLE h=CreateFileW(d.path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
  if(h==INVALID_HANDLE_VALUE){
   lastErr=GetLastError();
   h=CreateFileW(d.path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
   if(h==INVALID_HANDLE_VALUE){ lastErr=GetLastError(); continue; }
  }
  dev_=h;
  report_len_=d.output_report_len ? d.output_report_len : 65;
  PHIDP_PREPARSED_DATA pp=nullptr; HIDP_CAPS caps{};
  if(HidD_GetPreparsedData(dev_,&pp)){
   if(HidP_GetCaps(pp,&caps)==HIDP_STATUS_SUCCESS && caps.OutputReportByteLength) report_len_=caps.OutputReportByteLength;
   HidD_FreePreparsedData(pp);
  }
  if(report_len_<65) report_len_=65;
  return;
 }
 std::ostringstream oss; oss<<"Failed to open Quiklight HID device (Windows error "<<lastErr<<")";
 throw std::runtime_error(oss.str());
}
bool QuiklightHid::sendFrame(const LedFrame&f){return sendPacket(sync(f));}
bool QuiklightHid::setBrightness(uint8_t value){brightness_=value; return sendPacket(makeBrightnessPacket(value));}
void QuiklightHid::initialize(){if(!sendPacket(setOpen()))throw std::runtime_error("Failed to initialize Quiklight (setOpen)");if(!sendPacket(makeBrightnessPacket(brightness_)))throw std::runtime_error("Failed to initialize Quiklight (brightness)");if(!sendPacket(setSection()))throw std::runtime_error("Failed to initialize Quiklight (section)");LedFrame black{};if(!sendFrame(black))throw std::runtime_error("Failed to initialize Quiklight (black frame)");}
bool QuiklightHid::sendPacket(const std::vector<uint8_t>&p){size_t off=0;while(off<p.size()){std::vector<uint8_t> report(report_len_,0);size_t chunk=std::min<size_t>(report_len_-1,p.size()-off);report[0]=0;memcpy(report.data()+1,p.data()+off,chunk);DWORD wr=0;if(!WriteFile(dev_,report.data(),(DWORD)report.size(),&wr,nullptr))return false;off+=chunk;}return true;}
std::vector<uint8_t> QuiklightHid::simple(uint8_t id,uint8_t action,const uint8_t*payload,size_t len)const{std::vector<uint8_t>o;o.reserve(2+1+1+1+len+1);o.push_back('R');o.push_back('B');o.push_back((uint8_t)(2+1+1+1+len+1));o.push_back(id);o.push_back(action);o.insert(o.end(),payload,payload+len);o.push_back(checksum8(o));return o;}
std::vector<uint8_t> QuiklightHid::setOpen()const{uint8_t p[]={0};return simple(next_msg_id_++,147,p,1);}
std::vector<uint8_t> QuiklightHid::makeBrightnessPacket(uint8_t v)const{uint8_t p[]={v};return simple(next_msg_id_++,135,p,1);}
std::vector<uint8_t> QuiklightHid::setSection()const{uint8_t p[]={1,85,85,85,63,64,0,0,0,254};return simple(next_msg_id_++,134,p,10);}
std::vector<uint8_t> QuiklightHid::sync(const LedFrame&f)const{const size_t plen=kLedCount*5,total=2+2+1+1+plen+1;std::vector<uint8_t>o;o.reserve(total);o.push_back('S');o.push_back('C');o.push_back((uint8_t)(total>>8));o.push_back((uint8_t)total);o.push_back(next_msg_id_++);o.push_back(128);for(size_t i=0;i<f.size();i++){uint8_t n=(uint8_t)(i+1);o.push_back(n);o.push_back(f[i].r);o.push_back(f[i].g);o.push_back(f[i].b);o.push_back(n);}o.push_back(checksum8(o));return o;}
}
