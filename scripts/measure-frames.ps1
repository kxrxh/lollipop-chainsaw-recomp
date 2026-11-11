#requires -Version 7.2
param(
    [Parameter(Mandatory)][string]$Path,
    [ValidateRange(0, 2147483647)][int]$AfterFrame = 0,
    [ValidateRange(1, 1000000)][int]$LastFrames = 600
)
$ErrorActionPreference = 'Stop'
$stream = [IO.File]::Open($Path, 'Open', 'Read', 'ReadWrite')
$reader = [IO.StreamReader]::new($stream)
try { $contents = $reader.ReadToEnd() } finally { $reader.Dispose() }
$end = $contents.LastIndexOf("`n")
if ($end -lt 0) { throw 'Frame log has no complete header.' }
$rows = @($contents.Substring(0, $end) | ConvertFrom-Csv)
$recent = @($rows | Select-Object -Skip $AfterFrame | Select-Object -Last $LastFrames |
    Where-Object { [double]$_.frame_time_us -gt 0 })
if ($recent.Count -eq 0) { throw 'No complete frame samples in the selected range.' }
$times = @($recent | ForEach-Object { [double]$_.frame_time_us / 1000 } | Sort-Object)
$mean = ($times | Measure-Object -Average).Average
[pscustomobject]@{
    TotalFrames = $rows.Count
    Samples = $times.Count
    MeanMs = [Math]::Round($mean, 3)
    EffectiveFPS = [Math]::Round(1000 / $mean, 2)
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
}
