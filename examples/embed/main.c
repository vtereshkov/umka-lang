#include <stdlib.h>
#include <umka_api.h>

int main(int argc, char *argv[]) {
  void *umka = umkaAlloc();
  char *code = "fn main() { printf(\"Hello Umka!\\n\") }";

  bool ok = umkaInit(umka, NULL, code, 1024 * 1024, NULL, 0, NULL, false, false,
                     NULL);

  if (ok)
    ok = umkaCompile(umka);

  if (ok)
    ok = umkaRun(umka);

  umkaFree(umka);
  return !ok;
}
