struct type_u002e_StructuredBuffer_u002e_uint {
    member: array<u32>,
}

struct type_u002e_BallUniforms {
    boardOrigin: vec2<f32>,
    unpackScale: vec2<f32>,
    viewportSize: vec2<f32>,
    cellPixels: f32,
    radiusPixels: f32,
    palette: array<vec4<f32>, 7>,
}

struct VertexOutput {
    @builtin(position) member: vec4<f32>,
    @location(0) member_1: vec2<f32>,
    @location(1) @interpolate(flat) member_2: vec3<f32>,
}

@group(0) @binding(0) 
var<storage> visibleBalls: type_u002e_StructuredBuffer_u002e_uint;
@group(1) @binding(0) 
var<uniform> BallUniforms: type_u002e_BallUniforms;
var<private> global: u32;
var<private> global_1: u32;
var<private> global_2: vec4<f32> = vec4<f32>(0f, 0f, 0f, 1f);
var<private> out_u002e_var_u002e_TEXCOORD0_: vec2<f32>;
var<private> out_u002e_var_u002e_TEXCOORD1_: vec3<f32>;

fn vertexMain_1() {
    var quadCorners: array<vec2<f32>, 6>;

    quadCorners = array<vec2<f32>, 6>(vec2<f32>(-1f, -1f), vec2<f32>(1f, -1f), vec2<f32>(1f, 1f), vec2<f32>(-1f, -1f), vec2<f32>(1f, 1f), vec2<f32>(-1f, 1f));
    let _e28 = global;
    let _e29 = global_1;
    let _e32 = visibleBalls.member[_e29];
    let _e41 = BallUniforms.unpackScale;
    let _e44 = quadCorners[_e28];
    let _e46 = BallUniforms.boardOrigin;
    let _e48 = BallUniforms.cellPixels;
    let _e52 = BallUniforms.radiusPixels;
    let _e54 = ((_e46 + ((vec2<f32>(f32((_e32 & 16383u)), f32(((_e32 >> bitcast<u32>(14u)) & 16383u))) * _e41) * _e48)) + (_e44 * _e52));
    let _e58 = BallUniforms.viewportSize[0i];
    let _e65 = BallUniforms.viewportSize[1i];
    let _e75 = BallUniforms.palette[min((_e32 >> bitcast<u32>(28u)), 6u)];
    global_2 = vec4<f32>((((_e54.x / _e58) * 2f) - 1f), (1f - ((_e54.y / _e65) * 2f)), 0f, 1f);
    out_u002e_var_u002e_TEXCOORD0_ = _e44;
    out_u002e_var_u002e_TEXCOORD1_ = _e75.xyz;
    return;
}

@vertex 
fn vertexMain(@builtin(vertex_index) param: u32, @builtin(instance_index) param_1: u32) -> VertexOutput {
    global = param;
    global_1 = param_1;
    vertexMain_1();
    let _e8 = global_2.y;
    global_2.y = -(_e8);
    let _e10 = global_2;
    let _e11 = out_u002e_var_u002e_TEXCOORD0_;
    let _e12 = out_u002e_var_u002e_TEXCOORD1_;
    return VertexOutput(_e10, _e11, _e12);
}
