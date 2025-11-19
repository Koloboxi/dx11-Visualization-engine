struct PS_INPUT
{
    float4 inPosCam : SV_POSITION;
    float3 inNormalCam : NORMAL;
};

cbuffer Color : register(b0)
{
    float4 col;
    float ambient;
    float intensity;
    float shininess;
    bool illuminated;
};

struct PS_OUTPUT
{
    float4 color : SV_Target0;
    float4 mask : SV_Target1;
};

PS_OUTPUT PS_MAIN(PS_INPUT input)
{
    PS_OUTPUT o;
    
    if (illuminated)
    {
        float3 lightDir = normalize(float3(1, 1, 1));
        float3 viewDir = normalize(float3(0, 0, 1));
        float diff = max(dot(input.inNormalCam, lightDir), 0.0f);
        float3 reflectDir = reflect(-lightDir, input.inNormalCam);
        float spec = pow(max(dot(viewDir, reflectDir), 0.0f), shininess);
        float illumination = saturate(ambient + diff * intensity);
        o.color = float4(col.rgb * illumination + float3(0.5, 0.5, 0.5) * spec, col.a);
    }
    else
    {
        o.color = col.rgba;
    }
    
    
    o.mask = float4(1, 0, 0, 1);

    return o;
}

