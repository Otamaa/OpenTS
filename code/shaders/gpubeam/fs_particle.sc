$input v_texcoord0, v_color0, v_depth

#include <bgfx_shader.sh>

SAMPLER2D(s_depth, 0);
SAMPLER2D(s_visibility, 1);
SAMPLER2D(s_particletex, 2);

// See fs_gpubeam.sc for what each of these carry; identical contract.
uniform vec4 u_depthParams;
uniform vec4 u_visibilityParams;
uniform vec4 u_visibilityFlags;

// x = origin flip flag (see fs_gpubeam.sc). y = 1.0 when a real texture is bound to
// s_particletex; 0.0 falls back to the plain soft circular falloff. z/w unused.
uniform vec4 u_particleParams;

// VERIFY: see fs_gpubeam.sc's own note on gl_FragCoord; the same assumption applies here.
void main()
{
	vec2 sampletexel = (gl_FragCoord.xy - u_depthParams.xy) * u_depthParams.zw;
	if (u_particleParams.x > 0.5) {
		sampletexel.y = 1.0 - sampletexel.y;
	}
	if (sampletexel.x >= 0.0 && sampletexel.x <= 1.0 && sampletexel.y >= 0.0 && sampletexel.y <= 1.0) {
		float scenedepth = texture2D(s_depth, sampletexel).r;
		if (v_depth - 0.0005 >= scenedepth) {
			discard;
		}
	}

	// EXTENSION: a particle only draws where the player currently has line of sight --
	// shroud and fog both hide it, same as a beam or a unit. See fs_gpubeam.sc's own copy
	// of this block for the reasoning.
	if (u_visibilityFlags.x > 0.5) {
		vec2 visibletexel = (gl_FragCoord.xy - u_visibilityParams.xy) * u_visibilityParams.zw;
		if (u_particleParams.x > 0.5) {
			visibletexel.y = 1.0 - visibletexel.y;
		}
		if (visibletexel.x >= 0.0 && visibletexel.x <= 1.0 && visibletexel.y >= 0.0 && visibletexel.y <= 1.0) {
			float visibility = texture2D(s_visibility, visibletexel).r;
			if (visibility < 0.9) {
				discard;
			}
		}
	}

	vec3 rgb;
	float alpha;

	if (u_particleParams.y > 0.5) {
		vec4 texel = texture2D(s_particletex, v_texcoord0);
		rgb = texel.rgb * v_color0.rgb;
		alpha = texel.a * v_color0.a;
	} else {
		// A soft circular falloff from the quad's center -- the common case for sparks,
		// embers, and smoke puffs that don't need a real texture asset.
		float dist = length(v_texcoord0 - vec2(0.5, 0.5)) * 2.0;
		alpha = (1.0 - smoothstep(0.0, 1.0, dist)) * v_color0.a;
		rgb = v_color0.rgb;
	}

	gl_FragColor = vec4(rgb * alpha, alpha);
}
