param(
    [string]$Rom = '',
    [string]$Framework = '',
    [string]$Ui = '',
    [string]$Oracle = '',
    [string]$Build = 'build',
    [string]$Toolchain = 'C:\msys64\mingw64\bin',
    [string]$Python = '',
    [switch]$NoUi,
    [switch]$Production
)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(!$Python) { $Python=(Get-Command python.exe -ErrorAction Stop).Source }
if(!$Rom) { $Rom=Join-Path $projectRoot 'roms\zero_racers.vb' }
if(!$Framework) { $Framework=Join-Path $projectRoot 'vbrecomp' }
if(!$Ui) { $Ui=Join-Path $projectRoot 'recomp-ui' }
if(!$Oracle) { $Oracle=Join-Path $projectRoot 'beetle-vb' }
foreach($path in @($Rom,$Framework,$Python,(Join-Path $Toolchain 'cmake.exe'),(Join-Path $Toolchain 'ninja.exe'),
    (Join-Path $Toolchain 'gcc.exe'),(Join-Path $Toolchain 'g++.exe'))) {
    if(!(Test-Path -LiteralPath $path)) { throw "Missing dependency: $path" }
}
if(!$NoUi -and !(Test-Path -LiteralPath (Join-Path $Ui 'recomp_ui.cmake'))) {
    throw 'recomp-ui is missing. Run git submodule update --init --recursive.'
}
$expected='47421cd82dfd414d042d2f7f9db51e657d414d4ddfa46887efb544029fa11ce7'
if((Get-FileHash -LiteralPath $Rom -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) { throw 'Wrong Zero Racers cartridge revision' }
$Rom=(Resolve-Path -LiteralPath $Rom).Path
$Framework=(Resolve-Path -LiteralPath $Framework).Path
$buildPath=[IO.Path]::GetFullPath((Join-Path $projectRoot $Build))
$previousPath=$env:PATH
function Invoke-Native([string]$Executable,[string[]]$Arguments) {
    $previousPreference=$ErrorActionPreference
    try {
        $ErrorActionPreference='Continue'
        & $Executable @Arguments
        $nativeExit=$LASTEXITCODE
    } finally { $ErrorActionPreference=$previousPreference }
    if($nativeExit -ne 0) { throw "$Executable failed ($nativeExit)" }
}
try {
    $env:PATH=$Toolchain+';'+$env:PATH
    Push-Location -LiteralPath $Framework
    try {
        Invoke-Native $Python @('-m','recompiler.cli.vbrecomp_codegen','--rom',$Rom,'--module','zero_racers',
            '--out',(Join-Path $projectRoot 'generated'),'--seeds-toml',(Join-Path $projectRoot 'zero_racers.toml'))
    } finally { Pop-Location }
    $options=@('-S',$projectRoot,'-B',$buildPath,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',
        "-DCMAKE_MAKE_PROGRAM=$Toolchain/ninja.exe","-DCMAKE_C_COMPILER=$Toolchain/gcc.exe",
        "-DCMAKE_CXX_COMPILER=$Toolchain/g++.exe","-DPython3_EXECUTABLE=$Python",
        "-DVBRECOMP_ROOT=$Framework","-DRECOMP_UI_ROOT=$Ui","-DBEETLE_VB_ROOT=$Oracle",
        ('-DZERO_RACERS_UI='+$(if($NoUi){'OFF'}else{'ON'})),
        ('-DVBRECOMP_DEBUG_TOOLS='+$(if($Production){'OFF'}else{'ON'})),
        ('-DVBRECOMP_CPUHOOK='+$(if($Production){'OFF'}else{'ON'})))
    Invoke-Native (Join-Path $Toolchain 'cmake.exe') $options
    Invoke-Native (Join-Path $Toolchain 'cmake.exe') @('--build',$buildPath,'--parallel','8')
    Invoke-Native (Join-Path $Toolchain 'ctest.exe') @('--test-dir',$buildPath,'--output-on-failure')
} finally { $env:PATH=$previousPath }
