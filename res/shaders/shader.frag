#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 frag_color;
layout(location = 0) out vec4 gbuffer_position;
layout(location = 1) out vec4 gbuffer_albedo;
layout(location = 2) out vec4 gbuffer_normal;

void main()
{
    gbuffer_position = vec4(frag_color, 1.0);
    gbuffer_albedo = vec4(frag_color, 1.0);
    gbuffer_normal = vec4(frag_color, 1.0);
}
