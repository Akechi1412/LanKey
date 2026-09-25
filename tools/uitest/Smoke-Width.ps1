# Width conversion end to end: the cases most likely to break in a real editor - voiced
# katakana, the yen sign, an irregular voiced form, a long paragraph, and text with nothing
# convertible. Types into a text box owned by this script; no user application is touched.
# Run with LanKey running, outside any sandbox that hides windows.
#
# Every step waits for the state it needs (foreground, the text actually changing) instead
# of sleeping a fixed amount: synthetic input is delivered when Windows feels like it, and
# a fixed sleep turns that into a flaky test.
. "$PSScriptRoot\LanKeyUi.ps1"

$form = New-Object System.Windows.Forms.Form
$form.Text = "LanKey width test"; $form.Width = 640; $form.Height = 260
$form.StartPosition = "CenterScreen"
$box = New-Object System.Windows.Forms.TextBox
$box.Multiline = $true; $box.Dock = "Fill"; $box.Font = New-Object System.Drawing.Font("Segoe UI", 12)
$form.Controls.Add($box)
$form.Show(); $form.Activate(); $box.Focus() | Out-Null
function Pump([int]$ms) { $end = [DateTime]::Now.AddMilliseconds($ms); while ([DateTime]::Now -lt $end) { [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 20 } }
Pump 300
EnsureForeground $form.Handle
$box.Focus() | Out-Null; Pump 200

$results = @()
function Check($name, $cond, $detail = "") { $script:results += ("{0} {1} {2}" -f ($(if ($cond) { "PASS" } else { "FAIL" }), $name, $detail)) }

# Selects everything, presses Ctrl+Alt+F and waits for the text to change (up to 4 s).
# Retries once: a chord sent while Windows is still settling the foreground is simply lost.
function Convert($expectChange = $true) {
    for ($attempt = 0; $attempt -lt 2; $attempt++) {
        EnsureForeground $form.Handle
        $box.Focus() | Out-Null; $box.SelectAll(); Pump 250
        $before = $box.Text
        Chord @($VK_CONTROL, $VK_MENU) 0x46      # Ctrl+Alt+F
        $deadline = [DateTime]::Now.AddSeconds(4)
        while ([DateTime]::Now -lt $deadline -and $box.Text -eq $before) { Pump 100 }
        if (-not $expectChange -or $box.Text -ne $before) { return $box.Text }
    }
    return $box.Text
}

# name, input, expected after one Ctrl+Alt+F
$cases = @(
    @("ascii+kana",    "hello ｶﾞｷﾞ 123",      "ｈｅｌｌｏ　ガギ　１２３"),
    @("yen sign",      "¥1,000 (税込)",        "￥１，０００　（税込）"),
    @("irregular kana", "ｳﾞｧｲｵﾘﾝ ﾜﾞ",         "ヴァイオリン　ヷ"),
    @("punctuation",   "｡｢hello｣､ ･ ｰ",        "。「ｈｅｌｌｏ」、　・　ー"),
    @("nothing to do", "日本語ひらがな漢字",    "日本語ひらがな漢字")
)
foreach ($c in $cases) {
    EnsureForeground $form.Handle
    $box.Text = $c[1]; $box.Focus() | Out-Null; Pump 200
    $after = Convert ($c[2] -ne $c[1])
    Check $c[0] ($after -eq $c[2]) "[$after]"
    if ($c[2] -ne $c[1]) {
        $back = Convert
        Check "$($c[0]) round trip" ($back -eq $c[1]) "[$back]"
    }
}

# A long selection: converted once, and byte for byte the same after converting back.
$long = ("ｱｲｳ ABC 123 ｶﾞｷﾞ 日本語 xyz " * 200)
EnsureForeground $form.Handle
$box.Text = $long; $box.Focus() | Out-Null; Pump 300
$started = [DateTime]::Now
$converted = Convert
$elapsed = [int]([DateTime]::Now - $started).TotalMilliseconds
Check "long selection converted" ($converted -ne $long -and $converted.Length -gt 0) "len=$($converted.Length) in $elapsed ms"
$back = Convert
Check "long selection round trip" ($back -eq $long) "len=$($back.Length)"

$form.Close()
$results
