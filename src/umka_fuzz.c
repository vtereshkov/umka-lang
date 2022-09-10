#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "umka_api.h"

int LLVMFuzzerTestOneInput(uint8_t *data, size_t size) {
  // create null-terminated string from the input data
  char *string = malloc(size + 1);

  for (size_t i = 0; i < size; i++) {
    if (data[i] == 0)
      return -1;
    string[i] = data[i];
  }
  string[size] = 0;

  void *umka = umkaAlloc();

  bool ok = umkaInit(umka, "fuzz", string, 1024 * 1024, NULL, 0, NULL, false,
                     false, NULL);

  if (!ok) // should never happen - kill fuzzer
    exit(1);

  umkaCompile(umka);

  umkaFree(umka);

  return 0;
}
