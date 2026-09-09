$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_tex, 0);

// xy = per-texel step along the blur axis (1/width,0 for the horizontal pass, 0,1/height
// for the vertical one); z/w unused.
uniform vec4 u_blurDir;

// 5 tap linear-sampled Gaussian: sampling between texel centers lets two taps stand in
// for four, so this reaches the same falloff as a naive 9 tap kernel for less than half
// the texture fetches.
void main()
{
	vec2 texelstep = u_blurDir.xy;

	vec4 sum = texture2D(s_tex, v_texcoord0) * 0.227027;
	sum += texture2D(s_tex, v_texcoord0 + texelstep * 1.384615) * 0.316216;
	sum += texture2D(s_tex, v_texcoord0 - texelstep * 1.384615) * 0.316216;
	sum += texture2D(s_tex, v_texcoord0 + texelstep * 3.230769) * 0.070270;
	sum += texture2D(s_tex, v_texcoord0 - texelstep * 3.230769) * 0.070270;

	gl_FragColor = vec4(sum.rgb, 1.0);
}
