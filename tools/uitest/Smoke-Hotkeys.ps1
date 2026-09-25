# Hotkey page smoke test: record a chord in a field with real keys, check settings.json,
# reject a duplicate, restore defaults. Run with LanKey running, outside a sandbox.
. "$PSScriptRoot\LanKeyUi.ps1"
$out = Join-Path $PSScriptRoot "shots"; New-Item -ItemType Directory -Force $out | Out-Null
$results = @()
function Check($name, $cond, $detail = "") { $script:results += ("{0} {1} {2}" -f ($(if ($cond) { "PASS" } else { "FAIL" }), $name, $detail)) }

EnsureValidSettings | Out-Null   # never start from a file the app cannot read
Copy-Item "$env:APPDATA\LanKey\settings.json" "$out\settings.backup.json" -Force
CloseAllMsgBoxes
$s = Settings; if ($s -eq [IntPtr]::Zero) { $s = OpenSettings }
SelectNav $s 5   # Phím tắt
# Bring the window to front so real keys land in the field, never anywhere else.
if (-not (ForceForeground $s)) { throw "Settings window is not in the foreground; aborting" }
Shot $s "$out\page-hotkeys.png" | Out-Null

# Start from a known state: a run interrupted while a field was cleared would otherwise
# leave the next run testing against an unassigned hotkey.
Click $s 620   # Mặc định
Start-Sleep -Milliseconds 400
Check "starts from defaults" ((Json).hotkeys.convertWidth -eq "Ctrl+Alt+F") (Json).hotkeys.convertWidth

$field = Ctl $s 601   # convertWidth
Check "field focused" (FocusCtl $field)
Check "field shows its value" ((CtlText $s 601) -eq "Ctrl+Alt+F") (CtlText $s 601)
Chord @() 0x08                                 # Backspace clears the assignment
Start-Sleep -Milliseconds 400
Check "cleared field prompts" ((CtlText $s 601) -eq "Nhấn tổ hợp phím") (CtlText $s 601)
Check "cleared in json" ((Json).hotkeys.convertWidth -eq "") "[$((Json).hotkeys.convertWidth)]"
Chord @() 0x1B                                 # Esc restores the value it had on focus
Start-Sleep -Milliseconds 400
Check "esc restores" ((CtlText $s 601) -eq "Ctrl+Alt+F") (CtlText $s 601)
Check "esc restores json" ((Json).hotkeys.convertWidth -eq "Ctrl+Alt+F") (Json).hotkeys.convertWidth
Chord @($VK_CONTROL, $VK_MENU) 0x47      # Ctrl+Alt+G
Start-Sleep -Milliseconds 400
Check "field shows chord" ((CtlText $s 601) -eq "Ctrl+Alt+G") (CtlText $s 601)
Check "json updated" ((Json).hotkeys.convertWidth -eq "Ctrl+Alt+G") (Json).hotkeys.convertWidth

Chord @($VK_CONTROL, $VK_MENU) 0x4C      # Ctrl+Alt+L: taken by convertLanguage
Start-Sleep -Milliseconds 400
$t = CloseMsgBox
Check "duplicate refused" ($t -like "*đã dùng cho*") "$t"
Check "field kept previous" ((CtlText $s 601) -eq "Ctrl+Alt+G") (CtlText $s 601)
Check "json kept previous" ((Json).hotkeys.convertWidth -eq "Ctrl+Alt+G")

# The new chord works end to end: Ctrl+Alt+G now toggles width (log line appears).
$before = (Get-Content "$env:APPDATA\LanKey\lankey.log").Count
Chord @($VK_CONTROL, $VK_MENU) 0x47
Start-Sleep -Milliseconds 1200
$new = (Get-Content "$env:APPDATA\LanKey\lankey.log") | Select-Object -Skip $before | Out-String
Check "new chord reaches the app" ($new -like "*convertWidth*") ($new.Trim())

Click $s 620   # Mặc định
Check "defaults restored" ((Json).hotkeys.convertWidth -eq "Ctrl+Alt+F") (Json).hotkeys.convertWidth
Check "field shows default" ((CtlText $s 601) -eq "Ctrl+Alt+F") (CtlText $s 601)

# Give the other settings back to the user, but leave the hotkeys at the documented
# defaults: the snapshot taken at the start may itself have come from an interrupted run.
WriteSettings (Get-Content "$out\settings.backup.json" -Raw -Encoding UTF8)
SelectNav $s 5; Click $s 620   # Mặc định
Start-Sleep -Milliseconds 300
Check "left at defaults" ((Json).hotkeys.convertWidth -eq "Ctrl+Alt+F") (Json).hotkeys.convertWidth
$results
