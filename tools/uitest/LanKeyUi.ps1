# UI driver for LanKey: finds the windows, clicks controls by id, screenshots windows.
# Dot-source this file, then call the functions.
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class UI {
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowW(string cls, string title);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr parent, IntPtr after, string cls, string title);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, StringBuilder l);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern IntPtr GetWindowDC(IntPtr h);
  [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr h, IntPtr dc);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l); public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint from, uint to, bool attach);
  [DllImport("user32.dll")] public static extern bool GetScrollInfo(IntPtr h, int bar, ref SCROLLINFO si);
  [StructLayout(LayoutKind.Sequential)] public struct SCROLLINFO { public uint cbSize; public uint fMask; public int nMin; public int nMax; public uint nPage; public int nPos; public int nTrackPos; }
  [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetFocus();
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("shcore.dll")] public static extern int GetDpiForMonitor(IntPtr mon, int type, out uint x, out uint y);
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr ctx);
  [DllImport("user32.dll")] public static extern IntPtr MonitorFromPoint(POINT p, uint f);
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] inputs, int size);
  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort wVk; public ushort wScan; public uint dwFlags; public uint time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Explicit)] public struct INPUTUNION { [FieldOffset(0)] public KEYBDINPUT ki; [FieldOffset(0)] public long pad1; [FieldOffset(8)] public long pad2; [FieldOffset(16)] public long pad3; [FieldOffset(24)] public long pad4; }
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public INPUTUNION u; }
  public static INPUT Key(ushort vk, bool down) { var i = new INPUT(); i.type = 1; i.u.ki.wVk = vk; i.u.ki.dwFlags = down ? 0u : 2u; i.u.ki.dwExtraInfo = (IntPtr)Tag; return i; }
  public static long Tag = 0;
  public static INPUT Char(char c, bool down) { var i = new INPUT(); i.type = 1; i.u.ki.wScan = c; i.u.ki.dwFlags = 4u | (down ? 0u : 2u); return i; }
  public static uint Send(INPUT[] a) { return SendInput((uint)a.Length, a, Marshal.SizeOf(typeof(INPUT))); }
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; public POINT(int x,int y){X=x;Y=y;} }
}
"@
[void][UI]::SetProcessDpiAwarenessContext([IntPtr](-4)) # per-monitor v2: physical pixels everywhere

$script:LanKeyExe = Join-Path $PSScriptRoot "..\..\build\win-clang\app\Release\lankey.exe"
$script:HWND_MESSAGE = [IntPtr](-3)
$WM_COMMAND = 0x111; $WM_SETTEXT = 0x0C; $WM_GETTEXT = 0x0D; $WM_CLOSE = 0x10
$BM_CLICK = 0xF5; $LB_SETCURSEL = 0x186; $LB_GETCURSEL = 0x188; $CB_SETCURSEL = 0x14E; $CB_GETCURSEL = 0x147
$LVM_GETITEMCOUNT = 0x1004; $EM_SETSEL = 0xB1

