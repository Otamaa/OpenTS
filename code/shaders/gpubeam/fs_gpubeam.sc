$input v_texcoord0, v_color0, v_depth

#include <bgfx_shader.sh>

SAMPLER2D(s_depth, 0);
SAMPLER2D(s_beamtex, 1);
SAMPLER2D(s_ambient, 2);

// x/y = where the depth snapshot's own pixel (0,0) sits in this view's own pixel space;
// z/w = 1/width, 1/height of the depth snapshot texture. The ambient (shroud/lighting)
// snapshot shares this exactly -- DepthBuffer and AlphaBuffer are always constructed
// covering the same Rect -- so there is no separate set of these for it.
uniform vec4 u_depthParams;

// x = 1.0 when the depth snapshot's rows run bottom-to-top relative to this view's own
// top-to-bottom pixel space (the same origin correction every render-to-texture read in
// this renderer already applies). y = a scroll phase for LaserTextureSpeed, already scaled
// to the mapping mode (see Backend_Queue_GPU_Beam). z = 1.0 when a real LaserTexture is
// bound to s_beamtex; 0.0 falls back to the plain procedural gradient. w = 1.0 when an
// ambient snapshot was uploaded this frame; 0.0 skips sampling s_ambient entirely, since
// nothing is bound to it that frame.
uniform vec4 u_beamParams;

// x = current sheet frame index; y/z = LaserTextureSheetHorizontal/Vertical; w = 1.0 when
// the texture is a sheet at all (LaserTextureIsSheet), so a non-sheet texture skips the
// remap entirely rather than dividing by a 1x1 "sheet".
uniform vec4 u_beamSheetParams;

// VERIFY: gl_FragCoord is assumed to be in this view's own top-left-origin pixel space,
// matching every coordinate elsewhere in this renderer. Confirm against a running build;
// if the beam appears to occlude at the wrong height, u_beamParams.x is the first thing to
// check, since that is the one sign this shader cannot determine on its own.

// Ported from the reference package's own compute_uv (laserPSdefinitions.hlsl /
// animPSdefinitions.hlsl): slices a sheet texture into sheetdim.x by sheetdim.y cells,
// row-major, and returns the sub-rect for the given frame with uv mapped inside it.
vec2 sheet_uv(float frame, vec2 uv, vec2 sheetdim)
{
	float nline = floor(frame / sheetdim.x);
	float nrow = frame - nline * sheetdim.x;
	vec2 singleframedim = vec2(1.0, 1.0) / sheetdim;
	return vec2(nrow, nline) * singleframedim + singleframedim * uv;
}

void main()
{
	vec2 sampletexel = (gl_FragCoord.xy - u_depthParams.xy) * u_depthParams.zw;
	if (u_beamParams.x > 0.5) {
		sampletexel.y = 1.0 - sampletexel.y;
	}
	bool insnapshot = sampletexel.x >= 0.0 && sampletexel.x <= 1.0 && sampletexel.y >= 0.0 && sampletexel.y <= 1.0;

	// Outside the snapshot's own bounds there is nothing to occlude against, so the beam
	// simply draws unobstructed there rather than being clipped to the snapshot's extent.
	if (insnapshot) {
		float scenedepth = texture2D(s_depth, sampletexel).r;
		if (v_depth - 0.0005 >= scenedepth) {
			discard;
		}
	}

	float alpha;
	vec3 rgb;

	if (u_beamParams.z > 0.5) {
		// LaserTextureNoStretch (bullet mapping) scrolls the texture along the beam
		// instead of remapping to a sheet cell; the two are mutually exclusive per weapon,
		// same as the reference package.
		vec2 uv = v_texcoord0;
		if (u_beamSheetParams.w > 0.5) {
			uv = sheet_uv(u_beamSheetParams.x, uv, u_beamSheetParams.yz);
		} else {
			uv.x += u_beamParams.y;
		}

		vec4 texel = texture2D(s_beamtex, uv);
		rgb = texel.rgb * v_color0.rgb;
		alpha = texel.a * v_color0.a;
	} else {
		// No LaserTexture was set: the same soft-core, scrolling-stripe placeholder this
		// shader always drew before real texture support existed.
		float across = abs(v_texcoord0.y - 0.5) * 2.0;
		float core = 1.0 - smoothstep(0.0, 1.0, across);
		float stripe = 0.65 + 0.35 * sin((v_texcoord0.x * 18.0) - (u_beamParams.y * 6.28318));
		alpha = core * stripe * v_color0.a;
		rgb = v_color0.rgb;
	}

	// Ported from DSurface::Draw_Depth_Shaded_Line's own darkening: color * ambient >> 7,
	// i.e. divided by 128 rather than the reference's own 127 -- matching this engine's
	// actual buffer convention rather than the reference's, since the two happen to differ
	// by one. A raw value of 0 (fully shrouded) discards outright, the same as that
	// function's own "v != 0" check, rather than fading to black.
	if (u_beamParams.w > 0.5 && insnapshot) {
		float ambient = texture2D(s_ambient, sampletexel).r;
		if (ambient <= 0.0) {
			discard;
		}
		rgb *= ambient;
	}

	gl_FragColor = vec4(rgb * alpha, alpha);
}
