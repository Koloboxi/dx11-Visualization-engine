Texture2D outlineTex : register(t1);
Texture2D mainRTVTex : register(t2);
SamplerState samp : register(s0);

struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

float4 PS_OutlineMerge(VS_OUTPUT IN) : SV_Target0
{
    float4 outlineCol = outlineTex.Sample(samp, IN.uv);
    float4 sceneCol = mainRTVTex.Sample(samp, IN.uv);
    
    float4 finalCol = outlineCol + sceneCol;
    finalCol.a = 1.0f;    
    return finalCol;
}
