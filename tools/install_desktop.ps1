$ErrorActionPreference = 'Stop'
$repoPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$launcherPath = Join-Path $repoPath 'start-console.vbs'
if (-not (Test-Path -LiteralPath $launcherPath)) { throw 'Extract the entire Passport package before installing.' }
$shortcutShell = New-Object -ComObject WScript.Shell
$shortcutFolders = @([Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('Programs'))
foreach ($shortcutFolder in $shortcutFolders) {
    $shortcut = $shortcutShell.CreateShortcut((Join-Path $shortcutFolder 'AI 用量桌伴.lnk'))
    $shortcut.TargetPath = Join-Path $env:WINDIR 'System32\wscript.exe'
    $shortcut.Arguments = '"' + $launcherPath + '"'
    $shortcut.WorkingDirectory = $repoPath
    $shortcut.IconLocation = (Join-Path $PSScriptRoot 'console\passport.ico') + ',0'
    $shortcut.Description = 'AI 用量桌伴控制台'
    $shortcut.Save()
}
Write-Host 'AI 用量桌伴快捷方式已创建。'
Write-Host '请保留此目录，并使用快捷方式启动。'
Read-Host 'Press Enter to close'
