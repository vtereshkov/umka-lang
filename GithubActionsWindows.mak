all: $(Objs)

CFLAGS = -DUMKA_BUILD
CC = CL

Objs = src\umka_api.obj src\umka_common.obj src\umka_compiler.obj src\umka_const.obj src\umka_decl.obj src\umka_expr.obj src\umka_gen.obj src\umka_ident.obj src\umka_lexer.obj src\umka_runtime.obj src\umka_stmt.obj src\umka_types.obj src\umka_vm.obj

{src}.c.{src}obj:
	$(CC) $(CFLAGS) -c  $<

$(Objs) :


