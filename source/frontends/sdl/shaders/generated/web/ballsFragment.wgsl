var<private> in_u002e_var_u002e_TEXCOORD0_1: vec2<f32>;
var<private> in_u002e_var_u002e_TEXCOORD1_1: vec3<f32>;
var<private> out_u002e_var_u002e_SV_Target0_: vec4<f32>;

fn fragmentMain_1() {
    let _e12 = in_u002e_var_u002e_TEXCOORD0_1;
    let _e13 = in_u002e_var_u002e_TEXCOORD1_1;
    let _e14 = dot(_e12, _e12);
    if (_e14 > 1f) {
        discard;
    }
    let _e20 = clamp((1f - (length((_e12 - vec2<f32>(-0.35f, -0.4f))) * 1.6f)), 0f, 1f);
    let _e28 = ((_e13 * (0.65f + (0.35f * (1f - _e14)))) + vec3(((_e20 * _e20) * 0.55f)));
    out_u002e_var_u002e_SV_Target0_ = vec4<f32>(_e28.x, _e28.y, _e28.z, 1f);
    return;
}

@fragment
fn fragmentMain(@location(0) in_u002e_var_u002e_TEXCOORD0_: vec2<f32>, @location(1) @interpolate(flat) in_u002e_var_u002e_TEXCOORD1_: vec3<f32>) -> @location(0) vec4<f32> {
    in_u002e_var_u002e_TEXCOORD0_1 = in_u002e_var_u002e_TEXCOORD0_;
    in_u002e_var_u002e_TEXCOORD1_1 = in_u002e_var_u002e_TEXCOORD1_;
    fragmentMain_1();
    let _e5 = out_u002e_var_u002e_SV_Target0_;
    return _e5;
}
