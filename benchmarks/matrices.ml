(* Matrix multiplication benchmark - OCaml version *)

(* To install ocaml:
  1) you need opam >= 2.1
  2) opam switch create 4.12.1+flambda --package=ocaml-variants.4.12.1+options,ocaml-option-flambda
     eval `opam env`
     opam install ocamlfind mtime --yes
  3) run benchmarks  ## Intel(R) Core(TM) i5-4570 CPU @ 3.20GHz
    ```
    ✗ ocaml benchmarks/matrices.ml
    elapsed: 2.393648
    check: -122027500.000000
    ✗ wren_cli benchmarks/matrices.wren
    elapsed: 6.258158
    check: -122027500
    ✗ umka_linux/umka benchmarks/matrices.um
    elapsed: 7.191
    check: -122027500.000000
    ✗ python3 benchmarks/matrices.py
    elapsed: 11.765505829000176
    check: -122027500.0
    ```
*)

#use "topfind";;
#require "mtime.clock.os";;


let size = 400

let a = Array.init size (fun i -> Array.init size (fun j -> (3. *. (float_of_int i) +. (float_of_int j))))
let b = Array.init size (fun i -> Array.init size (fun j -> ((float_of_int i) -. 3. *. (float_of_int j))))
let c = Array.init size (fun _ -> Array.init size (fun _ -> 0.))

(* Multiply matrices *)
let start = Mtime_clock.counter ();;

let () =
  for i = 0 to size-1 do
    for j = 0 to size-1 do
      let s = ref 0.0 in
      for k = 0 to size-1 do  s := !s +. a.(i).(k) *. b.(k).(j) done;
      c.(i).(j) <- !s
    done
  done

let () = Printf.printf "elapsed: %f\n%!" (Mtime.Span.to_s (Mtime_clock.count start))

(* Check result *)
let check =
  let check = ref 0.0 in
  for i = 0 to size-1 do
      for j = 0 to size-1 do
        check := !check +. c.(i).(j)
      done
  done;
  !check

let () = Printf.printf "check: %f\n" (check /. float_of_int size ** 2.)