function Tray { [UI]::FindWindowExW($script:HWND_MESSAGE, [IntPtr]::Zero, "LanKeyTrayWindow", [NullString]::Value) }
function Win($cls) { [UI]::FindWindowW($cls, [NullString]::Value) }
function Settings { Win "LanKeySettingsWindow" }
function Dialog { Win "LanKeyCustomMethodDialog" }
function MsgBox { [UI]::FindWindowW("#32770", "LanKey") }
function Ctl($win, $id) { [UI]::GetDlgItem($win, $id) }
function Click($win, $id) { $c = Ctl $win $id; if ($c -eq [IntPtr]::Zero) { throw "no control $id" }; [void][UI]::PostMessageW($c, $BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero); Start-Sleep -Milliseconds 400 }
function SetText($win, $id, $text) { $c = Ctl $win $id; [void][UI]::SendMessageW($c, $WM_SETTEXT, [IntPtr]::Zero, [string]$text); Start-Sleep -Milliseconds 100 }
function GetText($h) { $sb = New-Object System.Text.StringBuilder 4096; [void][UI]::SendMessageW($h, $WM_GETTEXT, [IntPtr]4096, $sb); $sb.ToString() }
function CtlText($win, $id) { GetText (Ctl $win $id) }
function Enabled($win, $id) { [UI]::IsWindowEnabled((Ctl $win $id)) }
function SelectNav($win, $index) { $nav = Ctl $win 10; [void][UI]::SendMessageW($nav, $LB_SETCURSEL, [IntPtr]$index, [IntPtr]::Zero); [void][UI]::PostMessageW($win, $WM_COMMAND, [IntPtr](10 -bor (1 -shl 16)), $nav); Start-Sleep -Milliseconds 400 }
function ComboSelect($win, $id, $index) { $c = Ctl $win $id; [void][UI]::SendMessageW($c, $CB_SETCURSEL, [IntPtr]$index, [IntPtr]::Zero); Start-Sleep -Milliseconds 100 }
function ListCount($win, $id) { [int][UI]::SendMessageW((Ctl $win $id), $LVM_GETITEMCOUNT, [IntPtr]::Zero, [IntPtr]::Zero) }
function CloseMsgBox([int]$id = 1) { $m = MsgBox; if ($m -ne [IntPtr]::Zero) { $t = GetText (Ctl $m 0xFFFF); $b = Ctl $m $id; if ($b -ne [IntPtr]::Zero) { [void][UI]::PostMessageW($b, $BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero) } else { [void][UI]::PostMessageW($m, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero) }; Start-Sleep -Milliseconds 500; return $t }; return $null }

function MsgBoxText { $m = MsgBox; if ($m -eq [IntPtr]::Zero) { return $null }; GetText (Ctl $m 0xFFFF) }
function OpenSettings { Start-Process -FilePath $script:LanKeyExe -Wait; Start-Sleep -Milliseconds 800; Settings }
function Shot($h, $path) {
  $r = New-Object UI+RECT; [void][UI]::GetWindowRect($h, [ref]$r)
  $w = $r.R - $r.L; $hh = $r.B - $r.T
  $bmp = New-Object System.Drawing.Bitmap $w, $hh
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size $w, $hh))
  $g.Dispose(); $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
  "$path ($w x $hh)"
}
function MoveWin($h, $x, $y) { [void][UI]::SetWindowPos($h, [IntPtr]::Zero, $x, $y, 0, 0, 0x0001 -bor 0x0004 -bor 0x0010); Start-Sleep -Milliseconds 600 }
function Rect($h) { $r = New-Object UI+RECT; [void][UI]::GetWindowRect($h, [ref]$r); "{0},{1} {2}x{3} dpi={4}" -f $r.L, $r.T, ($r.R-$r.L), ($r.B-$r.T), [UI]::GetDpiForWindow($h) }
function Monitors { [System.Windows.Forms.Screen]::AllScreens | ForEach-Object { $b = $_.Bounds; $p = New-Object UI+POINT ($b.X + 10), ($b.Y + 10); $m = [UI]::MonitorFromPoint($p, 2); $x = 0; $y = 0; [void][UI]::GetDpiForMonitor($m, 0, [ref]$x, [ref]$y); "{0} bounds={1} dpi={2} primary={3}" -f $_.DeviceName, $b, $x, $_.Primary } }
function Json { Get-Content "$env:APPDATA\LanKey\settings.json" -Raw -Encoding UTF8 | ConvertFrom-Json }

