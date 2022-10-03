project('umka-embed', 'c')

umka_dep = dependency('umka', required: false)

if not umka_dep.found()
  umka_proj = subproject('umka')
  umka_dep = umka_proj.get_variable('umka_dep')
endif

umka_proj = subproject('umka')
umka_dep = umka_proj.get_variable('umka_dep')

exe = executable('umka_test', 'main.c',
  dependencies: umka_dep)

