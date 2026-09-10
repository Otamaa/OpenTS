$input v_texcoord0, v_color0, v_depth

#include <bgfx_shader.sh>

SAMPLER2D(s_depth, 0);
SAMPLER2D(s_distorttex, 1);
SAMPLER2D(s_visibility, 3);

// See fs_gpubeam.sc for what each of these carry; identical contract.
uniform vec4 u_depthParams;
uniform vec4 u_beamParams;
uniform vec4 u_beamSheetParams;
uniform vec4 u_visibilityParams;
uniform vec4 u_visibilityFlags;

// x = LaserDistortionDisplacement, y/z/w unused.
uniform vec4 u_distortParams;

vec2 sheet_uv(float frame, vec2 uv, vec2 sheetdim)
{
	float nline = floor(frame / sheetdim.x);
	float nrow = frame - nline * sheetdim.x;
	vec2 singleframedim = vec2(1.0, 1.0) / sheetdim;
	return vec2(nrow, nline) * singleframedim + singleframedim * uv;
}

// Ported from the reference package's dmain: the distortion texture's RG channels are a
// tangent-space-style offset around (0.5, 0.5); this scales that offset by displacement
// and re-centers it back around (0.5, 0.5) for the warp pass to read as a UV bias.
void main()
{
	vec2 depthtexel = (gl_FragCoord.xy - u_depthParams.xy) * u_depthParams.zw;
	if (u_beamParams.x > 0.5) {
		depthtexel.y = 1.0 - depthtexel.y;
	}

	if (depthtexel.x >= 0.0 && depthtexel.x <= 1.0 && depthtexel.y >= 0.0 && depthtexel.y <= 1.0) {
		float scenedepth = texture2D(s_depth, depthtexel).r;
		if (v_depth - 0.0005 >= scenedepth) {
			discard;
		}
	}

	// EXTENSION: same line-of-sight rule as the color pass -- a beam hidden by shroud or
	// fog shouldn't still visibly warp the scene behind it. See fs_gpubeam.sc's copy of
	// this block for the reasoning.
	if (u_visibilityFlags.x > 0.5) {
		vec2 visibletexel = (gl_FragCoord.xy - u_visibilityParams.xy) * u_visibilityParams.zw;
		if (u_beamParams.x > 0.5) {
			visibletexel.y = 1.0 - visibletexel.y;
		}
		if (visibletexel.x >= 0.0 && visibletexel.x <= 1.0 && visibletexel.y >= 0.0 && visibletexel.y <= 1.0) {
			float visibility = texture2D(s_visibility, visibletexel).r;
			if (visibility < 0.9) {
				discard;
			}
		}
	}

	vec2 uv = v_texcoord0;
	if (u_beamSheetParams.w > 0.5) {
		uv = sheet_uv(u_beamSheetParams.x, uv, u_beamSheetParams.yz);
	} else {
		uv.x += u_beamParams.y;
	}

	vec2 offset = texture2D(s_distorttex, uv).rg - vec2(0.5, 0.5);
	vec2 warp = vec2(0.5, 0.5) + u_distortParams.x * offset;

	gl_FragColor = vec4(warp, 1.0, v_color0.a);
}
