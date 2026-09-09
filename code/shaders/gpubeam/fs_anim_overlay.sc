$input v_texcoord0, v_color0, v_depth

#include <bgfx_shader.sh>

SAMPLER2D(s_depth, 0);
SAMPLER2D(s_tex, 1);
SAMPLER2D(s_ambient, 2);

// See fs_gpubeam.sc for what each of these carry; identical contract, reused as-is since
// an overlay anim's depth test works exactly the same way a beam's does.
uniform vec4 u_depthParams;

// x = origin flip flag (see fs_gpubeam.sc); y unused; z unused; w = 1.0 when an ambient
// snapshot was uploaded this frame.
uniform vec4 u_beamParams;

// VERIFY: see fs_gpubeam.sc's own note on gl_FragCoord; the same assumption applies here.
void main()
{
	vec2 sampletexel = (gl_FragCoord.xy - u_depthParams.xy) * u_depthParams.zw;
	if (u_beamParams.x > 0.5) {
		sampletexel.y = 1.0 - sampletexel.y;
	}
	bool insnapshot = sampletexel.x >= 0.0 && sampletexel.x <= 1.0 && sampletexel.y >= 0.0 && sampletexel.y <= 1.0;

	if (insnapshot) {
		float scenedepth = texture2D(s_depth, sampletexel).r;
		if (v_depth - 0.0005 >= scenedepth) {
			discard;
		}
	}

	vec4 texel = texture2D(s_tex, v_texcoord0);
	vec3 rgb = texel.rgb * v_color0.rgb;
	float alpha = texel.a * v_color0.a;

	// Ported from DSurface::Draw_Depth_Shaded_Line's own darkening; see fs_gpubeam.sc for
	// why this divides by 128 rather than the reference package's 127.
	if (u_beamParams.w > 0.5 && insnapshot) {
		float ambient = texture2D(s_ambient, sampletexel).r;
		if (ambient <= 0.0) {
			discard;
		}
		rgb *= ambient;
	}

	gl_FragColor = vec4(rgb * alpha, alpha);
}
