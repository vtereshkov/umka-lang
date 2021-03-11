" Vim syntax file
" Language:	umka
" Maintainer: Marek Maskarinec	
" Last Change:	2021 Feb 11
" Mosly made by: David Barnett (https://github.com/google/vim-ft-go

syn case match

syn keyword     umkaDirective         import
syn keyword     umkaDeclaration       var const type
syn keyword     umkaDeclType          struct

hi def link     umkaDirective         Statement
hi def link     umkaDeclaration       Keyword
hi def link     umkaDeclType          Keyword

" Keywords within functions
syn keyword     umkaStatement         return break continue
syn keyword     umkaConditional       if else switch select
syn keyword     umkaLabel             case default
syn keyword     umkaRepeat            for in 

hi def link     umkaStatement         Statement
hi def link     umkaConditional       Conditional
hi def link     umkaLabel             Label
hi def link     umkaRepeat            Repeat

" Predefined types
syn keyword     umkaType              bool str error
syn keyword     umkaSignedInts        int int8 int16 int32 int64 char
syn keyword     umkaUnsignedInts      uint uint8 uint16 uint32 uint64
syn keyword     umkaFloats            real32 real

hi def link     umkaType              Type
hi def link     umkaSignedInts        Type
hi def link     umkaUnsignedInts      Type
hi def link     umkaFloats            Type

" Treat func specially: it's a declaration at the start of a line, but a type
" elsewhere. Order matters here.
syn match       umkaType              /\<fn\>/
syn match       umkaDeclaration       /^fn\>/

" Predefined functions and values
syn keyword     umkaBuiltins          append len
syn keyword     umkaBuiltins          make new printf
syn keyword     umkaConstants         true false null

hi def link     umkaBuiltins          Keyword
hi def link     umkaConstants         Keyword

" Comments; their contents
syn keyword     umkaTodo              contained TODO FIXME XXX BUG
syn cluster     umkaCommentGroup      contains=umkaTodo
syn region      umkaComment           start="/\*" end="\*/" contains=@umkaCommentGroup,@Spell
syn region      umkaComment           start="//" end="$" contains=@umkaCommentGroup,@Spell

hi def link     umkaComment           Comment
hi def link     umkaTodo              Todo

" umka escapes
syn match       umkaEscapeOctal       display contained "\\[0-7]\{3}"
syn match       umkaEscapeC           display contained +\\[abfnrtv\\'"]+
syn match       umkaEscapeX           display contained "\\x\x\{2}"
syn match       umkaEscapeU           display contained "\\u\x\{4}"
syn match       umkaEscapeBigU        display contained "\\U\x\{8}"
syn match       umkaEscapeError       display contained +\\[^0-7xuUabfnrtv\\'"]+

hi def link     umkaEscapeOctal       umkaSpecialString
hi def link     umkaEscapeC           umkaSpecialString
hi def link     umkaEscapeX           umkaSpecialString
hi def link     umkaEscapeU           umkaSpecialString
hi def link     umkaEscapeBigU        umkaSpecialString
hi def link     umkaSpecialString     Special
hi def link     umkaEscapeError       Error

" Strings and their contents
syn cluster     umkaStringGroup       contains=umkaEscapeOctal,umkaEscapeC,umkaEscapeX,umkaEscapeU,umkaEscapeBigU,umkaEscapeError
syn region      umkaString            start=+"+ skip=+\\\\\|\\"+ end=+"+ contains=@umkaStringGroup
syn region      umkaRawString         start=+`+ end=+`+

hi def link     umkaString            String
hi def link     umkaRawString         String

" Characters; their contents
syn cluster     umkaCharacterGroup    contains=umkaEscapeOctal,umkaEscapeC,umkaEscapeX,umkaEscapeU,umkaEscapeBigU
syn region      umkaCharacter         start=+'+ skip=+\\\\\|\\'+ end=+'+ contains=@umkaCharacterGroup

hi def link     umkaCharacter         Character

" Regions
syn region      umkaBlock             start="{" end="}" transparent fold
syn region      umkaParen             start='(' end=')' transparent

" Integers
syn match       umkaDecimalInt        "\<\d\+\([Ee]\d\+\)\?\>"
syn match       umkaHexadecimalInt    "\<0x\x\+\>"
syn match       umkaOctalInt          "\<0\o\+\>"
syn match       umkaOctalError        "\<0\o*[89]\d*\>"

hi def link     umkaDecimalInt        Integer
hi def link     umkaHexadecimalInt    Integer
hi def link     umkaOctalInt          Integer
hi def link     Integer             Number

" Floating point
syn match       umkaFloat             "\<\d\+\.\d*\([Ee][-+]\d\+\)\?\>"
syn match       umkaFloat             "\<\.\d\+\([Ee][-+]\d\+\)\?\>"
syn match       umkaFloat             "\<\d\+[Ee][-+]\d\+\>"

hi def link     umkaFloat             Float


" Search backwards for a global declaration to start processing the syntax.
"syn sync match umkaSync grouphere NONE /^\(const\|var\|type\|func\)\>/

" There's a bug in the implementation of grouphere. For now, use the
" following as a more expensive/less precise workaround.
syn sync minlines=500

let b:current_syntax = 'umka'

" vim: sw=2 sts=2 et
