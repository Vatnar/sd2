struct CameraUBO {
    float4x4 view;
    float4x4 proj;
};

struct MaterialGpuData {
    int32_t albedo_index;
    int32_t normal_index;
    int32_t orm_index;
    int32_t emissive_index;
};

struct ModelPC {
    float4x4 model;
    int32_t material_id;
};

[[vk::binding(0, 0)]] ConstantBuffer<CameraUBO> camera_ubo;
[[vk::binding(0, 1)]] Sampler2D textures[];
[[vk::binding(1, 1)]] StructuredBuffer<MaterialGpuData> material_table;
[[vk::push_constant]] ModelPC model_pc;

struct VSInput {
    float3 in_position;
    float3 in_color;
    float2 in_tex_coord;
};

struct VSOutput {
    float4 pos : SV_Position;
    float3 color;
    float2 frag_tex_coord;
    nointerpolation uint material_id : TEXCOORD2;
};

[shader("vertex")]
VSOutput vert_main(VSInput input) {
    VSOutput output;
    output.pos = mul(camera_ubo.proj, mul(camera_ubo.view, mul(model_pc.model, float4(input.in_position, 1.0))));
    output.color = input.in_color;
    output.frag_tex_coord = input.in_tex_coord;
    output.material_id = model_pc.material_id;
    return output;
}

[shader("fragment")]
float4 frag_main(VSOutput vert_in) : SV_Target
{
    MaterialGpuData mat = material_table[vert_in.material_id];
    float3 color = vert_in.color;
    if (mat.albedo_index >= 0) {
        color = textures[NonUniformResourceIndex(mat.albedo_index)].Sample(vert_in.frag_tex_coord).rgb;
    }
    return float4(color, 1.0);
}
