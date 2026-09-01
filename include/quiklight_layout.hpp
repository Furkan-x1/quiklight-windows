#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace quiklight {
struct ColorRgb { uint8_t r{}, g{}, b{}; };
constexpr size_t kTopCount=29, kRightCount=17, kLeftCount=17, kLedCount=kTopCount+kRightCount+kLeftCount;
using LedFrame=std::array<ColorRgb,kLedCount>;

enum class LightMode {
    Capture = 0,
    Static,
    Rainbow,
    ColorCycle,
    Breathing,
    Wave,
    Strobe,
    Fire,
    Ocean,
    Forest,
    Aurora,
    Twinkle,
    Police,
    Warm,
    Cool,
    White
};

inline const wchar_t* light_mode_name(LightMode m) {
    switch(m) {
        case LightMode::Capture: return L"Capture (screen)";
        case LightMode::Static: return L"Static / Manual color";
        case LightMode::Rainbow: return L"Rainbow";
        case LightMode::ColorCycle: return L"Color cycle";
        case LightMode::Breathing: return L"Breathing";
        case LightMode::Wave: return L"Wave";
        case LightMode::Strobe: return L"Strobe";
        case LightMode::Fire: return L"Fire";
        case LightMode::Ocean: return L"Ocean";
        case LightMode::Forest: return L"Forest";
        case LightMode::Aurora: return L"Aurora";
        case LightMode::Twinkle: return L"Twinkle";
        case LightMode::Police: return L"Police";
        case LightMode::Warm: return L"Warm white";
        case LightMode::Cool: return L"Cool white";
        case LightMode::White: return L"White";
    }
    return L"Capture (screen)";
}

