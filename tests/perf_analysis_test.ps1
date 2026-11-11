$ErrorActionPreference = 'Stop'
$temporary = [IO.Path]::GetTempFileName()
try {
    [IO.File]::WriteAllText($temporary, "frame_time_us,draw_calls,command_buffer_stalls,pipeline_cache_misses,texture_cache_misses`n10000,10,0,0,0`n20000,20,1,0,0`n30000,30,2,1,3`n40000,40,3,0,0`n99999")
    $result = & "$PSScriptRoot/../scripts/measure-frames.ps1" -Path $temporary -AfterFrame 1 -LastFrames 2
    if ($result.TotalFrames -ne 4 -or $result.Samples -ne 2 -or
        $result.MeanMs -ne 35 -or $result.P99Ms -ne 40 -or
        $result.GPUWaits -ne 5 -or $result.PipelineMisses -ne 1) {
        throw "Incorrect frame summary: $($result | ConvertTo-Json -Compress)"
    }
    if ($null -ne $result.MeanGPUFenceWaitMs) { throw 'Old logs must not invent wait timings' }
    if ($null -ne $result.MaxTextureCacheMB -or $null -ne $result.TextureEvictions -or
        $null -ne $result.NoEffectDrawsSkipped) {
        throw 'Old logs must not invent texture cache measurements'
    }
    [IO.File]::WriteAllText($temporary, "frame_time_us,draw_calls,command_buffer_stalls,pipeline_cache_misses,texture_cache_misses,command_buffer_wait_us`n20000,20,1,0,0,3000`n30000,30,2,0,0,7000`n")
    $result = & "$PSScriptRoot/../scripts/measure-frames.ps1" -Path $temporary
    if ($result.MeanGPUFenceWaitMs -ne 5) { throw 'Incorrect GPU fence wait duration' }
    [IO.File]::WriteAllText($temporary, "frame_time_us,draw_calls,command_buffer_stalls,pipeline_cache_misses,texture_cache_misses,texture_cache_memory_mb,texture_cache_evictions,draw_calls_skipped_no_effect`n20000,20,1,0,0,512,3,100`n30000,30,2,0,0,448,5,200`n")
    $result = & "$PSScriptRoot/../scripts/measure-frames.ps1" -Path $temporary
    if ($result.MaxTextureCacheMB -ne 512 -or $result.TextureEvictions -ne 8 -or
        $result.NoEffectDrawsSkipped -ne 300) {
        throw 'Incorrect texture cache memory or eviction summary'
    }
} finally { Remove-Item -LiteralPath $temporary }
