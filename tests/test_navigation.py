#!/usr/bin/env python3
"""Controller/menu and frame-lifetime regressions using production UI methods."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def method(path, signature):
    source = (ROOT / path).read_text()
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

menu = "externals/Plutonium/Plutonium/Plutonium/Source/pu/ui/elm/elm_Menu.cpp"
application = "externals/Plutonium/Plutonium/Plutonium/Source/pu/ui/ui_Application.cpp"
production = "\n".join(method(menu, name) for name in [
    "void Menu::ClearItems()", "void Menu::SetSelectedIndex", "void Menu::OnInput"])
production += "\n" + method(application, "void Application::OnRender()")

harness = r'''
#include <algorithm>
#include <cassert>
#include <chrono>
#include <functional>
#include <memory>
#include <limits>
#include <common/ipc_structures.hpp>
#include "ui_browser_model.hpp"
using s32 = int;
using u64 = uint64_t;
using u8 = uint8_t;
constexpr u64 KEY_A=1, KEY_L=2, KEY_R=4, KEY_DUP=8, KEY_DDOWN=16,
  KEY_LSTICK_UP=32, KEY_LSTICK_DOWN=64, KEY_RSTICK_UP=128, KEY_RSTICK_DOWN=256, KEY_TOUCH=512;
constexpr int CONTROLLER_P1_AUTO=0, CONTROLLER_HANDHELD=1;
struct Touch {
  int X=-1,Y=-1;
  bool IsEmpty() const { return X < 0; }
  static const Touch Empty;
};
const Touch Touch::Empty{};
struct touchPosition { int px=0,py=0; };
u64 input_down=0;
void hidScanInput() {}
u64 hidKeysDown(int controller) { return controller == 0 ? input_down : 0; }
u64 hidKeysUp(int) { return 0; }
u64 hidKeysHeld(int) { return 0; }
void hidTouchRead(touchPosition *, int) {}
struct Color { int r,g,b,a; };
namespace render {
  int deleted=0;
  void DeleteTexture(int) { ++deleted; }
  struct Renderer {
    using Ref=std::shared_ptr<Renderer>;
    bool initialized=true;
    bool HasInitialized() { return initialized; }
    void RenderTexture(int,int,int) {}
    void RenderRectangleFill(Color,int,int,int,int) { assert(initialized); }
  };
}
struct MenuItem {
  std::function<void()> callback=[]{};
  int GetCallbackCount() { return 1; }
  u64 GetCallbackKey(int) { return KEY_A; }
  auto GetCallback(int) { return callback; }
};
struct Menu {
  int isel=0, fisel=0, previsel=0, ishow=7, isize=56,w=784;
  int selfact=255,pselfact=0,basestatus=0;
  bool dtouch=false;
  std::chrono::steady_clock::time_point basetime;
  std::vector<int> loadednames,loadedicons;
  std::vector<std::shared_ptr<MenuItem>> itms;
  std::function<void()> onselch=[]{};
  int GetProcessedX() { return 36; }
  int GetProcessedY() { return 206; }
  void ReloadItemRenders() { loadednames.assign(std::min<int>(ishow,itms.size()),1); }
  void ClearItems();
  void SetSelectedIndex(s32);
  void OnInput(u64,u64,u64,Touch);
  void populate(int count) { for(int i=0;i<count;++i) itms.push_back(std::make_shared<MenuItem>()); }
};
struct Element {
  int rendered=0, activated=0;
  std::function<void()> action=[]{};
  bool IsVisible() { return true; }
  int GetProcessedX() { return 0; }
  int GetProcessedY() { return 0; }
  void OnRender(render::Renderer::Ref&,int,int) { ++rendered; }
  void OnInput(u64 down,u64,u64,Touch) { if(down & KEY_A) { ++activated; action(); } }
};
struct Layout {
  std::vector<std::shared_ptr<Element>> elements;
  std::vector<std::function<void()>> threads;
  Touch GetSimulatedTouch() { return {}; }
  void PreRender() {}
  auto GetAllThreads() { return threads; }
  bool HasBackgroundImage() { return false; }
  int GetBackgroundImageTexture() { return 0; }
  std::function<void(u64,u64,u64,Touch)> GetOnInput() { return [](u64,u64,u64,Touch){}; }
  bool HasChilds() { return !elements.empty(); }
  int GetCount() { return elements.size(); }
  auto At(int index) { return elements.at(index); }
};
struct Overlay {
  bool Render(render::Renderer::Ref&) { return true; }
  void NotifyEnding(bool) {}
};
struct Application {
  std::shared_ptr<Layout> lyt;
  render::Renderer::Ref rend=std::make_shared<render::Renderer>();
  std::vector<std::function<void()>> thds;
  std::function<void(u64,u64,u64,Touch)> cbipt=[](u64,u64,u64,Touch){};
  std::shared_ptr<Overlay> ovl;
  bool rover=false;
  int tmillis=0,fadea=255;
  std::chrono::steady_clock::time_point tclock;
  void EndOverlay() { ovl.reset(); }
  void OnRender();
};
'''

checks = r'''
int main() {
  Menu menu;
  menu.populate(2);
  menu.SetSelectedIndex(0);
  assert(menu.fisel == 0);
  menu.SetSelectedIndex(999);
  assert(menu.isel == 1 && menu.fisel == 0);
  menu.ClearItems();
  assert(menu.isel == 0 && menu.fisel == 0 && render::deleted > 0);
  menu.populate(200);
  menu.SetSelectedIndex(174);
  assert(menu.isel == 174 && menu.fisel <= 174 && menu.fisel >= 0);
  menu.OnInput(KEY_R,0,0,{});
  assert(menu.isel == 181);
  menu.OnInput(KEY_L,0,0,{});
  assert(menu.isel == 174);
  menu.basetime=std::chrono::steady_clock::now()-std::chrono::milliseconds(400);
  menu.basestatus=1;
  menu.OnInput(0,0,KEY_LSTICK_DOWN,{});
  assert(menu.isel == 175);
  menu.OnInput(0,0,KEY_LSTICK_DOWN,{});
  assert(menu.isel == 175);
  menu.basetime-=std::chrono::milliseconds(100);
  menu.OnInput(0,0,KEY_LSTICK_DOWN,{});
  assert(menu.isel == 176);
  menu.ClearItems(); menu.populate(2);
  int activated=0;
  menu.itms[1]->callback=[&] { ++activated; menu.ClearItems(); menu.populate(3); };
  menu.OnInput(0,0,0,Touch{60,272});
  menu.OnInput(0,0,0,{});
  assert(activated == 1 && menu.itms.size() == 3 && menu.isel == 0);

  Application app;
  app.lyt=std::make_shared<Layout>();
  std::weak_ptr<Layout> old=app.lyt;
  auto next=std::make_shared<Layout>();
  auto old_item=std::make_shared<Element>();
  auto new_item=std::make_shared<Element>();
  app.lyt->elements.push_back(old_item);
  next->elements.push_back(new_item);
  old_item->action=[&] { app.lyt=next; assert(!old.expired()); };
  input_down=KEY_A;
  app.OnRender();
  assert(old.expired());
  assert(old_item->activated == 1 && new_item->activated == 0 && new_item->rendered == 0);
  input_down=0;
  app.OnRender();
  assert(new_item->rendered == 1 && new_item->activated == 0);
  app.cbipt=[&](u64,u64,u64,Touch) { app.rend->initialized=false; };
  app.OnRender(); // no rendering after Close() finalizes SDL.

  std::vector<IPCStruct::DiscordServer> rows{
    IPCStruct::create_discord_server("Zulu",1), IPCStruct::create_discord_server("Alpha",2)};
  auto sorted=UIBrowser::filter(rows,"");
  assert(sorted[0].id == 2 && UIBrowser::selection(sorted,1) == 1);
  assert(UIBrowser::filter(rows,"ALP").size() == 1);
  rows[1]=IPCStruct::create_discord_server("Beta",2);
  assert(!UIBrowser::equal(rows,sorted));
  assert(UIBrowser::range_index(0,0.1f,20) == 0);
  assert(UIBrowser::range_index(100,0.1f,20) == 19);
  assert(UIBrowser::range_index(std::numeric_limits<float>::quiet_NaN(),0.1f,20) == 0);
  puts("Navigation/controller regression tests passed");
}
'''

with tempfile.TemporaryDirectory(prefix="nxcord-navigation-") as temp:
    temp = Path(temp)
    cpp = temp / "navigation.cpp"
    cpp.write_text(harness + production + checks)
    binary = temp / "navigation"
    subprocess.run([
        "g++", "-std=c++17", "-O1", "-fsanitize=address,undefined",
        "-fno-omit-frame-pointer", "-I" + str(ROOT / "include"),
        "-I" + str(ROOT / "client/src/ui"), str(cpp),
        str(ROOT / "common/src/ipc_structures.cpp"), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
