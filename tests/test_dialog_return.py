#!/usr/bin/env python3
"""Compile and execute Plutonium's dialog-dismissal callback on the host.

The Switch renderer is stubbed, but the cleanup call is taken verbatim from
Dialog::Show. The original missing bool return must fail compilation, even
without a console/GPU. Run with: python3 tests/test_dialog_return.py
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "externals/Plutonium/Plutonium/Plutonium/Source/pu/ui/ui_Dialog.cpp").read_text()
start = source.index("((Application*)App)->CallForRenderWithRenderOver", source.index("if(!ok)"))
end = source.index("break;", start)
cleanup = source[start:end]

harness = r'''
#include <cassert>
#include <functional>
#include <memory>
namespace render {
struct Renderer { using Ref = std::shared_ptr<Renderer>; };
}
struct Application {
    render::Renderer::Ref renderer = std::make_shared<render::Renderer>();
    int cleanup_frames = 0;
    bool CallForRenderWithRenderOver(std::function<bool(render::Renderer::Ref &)> callback) {
        ++cleanup_frames;
        const bool result = callback(renderer);
        assert(result);
        return result;
    }
};
void dismissDialog(void *App) {
'''
checks = r'''
}
int main() {
    Application application;
    for (int i = 0; i < 100; ++i) dismissDialog(&application);
    assert(application.cleanup_frames == 100);
}
'''

with tempfile.TemporaryDirectory(prefix="nxcord-dialog-") as temp:
    temp = Path(temp)
    cpp = temp / "dialog.cpp"
    cpp.write_text(harness + cleanup + checks)
    binary = temp / "dialog"
    subprocess.run([
        "g++", "-std=c++17", "-O2", "-Werror=return-type",
        str(cpp), "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
    print("Dialog-dismissal regression test passed")
