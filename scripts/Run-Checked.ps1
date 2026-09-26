# SPDX-License-Identifier: GPL-3.0-only
# Build-host helper (PowerShell 7). Bounded child process, no shell interpolation.
function Run([string]$Exe,[string[]]$ArgumentList) {
    $resolved = (Get-Command $Exe -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $label = "[$([DateTime]::UtcNow.ToString('o'))] RUN $resolved $($ArgumentList -join ' ')"
    Write-Host $label
    Add-Content (Join-Path $Out 'native.log') $label
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $resolved
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardInput = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in $ArgumentList) { [void]$info.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    try {
        if (!$process.Start()) { throw "Cannot start $Exe" }
        $process.StandardInput.Close()
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $timedOut = !$process.WaitForExit(120000)
        if ($timedOut) { $process.Kill($true); [void]$process.WaitForExit(10000) }
        foreach ($task in @($stdout,$stderr)) {
            if ($task.Wait(10000)) {
                $text = $task.GetAwaiter().GetResult()
                if ($text) { Write-Host $text; Add-Content (Join-Path $Out 'native.log') $text }
            }
        }
        if ($timedOut) { throw "$Exe exceeded the 120-second tool timeout; process tree terminated. Validation did not pass." }
        if ($process.ExitCode -ne 0) { throw "$Exe exited with $($process.ExitCode)" }
        Write-Host "PASS tool: $Exe"
    } finally { $process.Dispose() }
}
