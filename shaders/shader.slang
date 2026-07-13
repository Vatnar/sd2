struct VSInput {
    float2 in_position;
    float3 in_color;
}

struct UniformBuffer {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
}
ConstantBuffer<UniformBuffer> ubo;

struct VSOutput {
    float4 pos : SV_Position;
    float3 color;
}

[shader("vertex")]
VSOutput vert_main(VSInput input) {
    VSOutput output;
    output.pos = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(input.in_position, 0.0, 1.0))));
    output.color = input.in_color;
    return output;
}

[shader("fragment")]
float4 frag_main(VSOutput vert_in) : SV_Target
{
    return float4(vert_in.color, 1.0);
}