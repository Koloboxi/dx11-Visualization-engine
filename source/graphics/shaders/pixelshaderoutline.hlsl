Texture2D maskTex : register(t0);
SamplerState samp : register(s0);

cbuffer ScreenSizeCB : register(b0)
{
    float4 outlineColor;
    float2 screenSize;
    float outlineScale;
    float pad;
};

struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

float4 PS_Outline(VS_OUTPUT IN) : SV_Target2
{
    float2 texel = 1.0f / screenSize;
    
    float tl = maskTex.Sample(samp, IN.uv + texel * float2(-1, -1)).r;
    float tc = maskTex.Sample(samp, IN.uv + texel * float2(0, -1)).r;
    float tr = maskTex.Sample(samp, IN.uv + texel * float2(1, -1)).r;
    
    float cl = maskTex.Sample(samp, IN.uv + texel * float2(-1, 0)).r;
    float cr = maskTex.Sample(samp, IN.uv + texel * float2(1, 0)).r;
    
    float bl = maskTex.Sample(samp, IN.uv + texel * float2(-1, 1)).r;
    float bc = maskTex.Sample(samp, IN.uv + texel * float2(0, 1)).r;
    float br = maskTex.Sample(samp, IN.uv + texel * float2(1, 1)).r;

    float gx = -tl - 2.0 * cl - bl + tr + 2.0 * cr + br;
    float gy = tl + 2.0 * tc + tr - bl - 2.0 * bc - br;
    
    float g = sqrt(gx * gx + gy * gy);
    
    float alpha = saturate(g * outlineScale);

    return float4(outlineColor.rgb, outlineColor.a * alpha);
}
