cbuffer vs_const_buffer_t {
	float4x4 mat_world_view_proj;
	float4x4 mat_world_view;
	float4 padding[8];
};

struct vs_output_t {
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 tex : TEXCOORD;
};

vs_output_t main(float3 pos : POSITION, float3 norm : NORMAL, float4 col : COLOR, float2 tex : TEXCOORD) {
    vs_output_t result;
    result.position = mul(float4(pos.x, pos.y, pos.z, 1.0f), mat_world_view_proj);
    result.color = col;
    result.tex = tex;
    return result;
}
