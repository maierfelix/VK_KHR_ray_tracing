start "" /d "%cd%" "%VULKAN_SDK%/bin/glslangValidator" --target-env vulkan1.2 -V ray-generation.rgen   -o ray-generation.spv
start "" /d "%cd%" "%VULKAN_SDK%/bin/glslangValidator" --target-env vulkan1.2 -V ray-closest-hit.rchit -o ray-closest-hit.spv
start "" /d "%cd%" "%VULKAN_SDK%/bin/glslangValidator" --target-env vulkan1.2 -V ray-miss.rmiss        -o ray-miss.spv
