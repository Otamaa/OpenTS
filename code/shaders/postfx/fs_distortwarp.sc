$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_tex, 0);      // The scene to warp (frame texture or an earlier composite).
SAMPLER2D(s_distort, 1);  // The distortion target: rg = warp UV bias, a = its own opacity.

// Ported from the reference package's ReShade LaserBlit.fx pmain: the distortion target's
// rg channels center on (0.5, 0.5) for "no displacement", so (2*rg - 1) recovers a signed
// offset in the -1..1 range, which is then used to bias the sampled UV directly.
void main()
{
	vec4 distort = texture2D(s_distort, v_texcoord0);
	vec2 warped = v_texcoord0 + (2.0 * distort.rg - vec2(1.0, 1.0)) * distort.a;
	gl_FragColor = texture2D(s_tex, warped);
}
