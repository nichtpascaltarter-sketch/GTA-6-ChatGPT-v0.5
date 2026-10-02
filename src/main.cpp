#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <xinput.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <stdexcept>
#include "game.h"
#include "renderer.h"
#include "audio.h"
#include "audio_scene.h"
#include "ui.h"
#include "map_ui.h"
#include "world_streamer.h"
#include "streaming_probe.h"
#include "lod_probe.h"
#include "timing_probe.h"
#include "resident_scenes.h"
#include "market_scenes.h"

namespace {
using namespace mc;
struct Options {bool smoke=false,warp=false;unsigned frames=120;std::string screenshot,scene="city";};
std::string utf8(const std::wstring& s) {
    if(s.empty())return {};
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
    std::string out(size_t(n),'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);return out;
}
Options parseOptions() {
    Options options;int count=0;LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&count);
    for(int i=1;args&&i<count;++i){std::wstring a=args[i];if(a==L"--smoke")options.smoke=true;else if(a==L"--warp")options.warp=true;
        else if(a==L"--frames"&&i+1<count)options.frames=unsigned(std::clamp(_wtoi(args[++i]),1,10000));
        else if(a==L"--screenshot"&&i+1<count)options.screenshot=utf8(args[++i]);
        else if(a==L"--scene"&&i+1<count)options.scene=utf8(args[++i]);}
    if(args)LocalFree(args);return options;
}
std::filesystem::path dataDirectory() {
    PWSTR path=nullptr;std::filesystem::path result;
    if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_CREATE,nullptr,&path))){result=path;CoTaskMemFree(path);}
    else {wchar_t temp[MAX_PATH]{};GetTempPathW(MAX_PATH,temp);result=temp;}
    result/=L"MeridianCoast";std::error_code ec;std::filesystem::create_directories(result,ec);return result;
}
struct Settings {uint32_t magic=0x4d435331,version=1,fullscreen=0,vsync=1,rayTracing=1;float volume=.65f,exposure=1;};
bool valid(const Settings& s) {return s.magic==0x4d435331&&s.version==1&&s.fullscreen<2&&s.vsync<2&&s.rayTracing<2&&std::isfinite(s.volume)&&s.volume>=0&&s.volume<=1&&std::isfinite(s.exposure)&&s.exposure>=.5f&&s.exposure<=1.8f;}
Settings readSettings(const std::filesystem::path& path) {Settings s,t;std::ifstream f(path,std::ios::binary);if(f.read(reinterpret_cast<char*>(&t),sizeof(t))&&f.peek()==std::char_traits<char>::eof()&&valid(t))s=t;return s;}
void saveSettings(const std::filesystem::path& path,const Settings& s) {
    auto temporary=path;temporary+=L".tmp";std::ofstream f(temporary,std::ios::binary|std::ios::trunc);f.write(reinterpret_cast<const char*>(&s),sizeof(s));f.flush();bool ok=bool(f);f.close();
    if(ok)MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
struct App {
    HWND window=nullptr;bool running=true,active=true,resized=false,focusedMouse=false;
    unsigned width=1600,height=900;std::array<bool,256> pressed{};float mouseX=0,mouseY=0,wheel=0;
    bool title=true,menu=true,showSettings=false,diagnostics=false,mapOpen=false,mapClick=false;int selected=0;
    RECT windowed{};DWORD oldStyle=0;bool borderless=false;WORD padButtons=0;
    ~App(){if(focusedMouse){ClipCursor(nullptr);ReleaseCapture();ShowCursor(TRUE);}if(window&&IsWindow(window))DestroyWindow(window);}
};
bool pumpDistant(WorldStreamer& streamer,Game& game,App& app,uint64_t epoch,
                 double budgetMilliseconds,bool requireComplete,std::string& error) {
    const auto began=std::chrono::steady_clock::now();
    do {
        MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){
            if(msg.message==WM_QUIT)app.running=false;
            TranslateMessage(&msg);DispatchMessageW(&msg);
        }
        if(!app.running)return true;
        if(!streamer.update(game.world,game.player,epoch,error))return false;
        const auto stats=streamer.stats();
        if(!stats.pendingChunks&&!stats.distantPending&&!stats.queued&&!stats.inFlight&&!stats.completed)return true;
        SwitchToThread();
    }while(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count()<budgetMilliseconds);
    if(requireComplete){error="Distant world generation did not settle within its startup budget.";return false;}
    return true;
}
void captureMouse(App& app,bool capture) {
    capture=capture&&app.active;
    if(capture==app.focusedMouse)return;app.focusedMouse=capture;
    if(capture){RECT rect;GetClientRect(app.window,&rect);POINT a{rect.left,rect.top},b{rect.right,rect.bottom};ClientToScreen(app.window,&a);ClientToScreen(app.window,&b);rect={a.x,a.y,b.x,b.y};ClipCursor(&rect);SetCapture(app.window);ShowCursor(FALSE);}
    else {ClipCursor(nullptr);ReleaseCapture();ShowCursor(TRUE);}
    app.mouseX=app.mouseY=0;
}
void fullscreen(App& app,bool enabled) {
    if(enabled==app.borderless)return;captureMouse(app,false);
    if(enabled){app.oldStyle=DWORD(GetWindowLongPtrW(app.window,GWL_STYLE));GetWindowRect(app.window,&app.windowed);MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(app.window,MONITOR_DEFAULTTONEAREST),&mi);
        SetWindowLongPtrW(app.window,GWL_STYLE,app.oldStyle&~WS_OVERLAPPEDWINDOW);SetWindowPos(app.window,HWND_TOP,mi.rcMonitor.left,mi.rcMonitor.top,mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top,SWP_FRAMECHANGED|SWP_NOOWNERZORDER);}
    else {SetWindowLongPtrW(app.window,GWL_STYLE,app.oldStyle);SetWindowPos(app.window,nullptr,app.windowed.left,app.windowed.top,app.windowed.right-app.windowed.left,app.windowed.bottom-app.windowed.top,SWP_FRAMECHANGED|SWP_NOZORDER|SWP_NOOWNERZORDER);}
    app.borderless=enabled;
}
LRESULT CALLBACK windowProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    App* app=reinterpret_cast<App*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(message==WM_NCCREATE){app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);app->window=hwnd;SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    if(!app)return DefWindowProcW(hwnd,message,wp,lp);
    switch(message){
    case WM_CLOSE:app->running=false;return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    case WM_SIZE:if(wp!=SIZE_MINIMIZED){app->width=LOWORD(lp);app->height=HIWORD(lp);app->resized=true;}return 0;
    case WM_GETMINMAXINFO:{auto* limits=reinterpret_cast<MINMAXINFO*>(lp);limits->ptMinTrackSize={960,580};return 0;}
    case WM_DPICHANGED:{if(!app->borderless){const auto* r=reinterpret_cast<const RECT*>(lp);SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);}return 0;}
    case WM_ACTIVATEAPP:app->active=wp!=0;if(!app->active)captureMouse(*app,false);return 0;
    case WM_KILLFOCUS:app->pressed.fill(false);app->mouseX=app->mouseY=app->wheel=0;app->mapClick=false;captureMouse(*app,false);return 0;
    case WM_MOUSEWHEEL:app->wheel+=float(GET_WHEEL_DELTA_WPARAM(wp))/WHEEL_DELTA;return 0;
    case WM_LBUTTONDOWN:app->mapClick=true;return 0;
    case WM_KEYDOWN:case WM_SYSKEYDOWN:if(wp<256&&!(lp&(1ll<<30)))app->pressed[size_t(wp)]=true;if(message==WM_SYSKEYDOWN)return DefWindowProcW(hwnd,message,wp,lp);return 0;
    case WM_INPUT:if(app->focusedMouse){RAWINPUT input{};UINT size=sizeof(input);if(GetRawInputData(reinterpret_cast<HRAWINPUT>(lp),RID_INPUT,&input,&size,sizeof(RAWINPUTHEADER))==sizeof(RAWINPUT)&&input.header.dwType==RIM_TYPEMOUSE&&!(input.data.mouse.usFlags&MOUSE_MOVE_ABSOLUTE)){app->mouseX+=float(input.data.mouse.lLastX);app->mouseY+=float(input.data.mouse.lLastY);}}return DefWindowProcW(hwnd,message,wp,lp);
    case WM_SETCURSOR:if(app->focusedMouse){SetCursor(nullptr);return TRUE;}break;
    default:break;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
