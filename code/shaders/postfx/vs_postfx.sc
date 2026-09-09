$input a_position, a_texcoord0, a_color0
$output v_texcoord0, v_color0

#include <bgfx_shader.sh>

// Every postfx pass draws one screen-covering quad in the target's own pixel space, so
// this is the same orthographic transform Set_View_Transform builds for that view.
void main()
{
	gl_Position = mul(u_modelViewProj, vec4(a_position, 0.0, 1.0));
	v_texcoord0 = a_texcoord0;
	v_color0 = a_color0;
}
