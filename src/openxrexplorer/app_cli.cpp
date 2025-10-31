#include "app_cli.h"
#include "array.h"
#include "openxr_info.h"

#include <stdbool.h>
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

// File-scope minimum GPU log level for CLI (used by non-capturing log callback)
// skg_log_ enum order: info < warning < critical
static int g_cli_gpu_min_log_level = 1; // default: warn (prints warnings and critical)

// Helper to set environment variables cross-platform
static void set_env_var(const char* name, const char* value) {
#if defined(_WIN32)
	_putenv_s(name, value);
#else
	setenv(name, value, 1);
#endif
}

/*** Types *******************************/

struct command_t {
	const char *name_func;
	const char *name_type;
	bool        requires_session;
	void      (*show)();
};

/*** Signatures **************************/

void cli_print_table(const display_table_t *table);
void cli_show_help();
int32_t strcmp_nocase(char const *a, char const *b);

/*** Code ********************************/

static void print_supported_backends() {
	printf("Supported graphics backends in this build: ");
	bool first = true;
#if defined(XR_USE_GRAPHICS_API_D3D11)
	printf("%sD3D11", first?"":"; "); first=false;
#endif
#if defined(XR_USE_GRAPHICS_API_OPENGL)
	printf("%sOpenGL", first?"":"; "); first=false;
#endif
#if defined(XR_USE_GRAPHICS_API_D3D12)
	printf("%sD3D12", first?"":"; "); first=false;
#endif
	if (first) printf("(none)\n"); else printf("\n");
}

static bool backend_compiled(xr_graphics_preference_t pref) {
	switch (pref) {
	case xr_gfx_d3d11:
		#if defined(XR_USE_GRAPHICS_API_D3D11)
		return true;
		#else
		return false;
		#endif
	case xr_gfx_opengl:
		#if defined(XR_USE_GRAPHICS_API_OPENGL)
		return true;
		#else
		return false;
		#endif
	case xr_gfx_d3d12:
		#if defined(XR_USE_GRAPHICS_API_D3D12)
		return true;
		#else
		return false;
		#endif
	default:
		return true; // auto/headless always allowed for validation; runtime may reject headless
	}
}