float stick(SHORT v,SHORT deadzone) {int n=int(v);if(std::abs(n)<deadzone)return 0;return clamp((float(std::abs(n))-deadzone)/(32767.f-deadzone),0,1)*(n<0?-1.f:1.f);}
bool key(int k){return (GetAsyncKeyState(k)&0x8000)!=0;}
Input readInput(App& app,float dt,XINPUT_STATE& state,WORD& newlyPressed) {
    Input input;bool pad=XInputGetState(0,&state)==ERROR_SUCCESS;WORD buttons=pad?state.Gamepad.wButtons:0;newlyPressed=buttons&~app.padButtons;app.padButtons=buttons;
    if(!app.active)return input;
    input.moveX=float(key('D'))-float(key('A'));input.moveY=float(key('W'))-float(key('S'));
    input.lookX=app.mouseX*.0024f;input.lookY=app.mouseY*.0024f;app.mouseX=app.mouseY=0;
    input.sprint=key(VK_SHIFT);input.brake=key(VK_SPACE);input.jump=app.pressed[VK_SPACE];input.fire=key(VK_LBUTTON);input.aim=key(VK_RBUTTON);
    input.interact=app.pressed['E'];input.reload=app.pressed['R'];input.mission=app.pressed['M'];input.radio=app.pressed['Q'];
    if(pad){input.moveX=clamp(input.moveX+stick(state.Gamepad.sThumbLX,XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE),-1,1);input.moveY=clamp(input.moveY+stick(state.Gamepad.sThumbLY,XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE),-1,1);
        input.lookX+=stick(state.Gamepad.sThumbRX,XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE)*dt*2.4f;input.lookY-=stick(state.Gamepad.sThumbRY,XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE)*dt*2.0f;
        input.sprint|=(buttons&XINPUT_GAMEPAD_A)!=0;input.brake|=(buttons&XINPUT_GAMEPAD_B)!=0;input.jump|=(newlyPressed&XINPUT_GAMEPAD_LEFT_SHOULDER)!=0;
        input.fire|=state.Gamepad.bRightTrigger>40;input.aim|=state.Gamepad.bLeftTrigger>40;input.interact|=(newlyPressed&XINPUT_GAMEPAD_Y)!=0;
        input.reload|=(newlyPressed&XINPUT_GAMEPAD_X)!=0;input.mission|=(newlyPressed&XINPUT_GAMEPAD_DPAD_UP)!=0;input.radio|=(newlyPressed&XINPUT_GAMEPAD_DPAD_RIGHT)!=0;
    }
    return input;
}
void drawMenu(Ui& ui,const App& app,const Settings& settings,const Renderer& renderer) {
    float s=ui.scale;const Vec3 teal{.31f,.88f,.77f},muted{.58f,.68f,.71f};
    ui.rect(0,0,ui.width,ui.height,{.012f,.028f,.045f},.66f);
    float panel=std::min(ui.width*.63f,760*s);ui.rect(0,0,panel,ui.height,{.016f,.035f,.052f},.94f);
    ui.rect(56*s,68*s,44*s,4*s,teal);ui.text(56*s,92*s,"A CITY BETWEEN TIDES",1.6f*s,teal);
    ui.text(52*s,144*s,"MERIDIAN",7*s);ui.text(56*s,208*s,"COAST",7*s);
    ui.text(56*s,292*s,app.showSettings?"SETTINGS":app.title?"EVERY ROAD LEAVES A TRACE":"PAUSED",1.6f*s,muted);
    const char* normal[]={app.title?"ENTER THE CITY":"RESUME","SETTINGS","SAVE GAME","QUIT"};
    char volume[64],exposure[64];std::snprintf(volume,sizeof(volume),"MASTER VOLUME   %d",int(settings.volume*100+.5f));std::snprintf(exposure,sizeof(exposure),"EXPOSURE        %.1f",settings.exposure);
    const char* options[]={settings.fullscreen?"FULLSCREEN      ON":"FULLSCREEN      OFF",settings.vsync?"VSYNC           ON":"VSYNC           OFF",settings.rayTracing?"RAY TRACING     ON":"RAY TRACING     OFF",volume,exposure,"BACK"};
    const char** items=app.showSettings?options:normal;int count=app.showSettings?6:4;
    for(int i=0;i<count;++i){float y=(344+i*48)*s;if(i==app.selected){ui.rect(46*s,y-12*s,panel-98*s,42*s,teal,.13f);ui.rect(46*s,y-12*s,3*s,42*s,teal);}ui.text(66*s,y,items[i],2*s,i==app.selected?Vec3{.94f,.98f,.95f}:muted);}
    if(app.showSettings&&!renderer.rayTracingAvailable())ui.text(56*s,658*s,"DXR UNAVAILABLE ON THIS ADAPTER",1.3f*s,{.98f,.68f,.38f});
    if(!app.showSettings){ui.text(56*s,578*s,"WASD MOVE / DRIVE   M JOB / TRIAL / SERVICE",1.35f*s,muted);ui.text(56*s,603*s,"E ENTER VEHICLE      MOUSE LOOK / AIM",1.35f*s,muted);ui.text(56*s,628*s,"R RELOAD  Q RADIO    TAB WORLD MAP",1.35f*s,muted);ui.text(56*s,653*s,"F5 SAVE  F9 LOAD     F11 FULLSCREEN",1.35f*s,muted);}
    ui.text(56*s,ui.height-63*s,"ARROWS / DPAD SELECT   ENTER / A CONFIRM",1.25f*s,muted);
    ui.text(56*s,ui.height-37*s,"ORIGINAL WORLD. ORIGINAL SOUND.",1.25f*s,teal);
    if(ui.width>1100*s){float x=panel+45*s;ui.text(x,ui.height-142*s,"PORT SOLACE",2.2f*s);ui.text(x,ui.height-112*s,"THE COAST IS CALLING.",1.5f*s,teal);ui.wrapped(x,ui.height-83*s,"A stolen tide chart. A missing courier. One last job before the storm.",1.35f*s,ui.width-x-50*s,muted);}
}
int execute(HINSTANCE instance,const Options& options) {
    App app;if(options.smoke){app.width=960;app.height=540;app.menu=app.title=false;app.diagnostics=options.scene=="trial-run";}
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto directory=dataDirectory();auto settingsPath=directory/L"settings.bin";auto savePath=directory/L"save.bin";
    auto u8=savePath.u8string();std::string saveFile(reinterpret_cast<const char*>(u8.data()),u8.size());Settings settings=options.smoke?Settings{}:readSettings(settingsPath);
    WNDCLASSEXW cls{};cls.cbSize=sizeof(cls);cls.style=CS_HREDRAW|CS_VREDRAW;cls.lpfnWndProc=windowProc;cls.hInstance=instance;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpszClassName=L"MeridianCoastWindow";
    if(!RegisterClassExW(&cls))return 1;RECT rect{0,0,LONG(app.width),LONG(app.height)};AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);
    if(!CreateWindowExW(0,cls.lpszClassName,L"Meridian Coast",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,&app))return 2;
    std::ofstream log(directory/L"session.log",std::ios::trunc);log<<"Meridian Coast session\n";
    int result=0;
    {
        Renderer renderer;std::string error;
        if(!renderer.initialize(app.window,app.width,app.height,error,options.warp)){log<<error<<'\n';std::fprintf(stderr,"%s\n",error.c_str());if(!options.smoke)MessageBoxA(app.window,error.c_str(),"Meridian Coast - graphics initialization",MB_OK|MB_ICONERROR);DestroyWindow(app.window);return 3;}
        log<<"Adapter: "<<renderer.adapterName()<<"\nDXR available: "<<renderer.rayTracingAvailable()<<'\n';log.flush();
        Game game;game.initialize();ResidentCapture residentCapture;MarketCapture marketCapture;if(!options.smoke&&std::filesystem::exists(savePath))game.load(saveFile);
        if(options.smoke){
            if(options.scene.rfind("market-",0)==0){
                if(!marketCapture.prepare(game,options.scene,log,error)){log<<error<<'\n';throw std::runtime_error(error);}
            }
            else if(options.scene.rfind("residents-",0)==0){
                auto pump=[&](){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT)app.running=false;TranslateMessage(&message);DispatchMessageW(&message);}return app.running;};
                if(!residentCapture.prepare(game,options.scene,log,error,pump)){log<<error<<'\n';throw std::runtime_error(error);}
            }
            else if(options.scene=="trial"||options.scene=="trial-run"||options.scene=="trial-map"){
                game.player=Game::harborSplitContact()+Vec3{0,0,-5};game.player.y=game.world.height(game.player.x,game.player.z);
                game.yaw=0;game.pitch=.14f;game.world.stream(game.player);
                if(options.scene=="trial-run"){
                    Input accept;accept.mission=true;game.update(accept,1.f/60);
                    if(game.harborSplit.phase!=TrialPhase::Boarding)throw std::runtime_error("Trial capture could not accept Harbor Split.");
                    const Vec3 loan=game.objectiveTarget();
                    for(size_t index=0;index<game.vehicles.size();++index)if(game.vehicles[index].kind==VehicleKind::Motorcycle&&length(game.vehicles[index].position-loan)<.01f){
                        auto& bike=game.vehicles[index];game.occupied=int(index);bike.position=Game::harborSplitStart();
                        bike.position.y=game.world.height(bike.position.x,bike.position.z);bike.yaw=0;bike.speed=0;bike.velocity={};game.player=bike.position;break;
                    }
                    for(unsigned step=0;step<240&&game.harborSplit.phase!=TrialPhase::Running;++step)game.update({},1.f/60);
                    if(game.harborSplit.phase!=TrialPhase::Running)throw std::runtime_error("Trial capture could not complete the standing start.");
                    Input ride;ride.moveY=.65f;for(unsigned step=0;step<60;++step)game.update(ride,1.f/60);
                    if(game.harborSplit.phase!=TrialPhase::Running||game.occupied<0)throw std::runtime_error("Trial capture lost its active rider.");
                }
                log<<"Trial capture: phase="<<int(game.harborSplit.phase)<<"; checkpoint="<<game.harborSplit.checkpoint
                   <<"; story="<<game.activeMission<<"; completedStory="<<game.completedMissions
                   <<"; occupied="<<game.occupied<<"; elapsed="<<game.harborSplit.elapsed<<"; penalty="<<game.harborSplit.penalty<<'\n';
            }
            else if(options.scene.rfind("workshop",0)==0){
                const auto& site=World::garageSite();game.player=site.marker;game.yaw=Pi;game.pitch=.12f;
                if(options.scene=="workshop-day"||options.scene=="workshop-night")game.player=site.vehicleStop+Vec3{-3,0,5};
                else if(options.scene=="workshop-office"){game.player=site.counter;game.health=65;}
                else if(options.scene=="workshop-door")game.player=site.pedestrianDoor+Vec3{0,0,-2};
                else if(options.scene=="workshop-service"){
                    game.occupied=0;auto& vehicle=game.vehicles[0];
                    vehicle.kind=VehicleKind::Car;vehicle.position=site.vehicleStop;vehicle.yaw=site.vehicleHeading;
                    vehicle.speed=0;vehicle.velocity={};vehicle.throttle=0;vehicle.parked=false;vehicle.health=55;
                    game.player=vehicle.position;game.yaw=vehicle.yaw;
                    const int before=game.money;Input service;service.mission=true;game.update(service,1.f/60);
                    if(vehicle.health!=100||game.money!=before-75||game.activeMission!=-1)
                        throw std::runtime_error("Workshop capture failed its vehicle service transaction.");
                }
                game.player.y=game.world.height(game.player.x,game.player.z);game.world.stream(game.player);
                if(game.world.blocked(game.player,.35f))throw std::runtime_error("Workshop capture position is obstructed.");
                log<<"Workshop capture: scene="<<options.scene<<"; position="<<game.player.x<<','<<game.player.y<<','<<game.player.z
                   <<"; occupied="<<game.occupied<<"; money="<<game.money<<"; health="<<game.health<<'\n';
            }
            else if(options.scene=="coast"){game.player={2510,0,260};game.yaw=1.4f;game.pitch=.10f;}
            else if(options.scene=="wetland"){game.player={1024,0,-2560};game.yaw=.5f;game.pitch=.13f;}
            else if(options.scene=="suburbs"){game.player={-2048,0,128};game.yaw=.8f;game.pitch=.12f;}
            else if(options.scene=="rural"){game.player={-4096,0,1536};game.yaw=.4f;game.pitch=.12f;}
            else if(options.scene=="drive"){game.occupied=0;game.player=game.vehicles[0].position;game.vehicles[0].parked=false;}
            else if(options.scene=="passenger-car"||options.scene=="passenger-bike"||options.scene=="passenger-boat"||options.scene=="passenger-plane"){
                const VehicleKind kind=options.scene=="passenger-car"?VehicleKind::Car:options.scene=="passenger-bike"?VehicleKind::Motorcycle:options.scene=="passenger-boat"?VehicleKind::Boat:VehicleKind::Aircraft;
                for(size_t index=0;index<game.vehicles.size();++index)if(game.vehicles[index].kind==kind){
                    game.occupied=int(index);game.player=game.vehicles[index].position;game.yaw=game.vehicles[index].yaw;
                    game.activeMission=game.completedMissions=4;game.missionStage=2;game.missionTimer=200;break;
                }
            }
            else if(options.scene=="boat"||options.scene=="aircraft"||options.scene=="rescue"||options.scene=="survey"){
                const bool rescue=options.scene=="rescue",survey=options.scene=="survey";
                const VehicleKind kind=options.scene=="boat"||rescue?VehicleKind::Boat:VehicleKind::Aircraft;
                for(size_t index=0;index<game.vehicles.size();++index)if(game.vehicles[index].kind==kind){
                    game.occupied=int(index);auto& craft=game.vehicles[index];craft.parked=false;
                    if(kind==VehicleKind::Aircraft){craft.position.y+=80;craft.speed=42;craft.throttle=.75f;craft.velocity={0,0,42};}
                    if(rescue){craft.position={3079,World::WaterLevel,1080};craft.yaw=Pi*.5f;game.activeMission=game.completedMissions=4;game.missionStage=1;game.missionTimer=210;}
                    if(survey){craft.position={-3200,game.world.height(-3200,-610)+90,-610};game.activeMission=game.completedMissions=5;game.missionStage=0;game.missionTimer=330;}
                    game.player=craft.position;game.yaw=craft.yaw;game.pitch=.15f;break;
                }
            }
            if(game.occupied<0)game.player.y=game.world.height(game.player.x,game.player.z);game.world.stream(game.player);game.messageTime=0;
            log<<"Smoke scene: "<<options.scene<<'\n';
        }
        uint64_t uploaded=UINT64_MAX,uploadedEpoch=0,worldEpoch=1;
        WorldStreamer worldStreamer;StreamingProbe streamingProbe;LodProbe lodProbe;TimingProbe timingProbe;
        const bool distant=!(options.smoke&&options.scene=="streaming");
        worldStreamer.setDistantEnabled(distant);
        if(distant&&!(options.smoke&&options.scene=="lod")){
            const auto began=std::chrono::steady_clock::now();
            if(!pumpDistant(worldStreamer,game,app,worldEpoch,options.smoke?15000.0:100.0,options.smoke,error)){
                log<<"ERROR 9: "<<error<<'\n';std::fprintf(stderr,"%s\n",error.c_str());
                if(!options.smoke)MessageBoxA(app.window,error.c_str(),"Meridian Coast - world generation",MB_OK|MB_ICONERROR);
                return 9;
            }
            log<<"Distant startup: ms="<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count()
               <<"; ready="<<game.world.renderReadyRadius(game.cameraEye())<<"; cacheTiles="<<game.world.distantTileCount()
               <<"; cacheBytes="<<game.world.distantBytes()<<"; selected="<<game.world.renderTiles().size()<<'\n';log.flush();
        }
        if(!app.running)return 0;
        Audio audio;std::string audioError;if(!options.smoke&&!audio.initialize(audioError)){log<<"Audio: "<<audioError<<'\n';game.message="No audio output device is available. The city is ready to play.";game.messageTime=7;}
        ShowWindow(app.window,options.smoke?SW_SHOWNOACTIVATE:SW_SHOW);UpdateWindow(app.window);if(settings.fullscreen&&!options.smoke)fullscreen(app,true);
        RAWINPUTDEVICE rid{1,2,0,app.window};if(!RegisterRawInputDevices(&rid,1,sizeof(rid)))log<<"Raw mouse registration failed\n";
        auto last=std::chrono::steady_clock::now();float presentationTime=game.time;FrameRateWindow frameRate;Ui ui;Cinematic cinematic;WorldMap worldMap;
        WorldAudioScene worldAudio;worldAudio.reset(worldEpoch);
        Vec3 audioListener=game.cameraEye(),audioForward=game.cameraTarget()-audioListener;
        AudioState audible;audible.station=game.radioStation;audible.volume=settings.volume;
        auto suspendAudio=[&](){audible.world=worldAudio.update(game,audioListener,audioForward,0,false,worldEpoch);audible.paused=true;audio.update(audible);};
        if(options.smoke&&(options.scene=="map"||options.scene=="trial-map")){app.mapOpen=true;if(options.scene=="trial-map")worldMap.span=1024;worldMap.focus(game.player);}
        RECT lifecycleWindow{};
        if(options.smoke&&options.scene=="cinematic")cinematic.start(0,game.player,game.yaw);
        while(app.running){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT)app.running=false;TranslateMessage(&msg);DispatchMessageW(&msg);}if(!app.running)break;
            auto now=std::chrono::steady_clock::now();float elapsed=std::chrono::duration<float>(now-last).count();last=now;float dt=clamp(elapsed,0.0001f,.05f);
            if(!app.active&&!options.smoke){game.paused=true;captureMouse(app,false);suspendAudio();WaitMessage();last=std::chrono::steady_clock::now();frameRate.reset();continue;}
            frameRate.observe(renderer.frameCount(),std::chrono::duration<double>(now.time_since_epoch()).count());
            if(IsIconic(app.window)){suspendAudio();WaitMessage();last=std::chrono::steady_clock::now();frameRate.reset();continue;}
            if(options.smoke&&options.scene=="lifecycle"){
                const uint64_t frameNumber=renderer.frameCount();
                if(frameNumber==30){
                    MONITORINFO monitor{sizeof(monitor)};
                    if(!GetMonitorInfoW(MonitorFromWindow(app.window,MONITOR_DEFAULTTONEAREST),&monitor)){error="Lifecycle monitor work area is unavailable.";result=8;break;}
                    const int targetWidth=std::clamp(int(monitor.rcWork.right-monitor.rcWork.left)-32,960,1280);
                    const int targetHeight=std::clamp(int(monitor.rcWork.bottom-monitor.rcWork.top)-32,580,760);
                    if(!SetWindowPos(app.window,nullptr,0,0,targetWidth,targetHeight,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)||!GetWindowRect(app.window,&lifecycleWindow)||lifecycleWindow.right-lifecycleWindow.left!=targetWidth||lifecycleWindow.bottom-lifecycleWindow.top!=targetHeight){error="Lifecycle resize did not reach its requested window dimensions.";result=8;break;}
                    log<<"Lifecycle: resize at frame 30\n";
                }
                if(frameNumber==60){
                    fullscreen(app,true);RECT actual{};MONITORINFO monitor{sizeof(monitor)};
                    if(!GetWindowRect(app.window,&actual)||!GetMonitorInfoW(MonitorFromWindow(app.window,MONITOR_DEFAULTTONEAREST),&monitor)||!EqualRect(&actual,&monitor.rcMonitor)||(GetWindowLongPtrW(app.window,GWL_STYLE)&WS_OVERLAPPEDWINDOW)!=0){error="Lifecycle fullscreen did not cover its monitor without window borders.";result=8;break;}
                    log<<"Lifecycle: fullscreen at frame 60\n";
                }
                if(frameNumber==90){
                    fullscreen(app,false);RECT actual{};
                    if(!GetWindowRect(app.window,&actual)||!EqualRect(&actual,&lifecycleWindow)||(GetWindowLongPtrW(app.window,GWL_STYLE)&WS_OVERLAPPEDWINDOW)!=WS_OVERLAPPEDWINDOW){error="Lifecycle windowed transition did not restore its original rectangle and style.";result=8;break;}
                    log<<"Lifecycle: windowed at frame 90\n";
                }
                if(frameNumber==30||frameNumber==60||frameNumber==90){RECT client{};
                    if(!GetClientRect(app.window,&client)||client.right<=0||client.bottom<=0||unsigned(client.right)!=app.width||unsigned(client.bottom)!=app.height){error="Lifecycle client dimensions disagree with the resize event.";result=8;break;}
                    log<<"Lifecycle verified client: "<<client.right<<'x'<<client.bottom<<" at frame "<<frameNumber<<'\n';
                }
            }
            if(app.resized&&app.width&&app.height){if(!renderer.resize(app.width,app.height,error)){result=4;break;}app.resized=false;}
            const bool menuAtInput=app.menu;
            XINPUT_STATE state{};WORD padPressed=0;Input input=readInput(app,dt,state,padPressed);
            if(!app.menu&&(app.pressed[VK_TAB]||(padPressed&XINPUT_GAMEPAD_BACK))){app.mapOpen=!app.mapOpen;if(app.mapOpen)worldMap.focus(game.player);}
            if(app.pressed[VK_F11]){settings.fullscreen=!settings.fullscreen;fullscreen(app,settings.fullscreen!=0);}
            if(app.pressed[VK_F3])app.diagnostics=!app.diagnostics;
            if(app.pressed[VK_ESCAPE]||(padPressed&XINPUT_GAMEPAD_START)){if(app.mapOpen)app.mapOpen=false;else if(app.showSettings){app.showSettings=false;app.selected=0;}else if(!app.title){app.menu=!app.menu;app.selected=0;}}
            if(app.menu){captureMouse(app,false);int count=app.showSettings?6:4;
                if(app.pressed[VK_UP]||(padPressed&XINPUT_GAMEPAD_DPAD_UP))app.selected=(app.selected+count-1)%count;
                if(app.pressed[VK_DOWN]||(padPressed&XINPUT_GAMEPAD_DPAD_DOWN))app.selected=(app.selected+1)%count;
                bool confirm=app.pressed[VK_RETURN]||(padPressed&XINPUT_GAMEPAD_A);int direction=(app.pressed[VK_RIGHT]||(padPressed&XINPUT_GAMEPAD_DPAD_RIGHT)?1:0)-(app.pressed[VK_LEFT]||(padPressed&XINPUT_GAMEPAD_DPAD_LEFT)?1:0);
                if(app.showSettings&&(confirm||direction)){switch(app.selected){case 0:settings.fullscreen=!settings.fullscreen;fullscreen(app,settings.fullscreen!=0);break;case 1:settings.vsync=!settings.vsync;break;case 2:settings.rayTracing=!settings.rayTracing;break;case 3:settings.volume=clamp(settings.volume+(direction?float(direction):1)*.05f,0,1);break;case 4:settings.exposure=clamp(settings.exposure+(direction?float(direction):1)*.1f,.5f,1.8f);break;case 5:app.showSettings=false;app.selected=0;break;}}
                else if(!app.showSettings&&confirm){switch(app.selected){case 0:app.menu=app.title=false;break;case 1:app.showSettings=true;app.selected=0;break;case 2:game.message=game.save(saveFile)?"Progress saved.":"The save could not be written.";game.messageTime=5;app.menu=app.title=false;break;case 3:app.running=false;break;}}
            }else {captureMouse(app,!options.smoke&&!app.mapOpen);if(app.pressed[VK_F5]){game.message=game.save(saveFile)?"Progress saved.":"The save could not be written.";game.messageTime=4;}
                if(app.pressed[VK_F9]){bool loaded=game.load(saveFile);if(!loaded){game.message="No valid saved game was found.";game.messageTime=4;}if(loaded){++worldEpoch;worldStreamer.reset(worldEpoch);worldAudio.reset(worldEpoch);uploaded=UINT64_MAX;cinematic.advance(0,true);}}}
            if(!app.running)break;game.paused=app.menu||app.mapOpen||cinematic.active();
            if(options.smoke){dt=1.f/60;input={};
                if(options.scene=="city"){input.moveY=.45f;input.lookX=.0015f;}
                if(options.scene=="drive")input.moveY=.7f;
                if(options.scene=="boat")input.moveY=.55f;
                if(options.scene=="night")game.dayTime=23;
                if(options.scene=="storm"){game.dayTime=14;game.rain=.9f;}
            }
            if(options.smoke&&options.scene.rfind("passenger-",0)==0)game.paused=true;
            if(options.smoke&&(options.scene=="trial"||options.scene=="trial-run"))game.paused=true;
            if(options.smoke&&options.scene.rfind("workshop",0)==0){game.paused=true;game.dayTime=options.scene=="workshop-night"?23.0f:14.0f;game.rain=0;}
            if(residentCapture.active)game.paused=true;
            if(marketCapture.active)game.paused=true;
            if(options.smoke&&options.scene=="streaming"){streamingProbe.beginFrame(game,worldStreamer,worldEpoch,renderer.frameCount());game.paused=true;}
            if(options.smoke&&options.scene=="lod"){
                lodProbe.beginFrame(game,worldStreamer,worldEpoch,renderer.frameCount());game.paused=true;
                if(!pumpDistant(worldStreamer,game,app,worldEpoch,10.0,false,error)){result=9;break;}
            }
            if(!worldStreamer.update(game.world,game.player,worldEpoch,error)){result=9;break;}
            const Vec3 movementStart=game.player;const int movementVehicle=game.occupied;bool simulationAdvanced=false;
            if(!app.menu){
                presentationTime+=dt;
                if(app.mapOpen){
                    const float zoom=app.wheel+float(app.pressed[VK_OEM_PLUS]||app.pressed[VK_ADD]||(padPressed&XINPUT_GAMEPAD_RIGHT_SHOULDER))-float(app.pressed[VK_OEM_MINUS]||app.pressed[VK_SUBTRACT]||(padPressed&XINPUT_GAMEPAD_LEFT_SHOULDER));
                    worldMap.navigate(input.moveX,input.moveY,dt,zoom);
                    if(app.pressed['F']||(padPressed&XINPUT_GAMEPAD_Y))worldMap.focus(game.player);
                    if(app.pressed[VK_RETURN]||(padPressed&XINPUT_GAMEPAD_A))worldMap.placeCenter(game.world);
                    if(app.mapClick){POINT point{};if(GetCursorPos(&point)&&ScreenToClient(app.window,&point))worldMap.place(float(point.x),float(point.y),float(app.width),float(app.height),std::max(.65f,app.height/900.f),game.world);}
                    if(app.pressed[VK_DELETE]||(padPressed&XINPUT_GAMEPAD_X))worldMap.hasWaypoint=false;
                    if(padPressed&XINPUT_GAMEPAD_B)app.mapOpen=false;
                }
                else if(cinematic.active())cinematic.advance(dt,!menuAtInput&&(app.pressed[VK_SPACE]||(padPressed&XINPUT_GAMEPAD_A)));
                else {
                    int previousMission=game.activeMission;simulationAdvanced=!game.paused;game.update(input,dt,false);
                    if(!options.smoke&&previousMission<0&&game.activeMission>=0){cinematic.start(game.activeMission,game.player,game.yaw);game.messageTime=0;}
                }
            }
            if(!worldStreamer.update(game.world,game.player,worldEpoch,error)){result=9;break;}
            RenderFrame frame;frame.eye=game.cameraEye();frame.target=game.cameraTarget();frame.time=presentationTime;frame.dayTime=game.dayTime;frame.rain=game.rain;frame.rayTracing=settings.rayTracing!=0;frame.vsync=!options.smoke&&settings.vsync!=0;frame.exposure=settings.exposure;
            if(cinematic.active())cinematic.camera(game.world,frame.eye,frame.target);
            if(options.smoke&&options.scene=="portrait"){frame.eye=game.player+Vec3{1,1.65f,1.85f};frame.target=game.player+Vec3{0,1.52f,0};}
            if(options.smoke&&options.scene=="vehicle"&&!game.vehicles.empty()){frame.eye=game.vehicles[0].position+Vec3{4,2.1f,5};frame.target=game.vehicles[0].position+Vec3{0,.85f,0};}
            if(options.smoke&&options.scene=="rescue"){frame.eye=game.player+Vec3{-6,4,-8};frame.target=Vec3{3085,World::WaterLevel+1,1080};}
            if(options.smoke&&options.scene=="trial"){frame.eye={249,game.world.height(249,-184)+3.6f,-184};frame.target={269,game.world.height(269,-173)+1.4f,-173};}
            if(options.smoke&&options.scene=="workshop"){
                const auto& site=World::garageSite();frame.eye=site.marker+Vec3{16,7,20};frame.target=(site.shell.min+site.shell.max)*.5f;
            }
            if(options.smoke&&(options.scene=="workshop-day"||options.scene=="workshop-night")){
                const auto& site=World::garageSite();frame.eye=site.vehicleStop+Vec3{5,2.1f,5};frame.target=site.vehicleStop+Vec3{-4,1.8f,-4};
            }
            if(options.smoke&&options.scene=="workshop-office"){
                const auto& site=World::garageSite();frame.eye=site.counter+Vec3{.8f,1.8f,2};frame.target=site.staff+Vec3{0,1.1f,0};
            }
            if(options.smoke&&options.scene.rfind("passenger-",0)==0&&game.occupied>=0){
                const auto& carrier=game.vehicles[size_t(game.occupied)];
                const Vec3 view=carrier.kind==VehicleKind::Aircraft?Vec3{2.4f,2.6f,2.5f}:carrier.kind==VehicleKind::Motorcycle?Vec3{-3,2.1f,3.4f}:carrier.kind==VehicleKind::Car?Vec3{2.7f,1.7f,.4f}:Vec3{-3.3f,2.2f,-4.4f};
                const Vec3 target=carrier.kind==VehicleKind::Aircraft?Vec3{0,1.5f,.6f}:carrier.kind==VehicleKind::Boat?Vec3{-.1f,1,-.6f}:carrier.kind==VehicleKind::Car?Vec3{.3f,1,.05f}:Vec3{0,.85f,0};
                frame.eye=carrier.position+right(carrier.yaw)*view.x+forward(carrier.yaw)*view.z+Vec3{0,view.y,0};
                frame.target=carrier.position+right(carrier.yaw)*target.x+forward(carrier.yaw)*target.z+Vec3{0,target.y,0};
            }
            if(options.smoke&&options.scene=="lod")lodProbe.camera(game,frame.eye,frame.target);
            if(residentCapture.active){frame.eye=residentCapture.eye;frame.target=residentCapture.target;}
            if(marketCapture.active){frame.eye=marketCapture.eye;frame.target=marketCapture.target;}
            AudioState audioState;audioState.rain=game.rain;audioState.wanted=float(game.wanted);audioState.shot=game.shotFlash;audioState.station=game.radioStation;audioState.volume=settings.volume*(cinematic.active()?.35f:1.f);audioState.paused=app.menu||app.mapOpen;
            const Biome listenerBiome=game.world.biome(game.player.x,game.player.z);
            audioState.shore=listenerBiome==Biome::Ocean||listenerBiome==Biome::Beach?1.f:listenerBiome==Biome::Island?.55f:0;
            audioState.nature=listenerBiome==Biome::Wetland?1.f:listenerBiome==Biome::Countryside?.65f:listenerBiome==Biome::Residential?.25f:0;
            audioState.urban=listenerBiome==Biome::Downtown?1.f:listenerBiome==Biome::Residential?.4f:0;
            if(game.occupied>=0&&game.occupied<int(game.vehicles.size())){const auto& vehicle=game.vehicles[size_t(game.occupied)];audioState.engine=vehicle.health>0?1.f:0;audioState.speed=vehicle.speed;audioState.engineKind=int(vehicle.kind);audioState.throttle=std::fabs(vehicle.throttle);
                if(vehicle.kind==VehicleKind::Aircraft){float groundGain=clamp(1-(game.player.y-game.world.height(game.player.x,game.player.z))/120,0,1);audioState.shore*=groundGain;audioState.nature*=groundGain;audioState.urban*=groundGain;}}
            movementAudio(audioState,game,movementStart,movementVehicle,input,dt);
            audioListener=frame.eye;audioForward=frame.target-frame.eye;
            audioState.world=worldAudio.update(game,audioListener,audioForward,simulationAdvanced?dt:0,simulationAdvanced,worldEpoch);
            audible=audioState;audio.update(audible);
            if(uploaded!=game.world.renderRevision||uploadedEpoch!=worldEpoch){
                size_t vertices=0,indices=0;for(const auto& chunk:game.world.chunks){vertices+=chunk.mesh.vertices.size();indices+=chunk.mesh.indices.size();}
                log<<"World revision "<<game.world.revision<<" / render "<<game.world.renderRevision<<": "<<vertices
                   <<" detailed vertices, "<<indices/3<<" detailed triangles; selected tiles="<<game.world.renderTiles().size()<<'\n';log.flush();
                if(!renderer.setWorld(game.world,worldEpoch,error)){result=5;break;}uploaded=game.world.renderRevision;uploadedEpoch=worldEpoch;
            }
            Mesh dynamic=game.dynamicMesh();std::vector<Light> lights=game.lightSources();ui.begin(float(app.width),float(app.height));
            if(app.menu)drawMenu(ui,app,settings,renderer);
            else if(app.mapOpen)drawWorldMap(ui,game,worldMap);
            else if(cinematic.active())drawCinematic(ui,cinematic,game.missionInfo()?game.missionInfo()->title:nullptr);
            else drawHud(ui,game,frameRate.framesPerSecond(),app.diagnostics,renderer,worldMap.hasWaypoint?&worldMap.waypoint:nullptr);
            frame.dynamic=&dynamic;frame.lights=&lights;frame.ui=&ui.vertices;
            frame.coverageRadius=game.world.distantEnabled()?game.world.renderReadyRadius(frame.eye):-1.0f;
            frame.groundHeight=game.world.height(frame.eye.x,frame.eye.z);
            if(!renderer.render(frame,error)){result=6;break;}
            if(options.smoke&&!timingProbe.observe(renderer.timingStats(),renderer.frameCount(),error)){result=12;break;}
            if(options.smoke&&options.scene=="streaming"&&!streamingProbe.observe(game.world,worldStreamer.stats(),renderer.streamStats(),worldEpoch,renderer.frameCount(),log,error)){result=10;break;}
            if(options.smoke&&options.scene=="lod"&&!lodProbe.observe(game.world,worldStreamer.stats(),renderer.streamStats(),frame.eye,worldEpoch,renderer.frameCount(),log,error)){result=10;break;}
            if(options.smoke&&renderer.frameCount()>=options.frames){
                if(options.scene=="streaming"&&!streamingProbe.complete()){error="Streaming diagnostic reached its frame limit before all phases settled.";result=10;break;}
                if(options.scene=="lod"&&!lodProbe.complete()){error="Distant-world diagnostic reached its frame limit before all phases settled.";result=10;break;}
                if(!options.screenshot.empty()&&!renderer.capture(options.screenshot,error))result=7;
                if(!result&&!timingProbe.observe(renderer.timingStats(),renderer.frameCount(),error))result=12;
                break;
            }
            app.pressed.fill(false);app.wheel=0;app.mapClick=false;
        }
        captureMouse(app,false);
        TimingProbe::write(log,renderer.timingStats());
        const auto& soundStats=worldAudio.stats();
        log<<"Spatial audio: snapshots="<<soundStats.snapshots<<"; engineHistories="<<soundStats.trackedEngines<<"; footHistories="<<soundStats.trackedFeet
           <<"; contacts="<<soundStats.contacts<<"; renewals="<<soundStats.sourceRenewals<<"; rejected="<<soundStats.rejectedSamples
           <<"; duplicateIds="<<soundStats.duplicateIdentities<<"; capacityDrops="<<soundStats.capacityDrops<<'\n';
        if(result){log<<"ERROR "<<result<<": "<<error<<'\n';std::fprintf(stderr,"%s\n",error.c_str());if(!options.smoke)MessageBoxA(app.window,error.c_str(),"Meridian Coast",MB_OK|MB_ICONERROR);}
    if(!options.smoke){if(!game.save(saveFile))log<<"Autosave failed\n";saveSettings(settingsPath,settings);}
        log<<"Exit "<<result<<" after "<<renderer.frameCount()<<" frames\n";
    }
    DestroyWindow(app.window);UnregisterClassW(cls.lpszClassName,instance);return result;
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    Options options=parseOptions();
    try{return execute(instance,options);}
    catch(const std::exception& exception){std::fprintf(stderr,"%s\n",exception.what());if(!options.smoke)MessageBoxA(nullptr,exception.what(),"Meridian Coast - unrecoverable error",MB_OK|MB_ICONERROR);return 10;}
    catch(...){std::fprintf(stderr,"An unexpected error ended the session.\n");if(!options.smoke)MessageBoxW(nullptr,L"An unexpected error ended the session.",L"Meridian Coast",MB_OK|MB_ICONERROR);return 11;}
}
