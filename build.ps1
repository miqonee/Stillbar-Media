param([switch]$ProbeOnly)
$ErrorActionPreference = 'Stop'
$taskbarRoot = $PSScriptRoot
$taskbarVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$taskbarInstall = ''
if (Test-Path -LiteralPath $taskbarVswhere) {
    $taskbarDetected = & $taskbarVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($taskbarDetected) { $taskbarInstall = $taskbarDetected }
}
if (-not $taskbarInstall) { throw 'Microsoft C++ Build Tools are not installed.' }
$taskbarDeveloperShell = Join-Path $taskbarInstall 'Common7\Tools\Launch-VsDevShell.ps1'
if (-not (Test-Path -LiteralPath $taskbarDeveloperShell)) { throw 'Microsoft C++ Build Tools are not installed.' }
& $taskbarDeveloperShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$taskbarBuild = Join-Path $taskbarRoot 'build'
$taskbarDist = Join-Path $taskbarRoot ('dist\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $taskbarBuild, $taskbarDist -Force | Out-Null
Push-Location $taskbarBuild
try {
    $taskbarFlags = @('/nologo','/std:c++20','/EHsc','/W4','/O2','/MT','/utf-8','/DUNICODE','/D_UNICODE','/D_WIN32_WINNT=0x0A00','/DWINVER=0x0A00',"/I$taskbarRoot\src")
    & rc.exe /nologo /DSTILLBAR_CORE "/I$taskbarRoot\assets" "/fo$taskbarBuild\core.res" "$taskbarRoot\src\app.rc"
    if ($LASTEXITCODE -ne 0) { throw 'Core resource compilation failed.' }
    & cl.exe @taskbarFlags /WX /Z7 /LD "$taskbarRoot\src\core.cpp" "$taskbarBuild\core.res" "/Fe:$taskbarDist\Stillbar.Core.dll" /link /DEBUG:FULL /OPT:REF /OPT:ICF "/PDB:$taskbarDist\Stillbar.Core.pdb" "/MAP:$taskbarDist\Stillbar.Core.map" "/DEF:$taskbarRoot\src\core.def" ole32.lib oleaut32.lib runtimeobject.lib windowsapp.lib user32.lib advapi32.lib d2d1.lib dxguid.lib dwmapi.lib /DYNAMICBASE /NXCOMPAT
    if ($LASTEXITCODE -ne 0) { throw 'Core compilation failed.' }
    & cl.exe @taskbarFlags "$taskbarRoot\src\probe.cpp" "/Fe:$taskbarDist\Stillbar.Probe.exe" /link user32.lib advapi32.lib ole32.lib /DYNAMICBASE /NXCOMPAT
    if ($LASTEXITCODE -ne 0) { throw 'Probe compilation failed.' }
    if (-not $ProbeOnly) {
        & rc.exe /nologo "/I$taskbarRoot\assets" "/fo$taskbarBuild\app.res" "$taskbarRoot\src\app.rc"
        if ($LASTEXITCODE -ne 0) { throw 'Icon/resource compilation failed.' }
        & cl.exe @taskbarFlags /WX "$taskbarRoot\src\main.cpp" "$taskbarBuild\app.res" "/Fe:$taskbarDist\Stillbar.exe" /link user32.lib advapi32.lib ole32.lib oleaut32.lib shell32.lib propsys.lib comctl32.lib gdi32.lib version.lib dwmapi.lib uxtheme.lib /SUBSYSTEM:WINDOWS /MANIFEST:EMBED "/MANIFESTINPUT:$taskbarRoot\src\app.manifest" /DYNAMICBASE /NXCOMPAT
        if ($LASTEXITCODE -ne 0) { throw 'Application compilation failed.' }
        & cl.exe @taskbarFlags /WX "$taskbarRoot\tools\media_diagnostics.cpp" "/Fe:$taskbarDist\Stillbar.MediaDiagnostics.exe" /link runtimeobject.lib windowsapp.lib user32.lib shell32.lib advapi32.lib version.lib /SUBSYSTEM:WINDOWS /DYNAMICBASE /NXCOMPAT
        if ($LASTEXITCODE -ne 0) { throw 'Media diagnostics compilation failed.' }
        & cl.exe @taskbarFlags /WX "$taskbarRoot\src\browser_host.cpp" "/Fe:$taskbarDist\Stillbar.BrowserHost.exe" /link runtimeobject.lib windowsapp.lib user32.lib shell32.lib ole32.lib crypt32.lib /DYNAMICBASE /NXCOMPAT
        if ($LASTEXITCODE -ne 0) { throw 'Browser host compilation failed.' }
        Copy-Item -LiteralPath "$taskbarRoot\browser-extension" -Destination "$taskbarDist\browser-extension" -Recurse
    }
} finally { Pop-Location }
Write-Output "BUILT_OUTPUT=$taskbarDist"
