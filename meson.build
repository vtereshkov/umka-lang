project('umka', 'c', version: '0.8')

add_project_arguments(['-fPIC', '-DUMKA_EXT_LIBS', '-DUMKA_BUILD'], language: 'c')

cc = meson.get_compiler('c')
deps = []

if target_machine.system() != 'darwin'
  deps += [cc.find_library('m', required: true)]
endif

if target_machine.system() == 'linux'
  deps += [cc.find_library('dl', required: true)]
endif

debug = get_option('buildtype') == 'debug' or get_option('buildtype') == 'debugoptimized'

sanitizers = []

if debug
  sanitizers += ['address', 'undefined']
endif

if sanitizers.length() > 0
  sans = ['-fsanitize=' + ','.join(sanitizers)]
  sans_f = ['-fsanitize=' + ','.join(sanitizers + ['fuzzer'])]
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

src_exe = src + ['src/umka.c']
src_fuz = src + ['src/umka_fuzz.c']

public_headers = 'include'

inc = include_directories(['import/embed', public_headers])

executable('umka', src_exe,
  include_directories: inc,
  dependencies: deps,
  c_args: sans,
  link_args: sans,
  install: true)

if debug and meson.get_compiler('c').has_argument(sans_f)
  executable('umkafuzz', src_fuz,
    include_directories: inc,
    dependencies: deps,
    c_args: sans_f,
    link_args: sans_f)
  executable('umkacovfuzz', src_fuz,
    include_directories: inc,
    dependencies: deps,
    c_args: sans_f + ['-fprofile-instr-generate', '-fcoverage-mapping'],
    link_args: sans_f + ['-fprofile-instr-generate', '-fcoverage-mapping'])
endif

umka_lib = both_libraries(
  'umka',
  src,
  include_directories: inc,
  dependencies: deps,
  install: true)



umka_dep = declare_dependency(
  include_directories: public_headers,
  link_with: umka_lib)

install_headers(public_headers / 'umka_api.h', subdir: meson.project_name())

pkg_mod = import('pkgconfig')
pkg_mod.generate(
  name: meson.project_name(),
  filebase: meson.project_name(),
  description: 'A statically typed embeddable scripting language',
  subdirs: meson.project_name())

