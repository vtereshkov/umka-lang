(defun(
			(fac (lambda (n) (cond
				((eq n 0) 1)
				(T (mul n (fac (sub n 1))))
			)))
		))