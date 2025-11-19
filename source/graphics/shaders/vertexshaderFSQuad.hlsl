struct VS_OUTPUT
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

VS_OUTPUT VS_FullscreenQuad(uint id : SV_VertexID)
{
    float2 verts[6] =
    {
        float2(-1, -1), float2(1, -1), float2(-1, 1),
        float2(-1, 1), float2(1, -1), float2(1, 1)
    };

    VS_OUTPUT o;
    o.pos = float4(verts[id], 0, 1);
    o.uv = verts[id] * 0.5f + 0.5f;
    o.uv.y = 1.0f - o.uv.y;
    return o;
}
