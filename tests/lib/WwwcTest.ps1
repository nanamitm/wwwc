## Helpers for the WWWC tests: Win32 window access, a test environment per case,
## the local test server and simple assertions.
## The tests drive WWWC through window messages (no mouse or keyboard input),
## so they do not depend on the foreground window or the system locale.

Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class WwwcWin {
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet = CharSet.Ansi, EntryPoint = "SendMessageA")] public static extern IntPtr SendMessageText(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageW")] public static extern IntPtr SendMessageTextW(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hh, bool r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }

  // a descendant window with the given control id (controls on child dialogs)
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr h, EnumProc f, IntPtr l);
  [DllImport("user32.dll")] static extern int GetDlgCtrlID(IntPtr h);
  public static IntPtr FindChildById(IntPtr parent, int id) {
    IntPtr found = IntPtr.Zero;
    EnumChildWindows(parent, (h, l) => { if (GetDlgCtrlID(h) == id) { found = h; return false; } return true; }, IntPtr.Zero);
    return found;
  }

  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);

  // window text as Unicode (WM_GETTEXT is marshaled across processes)
  [DllImport("user32.dll", CharSet = CharSet.Unicode, EntryPoint = "SendMessageW")] static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, StringBuilder l);
  public static string GetTextW(IntPtr h) {
    int n = (int)SendMessage(h, 0x000E, IntPtr.Zero, IntPtr.Zero);   // WM_GETTEXTLENGTH
    var sb = new StringBuilder(n + 1);
    SendMessageW(h, 0x000D, (IntPtr)(n + 1), sb);                    // WM_GETTEXT
    return sb.ToString();
  }
}

// Cross-process access to WWWC's list view (ANSI, 32-bit or 64-bit WWWC)
public static class WwwcList {
  [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint a, bool i, uint pid);
  [DllImport("kernel32.dll")] static extern IntPtr VirtualAllocEx(IntPtr p, IntPtr a, UIntPtr s, uint t, uint pr);
  [DllImport("kernel32.dll")] static extern bool VirtualFreeEx(IntPtr p, IntPtr a, UIntPtr s, uint t);
  [DllImport("kernel32.dll")] static extern bool WriteProcessMemory(IntPtr p, IntPtr a, byte[] b, UIntPtr s, out UIntPtr w);
  [DllImport("kernel32.dll")] static extern bool ReadProcessMemory(IntPtr p, IntPtr a, byte[] b, UIntPtr s, out UIntPtr r);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
  [DllImport("kernel32.dll")] static extern bool IsWow64Process(IntPtr p, out bool w);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);

  class Remote : IDisposable {
    public IntPtr hp, mem; public bool is32;
    public Remote(IntPtr hwnd, int size) {
      uint pid; GetWindowThreadProcessId(hwnd, out pid);
      hp = OpenProcess(0x0008 | 0x0010 | 0x0020 | 0x0400, false, pid);
      IsWow64Process(hp, out is32);
      mem = VirtualAllocEx(hp, IntPtr.Zero, (UIntPtr)size, 0x3000, 0x04);
    }
    public void Write(byte[] b, int off) { UIntPtr w; WriteProcessMemory(hp, mem + off, b, (UIntPtr)b.Length, out w); }
    public byte[] Read(int off, int n) { var b = new byte[n]; UIntPtr r; ReadProcessMemory(hp, mem + off, b, (UIntPtr)n, out r); return b; }
    public void Dispose() { VirtualFreeEx(hp, mem, UIntPtr.Zero, 0x8000); CloseHandle(hp); }
  }

  public static int Count(IntPtr lv) { return (int)SendMessage(lv, 0x1000 + 4, IntPtr.Zero, IntPtr.Zero); }

  // WM_DPICHANGED with the suggested rectangle in the target process (simulates moving to another DPI)
  public static void SendDpiChanged(IntPtr hwnd, int dpi, int left, int top, int right, int bottom) {
    // the system marshals the RECT of WM_DPICHANGED from the sending process
    IntPtr rc = Marshal.AllocHGlobal(16);
    try {
      Marshal.WriteInt32(rc, 0, left); Marshal.WriteInt32(rc, 4, top);
      Marshal.WriteInt32(rc, 8, right); Marshal.WriteInt32(rc, 12, bottom);
      SendMessage(hwnd, 0x02E0, (IntPtr)((dpi << 16) | dpi), rc);
    } finally { Marshal.FreeHGlobal(rc); }
  }

  // LVITEMW: mask@0 iItem@4 iSubItem@8 state@12 stateMask@16, pszText@20/24 cchTextMax@24/32 (32/64-bit)
  // The text is read as UTF-16, as the list view shows it: WWWC sets it with ANSI (UTF-8) messages,
  // and a wrong conversion in the control (for example with the code page of its font) is seen here
  public static string GetText(IntPtr lv, int item, int sub) {
    using (var r = new Remote(lv, 2048)) {
      var it = new byte[88];
      BitConverter.GetBytes(1).CopyTo(it, 0);
      BitConverter.GetBytes(item).CopyTo(it, 4);
      BitConverter.GetBytes(sub).CopyTo(it, 8);
      if (r.is32) { BitConverter.GetBytes((int)(long)(r.mem + 512)).CopyTo(it, 20); BitConverter.GetBytes(768).CopyTo(it, 24); }
      else { BitConverter.GetBytes((long)(r.mem + 512)).CopyTo(it, 24); BitConverter.GetBytes(768).CopyTo(it, 32); }
      r.Write(it, 0); r.Write(new byte[1536], 512);
      SendMessage(lv, 0x1000 + 115, (IntPtr)item, r.mem);  // LVM_GETITEMTEXTW
      var b = r.Read(512, 1536); int n = 0; while (n + 1 < b.Length && (b[n] != 0 || b[n + 1] != 0)) n += 2;
      return Encoding.Unicode.GetString(b, 0, n);
    }
  }

  // select only the given item (LVM_SETITEMSTATE, LVIS_SELECTED | LVIS_FOCUSED)
  public static void Select(IntPtr lv, int item) {
    using (var r = new Remote(lv, 256)) {
      var it = new byte[88];
      BitConverter.GetBytes(8).CopyTo(it, 0); BitConverter.GetBytes(3).CopyTo(it, 16);
      r.Write(it, 0); SendMessage(lv, 0x1000 + 43, (IntPtr)(-1), r.mem);
      BitConverter.GetBytes(3).CopyTo(it, 12);
      r.Write(it, 0); SendMessage(lv, 0x1000 + 43, (IntPtr)item, r.mem);
    }
  }
}
"@

