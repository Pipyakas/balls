var<private> in_u002e_var_u002e_TEXCOORD0_1: vec2<f32>;
var<private> in_u002e_var_u002e_TEXCOORD1_1: vec2<f32>;
var<private> in_u002e_var_u002e_TEXCOORD2_1: vec4<f32>;
var<private> in_u002e_var_u002e_TEXCOORD3_1: vec4<f32>;
var<private> out_u002e_var_u002e_SV_Target0_: vec4<f32>;

fn fragmentMain_1() {
    var phi_65_: bool;
    var phi_69_: vec3<f32>;
    var phi_76_: bool;
    var phi_82_: bool;
    var phi_87_: bool;
    var phi_93_: bool;
    var phi_98_: vec3<f32>;

    let _e13 = in_u002e_var_u002e_TEXCOORD0_1;
    let _e14 = in_u002e_var_u002e_TEXCOORD1_1;
    let _e15 = in_u002e_var_u002e_TEXCOORD2_1;
    let _e16 = in_u002e_var_u002e_TEXCOORD3_1;
    let _e22 = min(_e16.x, (min(_e14.x, _e14.y) * 0.5f));
    let _e25 = (_e14 * 0.5f);
    let _e30 = (abs((_e13 - _e25)) - (_e25 - vec2(_e22)));
    let _e40 = clamp((0.5f - ((length(max(_e30, vec2<f32>(0f, 0f))) + min(max(_e30.x, _e30.y), 0f)) - _e22)), 0f, 1f);
    if (_e40 <= 0f) {
        discard;
    }
    let _e42 = _e15.xyz;
    phi_65_ = false;
    if (_e16.y > 0f) {
        phi_65_ = (_e13.y > (_e14.y - _e16.y));
    }
    let _e48 = phi_65_;
    phi_69_ = _e42;
    if _e48 {
        phi_69_ = (_e42 * 0.7f);
    }
    let _e51 = phi_69_;
    let _e52 = (_e16.z * 0.5f);
    phi_76_ = false;
    if (_e16.z > 0f) {
        phi_76_ = (_e13.y > _e52);
    }
    let _e57 = phi_76_;
    phi_82_ = false;
    if _e57 {
        phi_82_ = (_e13.y < (_e52 + _e16.z));
    }
    let _e62 = phi_82_;
    phi_87_ = false;
    if _e62 {
        phi_87_ = (_e13.x > _e16.z);
    }
    let _e66 = phi_87_;
    phi_93_ = false;
    if _e66 {
        phi_93_ = (_e13.x < (_e14.x - _e16.z));
    }
    let _e71 = phi_93_;
    phi_98_ = _e51;
    if _e71 {
        phi_98_ = min((_e51 * 1.25f), vec3<f32>(1f, 1f, 1f));
    }
    let _e75 = phi_98_;
    out_u002e_var_u002e_SV_Target0_ = vec4<f32>(_e75.x, _e75.y, _e75.z, (_e15.w * _e40));
    return;
}

@fragment
fn fragmentMain(@location(0) in_u002e_var_u002e_TEXCOORD0_: vec2<f32>, @location(1) @interpolate(flat) in_u002e_var_u002e_TEXCOORD1_: vec2<f32>, @location(2) @interpolate(flat) in_u002e_var_u002e_TEXCOORD2_: vec4<f32>, @location(3) @interpolate(flat) in_u002e_var_u002e_TEXCOORD3_: vec4<f32>) -> @location(0) vec4<f32> {
    in_u002e_var_u002e_TEXCOORD0_1 = in_u002e_var_u002e_TEXCOORD0_;
    in_u002e_var_u002e_TEXCOORD1_1 = in_u002e_var_u002e_TEXCOORD1_;
    in_u002e_var_u002e_TEXCOORD2_1 = in_u002e_var_u002e_TEXCOORD2_;
    in_u002e_var_u002e_TEXCOORD3_1 = in_u002e_var_u002e_TEXCOORD3_;
    fragmentMain_1();
    let _e9 = out_u002e_var_u002e_SV_Target0_;
    return _e9;
}
