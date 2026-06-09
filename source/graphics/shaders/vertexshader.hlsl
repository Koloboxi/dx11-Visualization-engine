cbuffer mycBuffer : register(b0)
{
    float4x4 view;
    float4x4 world;
    float4x4 projection;
};

// Global section / clip-plane state (see CB_VS_section on the C++ side).
// Only the main mesh path reaches the rasterizer straight from this VS, so
// SV_ClipDistance here clips solids only; points/lines go through a GS that
// drops the clip distance and are left intact.
cbuffer Section : register(b1)
{
    float4 sectionPlaneNormal;
    float  sectionPlaneD;
    int    sectionEnabled;
    float2 sectionPad;
};

struct VS_INPUT
{
    float3 inPos : POSITION;
    float3 inNormal : NORMAL;
    float4 inColor : COLOR;
};

struct VS_OUTPUT
{
    float4 outPos : SV_POSITION;
    float3 outNormalCam : NORMAL;
    float4 outColor : COLOR;
    float  outClip : SV_ClipDistance0;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    float4 worldPos = mul(float4(input.inPos, 1.0f), world);
    float4 pos = mul(worldPos, view);
    pos = mul(pos, projection);

    output.outPos = pos;

    output.outNormalCam = normalize(mul(input.inNormal, (float3x3)view));
    //output.outNormalCam = input.inNormal;

    output.outColor = input.inColor;

    // Positive => keep the fragment. When sectioning is off, output a constant
    // positive distance so nothing is clipped.
    output.outClip = sectionEnabled
        ? dot(worldPos.xyz, sectionPlaneNormal.xyz) - sectionPlaneD
        : 1.0f;

    return output;
}
