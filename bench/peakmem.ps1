# Peak working set of one run, in MB:
#   pwsh -NoProfile -File bench/peakmem.ps1 <absolute exe> <file> " <args...>"
# Windows has no /usr/bin/time -v. GetProcessMemoryInfo on the process
# handle still works after exit, unlike .NET's Process.PeakWorkingSet64.
# The file is its own argument (paths may contain spaces). The funiq flags
# travel as one string starting with a SPACE: pwsh -File would bind a
# leading "-d" (and, case-insensitively, "-D") as one of its own parameters.
param([Parameter(Mandatory)][string]$Exe, [Parameter(Mandatory)][string]$File,
      [Parameter(Mandatory)][string]$Flags)
$ErrorActionPreference = "Stop"
Add-Type -Namespace W -Name P -MemberDefinition @'
[StructLayout(LayoutKind.Sequential)] public struct PMC {
  public uint cb, PageFaultCount; public UIntPtr PeakWorkingSetSize, WorkingSetSize,
  QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage, QuotaPeakNonPagedPoolUsage,
  QuotaNonPagedPoolUsage, PagefileUsage, PeakPagefileUsage; }
[DllImport("psapi.dll")] public static extern bool GetProcessMemoryInfo(IntPtr h, out PMC c, uint cb);
'@
$psi = [Diagnostics.ProcessStartInfo]::new($Exe)
$psi.ArgumentList.Add($File)
foreach ($a in $Flags.Trim() -split " +") { $psi.ArgumentList.Add($a) }
$psi.RedirectStandardOutput = $true; $psi.UseShellExecute = $false
$p = [Diagnostics.Process]::Start($psi); $h = $p.Handle
$null = $p.StandardOutput.BaseStream.CopyToAsync([IO.Stream]::Null)
$p.WaitForExit()
$c = [W.P+PMC]::new(); $c.cb = [Runtime.InteropServices.Marshal]::SizeOf($c)
[void][W.P]::GetProcessMemoryInfo($h, [ref]$c, $c.cb)
'{0:N1}' -f ($c.PeakWorkingSetSize.ToUInt64() / 1MB)
