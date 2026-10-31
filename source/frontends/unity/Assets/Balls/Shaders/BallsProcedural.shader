// Every ball in one draw call. The instance id comes from SV_InstanceID and indexes the same packed 32-bit words
// the C++ core produces (14-bit x, 14-bit y, 4-bit colour), so a ball costs 4 bytes on the bus instead of the 64 a
// Matrix4x4 would.
Shader "Balls/Procedural"
{
    Properties
    {
        _Radius ("Ball radius (cells)", Float) = 0.25
    }

    SubShader
    {
        Tags
        {
            "RenderType" = "Opaque"
            "RenderPipeline" = "UniversalPipeline"
            "Queue" = "Geometry"
        }

        Pass
        {
            Name "Balls"
            Cull Off
            ZWrite On
            ZTest LEqual

            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #pragma target 4.5

            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"

            StructuredBuffer<uint> _Balls;
            float2 _FieldToWorld;   // packed coordinate -> world units, per axis
            float _Radius;
            float4 _Palette[7];

            struct Attributes
            {
                float4 positionOS : POSITION;
                uint instanceID : SV_InstanceID;
            };

            struct Varyings
            {
                float4 positionCS : SV_POSITION;
                float4 color : COLOR0;
                float2 local : TEXCOORD0;
            };

            Varyings vert(Attributes input)
            {
                Varyings output;
                uint packed = _Balls[input.instanceID];
                // An empty visibility slot is all ones (colour 15, the far corner): collapse it rather than draw a
                // stray ball there, as SDL's renderer skips its emptySlot.
                if ((packed >> 28u) >= 7u)
                {
                    output.positionCS = float4(0.0, 0.0, 0.0, 0.0);
                    output.color = 0.0;
                    output.local = 0.0;
                    return output;
                }
                float2 center = float2(packed & 0x3FFFu, (packed >> 14u) & 0x3FFFu) * _FieldToWorld;
                float3 world = float3(center + input.positionOS.xy * _Radius, 0.0);
                output.positionCS = mul(UNITY_MATRIX_VP, float4(world, 1.0));
                output.color = _Palette[packed >> 28u];
                output.local = input.positionOS.xy;
                return output;
            }

            half4 frag(Varyings input) : SV_Target
            {
                // The instance is a quad; balls are round.
                if (dot(input.local, input.local) > 1.0)
                {
                    discard;
                }
                return half4(input.color.rgb, 1.0);
            }
            ENDHLSL
        }
    }

    Fallback Off
}