# WWWC keeps its data files in UTF-8 (Shift_JIS before 1.3.0, converted on first start)
$script:Enc = New-Object Text.UTF8Encoding($false)
$script:Sjis = [Text.Encoding]::GetEncoding(932)
# WWWC_TEST_ROOT: folder for the test copies, logs and ASan reports (default %TEMP%\wwwc-tests)
$script:TestRoot = if ($env:WWWC_TEST_ROOT) { $env:WWWC_TEST_ROOT } else { Join-Path ([IO.Path]::GetTempPath()) "wwwc-tests" }
$script:Results = New-Object System.Collections.ArrayList

# WWWC command ids (WWWC/resource.h, WWWC/General.h)
$script:CMD_ALLCHECK = 40002
$script:CMD_CHECKEND = 40004
$script:CMD_PROP = 40013
$script:CMD_ACTION = 52002      # + index of the protocol menu: 3 header, 4 source, 5 title
$script:ID_LIST = 1002

## ---------------------------------------------------------------- assertions

function Test-Case([string]$Name, [scriptblock]$Body) {
  $script:CaseErrors = New-Object System.Collections.ArrayList
  $sw = [Diagnostics.Stopwatch]::StartNew()
  try { & $Body } catch { [void]$script:CaseErrors.Add("exception: $($_.Exception.Message) at $($_.InvocationInfo.PositionMessage)") }
  finally { Stop-AllWwwc }
  foreach ($r in Get-AsanReports) { [void]$script:CaseErrors.Add("AddressSanitizer: $r") }
  $ok = $script:CaseErrors.Count -eq 0
  [void]$script:Results.Add([pscustomobject]@{ Name = $Name; Passed = $ok; Errors = @($script:CaseErrors); Seconds = [int]$sw.Elapsed.TotalSeconds })
  if ($ok) { Write-Host "  PASS  $Name ($([int]$sw.Elapsed.TotalSeconds)s)" -ForegroundColor Green }
  else {
    Write-Host "  FAIL  $Name" -ForegroundColor Red
    $script:CaseErrors | ForEach-Object { Write-Host "        $_" -ForegroundColor Red }
  }
}

