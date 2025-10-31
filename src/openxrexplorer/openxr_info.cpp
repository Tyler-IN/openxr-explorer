#include "openxr_info.h"
#include "openxr_properties.h"
#if defined(__linux__) && defined(SKG_OPENGL)
#include <X11/Xlib.h>
#include <GL/glx.h>
#endif
#if defined(_WIN32) && defined(XR_USE_GRAPHICS_API_OPENGL)
#include <Windows.h>
#include <wingdi.h>
#pragma comment(lib, "opengl32.lib")
// Forward declarations to ensure visibility
static bool create_hidden_wgl_context();
static void destroy_hidden_wgl_context();
static HWND  g_cli_gl_hwnd = nullptr;
static HDC   g_cli_gl_hdc  = nullptr;
static HGLRC g_cli_gl_hrc  = nullptr;
#endif
#if defined(XR_USE_GRAPHICS_API_D3D11)
#include <d3d11.h>
#include <dxgi1_2.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
// Make the D3D11 helper visible before use
static ID3D11Device* g_cli_d3d11_device = nullptr;
static ID3D11Device* create_d3d11_device_for_luid(LUID luid, D3D_FEATURE_LEVEL min_level);
#endif
#if defined(XR_USE_GRAPHICS_API_D3D12)
#include <d3d12.h>
#include <dxgi1_2.h>
#pragma comment(lib, "d3d12.lib")
// D3D12 globals and forward decls
static ID3D12Device*       g_cli_d3d12_device = nullptr;
static ID3D12CommandQueue* g_cli_d3d12_queue  = nullptr;
static ID3D12Device* create_d3d12_device_for_luid(LUID luid);
#endif
#include <openxr/openxr_platform.h>
#include <openxr/openxr_reflection.h>

#include <stdio.h>
#include <stdarg.h>
#include <malloc.h>

/*** Types *******************************/

struct xr_extensions_t {
	array_t<XrExtensionProperties> extensions;
	array_t<XrApiLayerProperties>  layers;
};

/*** Global Variables ********************/

array_t<display_table_t> xr_tables        = {};
array_t<char *>          xr_table_strings = {};

array_t<xr_enum_info_t> xr_misc_enums = {};
xr_properties_t         xr_properties = {};
xr_view_info_t          xr_view       = {};
xr_extensions_t         xr_extensions = {};

XrInstance  xr_instance     = {};
const char *xr_instance_err = nullptr;
XrSession   xr_session      = {};
const char *xr_session_err  = nullptr;
XrSystemId  xr_system_id    = {};
const char *xr_system_err   = nullptr;

const char* xr_runtime_name = "No runtime set";

#define XR_NEXT_INSERT(obj, obj_next) obj_next.next = obj.next; obj.next = &obj_next;

/*** Signatures **************************/

void openxr_init_instance(array_t<XrExtensionProperties> extensions, xr_settings_t settings);
void openxr_init_system  (XrFormFactor form);
void openxr_init_session (xr_settings_t settings);

xr_extensions_t openxr_load_exts      ();
xr_properties_t openxr_load_properties();
xr_view_info_t  openxr_load_view      (XrViewConfigurationType view_config);
void            openxr_load_enums     (xr_settings_t settings);
const char *    openxr_result_string  (XrResult result);
void            openxr_register_enums ();
bool            openxr_has_ext        (const char *ext_name);


/*** Code ********************************/

void openxr_info_reload(xr_settings_t settings) {
	openxr_info_release();

	xr_extensions = openxr_load_exts();
	openxr_init_instance(xr_extensions.extensions, settings);
	openxr_init_system  (settings.form);
	xr_properties = openxr_load_properties();
	xr_view       = openxr_load_view      (settings.view_config);

	openxr_register_enums();
	openxr_load_enums    (settings);

	if (xr_session) {
		xrDestroySession(xr_session);
		xr_session = XR_NULL_HANDLE;
	}
}

///////////////////////////////////////////

void openxr_info_release() {
	xr_misc_enums.each([](xr_enum_info_t &i) { i.items.free(); });
	xr_misc_enums.free();
	xr_properties = {};
	xr_view.available_configs     .free();
	xr_view.available_config_names.free();
	xr_view.config_views          .free();
	xr_view = {};
	xr_extensions.extensions.free();
	xr_extensions.layers    .free();
	xr_extensions = {};
	xr_runtime_name = "No runtime set";

	xr_table_strings.each(free);
	xr_table_strings.free();
	xr_tables.each([](display_table_t &t) {for (int32_t i=0; i<t.column_count; i++) t.cols[i].free(); });
	xr_tables.free();

	if (xr_session)  xrDestroySession (xr_session);
	if (xr_instance) xrDestroyInstance(xr_instance);
#if defined(XR_USE_GRAPHICS_API_D3D11)
	if (g_cli_d3d11_device) { g_cli_d3d11_device->Release(); g_cli_d3d11_device = nullptr; }
#endif
#if defined(XR_USE_GRAPHICS_API_D3D12)
	if (g_cli_d3d12_queue) { g_cli_d3d12_queue->Release(); g_cli_d3d12_queue = nullptr; }
	if (g_cli_d3d12_device) { g_cli_d3d12_device->Release(); g_cli_d3d12_device = nullptr; }
#endif
#if defined(_WIN32) && defined(XR_USE_GRAPHICS_API_OPENGL)
	destroy_hidden_wgl_context();
#endif

	xr_session      = XR_NULL_HANDLE;
	xr_instance     = XR_NULL_HANDLE;
	xr_system_id    = XR_NULL_SYSTEM_ID;
	xr_session_err  = nullptr;
	xr_instance_err = nullptr;
	xr_system_err   = nullptr;
}

///////////////////////////////////////////

const char *openxr_result_string(XrResult result) {
	switch (result) {
#define ENTRY(NAME, VALUE) \
	case VALUE: return #NAME;
		XR_LIST_ENUM_XrResult(ENTRY)
#undef ENTRY
	default: return "<UNKNOWN>";
	}
}

///////////////////////////////////////////

const char* openxr_path_string(XrPath path) {
	uint32_t count  = 0;
	XrResult result = xrPathToString(xr_instance, path, 0, &count, nullptr);
	if (XR_FAILED(result)) return openxr_result_string(result);

	char* path_str = (char*)malloc(count + 1);
	result = xrPathToString(xr_instance, path, count, &count, path_str);
	if (XR_FAILED(result)) return openxr_result_string(result);

	return path_str;
}

