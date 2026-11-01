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

@group(2) @binding(0)
var<uniform> VisibilityUniforms: type_u002e_VisibilityUniforms;
@group(0) @binding(0)
var<storage> balls: type_u002e_StructuredBuffer_u002e_uint;
@group(1) @binding(0)
var<storage, read_write> priorities: type_u002e_RWStructuredBuffer_u002e_uint;
@group(1) @binding(1)
var<storage, read_write> winners: type_u002e_RWStructuredBuffer_u002e_uint;
var<private> global: vec3<u32>;

fn computeMain_1() {
    let _e19 = global;
    switch bitcast<i32>(0u) {
        default: {
            let _e23 = VisibilityUniforms.itemCount;
            if (_e19.x >= _e23) {
                break;
            }
            let _e27 = balls.member[_e19.x];
            let _e31 = VisibilityUniforms.coordinateToSlotX;
            let _e35 = VisibilityUniforms.gridWidth;
            let _e43 = VisibilityUniforms.coordinateToSlotY;
            let _e47 = VisibilityUniforms.gridHeight;
            let _e51 = ((min(u32((f32(((_e27 >> bitcast<u32>(14u)) & 16383u)) * _e43)), (_e47 - 1u)) * _e35) + min(u32((f32((_e27 & 16383u)) * _e31)), (_e35 - 1u)));
            let _e54 = priorities.member[_e51];
            let _e56 = VisibilityUniforms.frameSeed;
            let _e57 = (_e19.x ^ _e56);
            let _e61 = ((_e57 ^ (_e57 >> bitcast<u32>(16u))) * 2146121005u);
            let _e65 = ((_e61 ^ (_e61 >> bitcast<u32>(15u))) * 2221713035u);
            if (_e54 == ((_e65 ^ (_e65 >> bitcast<u32>(16u))) | 1u)) {
                winners.member[_e51] = _e27;
            }
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