function Assert-True($Condition, [string]$Message) {
  if (-not $Condition) { [void]$script:CaseErrors.Add($Message) }
}

function Assert-Equal($Expected, $Actual, [string]$Message) {
  if ($Expected -ne $Actual) { [void]$script:CaseErrors.Add("$Message (expected '$Expected', actual '$Actual')") }
}

function Wait-Until([scriptblock]$Condition, [int]$TimeoutSeconds = 15) {
  $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
  while ((Get-Date) -lt $deadline) {
    if (& $Condition) { return $true }
    Start-Sleep -Milliseconds 200
  }
  return [bool](& $Condition)
}

## ---------------------------------------------------------------- AddressSanitizer

# With an ASan build (built with -p:EnableASAN=true), WWWC writes a report to
# a file and stops at the first memory error. The reports are collected after
# each test and fail it. WWWC allocates with GlobalAlloc, so the RTL heap
# allocators are hooked as well.
function Enable-AsanReports {
  $script:AsanDir = Join-Path $script:TestRoot "asan"
  if (Test-Path $script:AsanDir) { Remove-Item -Recurse -Force $script:AsanDir }
  New-Item -ItemType Directory -Force $script:AsanDir | Out-Null
  # the path is quoted: ':' also separates the options
  $env:ASAN_OPTIONS = "log_path='$(Join-Path $script:AsanDir 'report')':windows_hook_rtl_allocators=true:detect_leaks=0"
}

# Summary lines of the reports written since the last call
function Get-AsanReports {
  if (-not $script:AsanDir -or -not (Test-Path $script:AsanDir)) { return }
  foreach ($f in Get-ChildItem $script:AsanDir -File -Filter "report*") {
    $lines = Get-Content $f.FullName
    $summary = ($lines | Where-Object { $_ -match "^SUMMARY: AddressSanitizer|ERROR: AddressSanitizer" } | Select-Object -First 2) -join " / "
    $frames = ($lines | Where-Object { $_ -match "^\s+#\d+ .* in " } | Select-Object -First 4 | ForEach-Object { ($_ -replace "^\s+#\d+ 0x[0-9a-f]+ in ", "").Trim() }) -join " <- "
    "$summary [$frames] (full report: $($f.Name))"
    Move-Item $f.FullName (Join-Path $script:AsanDir "seen-$($f.Name)") -Force
  }
}

## ---------------------------------------------------------------- test server

function Start-TestServer([int]$Port) {
  $script:ServerPort = $Port
  $script:ServerLog = Join-Path $script:TestRoot "server.log"
  New-Item -ItemType Directory -Force $script:TestRoot | Out-Null
  Set-Content -Path $script:ServerLog -Value $null
  $py = (Get-Command python -ErrorAction SilentlyContinue).Source
  if (-not $py) { $py = (Get-Command py -ErrorAction Stop).Source }
  $inUse = try { $c = New-Object Net.Sockets.TcpClient; $c.Connect("127.0.0.1", $Port); $c.Close(); $true } catch { $false }
  if ($inUse) { throw "port $Port is already in use (another test server still running?)" }
  # redirect both streams to files: an inherited console or pipe would keep the
  # caller waiting as long as the server runs
  $out = Join-Path $script:TestRoot "server.out"
  $err = Join-Path $script:TestRoot "server.err"
  $script:ServerProcess = Start-Process $py -ArgumentList "-I", "`"$PSScriptRoot\..\server\test_server.py`"", $Port, "`"$script:ServerLog`"" -PassThru -WindowStyle Hidden -RedirectStandardOutput $out -RedirectStandardError $err
  # the first start of Python can take a while (e.g. while it is scanned)
  $ok = Wait-Until {
    try { $c = New-Object Net.Sockets.TcpClient; $c.Connect("127.0.0.5", $Port); $c.Close(); $true } catch { $false }
  } 60
  if (-not $ok) {
    Stop-TestServer
    throw "test server did not start on port ${Port}: $(Get-Content $err -Raw)"
  }
}

function Stop-TestServer {
  if (-not $script:ServerProcess) { return }
  # python.exe from the Microsoft Store is a launcher that runs the server as a child process
  $id = $script:ServerProcess.Id
  Get-CimInstance Win32_Process -Filter "ParentProcessId=$id" -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
  if (-not $script:ServerProcess.HasExited) { $script:ServerProcess | Stop-Process -Force }
}