void app_cli(int32_t arg_count, const char **args) {
	xr_settings_t settings = {};
	settings.allow_session = false;
	settings.form          = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	settings.graphics_preference = xr_gfx_auto;

	g_cli_gpu_min_log_level = 1; // warn by default

	// Loader logging controls
	const char* loader_level_cli = NULL; // if provided, sets XR_LOADER_DEBUG
	const char* loader_log_file  = NULL; // if provided, sets XR_LOADER_LOG_FILE

	// Pre-scan args for flags that affect initialization behavior
	for (size_t i = 1; i < (size_t)arg_count; i++) {
		const char *raw = args[i];
		if (!raw) continue;
		bool has_prefix = (raw[0] == '-' || raw[0] == '/');
		const char *curr = raw;
		while (*curr == '-' || *curr == '/') curr++;

		if (strcmp_nocase("session", curr) == 0 || strcmp_nocase("enableSession", curr) == 0) {
			settings.allow_session = true;
		} else if (has_prefix && (strncmp(curr, "gpuLogLevel=", 12) == 0)) {
			const char* level = curr + 12;
			if      (strcmp_nocase(level, "info" ) == 0) g_cli_gpu_min_log_level = 0; // print all
			else if (strcmp_nocase(level, "warn" ) == 0) g_cli_gpu_min_log_level = 1; // default
			else if (strcmp_nocase(level, "error") == 0) g_cli_gpu_min_log_level = 2; // critical only
		} else if (strcmp_nocase("gpuLogLevel", curr) == 0) {
			// Accept next arg as level
			if (i + 1 < (size_t)arg_count) {
				const char* level = args[++i];
				if (level && !(level[0] == '-' || level[0] == '/')) {
					if      (strcmp_nocase(level, "info" ) == 0) g_cli_gpu_min_log_level = 0;
					else if (strcmp_nocase(level, "warn" ) == 0) g_cli_gpu_min_log_level = 1;
					else if (strcmp_nocase(level, "error") == 0) g_cli_gpu_min_log_level = 2;
				}
			}
		} else if (has_prefix && (strncmp(curr, "xrGraphics=", 11) == 0)) {
			const char* val = curr + 11;
			if      (strcmp_nocase(val, "auto"    ) == 0) settings.graphics_preference = xr_gfx_auto;
			else if (strcmp_nocase(val, "headless") == 0) settings.graphics_preference = xr_gfx_headless;
			else if (strcmp_nocase(val, "d3d11"  ) == 0) settings.graphics_preference = xr_gfx_d3d11;
			else if (strcmp_nocase(val, "opengl" ) == 0) settings.graphics_preference = xr_gfx_opengl;
			else if (strcmp_nocase(val, "d3d12"  ) == 0) settings.graphics_preference = xr_gfx_d3d12;
		} else if (strcmp_nocase("xrGraphics", curr) == 0) {
			if (i + 1 < (size_t)arg_count) {
				const char* val = args[++i];
				if (val && !(val[0] == '-' || val[0] == '/')) {
					if      (strcmp_nocase(val, "auto"    ) == 0) settings.graphics_preference = xr_gfx_auto;
					else if (strcmp_nocase(val, "headless") == 0) settings.graphics_preference = xr_gfx_headless;
					else if (strcmp_nocase(val, "d3d11"  ) == 0) settings.graphics_preference = xr_gfx_d3d11;
					else if (strcmp_nocase(val, "opengl" ) == 0) settings.graphics_preference = xr_gfx_opengl;
					else if (strcmp_nocase(val, "d3d12"  ) == 0) settings.graphics_preference = xr_gfx_d3d12;
				}
			}
		} else if (has_prefix && (strncmp(curr, "loaderDebug=", 12) == 0)) {
			loader_level_cli = curr + 12; // value after '='
		} else if (strcmp_nocase("loaderDebug", curr) == 0) {
			// Accept next arg as level
			if (i + 1 < (size_t)arg_count) {
				loader_level_cli = args[++i];
				if (loader_level_cli && (loader_level_cli[0] == '-' || loader_level_cli[0] == '/')) loader_level_cli = NULL;
			}
		} else if (has_prefix && (strncmp(curr, "loaderLogFile=", 14) == 0)) {
			loader_log_file = curr + 14;
		} else if (strcmp_nocase("loaderLogFile", curr) == 0) {
			if (i + 1 < (size_t)arg_count) {
				loader_log_file = args[++i];
				if (loader_log_file && (loader_log_file[0] == '-' || loader_log_file[0] == '/')) loader_log_file = NULL;
			}
		}
	}

	// Default Loader logs to errors-only unless caller explicitly set via CLI or already in env
	if (loader_level_cli && *loader_level_cli) {
		set_env_var("XR_LOADER_DEBUG", loader_level_cli);
	} else if (!getenv("XR_LOADER_DEBUG")) {
		set_env_var("XR_LOADER_DEBUG", "error");
	}
	// Optional redirection of loader logs
	if (loader_log_file && *loader_log_file) {
		set_env_var("XR_LOADER_LOG_FILE", loader_log_file);
	}

	// GPU log filtering for CLI
	skg_callback_log([](skg_log_ level, const char *text) {
		if ((int)level >= g_cli_gpu_min_log_level) {
			printf("[%d] %s\n", level, text);
		}
	});

	if (!skg_init("OpenXR Explorer", nullptr))
		printf("Failed to init skg!\n");
	openxr_info_reload(settings);
	if (xr_instance_err) printf("XrInstance error: [%s]\n", xr_instance_err);
	if (xr_system_err)   printf("XrSystemId error: [%s]\n", xr_system_err);
	if (xr_session_err)  printf("XrSession error: [%s]\n", xr_session_err);

	// Validate xrGraphics selection against compiled backends
	if (!backend_compiled(settings.graphics_preference)) {
		printf("Warning: requested -xrGraphics backend not available in this build. ");
		print_supported_backends();
		printf("Falling back to auto.\n");
		settings.graphics_preference = xr_gfx_auto;
	}

	// Find all the commands we want to execute
	bool show = false;
	for (size_t i = 1; i < arg_count; i++) {
		const char *curr = args[i];
		while (*curr == '-') curr++;

		if (strcmp_nocase("help", curr) == 0 || strcmp_nocase("h", curr) == 0 || strcmp_nocase("/h", curr) == 0) {
			cli_show_help();
			show = true;
		} else {
			for (size_t c = 0; c < xr_tables.count; c++) {
				if ((xr_tables[c].name_func && strcmp_nocase(xr_tables[c].name_func, curr) == 0) ||
					(xr_tables[c].name_type && strcmp_nocase(xr_tables[c].name_type, curr) == 0)) {
					cli_print_table(&xr_tables[c]);
					show = true;
					break;
				}
			}
		}
	}
	if (!show)
		cli_show_help();

	openxr_info_release();
	skg_shutdown();
}

