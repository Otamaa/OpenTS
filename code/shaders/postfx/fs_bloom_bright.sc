$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_tex, 0);

// x = luma threshold, y/z/w unused by this pass.
uniform vec4 u_bloomParams;

void main()
{
	vec4 color = texture2D(s_tex, v_texcoord0);
	float luma = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));

	// Soft knee: pixels at the threshold fade in rather than popping on at full strength,
	// which is what a hard cutoff does and why bloom edges look banded without this.
	float contribution = max(luma - u_bloomParams.x, 0.0) / max(luma, 0.0001);

	gl_FragColor = vec4(color.rgb * contribution, 1.0);
}
