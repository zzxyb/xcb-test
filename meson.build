project(
	'xcb-test',
	'c',
	version: '0.19.0-dev',
	license: 'MIT',
	meson_version: '>=1.3',
	default_options: [
		'c_std=' + (meson.version().version_compare('>=1.4.0') ? 'c23,c11' : 'c11'),
		'warning_level=2',
		'werror=true',
	],
)

add_project_arguments([
	'-D_POSIX_C_SOURCE=200809L',
], language: 'c')

cc = meson.get_compiler('c')

add_project_arguments(cc.get_supported_arguments([
	'-Wundef',
	'-Wlogical-op',
	'-Wmissing-include-dirs',
	'-Wold-style-definition',
	'-Wpointer-arith',
	'-Winit-self',
	'-Wstrict-prototypes',
	'-Wimplicit-fallthrough=2',
	'-Wendif-labels',
	'-Wstrict-aliasing=2',
	'-Woverflow',
	'-Wmissing-prototypes',
	'-Walloca',

	'-Wno-missing-braces',
	'-Wno-missing-field-initializers',
	'-Wno-unused-parameter',
]), language: 'c')

xcb_dep = dependency('xcb', required: true, disabler: true)
xcb_randr_dep = dependency('xcb-randr', required: true, disabler: true)
xcb_icccm_dep = dependency('xcb-icccm', required: true, disabler: true)
xcb_glx_dep = dependency('xcb-glx', required: true, disabler: true)
xcb_xvmc_dep = dependency('xcb-xvmc', required: true, disabler: true)
xcb_xinput_dep = dependency('xcb-xinput', required: true, disabler: true)
xcb_render_dep = dependency('xcb-render', required: true, disabler: true)
xcb_atom_dep = dependency('xcb-atom', required: true, disabler: true)
xcb_keysyms_dep = dependency('xcb-keysyms', required: true, disabler: true)
xcb_errors_dep = dependency('xcb-errors', required: true, disabler: true)
xcb_aux_dep = dependency('xcb-aux', required: true, disabler: true)
xcb_record_dep = dependency('xcb-record', required: true, disabler: true)
x11_xcb_dep = dependency('x11-xcb', required: true, disabler: true)
xcb_dri3_dep = dependency('xcb-dri3', required: true, disabler: true)
xcb_res_dep = dependency('xcb-res', required: true, disabler: true)
xcb_util_dep = dependency('xcb-util', required: true, disabler: true)
xcb_present_dep = dependency('xcb-present', required: true, disabler: true)
xcb_shape_dep = dependency('xcb-shape', required: true, disabler: true)
xcb_dpms_dep = dependency('xcb-dpms', required: true, disabler: true)
cairo_xcb_dep = dependency('cairo-xcb', required: true, disabler: true)
cairo_xcb_shm_dep = dependency('cairo-xcb-shm', required: true, disabler: true)
xcb_renderutil_dep = dependency('xcb-renderutil', required: true, disabler: true)
xcb_composite_dep = dependency('xcb-composite', required: true, disabler: true)
xcb_image_dep = dependency('xcb-image', required: true, disabler: true)
xcb_event_dep = dependency('xcb-event', required: true, disabler: true)
xcb_xf86dri_dep = dependency('xcb-xf86dri', required: true, disabler: true)
xcb_cursor_dep = dependency('xcb-cursor', required: true, disabler: true)
xcb_screensaver_dep = dependency('xcb-screensaver', required: true, disabler: true)
xcb_ewmh_dep = dependency('xcb-ewmh', required: true, disabler: true)
xcb_xtest_dep = dependency('xcb-xtest', required: true, disabler: true)
xcb_xkb_dep = dependency('xcb-xkb', required: true, disabler: true)
xcb_imdkit_dep = dependency('xcb-imdkit', required: true, disabler: true)
xcb_shm_dep = dependency('xcb-shm', required: true, disabler: true)
xcb_damage_dep = dependency('xcb-damage', required: true, disabler: true)
xcb_xfixes_dep = dependency('xcb-xfixes', required: true, disabler: true)
xcb_sync_dep = dependency('xcb-sync', required: true, disabler: true)
xcb_dri2_dep = dependency('xcb-dri2', required: true, disabler: true)
xcb_xinerama_dep = dependency('xcb-xinerama', required: true, disabler: true)
xcb_xv_dep = dependency('xcb-xv', required: true, disabler: true)
xcb_proto_dep = dependency('xcb-proto', required: true, disabler: true)
xcb_input_dep =  dependency('xcb-xinput', required: true, disabler: true)

examples = {
	'monitoring_damage': {
		'src': 'monitoring_damage.c',
		'dep': [xcb_damage_dep, xcb_proto_dep],
	},

	'no_decorations_window': {
		'src': 'no_decorations_window.c',
		'dep': [xcb_dep, xcb_proto_dep],
	},
}

foreach name, element : examples
	executable(
		name,
		element.get('src', []),
		dependencies: [xcb_dep, element.get('dep', [])],
	)
endforeach
