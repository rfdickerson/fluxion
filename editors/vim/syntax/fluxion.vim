if exists('b:current_syntax')
  finish
endif

syntax keyword fluxionKeyword module type func fn let in if then else for static do
syntax keyword fluxionKeyword reactor phase before after tick deadline priority parallel safe
syntax keyword fluxionKeyword input output stream sampled state region overflow capacity on event init emit
syntax keyword fluxionBoolean true false
syntax keyword fluxionType Int Double Bool String Void Matrix
syntax match fluxionNumber "\v<\d+(\.\d+)?>"
syntax match fluxionOperator "\v(\+|-|\*|/|\^|=|==|!=|<|<=|>|>=|->|<-|\.\.)"
syntax match fluxionComment "//.*$"
syntax region fluxionBlockComment start="/\*" end="\*/"
syntax region fluxionString start=+"+ skip=+\\\\\|\\"+ end=+"+

highlight default link fluxionKeyword Keyword
highlight default link fluxionBoolean Boolean
highlight default link fluxionType Type
highlight default link fluxionNumber Number
highlight default link fluxionOperator Operator
highlight default link fluxionComment Comment
highlight default link fluxionBlockComment Comment
highlight default link fluxionString String

let b:current_syntax = 'fluxion'
