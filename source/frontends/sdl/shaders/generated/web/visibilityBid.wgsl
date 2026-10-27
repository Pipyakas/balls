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
var<storage> balls: type_u002e_StructuredBuffer_u002e_uint;
@group(1) @binding(0) 
var<storage, read_write> priorities: type_u002e_RWStructuredBuffer_u002e_uint_1;
var<private> global: vec3<u32>;

fn computeMain_1() {
    let _e19 = global;
    switch bitcast<i32>(0u) {
        default: {
            let _e23 = VisibilityUniforms.itemCount;
            if (_e19.x >= _e23) {
                break;
            }
            let _e26 = VisibilityUniforms.frameSeed;
            let _e27 = (_e19.x ^ _e26);
            let _e31 = ((_e27 ^ (_e27 >> bitcast<u32>(16u))) * 2146121005u);
            let _e35 = ((_e31 ^ (_e31 >> bitcast<u32>(15u))) * 2221713035u);
            let _e39 = ((_e35 ^ (_e35 >> bitcast<u32>(16u))) | 1u);
            let _e43 = VisibilityUniforms.bidderMask;
            if (((_e39 >> bitcast<u32>(1u)) & _e43) != 0u) {
                break;
            }
            let _e48 = balls.member[_e19.x];
            let _e52 = VisibilityUniforms.coordinateToSlotX;
            let _e56 = VisibilityUniforms.gridWidth;
            let _e64 = VisibilityUniforms.coordinateToSlotY;
            let _e68 = VisibilityUniforms.gridHeight;
            let _e75 = atomicLoad((&priorities.member[((min(u32((f32(((_e48 >> bitcast<u32>(14u)) & 16383u)) * _e64)), (_e68 - 1u)) * _e56) + min(u32((f32((_e48 & 16383u)) * _e52)), (_e56 - 1u)))]));
            if (_e39 > _e75) {
                let _e77 = atomicMax((&priorities.member[((min(u32((f32(((_e48 >> bitcast<u32>(14u)) & 16383u)) * _e64)), (_e68 - 1u)) * _e56) + min(u32((f32((_e48 & 16383u)) * _e52)), (_e56 - 1u)))]), _e39);
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