///////////////////////////////////////////

const char *new_string(const char *format, ...) {
	va_list args;
	char   *result;

	va_start(args, format);
	size_t len = vsnprintf(0, 0, format, args);
	va_end(args);
	if ((result = (char*)malloc(len + 1)) != 0) {
		va_start(args, format);
		vsnprintf(result, len+1, format, args);
		va_end(args);
		xr_table_strings.add(result);
		return result;
	} else {
		return "";
	}
}

///////////////////////////////////////////

void openxr_init_instance(array_t<XrExtensionProperties> extensions, xr_settings_t settings) {
	if (xr_instance != XR_NULL_HANDLE || xr_instance_err != nullptr)
		return;

	array_t<const char *> exts = {};

	// Headless requested: enable XR_MND_headless if present
	if (settings.graphics_preference == xr_gfx_headless) {
		for (size_t i = 0; i < extensions.count; i++) {
			if (strcmp(extensions[i].extensionName, XR_MND_HEADLESS_EXTENSION_NAME) == 0) {
				exts.add(XR_MND_HEADLESS_EXTENSION_NAME);
				break;
			}
		}
	}

	// Include the requested graphics extension unless headless was explicitly requested
	if (settings.graphics_preference != xr_gfx_headless) {
#if defined(XR_USE_GRAPHICS_API_D3D11)
		bool want_d3d11 = (settings.graphics_preference == xr_gfx_d3d11) || (settings.graphics_preference == xr_gfx_auto);
		if (want_d3d11) {
			for (size_t i = 0; i < extensions.count; i++) {
				if (strcmp(extensions[i].extensionName, XR_KHR_D3D11_ENABLE_EXTENSION_NAME) == 0) {
					exts.add(XR_KHR_D3D11_ENABLE_EXTENSION_NAME);
					break;
				}
			}
		}
#endif
#if defined(XR_USE_GRAPHICS_API_OPENGL)
		bool want_gl = (settings.graphics_preference == xr_gfx_opengl) || (settings.graphics_preference == xr_gfx_auto);
		if (want_gl) {
			for (size_t i = 0; i < extensions.count; i++) {
				if (strcmp(extensions[i].extensionName, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME) == 0) {
					exts.add(XR_KHR_OPENGL_ENABLE_EXTENSION_NAME);
					break;
				}
			}
		}
#endif
#if defined(XR_USE_GRAPHICS_API_D3D12)
		bool want_d3d12 = (settings.graphics_preference == xr_gfx_d3d12) || (settings.graphics_preference == xr_gfx_auto);
		if (want_d3d12) {
			for (size_t i = 0; i < extensions.count; i++) {
				if (strcmp(extensions[i].extensionName, XR_KHR_D3D12_ENABLE_EXTENSION_NAME) == 0) {
					exts.add(XR_KHR_D3D12_ENABLE_EXTENSION_NAME);
					break;
				}
			}
		}
#endif
	}

	// Optionally include debug utils if available
	for (size_t i = 0; i < extensions.count; i++) {
		if (strcmp(extensions[i].extensionName, XR_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0) {
			exts.add(XR_EXT_DEBUG_UTILS_EXTENSION_NAME);
			break;
		}
	}

	XrInstanceCreateInfo create_info = { XR_TYPE_INSTANCE_CREATE_INFO };
	create_info.enabledExtensionCount = (uint32_t)exts.count;
	create_info.enabledExtensionNames = exts.data;
	create_info.enabledApiLayerCount  = 0;
	create_info.enabledApiLayerNames  = nullptr;
	create_info.applicationInfo.applicationVersion = 1;
	create_info.applicationInfo.engineVersion      = 1;
	create_info.applicationInfo.apiVersion         = XR_CURRENT_API_VERSION;
	snprintf(create_info.applicationInfo.applicationName, sizeof(create_info.applicationInfo.applicationName), "%s", "OpenXR Explorer");
	snprintf(create_info.applicationInfo.engineName,      sizeof(create_info.applicationInfo.engineName     ), "None");
	
	XrResult result = xrCreateInstance(&create_info, &xr_instance);
	if (result == XR_ERROR_API_VERSION_UNSUPPORTED) {
		create_info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
		result = xrCreateInstance(&create_info, &xr_instance);
	}
	if (XR_FAILED(result)) {
		xr_instance_err = openxr_result_string(result);
		xr_system_err   = "No XrInstance available";
		xr_session_err  = "No XrInstance available";
	}
}

///////////////////////////////////////////

void openxr_init_system(XrFormFactor form) {
	if (xr_instance_err != nullptr) {
		xr_system_err  = "No XrInstance available";
		xr_session_err = "No XrInstance available";
		return;
	}
	if (xr_system_id != XR_NULL_SYSTEM_ID || xr_system_err != nullptr) 
		return;

	XrSystemGetInfo system_info = { XR_TYPE_SYSTEM_GET_INFO };
	system_info.formFactor = form;
	XrResult result = xrGetSystem(xr_instance, &system_info, &xr_system_id);
	if (XR_FAILED(result)) {
		xr_system_err = openxr_result_string(result);
		xr_session_err = "No XrSystemId available";
	}
}

///////////////////////////////////////////

void openxr_init_session(xr_settings_t settings) {
	if (xr_instance_err != nullptr) { xr_session_err = "No XrInstance available"; return; }
	if (xr_system_err   != nullptr) { xr_session_err = "No XrSystemId available"; return; }
	if (xr_session != XR_NULL_HANDLE || xr_session_err != nullptr) return;

	skg_platform_data_t platform = skg_get_platform_data();

	void* binding_ptr = nullptr;
	bool try_headless = (settings.graphics_preference == xr_gfx_headless);
	bool has_headless = openxr_has_ext("XR_MND_headless");
	if (!(try_headless && has_headless)) {
#if defined(XR_USE_GRAPHICS_API_D3D11)
		if (settings.graphics_preference == xr_gfx_auto || settings.graphics_preference == xr_gfx_d3d11) {
			PFN_xrGetD3D11GraphicsRequirementsKHR ext_xrGetD3D11GraphicsRequirementsKHR;
			XrGraphicsRequirementsD3D11KHR        requirement = { XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR };
			xrGetInstanceProcAddr(xr_instance, "xrGetD3D11GraphicsRequirementsKHR", (PFN_xrVoidFunction *)(&ext_xrGetD3D11GraphicsRequirementsKHR));
			ext_xrGetD3D11GraphicsRequirementsKHR(xr_instance, xr_system_id, &requirement);

			if (g_cli_d3d11_device) { g_cli_d3d11_device->Release(); g_cli_d3d11_device = nullptr; }
			g_cli_d3d11_device = create_d3d11_device_for_luid(requirement.adapterLuid, requirement.minFeatureLevel);
			if (!g_cli_d3d11_device) { xr_session_err = "Failed to create D3D11 device for XR"; return; }

			XrGraphicsBindingD3D11KHR *binding = new XrGraphicsBindingD3D11KHR{ XR_TYPE_GRAPHICS_BINDING_D3D11_KHR };
			binding->device = g_cli_d3d11_device;
			binding_ptr = binding;
		}
#endif
#if defined(XR_USE_GRAPHICS_API_D3D12)
		if (!binding_ptr && (settings.graphics_preference == xr_gfx_auto || settings.graphics_preference == xr_gfx_d3d12)) {
			PFN_xrGetD3D12GraphicsRequirementsKHR extGet;
			XrGraphicsRequirementsD3D12KHR req{ XR_TYPE_GRAPHICS_REQUIREMENTS_D3D12_KHR };
			xrGetInstanceProcAddr(xr_instance, "xrGetD3D12GraphicsRequirementsKHR", (PFN_xrVoidFunction*)&extGet);
			extGet(xr_instance, xr_system_id, &req);
			if (g_cli_d3d12_device) { g_cli_d3d12_device->Release(); g_cli_d3d12_device = nullptr; }
			if (g_cli_d3d12_queue)  { g_cli_d3d12_queue->Release();  g_cli_d3d12_queue  = nullptr; }
			g_cli_d3d12_device = create_d3d12_device_for_luid(req.adapterLuid);
			if (!g_cli_d3d12_device) { xr_session_err = "Failed to create D3D12 device for XR"; return; }
			D3D12_COMMAND_QUEUE_DESC qd{ D3D12_COMMAND_LIST_TYPE_DIRECT, (INT)D3D12_COMMAND_QUEUE_PRIORITY_NORMAL, D3D12_COMMAND_QUEUE_FLAG_NONE, 0 };
			HRESULT hr = g_cli_d3d12_device->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&g_cli_d3d12_queue);
			if (FAILED(hr) || !g_cli_d3d12_queue) { xr_session_err = "Failed to create D3D12 command queue"; return; }
			XrGraphicsBindingD3D12KHR* bind = new XrGraphicsBindingD3D12KHR{ XR_TYPE_GRAPHICS_BINDING_D3D12_KHR };
			bind->device = g_cli_d3d12_device; bind->queue = g_cli_d3d12_queue;
			binding_ptr = bind;
		}
#endif
#if defined(SKG_OPENGL) && defined(_WIN32)
		if (!binding_ptr && (settings.graphics_preference == xr_gfx_auto || settings.graphics_preference == xr_gfx_opengl)) {
			// Satisfy OpenGL graphics requirements per XR_KHR_opengl_enable
			PFN_xrGetOpenGLGraphicsRequirementsKHR ext_xrGetOpenGLGraphicsRequirementsKHR = nullptr;
			XrGraphicsRequirementsOpenGLKHR        requirement = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
			xrGetInstanceProcAddr(xr_instance, "xrGetOpenGLGraphicsRequirementsKHR", (PFN_xrVoidFunction *)(&ext_xrGetOpenGLGraphicsRequirementsKHR));
			if (ext_xrGetOpenGLGraphicsRequirementsKHR) {
				ext_xrGetOpenGLGraphicsRequirementsKHR(xr_instance, xr_system_id, &requirement);
			}

			XrGraphicsBindingOpenGLWin32KHR *binding = new XrGraphicsBindingOpenGLWin32KHR{ XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
			binding->hDC   = (HDC  )platform._gl_hdc;
			binding->hGLRC = (HGLRC)platform._gl_hrc;
			// If skg didn't provide an OpenGL context, create a hidden one
			if (!binding->hGLRC || !binding->hDC) {
#if defined(_WIN32) && defined(XR_USE_GRAPHICS_API_OPENGL)
				if (create_hidden_wgl_context()) {
					binding->hDC   = g_cli_gl_hdc;
					binding->hGLRC = g_cli_gl_hrc;
				}
#endif
			}
			binding_ptr = binding;
		}
#endif
#if !defined(SKG_OPENGL) && defined(_WIN32) && defined(XR_USE_GRAPHICS_API_OPENGL)
		if (!binding_ptr && (settings.graphics_preference == xr_gfx_auto || settings.graphics_preference == xr_gfx_opengl)) {
			// Satisfy OpenGL graphics requirements per XR_KHR_opengl_enable
			PFN_xrGetOpenGLGraphicsRequirementsKHR ext_xrGetOpenGLGraphicsRequirementsKHR = nullptr;
			XrGraphicsRequirementsOpenGLKHR        requirement = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
			xrGetInstanceProcAddr(xr_instance, "xrGetOpenGLGraphicsRequirementsKHR", (PFN_xrVoidFunction *)(&ext_xrGetOpenGLGraphicsRequirementsKHR));
			if (ext_xrGetOpenGLGraphicsRequirementsKHR) {
				ext_xrGetOpenGLGraphicsRequirementsKHR(xr_instance, xr_system_id, &requirement);
			}

			XrGraphicsBindingOpenGLWin32KHR *binding = new XrGraphicsBindingOpenGLWin32KHR{ XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
			if (create_hidden_wgl_context()) {
				binding->hDC   = g_cli_gl_hdc;
				binding->hGLRC = g_cli_gl_hrc;
				binding_ptr = binding;
			} else {
				delete binding;
			}
		}
#endif
		if (!binding_ptr && !has_headless) { xr_session_err = "Requested graphics backend not available in this build"; return; }
	}

	XrSessionCreateInfo session_info = { XR_TYPE_SESSION_CREATE_INFO };
	session_info.next     = binding_ptr;
	session_info.systemId = xr_system_id;
	if (try_headless && has_headless) session_info.next = nullptr;

	XrResult result = xrCreateSession(xr_instance, &session_info, &xr_session);
	if (XR_FAILED(result)) { xr_session_err = openxr_result_string(result); }

	if (binding_ptr) {
#if defined(XR_USE_GRAPHICS_API_D3D11)
		if (((XrBaseInStructure*)binding_ptr)->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) delete (XrGraphicsBindingD3D11KHR*)binding_ptr;
#endif
#if defined(XR_USE_GRAPHICS_API_D3D12)
		if (((XrBaseInStructure*)binding_ptr)->type == XR_TYPE_GRAPHICS_BINDING_D3D12_KHR) delete (XrGraphicsBindingD3D12KHR*)binding_ptr;
#endif
#if defined(SKG_OPENGL) && defined(_WIN32)
		if (((XrBaseInStructure*)binding_ptr)->type == XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR) delete (XrGraphicsBindingOpenGLWin32KHR*)binding_ptr;
#endif
#if defined(SKG_OPENGL) && defined(__linux__)
		if (((XrBaseInStructure*)binding_ptr)->type == XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR) delete (XrGraphicsBindingOpenGLXlibKHR*)binding_ptr;
#endif
	}
	// NOTE: D3D12 device/queue are released in openxr_info_release()
}

///////////////////////////////////////////

xr_extensions_t openxr_load_exts() {
	xr_extensions_t result = {};

	// Load layers.
	//
	// Layers are not sorted because the order is important; for example, layers that
	// use an extension should be before layers that provide the extension.
	uint32_t count = 0;
	if (XR_FAILED(xrEnumerateApiLayerProperties(0, &count, nullptr)))
		return result;
	result.layers = array_t<XrApiLayerProperties>::make_fill(count, {XR_TYPE_API_LAYER_PROPERTIES});
	xrEnumerateApiLayerProperties(count, &count, result.layers.data);

	display_table_t table = {};
	table.name_func = "xrEnumerateApiLayerProperties";
	table.name_type = "XrApiLayerProperties";
	table.spec      = "api-layers";
	table.tag       = display_tag_features;
	table.column_count = 3;
	table.header_row   = true;
	if (result.layers.count > 0) {
		table.cols[0].add({ "Layer Name" });
		table.cols[1].add({ "Description" });
		table.cols[2].add({ "Version", "Version" });
		for (size_t i = 0; i < result.layers.count; i++) {
			table.cols[0].add({result.layers[i].layerName});
			table.cols[1].add({result.layers[i].description});
			table.cols[2].add({new_string("v%u",result.layers[i].layerVersion)});
		}
	} else {
		table.error = "No layers present";
	}
	xr_tables.add(table);

	// Load and sort extensions
	count = 0;
	if (XR_FAILED(xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr)))
		return result;
	result.extensions = array_t<XrExtensionProperties>::make_fill(count, {XR_TYPE_EXTENSION_PROPERTIES});
	xrEnumerateInstanceExtensionProperties(nullptr, count, &count, result.extensions.data);
	result.extensions.sort([](const XrExtensionProperties &a, const XrExtensionProperties &b) {
		return strcmp(a.extensionName, b.extensionName);
	});

	table = {};
	table.name_func = "xrEnumerateInstanceExtensionProperties";
	table.name_type = "XrExtensionProperties";
	table.spec      = "extensions";
	table.tag       = display_tag_features;
	table.column_count = 3;
	table.header_row   = true;
	table.cols[0].add({ "Extension Name" });
	table.cols[1].add({ "Version", "Version" });
	table.cols[2].add({ "Spec", "Spec" });
	for (size_t i = 0; i < result.extensions.count; i++) {
		table.cols[0].add({result.extensions[i].extensionName});
		table.cols[1].add({new_string("v%u",result.extensions[i].extensionVersion)});
		table.cols[2].add({nullptr, result.extensions[i].extensionName});
	}
	xr_tables.add(table);

	return result;
}

///////////////////////////////////////////

bool openxr_has_ext(const char *ext_name){
	for (int32_t i = 0; i < xr_extensions.extensions.count ; i++) {
		if (strcmp(ext_name, xr_extensions.extensions[i].extensionName) == 0)
		return true;
	}
	return false;
}

///////////////////////////////////////////

xr_properties_t openxr_load_properties() {
	xr_properties_t result = {};

	//// Instance properties ////

	display_table_t table = {};
	table.name_func = "xrGetInstanceProperties";
	table.name_type = "XrInstanceProperties";
	table.spec      = "XrInstanceProperties";
	table.tag       = display_tag_properties;
	table.column_count = 2;

	if (!xr_instance_err) {
		result.instance = { XR_TYPE_INSTANCE_PROPERTIES };
		XrResult error = xrGetInstanceProperties(xr_instance, &result.instance);
		if (XR_FAILED(error)) {
			table.error = openxr_result_string(error);
		} else {
			xr_runtime_name = new_string("%s", result.instance.runtimeName);
			table.cols[0].add({ "runtimeName"    }); table.cols[1].add({ new_string("%s", result.instance.runtimeName) });
			table.cols[0].add({ "runtimeVersion" }); table.cols[1].add({ new_string("%d.%d.%d",
				(int32_t)XR_VERSION_MAJOR(result.instance.runtimeVersion),
				(int32_t)XR_VERSION_MINOR(result.instance.runtimeVersion),
				(int32_t)XR_VERSION_PATCH(result.instance.runtimeVersion)) });
		}
	} else {
		table.error = "No XrInstance available";
	}
	xr_tables.add(table);

	//// System properties ////
	
	openxr_load_system_properties(xr_instance, xr_system_id);

	return result;
}

///////////////////////////////////////////

xr_view_info_t openxr_load_view(XrViewConfigurationType view_config) {
	xr_view_info_t result = {};

	if (!xr_instance_err && ! xr_system_err) {
		// Get the list of available configurations
		uint32_t count = 0;
		xrEnumerateViewConfigurations(xr_instance, xr_system_id, 0, &count, nullptr);
		result.available_configs = array_t<XrViewConfigurationType>::make_fill(count, (XrViewConfigurationType)0);
		xrEnumerateViewConfigurations(xr_instance, xr_system_id, count, &count, result.available_configs.data);
		result.available_config_names.resize(count);
		for (size_t i = 0; i < count; i++) {
			switch (result.available_configs[i]) {
#define CASE_GET_NAME(e, val) case e: result.available_config_names.add( #e ); break;
				XR_LIST_ENUM_XrViewConfigurationType(CASE_GET_NAME)
#undef CASE_GET_NAME
			}
		}
	}

	// If the caller didn't select a view config, then we'll use the default
	result.current_config = view_config == 0 && result.available_configs.count > 0
		? result.available_configs[0]
		: view_config;

	display_table_t table = {};
	table.name_func = "xrGetViewConfigurationProperties";
	table.name_type = "XrViewConfigurationProperties";
	table.spec      = "XrViewConfigurationProperties";
	table.tag       = display_tag_view;
	table.column_count = 2;

	if (!xr_instance_err && !xr_system_err) {
		result.config_properties = { XR_TYPE_VIEW_CONFIGURATION_PROPERTIES };
		XrResult error = xrGetViewConfigurationProperties(xr_instance, xr_system_id, result.current_config, &result.config_properties);
		if (XR_FAILED(error)) {
			table.error = openxr_result_string(error);
		} else {
			table.cols[0].add({"fovMutable"}); table.cols[1].add({new_string("%s", result.config_properties.fovMutable ? "True" : "False")});
		}
	} else {
		if (xr_system_err)   table.error = "No XrSystemId available";
		if (xr_instance_err) table.error = "No XrInstance available";
	}
	xr_tables.add(table);

	// Load view configuration

	table = {};
	table.name_func = "xrEnumerateViewConfigurationViews";
	table.name_type = "XrViewConfigurationView";
	table.spec      = "XrViewConfigurationView";
	table.tag       = display_tag_view;
	table.column_count = 2;

	if (!xr_instance_err && !xr_system_err) {
		uint32_t count = 0;
		XrResult error = xrEnumerateViewConfigurationViews(xr_instance, xr_system_id, result.current_config, 0, &count, nullptr);
		result.config_views = array_t<XrViewConfigurationView>::make_fill(count, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
		xrEnumerateViewConfigurationViews(xr_instance, xr_system_id, result.current_config, count, &count, result.config_views.data);

		if (XR_FAILED(error)) {
			table.error = openxr_result_string(error);
		} else {
			for (uint32_t i = 0; i < result.config_views.count; i++) {
				table.cols[0].add({new_string("View %u", i)         }); table.cols[1].add({""});
				table.cols[0].add({"recommendedImageRectWidth"      }); table.cols[1].add({new_string("%u", result.config_views[i].recommendedImageRectWidth)});
				table.cols[0].add({"recommendedImageRectHeight"     }); table.cols[1].add({new_string("%u", result.config_views[i].recommendedImageRectHeight)});
				table.cols[0].add({"recommendedSwapchainSampleCount"}); table.cols[1].add({new_string("%u", result.config_views[i].recommendedSwapchainSampleCount)});
				table.cols[0].add({"maxImageRectWidth"              }); table.cols[1].add({new_string("%u", result.config_views[i].maxImageRectWidth)});
				table.cols[0].add({"maxImageRectHeight"             }); table.cols[1].add({new_string("%u", result.config_views[i].maxImageRectHeight)});
				table.cols[0].add({"maxSwapchainSampleCount"        }); table.cols[1].add({new_string("%u", result.config_views[i].maxSwapchainSampleCount)});
			}
		}
	} else {
		if (xr_system_err)   table.error = "No XrSystemId available";
		if (xr_instance_err) table.error = "No XrInstance available";
	}
	xr_tables.add(table);
	return result;
}

///////////////////////////////////////////

void openxr_load_enums(xr_settings_t settings) {
	// Check if any of the enums need a session
	if (!xr_session_err && settings.allow_session) {
		bool requires_session = false;
		for (size_t i = 0; i < xr_misc_enums.count; i++) {
			requires_session = requires_session || xr_misc_enums[i].requires_session;
		}
		if (requires_session) {
			openxr_init_session(settings);
		}
	} else if (!xr_session_err) {
		xr_session_err = "Reload with Session enabled";
	}

	for (size_t i = 0; i < xr_misc_enums.count; i++) {
		xr_misc_enums[i].items.clear();

		display_table_t table = {};
		table.name_func = xr_misc_enums[i].source_fn_name;
		table.name_type = xr_misc_enums[i].source_type_name;
		table.spec      = xr_misc_enums[i].spec_link;
		table.tag       = xr_misc_enums[i].tag;
		table.column_count = 1;

		if ((!xr_misc_enums[i].requires_session  || !xr_session_err ) &&
			(!xr_misc_enums[i].requires_instance || !xr_instance_err) &&
			(!xr_misc_enums[i].requires_system   || !xr_system_err  )) {

			XrResult error = xr_misc_enums[i].load_info(&xr_misc_enums[i], settings);

			for (size_t e = 0; e < xr_misc_enums[i].items.count; e++) {
				table.cols[0].add({ xr_misc_enums[i].items[e] });
			}
			if (XR_FAILED(error)) {
				table.error = openxr_result_string(error);
			}
		} else {

			if      (xr_misc_enums[i].requires_instance && xr_instance_err) table.error = "No XrInstance available";
			else if (xr_misc_enums[i].requires_system   && xr_system_err  ) table.error = "No XrSystemId available";
			else if (xr_misc_enums[i].requires_session  && xr_session_err ) table.error = "No XrSession available";
		}

		xr_tables.add(table);
	}
}

///////////////////////////////////////////

void openxr_register_enums() {
	xr_misc_enums.clear();

	xr_enum_info_t info = { "xrEnumerateReferenceSpaces" };
	info.source_type_name = "XrReferenceSpaceType";
	info.spec_link        = "reference-spaces";
	info.requires_session = true;
	info.tag              = display_tag_misc;
	info.load_info        = [](xr_enum_info_t *ref_info, xr_settings_t settings) {
		uint32_t count = 0;
		XrResult error = xrEnumerateReferenceSpaces(xr_session, 0, &count, nullptr);
		array_t<XrReferenceSpaceType> items(count, (XrReferenceSpaceType)0);
		xrEnumerateReferenceSpaces(xr_session, count, &count, items.data);
		for (size_t i = 0; i < items.count; i++) {
			switch (items[i]) {
#define CASE_GET_NAME(e, val) case e: ref_info->items.add( #e ); break;
				XR_LIST_ENUM_XrReferenceSpaceType(CASE_GET_NAME)
#undef CASE_GET_NAME
			}
		}
		items.free();
		return error;
	};
	xr_misc_enums.add(info);

	
	info = { "xrEnumerateEnvironmentBlendModes" };
	info.source_type_name = "XrEnvironmentBlendMode";
	info.spec_link        = "XrEnvironmentBlendMode";
	info.requires_instance= true;
	info.requires_system  = true;
	info.tag              = display_tag_view;
	info.load_info        = [](xr_enum_info_t *ref_info, xr_settings_t settings) {
		if (settings.view_config == 0) {
			if (xr_view.available_configs.count > 0) {
				settings.view_config = xr_view.available_configs[0];
			}
		}

		uint32_t count = 0;
		XrResult error = xrEnumerateEnvironmentBlendModes(xr_instance, xr_system_id, settings.view_config, 0, &count, nullptr);
		array_t<XrEnvironmentBlendMode> items(count, (XrEnvironmentBlendMode)0);
		xrEnumerateEnvironmentBlendModes(xr_instance, xr_system_id, settings.view_config, count, &count, items.data);

		for (size_t i = 0; i < count; i++) {
			switch (items[i]) {
#define CASE_GET_NAME(e, val) case e: ref_info->items.add({ #e }); break;
				XR_LIST_ENUM_XrEnvironmentBlendMode(CASE_GET_NAME)
#undef CASE_GET_NAME
			}
		}
		items.free();
		return error;
	};
	xr_misc_enums.add(info);

	info = { "xrEnumerateSwapchainFormats" };
	info.source_type_name = "skg_tex_fmt_";
	info.spec_link        = "xrEnumerateSwapchainFormats";
	info.requires_session = true;
	info.tag              = display_tag_misc;
	info.load_info        = [](xr_enum_info_t *ref_info, xr_settings_t settings) {
		uint32_t count = 0;
		XrResult error = xrEnumerateSwapchainFormats(xr_session, 0, &count, nullptr);
		array_t<int64_t> formats(count, 0);
		xrEnumerateSwapchainFormats(xr_session, count, &count, formats.data);

		for (size_t i = 0; i < formats.count; i++) {
			skg_tex_fmt_ format = skg_tex_fmt_from_native(formats[i]);
			switch (format) {
			case skg_tex_fmt_none:          ref_info->items.add(new_string("Unknown 0x%x #%d", formats[i], formats[i])); break;
			case skg_tex_fmt_rgba32:        ref_info->items.add("rgba32");          break;
			case skg_tex_fmt_rgba32_linear: ref_info->items.add("rgba32 linear");   break;
			case skg_tex_fmt_bgra32:        ref_info->items.add("bgra32");          break;
			case skg_tex_fmt_bgra32_linear: ref_info->items.add("bgra32 linear");   break;
			case skg_tex_fmt_rg11b10:       ref_info->items.add("rg11 b10");        break;
			case skg_tex_fmt_rgb10a2:       ref_info->items.add("rgb10 a2");        break;
			case skg_tex_fmt_rgba64u:       ref_info->items.add("rgba64u");         break;
			case skg_tex_fmt_rgba64s:       ref_info->items.add("rgba64s");         break;
			case skg_tex_fmt_rgba64f:       ref_info->items.add("rgba64f");         break;
			case skg_tex_fmt_rgba128:       ref_info->items.add("rgba128");         break;
			case skg_tex_fmt_r8:            ref_info->items.add("r8");              break;
			case skg_tex_fmt_r16:           ref_info->items.add("r16");             break;
			case skg_tex_fmt_r32:           ref_info->items.add("r32");             break;
			case skg_tex_fmt_depthstencil:  ref_info->items.add("depth24 stencil8");break;
			case skg_tex_fmt_depth32:       ref_info->items.add("depth32");         break;
			case skg_tex_fmt_depth16:       ref_info->items.add("depth16");         break;
			}
		}
		formats.free();
		return error;
	};
	xr_misc_enums.add(info);

	info = { "xrEnumerateColorSpacesFB" };
	info.source_type_name = "XrColorSpaceFB";
	info.spec_link        = "XrColorSpaceFB";
	info.requires_session = true;
	info.requires_instance= true;
	info.tag              = display_tag_misc;
	info.load_info        = [](xr_enum_info_t *ref_info, xr_settings_t settings) {
		PFN_xrEnumerateColorSpacesFB xrEnumerateColorSpacesFB;
		XrResult error = xrGetInstanceProcAddr(xr_instance, "xrEnumerateColorSpacesFB", (PFN_xrVoidFunction *)(&xrEnumerateColorSpacesFB));
		if (XR_FAILED(error)) return error;

		uint32_t count = 0;
		error = xrEnumerateColorSpacesFB(xr_session, 0, &count, nullptr);
		array_t<XrColorSpaceFB> color_spaces(count, (XrColorSpaceFB)0);
		xrEnumerateColorSpacesFB(xr_session, count, &count, color_spaces.data);

		for (size_t i = 0; i < color_spaces.count; i++) {
			switch (color_spaces[i]) {
#define CASE_GET_NAME(e, val) case e: ref_info->items.add({ #e }); break;
				XR_LIST_ENUM_XrColorSpaceFB(CASE_GET_NAME)
#undef CASE_GET_NAME
			}
		}
		color_spaces.free();
		return error;
	};
	xr_misc_enums.add(info);

	info = { "xrEnumerateDisplayRefreshRatesFB" };
	info.source_type_name = "float";
	info.spec_link        = "xrEnumerateDisplayRefreshRatesFB";
	info.requires_session = true;
	info.requires_instance= true;
	info.tag              = display_tag_misc;
	info.load_info        = [](xr_enum_info_t *ref_info, xr_settings_t settings) {
		PFN_xrEnumerateDisplayRefreshRatesFB xrEnumerateDisplayRefreshRatesFB;
		XrResult error = xrGetInstanceProcAddr(xr_instance, "xrEnumerateDisplayRefreshRatesFB", (PFN_xrVoidFunction *)(&xrEnumerateDisplayRefreshRatesFB));
		if (XR_FAILED(error)) return error;

		uint32_t count = 0;
		error = xrEnumerateDisplayRefreshRatesFB(xr_session, 0, &count, nullptr);
		array_t<float> refresh_rates(count, 0);
		xrEnumerateDisplayRefreshRatesFB(xr_session, count, &count, refresh_rates.data);

		for (size_t i = 0; i < refresh_rates.count; i++) {
			ref_info->items.add({ new_string("%f", refresh_rates[i])});
		}
		refresh_rates.free();
		return error;
	};
	xr_misc_enums.add(info);

	info = { "xrEnumerateRenderModelPathsFB" };
	info.source_type_name = "XrRenderModelPathInfoFB";
	info.spec_link        = "XrRenderModelPathInfoFB";
	info.requires_session = true;
	info.requires_instance= true;
	info.tag              = display_tag_misc;
	info.load_info        = [](xr_enum_info_t *ref_info, xr_settings_t settings) {
		PFN_xrEnumerateRenderModelPathsFB xrEnumerateRenderModelPathsFB;
		XrResult error = xrGetInstanceProcAddr(xr_instance, "xrEnumerateRenderModelPathsFB", (PFN_xrVoidFunction *)(&xrEnumerateRenderModelPathsFB));
		if (XR_FAILED(error)) return error;

		uint32_t count = 0;
		error = xrEnumerateRenderModelPathsFB(xr_session, 0, &count, nullptr);
		array_t<XrRenderModelPathInfoFB> model_paths(count, XrRenderModelPathInfoFB{ XR_TYPE_RENDER_MODEL_PATH_INFO_FB });
		xrEnumerateRenderModelPathsFB(xr_session, count, &count, model_paths.data);

		for (size_t i = 0; i < model_paths.count; i++) {
			ref_info->items.add({ openxr_path_string(model_paths[i].path) });
		}
		model_paths.free();
		return error;
	};
	xr_misc_enums.add(info);

	info = { "xrEnumerateViveTrackerPathsHTCX" };
	info.source_type_name = "XrViveTrackerPathsHTCX";
	info.spec_link        = "XrViveTrackerPathsHTCX";
	info.requires_instance= true;
	info.tag              = display_tag_misc;
	info.load_info        = [](xr_enum_info_t *ref_info, xr_settings_t settings) {
		PFN_xrEnumerateViveTrackerPathsHTCX xrEnumerateViveTrackerPathsHTCX;
		XrResult error = xrGetInstanceProcAddr(xr_instance, "xrEnumerateViveTrackerPathsHTCX", (PFN_xrVoidFunction *)(&xrEnumerateViveTrackerPathsHTCX));
		if (XR_FAILED(error)) return error;

		uint32_t count = 0;
		error = xrEnumerateViveTrackerPathsHTCX(xr_instance, 0, &count, nullptr);
		array_t<XrViveTrackerPathsHTCX> tracker_paths(count, XrViveTrackerPathsHTCX{ XR_TYPE_VIVE_TRACKER_PATHS_HTCX });
		xrEnumerateViveTrackerPathsHTCX(xr_instance, count, &count, tracker_paths.data);

		// TODO: This needs labels for persistentPath and rolePath, but the current
		// structure doens't exactly allow for this.
		for (size_t i = 0; i < tracker_paths.count; i++) {
			ref_info->items.add({ openxr_path_string(tracker_paths[i].persistentPath) });
			ref_info->items.add({ openxr_path_string(tracker_paths[i].rolePath) });
		}
		tracker_paths.free();
		return error;
	};
	xr_misc_enums.add(info);

	info = { "xrEnumeratePerformanceMetricsCounterPathsMETA" };
	info.source_type_name = "XrPath";
	info.spec_link        = "xrEnumeratePerformanceMetricsCounterPathsMETA";
	info.requires_instance= true;
	info.tag              = display_tag_misc;
	info.load_info        = [](xr_enum_info_t *ref_info, xr_settings_t settings) {
		PFN_xrEnumeratePerformanceMetricsCounterPathsMETA xrEnumeratePerformanceMetricsCounterPathsMETA;
		XrResult error = xrGetInstanceProcAddr(xr_instance, "xrEnumeratePerformanceMetricsCounterPathsMETA", (PFN_xrVoidFunction *)(&xrEnumeratePerformanceMetricsCounterPathsMETA));
		if (XR_FAILED(error)) return error;

		uint32_t count = 0;
		error = xrEnumeratePerformanceMetricsCounterPathsMETA(xr_instance, 0, &count, nullptr);
		array_t<XrPath> metric_paths(count, {});
		xrEnumeratePerformanceMetricsCounterPathsMETA(xr_instance, count, &count, metric_paths.data);

		for (size_t i = 0; i < metric_paths.count; i++) {
			ref_info->items.add({ openxr_path_string(metric_paths[i]) });
		}
		metric_paths.free();
		return error;
	};
	xr_misc_enums.add(info);

	info = { "xrEnumerateReprojectionModesMSFT" };
	info.source_type_name = "XrReprojectionModeMSFT";
	info.spec_link        = "XrReprojectionModeMSFT";
	info.requires_system  = true;
	info.requires_instance= true;
	info.tag              = display_tag_misc;
	info.load_info        = [](xr_enum_info_t *ref_info, xr_settings_t settings) {
		PFN_xrEnumerateReprojectionModesMSFT xrEnumerateReprojectionModesMSFT;
		XrResult error = xrGetInstanceProcAddr(xr_instance, "xrEnumerateReprojectionModesMSFT", (PFN_xrVoidFunction *)(&xrEnumerateReprojectionModesMSFT));
		if (XR_FAILED(error)) return error;

		uint32_t count = 0;
		error = xrEnumerateReprojectionModesMSFT(xr_instance, xr_system_id, xr_view.current_config, 0, &count, nullptr);
		array_t<XrReprojectionModeMSFT> reprojection_modes(count, (XrReprojectionModeMSFT)0);
		xrEnumerateReprojectionModesMSFT(xr_instance, xr_system_id, xr_view.current_config, count, &count, reprojection_modes.data);

		for (size_t i = 0; i < reprojection_modes.count; i++) {
			switch (reprojection_modes[i]) {
#define CASE_GET_NAME(e, val) case e: ref_info->items.add({ #e }); break;
				XR_LIST_ENUM_XrReprojectionModeMSFT(CASE_GET_NAME)
#undef CASE_GET_NAME
			}
		}
		reprojection_modes.free();
		return error;
	};
	xr_misc_enums.add(info);

	info = { "xrEnumerateSceneComputeFeaturesMSFT" };
	info.source_type_name = "XrSceneComputeFeatureMSFT";
	info.spec_link        = "XrSceneComputeFeatureMSFT";
	info.requires_system  = true;
	info.requires_instance= true;
	info.tag              = display_tag_misc;
	info.load_info        = [](xr_enum_info_t *ref_info, xr_settings_t settings) {
		PFN_xrEnumerateSceneComputeFeaturesMSFT xrEnumerateSceneComputeFeaturesMSFT;
		XrResult error = xrGetInstanceProcAddr(xr_instance, "xrEnumerateSceneComputeFeaturesMSFT", (PFN_xrVoidFunction *)(&xrEnumerateSceneComputeFeaturesMSFT));
		if (XR_FAILED(error)) return error;

		uint32_t count = 0;
		error = xrEnumerateSceneComputeFeaturesMSFT(xr_instance, xr_system_id, 0, &count, nullptr);
		array_t<XrSceneComputeFeatureMSFT> compute_features(count, (XrSceneComputeFeatureMSFT)0);
		xrEnumerateSceneComputeFeaturesMSFT(xr_instance, xr_system_id, count, &count, compute_features.data);

		for (size_t i = 0; i < compute_features.count; i++) {
			switch (compute_features[i]) {
#define CASE_GET_NAME(e, val) case e: ref_info->items.add({ #e }); break;
				XR_LIST_ENUM_XrSceneComputeFeatureMSFT(CASE_GET_NAME)
#undef CASE_GET_NAME
			}
		}
		compute_features.free();
		return error;
	};
	xr_misc_enums.add(info);
}

// Hold a created D3D11 device for CLI binding lifetime
#if defined(XR_USE_GRAPHICS_API_D3D11)
static ID3D11Device* create_d3d11_device_for_luid(LUID luid, D3D_FEATURE_LEVEL min_level) {
	IDXGIFactory1* factory = nullptr;
	if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory))) return nullptr;
	IDXGIAdapter1* match = nullptr;
	for (UINT i = 0; ; ++i) {
		IDXGIAdapter1* a = nullptr;
		if (factory->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
		DXGI_ADAPTER_DESC1 desc;
		if (SUCCEEDED(a->GetDesc1(&desc))) {
			if (desc.AdapterLuid.HighPart == luid.HighPart && desc.AdapterLuid.LowPart == luid.LowPart) { match = a; break; }
		}
		a->Release();
	}
	factory->Release();

	D3D_FEATURE_LEVEL requested[] = {
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0,
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
	};
	UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	ID3D11Device* dev = nullptr;
	ID3D11DeviceContext* ctx = nullptr;
	if (FAILED(D3D11CreateDevice(
		match, D3D_DRIVER_TYPE_UNKNOWN,
		nullptr, flags,
		requested, (UINT)(sizeof(requested)/sizeof(requested[0])),
		D3D11_SDK_VERSION, &dev, nullptr, &ctx))) {
		if (match) match->Release();
		return nullptr;
	}
	if (ctx) ctx->Release();
	if (match) match->Release();
	return dev;
}
#endif

