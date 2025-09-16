#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

template <typename T>
constexpr T clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
using u8=uint8_t; using u16=uint16_t; using u32=uint32_t;

namespace chip8c {
  constexpr int kDisplayWidth=64, kDisplayHeight=32, kPixelCount=kDisplayWidth*kDisplayHeight;
  constexpr int kMemSize=4096; constexpr u16 kEntryAddr=0x200;
  constexpr int kRegCount=16, kStackDepth=16, kTimerHz=60, kKeyCount=16, kGlyphBytes=5;
  constexpr std::array<u8,16*kGlyphBytes> kFontSprites={
    0xF0,0x90,0x90,0x90,0xF0, 0x20,0x60,0x20,0x20,0x70, 0xF0,0x10,0xF0,0x80,0xF0, 0xF0,0x10,0xF0,0x10,0xF0,
    0x90,0x90,0xF0,0x10,0x10, 0xF0,0x80,0xF0,0x10,0xF0, 0xF0,0x80,0xF0,0x90,0xF0, 0xF0,0x10,0x20,0x40,0x40,
    0xF0,0x90,0xF0,0x90,0xF0, 0xF0,0x90,0xF0,0x10,0xF0, 0xF0,0x90,0xF0,0x90,0x90, 0xE0,0x90,0xE0,0x90,0xE0,
    0xF0,0x80,0x80,0x80,0xF0, 0xE0,0x90,0x90,0x90,0xE0, 0xF0,0x80,0xF0,0x80,0xF0, 0xF0,0x80,0xF0,0x80,0x80
  };
}

class Display {
 public:
  struct Config{ std::string title="Chip8 VM"; int w=chip8c::kDisplayWidth,h=chip8c::kDisplayHeight,sx=12,sy=12; bool vsync=true; };
  explicit Display(const Config& c):cfg(c) {}
  bool init(){
    if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER)!=0){ std::cerr<<"SDL_Init: "<<SDL_GetError()<<"\n"; return false; }
    int img=IMG_INIT_PNG; if((IMG_Init(img)&img)==0){ /*optional*/ }
    win=SDL_CreateWindow(cfg.title.c_str(),SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,cfg.w*cfg.sx,cfg.h*cfg.sy,SDL_WINDOW_SHOWN);
    if(!win){ std::cerr<<"SDL_CreateWindow: "<<SDL_GetError()<<"\n"; return false; }
    rend=SDL_CreateRenderer(win,-1,SDL_RENDERER_ACCELERATED|(cfg.vsync?SDL_RENDERER_PRESENTVSYNC:0));
    if(!rend){ std::cerr<<"SDL_CreateRenderer: "<<SDL_GetError()<<"\n"; return false; }
    SDL_RenderSetScale(rend,(float)cfg.sx,(float)cfg.sy);
    clear(); present(); return true;
  }
  ~Display(){ if(rend)SDL_DestroyRenderer(rend); if(win)SDL_DestroyWindow(win); IMG_Quit(); SDL_Quit(); }
  void clear(){ SDL_SetRenderDrawColor(rend,0,0,0,255); SDL_RenderClear(rend); }
  void pixel(int x,int y,bool on){ if(on){ SDL_SetRenderDrawColor(rend,255,255,255,255); SDL_RenderDrawPoint(rend,x,y);} }
  void present(){ SDL_RenderPresent(rend); }
 private:
  Config cfg; SDL_Window* win=nullptr; SDL_Renderer* rend=nullptr;
};

class Keypad {
 public:
  void reset(){ keys.fill(false); }
  void set(u8 i,bool v){ if(i<keys.size()) keys[i]=v; }
  bool down(u8 i)const{ return i<keys.size()?keys[i]:false; }
  static std::optional<u8> map(SDL_Keycode k){
    switch(k){
      case SDLK_1:return 0x1; case SDLK_2:return 0x2; case SDLK_3:return 0x3; case SDLK_4:return 0xC;
      case SDLK_q:return 0x4; case SDLK_w:return 0x5; case SDLK_e:return 0x6; case SDLK_r:return 0xD;
      case SDLK_a:return 0x7; case SDLK_s:return 0x8; case SDLK_d:return 0x9; case SDLK_f:return 0xE;
      case SDLK_z:return 0xA; case SDLK_x:return 0x0; case SDLK_c:return 0xB; case SDLK_v:return 0xF;
      default:return std::nullopt;
    }
  }
 private: std::array<bool,chip8c::kKeyCount> keys{};
};

