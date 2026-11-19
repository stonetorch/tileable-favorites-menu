[CmdletBinding()]
param(
    [switch]$Fresh,
    [switch]$AlwaysShowLog,
    [switch]$ConfigureOnly
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $projectRoot 'build\debug'
$clionRoot = 'C:\Program Files\JetBrains\CLion 2026.2.3\bin'
$cmake = Join-Path $clionRoot 'cmake\win\x64\bin\cmake.exe'
$ninja = Join-Path $clionRoot 'ninja\win\x64\ninja.exe'
$vsDevCmd = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat'
$vcpkgRoot = 'D:\Tes5\Dev\CustomAnimationFramework\extern\vcpkg'
$commonLib = 'D:\Tes5\Dev\CustomAnimationFramework\extern\CommonLibSSE-NG'
$toolchain = Join-Path $vcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
foreach ($file in @($cmake, $ninja, $vsDevCmd, $toolchain, (Join-Path $commonLib 'CMakeLists.txt'))) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "所需文件不存在：$file" }
}

# 与 vcpkg 库使用同一 MSVC 版本，并完整初始化编译器、链接器和库路径。
$lines = @(& $env:ComSpec /d /c "`"$vsDevCmd`" -no_logo -arch=amd64 -vcvars_ver=14.44 >nul && set")
if ($LASTEXITCODE -ne 0) { throw 'MSVC 14.44 开发环境初始化失败。' }
foreach ($line in $lines) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
}
$env:VCPKG_ROOT = $vcpkgRoot
$env:Path = (Split-Path $cmake) + ';' + (Split-Path $ninja) + ';' + $env:Path

function Invoke-CMakeStage {
    param([string]$Name, [string[]]$Arguments)
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $stageLog = @(& $cmake @Arguments 2>&1)
        $stageExitCode = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    if ($AlwaysShowLog -or $stageExitCode -ne 0) { $stageLog | ForEach-Object { Write-Host $_ } }
    if ($stageExitCode -ne 0) { throw "$Name 失败（退出码 $stageExitCode）。" }
}

Push-Location -LiteralPath $projectRoot
try {
    $argsConfigure = @('-S', $projectRoot, '-B', $buildDir, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Debug',
        '-DCMAKE_CXX_COMPILER=C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe',
        "-DCMAKE_MAKE_PROGRAM=$($ninja -replace '\\','/')",
        "-DCMAKE_TOOLCHAIN_FILE=$($toolchain -replace '\\','/')",
        '-DVCPKG_TARGET_TRIPLET=x64-windows-static',
        '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDebug',
        '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DCMAKE_NINJA_CMCLDEPS_RC=0',
        '-DCMAKE_CXX_FLAGS=/permissive- /Zc:preprocessor /EHsc /MP /W4 -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DUNICODE -D_UNICODE',
        "-DFETCHCONTENT_SOURCE_DIR_COMMONLIBSSE=$($commonLib -replace '\\','/')")
    if ($Fresh) { $argsConfigure = @('--fresh') + $argsConfigure }
    Invoke-CMakeStage -Name '配置' -Arguments $argsConfigure
    if ($ConfigureOnly) { Write-Output 'CMake 配置完成（未编译）。'; return }
    Invoke-CMakeStage -Name 'Debug 编译' -Arguments @('--build', $buildDir, '--parallel', '4')
    Write-Output "Debug 编译成功：$(Join-Path $buildDir 'TileableFavoritesMenu.dll')"
} finally { Pop-Location }

