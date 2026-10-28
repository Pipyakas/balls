#!/bin/sh
# Starts the packaged game. Unreal writes the plain [launcher] form of this script; build.bat stages this one instead,
# because RADV (Mesa's AMD Vulkan driver) segfaults inside vkCreateComputePipelines while Unreal precaches the global
# compute shaders at startup, at least on GCN cards. Precache stays on everywhere else; on RADV those pipelines are
# built on first use instead, which costs a small hitch and no crash.
UE_TRUE_SCRIPT_NAME=$(echo \"$0\" | xargs readlink -f)
UE_PROJECT_ROOT=$(dirname "$UE_TRUE_SCRIPT_NAME")
chmod +x "$UE_PROJECT_ROOT/ColorBlastBalls/Binaries/Linux/ColorBlastBalls"

NO_GLOBAL_PSO_PRECACHE=
if command -v vulkaninfo >/dev/null 2>&1 && vulkaninfo --summary 2>/dev/null | grep -q 'driverName *= *radv'; then
    NO_GLOBAL_PSO_PRECACHE="-ini:Engine:[ConsoleVariables]:r.PSOPrecache.GlobalShaders=0"
fi

"$UE_PROJECT_ROOT/ColorBlastBalls/Binaries/Linux/ColorBlastBalls" ColorBlastBalls $NO_GLOBAL_PSO_PRECACHE "$@"
