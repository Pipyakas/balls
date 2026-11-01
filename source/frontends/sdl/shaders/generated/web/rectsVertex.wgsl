struct RectInstance {
    bounds: vec4<f32>,
    color: vec4<f32>,
    style: vec4<f32>,
}

struct type_u002e_StructuredBuffer_u002e_RectInstance {
    member: array<RectInstance>,
}

struct type_u002e_RectUniforms {
    viewportSize: vec2<f32>,
    unusedPadding: vec2<f32>,
}

struct VertexOutput {
    @builtin(position) member: vec4<f32>,
    @location(0) member_1: vec2<f32>,
    @location(1) @interpolate(flat) member_2: vec2<f32>,
    @location(2) @interpolate(flat) member_3: vec4<f32>,
    @location(3) @interpolate(flat) member_4: vec4<f32>,
}

@group(0) @binding(0)
var<storage> rects: type_u002e_StructuredBuffer_u002e_RectInstance;
@group(1) @binding(0)
var<uniform> RectUniforms: type_u002e_RectUniforms;
var<private> global: u32;
var<private> global_1: u32;
var<private> global_2: vec4<f32> = vec4<f32>(0f, 0f, 0f, 1f);
var<private> out_u002e_var_u002e_TEXCOORD0_: vec2<f32>;
var<private> out_u002e_var_u002e_TEXCOORD1_: vec2<f32>;
var<private> out_u002e_var_u002e_TEXCOORD2_: vec4<f32>;
var<private> out_u002e_var_u002e_TEXCOORD3_: vec4<f32>;

fn vertexMain_1() {
    var quadCorners: array<vec2<f32>, 6>;

    quadCorners = array<vec2<f32>, 6>(vec2<f32>(0f, 0f), vec2<f32>(1f, 0f), vec2<f32>(1f, 1f), vec2<f32>(0f, 0f), vec2<f32>(1f, 1f), vec2<f32>(0f, 1f));
    let _e24 = global;
    let _e25 = global_1;
    let _e29 = rects.member[_e25].bounds;
    let _e31 = rects.member[_e25].color;
    let _e33 = rects.member[_e25].style;
    let _e35 = quadCorners[_e24];
    let _e36 = _e29.zw;
    let _e37 = (_e35 * _e36);
    let _e39 = (_e29.xy + _e37);
    let _e43 = RectUniforms.viewportSize[0i];
    let _e50 = RectUniforms.viewportSize[1i];
    global_2 = vec4<f32>((((_e39.x / _e43) * 2f) - 1f), (1f - ((_e39.y / _e50) * 2f)), 0f, 1f);
    out_u002e_var_u002e_TEXCOORD0_ = _e37;
    out_u002e_var_u002e_TEXCOORD1_ = _e36;
    out_u002e_var_u002e_TEXCOORD2_ = _e31;
    out_u002e_var_u002e_TEXCOORD3_ = _e33;
    return;
}

@vertex
fn vertexMain(@builtin(vertex_index) param: u32, @builtin(instance_index) param_1: u32) -> VertexOutput {
    global = param;
    global_1 = param_1;
    vertexMain_1();
    let _e10 = global_2.y;
    global_2.y = _e10;
    let _e12 = global_2;
    let _e13 = out_u002e_var_u002e_TEXCOORD0_;
    let _e14 = out_u002e_var_u002e_TEXCOORD1_;
    let _e15 = out_u002e_var_u002e_TEXCOORD2_;
    let _e16 = out_u002e_var_u002e_TEXCOORD3_;
    return VertexOutput(_e12, _e13, _e14, _e15, _e16);
}
