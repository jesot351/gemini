#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 in_position_view;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;

layout(location = 0) out vec4 gbuffer_position;
layout(location = 1) out vec4 gbuffer_albedo;
layout(location = 2) out vec4 gbuffer_normal;

void main()
{
    gbuffer_position = vec4(in_position_view, 1.0f);
    gbuffer_albedo = vec4(in_uv, 0.0f, 1.0f);
    gbuffer_normal = vec4(in_normal, 0.0f);
}