function Url([string]$Path, [string]$HostName = "127.0.0.1") { "http://${HostName}:$($script:ServerPort)$Path" }

# Server log lines written after the mark
function Get-ServerMark { @(Get-Content $script:ServerLog).Count }
function Get-ServerLog([int]$Mark = 0) { @(Get-Content $script:ServerLog | Select-Object -Skip $Mark) }

## ---------------------------------------------------------------- test environment

# A fresh copy of WWWC with its own data directory
function New-TestEnv([string]$Name, [string]$BinDir) {
  $dir = Join-Path $script:TestRoot $Name
  if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
  $data = Join-Path $dir "data\$env:USERNAME"
  New-Item -ItemType Directory -Force (Join-Path $dir "bin"), $data | Out-Null
  Copy-Item (Join-Path $BinDir "WWWC.exe"), (Join-Path $BinDir "wwwc.dll") (Join-Path $dir "bin")
  # AddressSanitizer runtime of an ASan build (see Enable-AsanReports)
  Get-ChildItem $BinDir -Filter "clang_rt.asan*.dll" | Copy-Item -Destination (Join-Path $dir "bin")
  [pscustomobject]@{ Dir = $dir; Bin = (Join-Path $dir "bin"); Data = $data; ItemFile = (Join-Path $data "Item.dat"); Process = $null; Window = [IntPtr]::Zero }
}

# One Item.dat line: Title, CheckURL, Size, Date, Status, CheckDate, OldSize, OldDate, ViewURL,
# Option1 (2 = GET), Option2, Comment, CheckSt, ErrStatus, DLLData1, DLLData2
function ItemLine([string]$Title, [string]$Url, [string]$Option2 = "0;;0;;;;;;0;;;;;;;;0", [string]$Status = "0") {
  "$Title`t$Url`t`t`t$Status`t`t`t`t`t2`t$Option2`t`t0`t`t`t"
}

function Set-Items($Env, [string[]]$Lines) {
  [IO.File]::WriteAllBytes($Env.ItemFile, $script:Enc.GetBytes(($Lines -join "`r`n") + "`r`n"))
}

# Fields of the saved item (read after WWWC exits)
function Get-SavedItem($Env, [string]$Title) {
  $line = $script:Enc.GetString([IO.File]::ReadAllBytes($Env.ItemFile)) -split "`r`n" | Where-Object { $_ -like "$Title`t*" } | Select-Object -First 1
  if ($null -eq $line) { return $null }
  $f = $line -split "`t"
  [pscustomobject]@{ Title = $f[0]; Url = $f[1]; Size = $f[2]; Date = $f[3]; Status = [int]$f[4]; Option2 = $f[10]; ErrStatus = $f[13] }
}

function Get-SavedTitles($Env) {
  @($script:Enc.GetString([IO.File]::ReadAllBytes($Env.ItemFile)) -split "`r`n" | Where-Object { $_ } | ForEach-Object { ($_ -split "`t")[0] })
}

## ---------------------------------------------------------------- windows

function Get-ProcessWindows([int]$ProcessId) {
  $list = New-Object System.Collections.ArrayList
  [void][WwwcWin]::EnumWindows({ param($h, $l)
      $p = 0; [void][WwwcWin]::GetWindowThreadProcessId($h, [ref]$p)
      if ($p -eq $ProcessId -and [WwwcWin]::IsWindowVisible($h)) {
        $t = New-Object Text.StringBuilder 256; $c = New-Object Text.StringBuilder 256
        [void][WwwcWin]::GetWindowText($h, $t, 256); [void][WwwcWin]::GetClassName($h, $c, 256)
        [void]$list.Add([pscustomobject]@{ Handle = $h; Title = $t.ToString(); Class = $c.ToString() })
      }
      $true }, [IntPtr]::Zero)
  $list
}

# Windows of WWWC other than the main window
function Get-Dialogs($Env) { @(Get-ProcessWindows $Env.Process.Id | Where-Object { $_.Handle -ne $Env.Window }) }