# Click posts BM_CLICK, so the app writes settings.json a moment later: reading it straight
# away is a race that fails a passing feature. Waits for the value to arrive, then returns
# the whole document - so a check still fails, and prints the last value it saw, when the
# setting really never lands.
#   JsonWhen { param($j) $j.engine.codeTable -eq "unicode" }
function JsonWhen([scriptblock]$ready, [int]$timeoutMs = 2000) {
    $deadline = [DateTime]::Now.AddMilliseconds($timeoutMs)
    $j = $null
    while ([DateTime]::Now -lt $deadline) {
        try { $j = Json } catch { $j = $null }   # a half-written file parses next time
        if ($null -ne $j -and (& $ready $j)) { return $j }
        PumpMs 60
    }
    return $j
}


function CloseAllMsgBoxes { for ($i = 0; $i -lt 10; $i++) { if ((MsgBox) -eq [IntPtr]::Zero) { break }; CloseMsgBox | Out-Null } }


# Waits while keeping any WinForms window of this script responsive (a test target that is
# not pumping messages would receive the keys late and fail the timing-sensitive checks).
function PumpMs([int]$ms) { $end = [DateTime]::Now.AddMilliseconds($ms); while ([DateTime]::Now -lt $end) { [System.Windows.Forms.Application]::DoEvents(); Start-Sleep -Milliseconds 15 } }

# Real keyboard input through SendInput (untagged, so LanKey's hook treats it as the user).
# Chord: modifiers held while the key is pressed, e.g. Chord @(0x11, 0x12) 0x46 = Ctrl+Alt+F.
function Chord([int[]]$mods, [int]$vk) {
    $list = New-Object System.Collections.Generic.List[UI+INPUT]
    foreach ($m in $mods) { $list.Add([UI]::Key([uint16]$m, $true)) }
    $list.Add([UI]::Key([uint16]$vk, $true)); $list.Add([UI]::Key([uint16]$vk, $false))
    foreach ($m in ($mods | Sort-Object -Descending)) { $list.Add([UI]::Key([uint16]$m, $false)) }
    $n = [UI]::Send($list.ToArray()); if ($n -ne $list.Count) { throw "SendInput sent $n of $($list.Count)" }; PumpMs 350
}
function TypeText([string]$text) {
    $list = New-Object System.Collections.Generic.List[UI+INPUT]
    foreach ($c in $text.ToCharArray()) { $list.Add([UI]::Char($c, $true)); $list.Add([UI]::Char($c, $false)) }
    $n = [UI]::Send($list.ToArray()); if ($n -ne $list.Count) { throw "SendInput sent $n of $($list.Count)" }; PumpMs (50 + 10 * $text.Length)
}
$VK_CONTROL = 0x11; $VK_MENU = 0x12; $VK_SHIFT = 0x10; $VK_END = 0x23; $VK_DELETE = 0x2E

# Give a control of another process the keyboard focus without touching the mouse:
# attach to its input queue, SetFocus, detach. Returns true when the control has it.
function FocusCtl($h) {
    $target = [UI]::GetWindowThreadProcessId($h, [ref]([uint32]0))
    $mine = [UI]::GetCurrentThreadId()
    [void][UI]::AttachThreadInput($mine, $target, $true)
    [void][UI]::SetFocus($h)
    $ok = ([UI]::GetFocus() -eq $h)
    [void][UI]::AttachThreadInput($mine, $target, $false)
    PumpMs 150
    return $ok
}

# SetForegroundWindow is refused to a background process unless it shares the input
# queue of the current foreground thread: attach, raise, detach.
function ForceForeground($h) {
    $mine = [UI]::GetCurrentThreadId()
    $fgThread = [UI]::GetWindowThreadProcessId([UI]::GetForegroundWindow(), [ref]([uint32]0))
    $target = [UI]::GetWindowThreadProcessId($h, [ref]([uint32]0))
    [void][UI]::AttachThreadInput($mine, $fgThread, $true)
    [void][UI]::AttachThreadInput($mine, $target, $true)
    [void][UI]::SetForegroundWindow($h)
    [void][UI]::AttachThreadInput($mine, $target, $false)
    [void][UI]::AttachThreadInput($mine, $fgThread, $false)
    PumpMs 250
    return ([UI]::GetForegroundWindow() -eq $h)
}