struct MappingConfig { bool reverse_top=true, reverse_right=true, reverse_left=false, swap_left_right=false; int top_offset=0,right_offset=0,left_offset=0; };
struct ColorEnhancementConfig {
    float saturation_boost=1.60f,value_boost=1.14f,gamma=0.93f,min_saturation=0.18f,hue_shift_degrees=0.0f;
    float capture_depth_percent=2.0f;
};
struct EffectConfig {
    LightMode mode=LightMode::Capture;
    float speed=1.0f;
    ColorRgb manual{255,64,0};
};
inline ColorEnhancementConfig& cfg_store(){ static ColorEnhancementConfig c{}; return c; }
inline void set_color_enhancement_config(const ColorEnhancementConfig& c){cfg_store()=c;}
inline bool frames_equal(const LedFrame&a,const LedFrame&b){return std::memcmp(a.data(),b.data(),sizeof(LedFrame))==0;}
inline int pmod(int v,int m){int r=v%m;return r<0?r+m:r;}
inline float clamp01(float x){return std::max(0.0f,std::min(1.0f,x));}
inline uint8_t clamp_u8(float x){if(x<0)return 0;if(x>255)return 255;return (uint8_t)(x+0.5f);}
inline ColorRgb scale_color(ColorRgb c,float f){return {clamp_u8(c.r*f),clamp_u8(c.g*f),clamp_u8(c.b*f)};}
inline ColorRgb blend_color(const ColorRgb&p,const ColorRgb&n,float smoothing){float a=clamp01(1.0f-smoothing);return {clamp_u8(p.r+(n.r-p.r)*a),clamp_u8(p.g+(n.g-p.g)*a),clamp_u8(p.b+(n.b-p.b)*a)};}
inline LedFrame smooth_frame(const LedFrame&p,const LedFrame&n,float s){if(s<=0)return n;LedFrame o{};for(size_t i=0;i<o.size();i++)o[i]=blend_color(p[i],n[i],s);return o;}
inline ColorRgb hsv(float h,float s,float v){h=h-floorf(h);s=clamp01(s);v=clamp01(v);float hh=h*6.f;int i=(int)floorf(hh);float f=hh-i,p=v*(1-s),q=v*(1-s*f),t=v*(1-s*(1-f));switch(i%6){case 0:return {clamp_u8(v*255),clamp_u8(t*255),clamp_u8(p*255)};case 1:return {clamp_u8(q*255),clamp_u8(v*255),clamp_u8(p*255)};case 2:return {clamp_u8(p*255),clamp_u8(v*255),clamp_u8(t*255)};case 3:return {clamp_u8(p*255),clamp_u8(q*255),clamp_u8(v*255)};case 4:return {clamp_u8(t*255),clamp_u8(p*255),clamp_u8(v*255)};default:return {clamp_u8(v*255),clamp_u8(p*255),clamp_u8(q*255)};}}
inline ColorRgb enhance_color(ColorRgb in){float r=in.r/255.f,g=in.g/255.f,b=in.b/255.f;float mx=std::max({r,g,b}),mn=std::min({r,g,b}),d=mx-mn;float h=0,s=mx<=.0001f?0:d/mx,v=mx;if(d>.0001f){if(mx==r)h=fmodf((g-b)/d,6.f);else if(mx==g)h=(b-r)/d+2.f;else h=(r-g)/d+4.f;h/=6.f;if(h<0)h+=1.f;}auto&c=cfg_store();s=clamp01(std::max(s,c.min_saturation)*c.saturation_boost);v=clamp01(powf(v,c.gamma)*c.value_boost);h+=c.hue_shift_degrees/360.f;return hsv(h,s,v);}
inline ColorRgb average_region(uint32_t w,uint32_t h,const uint32_t*p,uint32_t x0,uint32_t y0,uint32_t x1,uint32_t y1){x0=std::min(x0,w);x1=std::min(x1,w);y0=std::min(y0,h);y1=std::min(y1,h);if(x0>=x1||y0>=y1)return{};uint64_t sr=0,sg=0,sb=0,n=0;for(uint32_t y=y0;y<y1;y++)for(uint32_t x=x0;x<x1;x++){uint32_t q=p[(size_t)y*w+x];sb+=(q>>0)&255;sg+=(q>>8)&255;sr+=(q>>16)&255;n++;}return enhance_color({(uint8_t)(sr/n),(uint8_t)(sg/n),(uint8_t)(sb/n)});}
inline void compute_colors(uint32_t w,uint32_t h,const uint32_t*p,LedFrame&out){if(!p||!w||!h){out.fill({});return;}float pct=clamp01(cfg_store().capture_depth_percent/100.f);uint32_t depth=std::max<uint32_t>(1,(uint32_t)(pct*std::min(w,h)));std::array<ColorRgb,kRightCount>right{};std::array<ColorRgb,kTopCount>top{};std::array<ColorRgb,kLeftCount>left{};for(size_t i=0;i<kRightCount;i++){uint32_t y0=(uint64_t)i*h/kRightCount,y1=(uint64_t)(i+1)*h/kRightCount;right[i]=average_region(w,h,p,w>depth?w-depth:0,y0,w,y1);}for(size_t i=0;i<kTopCount;i++){uint32_t x0=(uint64_t)i*w/kTopCount,x1=(uint64_t)(i+1)*w/kTopCount;top[i]=average_region(w,h,p,x0,0,x1,std::min(depth,h));}for(size_t i=0;i<kLeftCount;i++){uint32_t y0=(uint64_t)i*h/kLeftCount,y1=(uint64_t)(i+1)*h/kLeftCount;left[i]=average_region(w,h,p,0,y0,std::min(depth,w),y1);}for(size_t i=0;i<kRightCount;i++)out[i]=right[i];for(size_t i=0;i<kTopCount;i++)out[kRightCount+i]=top[i];for(size_t i=0;i<kLeftCount;i++)out[kRightCount+kTopCount+i]=left[i];}

