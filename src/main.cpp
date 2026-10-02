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
#include "game.h"
#include "renderer.h"
#include "audio.h"
#include "ui.h"

namespace {
using namespace mc;
struct Options {bool smoke=false,warp=false;unsigned frames=120;std::string screenshot;};
std::string utf8(const std::wstring& s) {
    if(s.empty())return {};
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),nullptr,0,nullptr,nullptr);
    std::string out(size_t(n),'\0');WideCharToMultiByte(CP_UTF8,0,s.data(),int(s.size()),out.data(),n,nullptr,nullptr);return out;
}
Options parseOptions() {
    Options options;int count=0;LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&count);
    for(int i=1;args&&i<count;++i){std::wstring a=args[i];if(a==L"--smoke")options.smoke=true;else if(a==L"--warp")options.warp=true;
        else if(a==L"--frames"&&i+1<count)options.frames=unsigned(std::clamp(_wtoi(args[++i]),1,10000));
        else if(a==L"--screenshot"&&i+1<count)options.screenshot=utf8(args[++i]);}
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
    unsigned width=1600,height=900;std::array<bool,256> pressed{};float mouseX=0,mouseY=0;
    bool title=true,menu=true,showSettings=false,diagnostics=false;int selected=0;
    RECT windowed{};DWORD oldStyle=0;bool borderless=false;WORD padButtons=0;
};
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
    case WM_ACTIVATEAPP:app->active=wp!=0;if(!app->active)captureMouse(*app,false);return 0;
    case WM_KILLFOCUS:app->pressed.fill(false);app->mouseX=app->mouseY=0;captureMouse(*app,false);return 0;
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
    if(!app.showSettings){ui.text(56*s,578*s,"WASD MOVE / DRIVE    M ACCEPT CONTRACT",1.35f*s,muted);ui.text(56*s,603*s,"E ENTER VEHICLE      MOUSE LOOK / AIM",1.35f*s,muted);ui.text(56*s,628*s,"R RELOAD  Q RADIO    SPACE BRAKE / JUMP",1.35f*s,muted);ui.text(56*s,653*s,"F5 SAVE  F9 LOAD     F11 FULLSCREEN",1.35f*s,muted);}
    ui.text(56*s,ui.height-63*s,"ARROWS / DPAD SELECT   ENTER / A CONFIRM",1.25f*s,muted);
    ui.text(56*s,ui.height-37*s,"ORIGINAL WORLD. ORIGINAL SOUND.",1.25f*s,teal);
    if(ui.width>1100*s){float x=panel+45*s;ui.text(x,ui.height-142*s,"PORT SOLACE",2.2f*s);ui.text(x,ui.height-112*s,"THE COAST IS CALLING.",1.5f*s,teal);ui.wrapped(x,ui.height-83*s,"A stolen tide chart. A missing courier. One last job before the storm.",1.35f*s,ui.width-x-50*s,muted);}
}
int execute(HINSTANCE instance,const Options& options) {
    App app;if(options.smoke){app.width=960;app.height=540;app.menu=app.title=false;}
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
        Game game;game.initialize();if(!options.smoke&&std::filesystem::exists(savePath))game.load(saveFile);
        uint64_t uploaded=UINT64_MAX;
        Audio audio;std::string audioError;if(!options.smoke&&!audio.initialize(audioError)){log<<"Audio: "<<audioError<<'\n';game.message="No audio output device is available. The city is ready to play.";game.messageTime=7;}
        ShowWindow(app.window,options.smoke?SW_SHOWNOACTIVATE:SW_SHOW);UpdateWindow(app.window);if(settings.fullscreen&&!options.smoke)fullscreen(app,true);
        RAWINPUTDEVICE rid{1,2,0,app.window};if(!RegisterRawInputDevices(&rid,1,sizeof(rid)))log<<"Raw mouse registration failed\n";
        auto last=std::chrono::steady_clock::now();float fps=60;Ui ui;
        while(app.running){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT)app.running=false;TranslateMessage(&msg);DispatchMessageW(&msg);}if(!app.running)break;
            auto now=std::chrono::steady_clock::now();float elapsed=std::chrono::duration<float>(now-last).count();last=now;float dt=clamp(elapsed,0.0001f,.05f);fps=lerp(fps,1/std::max(elapsed,.0001f),.04f);
            if(!app.active&&!options.smoke){game.paused=true;captureMouse(app,false);WaitMessage();last=std::chrono::steady_clock::now();continue;}
            if(IsIconic(app.window)){WaitMessage();last=std::chrono::steady_clock::now();continue;}
            if(app.resized&&app.width&&app.height){if(!renderer.resize(app.width,app.height,error)){result=4;break;}app.resized=false;}
            XINPUT_STATE state{};WORD padPressed=0;Input input=readInput(app,dt,state,padPressed);
            if(app.pressed[VK_F11]){settings.fullscreen=!settings.fullscreen;fullscreen(app,settings.fullscreen!=0);}
            if(app.pressed[VK_F3])app.diagnostics=!app.diagnostics;
            if(app.pressed[VK_ESCAPE]||(padPressed&XINPUT_GAMEPAD_START)){if(app.showSettings){app.showSettings=false;app.selected=0;}else if(!app.title){app.menu=!app.menu;app.selected=0;}}
            if(app.menu){captureMouse(app,false);int count=app.showSettings?6:4;
                if(app.pressed[VK_UP]||(padPressed&XINPUT_GAMEPAD_DPAD_UP))app.selected=(app.selected+count-1)%count;
                if(app.pressed[VK_DOWN]||(padPressed&XINPUT_GAMEPAD_DPAD_DOWN))app.selected=(app.selected+1)%count;
                bool confirm=app.pressed[VK_RETURN]||(padPressed&XINPUT_GAMEPAD_A);int direction=(app.pressed[VK_RIGHT]||(padPressed&XINPUT_GAMEPAD_DPAD_RIGHT)?1:0)-(app.pressed[VK_LEFT]||(padPressed&XINPUT_GAMEPAD_DPAD_LEFT)?1:0);
                if(app.showSettings&&(confirm||direction)){switch(app.selected){case 0:settings.fullscreen=!settings.fullscreen;fullscreen(app,settings.fullscreen!=0);break;case 1:settings.vsync=!settings.vsync;break;case 2:settings.rayTracing=!settings.rayTracing;break;case 3:settings.volume=clamp(settings.volume+(direction?float(direction):1)*.05f,0,1);break;case 4:settings.exposure=clamp(settings.exposure+(direction?float(direction):1)*.1f,.5f,1.8f);break;case 5:app.showSettings=false;app.selected=0;break;}}
                else if(!app.showSettings&&confirm){switch(app.selected){case 0:app.menu=app.title=false;break;case 1:app.showSettings=true;app.selected=0;break;case 2:game.message=game.save(saveFile)?"Progress saved.":"The save could not be written.";game.messageTime=5;app.menu=app.title=false;break;case 3:app.running=false;break;}}
            }else {captureMouse(app,!options.smoke);if(app.pressed[VK_F5]){game.message=game.save(saveFile)?"Progress saved.":"The save could not be written.";game.messageTime=4;}
                if(app.pressed[VK_F9]){game.message=game.load(saveFile)?"Progress restored.":"No valid saved game was found.";game.messageTime=4;}}
            if(!app.running)break;game.paused=app.menu;
            if(options.smoke){dt=1.f/60;input={};input.moveY=.45f;input.lookX=.0015f;}
            if(!app.menu)game.update(input,dt);
            game.world.stream(game.player);
            if(uploaded!=game.world.revision){Mesh world=game.world.combinedMesh();log<<"World revision "<<game.world.revision<<": "<<world.vertices.size()<<" vertices, "<<world.indices.size()/3<<" triangles\n";log.flush();if(!renderer.setWorld(world,error)){result=5;break;}uploaded=game.world.revision;}
            Mesh dynamic=game.dynamicMesh();ui.begin(float(app.width),float(app.height));
            if(app.menu)drawMenu(ui,app,settings,renderer);else drawHud(ui,game,fps,app.diagnostics,renderer);
            RenderFrame frame;frame.eye=game.cameraEye();frame.target=game.cameraTarget();frame.time=game.time;frame.dayTime=game.dayTime;frame.rain=game.rain;frame.rayTracing=settings.rayTracing!=0;frame.vsync=!options.smoke&&settings.vsync!=0;frame.exposure=settings.exposure;frame.dynamic=&dynamic;frame.ui=&ui.vertices;
            if(!renderer.render(frame,error)){result=6;break;}
            AudioState audioState;audioState.rain=game.rain;audioState.wanted=float(game.wanted);audioState.shot=game.shotFlash;audioState.station=game.radioStation;audioState.volume=settings.volume;audioState.paused=app.menu;
            if(game.occupied>=0&&game.occupied<int(game.vehicles.size())){audioState.engine=1;audioState.speed=game.vehicles[size_t(game.occupied)].speed;}audio.update(audioState);
            if(options.smoke&&renderer.frameCount()>=options.frames){if(!options.screenshot.empty()&&!renderer.capture(options.screenshot,error))result=7;break;}
            app.pressed.fill(false);
        }
        captureMouse(app,false);
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
    catch(const std::exception& exception){if(!options.smoke)MessageBoxA(nullptr,exception.what(),"Meridian Coast - unrecoverable error",MB_OK|MB_ICONERROR);return 10;}
    catch(...){if(!options.smoke)MessageBoxW(nullptr,L"An unexpected error ended the session.",L"Meridian Coast",MB_OK|MB_ICONERROR);return 11;}
}