# Foreground with retries. GetForegroundWindow briefly returns 0 while Windows shuffles
# z-order (after a toast appears, say), and keys sent in that window go nowhere - which
# looks exactly like a failing feature. Throws rather than typing into the wrong place.
function EnsureForeground($h) {
    for ($i = 0; $i -lt 6 -and [UI]::GetForegroundWindow() -ne $h; $i++) {
        [void](ForceForeground $h); PumpMs 250
    }
    if ([UI]::GetForegroundWindow() -ne $h) { throw "target window is not in the foreground" }
}

# Vertical scroll state of a window: @(pos, min, max, page). page = 0 means "no scrollbar".
function ScrollInfo($h) {
    $si = New-Object UI+SCROLLINFO
    $si.cbSize = [uint32][System.Runtime.InteropServices.Marshal]::SizeOf([type][UI+SCROLLINFO])
    $si.fMask = 0x17   # SIF_RANGE | SIF_PAGE | SIF_POS | SIF_TRACKPOS
    [void][UI]::GetScrollInfo($h, 1, [ref]$si)   # SB_VERT
    return @($si.nPos, $si.nMin, $si.nMax, [int]$si.nPage)
}
function Scrollable($h) { $i = ScrollInfo $h; return ($i[2] - $i[1] + 1) -gt $i[3] -and $i[3] -gt 1 }

# Screenshot of a window's own pixels (PW_RENDERFULLCONTENT). Unlike CopyFromScreen this
# works when the window is behind others - or when the screen is locked.
function ShotWindow($h, $path) {
    $r = New-Object UI+RECT; [void][UI]::GetWindowRect($h, [ref]$r)
    $w = $r.R - $r.L; $ht = $r.B - $r.T
    $bmp = New-Object System.Drawing.Bitmap $w, $ht
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $dc = $g.GetHdc()
    [void][UI]::PrintWindow($h, $dc, 2)
    $g.ReleaseHdc($dc); $g.Dispose()
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
    "$path ($w x $ht)"
}

# Writes settings.json the way an editor does (temporary file + rename) and waits until the
# running LanKey has reacted. There is no "reload" button: saving the file is the API.
# LanKey does not rewrite the file when it applies it (that would fight the editor), so the
# signal is its log, not the file.
function WriteSettings([string]$json, [int]$timeoutMs = 6000) {
    $log = "$env:APPDATA\LanKey\lankey.log"
    $before = (Get-Content $log).Count
    $file = "$env:APPDATA\LanKey\settings.json"
    $tmp = "$file.uitest.tmp"
    [IO.File]::WriteAllText($tmp, $json, (New-Object Text.UTF8Encoding $false))
    Move-Item $tmp $file -Force
    $deadline = [DateTime]::Now.AddMilliseconds($timeoutMs)
    while ([DateTime]::Now -lt $deadline) {
        PumpMs 150
        $new = (Get-Content $log) | Select-Object -Skip $before
        if ($new -match "settings: (applied from file|reload failed|file matches)") { PumpMs 300; return $true }
    }
    return $false
}

# A run interrupted while settings.json held half-written JSON would break every later run.
# Recover by handing the app an empty object (it then holds all defaults) and making it
# write the file out in full - an empty file parses, but has no keys for a test to edit.
function EnsureValidSettings {
    $file = "$env:APPDATA\LanKey\settings.json"
    $ok = $false
    try { $ok = $null -ne ((Get-Content $file -Raw -Encoding UTF8) | ConvertFrom-Json).suggestions } catch { }
    if ($ok) { return $true }
    [void](WriteSettings "{}")
    $s = Settings
    if ($s -eq [IntPtr]::Zero) { $s = OpenSettings }
    SelectNav $s 1          # Tuỳ chọn gõ
    Click $s 111; PumpMs 300   # toggle a setting and back: each change makes the app save
    Click $s 111; PumpMs 300
    return $false
}
