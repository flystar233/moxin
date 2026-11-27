# 把 tools/usbput.py 打成单文件 exe（双击即用，不需要装 Python）
# 用法（仓库根目录）：powershell -ExecutionPolicy Bypass -File tools\build_exe.ps1
$ErrorActionPreference = 'Stop'
$py = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
if (!(Test-Path $py)) { throw "没有找到 PlatformIO 的 Python：$py" }

& $py -m pip install --quiet pyinstaller
& $py -m PyInstaller --noconfirm --onefile --console --name "墨芯传书" `
    --distpath dist --workpath build\pyinstaller --specpath build tools\usbput.py

Write-Host ""
Write-Host "生成好了：dist\墨芯传书.exe（双击运行，浏览器会自动打开）"
