struct PS_INPUT
{
    float4 inPosCam : SV_POSITION;
    float3 inNormalCam : NORMAL;
    float4 inColor : COLOR;
};

cbuffer Color : register(b0)
{
    float4 col;
    float ambient;
    float intensity;
    float shininess;
    bool illuminated;
    int  useVertexColor;
    int  twoSided;
    float2 pad;
    float4 backCol;
};

struct PS_OUTPUT
{
    float4 color : SV_Target0;
    float4 mask : SV_Target1;
};

PS_OUTPUT PS_MAIN(PS_INPUT input, bool isFront : SV_IsFrontFace)
{
    PS_OUTPUT o;

    float4 baseCol = useVertexColor ? input.inColor : col;
    float alpha = useVertexColor ? baseCol.a * col.a : baseCol.a;

    float3 n = input.inNormalCam;
    if (twoSided && !isFront)
    {
        baseCol = backCol;
        alpha   = backCol.a;
        n       = -n;
    }

    if (illuminated)
    {
        float3 lightDir = normalize(float3(1, 1, 1));
        float3 viewDir = normalize(float3(0, 0, 1));
        float diff = max(dot(n, lightDir), 0.0f);
        float3 reflectDir = reflect(-lightDir, n);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0f), shininess);
        float illumination = saturate(ambient + diff * intensity);
        o.color = float4(baseCol.rgb * illumination + float3(0.5, 0.5, 0.5) * spec, alpha);
    }
    else
    {
        o.color = float4(baseCol.rgb, alpha);
    }

    o.mask = float4(1, 0, 0, 1);

    return o;
}

