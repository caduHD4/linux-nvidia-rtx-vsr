# Native denoise above 1080p

User requests denoise for 1440p/4K sources. Extend the existing browser native-denoise path to input dimensions up to 4096x2160. Above 1920x1080, always keep output dimensions identical to input, use the saved denoise level and sharpness, irrespective of the upscaling target. Off/invalid settings bypass before processing. Sources <=1080p retain current target selection. No extra pass, resize, or new synchronization path.

Browser eligibility uses a 4096x2160 cap while generic ClassifyFrame retains its default target contract. Preserve protected/encrypted/HDR/10-bit/CPU/geometry gates before import, fullscreen-only execution, watchdog and original fallback. Larger inputs remain bypass pending resource/performance validation.

Test native dimensions/level/sharpness across target choices, Off and oversized bypass; verify high-res eligibility still rejects protected/HDR. Export atomic full patch and verified migrations from published baseline, quality selector, and image controls revisions. Run incremental build with visible log; real 1440p/4K SDR playback, continuity and fullscreen exit. Report measured limits honestly; no 4K60 guarantee.
