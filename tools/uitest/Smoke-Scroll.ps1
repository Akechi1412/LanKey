# Settings window scrolling: a page taller than the window must scroll (wheel, scrollbar,
# keyboard) and every control must be reachable; a page that fits must not scroll. Also
# checks the window can be resized and that scrolling resets when switching pages.
# Run with LanKey running, outside any sandbox that hides windows.
. "$PSScriptRoot\LanKeyUi.ps1"
$out = Join-Path $PSScriptRoot "shots"; New-Item -ItemType Directory -Force $out | Out-Null
$results = @()
function Check($name, $cond, $detail = "") { $script:results += ("{0} {1} {2}" -f ($(if ($cond) { "PASS" } else { "FAIL" }), $name, $detail)) }

CloseAllMsgBoxes
$s = Settings; if ($s -eq [IntPtr]::Zero) { $s = OpenSettings }
# No foreground needed: this suite only sends window messages (wheel, scrollbar, nav) and
# reads rectangles, so it also runs while the screen is locked.

# Resize and wait for the size to actually take effect (a window busy re-laying out can
# report the old rect for a while).
function ResizeTo([int]$w, [int]$h) {
    $cur = New-Object UI+RECT; [void][UI]::GetWindowRect($s, [ref]$cur)
    [void][UI]::SetWindowPos($s, [IntPtr]::Zero, $cur.L, $cur.T, $w, $h, 0x0004)  # SWP_NOZORDER
    $deadline = [DateTime]::Now.AddSeconds(3)
    while ([DateTime]::Now -lt $deadline) {
        $now = New-Object UI+RECT; [void][UI]::GetWindowRect($s, [ref]$now)
        if (($now.R - $now.L) -eq $w -and ($now.B - $now.T) -eq $h) { PumpMs 200; return $true }
        PumpMs 100
    }
    return $false
}

# Start from a known place. Another suite may have left the window on a monitor with a
# different scale, where the same height holds a different amount of the page - which made
# this suite pass alone and fail when it ran second.
MoveWin $s 200 120
$r = New-Object UI+RECT; [void][UI]::GetWindowRect($s, [ref]$r)
$originalW = $r.R - $r.L; $originalH = $r.B - $r.T
$narrow = ResizeTo 900 540
Check "width is clamped to the design width" (-not $narrow) "asking for 900 wide must be refused"
Check "window is resizable in height" (ResizeTo $originalW 540) "asked for $originalW x540"

function CtlBottom($id) {
    $c = Ctl $s $id
    if ($c -eq [IntPtr]::Zero) { return -1 }
    $cr = New-Object UI+RECT; [void][UI]::GetWindowRect($c, [ref]$cr)
    return $cr.B
}
function ClientBottom {
    $cr = New-Object UI+RECT; [void][UI]::GetWindowRect($s, [ref]$cr)
    return $cr.B
}

SelectNav $s 6   # Nâng cao: the tallest page
PumpMs 300
$lastButton = 508   # "Đặt lại mặc định"
$bottomBefore = CtlBottom $lastButton
Check "long page is scrollable" (Scrollable $s) ("info=" + ((ScrollInfo $s) -join ","))
Check "last control starts off screen" ($bottomBefore -gt (ClientBottom)) "button bottom=$bottomBefore window bottom=$(ClientBottom)"
ShotWindow $s "$out\scroll-top.png" | Out-Null

# Wheel over the content area.
$cr = New-Object UI+RECT; [void][UI]::GetWindowRect($s, [ref]$cr)
[void][UI]::SendMessageW($s, 0x020A, [IntPtr](-120 * 65536), [IntPtr](($cr.L + 500) -bor (($cr.T + 300) -shl 16)))
PumpMs 300
$bottomAfterWheel = CtlBottom $lastButton
Check "wheel scrolls down" ($bottomAfterWheel -lt $bottomBefore) "$bottomBefore -> $bottomAfterWheel"

# Scroll all the way down through the scrollbar (WM_VSCROLL, SB_BOTTOM = 7).
[void][UI]::SendMessageW($s, 0x0115, [IntPtr]7, [IntPtr]::Zero)
PumpMs 300
$bottomEnd = CtlBottom $lastButton
Check "scrolled to the bottom shows the last control" ($bottomEnd -le (ClientBottom)) "button bottom=$bottomEnd window bottom=$(ClientBottom)"
ShotWindow $s "$out\scroll-bottom.png" | Out-Null

# Back to the top (SB_TOP = 6).
[void][UI]::SendMessageW($s, 0x0115, [IntPtr]6, [IntPtr]::Zero)
PumpMs 300
Check "back to the top" ((CtlBottom $lastButton) -gt (ClientBottom))

# Switching pages resets the scroll: scroll down, switch away and back.
[void][UI]::SendMessageW($s, 0x0115, [IntPtr]7, [IntPtr]::Zero)
PumpMs 200
SelectNav $s 0; PumpMs 200; SelectNav $s 6; PumpMs 300
Check "page switch resets scroll" ((CtlBottom $lastButton) -gt (ClientBottom))

# Tall window: the page now fits, so the scrollbar goes away and the wheel does nothing.
Check "window grows" (ResizeTo $originalW 980) "asked for $originalW x980"
SelectNav $s 6; PumpMs 400
Check "tall window shows the last control" ((CtlBottom $lastButton) -le (ClientBottom)) "button bottom=$(CtlBottom $lastButton) window bottom=$(ClientBottom)"
Check "tall window has no scrollbar" (-not (Scrollable $s)) ("info=" + ((ScrollInfo $s) -join ","))
$fitsBottom = CtlBottom $lastButton
[void][UI]::SendMessageW($s, 0x020A, [IntPtr](-120 * 65536), [IntPtr](($cr.L + 500) -bor (($cr.T + 300) -shl 16)))
PumpMs 300
Check "page that fits ignores the wheel" ((CtlBottom $lastButton) -eq $fitsBottom) "$fitsBottom -> $(CtlBottom $lastButton)"

# Every page is reachable at the small size: each one either fits or offers a scrollbar,
# and scrolling to the bottom never leaves a gap below the last control.
[void](ResizeTo $originalW 540)
for ($page = 0; $page -lt 8; $page++) {
    SelectNav $s $page; PumpMs 250
    $info = ScrollInfo $s
    $fits = -not (Scrollable $s)
    [void][UI]::SendMessageW($s, 0x0115, [IntPtr]7, [IntPtr]::Zero)   # SB_BOTTOM
    PumpMs 200
    $atBottom = ScrollInfo $s
    $ok = $fits -or ($atBottom[0] -ge ($atBottom[2] - $atBottom[3] + 1))
    Check "page $page reachable" $ok ("fits=$fits info=" + ($atBottom -join ","))
}
[void](ResizeTo $originalW $originalH)
$results
