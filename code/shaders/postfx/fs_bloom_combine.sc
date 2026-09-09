$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_tex, 0);   // The frame as the game rendered it.
SAMPLER2D(s_bloom, 1); // The blurred bright-pass result, at the frame's own resolution.

// x unused here (only the bright pass reads the threshold), y = bloom intensity.
uniform vec4 u_bloomParams;

void main()
{
	vec4 base = texture2D(s_tex, v_texcoord0);
	vec4 bloom = texture2D(s_bloom, v_texcoord0);

	gl_FragColor = vec4(base.rgb + bloom.rgb * u_bloomParams.y, base.a);
}
