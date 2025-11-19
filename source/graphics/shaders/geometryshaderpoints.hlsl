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

#define SEGMENTS 64 
#define FIXED_RADIUS 0.01

[maxvertexcount(SEGMENTS + 1)]
void GS_Main(
    point VS_OUT input[1],
    inout LineStream<GS_OUT> OutputStream
)
{
    GS_OUT output;
    output.normal = input[0].normal;
    
    float4 center = input[0].pos;
    
    for (int i = 0; i <= SEGMENTS; i++)
    {
        float angle = 2.0 * 3.1415926 * i / SEGMENTS;
        output.pos = center + float4(
            FIXED_RADIUS * cos(angle),
            FIXED_RADIUS * sin(angle) * aspectRatio,
            0,
            0
        );
        OutputStream.Append(output);
    }
    
    OutputStream.RestartStrip();
}