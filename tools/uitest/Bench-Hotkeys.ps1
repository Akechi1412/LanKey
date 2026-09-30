# How long a selection hotkey takes, end to end (PLAN Giai đoạn 5: <= 300 ms).
#
# This lives here and not in tests/bench because there is nothing to measure without a real
# window holding a real selection: the hotkey works by sending Ctrl+C, reading the
# clipboard, and sending Ctrl+V back. The numbers come from LanKey's own log, which times
# the whole round trip from the hook to the replacement landing.
#
# Run with LanKey running, outside a sandbox, on an unlocked desktop.
. "$PSScriptRoot\LanKeyUi.ps1"
$LIMIT_MS = 300
$RUNS = 5

# A window of our own: measuring against the user's editor would mean typing into it.
$script = @'
Add-Type -AssemblyName System.Windows.Forms
$f = New-Object System.Windows.Forms.Form
$f.Text = "LanKeyBenchTarget"; $f.Width = 700; $f.Height = 200
$t = New-Object System.Windows.Forms.TextBox
$t.Multiline = $true; $t.Width = 640; $t.Height = 110; $t.Left = 20; $t.Top = 20
$f.Controls.Add($t)
$f.Add_Shown({ $t.Focus() })
[System.Windows.Forms.Application]::Run($f)
'@
$targetScript = Join-Path $env:TEMP "lankey-bench-target.ps1"
$script | Set-Content $targetScript -Encoding UTF8
Start-Process powershell -ArgumentList "-NoProfile","-WindowStyle","Hidden","-File",$targetScript
Start-Sleep -Seconds 3
$w = [UI]::FindWindowW([NullString]::Value, "LanKeyBenchTarget")
if ($w -eq [IntPtr]::Zero) { throw "bench target window did not open" }
# [NullString]::Value, not $null: PowerShell marshals $null for a string parameter as an
# empty string, and FindWindowEx then looks for a child whose class name is "" and finds
# none. A zero handle here means every SendMessage below goes nowhere and the bench ends up
# timing the "nothing was selected" path instead of a conversion.
$edit = [UI]::FindWindowExW($w, [IntPtr]::Zero, [NullString]::Value, [NullString]::Value)
if ($edit -eq [IntPtr]::Zero) { throw "bench target has no edit control" }

# Every measurement starts from the same text, selected, in the foreground.
function Arm([string]$text) {
  [void][UI]::SendMessageW($edit, 0x000C, [IntPtr]::Zero, [string]$text)   # WM_SETTEXT
  if (-not (ForceForeground $w)) { throw "bench target is not in the foreground" }
  Start-Sleep -Milliseconds 300
  [void][UI]::SendMessageW($edit, 0x00B1, [IntPtr]0, [IntPtr](-1))         # EM_SETSEL all
  Start-Sleep -Milliseconds 200
}

# The log line each hotkey writes ends with "-> <result> in <n> ms".
function RunOne([string]$label, [int[]]$mods, [int]$vk, [string]$text, [string]$marker) {
  Arm $text
  $before = (Get-Content "$env:APPDATA\LanKey\lankey.log").Count
  Chord $mods $vk
  Start-Sleep -Milliseconds 1500
  $line = (Get-Content "$env:APPDATA\LanKey\lankey.log") | Select-Object -Skip $before |
          Where-Object { $_ -like "*$marker*" } | Select-Object -Last 1
  if (-not $line) { return -1 }
  if ($line -match 'in (\d+) ms') { return [int]$Matches[1] }
  return -1
}

$VK_CONTROL = 0x11; $VK_ALT = 0x12
$cases = @(
  @{ label = "Ctrl+Alt+E  (VI -> EN)"; vk = 0x45; text = "đăng nhập"; marker = "convert to en" },
  @{ label = "Ctrl+Alt+J  (VI -> JA)"; vk = 0x4A; text = "đăng nhập"; marker = "convert to ja" },
  @{ label = "Ctrl+Alt+F  (half/full)"; vk = 0x46; text = "abc123";   marker = "convertWidth" }
)

$results = @()
foreach ($c in $cases) {
  $samples = @()
  for ($i = 0; $i -lt $RUNS; $i++) {
    $ms = RunOne $c.label @($VK_CONTROL, $VK_ALT) $c.vk $c.text $c.marker
    if ($ms -ge 0) { $samples += $ms }
  }
  if ($samples.Count -eq 0) {
    $results += ("FAIL {0}: no log line - is the glossary loaded and the desktop unlocked?" -f $c.label)
    continue
  }
  $worst = ($samples | Measure-Object -Maximum).Maximum
  $mean = [int](($samples | Measure-Object -Average).Average)
  $verdict = if ($worst -le $LIMIT_MS) { "PASS" } else { "FAIL" }
  $results += ("{0} {1}  mean {2} ms, worst {3} ms of {4} runs (limit {5} ms)" -f
               $verdict, $c.label, $mean, $worst, $samples.Count, $LIMIT_MS)
}

Get-Process powershell -ErrorAction SilentlyContinue |
  Where-Object { $_.MainWindowTitle -eq "LanKeyBenchTarget" } | Stop-Process -Force
Remove-Item $targetScript -ErrorAction SilentlyContinue
$results
exit (@($results | Where-Object { $_ -like "FAIL*" }).Count)
