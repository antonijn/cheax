Output demo.gif

Set Shell "bash"
Set FontSize 24
Set LineHeight 1.2
Set Width 1200
Set Height 680
Set WindowBar ColorfulRight
Set WindowBarSize 60
Set Theme "Catppuccin Frappe"

Hide
Type "PS1='\[\e[1m\]\$\[\e[0m\] '; clear" Enter
Show

Type "cheaky" Sleep 500ms  Enter
Sleep 2s
Type "(.. 10)" Sleep 500ms  Enter
Sleep 2s
Type "(def my-list (.. 10))" Sleep 500ms  Enter
Sleep 1s
Type "(defun sqr (n) (* n n))" Sleep 500ms  Enter
Sleep 2s
Type "(map sqr my-list)" Sleep 500ms  Enter
Sleep 2s
Type '(put "\U0001F60A\n")' Sleep 500ms  Enter
Sleep 2s
