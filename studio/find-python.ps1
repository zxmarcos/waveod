# The newest Python 3.10+ on this computer, for the .bat launchers: the PATH's, the py launcher's and the usual
# install folders, each asked for its version (folder names sort 38 after 313). Prints its path, or nothing.
$found = @()
foreach ($name in 'python', 'python3') {
    Get-Command $name -All -ErrorAction SilentlyContinue | ForEach-Object { $found += $_.Source }
}
if (Get-Command py -ErrorAction SilentlyContinue) {
    py -0p 2>$null | ForEach-Object { if ($_ -match '([A-Za-z]:\\.*python\.exe)') { $found += $Matches[1] } }
}
foreach ($pattern in "$env:LOCALAPPDATA\Programs\Python\Python3*", "$env:ProgramFiles\Python3*", 'C:\Python3*',
                     'C:\Python\Python3*', 'D:\Python\Python3*') {
    Get-ChildItem $pattern -Directory -ErrorAction SilentlyContinue | ForEach-Object {
        $found += Join-Path $_.FullName 'python.exe'
    }
}
$best = $null
$bestVersion = [version]'3.10'
foreach ($exe in ($found | Where-Object { $_ -and $_ -notmatch 'WindowsApps' -and (Test-Path $_) } | Select-Object -Unique)) {
    $v = & $exe -c "import sys; print('%d.%d' % sys.version_info[:2])" 2>$null
    if ($v -and [version]$v -ge $bestVersion) {
        $bestVersion = [version]$v
        $best = $exe
    }
}
if ($best) { $best }