#if defined(XR_USE_GRAPHICS_API_D3D12)
static ID3D12Device* create_d3d12_device_for_luid(LUID luid) {
	IDXGIFactory1* factory = nullptr; if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory))) return nullptr;
	IDXGIAdapter1* match = nullptr;
	for (UINT i = 0; ; ++i) {
		IDXGIAdapter1* a = nullptr; if (factory->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
		DXGI_ADAPTER_DESC1 desc; if (SUCCEEDED(a->GetDesc1(&desc))) {
			if (desc.AdapterLuid.HighPart == luid.HighPart && desc.AdapterLuid.LowPart == luid.LowPart) { match = a; break; }
		}
		a->Release();
	}
	factory->Release();
	if (!match) return nullptr;
	ID3D12Device* dev = nullptr;
	HRESULT hr = D3D12CreateDevice(match, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&dev);
	match->Release();
	return SUCCEEDED(hr) ? dev : nullptr;
}
#endif

#if defined(_WIN32) && defined(XR_USE_GRAPHICS_API_OPENGL)
static bool create_hidden_wgl_context() {
	if (g_cli_gl_hrc && g_cli_gl_hdc && g_cli_gl_hwnd) return true;
	HINSTANCE hinst = GetModuleHandleA(nullptr);
	const char* cls = "OpenXRExplorerHiddenGL";
	WNDCLASSA wc = {};
	wc.style         = CS_OWNDC;
	wc.lpfnWndProc   = DefWindowProcA;
	wc.hInstance     = hinst;
	wc.lpszClassName = cls;
	if (!GetClassInfoA(hinst, cls, &wc)) {
		if (!RegisterClassA(&wc)) return false;
	}
	g_cli_gl_hwnd = CreateWindowExA(0, cls, "", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, 1, 1, nullptr, nullptr, hinst, nullptr);
	if (!g_cli_gl_hwnd) return false;
	g_cli_gl_hdc = GetDC(g_cli_gl_hwnd);
	if (!g_cli_gl_hdc) { DestroyWindow(g_cli_gl_hwnd); g_cli_gl_hwnd = nullptr; return false; }

	PIXELFORMATDESCRIPTOR pfd = {};
	pfd.nSize      = sizeof(pfd);
	pfd.nVersion   = 1;
	pfd.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.cDepthBits = 24;
	pfd.iLayerType = PFD_MAIN_PLANE;
	int pf = ChoosePixelFormat(g_cli_gl_hdc, &pfd);
	if (pf == 0) { ReleaseDC(g_cli_gl_hwnd, g_cli_gl_hdc); DestroyWindow(g_cli_gl_hwnd); g_cli_gl_hdc=nullptr; g_cli_gl_hwnd=nullptr; return false; }
	if (!SetPixelFormat(g_cli_gl_hdc, pf, &pfd)) { ReleaseDC(g_cli_gl_hwnd, g_cli_gl_hdc); DestroyWindow(g_cli_gl_hwnd); g_cli_gl_hdc=nullptr; g_cli_gl_hwnd=nullptr; return false; }

	g_cli_gl_hrc = wglCreateContext(g_cli_gl_hdc);
	if (!g_cli_gl_hrc) { ReleaseDC(g_cli_gl_hwnd, g_cli_gl_hdc); DestroyWindow(g_cli_gl_hwnd); g_cli_gl_hdc=nullptr; g_cli_gl_hwnd=nullptr; return false; }
	if (!wglMakeCurrent(g_cli_gl_hdc, g_cli_gl_hrc)) { wglDeleteContext(g_cli_gl_hrc); g_cli_gl_hrc=nullptr; ReleaseDC(g_cli_gl_hwnd, g_cli_gl_hdc); DestroyWindow(g_cli_gl_hwnd); g_cli_gl_hdc=nullptr; g_cli_gl_hwnd=nullptr; return false; }
	return true;
}

static void destroy_hidden_wgl_context() {
	if (g_cli_gl_hrc) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(g_cli_gl_hrc); g_cli_gl_hrc = nullptr; }
	if (g_cli_gl_hdc && g_cli_gl_hwnd) { ReleaseDC(g_cli_gl_hwnd, g_cli_gl_hdc); g_cli_gl_hdc = nullptr; }
	if (g_cli_gl_hwnd) { DestroyWindow(g_cli_gl_hwnd); g_cli_gl_hwnd = nullptr; }
}
#endif