class Chip8VM {
 public:
  struct FB{ std::array<u8,chip8c::kPixelCount> pix{}; void clear(){ pix.fill(0);} u8& at(int x,int y){return pix[y*chip8c::kDisplayWidth+x];} };
  struct State{
    std::array<u8,chip8c::kMemSize> mem{}; std::array<u8,chip8c::kRegCount> v{}; u16 I=0, pc=chip8c::kEntryAddr;
    std::array<u16,chip8c::kStackDepth> stack{}; u8 sp=0, DT=0, ST=0;
  };
  Chip8VM(){ reset(); }
  void reset(){
    st=State{}; fb.clear();
    constexpr u16 fontAddr=0x050; for(size_t i=0;i<chip8c::kFontSprites.size();++i) st.mem[fontAddr+i]=chip8c::kFontSprites[i];
  }
  bool load(const std::string& path){
    std::ifstream f(path, std::ios::binary); if(!f){ std::cerr<<"ROM open fail: "<<path<<"\n"; return false; }
    std::vector<u8> bytes((std::istreambuf_iterator<char>(f)),{});
    if(chip8c::kEntryAddr+bytes.size()>st.mem.size()){ std::cerr<<"ROM too big\n"; return false; }
    for(size_t i=0;i<bytes.size();++i) st.mem[chip8c::kEntryAddr+i]=bytes[i];
    st.pc=chip8c::kEntryAddr; return true;
  }
  bool step(Keypad& k){
    u16 op=(st.mem[st.pc]<<8)|st.mem[st.pc+1]; st.pc+=2;
    u16 nnn=op&0x0FFF; u8 nn=op&0xFF, n=op&0xF, x=(op>>8)&0xF, y=(op>>4)&0xF; bool draw=false;
    switch(op&0xF000){
      case 0x0000: if(nn==0xE0){ fb.clear(); draw=true; } else if(nn==0xEE){ if(st.sp){ st.pc=st.stack[--st.sp]; } } break;
      case 0x1000: st.pc=nnn; break;
      case 0x2000: if(st.sp<chip8c::kStackDepth){ st.stack[st.sp++]=st.pc; st.pc=nnn; } break;
      case 0x3000: if(st.v[x]==nn) st.pc+=2; break;
      case 0x4000: if(st.v[x]!=nn) st.pc+=2; break;
      case 0x5000: if((op&0xF)==0 && st.v[x]==st.v[y]) st.pc+=2; break;
      case 0x6000: st.v[x]=nn; break;
      case 0x7000: st.v[x]=u8(st.v[x]+nn); break;
      case 0x8000:{
        switch(op&0xF){
          case 0x0: st.v[x]=st.v[y]; break;
          case 0x1: st.v[x]|=st.v[y]; break;
          case 0x2: st.v[x]&=st.v[y]; break;
          case 0x3: st.v[x]^=st.v[y]; break;
          case 0x4:{ u16 s=st.v[x]+st.v[y]; st.v[0xF]=s>0xFF; st.v[x]=u8(s); }break;
          case 0x5:{ st.v[0xF]=st.v[x]>st.v[y]; st.v[x]-=st.v[y]; }break;
          case 0x6:{ st.v[0xF]=st.v[x]&1; st.v[x]>>=1; }break;
          case 0x7:{ st.v[0xF]=st.v[y]>st.v[x]; st.v[x]=u8(st.v[y]-st.v[x]); }break;
          case 0xE:{ st.v[0xF]=(st.v[x]&0x80)>>7; st.v[x]<<=1; }break;
        }} break;
      case 0x9000: if((op&0xF)==0 && st.v[x]!=st.v[y]) st.pc+=2; break;
      case 0xA000: st.I=nnn; break;
      case 0xB000: st.pc=nnn+st.v[0]; break;
      case 0xC000: st.v[x]=u8((rand()&0xFF)&nn); break;
      case 0xD000:{
        u8 px=st.v[x]%chip8c::kDisplayWidth, py=st.v[y]%chip8c::kDisplayHeight; st.v[0xF]=0;
        for(u8 row=0; row<n; ++row){ u8 sprite=st.mem[st.I+row];
          for(u8 col=0; col<8; ++col){ if(sprite&(0x80>>col)){
              int sx=(px+col)%chip8c::kDisplayWidth, sy=(py+row)%chip8c::kDisplayHeight; u8& p=fb.at(sx,sy);
              if(p==1) st.v[0xF]=1; p^=1;
          }}
        } draw=true; } break;
      case 0xE000:
        if(nn==0x9E){ if(k.down(st.v[x])) st.pc+=2; }
        else if(nn==0xA1){ if(!k.down(st.v[x])) st.pc+=2; }
        break;
      case 0xF000:
        switch(nn){
          case 0x07: st.v[x]=st.DT; break;
          case 0x0A: waitKey=true; waitReg=x; break;
          case 0x15: st.DT=st.v[x]; break;
          case 0x18: st.ST=st.v[x]; break;
          case 0x1E: st.I=u16(st.I+st.v[x]); break;
          case 0x29: st.I=u16(0x050+(st.v[x]&0xF)*chip8c::kGlyphBytes); break;
          case 0x33:{ u8 v=st.v[x]; st.mem[st.I]=v/100; st.mem[st.I+1]=(v/10)%10; st.mem[st.I+2]=v%10; }break;
          case 0x55: for(u8 i=0;i<=x;++i) st.mem[st.I+i]=st.v[i]; break;
          case 0x65: for(u8 i=0;i<=x;++i) st.v[i]=st.mem[st.I+i]; break;
        } break;
    }
    return draw;
  }
  void timerTick(){ if(st.DT>0) --st.DT; if(st.ST>0){ --st.ST; if(st.ST>0) std::cout<<"BEEP\n"; } }
  void feedKey(u8 k){ if(waitKey){ st.v[waitReg]=k; waitKey=false; } }
  const FB& framebuffer()const{ return fb; }
 private:
  State st{}; FB fb{}; bool waitKey=false; u8 waitReg=0;
};