///////////////////////////////////////////

void cli_show_help() {
	printf(R"_(
Usage: openxr-explorer [option list...]

Notes:	This tool shows a list of values provided from the active OpenXR
	runtime. If a type is specified, the associated function will be
	called. If a function is specified, the associated data will be 
	shown. Options are case insensitive.

Options:
	-help	Show this help information!
	-session	Create an XrSession in CLI mode (needed for queries that require a Session)
	-enableSession	Alias for -session
	-xrGraphics <auto|headless|d3d11|opengl|d3d12> | -xrGraphics=<value>
		Select graphics preference for instance/session creation.
		Default: auto (prefer compiled backend; use headless if XR_MND_headless).
	-gpuLogLevel <level> | -gpuLogLevel=<level>
		Set GPU log verbosity for CLI (sk_gpu): info, warn (default), error
	-loaderDebug <level> | -loaderDebug=<level>
		Set OpenXR Loader log level (XR_LOADER_DEBUG): error (default), warn, info, verbose, trace
	-loaderLogFile <path> | -loaderLogFile=<path>
		Redirect OpenXR Loader logs to a file (XR_LOADER_LOG_FILE)

Notes:
	- Backend availability depends on this binary's build. )_");
	print_supported_backends();
	printf(R"_(
	- SteamVR typically does not expose XR_MND_headless; headless requests may fall back to the compiled backend.
	- Unrecognized values (e.g. "vulkan") are treated as auto.

)_");
	printf("\tFUNCTIONS\n");
	for (size_t i = 0; i < xr_tables.count; i++) {
		if (xr_tables[i].name_func)
			printf("\t-%s\n", xr_tables[i].name_func);
	}
	printf("\n\tTYPES\n");
	for (size_t i = 0; i < xr_tables.count; i++) {
		if (xr_tables[i].name_type)
			printf("\t-%s\n", xr_tables[i].name_type);
	}
}

///////////////////////////////////////////

void cli_print_table(const display_table_t *table) {
	printf("%s\n", table->show_type ? table->name_type : table->name_func);

	size_t max[3] = {};
	for (size_t i = table->header_row?1:0; i < table->cols[0].count; i++) {
		for (size_t c = 0; c < table->column_count; c++) {
			size_t len = table->cols[c][i].text ? strlen(table->cols[c][i].text) : 0;
			if (max[c] < len)
				max[c] = len;
		}
	}

	for (size_t i = table->header_row ? 1 : 0; i < table->cols[0].count; i++) {
		printf("| ");
		for (size_t c = 0; c < table->column_count; c++) {
			printf("%-*s", (int32_t)max[c], table->cols[c][i].text ? table->cols[c][i].text : "");
			if (c != table->column_count-1)
				printf(" | ");
		}
		printf(" |\n");
	}
}

///////////////////////////////////////////

int32_t strcmp_nocase(char const *a, char const *b) {
	for (;; a++, b++) {
		int d = tolower((unsigned char)*a) - tolower((unsigned char)*b);
		if (d != 0 || !*a)
			return d;
	}
	return -1;
}