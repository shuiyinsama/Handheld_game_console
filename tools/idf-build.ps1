param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$IdfArgs = @("build")
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$EspressifRoot = "D:\1workandstudy\Espressif"
$IdfPath = Join-Path $EspressifRoot "frameworks\esp-idf-v5.5.1"
$PythonEnv = Join-Path $EspressifRoot "python_env\idf5.5_py3.11_env"

$env:IDF_PATH = $IdfPath
$env:ESP_IDF_VERSION = "5.5"
$env:IDF_PYTHON_ENV_PATH = $PythonEnv
$env:OPENOCD_SCRIPTS = Join-Path $EspressifRoot "tools\openocd-esp32\v0.12.0-esp32-20250707\openocd-esp32\share\openocd\scripts"
$env:ESP_ROM_ELF_DIR = Join-Path $EspressifRoot "tools\esp-rom-elfs\20241011"
$env:GIT_CONFIG_GLOBAL = Join-Path $ProjectRoot ".gitconfig-build"
$env:CC = Join-Path $EspressifRoot "tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin\xtensa-esp32s3-elf-gcc.exe"
$env:CXX = Join-Path $EspressifRoot "tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin\xtensa-esp32s3-elf-g++.exe"
$env:ASM = $env:CC

$toolPaths = @(
    (Join-Path $IdfPath "components\espcoredump"),
    (Join-Path $IdfPath "components\partition_table"),
    (Join-Path $IdfPath "components\app_update"),
    (Join-Path $EspressifRoot "tools\xtensa-esp-elf-gdb\16.2_20250324\xtensa-esp-elf-gdb\bin"),
    (Join-Path $EspressifRoot "tools\riscv32-esp-elf-gdb\16.2_20250324\riscv32-esp-elf-gdb\bin"),
    (Join-Path $EspressifRoot "tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin"),
    (Join-Path $EspressifRoot "tools\esp-clang\esp-19.1.2_20250312\esp-clang\bin"),
    (Join-Path $EspressifRoot "tools\riscv32-esp-elf\esp-14.2.0_20241119\riscv32-esp-elf\bin"),
    (Join-Path $EspressifRoot "tools\esp32ulp-elf\2.38_20240113\esp32ulp-elf\bin"),
    (Join-Path $EspressifRoot "tools\cmake\3.30.2\bin"),
    (Join-Path $EspressifRoot "tools\openocd-esp32\v0.12.0-esp32-20250707\openocd-esp32\bin"),
    (Join-Path $EspressifRoot "tools\ninja\1.12.1"),
    (Join-Path $EspressifRoot "tools\idf-exe\1.0.3"),
    (Join-Path $EspressifRoot "tools\ccache\4.11.2\ccache-4.11.2-windows-x86_64"),
    (Join-Path $EspressifRoot "tools\dfu-util\0.11\dfu-util-0.11-win64"),
    (Join-Path $PythonEnv "Scripts"),
    (Join-Path $IdfPath "tools")
)

$basePaths = @(
    "$env:SystemRoot\system32",
    "$env:SystemRoot",
    "$env:SystemRoot\System32\Wbem",
    "$env:SystemRoot\System32\WindowsPowerShell\v1.0",
    "D:\tool\git\Git\cmd"
)

$env:PATH = (($toolPaths + $basePaths) -join ";")

Push-Location $ProjectRoot
try {
    & (Join-Path $PythonEnv "Scripts\python.exe") (Join-Path $IdfPath "tools\idf.py") @IdfArgs
    exit $LASTEXITCODE
} finally {
    Pop-Location
}
