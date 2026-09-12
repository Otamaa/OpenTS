$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_tex, 0);         // The composited scene so far.
SAMPLER2D(s_visibility, 1);  // Shroud/fog mask -- see fs_gpubeam.sc for the format.
SAMPLER2D(s_weathertex, 2);  // Cloud shadow texture (red channel used as an alpha mask).
SAMPLER2D(s_watertex, 3);    // Caustic light sheet, animated frames tiled horizontally.

// x/y = where the visibility snapshot's own pixel (0,0) sits in this view's own pixel
// space; z/w = 1/width, 1/height of it.
uniform vec4 u_visibilityParams;

// x = 1.0 when the visibility snapshot's rows run bottom-to-top relative to this view's
// own top-to-bottom pixel space (see fs_gpubeam.sc for the same correction elsewhere).
// y = 1.0 when weather is enabled this frame. z = 1.0 when water is enabled. w = 1.0 when
// a visibility snapshot was uploaded this frame; 0.0 skips gating entirely (both effects
// simply apply everywhere, same fail-open choice every other GPU effect here makes).
uniform vec4 u_atmosphereFlags;

// xy = the cloud texture's own scroll offset this frame, already wrapped to 0..1; z = how
// many times the cloud texture tiles across the screen; w = how strongly it darkens the
// scene, 0 (no effect) to 1 (full multiply).
uniform vec4 u_weatherParams;

// xy = the caustic sheet's own tile-scroll offset this frame, wrapped to 0..1; z = the
// current animation frame, already divided into 0..1 (frame/32, since the sheet the
// reference package's own caustic_map ships is a 32-frame horizontal strip); w = how
// strongly the caustic light brightens the scene.
uniform vec4 u_waterParams;

// How many times the caustic sheet tiles across the screen (x, y) independent of
// u_weatherParams' own tiling, since water and weather scroll and repeat at different
// rates in the reference this is ported from.
uniform vec2 u_waterTiling;

// rgb = the underwater depth-fog tint color; a = how strongly it blends in toward the
// bottom of the screen (0 = no fog tint at all, 1 = fully replaced by fog_color there).
uniform vec4 u_waterFogColor;

void main()
{
	vec4 base = texture2D(s_tex, v_texcoord0);
	vec3 color = base.rgb;
	vec3 tinted = color;

	if (u_atmosphereFlags.y > 0.5) {
		// Ported from the reference package's pixelshader_cloud: a tiled, scrolling
		// grayscale cloud texture read as a straight brightness multiplier.
		vec2 cloudcoord = fract(v_texcoord0 * u_weatherParams.z + u_weatherParams.xy);
		float cloud = texture2D(s_weathertex, cloudcoord).r;
		float shade = mix(1.0, cloud, u_weatherParams.w);
		tinted *= shade;
	}

	if (u_atmosphereFlags.z > 0.5) {
		// Ported from the reference package's pixelshader_caustic_maps: a tiled,
		// scrolling, frame-animated caustic light texture, sampled from its own current
		// animation frame (a horizontal strip, hence dividing x into 32 equal slices and
		// offsetting into the current one -- u_waterParams.z already carries frame/32.0).
		vec2 watercoord = fract(v_texcoord0 * u_waterTiling + u_waterParams.xy);
		watercoord.x = watercoord.x / 32.0 + u_waterParams.z;

		float caustic = texture2D(s_watertex, watercoord).r;
		vec3 lit = color * mix(1.0, caustic, u_waterParams.w);

		// The reference's own vertical depth-fog gradient: more fog toward the bottom of
		// the screen, blended by u_waterFogColor.a.
		float fogamount = v_texcoord0.y * u_waterFogColor.a;
		tinted = lit * (1.0 - fogamount) + u_waterFogColor.rgb * fogamount;
	}

	float explored = 1.0;
	if (u_atmosphereFlags.w > 0.5) {
		vec2 vistexel = (gl_FragCoord.xy - u_visibilityParams.xy) * u_visibilityParams.zw;
		if (u_atmosphereFlags.x > 0.5) {
			vistexel.y = 1.0 - vistexel.y;
		}
		if (vistexel.x >= 0.0 && vistexel.x <= 1.0 && vistexel.y >= 0.0 && vistexel.y <= 1.0) {
			// Hidden only in unexplored shroud (value 0); still shown through fog (value
			// 128) as well as full visibility (255) -- this effect's own gating rule is
			// different from a beam or particle's, which need full visibility instead.
			explored = (texture2D(s_visibility, vistexel).r > 0.1) ? 1.0 : 0.0;
		}
	}

	color = mix(color, tinted, explored);
	gl_FragColor = vec4(color, base.a);
}
