cbuffer AspectRatio : register(b0)
{
    float aspectRatio;
}

struct VS_OUT
{
    float4 pos : SV_POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
};

struct GS_OUT
{
    float4 pos : SV_POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
};

#define FIXED_RADIUS 0.013

// Expands a single point into a screen-facing "+" cross made of two line
// segments. Mirrors geometryshaderpoints.hlsl (the ring skin) in clip-space
// sizing and aspect-ratio correction so both skins look consistent.
[maxvertexcount(4)]
void GS_Main(
    point VS_OUT input[1],
    inout LineStream<GS_OUT> OutputStream
)
{
    GS_OUT output;
    output.normal = input[0].normal;
    output.color = input[0].color;

    float4 center = input[0].pos;
    float rx = FIXED_RADIUS;
    float ry = FIXED_RADIUS * aspectRatio;

    // Horizontal stroke.
    output.pos = center + float4(-rx, 0, 0, 0); OutputStream.Append(output);
    output.pos = center + float4( rx, 0, 0, 0); OutputStream.Append(output);
    OutputStream.RestartStrip();

    // Vertical stroke.
    output.pos = center + float4(0, -ry, 0, 0); OutputStream.Append(output);
    output.pos = center + float4(0,  ry, 0, 0); OutputStream.Append(output);
    OutputStream.RestartStrip();
}
