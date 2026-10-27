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

struct type_u002e_RWStructuredBuffer_u002e_uint {
    member: array<u32>,
}

@group(2) @binding(0) 
var<uniform> VisibilityUniforms: type_u002e_VisibilityUniforms;
@group(1) @binding(0) 
var<storage, read_write> priorities: type_u002e_RWStructuredBuffer_u002e_uint;
@group(1) @binding(1) 
var<storage, read_write> winners: type_u002e_RWStructuredBuffer_u002e_uint;
@group(1) @binding(2) 
var<storage, read_write> drawArguments: type_u002e_RWStructuredBuffer_u002e_uint;
var<private> global: vec3<u32>;

fn computeMain_1() {
    let _e12 = global;
    if (_e12.x == 0u) {
        drawArguments.member[0u] = 6u;
        drawArguments.member[1u] = 0u;
        drawArguments.member[2u] = 0u;
        drawArguments.member[3u] = 0u;
    }
    let _e24 = VisibilityUniforms.itemCount;
    if (_e12.x < _e24) {
        priorities.member[_e12.x] = 0u;
        winners.member[_e12.x] = 4294967295u;
    }
    return;
}

@compute @workgroup_size(256, 1, 1) 
fn computeMain(@builtin(global_invocation_id) param: vec3<u32>) {
    global = param;
    computeMain_1();
}
