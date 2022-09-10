project('umka', 'c', version: '0.8')

add_project_arguments(['-fPIC', '-DUMKA_EXT_LIBS'], language: 'c')

cc = meson.get_compiler('c')
deps = []

if target_machine.system() != 'darwin'
  deps += cc.find_library('m', required: true)
endif

if target_machine.system() == 'linux'
  deps += cc.find_library('dl', required: true)
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

src_exe = src + 'src/umka.c'

inc = include_directories('import/embed')

executable(
  'umka',
  src_exe,
  include_directories: inc,
  dependencies: deps)

both_libraries(
  'umka',
  src,
  include_directories: inc,
  dependencies: deps)