template<size_t N> inline void reverse_segment(std::array<ColorRgb,N>&s){std::reverse(s.begin(),s.end());}
template<size_t N> inline std::array<ColorRgb,N> rotate_segment(const std::array<ColorRgb,N>&s,int off){std::array<ColorRgb,N>o{};for(size_t i=0;i<N;i++)o[i]=s[(size_t)pmod((int)i-off,(int)N)];return o;}
inline LedFrame remap_frame(const LedFrame&in,const MappingConfig&c){std::array<ColorRgb,kRightCount>r{};std::array<ColorRgb,kTopCount>t{};std::array<ColorRgb,kLeftCount>l{};for(size_t i=0;i<kRightCount;i++)r[i]=in[i];for(size_t i=0;i<kTopCount;i++)t[i]=in[kRightCount+i];for(size_t i=0;i<kLeftCount;i++)l[i]=in[kRightCount+kTopCount+i];if(c.reverse_right)reverse_segment(r);if(c.reverse_top)reverse_segment(t);if(c.reverse_left)reverse_segment(l);std::array<ColorRgb,kRightCount> rr=rotate_segment(r,c.right_offset);std::array<ColorRgb,kTopCount> tt=rotate_segment(t,c.top_offset);std::array<ColorRgb,kLeftCount> ll=rotate_segment(l,c.left_offset);LedFrame o{};if(!c.swap_left_right){for(size_t i=0;i<kRightCount;i++)o[i]=rr[i];for(size_t i=0;i<kTopCount;i++)o[kRightCount+i]=tt[i];for(size_t i=0;i<kLeftCount;i++)o[kRightCount+kTopCount+i]=ll[i];}else{for(size_t i=0;i<kLeftCount;i++)o[i]=ll[i];for(size_t i=0;i<kTopCount;i++)o[kLeftCount+i]=tt[i];for(size_t i=0;i<kRightCount;i++)o[kLeftCount+kTopCount+i]=rr[i];}return o;}

inline ColorRgb mix(ColorRgb a,ColorRgb b,float t){t=clamp01(t);return {clamp_u8(a.r+(b.r-a.r)*t),clamp_u8(a.g+(b.g-a.g)*t),clamp_u8(a.b+(b.b-a.b)*t)};}
inline LedFrame effect_frame(const EffectConfig& e,double time_s){
    LedFrame o{}; float speed=std::max(0.05f,std::min(e.speed,5.0f)); float t=(float)(time_s*speed);
    switch(e.mode){
        case LightMode::Static: o.fill(e.manual); break;
        case LightMode::White: o.fill({255,255,255}); break;
        case LightMode::Warm: o.fill({255,150,70}); break;
        case LightMode::Cool: o.fill({170,210,255}); break;
        case LightMode::Rainbow: for(size_t i=0;i<o.size();++i)o[i]=hsv((i/(float)o.size())+t*0.12f,1,1); break;
        case LightMode::ColorCycle: {ColorRgb c=hsv(t*0.08f,1,1); o.fill(c); break;}
        case LightMode::Breathing: {float a=0.5f+0.5f*sinf(t*3.1415926f*2.f); ColorRgb c=scale_color(e.manual,0.15f+0.85f*a);o.fill(c);break;}
        case LightMode::Wave: for(size_t i=0;i<o.size();++i){float a=0.5f+0.5f*sinf((i/(float)o.size())*6.28318f*3.f-t*5.f);o[i]=scale_color(e.manual,0.08f+0.92f*a);} break;
        case LightMode::Strobe: {bool on=fmodf(t*4.f,1.f)<0.25f;o.fill(on?e.manual:ColorRgb{});break;}
        case LightMode::Police: {bool red=fmodf(t*2.f,2.f)<1.f;for(size_t i=0;i<o.size();++i){bool left=i<o.size()/2;o[i]=(red==left)?ColorRgb{255,0,0}:ColorRgb{0,0,255};}break;}
        case LightMode::Fire: for(size_t i=0;i<o.size();++i){float n=0.55f+0.45f*sinf(i*2.3f+t*8.f);o[i]={255,clamp_u8(45+95*n),clamp_u8(5+35*n)};} break;
        case LightMode::Ocean: for(size_t i=0;i<o.size();++i){float n=0.5f+0.5f*sinf(i*1.7f-t*2.3f);o[i]=mix({0,35,100},{0,210,255},n);} break;
        case LightMode::Forest: for(size_t i=0;i<o.size();++i){float n=0.5f+0.5f*sinf(i*1.1f+t*1.5f);o[i]=mix({5,35,5},{40,200,45},n);} break;
        case LightMode::Aurora: for(size_t i=0;i<o.size();++i){float h=0.36f+0.12f*sinf(i*0.7f+t*0.8f);float v=0.55f+0.45f*sinf(i*0.45f-t*1.7f);o[i]=hsv(h,0.8f,0.35f+0.65f*v);} break;
        case LightMode::Twinkle: for(size_t i=0;i<o.size();++i){float n=0.5f+0.5f*sinf(i*12.3f+t*(6.f+(i%5))); if(n>0.84f)o[i]=e.manual;else o[i]=scale_color(e.manual,0.05f); } break;
        case LightMode::Capture: break;
    }
    return o;
}
}
