#!/usr/bin/env python3
"""Exercise the actual per-list pacer in isolation; this is not game FPS."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[2]
text=(root/'lcs/host/lcs_profile.cpp').read_text()
a=text.index('void cap_frame_rate(')
b=text.index('\n}\n',a)+2
body=text[a:b]
bridge=(root/'android/app/src/main/cpp/android_bridge.cpp').read_text()
assert 'setenv("LCS_FPS_CAP", "0", 1);' in bridge
assert 'unsetenv("LCS_UNCAPPED");' in bridge
assert 'throttle_vblank_to_real_time()' in text
source='''#include <cstdint>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <iostream>
'''+body+'''
int main(int argc,char **argv) {
    if(argc!=2) return 2;
    setenv("LCS_FPS_CAP",argv[1],1);
    auto begin=std::chrono::steady_clock::now();
    for(std::uint32_t i=0;i<24;++i) cap_frame_rate(0x04000000u+i*256);
    std::cout<<std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
}
'''
with tempfile.TemporaryDirectory() as folder:
    cpp=Path(folder)/'test.cpp';binary=Path(folder)/'test';cpp.write_text(source)
    subprocess.run(['g++','-O2','-std=c++20','-pthread',str(cpp),'-o',str(binary)],check=True)
    old=float(subprocess.check_output([binary,'60']))
    new=float(subprocess.check_output([binary,'0']))
    assert old>0.30,old
    assert new<0.05,new
    print(f'PASS: 24 changing GE-list addresses: old cap={old:.6f}s; disabled cap={new:.6f}s. Vblank pacing retained. Not a game FPS benchmark.')
