#requires -Version 7.2
[CmdletBinding(DefaultParameterSetName='Frames')]
param(
    [Parameter(Mandatory)][string]$Path,
    [Parameter(ParameterSetName='Frames')]
    [ValidateRange(0, 2147483647)][int]$AfterFrame = 0,
    [Parameter(ParameterSetName='Frames')]
    [ValidateRange(1, 1000000)][int]$LastFrames = 600,
    [Parameter(Mandatory,ParameterSetName='Window')][long]$StartHostUs,
    [Parameter(Mandatory,ParameterSetName='Window')][long]$EndHostUs,
    [ValidateSet(30,60,90,120)][int]$TargetFPS
)
$ErrorActionPreference = 'Stop'
$window = $PSCmdlet.ParameterSetName -eq 'Window'
if ($window -and ($StartHostUs -lt 0 -or $StartHostUs -ge $EndHostUs)) { throw 'Invalid frame window.' }
$deadline = [Diagnostics.Stopwatch]::StartNew()
do {
    $stream = [IO.File]::Open($Path, 'Open', 'Read', 'ReadWrite')
    $reader = [IO.StreamReader]::new($stream)
    try { $contents = $reader.ReadToEnd() } finally { $reader.Dispose() }
    $end = $contents.LastIndexOf("`n")
    if ($end -lt 0) { throw 'Frame log has no complete header.' }
    $lines = $contents.Substring(0, $end).Split("`n")
    $header = $lines[0].TrimEnd("`r").Split(',')
    $total = $lines.Length - 1
    if (!$window) { break }
    $stamp = [Array]::IndexOf($header, 'host_us')
    $duration = [Array]::IndexOf($header, 'frame_time_us')
    if ($stamp -lt 0 -or $duration -lt 0) { throw 'Phase timing requires a timestamped SDK frame log.' }
    if ($total -gt 0 -and [long]$lines[-1].Split(',')[$stamp] -ge $EndHostUs) { break }
    if ($deadline.Elapsed.TotalSeconds -ge 5) { throw 'Frame log did not reach the end of the phase.' }
    Start-Sleep -Milliseconds 100
} while ($true)
$first = if ($window) { 1 } else { [Math]::Max($AfterFrame + 1L, $total - $LastFrames + 1L) }
$recent = @(for ($i = $first; $i -le $total; $i++) {
    if ($window) {
        $fields = $lines[$i].Split(',')
        $hostUs = [long]$fields[$stamp]
        if ($hostUs -gt $EndHostUs) { break }
        if ($hostUs - [long]$fields[$duration] -lt $StartHostUs) { continue }
    }
    $lines[$i]
} )
$recent = @($recent | ConvertFrom-Csv -Header $header | Where-Object { [double]$_.frame_time_us -gt 0 })
if ($recent.Count -eq 0) { throw 'No complete frame samples in the selected range.' }
$times = @($recent | ForEach-Object { [double]$_.frame_time_us / 1000 } | Sort-Object)
$mean = ($times | Measure-Object -Average).Average
[pscustomobject]@{
    TotalFrames = $total
    Samples = $times.Count
    MeanMs = [Math]::Round($mean, 3)
    EffectiveFPS = [Math]::Round(1000 / $mean, 2)
    TargetFPS = if ($TargetFPS) { $TargetFPS }
    OverBudgetPercent = if ($TargetFPS) {
        [Math]::Round(100 * @($times.Where({ $_ -gt 1000.0 / $TargetFPS })).Count / $times.Count, 2)
    }
    P50Ms = $times[[Math]::Min($times.Count - 1, [int][Math]::Floor($times.Count * 0.5))]
    P95Ms = $times[[Math]::Min($times.Count - 1, [int][Math]::Floor($times.Count * 0.95))]
    P99Ms = $times[[Math]::Min($times.Count - 1, [int][Math]::Floor($times.Count * 0.99))]
    MaxMs = $times[-1]
    Over20Ms = @($times | Where-Object { $_ -gt 20 }).Count
    Over25Ms = @($times | Where-Object { $_ -gt 25 }).Count
    Over50Ms = @($times | Where-Object { $_ -gt 50 }).Count
    MeanDrawCalls = [Math]::Round(($recent.draw_calls | Measure-Object -Average).Average, 1)
    GPUWaits = ($recent.command_buffer_stalls | Measure-Object -Sum).Sum
    MeanGPUFenceWaitMs = if ($recent[0].PSObject.Properties['command_buffer_wait_us']) {
        [Math]::Round(($recent.command_buffer_wait_us | Measure-Object -Average).Average / 1000, 3)
    } else { $null }
    PipelineMisses = ($recent.pipeline_cache_misses | Measure-Object -Sum).Sum
    TextureMisses = ($recent.texture_cache_misses | Measure-Object -Sum).Sum
    MaxTextureCacheMB = if ($recent[0].PSObject.Properties['texture_cache_memory_mb']) {
        ($recent.texture_cache_memory_mb | Measure-Object -Maximum).Maximum
    } else { $null }
    TextureEvictions = if ($recent[0].PSObject.Properties['texture_cache_evictions']) {
        ($recent.texture_cache_evictions | Measure-Object -Sum).Sum
    } else { $null }
    NoEffectDrawsSkipped = if ($recent[0].PSObject.Properties['draw_calls_skipped_no_effect']) {
        ($recent.draw_calls_skipped_no_effect | Measure-Object -Sum).Sum
    } else { $null }
    PromptCandidates = if ($recent[0].PSObject.Properties['prompt_candidates']) {
        ($recent.prompt_candidates | Measure-Object -Sum).Sum
    }
    PromptHashBytes = if ($recent[0].PSObject.Properties['prompt_hash_bytes']) {
        ($recent.prompt_hash_bytes | Measure-Object -Sum).Sum
    }
    PromptReplacements = if ($recent[0].PSObject.Properties['prompt_replacements']) {
        ($recent.prompt_replacements | Measure-Object -Sum).Sum
    }
    MeanPromptMatchMs = if ($recent[0].PSObject.Properties['prompt_match_ns']) {
        [Math]::Round(($recent.prompt_match_ns | Measure-Object -Average).Average / 1000000, 4)
    }
    MeanMemexportFenceWaitMs = if ($recent[0].PSObject.Properties['memexport_sync_us']) {
        [Math]::Round(($recent.memexport_sync_us | Measure-Object -Average).Average / 1000, 3)
    }
}
