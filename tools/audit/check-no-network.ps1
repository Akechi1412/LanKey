# Fails when a LanKey binary imports any networking DLL. DATA-POLICY.md promises the app
# has no network code; this keeps the promise checkable on every build (CI runs it on the
# release exe). Walks the PE import directory - a plain string search would trip over the
# GUID name tables that libuuid links in (IID_IWinHttpRequest and friends).
param([Parameter(Mandatory = $true)][string]$Binary)

$forbidden = @("ws2_32.dll", "wsock32.dll", "winhttp.dll", "wininet.dll", "urlmon.dll",
               "dnsapi.dll", "iphlpapi.dll", "webservices.dll", "mswsock.dll", "rpcrt4.dll")

$bytes = [System.IO.File]::ReadAllBytes($Binary)
$u16 = { param($o) [BitConverter]::ToUInt16($bytes, $o) }
$u32 = { param($o) [BitConverter]::ToUInt32($bytes, $o) }

$peOffset = & $u32 0x3C
if ((& $u32 $peOffset) -ne 0x00004550) { throw "not a PE file: $Binary" }
$coff = $peOffset + 4
$sectionCount = & $u16 ($coff + 2)
$optSize = & $u16 ($coff + 16)
$opt = $coff + 20
$magic = & $u16 $opt
$dataDirOffset = if ($magic -eq 0x20B) { $opt + 112 } else { $opt + 96 }
$importRva = & $u32 ($dataDirOffset + 8)   # directory entry 1 = import table
$sections = @()
$sec = $opt + $optSize
for ($i = 0; $i -lt $sectionCount; $i++) {
    $s = $sec + $i * 40
    $sections += [pscustomobject]@{
        VirtualAddress = (& $u32 ($s + 12)); VirtualSize = (& $u32 ($s + 8))
        RawOffset = (& $u32 ($s + 20)); RawSize = (& $u32 ($s + 16))
    }
}
function ToOffset([uint32]$rva) {
    foreach ($s in $sections) {
        if ($rva -ge $s.VirtualAddress -and $rva -lt ($s.VirtualAddress + [Math]::Max($s.VirtualSize, $s.RawSize))) {
            return $rva - $s.VirtualAddress + $s.RawOffset
        }
    }
    throw "RVA $rva outside every section"
}
function ReadCString([int]$offset) {
    $end = $offset
    while ($bytes[$end] -ne 0) { $end++ }
    return [System.Text.Encoding]::ASCII.GetString($bytes, $offset, $end - $offset)
}

$imports = @()
if ($importRva -ne 0) {
    $desc = ToOffset $importRva
    while ($true) {
        $nameRva = & $u32 ($desc + 12)
        if ($nameRva -eq 0) { break }
        $imports += (ReadCString (ToOffset $nameRva)).ToLowerInvariant()
        $desc += 20
    }
}
$hits = $imports | Where-Object { $forbidden -contains $_ }
Write-Output ("imports: " + ($imports -join ", "))
if ($hits) {
    Write-Error ("networking DLL imported by " + $Binary + ": " + ($hits -join ", "))
    exit 1
}
Write-Output ("OK: no networking imports in " + $Binary)
exit 0
