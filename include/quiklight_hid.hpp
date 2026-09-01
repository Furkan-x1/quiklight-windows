#pragma once
#include "quiklight_layout.hpp"
#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <string>
#include <vector>

namespace quiklight {
struct HidDeviceInfo { std::wstring path; uint16_t vendor_id{},product_id{}; std::wstring manufacturer,product,serial; DWORD output_report_len=65; };
class QuiklightHid {
public:
 QuiklightHid(uint16_t vid,uint16_t pid,uint8_t brightness,const std::wstring&forced_path=L""); ~QuiklightHid();
 QuiklightHid(const QuiklightHid&)=delete; QuiklightHid& operator=(const QuiklightHid&)=delete;
 static std::vector<HidDeviceInfo> listDevices(); bool sendFrame(const LedFrame&); bool setBrightness(uint8_t value);
private:
 void open(); void initialize(); bool sendPacket(const std::vector<uint8_t>&); std::vector<uint8_t> simple(uint8_t id,uint8_t action,const uint8_t*payload,size_t len)const; std::vector<uint8_t> setOpen()const; std::vector<uint8_t> makeBrightnessPacket(uint8_t)const; std::vector<uint8_t> setSection()const; std::vector<uint8_t> sync(const LedFrame&)const;
 HANDLE dev_=INVALID_HANDLE_VALUE; uint16_t vid_=0x1A86,pid_=0xFE07;uint8_t brightness_=255;std::wstring forced_path_;DWORD report_len_=65; mutable uint8_t next_msg_id_=1;
};
}
