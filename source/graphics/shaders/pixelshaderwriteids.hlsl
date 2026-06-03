cbuffer IDCB : register(b1)
{
	unsigned int id;
	float pad[3];
};

struct PS_INPUT
{
	float4 inPosCam : SV_POSITION;
	float3 inNormalCam : NORMAL;
};

unsigned int PS_WriteIDs(PS_INPUT IN) : SV_Target0
{
	return id;
}
