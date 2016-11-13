#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform ubo_transforms
{
    mat4 model;
    mat4 view;
    mat4 projection;
} transforms;


layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec3 frag_color;

out gl_PerVertex
{
    vec4 gl_Position;
};

void main()
{
    gl_Position = transforms.projection *
        transforms.view *
        transforms.model *
        vec4(in_position, 1.0);
    frag_color = in_color;
}
