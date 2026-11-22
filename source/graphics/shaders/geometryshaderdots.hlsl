cbuffer AspectRatio : register(b0)
{
    float  aspectRatio;
    float  thickness;
    float2 viewport;    // render target size, pixels
}

struct VS_OUT
{
    float4 pos : SV_POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
    float  clip : SV_ClipDistance0;
};

struct GS_OUT
{
    float4 pos : SV_POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR;
    float  clip : SV_ClipDistance0;
};

// A small filled disc of a fixed size on screen, whatever the zoom: a polygon of
// DOT_SEGMENTS sides, DOT_RADIUS_PX pixels to its corners (about 4 px across).
#define DOT_SEGMENTS  12
#define DOT_RADIUS_PX 2.0

[maxvertexcount(DOT_SEGMENTS)]
void GS_Dot(point VS_OUT input[1], inout TriangleStream<GS_OUT> stream)
{
    GS_OUT o;
    o.normal = input[0].normal;
    o.color  = input[0].color;
    // The dot takes its point's section distance, so a section removes it whole.
    o.clip   = input[0].clip;

    const float4 c = input[0].pos;
    // One pixel in clip space at the point's depth (w = 1 for the orthographic view).
    const float2 r = DOT_RADIUS_PX * 2.0 / max(viewport, float2(1.0, 1.0)) * c.w;

    // The polygon as one strip, zig-zagging between its two halves:
    // corners 0, 1, n-1, 2, n-2, ... (counter-clockwise first triangle).
    for (int i = 0; i < DOT_SEGMENTS; ++i)
    {
        const int k = (i & 1) ? (i + 1) / 2 : (DOT_SEGMENTS - i / 2) % DOT_SEGMENTS;
        const float a = 6.2831853 * k / DOT_SEGMENTS;
        o.pos = c + float4(r.x * cos(a), r.y * sin(a), 0, 0);
        stream.Append(o);
    }
    stream.RestartStrip();
}
