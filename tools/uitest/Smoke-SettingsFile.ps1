# settings.json as the editing surface: saving the file in an editor must apply to the
# running app and show in the Settings window without pressing anything, LanKey's own saves
# must not bounce back, and invalid JSON must be ignored quietly until the next good save.
# Only sends window messages and writes files, so it also runs while the screen is locked.
. "$PSScriptRoot\LanKeyUi.ps1"
$results = @()
function Check($name, $cond, $detail = "") { $script:results += ("{0} {1} {2}" -f ($(if ($cond) { "PASS" } else { "FAIL" }), $name, $detail)) }

$file = "$env:APPDATA\LanKey\settings.json"
$backup = "$env:TEMP\lankey-settings-file-test.json"
Copy-Item $file $backup -Force

CloseAllMsgBoxes
$s = Settings; if ($s -eq [IntPtr]::Zero) { $s = OpenSettings }
SelectNav $s 2   # Gợi ý & Tự sửa: idleDelayMs is visible there
PumpMs 300

function SaveJson($mutate) {
    # Write the way an editor does: a temporary file renamed over the original.
    $j = Get-Content $file -Raw -Encoding UTF8 | ConvertFrom-Json
    & $mutate $j
    $tmp = "$file.tmp"
    [IO.File]::WriteAllText($tmp, ($j | ConvertTo-Json -Depth 10), (New-Object Text.UTF8Encoding $false))
    Move-Item $tmp $file -Force
}
function WaitFor($predicate, [int]$ms = 4000) {
    $deadline = [DateTime]::Now.AddMilliseconds($ms)
    while ([DateTime]::Now -lt $deadline) { if (& $predicate) { return $true }; PumpMs 150 }
    return $false
}

# 1. An external save is applied without anyone pressing "Tải lại".
SaveJson { param($j) $j.suggestions.idleDelayMs = 411 }
$applied = WaitFor { (CtlText $s 201) -eq "411" }
Check "external save reaches the UI" $applied "field=[$(CtlText $s 201)]"
Check "external save reaches the settings" ((Json).suggestions.idleDelayMs -eq 411) (Json).suggestions.idleDelayMs

# 2. ... including values the engine uses, not just the ones on screen. (The check boxes
#    are owner-drawn, so their state lives in the app, not in the control: verify through
#    the file the app writes back after applying.)
SaveJson { param($j) $j.engine.modernToneMark = $false }
$applied = WaitFor { (Json).engine.modernToneMark -eq $false }
Check "engine setting applied" $applied
SaveJson { param($j) $j.engine.modernToneMark = $true }
$applied = WaitFor { (Json).engine.modernToneMark -eq $true }
Check "engine setting applied back" $applied

# 3. LanKey's own save must not come back as an external change: change something in the
#    UI and make sure the file is not re-applied in a loop (the value stays put).
SetText $s 201 "333"; [void][UI]::SendMessageW($s, 0x0111, [IntPtr](201 -bor (0x0200 -shl 16)), (Ctl $s 201))
PumpMs 1200
Check "own save does not bounce" ((Json).suggestions.idleDelayMs -eq 333) (Json).suggestions.idleDelayMs
Check "field kept the typed value" ((CtlText $s 201) -eq "333") (CtlText $s 201)

# 4. Half-written JSON is ignored quietly; the next good save is applied.
[IO.File]::WriteAllText($file, '{ "suggestions": { "idleDelayMs": ', (New-Object Text.UTF8Encoding $false))
PumpMs 1200
Check "broken json changes nothing" ((CtlText $s 201) -eq "333") (CtlText $s 201)
Check "broken json shows no dialog" ((MsgBox) -eq [IntPtr]::Zero) "msgbox=$(MsgBox)"
Copy-Item $backup $file -Force
[IO.File]::WriteAllText($file, ([IO.File]::ReadAllText($backup)).Replace('"idleDelayMs": 350', '"idleDelayMs": 377'), (New-Object Text.UTF8Encoding $false))
$applied = WaitFor { (CtlText $s 201) -eq "377" }
Check "recovers on the next good save" $applied "field=[$(CtlText $s 201)]"

# Put the user's settings back and make sure that lands too.
Copy-Item $backup $file -Force
[void](WaitFor { (Json).suggestions.idleDelayMs -eq ((Get-Content $backup -Raw | ConvertFrom-Json).suggestions.idleDelayMs) })
$results
