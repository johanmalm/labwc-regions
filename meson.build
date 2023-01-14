project(
  'labwc-regions',
  'c',
  license: 'GPL-2.0-only',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ],
)

add_project_arguments(
  [
    '-Wno-unused-parameter',
  ],
  language: 'c',
)

cc = meson.get_compiler('c')
math = cc.find_library('m')

wayland_client = dependency('wayland-client', version: '>=1.20.0')
wayland_cursor = dependency('wayland-cursor')
wayland_protos = dependency('wayland-protocols')
xkbcommon = dependency('xkbcommon')
cairo = dependency('cairo')
pangocairo = dependency('pangocairo')
xml2 = dependency('libxml-2.0')

wayland_scanner = find_program('wayland-scanner')
wl_protocol_dir = wayland_protos.get_variable('pkgdatadir')
wayland_scanner_code = generator(
  wayland_scanner,
  output: '@BASENAME@-protocol.c',
  arguments: ['private-code', '@INPUT@', '@OUTPUT@'],
)

wayland_scanner_client = generator(
  wayland_scanner,
  output: '@BASENAME@-client-protocol.h',
  arguments: ['client-header', '@INPUT@', '@OUTPUT@'],
)

protos_src = []

client_protocols = [
  wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml',
  'wlr-layer-shell-unstable-v1.xml',
]

foreach xml : client_protocols
  protos_src += wayland_scanner_code.process(xml)
  protos_src += wayland_scanner_client.process(xml)
endforeach

dependencies = [
  cairo,
  math,
  pangocairo,
  xkbcommon,
  wayland_client,
  wayland_cursor,
  xml2,
]

sources = files(
  'main.c',
  'regions.c',
  'scene.c',
  'util.c',
)

executable(
  meson.project_name(),
  sources + protos_src,
  dependencies: dependencies,
)

