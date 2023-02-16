cbuffer vs_const_buffer_t {
	float4x4 mat_world_view_proj;
	float4 padding[12];
};

struct vs_output_t {
    float4 position : SV_POSITION;
    float4 color : COLOR;
};

vs_output_t main(float3 pos : POSITION, float4 col : COLOR) {
    vs_output_t result;
    result.position = mul(float4(pos.x, pos.y, pos.z, 1.0f), mat_world_view_proj);
    result.color = col;
    return result;
}
