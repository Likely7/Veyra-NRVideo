// Veyra for OBS — video filter.
//
// OBS owns capture, decode, composition and encode, so this module contributes
// only the per-frame GPU enhancement chain that is portable to Linux:
//
//   source frame -> 3D LUT -> colour grade -> sharpen -> FXAA -> output
//
// The Windows product's headline features are NGX-bound (DLSS SR/FG, RTX Video
// SR/HDR, DeepDVC) and have no public Linux SDK; they are deliberately absent
// rather than stubbed. See docs/OBS_FILTER.md.

#include <obs-module.h>
#include <graphics/graphics.h>
#include <util/dstr.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("veyra-filter", "en-US")

#define VEYRA_FILTER_ID "veyra_filter"
#define VEYRA_MAX_LUT_SIZE 64

struct veyra_filter {
	obs_source_t *context;

	gs_effect_t *effect;
	gs_eparam_t *param_grade;
	gs_eparam_t *param_texel;
	gs_eparam_t *param_lut_enabled;
	gs_eparam_t *param_lut_amount;
	gs_eparam_t *param_lut_domain_min;
	gs_eparam_t *param_lut_domain_max;
	gs_eparam_t *param_lut_texture;

	gs_texture_t *lut;          // volume texture, owned
	char *lut_path;             // as configured
	char *lut_loaded_path;      // last file that produced `lut`
	float lut_domain_min[3];
	float lut_domain_max[3];

	bool fxaa;
	float exposure;
	float contrast;
	float saturation;
	float sharpen;
	int lut_strength;
};

/* ------------------------------------------------------------------------- */
/* .cube parsing                                                             */
/* ------------------------------------------------------------------------- */

struct cube_lut {
	int size;
	float domain_min[3];
	float domain_max[3];
	uint8_t *rgba; // size^3 * 4
};

// Parses a 3D .cube LUT. Red varies fastest, then green, then blue, matching
// the format used by Resolve/Premiere and every other .cube consumer.
static bool cube_parse(const char *path, struct cube_lut *out)
{
	FILE *file = fopen(path, "rb");
	if (!file) {
		blog(LOG_ERROR, "[veyra] cannot open LUT '%s'", path);
		return false;
	}

	memset(out, 0, sizeof(*out));
	out->domain_max[0] = out->domain_max[1] = out->domain_max[2] = 1.0f;

	int entries = 0;
	char line[512];
	bool ok = false;

	while (fgets(line, sizeof(line), file)) {
		float r, g, b;

		if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
			continue;
		if (sscanf(line, "LUT_3D_SIZE %d", &out->size) == 1) {
			if (out->size < 2 || out->size > VEYRA_MAX_LUT_SIZE) {
				blog(LOG_ERROR, "[veyra] LUT size %d out of range (2..%d)", out->size,
				     VEYRA_MAX_LUT_SIZE);
				goto done;
			}
			size_t cells = (size_t)out->size * out->size * out->size * 4;
			out->rgba = bmalloc(cells);
			continue;
		}
		if (sscanf(line, "DOMAIN_MIN %f %f %f", &r, &g, &b) == 3) {
			out->domain_min[0] = r;
			out->domain_min[1] = g;
			out->domain_min[2] = b;
			continue;
		}
		if (sscanf(line, "DOMAIN_MAX %f %f %f", &r, &g, &b) == 3) {
			out->domain_max[0] = r;
			out->domain_max[1] = g;
			out->domain_max[2] = b;
			continue;
		}
		if (sscanf(line, "%f %f %f", &r, &g, &b) != 3)
			continue;
		if (!out->rgba || entries >= out->size * out->size * out->size)
			continue;

		out->rgba[entries * 4 + 0] = (uint8_t)(fminf(fmaxf(r, 0.0f), 1.0f) * 255.0f + 0.5f);
		out->rgba[entries * 4 + 1] = (uint8_t)(fminf(fmaxf(g, 0.0f), 1.0f) * 255.0f + 0.5f);
		out->rgba[entries * 4 + 2] = (uint8_t)(fminf(fmaxf(b, 0.0f), 1.0f) * 255.0f + 0.5f);
		out->rgba[entries * 4 + 3] = 255;
		entries++;
	}

	ok = entries == out->size * out->size * out->size;
	if (!ok)
		blog(LOG_ERROR, "[veyra] LUT '%s' has %d of %d entries", path, entries,
		     out->size * out->size * out->size);

done:
	fclose(file);
	if (!ok) {
		bfree(out->rgba);
		out->rgba = NULL;
	}
	return ok;
}

