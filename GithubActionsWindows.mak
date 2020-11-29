all: $(Objs)

CFLAGS = -nologo -DUMKA_BUILD
CC  = CL

Objs = src\umka_api.o src\umka_common.o src\umka_compiler.o src\umka_const.o src\umka_decl.o src\umka_expr.o src\umka_gen.o src\umka_ident.o src\umka_lexer.o src\umka_runtime.o src\umka_stmt.o src\umka_types.o src\umka_vm.o

{src}.c.{src}obj:
	$(CC) $(CFLAGS) -c  $<

$(Objs) :


