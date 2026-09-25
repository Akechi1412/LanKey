# Smoke test of the Settings window and the custom-method dialog against a running LanKey:
# every page, every kind of control, settings.json round trip, reset, and a drag between two
# monitors of different DPI (adjust the coordinates below to your monitors). Restores the
# settings it touched. Prints PASS/FAIL per check and writes screenshots to shots/.
. "$PSScriptRoot\LanKeyUi.ps1"
$out = Join-Path $PSScriptRoot "shots"
New-Item -ItemType Directory -Force $out | Out-Null
$results = @()
function Check($name, $cond, $detail = "") { $script:results += ("{0} {1} {2}" -f ($(if ($cond) { "PASS" } else { "FAIL" }), $name, $detail)) }

EnsureValidSettings | Out-Null   # never start from a file the app cannot read
Copy-Item "$env:APPDATA\LanKey\settings.json" "$out\settings.backup.json" -Force

CloseAllMsgBoxes

# ---- custom method dialog ------------------------------------------------------------------
$s = Settings
if ($s -eq [IntPtr]::Zero) { $s = OpenSettings }
$d = Dialog
if ($d -eq [IntPtr]::Zero -or -not [UI]::IsWindowVisible($d)) { SelectNav $s 0; Click $s 105; Start-Sleep -Milliseconds 500; $d = Dialog }
Check "dialog opens" ($d -ne [IntPtr]::Zero -and [UI]::IsWindowVisible($d))
Check "owner disabled while dialog up" (-not [UI]::IsWindowEnabled($s))

ComboSelect $d 100 0; Click $d 101            # Telex preset
Check "preset Telex -> 13 rows" ((ListCount $d 106) -eq 13) (ListCount $d 106)
ComboSelect $d 100 3; Click $d 101            # Telex + VNI
Check "preset Telex+VNI -> 24 rows" ((ListCount $d 106) -eq 24) (ListCount $d 106)
ComboSelect $d 102 8; SetText $d 103 "["; Click $d 104   # add [ to horn
Check "add row -> 25" ((ListCount $d 106) -eq 25) (ListCount $d 106)
SetText $d 103 "["; Click $d 104              # duplicate: no new row
Check "duplicate add keeps 25" ((ListCount $d 106) -eq 25) (ListCount $d 106)
SetText $d 103 "]"; Click $d 104; SetText $d 103 "-"; Click $d 104  # horn now w,8,[,],- = 5 > 4
$t = CloseMsgBox
Check "5th key refused with message" ($t -ne $null -and $t -like "*4 phím*") "$t"
Check "rows still 26" ((ListCount $d 106) -eq 26) (ListCount $d 106)
SetText $d 103 "s"; ComboSelect $d 102 1; Click $d 104   # s already sắc: duplicate across functions -> allowed to add, caught at OK
Click $d 111                                   # Đồng ý -> conflict
$t = CloseMsgBox
Check "conflict refused at OK" ($t -ne $null -and $t -like "*hai tính năng*") "$t"
Check "dialog still open after refusal" ([UI]::IsWindowVisible($d))
Click $d 110                                   # Xoá tất cả
Check "remove all -> 0" ((ListCount $d 106) -eq 0)
Click $d 111
$t = CloseMsgBox
Check "empty refused at OK" ($t -ne $null -and $t -like "*Chưa có phím*") "$t"
Click $d 109                                   # Xoá phím with nothing selected
$t = CloseMsgBox
Check "remove without selection explains" ($t -ne $null) "$t"
ComboSelect $d 100 0; Click $d 101             # Telex again
Shot $d "$out\dialog.png" | Out-Null
Click $d 111                                   # Đồng ý
Start-Sleep -Milliseconds 600
Check "dialog closed" (-not [UI]::IsWindowVisible($d))
Check "owner re-enabled" ([UI]::IsWindowEnabled($s))
$j = Json
Check "json custom keys" ($j.engine.customKeys -eq "s,f,r,x,j,a,o,e,w,d,z,[,]") $j.engine.customKeys
Check "json input method custom" ($j.engine.inputMethod -eq "custom") $j.engine.inputMethod
Check "summary label" ((CtlText $s 104) -like "*S  F  R*[[]  ]*") (CtlText $s 104)

# ---- typing page toggles --------------------------------------------------------------------
Click $s 101; $j = JsonWhen { param($j) $j.engine.inputMethod -eq "vni" }; Check "radio VNI -> json" ($j.engine.inputMethod -eq "vni") $j.engine.inputMethod
Click $s 100; $j = JsonWhen { param($j) $j.engine.inputMethod -eq "telex" }; Check "radio Telex -> json" ($j.engine.inputMethod -eq "telex") $j.engine.inputMethod
Click $s 108; $j = JsonWhen { param($j) $j.engine.codeTable -eq "tcvn3" }; Check "code table TCVN3" ($j.engine.codeTable -eq "tcvn3") $j.engine.codeTable
Click $s 106; $j = JsonWhen { param($j) $j.engine.codeTable -eq "unicode" }; Check "code table Unicode" ($j.engine.codeTable -eq "unicode") $j.engine.codeTable
Shot $s "$out\page-typing.png" | Out-Null

SelectNav $s 1
$before = (Json).engine.modernToneMark
Click $s 111; $j = JsonWhen { param($j) $j.engine.modernToneMark -ne $before }; Check "checkbox modern tone toggles" ($j.engine.modernToneMark -ne $before) "$before -> $($j.engine.modernToneMark)"
Click $s 111; $j = JsonWhen { param($j) $j.engine.modernToneMark -eq $before }; Check "checkbox toggles back" ($j.engine.modernToneMark -eq $before)
Shot $s "$out\page-options.png" | Out-Null

