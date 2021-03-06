// = INCLUDES ========
#include "Common.hlsl"
//====================

cbuffer MiscBuffer
{
	matrix mTransform;
};

struct VS_Output
{
    float4 position : SV_POSITION;
};

// Vertex Shader
VS_Output DirectusVertexShader(Vertex_Pos input)
{
	VS_Output output;
     
    input.position.w 	= 1.0f;
    output.position 	= mul(input.position, mTransform);
	
	return output;
}

// Pixel Shader
float DirectusPixelShader(VS_Output input) : SV_TARGET
{
	return input.position.z / input.position.w;
}