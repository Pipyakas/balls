struct type_u002e_VisibilityUniforms {
    itemCount: u32,
    gridWidth: u32,
    gridHeight: u32,
    frameSeed: u32,
    coordinateToSlotX: f32,
    coordinateToSlotY: f32,
    bidderMask: u32,
    unusedPadding: u32,
}

struct type_u002e_StructuredBuffer_u002e_uint {
    member: array<u32>,
}

struct type_u002e_RWStructuredBuffer_u002e_uint {
    member: array<u32>,
}

struct type_u002e_RWStructuredBuffer_u002e_uint_1 {
    member: array<atomic<u32>>,
}

@group(2) @binding(0) 
var<uniform> VisibilityUniforms: type_u002e_VisibilityUniforms;
@group(0) @binding(0) 
var<storage> candidates: type_u002e_StructuredBuffer_u002e_uint;
@group(1) @binding(0) 
var<storage, read_write> visibleBalls: type_u002e_RWStructuredBuffer_u002e_uint;
@group(1) @binding(1) 
var<storage, read_write> drawArguments: type_u002e_RWStructuredBuffer_u002e_uint_1;
var<private> global: vec3<u32>;

fn computeMain_1() {
    let _e9 = global;
    switch bitcast<i32>(0u) {
        default: {
            let _e13 = VisibilityUniforms.itemCount;
            if (_e9.x >= _e13) {
                break;
            }
            let _e17 = candidates.member[_e9.x];
            if (_e17 == 4294967295u) {
                break;
            }
            let _e21 = atomicAdd((&drawArguments.member[1u]), 1u);
            visibleBalls.member[_e21] = _e17;
            break;
        }
    }
    return;
}

@compute @workgroup_size(256, 1, 1) 
fn computeMain(@builtin(global_invocation_id) param: vec3<u32>) {
    global = param;
    computeMain_1();
}