static void load_lut(struct veyra_filter *filter)
{
	if (!filter->lut_path || !*filter->lut_path) {
		if (filter->lut) {
			obs_enter_graphics();
			gs_texture_destroy(filter->lut);
			obs_leave_graphics();
			filter->lut = NULL;
		}
		bfree(filter->lut_loaded_path);
		filter->lut_loaded_path = NULL;
		return;
	}
	if (filter->lut_loaded_path && strcmp(filter->lut_loaded_path, filter->lut_path) == 0)
		return;

	struct cube_lut cube;
	if (!cube_parse(filter->lut_path, &cube))
		return;

	obs_enter_graphics();
	if (filter->lut)
		gs_texture_destroy(filter->lut);
	const uint8_t *data = cube.rgba;
	filter->lut = gs_voltexture_create((uint32_t)cube.size, (uint32_t)cube.size,
					  (uint32_t)cube.size, GS_RGBA, 1, &data, 0);
	obs_leave_graphics();
	bfree(cube.rgba);

	if (!filter->lut) {
		blog(LOG_ERROR, "[veyra] volume texture creation failed for '%s'", filter->lut_path);
		return;
	}

	memcpy(filter->lut_domain_min, cube.domain_min, sizeof(cube.domain_min));
	memcpy(filter->lut_domain_max, cube.domain_max, sizeof(cube.domain_max));
	bfree(filter->lut_loaded_path);
	filter->lut_loaded_path = bstrdup(filter->lut_path);
	blog(LOG_INFO, "[veyra] LUT loaded: %d^3 from '%s'", cube.size, filter->lut_path);
}

static void free_lut(struct veyra_filter *filter)
{
	if (filter->lut) {
		obs_enter_graphics();
		gs_texture_destroy(filter->lut);
		obs_leave_graphics();
		filter->lut = NULL;
	}
	bfree(filter->lut_loaded_path);
	filter->lut_loaded_path = NULL;
}

/* ------------------------------------------------------------------------- */
/* source callbacks                                                          */
/* ------------------------------------------------------------------------- */

static const char *veyra_filter_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("VeyraFilter");
}

static void veyra_filter_update(void *data, obs_data_t *settings)
{
	struct veyra_filter *filter = data;

	filter->fxaa = obs_data_get_bool(settings, "fxaa");
	filter->exposure = (float)obs_data_get_double(settings, "exposure");
	filter->contrast = (float)obs_data_get_double(settings, "contrast");
	filter->saturation = (float)obs_data_get_double(settings, "saturation");
	filter->sharpen = (float)obs_data_get_double(settings, "sharpen");
	filter->lut_strength = (int)obs_data_get_int(settings, "lut_strength");

	bfree(filter->lut_path);
	const char *path = obs_data_get_string(settings, "lut_file");
	filter->lut_path = bstrdup(path ? path : "");
	load_lut(filter);
}

static void *veyra_filter_create(obs_data_t *settings, obs_source_t *context)
{
	struct veyra_filter *filter = bzalloc(sizeof(*filter));
	filter->context = context;
	filter->lut_domain_max[0] = filter->lut_domain_max[1] = filter->lut_domain_max[2] = 1.0f;

	char *path = obs_module_file("veyra.effect");
	char *error = NULL;
	// Effect compilation talks to the graphics backend, which the graphics
	// thread owns; frontend callbacks must borrow it.
	obs_enter_graphics();
	filter->effect = gs_effect_create_from_file(path, &error);
	obs_leave_graphics();
	if (!filter->effect)
		blog(LOG_ERROR, "[veyra] effect load failed: %s", error ? error : "(no error text)");
	bfree(error);
	bfree(path);

	if (filter->effect) {
		filter->param_grade = gs_effect_get_param_by_name(filter->effect, "grade");
		filter->param_texel = gs_effect_get_param_by_name(filter->effect, "texel");
		filter->param_lut_enabled = gs_effect_get_param_by_name(filter->effect, "lutEnabled");
		filter->param_lut_amount = gs_effect_get_param_by_name(filter->effect, "lutAmount");
		filter->param_lut_domain_min =
			gs_effect_get_param_by_name(filter->effect, "lutDomainMin");
		filter->param_lut_domain_max =
			gs_effect_get_param_by_name(filter->effect, "lutDomainMax");
		filter->param_lut_texture = gs_effect_get_param_by_name(filter->effect, "lut3d");
	}

	veyra_filter_update(filter, settings);
	return filter;
}