# Luminance (0-255) of a pixel of a window, drawn with PrintWindow
function Get-PixelLuminance([IntPtr]$Window, [int]$X, [int]$Y) {
  $r = New-Object WwwcWin+RECT; [void][WwwcWin]::GetWindowRect($Window, [ref]$r)
  $bmp = New-Object Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
  $g = [Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc(); [void][WwwcWin]::PrintWindow($Window, $dc, 2); $g.ReleaseHdc($dc); $g.Dispose()
  $c = $bmp.GetPixel($X, $Y); $bmp.Dispose()
  [int](($c.R * 299 + $c.G * 587 + $c.B * 114) / 1000)
}

## ---------------------------------------------------------------- WWWC

$script:Started = New-Object System.Collections.ArrayList

function Start-Wwwc($Env) {
  $Env.Process = Start-Process (Join-Path $Env.Bin "WWWC.exe") -ArgumentList "/d:`"$(Join-Path $Env.Dir 'data')`"" -PassThru
  [void]$script:Started.Add($Env.Process)
  $ok = Wait-Until { $Env.Process.HasExited -or (Get-ProcessWindows $Env.Process.Id | Where-Object Class -eq "WWWCWindowClass") } 15
  if ($Env.Process.HasExited) { return $false }
  if (-not $ok) { throw "WWWC main window did not appear" }
  $Env.Window = (Get-ProcessWindows $Env.Process.Id | Where-Object Class -eq "WWWCWindowClass" | Select-Object -First 1).Handle
  [void][WwwcWin]::MoveWindow($Env.Window, 40, 40, 900, 420, $true)
  Start-Sleep -Milliseconds 500
  return $true
}

# Close WWWC (answers the "exit while checking" question with Yes) and return the exit code
function Stop-Wwwc($Env, [int]$TimeoutSeconds = 15) {
  if ($Env.Process.HasExited) { return $Env.Process.ExitCode }
  Get-Dialogs $Env | ForEach-Object { [void][WwwcWin]::PostMessage($_.Handle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) }
  [void][WwwcWin]::PostMessage($Env.Window, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
  $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
  while (-not $Env.Process.HasExited -and (Get-Date) -lt $deadline) {
    Start-Sleep -Milliseconds 300
    foreach ($d in Get-Dialogs $Env) {
      if ([WwwcWin]::GetDlgItem($d.Handle, 6) -ne [IntPtr]::Zero) {
        [void][WwwcWin]::PostMessage($d.Handle, 0x0111, [IntPtr]6, [IntPtr]::Zero)   # IDYES
      }
    }
  }
  if (-not $Env.Process.HasExited) { $Env.Process | Stop-Process -Force; $Env.Process.WaitForExit(); return $null }
  $Env.Process.WaitForExit()
  return $Env.Process.ExitCode
}

# Kill only the WWWC processes started by the tests
function Stop-AllWwwc {
  foreach ($p in $script:Started) { if (-not $p.HasExited) { $p | Stop-Process -Force } }
  $script:Started.Clear()
}

function Send-Command($Env, [int]$Id) { [void][WwwcWin]::PostMessage($Env.Window, 0x0111, [IntPtr]$Id, [IntPtr]::Zero) }

function Get-ListHandle($Env) { [WwwcWin]::GetDlgItem($Env.Window, $script:ID_LIST) }

function Get-ListTitles($Env) {
  $lv = Get-ListHandle $Env
  @(for ($i = 0; $i -lt [WwwcList]::Count($lv); $i++) { [WwwcList]::GetText($lv, $i, 0) })
}

# Give the list view the focus (a click on its empty area) and select the item,
# so that item commands apply to the list and not to the folder tree
function Select-ListItem($Env, [string]$Title) {
  $lv = Get-ListHandle $Env
  $r = New-Object WwwcWin+RECT; [void][WwwcWin]::GetClientRect($lv, [ref]$r)
  $lp = [IntPtr](((($r.B - 8) -band 0xFFFF) -shl 16) -bor 8)
  [void][WwwcWin]::PostMessage($lv, 0x0201, [IntPtr]1, $lp)   # WM_LBUTTONDOWN
  [void][WwwcWin]::PostMessage($lv, 0x0202, [IntPtr]0, $lp)   # WM_LBUTTONUP
  Start-Sleep -Milliseconds 300
  $titles = Get-ListTitles $Env
  $index = [array]::IndexOf($titles, $Title)
  if ($index -lt 0) { throw "item '$Title' is not in the list ($($titles -join ', '))" }
  [WwwcList]::Select($lv, $index)
  Start-Sleep -Milliseconds 200
}

function Get-HandleCount($Env) { (Get-Process -Id $Env.Process.Id).HandleCount }
