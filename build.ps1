param([string]$Out="PasjansD.exe",[string]$Opt="-O2")
$ErrorActionPreference="Stop"
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force build | Out-Null

# C++ compiler: Zig-as-mingw, no Visual Studio/MinGW install needed.
# `pip install ziglang` (see README.md), then either works:
#   - `zig` on PATH, or
#   - `python -m ziglang` (always works after pip install, regardless of where it landed)
$zigCmd = $null
if(Get-Command zig -ErrorAction SilentlyContinue){ $zigCmd=@("zig") }
else { $zigCmd=@("python","-m","ziglang") }

function Invoke-Zig([string[]]$ZigArgs){
   & $zigCmd[0] @($zigCmd[1..($zigCmd.Length-1)] + $ZigArgs)
   if($LASTEXITCODE -ne 0){ throw "zig failed (exit $LASTEXITCODE): $ZigArgs" }
}

Invoke-Zig @("rc","res/app.rc","build/app_res.res")
Invoke-Zig @("rc","-I","res","res/cards.rc","build/cards_res.res")
Invoke-Zig @(
   "c++","-target","x86_64-windows-gnu","-std=c++17",$Opt,"-mwindows","-Wl,--subsystem,windows","-static",
   "-o","build/$Out","src/main.cpp","build/app_res.res","build/cards_res.res",
   "-lgdiplus","-ld2d1","-ldwrite","-lwindowscodecs","-ldsound","-lwinmm","-lcomctl32","-lcomdlg32",
   "-lole32","-lgdi32","-luser32","-lshell32","-luuid","-lwinhttp"
)
"OK build/$Out"
