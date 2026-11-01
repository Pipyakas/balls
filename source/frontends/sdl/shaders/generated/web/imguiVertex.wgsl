struct type_u002e_ConstantBuffer_u002e_ImguiUniforms {
    scale: vec2<f32>,
    translate: vec2<f32>,
}

struct VertexOutput {
    @builtin(position) member: vec4<f32>,
    @location(0) member_1: vec2<f32>,
    @location(1) member_2: vec4<f32>,
}

@group(0) @binding(0)
var<uniform> uniforms: type_u002e_ConstantBuffer_u002e_ImguiUniforms;
var<private> in_u002e_var_u002e_POSITION_1: vec2<f32>;
var<private> in_u002e_var_u002e_TEXCOORD0_1: vec2<f32>;
var<private> in_u002e_var_u002e_COLOR0_1: vec4<f32>;
var<private> global: vec4<f32> = vec4<f32>(0f, 0f, 0f, 1f);
var<private> out_u002e_var_u002e_TEXCOORD0_: vec2<f32>;
var<private> out_u002e_var_u002e_COLOR0_: vec4<f32>;

fn vertexMain_1() {
    let _e11 = in_u002e_var_u002e_POSITION_1;
    let _e12 = in_u002e_var_u002e_TEXCOORD0_1;
    let _e13 = in_u002e_var_u002e_COLOR0_1;
    let _e15 = uniforms.scale;
    let _e18 = uniforms.translate;
    let _e19 = ((_e11 * _e15) + _e18);
    global = vec4<f32>(_e19.x, _e19.y, 0f, 1f);
    out_u002e_var_u002e_TEXCOORD0_ = _e12;
    out_u002e_var_u002e_COLOR0_ = _e13;
    return;
}

@vertex
fn vertexMain(@location(0) in_u002e_var_u002e_POSITION: vec2<f32>, @location(1) in_u002e_var_u002e_TEXCOORD0_: vec2<f32>, @location(2) in_u002e_var_u002e_COLOR0_: vec4<f32>) -> VertexOutput {
    in_u002e_var_u002e_POSITION_1 = in_u002e_var_u002e_POSITION;
    in_u002e_var_u002e_TEXCOORD0_1 = in_u002e_var_u002e_TEXCOORD0_;
    in_u002e_var_u002e_COLOR0_1 = in_u002e_var_u002e_COLOR0_;
    vertexMain_1();
    let _e10 = global.y;
    global.y = _e10;
    let _e12 = global;
    let _e13 = out_u002e_var_u002e_TEXCOORD0_;
    let _e14 = out_u002e_var_u002e_COLOR0_;
    return VertexOutput(_e12, _e13, _e14);
}
