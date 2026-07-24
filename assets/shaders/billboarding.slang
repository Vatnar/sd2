struct CameraUBO {
    [[column_major]] float4x4 view;
    [[column_major]] float4x4 proj;
};
struct Arrow {
    float3 origin;
    float3 direction;
    float32_t magnitude;
    float32_t width;
    float32_t roundness;
    float4 color;
    uint32_t flags;
};

 [[vk::binding(0, 0)]] ConstantBuffer<CameraUBO> camera;
 [[vk::binding(0, 1)]] StructuredBuffer<Arrow> arrows;

 struct VSInput {
     uint vid : SV_VertexID;
     uint iid : SV_InstanceID;
 };

 struct VSOutput {
     float4 pos : SV_Position;
     float2 local : TEXCOORD0;
     float4 color : COLOR0;
 };

 [[shader("vertex")]]
 VSOutput vert_main(VSInput input) {
     VSOutput out;
     Arrow arrow = arrows[input.iid];

     const float2 corners[4] = {
         float2(-0.5, -0.5), 
         float2(0.5, -0.5),  
         float2(-0.5, 0.5),  
         float2(0.5, 0.5)    
     };

     const uint corner_index[6] = {
         0, 1, 2,
         2, 1, 3
     };
     float2 c = corners[corner_index[input.vid]];
     float2 local = c * arrow.width;
     float3 cam_right = camera.view[0].xyz;
     float3 cam_up = camera.view[1].xyz;

     float3 world_pos = arrow.origin + cam_right * local.x + cam_up * local.y;

     float4 view = mul(camera.view, float4(world_pos, 1.0));
     float4 clip = mul(camera.proj, view);
     out.pos = clip;
     out.local = c;
     out.color = arrow.color;

     return out;
 }

 [[shader("fragment")]]
 float4 frag_main(VSOutput input) {
     return float4(1.0, 0.0, 0.0, 1.0);
 }

