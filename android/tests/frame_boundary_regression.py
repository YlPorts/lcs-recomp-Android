#!/usr/bin/env python3
"""Exercise the actual GE-list splitting helper, without game assets."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
s=(root/'lcs/host/lcs_profile.cpp').read_text()
a=s.index('bool ge_frame_split_enabled()');b=s.index('\nvoid report_timestep',a)
code='''#include <cstdlib>
#include <cstdint>
#include <iostream>
namespace psprecomp {struct Runtime {Runtime &memory(){return *this;}};}
static bool g_ge_list_since_finish=false;
static std::uint32_t g_ge_last_list_address=0;
static unsigned swaps=0,lists=0;
static bool ge_gpu_backend_finish_color_frame(std::uint64_t){++swaps;return true;}
static void execute_ge_list_rendered(psprecomp::Runtime&,std::uint32_t){++lists;}
'''+s[a:b]+'''
int main(int argc,char**){
 if(argc>1)setenv("LCS_GE_NO_FRAME_SPLIT","1",1);
 psprecomp::Runtime r;
 for(unsigned i=0;i<4;i++)execute_ge_list_frame(r,0x04000000+i*256,1);
 std::cout<<swaps<<" "<<lists;
}
'''
with tempfile.TemporaryDirectory() as folder:
    cpp=Path(folder)/'test.cpp';binary=Path(folder)/'test';cpp.write_text(code)
    subprocess.run(['g++','-std=c++20','-O2',str(cpp),'-o',str(binary)],check=True)
    assert subprocess.check_output([binary],text=True)=='3 4'
    assert subprocess.check_output([binary,'vblank-only'],text=True)=='0 4'
    print('PASS: four GE lists execute; intermediate presentation calls reduced from 3 to 0. Vblank presentation remains separate.')