class App {
 public:
  struct Opt{ std::string rom; int sx=12,sy=12, timerHz=chip8c::kTimerHz, cycles=10; bool vsync=true; };
  explicit App(const Opt& o):opt(o),disp(Display::Config{ "Chip8 VM"+o.rom, chip8c::kDisplayWidth, chip8c::kDisplayHeight, o.sx, o.sy, o.vsync }){}
  bool run(){
    if(!disp.init()) return false;
    if(!vm.load(opt.rom)) return false;
    bool quit=false; u32 last=SDL_GetTicks(), dt=1000/opt.timerHz;
    while(!quit){
      SDL_Event ev; while(SDL_PollEvent(&ev)){
        if(ev.type==SDL_QUIT) quit=true;
        else if(ev.type==SDL_KEYDOWN){ if(ev.key.keysym.sym==SDLK_ESCAPE) quit=true; auto m=Keypad::map(ev.key.keysym.sym); if(m){ keys.set(*m,true); vm.feedKey(*m);} }
        else if(ev.type==SDL_KEYUP){ auto m=Keypad::map(ev.key.keysym.sym); if(m) keys.set(*m,false); }
      }
      bool draw=false; for(int i=0;i<opt.cycles;++i) draw|=vm.step(keys);
      u32 now=SDL_GetTicks(); if(now-last>=dt){ vm.timerTick(); last=now; }
      if(draw){ disp.clear(); const auto& fb=vm.framebuffer(); for(int y=0;y<chip8c::kDisplayHeight;++y) for(int x=0;x<chip8c::kDisplayWidth;++x) disp.pixel(x,y, fb.pix[y*chip8c::kDisplayWidth+x]!=0 ); disp.present(); }
      SDL_Delay(1);
    } return true;
  }
 private: Opt opt; Display disp; Keypad keys; Chip8VM vm;
};

static void usage(const char* a){ std::cout<<"Usage: "<<a<<" <rom_path> [scale]\n"; }

int main(int argc,char** argv){
  if(argc<2){ usage(argv[0]); return 1; }
  std::string rom=argv[1]; int scale= (argc>=3? clamp(std::atoi(argv[2]),1,64):12);
  App::Opt o; o.rom=rom; o.sx=scale; o.sy=scale; o.timerHz=chip8c::kTimerHz; o.cycles=10; o.vsync=true;
  App app(o); if(!app.run()){ std::cerr<<"Run failed.\n"; return 2; } return 0;
}