SelectNav $s 2
Check "select-with-Enter defaults to off" ((Json).suggestions.selectWithEnter -eq $false) "$((Json).suggestions.selectWithEnter)"
Click $s 210; $j = JsonWhen { param($j) $j.suggestions.selectWithEnter -eq $true }; Check "select-with-Enter on" ($j.suggestions.selectWithEnter -eq $true) "$($j.suggestions.selectWithEnter)"
Click $s 210; $j = JsonWhen { param($j) $j.suggestions.selectWithEnter -eq $false }; Check "select-with-Enter off again" ($j.suggestions.selectWithEnter -eq $false) "$($j.suggestions.selectWithEnter)"
Click $s 206; $j = JsonWhen { param($j) $j.autoCorrect.level -eq "balanced" }; Check "level balanced" ($j.autoCorrect.level -eq "balanced") $j.autoCorrect.level
Click $s 205; $j = JsonWhen { param($j) $j.autoCorrect.level -eq "cautious" }; Check "level cautious" ($j.autoCorrect.level -eq "cautious") $j.autoCorrect.level
Click $s 209                                   # refresh recent
Shot $s "$out\page-smart.png" | Out-Null

SelectNav $s 3; SetText $s 300 "a"; Start-Sleep -Milliseconds 300; Click $s 310; Start-Sleep -Milliseconds 800
Shot $s "$out\page-dictionary.png" | Out-Null
SelectNav $s 4; Shot $s "$out\page-privacy.png" | Out-Null
Click $s 404; Start-Sleep -Milliseconds 600; $dd = Win "LanKeyDataDialog"
Check "data dialog opens" ($dd -ne [IntPtr]::Zero -and [UI]::IsWindowVisible($dd))
if ($dd -ne [IntPtr]::Zero) { Shot $dd "$out\data-dialog.png" | Out-Null; [void][UI]::SendMessageW($dd, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero) }
SelectNav $s 5; Shot $s "$out\page-shortcuts.png" | Out-Null
SelectNav $s 6; Click $s 504; Start-Sleep -Milliseconds 500; Shot $s "$out\page-advanced.png" | Out-Null
SelectNav $s 7; Shot $s "$out\page-about.png" | Out-Null

# ---- settings.json round trip ---------------------------------------------------------------
SelectNav $s 6
$logBefore = (Get-Content "$env:APPDATA\LanKey\lankey.log").Count
Click $s 506; Start-Sleep -Milliseconds 2500
# Which editor opens depends on the machine (a code editor if one is installed, else the
# .json association, else Notepad), so check what the app itself reports: a ShellExecute
# result above 32 means it launched.
$opened = (Get-Content "$env:APPDATA\LanKey\lankey.log") | Select-Object -Skip $logBefore |
    Select-String "settings: open file" | Select-Object -Last 1
$rc = 0
if ($opened -and $opened.Line -match "-> (\d+)$") { $rc = [int]$Matches[1] }
Check "open settings.json launches an editor" ($rc -gt 32) "$($opened.Line)"
$np = Get-Process notepad -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowTitle -like "*settings.json*" }
if ($np) { $np | Stop-Process -Force }   # close Notepad only, never the user's code editor
$raw = Get-Content "$env:APPDATA\LanKey\settings.json" -Raw -Encoding UTF8
$raw2 = $raw -replace '"idleDelayMs":\s*\d+', '"idleDelayMs": 444'
WriteSettings $raw2
SelectNav $s 2
Check "reload from file updates control" ((CtlText $s 201) -eq "444") (CtlText $s 201)
SelectNav $s 6
WriteSettings "{ not json"
Check "broken json shows no dialog" ((MsgBox) -eq [IntPtr]::Zero) "msgbox=$(MsgBox)"
Check "broken json leaves the setting alone" ((CtlText $s 201) -eq "444") (CtlText $s 201)
Click $s 508; Start-Sleep -Milliseconds 400   # reset defaults -> confirm
$t = CloseMsgBox 1
Check "reset asks" ($t -like "*mặc định*") "$t"
Start-Sleep -Milliseconds 500
$j = Json
Check "reset -> defaults" ($j.engine.inputMethod -eq "telex" -and $j.suggestions.idleDelayMs -eq 350) "$($j.engine.inputMethod) $($j.suggestions.idleDelayMs)"

# ---- DPI: drag between monitors --------------------------------------------------------------
MoveWin $s 300 -900    # DISPLAY2 (96 dpi)
Start-Sleep -Milliseconds 800
Check "moved to 96 dpi" ((Rect $s) -like "*dpi=96*") (Rect $s)
Shot $s "$out\dpi96.png" | Out-Null
MoveWin $s 400 100     # back to DISPLAY1 (120 dpi)
Start-Sleep -Milliseconds 800
Check "back to 120 dpi" ((Rect $s) -like "*dpi=120*") (Rect $s)
Shot $s "$out\dpi120.png" | Out-Null

# Give the other settings back to the user, but put the hotkeys at the documented defaults
# rather than at whatever the snapshot held: a snapshot taken during an interrupted run
# carries cleared hotkeys, and restoring it here wiped the user's real ones on every run
# from then on (found 2026-09-25).
WriteSettings (Get-Content "$out\settings.backup.json" -Raw -Encoding UTF8)
SelectNav $s 5; Click $s 620   # Phim tat -> Mac dinh
Start-Sleep -Milliseconds 400
Check "hotkeys left at defaults" ((Json).hotkeys.convertLanguage -eq "Ctrl+Alt+L") (Json).hotkeys.convertLanguage
$results
