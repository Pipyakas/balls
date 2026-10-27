// Board cells and launcher slots in one draw call. SV_InstanceID indexes a structured buffer of quads, so no
// per-instance matrix or vertex attribute is needed.
Shader "Balls/Quads"
{
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
            Name "Quads"
            Cull Off
            ZWrite On
            ZTest LEqual

            HLSLPROGRAM
            #pragma vertex vert
            #pragma fragment frag
            #pragma target 4.5

            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"

            struct QuadInstance
            {
                float2 center;   // world units
                float2 size;     // half extents, world units
                float4 color;
            };

            StructuredBuffer<QuadInstance> _Quads;

            struct Attributes
            {
                float4 positionOS : POSITION;
                uint instanceID : SV_InstanceID;
            };

            struct Varyings
            {
                float4 positionCS : SV_POSITION;
                float4 color : COLOR0;
            };

            Varyings vert(Attributes input)
            {
                Varyings output;
                QuadInstance instance = _Quads[input.instanceID];
                float3 world = float3(instance.center + input.positionOS.xy * instance.size, 0.0);
                output.positionCS = mul(UNITY_MATRIX_VP, float4(world, 1.0));
                output.color = instance.color;
                return output;
            }

            half4 frag(Varyings input) : SV_Target
            {
                return half4(input.color.rgb, 1.0);
            }
            ENDHLSL
        }
    }

    Fallback Off
}
