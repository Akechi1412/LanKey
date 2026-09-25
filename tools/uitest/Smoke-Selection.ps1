# Selection-transform smoke test. A plain WinForms text box owned by this script is the
# target (no user application is touched): type into it, select all, press a real Ctrl+Alt+F
# through SendInput (untagged, so LanKey's hook sees a genuine hotkey), and check the text
# toggled to full-width and back while the user's clipboard survived.
# Run with LanKey running, outside any sandbox that hides windows.
. "$PSScriptRoot\LanKeyUi.ps1"

$form = New-Object System.Windows.Forms.Form
$form.Text = "LanKey selection test"
$form.Width = 520; $form.Height = 220
$form.StartPosition = "CenterScreen"
$box = New-Object System.Windows.Forms.TextBox
$box.Multiline = $true; $box.Dock = "Fill"; $box.Font = New-Object System.Drawing.Font("Segoe UI", 14)
$form.Controls.Add($box)
$form.Show(); $form.Activate(); $box.Focus() | Out-Null
function Pump([int]$ms) { $end = [DateTime]::Now.AddMilliseconds($ms); while ([DateTime]::Now -lt $end) { [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 20 } }
Pump 300
# Raising a window from a background process needs the input-queue trick; retry a few
# times, and never type if the test window is not the one that would receive the keys.
for ($i = 0; $i -lt 5 -and [UI]::GetForegroundWindow() -ne $form.Handle; $i++) {
    [void](ForceForeground $form.Handle); $form.Activate(); Pump 300
}
if ([UI]::GetForegroundWindow() -ne $form.Handle) { $form.Close(); throw "test window did not get the foreground; aborting to avoid typing elsewhere" }
$box.Focus() | Out-Null; Pump 200

$results = @()
function Check($name, $cond, $detail = "") { $script:results += ("{0} {1} {2}" -f ($(if ($cond) { "PASS" } else { "FAIL" }), $name, $detail)) }

Set-Clipboard -Value "SENTINEL"
TypeText "hello 123"; Pump 300
Check "typed" ($box.Text -eq "hello 123") $box.Text
$box.SelectAll(); Pump 100
Chord @($VK_CONTROL, $VK_MENU) 0x46; Pump 1200   # Ctrl+Alt+F
Check "to full-width" ($box.Text -eq "ｈｅｌｌｏ　１２３") $box.Text
Check "clipboard restored" ((Get-Clipboard -Raw) -eq "SENTINEL") (Get-Clipboard -Raw)

$box.SelectAll(); Pump 100
Chord @($VK_CONTROL, $VK_MENU) 0x46; Pump 1200
Check "back to half-width" ($box.Text -eq "hello 123") $box.Text

# Nothing selected: text and clipboard untouched.
$box.Select($box.Text.Length, 0); Pump 100
Set-Clipboard -Value "SENTINEL2"
Chord @($VK_CONTROL, $VK_MENU) 0x46; Pump 1200
Check "no selection leaves text" ($box.Text -eq "hello 123") $box.Text
Check "no selection leaves clipboard" ((Get-Clipboard -Raw) -eq "SENTINEL2") (Get-Clipboard -Raw)

# Katakana with voiced marks. (A WinForms multiline TextBox ignores Ctrl+A, so select
# programmatically; keyboard selection is covered by the Notepad-style apps in real use.)
$box.Text = "ｶﾞｷﾞ ﾊﾟ"; $box.Focus() | Out-Null; $box.SelectAll(); Pump 300
Chord @($VK_CONTROL, $VK_MENU) 0x46; Pump 1200
Check "kana to full-width" ($box.Text -eq "ガギ　パ") $box.Text

$form.Close()
$results
