# id Tech 3: text files

Status: **implemented** (`src/engines/idtech3/lexer.cpp`).

Source: the behaviour of `COM_ParseExt` in the GPL Quake III Arena source
(`code/qcommon/q_shared.c`), read for behaviour, not copied.

Shader scripts, a map's entity lump, `.arena`, `.skin` and `.cfg` files all
split into tokens the same way:

- whitespace is any byte up to and including space (so tabs, CR and NUL);
- `// ...` runs to the end of the line, `/* ... */` to its closing `*/`;
- a token starting with `"` runs to the next `"` (or the end of the text),
  without the quotes; it may hold spaces and even newlines;
- anything else is a token up to the next whitespace. Braces are tokens
  only because files put spaces around them: `{` must not touch a word.

Many directives take "the rest of the line", so the lexer can refuse to
cross a newline (`next(false)`).
