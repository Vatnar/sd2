struct VSInput {
    float3 in_position;
    float3 in_color;
    float2 in_tex_coord;
};

struct UniformBuffer {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
};
ConstantBuffer<UniformBuffer> ubo;
Sampler2D texture;

struct VSOutput {
    float4 pos : SV_Position;
    float3 color;
    float2 frag_tex_coord;
};

[shader("vertex")]
VSOutput vert_main(VSInput input) {
    VSOutput output;
    output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(input.in_position, 1.0))));
    output.color = input.in_color;
    output.frag_tex_coord = input.in_tex_coord;
    return output;
}

[shader("fragment")]
float4 frag_main(VSOutput vert_in) : SV_Target
{
    return float4(vert_in.color * texture.Sample(vert_in.frag_tex_coord).rgb, 1.0);
}