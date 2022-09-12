project('umka', 'c', version: '0.8')

add_project_arguments('-DUMKA_BUILD', language: 'c')

cc = meson.get_compiler('c')

m_dep = cc.find_library('m', required: false)
dl_dep = cc.find_library('dl', required: true)

deps = [m_dep, dl_dep]

if dl_dep.found()
  add_project_arguments('-DUMKA_EXT_LIBS', language: 'c')
endif

sanitizers = []

if get_option('debug')
  sanitizers += ['address', 'undefined']
endif

if sanitizers.length() > 0
  sans = ['-fsanitize=' + ','.join(sanitizers)]
  sans_f = ['-fsanitize=' + ','.join(sanitizers + ['fuzzer'])]

  if not cc.has_argument(sans)
    sans = []
  endif
else
  sans = []
  sans_f = []
endif

src = [
  'src/umka_api.c',
  'src/umka_common.c',
  'src/umka_compiler.c',
  'src/umka_const.c',
  'src/umka_decl.c',
  'src/umka_expr.c',
  'src/umka_gen.c',
  'src/umka_ident.c',
  'src/umka_lexer.c',
  'src/umka_runtime.c',
  'src/umka_stmt.c',
  'src/umka_types.c',
  'src/umka_vm.c']

src_exe = ['src/umka.c']
src_fuz = ['src/umka_fuzz.c']

public_headers = 'include'

inc = include_directories(['import/embed', public_headers])

umka_lib = library('umka', src,
  include_directories: inc,
  dependencies: deps,
  install: true)

executable('umka', src_exe,
  link_with: umka_lib,
  include_directories: inc,
  dependencies: deps,
  c_args: sans,
  link_args: sans,
  install: true)

if get_option('debug') and cc.has_argument(sans_f)
  executable('umkafuzz', src_fuz,
    link_with: umka_lib,
    include_directories: inc,
    dependencies: deps,
    c_args: sans_f,
    link_args: sans_f)
  executable('umkacovfuzz', src_fuz,
    link_with: umka_lib,
    include_directories: inc,
    dependencies: deps,
    c_args: sans_f + ['-fprofile-instr-generate', '-fcoverage-mapping'],
    link_args: sans_f + ['-fprofile-instr-generate', '-fcoverage-mapping'])
endif


umka_dep = declare_dependency(
  include_directories: public_headers,
  link_with: umka_lib)

install_headers(public_headers / 'umka_api.h', subdir: meson.project_name())

pkg_mod = import('pkgconfig')
pkg_mod.generate(
  name: meson.project_name(),
  filebase: meson.project_name(),
  description: 'A statically typed embeddable scripting language',
  libraries: umka_lib,
  subdirs: meson.project_name())

