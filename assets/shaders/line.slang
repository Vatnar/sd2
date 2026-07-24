struct LineVSInput {
    float3 in_position;
    float4 in_color;
};

struct LineVSOutput {
    float4 pos : SV_Position;
    float4 color;
};

struct UniformBuffer {
    float4x4 view;
    float4x4 proj;
};

[[vk::binding(0, 0)]] ConstantBuffer<UniformBuffer> ubo;

[shader("vertex")]
LineVSOutput vert_main(LineVSInput input) {
    LineVSOutput output;
    output.pos = mul(ubo.proj, mul(ubo.view, float4(input.in_position, 1.0)));
    output.color = input.in_color;
    return output;
}

[shader("fragment")]
float4 frag_main(LineVSOutput vert_in) : SV_Target {
    return vert_in.color;
}
