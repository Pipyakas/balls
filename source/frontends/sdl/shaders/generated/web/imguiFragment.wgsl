@group(0) @binding(1)
var atlas: texture_2d<f32>;
@group(0) @binding(2)
var atlasSampler: sampler;
var<private> in_u002e_var_u002e_TEXCOORD0_1: vec2<f32>;
var<private> in_u002e_var_u002e_COLOR0_1: vec4<f32>;
var<private> out_u002e_var_u002e_SV_Target0_: vec4<f32>;

fn fragmentMain_1() {
    let _e5 = in_u002e_var_u002e_TEXCOORD0_1;
    let _e6 = in_u002e_var_u002e_COLOR0_1;
    let _e7 = textureSample(atlas, atlasSampler, _e5);
    out_u002e_var_u002e_SV_Target0_ = (_e6 * _e7);
    return;
}

@fragment
fn fragmentMain(@location(0) in_u002e_var_u002e_TEXCOORD0_: vec2<f32>, @location(1) in_u002e_var_u002e_COLOR0_: vec4<f32>) -> @location(0) vec4<f32> {
    in_u002e_var_u002e_TEXCOORD0_1 = in_u002e_var_u002e_TEXCOORD0_;
    in_u002e_var_u002e_COLOR0_1 = in_u002e_var_u002e_COLOR0_;
    fragmentMain_1();
    let _e5 = out_u002e_var_u002e_SV_Target0_;
    return _e5;
}