static void veyra_filter_destroy(void *data)
{
	struct veyra_filter *filter = data;

	free_lut(filter);
	if (filter->effect) {
		obs_enter_graphics();
		gs_effect_destroy(filter->effect);
		obs_leave_graphics();
		filter->effect = NULL;
	}
	bfree(filter->lut_path);
	bfree(filter);
}

static void veyra_filter_render(void *data, gs_effect_t *unused)
{
	struct veyra_filter *filter = data;
	obs_source_t *target = obs_filter_get_target(filter->context);

	UNUSED_PARAMETER(unused);

	if (!target || !filter->effect) {
		obs_source_skip_video_filter(filter->context);
		return;
	}

	const uint32_t width = obs_source_get_base_width(target);
	const uint32_t height = obs_source_get_base_height(target);
	if (!width || !height ||
	    !obs_source_process_filter_begin(filter->context, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING)) {
		obs_source_skip_video_filter(filter->context);
		return;
	}

	struct vec4 grade;
	vec4_set(&grade, filter->exposure, filter->contrast, filter->saturation,
		 filter->fxaa ? 1.0f : 0.0f);
	gs_effect_set_vec4(filter->param_grade, &grade);

	struct vec4 texel;
	vec4_set(&texel, 1.0f / (float)width, 1.0f / (float)height, filter->sharpen, 0.0f);
	gs_effect_set_vec4(filter->param_texel, &texel);

	const bool lut_on = filter->lut != NULL && filter->lut_strength > 0;
	gs_effect_set_bool(filter->param_lut_enabled, lut_on);
	gs_effect_set_float(filter->param_lut_amount, (float)filter->lut_strength / 100.0f);
	if (lut_on) {
		gs_effect_set_texture(filter->param_lut_texture, filter->lut);
		struct vec3 domain_min, domain_max;
		vec3_set(&domain_min, filter->lut_domain_min[0], filter->lut_domain_min[1],
			 filter->lut_domain_min[2]);
		vec3_set(&domain_max, filter->lut_domain_max[0], filter->lut_domain_max[1],
			 filter->lut_domain_max[2]);
		gs_effect_set_vec3(filter->param_lut_domain_min, &domain_min);
		gs_effect_set_vec3(filter->param_lut_domain_max, &domain_max);
	}

	obs_source_process_filter_tech_end(filter->context, filter->effect, width, height, "Draw");
}

static void veyra_filter_defaults(obs_data_t *settings)
{
	obs_data_set_default_bool(settings, "fxaa", true);
	obs_data_set_default_double(settings, "exposure", 0.0);
	obs_data_set_default_double(settings, "contrast", 1.0);
	obs_data_set_default_double(settings, "saturation", 1.0);
	obs_data_set_default_double(settings, "sharpen", 0.0);
	obs_data_set_default_string(settings, "lut_file", "");
	obs_data_set_default_int(settings, "lut_strength", 100);
}

static obs_properties_t *veyra_filter_properties(void *data)
{
	UNUSED_PARAMETER(data);

	obs_properties_t *props = obs_properties_create();
	obs_properties_set_flags(props, OBS_PROPERTIES_DEFER_UPDATE);

	obs_properties_add_bool(props, "fxaa", obs_module_text("FXAA"));
	obs_properties_add_float_slider(props, "exposure", obs_module_text("Exposure"), -2.0, 2.0, 0.05);
	obs_properties_add_float_slider(props, "contrast", obs_module_text("Contrast"), 0.5, 2.0, 0.01);
	obs_properties_add_float_slider(props, "saturation", obs_module_text("Saturation"), 0.0, 2.0, 0.01);
	obs_properties_add_float_slider(props, "sharpen", obs_module_text("Sharpen"), 0.0, 1.0, 0.01);

	obs_properties_add_path(props, "lut_file", obs_module_text("LutFile"), OBS_PATH_FILE,
				obs_module_text("LutFilter"), NULL);
	obs_properties_add_int_slider(props, "lut_strength", obs_module_text("LutStrength"), 0, 100, 1);
	return props;
}

struct obs_source_info veyra_filter_info = {
	.id = VEYRA_FILTER_ID,
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO,
	.get_name = veyra_filter_name,
	.create = veyra_filter_create,
	.destroy = veyra_filter_destroy,
	.update = veyra_filter_update,
	.get_defaults = veyra_filter_defaults,
	.get_properties = veyra_filter_properties,
	.video_render = veyra_filter_render,
};

bool obs_module_load(void)
{
	obs_register_source(&veyra_filter_info);
	blog(LOG_INFO, "[veyra] filter registered (%s)", VEYRA_FILTER_ID);
	return true;
}
