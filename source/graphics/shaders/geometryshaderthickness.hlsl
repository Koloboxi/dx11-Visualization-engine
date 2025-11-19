cbuffer AspectRatio : register(b0)
{
    float aspectRatio;
}

struct VS_OUT
{
    float4 pos : SV_POSITION;
    float3 normal : NORMAL;
};

struct GS_OUT
{
    float4 pos : SV_POSITION;
    float3 normal : NORMAL;
};

[maxvertexcount(4)]
void gs_thick_line(line VS_OUT input[2], inout TriangleStream<GS_OUT> stream)
{
    float thickness = 0.001;
    
    float4 p0 = input[0].pos;
    float4 p1 = input[1].pos;
    
    float2 dir = normalize(p1.xy - p0.xy);
    
    float2 perp = float2(-dir.y, dir.x) * thickness * (1.0f + (aspectRatio - 1.0f) * abs(dir.x));
    
    GS_OUT output;
    output.normal = input[0].normal;
        
    output.pos = float4(p0.xy - perp, p0.z, p0.w);
    stream.Append(output);
    
    output.pos = float4(p1.xy - perp, p1.z, p1.w);
    stream.Append(output);
    
    output.pos = float4(p0.xy + perp, p0.z, p0.w);
    stream.Append(output);
    
    output.pos = float4(p1.xy + perp, p1.z, p1.w);
    stream.Append(output);
    
    stream.RestartStrip();
}