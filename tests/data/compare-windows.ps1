# Cross-check tests/data/width-pairs.tsv against Windows' own 全角/半角 conversion
# (LCMapStringEx with LCMAP_FULLWIDTH / LCMAP_HALFWIDTH, locale ja-JP) - the mapping Word,
# Excel and the Japanese IME use. Run it after changing the table or WidthConverter:
#
#   powershell -File tests/data/compare-windows.ps1
#
# Expected output (23/09/2026, Windows 11 26200): "192 pairs: 1 disagreement" - the
# backslash, see WidthConformance.DeliberateDeviationsFromWindows - and 54 code points
# Windows converts that the table omits: 52 Hangul jamo, ￦ and ヾ, all deliberate.
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class LC {
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode)]
  public static extern int LCMapStringEx(string locale, uint flags, string src, int srcLen,
      StringBuilder dest, int destLen, IntPtr ver, IntPtr reserved, IntPtr sortHandle);
}
"@
$FLAG_FULL = 0x00800000
$FLAG_HALF = 0x00400000
function Map([string]$s, [uint32]$flag) {
    $sb = New-Object System.Text.StringBuilder 64
    $n = [LC]::LCMapStringEx("ja-JP", $flag, $s, $s.Length, $sb, 64, [IntPtr]::Zero, [IntPtr]::Zero, [IntPtr]::Zero)
    if ($n -le 0) { return $null }
    return $sb.ToString().Substring(0, $n)
}
function Hexes([string]$s) { ($s.ToCharArray() | ForEach-Object { "{0:X4}" -f [int]$_ }) -join " " }
function FromHexes([string]$f) { -join ($f.Split(" ") | ForEach-Object { [char][Convert]::ToInt32($_, 16) }) }

$rows = Get-Content "C:\Work\LanKey\tests\data\width-pairs.tsv" | Where-Object { $_ -and $_[0] -ne "#" }
$diffFull = 0; $diffHalf = 0; $checked = 0
foreach ($row in $rows) {
    $parts = $row.Split("`t")
    $half = FromHexes $parts[0]; $full = FromHexes $parts[1]; $note = $parts[2]
    $checked++
    $winFull = Map $half $FLAG_FULL
    $winHalf = Map $full $FLAG_HALF
    if ($winFull -ne $full) {
        $diffFull++
        "FULL  $note  half=[$(Hexes $half)]  lankey=[$(Hexes $full)]  windows=[$(Hexes $winFull)]"
    }
    if ($winHalf -ne $half) {
        $diffHalf++
        "HALF  $note  full=[$(Hexes $full)]  lankey=[$(Hexes $half)]  windows=[$(Hexes $winHalf)]"
    }
}
"checked $checked pairs: $diffFull disagreements half->full, $diffHalf half<-full"

# And the other way round: every code point Windows converts that LanKey's table omits.
$tableHalf = @{}; $tableFull = @{}
foreach ($row in $rows) { $p = $row.Split("`t"); $tableHalf[$p[0]] = $true; $tableFull[$p[1]] = $true }
$missing = 0
foreach ($cp in 0x20..0xFFEF) {
    if ($cp -ge 0x100 -and $cp -lt 0x3000) { continue }
    if ($cp -ge 0x3100 -and $cp -lt 0xFF00) { continue }
    $c = [char]$cp
    $wf = Map $c $FLAG_FULL; $wh = Map $c $FLAG_HALF
    $key = "{0:X4}" -f $cp
    if ($wf -and $wf -ne $c -and -not $tableHalf.ContainsKey($key)) {
        $missing++; "MISSING half->full  U+$key -> [$(Hexes $wf)]"
    }
    if ($wh -and $wh -ne $c -and -not $tableFull.ContainsKey($key)) {
        $missing++; "MISSING full->half  U+$key -> [$(Hexes $wh)]"
    }
}
"code points Windows converts but the table omits: $missing"
