# Text-tool pages: the personal glossary, the snippets and the clipboard history.
#
# These three are pure Win32 with no unit tests behind them - the add/edit/delete flows
# exist only here - so this walks each one end to end and checks that what the window did
# reached settings.json. Run with LanKey running, outside a sandbox.
. "$PSScriptRoot\LanKeyUi.ps1"
$out = Join-Path $PSScriptRoot "shots"; New-Item -ItemType Directory -Force $out | Out-Null
$results = @()
function Check($name, $cond, $detail = "") { $script:results += ("{0} {1} {2}" -f ($(if ($cond) { "PASS" } else { "FAIL" }), $name, $detail)) }

EnsureValidSettings | Out-Null   # never start from a file the app cannot read
Copy-Item "$env:APPDATA\LanKey\settings.json" "$out\settings.backup.json" -Force
CloseAllMsgBoxes
$s = Settings; if ($s -eq [IntPtr]::Zero) { $s = OpenSettings }

# Control ids, from ui/win32/SettingsWindow.cpp.
$GLOSS_LIST = 351; $GLOSS_ADD = 353; $GLOSS_EDIT = 354; $GLOSS_DEL = 355
$SNIP_LIST = 361; $SNIP_ADD = 363; $SNIP_EDIT = 364; $SNIP_DEL = 365; $SNIP_VARS = 366
$CLIP_ON = 370; $CLIP_LIST = 371; $CLIP_PIN = 373; $CLIP_DEL = 374; $CLIP_CLEAR = 375

# Selects list row $row with a posted mouse click. Sending LVM_SETITEMSTATE from here would
# hand the control a pointer into THIS process and crash it (learned the hard way).
function PickRow($win, $id, [int]$row) {
  $list = Ctl $win $id
  $LVM_GETNEXTITEM = 0x100C
  for ($y = 40; $y -lt 400; $y += 8) {
    $lp = [IntPtr](($y -shl 16) -bor 30)
    [void][UI]::PostMessageW($list, 0x201, [IntPtr]1, $lp)
    [void][UI]::PostMessageW($list, 0x202, [IntPtr]0, $lp)
    Start-Sleep -Milliseconds 120
    if ([int][UI]::SendMessageW($list, $LVM_GETNEXTITEM, [IntPtr](-1), [IntPtr]2) -eq $row) { return $true }
  }
  return $false
}

# ---- glossary --------------------------------------------------------------------------------
SelectPage $s "Từ điển cá nhân"; Start-Sleep -Milliseconds 400
$before = ListCount $s $GLOSS_LIST
Check "Sửa/Xoá off with no row" ((-not (Enabled $s $GLOSS_EDIT)) -and (-not (Enabled $s $GLOSS_DEL)))

Click $s $GLOSS_ADD; Start-Sleep -Milliseconds 800
$ed = Win "LanKeyRowEditor"
Check "add opens the editor" ($ed -ne [IntPtr]::Zero -and [UI]::IsWindowVisible($ed))
Check "settings disabled while it is up" (-not [UI]::IsWindowEnabled($s))
SetText $ed 100 "máy chủ thử"; SetText $ed 101 "smoke server"; SetText $ed 102 "サーバー"
Click $ed 1; Start-Sleep -Milliseconds 1000
Check "row added to the list" ((ListCount $s $GLOSS_LIST) -eq ($before + 1)) (ListCount $s $GLOSS_LIST)
$j = Json
Check "row reached settings.json" (($j.glossary | Where-Object { $_.en -eq "smoke server" }) -ne $null)

Check "row can be selected" (PickRow $s $GLOSS_LIST ($before))
Check "Sửa on with a row" (Enabled $s $GLOSS_EDIT)
Click $s $GLOSS_EDIT; Start-Sleep -Milliseconds 800
$ed = Win "LanKeyRowEditor"
Check "edit preloads the row" ((CtlText $ed 101) -eq "smoke server") (CtlText $ed 101)
SetText $ed 103 "ghi chú thử"
Click $ed 1; Start-Sleep -Milliseconds 1000
Check "edit reached settings.json" ((Json).glossary | Where-Object { $_.note -eq "ghi chú thử" }) -ne $null

Check "row selectable again" (PickRow $s $GLOSS_LIST ($before))
Click $s $GLOSS_DEL; Start-Sleep -Milliseconds 700
$t = CloseMsgBox 1
Check "delete asks first" ($t -like "*Xoá*") "$t"
Start-Sleep -Milliseconds 900
Check "row gone again" ((ListCount $s $GLOSS_LIST) -eq $before) (ListCount $s $GLOSS_LIST)
Shot $s "$out\page-glossary.png" | Out-Null

