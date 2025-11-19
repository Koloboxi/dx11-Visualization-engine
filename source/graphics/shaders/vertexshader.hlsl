cbuffer mycBuffer : register(b0)
{
    float4x4 view;
    float4x4 world;
    float4x4 projection;
};

struct VS_INPUT
{
    float3 inPos : POSITION;
    float3 inNormal : NORMAL;
};

struct VS_OUTPUT
{
    float4 outPos : SV_POSITION;
    float3 outNormalCam : NORMAL;
};

VS_OUTPUT main(VS_INPUT input)
{
    VS_OUTPUT output;
    float4 pos = mul(float4(input.inPos, 1.0f), world);
    pos = mul(pos, view);
    pos = mul(pos, projection);
    
    output.outPos = pos;
    
    output.outNormalCam = normalize(mul(input.inNormal, (float3x3)view));
    //output.outNormalCam = input.inNormal;

    return output;
}