# ---- snippets --------------------------------------------------------------------------------
SelectPage $s "Gõ tắt"; Start-Sleep -Milliseconds 400
$before = ListCount $s $SNIP_LIST
Click $s $SNIP_ADD; Start-Sleep -Milliseconds 800
$ed = Win "LanKeyRowEditor"
SetText $ed 100 "smk"
SetText $ed 101 "dòng một`r`ndòng hai"
Click $ed 1; Start-Sleep -Milliseconds 1000
Check "snippet added" ((ListCount $s $SNIP_LIST) -eq ($before + 1)) (ListCount $s $SNIP_LIST)
$body = ((Json).snippets.items | Where-Object { $_.abbr -eq "smk" }).body
# The editor speaks CRLF and everything else speaks LF; a stray \r would be typed out as a
# character into the user's document.
Check "body stored with LF, no CR" ($body -eq "dòng một`ndòng hai") ([regex]::Escape($body))

Click $s $SNIP_VARS; Start-Sleep -Milliseconds 800
$ed = Win "LanKeyRowEditor"
SetText $ed 100 "smoke = một`r`ndate = hai"
Click $ed 1; Start-Sleep -Milliseconds 700
$t = MsgBoxText
Check "reserved variable refused" ($t -like "*{date}*") "$t"
CloseMsgBox 1 | Out-Null
Start-Sleep -Milliseconds 800
$vars = (Json).snippets.variables
Check "good variable kept" ($vars.smoke -eq "một") "$($vars.smoke)"
Check "reserved variable dropped" ($vars.PSObject.Properties.Name -notcontains "date")

Check "snippet selectable" (PickRow $s $SNIP_LIST ($before))
Click $s $SNIP_DEL; Start-Sleep -Milliseconds 700
CloseMsgBox 1 | Out-Null
Start-Sleep -Milliseconds 900
Check "snippet gone" ((ListCount $s $SNIP_LIST) -eq $before) (ListCount $s $SNIP_LIST)
Shot $s "$out\page-snippets.png" | Out-Null

# ---- clipboard history -----------------------------------------------------------------------
SelectPage $s "Clipboard"; Start-Sleep -Milliseconds 400
# Set the state, do not assume it: an earlier run (or the user) may have left it either
# way, and a check that only holds on a fresh profile is a check that fails for the wrong
# reason. The default itself is pinned by DataPolicy.ClipboardHistoryIsOffUntilAskedFor.
if ((Json).clipboard.enabled) { Click $s $CLIP_ON; Start-Sleep -Milliseconds 700 }
Check "switch turns the history off" (-not (Json).clipboard.enabled)
Click $s $CLIP_ON; Start-Sleep -Milliseconds 800
Check "switch turns it back on" ((Json).clipboard.enabled)

# Setting the clipboard needs an STA apartment, which this shell is not.
function SetClip([string]$text) {
  & powershell -STA -NoProfile -Command "Add-Type -AssemblyName System.Windows.Forms; [System.Windows.Forms.Clipboard]::SetText('$text')" 2>$null | Out-Null
  Start-Sleep -Milliseconds 700
}
SetClip "smoke mot"; SetClip "smoke hai"; SetClip "smoke mot"
Start-Sleep -Milliseconds 600
Check "three copies, one repeated -> two rows" ((ListCount $s $CLIP_LIST) -eq 2) (ListCount $s $CLIP_LIST)

Check "clipboard row selectable" (PickRow $s $CLIP_LIST 0)
Click $s $CLIP_PIN; Start-Sleep -Milliseconds 900
Check "pin writes the sealed file" (Test-Path "$env:APPDATA\LanKey\clipboard.enc")
Shot $s "$out\page-clipboard.png" | Out-Null

Click $s $CLIP_CLEAR; Start-Sleep -Milliseconds 700
$t = CloseMsgBox 1
Check "clear asks first" ($t -like "*ghim*") "$t"
Start-Sleep -Milliseconds 900
Check "history emptied" ((ListCount $s $CLIP_LIST) -eq 0) (ListCount $s $CLIP_LIST)
Check "sealed file removed with the pins" (-not (Test-Path "$env:APPDATA\LanKey\clipboard.enc"))

# ---- put the user's own settings back --------------------------------------------------------
# The history goes back to off whatever the snapshot said: this run switched it on, and
# leaving a clipboard recorder running because a test turned it on would be indefensible.
$restored = Get-Content "$out\settings.backup.json" -Raw -Encoding UTF8 | ConvertFrom-Json
$restored.clipboard.enabled = $false
WriteSettings ($restored | ConvertTo-Json -Depth 10)
Start-Sleep -Milliseconds 800
Check "history left off" (-not (Json).clipboard.enabled)
Check "other settings restored" ((Json).engine.inputMethod -eq $restored.engine.inputMethod)
$results
# The exit code follows the checks, not whatever external command ran last, so this can be
# used from a build step.
exit (@($results | Where-Object { $_ -like "FAIL*" }).Count)
