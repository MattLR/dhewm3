// Tik_parse.cpp
//
// See Tik_parse.h.
//
// The file is read in two steps:
//   1. Tokenize() turns the file and everything it $include's into one flat list of tokens.
//      Each token remembers whether it was the first one on its line, because that is what
//      ends a command's arguments.
//   2. tikParser_t walks the token list looking only at the setup / animations blocks.
//
// Step 1 is done with the engine's idLexer in LEXFL_ONLYSTRINGS mode, which gets us comments,
// quoted strings and line counting. Two things about a .tik need handling around it:
//   * LEXFL_ONLYSTRINGS still ends a word at characters like '+' or '{', so adjacent pieces with no
//     white space between them are glued back together, and braces are then split out again.
//   * idLexer::ReadWhiteSpace() skips one character after the end of a block comment, which eats
//     the line break (or a brace) that follows "*/". TikSanitize() puts a space after every "*/" so the
//     character that gets eaten is a harmless one.
//

#include "sys/platform.h"
#include "renderer/tr_local.h"

#include "idlib/Lexer.h"
#include "renderer/Tik_parse.h"

#include <stdarg.h>

/*
========================
TikNormalizeDir

"models/foo" -> "models/foo/", "" stays "".
========================
*/
static void TikNormalizeDir(const char* in, idStr& out) {
	out = in;
	out.BackSlashesToSlashes();
	if (out.Length() > 0 && out.c_str()[out.Length() - 1] != '/') {
		out += "/";
	}
}

/*
========================
TikJoin
========================
*/
static void TikJoin(const idStr& dir, const char* file, idStr& out) {
	out = dir.c_str();
	out += file;
	out.BackSlashesToSlashes();
}

/*
========================
TikSurfaceFlag

Returns 0 for flags that don't matter here.
========================
*/
static int TikSurfaceFlag(const char* name) {
	static const struct {
		const char* name;
		int			flag;
	} flags[] = {
		{ "nodraw",			TIK_SURF_NODRAW },
		{ "nocustomshader",	TIK_SURF_NOCUSTOMSHADER },
		{ "skin1",			TIK_SURF_SKIN1 },
		{ "skin2",			TIK_SURF_SKIN2 },
		{ "crossfade",		TIK_SURF_CROSSFADE },
		{ "nodamage",		TIK_SURF_NODAMAGE },
		{ "nomipmaps",		TIK_SURF_NOMIPMAPS },
		{ "nopicmip",		TIK_SURF_NOPICMIP },
	};

	for (int i = 0; i < (int)(sizeof(flags) / sizeof(flags[0])); i++) {
		if (idStr::Icmp(name, flags[i].name) == 0) {
			return flags[i].flag;
		}
	}
	return 0;
}

#define TIK_MAX_WARNINGS	32		// per Parse(), a broken file shouldn't be able to flood the console

struct tikToken_t {
	idStr	text;
	int		file;			// index into tikParser_t::files, for messages
	int		line;
	bool	newLine;		// first token on its line
	bool	quoted;			// was written "like this", so it is never a brace or a keyword
};

class tikParser_t {
public:
	tikParser_t(idTikFile& file, bool peekOnly = false) : out(file), cursor(0), overflow(false), modelOnly(peekOnly), numWarnings(peekOnly ? TIK_MAX_WARNINGS + 1000 : 0) {}

	bool			Run(const char* fileName);

private:
	idTikFile& out;
	idStrList		files;				// every file opened, for messages
	idStrList		stack;				// the include chain, to catch loops
	idList<tikToken_t> tokens;
	int				cursor;
	bool			overflow;
	bool			modelOnly;			// stop at the first skelmodel, skip animations, stay quiet

	// parser state that carries over from block to block and file to file
	idStr			pathDir;			// "path"
	idStr			texturePath;		// "texturepath"
	mutable int		numWarnings;

	void			WarnAt(const char* fileName, int line, const char* fmt, ...) const;

	// step 1
	struct lexState_t;
	bool			Tokenize(const char* fileName, int depth);
	void			EmitPiece(lexState_t& st, const char* text, int len, bool quoted, bool newLine);
	void			FlushWord(lexState_t& st);
	void			AddToken(const char* start, int len, bool quoted, int file, int line, bool newLine);

	// step 2
	bool			IsBrace(int i, char c) const;
	bool			IsKeyword(int i, const char* keyword) const;
	void			Warn(int tokenIndex, const char* fmt, ...) const;
	void			SkipBlock();
	void			SkipRestOfLine();
	void			GatherArgs(idStrList& args);

	void			ParseTokens();
	void			ParseSetup();
	void			ParseSurface(const idStrList& args, int tokenIndex);
	void			ParseAnimations();
};

/*
========================
tikParser_t::WarnAt
========================
*/
void tikParser_t::WarnAt(const char* fileName, int line, const char* fmt, ...) const {
	char	msg[1024];
	va_list	args;

	numWarnings++;
	if (numWarnings > TIK_MAX_WARNINGS) {
		if (numWarnings == TIK_MAX_WARNINGS + 1) {
			common->Warning("idTikFile: too many warnings, the rest are not shown");
		}
		return;
	}

	va_start(args, fmt);
	idStr::vsnPrintf(msg, sizeof(msg), fmt, args);
	va_end(args);
	msg[sizeof(msg) - 1] = '\0';

	common->Warning("idTikFile: %s:%i: %s", fileName, line, msg);
}

/*
========================
tikParser_t::Run
========================
*/
bool tikParser_t::Run(const char* fileName) {
	Tokenize(fileName, 0);

	// nothing was opened, so the file itself couldn't be read
	if (files.Num() == 0) {
		return false;
	}

	ParseTokens();
	return true;
}

/*
========================
tikParser_t::AddToken
========================
*/
void tikParser_t::AddToken(const char* start, int len, bool quoted, int file, int line, bool newLine) {
	char		tmp[TIK_MAX_TOKEN_LEN];
	tikToken_t	token;

	if (tokens.Num() >= TIK_MAX_TOKENS) {
		if (!overflow) {
			overflow = true;
			WarnAt(files[file].c_str(), line, "more than %i tokens, ignoring the rest", TIK_MAX_TOKENS);
		}
		return;
	}

	if (len >= TIK_MAX_TOKEN_LEN) {
		WarnAt(files[file].c_str(), line, "token is longer than %i characters, truncated", TIK_MAX_TOKEN_LEN - 1);
		len = TIK_MAX_TOKEN_LEN - 1;
	}
	memcpy(tmp, start, len);
	tmp[len] = '\0';

	token.text = tmp;
	token.file = file;
	token.line = line;
	token.newLine = newLine;
	token.quoted = quoted;
	tokens.Append(token);
}

/*
========================
tikParser_t::lexState_t

Everything Tokenize() needs to remember about the file it is reading. It is per call, because
an $include starts another Tokenize() while this one is still in the middle of a word.
========================
*/
struct tikParser_t::lexState_t {
	idStr		path;
	int			fileIndex;
	int			depth;
	bool		pendingInclude;		// the last piece was "$include", the next one is the file name
	int			includeLine;

	bool		haveWord;			// a word is being assembled, it is only complete once the next token isn't glued to it
	idStr		word;
	bool		wordQuoted;
	bool		wordNewLine;
	int			wordLine;
};

/*
========================
TikSanitize

Copies the file into a NUL terminated buffer that idLexer can read safely:
  * a space is added after every end-of-block-comment marker, see the note at the top of the file
  * braces outside quotes get a space on each side
  * NUL bytes become spaces, so the lexer cannot stop in the middle of the file
  * bytes of 0x80 and up become '?', idLexer treats them as white space because it reads plain
	(signed) chars, which would silently cut a name in two
  * an apostrophe becomes a backtick, idLexer would read it as the start of a character literal,
	and one in an unquoted word is not worth losing the rest of the line over
Returns a new[] buffer, and its length in outLen.
========================
*/
static char* TikSanitize(const char* src, int len, int& outLen) {
	char* out = new char[len * 3 + 2];
	int		n = 0;
	bool	inQuote = false;

	for (int i = 0; i < len; i++) {
		const unsigned char c = (unsigned char)src[i];

		if (c == '\n') {
			inQuote = false;
		}
		else if (c == '"') {
			inQuote = !inQuote;
		}

		if (c == 0) {
			out[n++] = ' ';
		}
		else if (c >= 0x80) {
			out[n++] = '?';
		}
		else if (c == '\'') {
			out[n++] = '`';
		}
		else if ((c == '{' || c == '}') && !inQuote) {
			// always a word of its own. Without this "}//comment" would be read as one name,
			// because ALLOWPATHNAMES lets a name carry on through slashes
			out[n++] = ' ';
			out[n++] = (char)c;
			out[n++] = ' ';
		}
		else {
			out[n++] = (char)c;
		}
		if (c == '/' && i > 0 && src[i - 1] == '*' && !inQuote) {
			out[n++] = ' ';
		}
	}
	out[n] = '\0';
	outLen = n;
	return out;
}

/*
========================
tikParser_t::EmitPiece

One finished piece of text: a word, a quoted string, or a single brace.
"$include <file>" is handled here and never reaches the token list.
========================
*/
void tikParser_t::EmitPiece(lexState_t& st, const char* text, int len, bool quoted, bool newLine) {
	if (st.pendingInclude) {
		st.pendingInclude = false;
		if (!quoted && len == 1 && (text[0] == '{' || text[0] == '}')) {
			WarnAt(st.path.c_str(), st.includeLine, "$include without a file name");
			// and carry on, the brace is an ordinary token
		}
		else {
			char	incName[TIK_MAX_TOKEN_LEN];

			if (len >= TIK_MAX_TOKEN_LEN) {
				len = TIK_MAX_TOKEN_LEN - 1;
			}
			memcpy(incName, text, len);
			incName[len] = '\0';
			if (!Tokenize(incName, st.depth + 1)) {
				WarnAt(st.path.c_str(), st.includeLine, "include of %s skipped", incName);
			}
			return;
		}
	}

	if (!quoted && len == 8 && idStr::Icmpn(text, "$include", 8) == 0) {
		st.pendingInclude = true;
		st.includeLine = st.wordLine;
		return;
	}

	AddToken(text, len, quoted, st.fileIndex, st.wordLine, newLine);
}

/*
========================
tikParser_t::FlushWord

Hands over the word that was being assembled. Quoted strings go through whole, anything else
is cut at every brace, so "setup{" and "foo}" give the same tokens as "setup {" and "foo }".
========================
*/
void tikParser_t::FlushWord(lexState_t& st) {
	if (!st.haveWord) {
		return;
	}
	st.haveWord = false;

	// EmitPiece can recurse into another file, so work from a copy
	const idStr	w = st.word;
	const char* text = w.c_str();
	const int	n = w.Length();

	if (st.wordQuoted) {
		EmitPiece(st, text, n, true, st.wordNewLine);
		return;
	}

	bool	newLine = st.wordNewLine;
	int		start = 0;

	for (int i = 0; i <= n; i++) {
		const bool brace = (i < n && (text[i] == '{' || text[i] == '}'));

		if (i < n && !brace) {
			continue;
		}
		if (i > start) {
			EmitPiece(st, text + start, i - start, false, newLine);
			newLine = false;
		}
		if (brace) {
			EmitPiece(st, text + i, 1, false, newLine);
			newLine = false;
		}
		start = i + 1;
	}
}

/*
========================
tikParser_t::Tokenize

Appends the tokens of fileName to the token list. "$include <file>" is replaced by the tokens
of that file right where it stands. Returns false if the file couldn't be used.
========================
*/
bool tikParser_t::Tokenize(const char* fileName, int depth) {
	idStr		path = fileName;
	void* buffer = NULL;
	ID_TIME_T	fileTime = 0;
	int			len, i;

	path.BackSlashesToSlashes();

	if (depth > TIK_MAX_INCLUDE_DEPTH) {
		common->Warning("idTikFile: includes nested more than %i deep, not reading %s", TIK_MAX_INCLUDE_DEPTH, path.c_str());
		return false;
	}

	for (i = 0; i < stack.Num(); i++) {
		if (stack[i].Icmp(path.c_str()) == 0) {
			common->Warning("idTikFile: %s includes itself, skipping", path.c_str());
			return false;
		}
	}

	len = fileSystem->ReadFile(path.c_str(), &buffer, &fileTime);
	if (len <= 0 || buffer == NULL) {
		// a zero length file still comes back with a buffer that has to be released
		if (buffer != NULL) {
			fileSystem->FreeFile(buffer);
		}
		common->Warning("idTikFile: couldn't read %s (missing or empty)", path.c_str());
		return false;
	}
	if (len > TIK_MAX_FILE_SIZE) {
		fileSystem->FreeFile(buffer);
		common->Warning("idTikFile: %s is larger than %i bytes, not reading it", path.c_str(), TIK_MAX_FILE_SIZE);
		return false;
	}

	if (depth == 0) {
		out.timeStamp = fileTime;
	}

	int			cleanLen;
	char* clean = TikSanitize((const char*)buffer, len, cleanLen);
	fileSystem->FreeFile(buffer);

	lexState_t	st;
	st.path = path;
	st.fileIndex = files.Append(path);
	st.depth = depth;
	st.pendingInclude = false;
	st.includeLine = 0;
	st.haveWord = false;
	st.wordQuoted = false;
	st.wordNewLine = false;
	st.wordLine = 0;
	stack.Append(path);

	idLexer		lexer(LEXFL_ONLYSTRINGS | LEXFL_ALLOWPATHNAMES | LEXFL_NOSTRINGCONCAT | LEXFL_NOSTRINGESCAPECHARS |
		LEXFL_ALLOWMULTICHARLITERALS | LEXFL_NOERRORS | LEXFL_NOWARNINGS);
	idToken		tok;
	int			line = 1;			// our own count, idLexer's loses a line at "*/"
	int			scanned = 0;		// clean[] has been counted up to here
	int			failures = 0;
	bool		first = true;

	lexer.LoadMemory(clean, cleanLen, path.c_str());

	while (tokens.Num() < TIK_MAX_TOKENS) {
		if (!lexer.ReadToken(&tok)) {
			// end of the file, or a string that has no closing quote. In the second case the lexer
			// stops at the end of that line and can carry on from the next one.
			if (lexer.EndOfFile()) {
				// idLexer stops quietly inside a comment that never ends. All that is left unread
				// at this point is white space and comments, so look for a "/*" with no end.
				const char* p = clean + lexer.GetLastWhiteSpaceStart();
				const char* e = clean + cleanLen;

				while (p < e) {
					if (*p <= ' ') {
						p++;
					}
					else if (p[0] == '/' && p[1] == '/') {
						while (p < e && *p != '\n') {
							p++;
						}
					}
					else if (p[0] == '/' && p[1] == '*') {
						const char* close = strstr(p + 2, "*/");
						if (close == NULL) {
							WarnAt(path.c_str(), line, "comment is never closed");
							break;
						}
						p = close + 2;
					}
					else {
						break;
					}
				}
				break;
			}
			for (; scanned < lexer.GetLastWhiteSpaceEnd(); scanned++) {
				if (clean[scanned] == '\n') {
					line++;
				}
			}
			WarnAt(path.c_str(), line, "quote is never closed");
			if (++failures > 16) {
				WarnAt(path.c_str(), line, "too many unreadable strings, ignoring the rest of the file");
				break;
			}
			continue;
		}

		const int	wsStart = lexer.GetLastWhiteSpaceStart();
		const int	wsEnd = lexer.GetLastWhiteSpaceEnd();
		bool		newLine = first;

		for (; scanned < wsEnd; scanned++) {
			if (clean[scanned] == '\n') {
				line++;
			}
		}
		for (i = wsStart; i < wsEnd; i++) {
			if (clean[i] == '\n') {
				newLine = true;
				break;
			}
		}

		const bool	quoted = (tok.type == TT_STRING || tok.type == TT_LITERAL);
		const bool	glued = (wsStart == wsEnd) && st.haveWord && !st.wordQuoted && !quoted;

		if (glued) {
			st.word += tok.c_str();
		}
		else {
			FlushWord(st);
			st.haveWord = true;
			st.word = tok.c_str();
			st.wordQuoted = quoted;
			st.wordNewLine = newLine;
			st.wordLine = line;
		}
		first = false;
	}

	if (tokens.Num() >= TIK_MAX_TOKENS && !overflow) {
		overflow = true;
		WarnAt(path.c_str(), line, "more than %i tokens, ignoring the rest", TIK_MAX_TOKENS);
	}
	FlushWord(st);

	if (st.pendingInclude) {
		WarnAt(path.c_str(), st.includeLine, "$include without a file name");
	}

	lexer.FreeSource();
	delete[] clean;
	stack.RemoveIndex(stack.Num() - 1);
	return true;
}

/*
========================
tikParser_t::IsBrace
========================
*/
bool tikParser_t::IsBrace(int i, char c) const {
	return i < tokens.Num() && !tokens[i].quoted && tokens[i].text.Length() == 1 && tokens[i].text.c_str()[0] == c;
}

/*
========================
tikParser_t::IsKeyword
========================
*/
bool tikParser_t::IsKeyword(int i, const char* keyword) const {
	return i < tokens.Num() && !tokens[i].quoted && tokens[i].text.Icmp(keyword) == 0;
}

/*
========================
tikParser_t::Warn
========================
*/
void tikParser_t::Warn(int tokenIndex, const char* fmt, ...) const {
	char	msg[1024];
	va_list	args;

	va_start(args, fmt);
	idStr::vsnPrintf(msg, sizeof(msg), fmt, args);
	va_end(args);
	msg[sizeof(msg) - 1] = '\0';

	if (tokenIndex >= tokens.Num()) {
		tokenIndex = tokens.Num() - 1;
	}
	if (tokenIndex < 0) {
		WarnAt(files.Num() > 0 ? files[0].c_str() : "?", 0, "%s", msg);
		return;
	}
	WarnAt(files[tokens[tokenIndex].file].c_str(), tokens[tokenIndex].line, "%s", msg);
}

/*
========================
tikParser_t::SkipBlock

The cursor is on a '{'. Leaves it just after the matching '}'.
========================
*/
void tikParser_t::SkipBlock() {
	const int	start = cursor;
	int			depth = 0;

	while (cursor < tokens.Num()) {
		if (IsBrace(cursor, '{')) {
			depth++;
		}
		else if (IsBrace(cursor, '}')) {
			depth--;
			if (depth == 0) {
				cursor++;
				return;
			}
		}
		cursor++;
	}
	Warn(start, "block is never closed");
}

/*
========================
tikParser_t::SkipRestOfLine

Skips the arguments of a command we don't care about.
========================
*/
void tikParser_t::SkipRestOfLine() {
	while (cursor < tokens.Num() && !tokens[cursor].newLine && !IsBrace(cursor, '{') && !IsBrace(cursor, '}')) {
		cursor++;
	}
}

/*
========================
tikParser_t::GatherArgs

The cursor is just after a command. Collects the rest of its line and stops at the next line
or at a brace.
========================
*/
void tikParser_t::GatherArgs(idStrList& args) {
	while (cursor < tokens.Num() && !tokens[cursor].newLine && !IsBrace(cursor, '{') && !IsBrace(cursor, '}')) {
		args.Append(tokens[cursor].text);
		cursor++;
	}
}

/*
========================
tikParser_t::ParseTokens
========================
*/
void tikParser_t::ParseTokens() {
	while (cursor < tokens.Num()) {
		if (modelOnly && out.skelModels.Num() > 0) {
			return;
		}
		if (IsBrace(cursor, '{')) {
			Warn(cursor, "'{' outside of a block, skipped");
			SkipBlock();
		}
		else if (IsBrace(cursor, '}')) {
			Warn(cursor, "'}' without a matching '{', ignored");
			cursor++;
		}
		else if (IsKeyword(cursor, "TIKI")) {
			cursor++;
		}
		else if (IsKeyword(cursor, "setup")) {
			cursor++;
			if (IsBrace(cursor, '{')) {
				ParseSetup();
			}
			else {
				Warn(cursor - 1, "setup without a block");
			}
		}
		else if (IsKeyword(cursor, "animations") && !modelOnly) {
			ParseAnimations();
		}
		else {
			// "init" and anything else we don't know: skip the block that goes with it
			cursor++;
			SkipRestOfLine();
			if (IsBrace(cursor, '{')) {
				SkipBlock();
			}
		}
	}
}

/*
========================
tikParser_t::ParseSetup

The cursor is on the '{' of a setup block.
========================
*/
void tikParser_t::ParseSetup() {
	const int start = cursor;

	cursor++;

	while (cursor < tokens.Num()) {
		if (IsBrace(cursor, '}')) {
			cursor++;
			return;
		}
		if (IsBrace(cursor, '{')) {
			SkipBlock();
			continue;
		}

		const int	cmd = cursor;
		idStrList	args;

		cursor++;
		GatherArgs(args);

		const char* name = tokens[cmd].text.c_str();

		if (tokens[cmd].quoted) {
			// not a command, nothing sensible to do with it
		}
		else if (idStr::Icmp(name, "path") == 0) {
			if (args.Num() >= 1) {
				TikNormalizeDir(args[0].c_str(), pathDir);
				out.path = pathDir;
			}
		}
		else if (idStr::Icmp(name, "loadAllAnims") == 0) {
			out.loadAllAnims = true;
		}
		else if (idStr::Icmp(name, "texturepath") == 0) {
			if (args.Num() >= 1) {
				TikNormalizeDir(args[0].c_str(), texturePath);
			}
		}
		else if (idStr::Icmp(name, "skelmodel") == 0) {
			if (args.Num() >= 1) {
				idStr model;

				TikJoin(pathDir, args[0].c_str(), model);
				if (out.skelModels.Num() < TIK_MAX_SKELMODELS) {
					out.skelModels.Append(model);
				}
				else {
					Warn(cmd, "more than %i skelmodels, ignoring %s", TIK_MAX_SKELMODELS, model.c_str());
				}
			}
			else {
				Warn(cmd, "skelmodel without a file name");
			}
		}
		else if (idStr::Icmp(name, "surface") == 0) {
			ParseSurface(args, cmd);
		}
		else if (idStr::Icmp(name, "replacesurface") == 0) {
			// replacesurface <oldsurface> <newsurface> <skbfile> [morphfile]
			if (args.Num() >= 3 && out.replaces.Num() >= TIK_MAX_SURFACES) {
				Warn(cmd, "more than %i replacesurface entries, ignoring the rest", TIK_MAX_SURFACES);
			}
			else if (args.Num() >= 3) {
				idTikFile::replace_t rep;

				rep.oldSurface = args[0].c_str();
				rep.newSurface = args[1].c_str();
				// unlike skelmodel, the file is a full path (see munro.tik)
				rep.file = args[2].c_str();
				rep.file.BackSlashesToSlashes();
				out.replaces.Append(rep);
			}
			else {
				Warn(cmd, "replacesurface needs <old> <new> <file>");
			}
		}
		// everything else (scale, lod, radius, bonemapping, morphfile, ...) is not our business

		// a command can be followed by a block of its own
		if (IsBrace(cursor, '{')) {
			SkipBlock();
		}
	}

	Warn(start, "setup block is never closed");
}

/*
========================
tikParser_t::ParseSurface

surface <name> shader <shader> [flags <flag> ...]
surface <name> flags <flag> ...
surface <name> +<flag> | -<flag>
========================
*/
void tikParser_t::ParseSurface(const idStrList& args, int tokenIndex) {
	if (args.Num() < 1) {
		Warn(tokenIndex, "surface without a name");
		return;
	}

	idTikFile::surface_t* surf = NULL;
	for (int i = 0; i < out.surfaces.Num(); i++) {
		if (out.surfaces[i].name.Icmp(args[0].c_str()) == 0) {
			surf = &out.surfaces[i];
			break;
		}
	}
	if (surf == NULL) {
		if (out.surfaces.Num() >= TIK_MAX_SURFACES) {
			Warn(tokenIndex, "more than %i surfaces, ignoring %s", TIK_MAX_SURFACES, args[0].c_str());
			return;
		}
		idTikFile::surface_t fresh;

		fresh.name = args[0].c_str();
		fresh.flags = 0;
		surf = &out.surfaces[out.surfaces.Append(fresh)];
	}

	for (int i = 1; i < args.Num(); i++) {
		const char* a = args[i].c_str();

		if (idStr::Icmp(a, "shader") == 0) {
			if (i + 1 >= args.Num()) {
				Warn(tokenIndex, "surface %s: shader without a name", surf->name.c_str());
				break;
			}
			i++;
			if (surf->shaders.Num() >= TIK_MAX_SHADERS) {
				Warn(tokenIndex, "surface %s: more than %i shaders, ignoring %s", surf->name.c_str(), TIK_MAX_SHADERS, args[i].c_str());
				continue;
			}
			idTikFile::shader_t sh;

			sh.shader = args[i].c_str();
			sh.texturePath = texturePath.c_str();
			surf->shaders.Append(sh);
		}
		else if (idStr::Icmp(a, "flags") == 0) {
			// the flag names run to the end of the line
			for (i++; i < args.Num(); i++) {
				surf->flags |= TikSurfaceFlag(args[i].c_str());
			}
		}
		else if (a[0] == '+') {
			surf->flags |= TikSurfaceFlag(a + 1);
		}
		else if (a[0] == '-') {
			surf->flags &= ~TikSurfaceFlag(a + 1);
		}
		else if (idStr::Icmp(a, "damage_multiplier") == 0) {
			i++;
		}
	}
}

/*
========================
tikParser_t::ParseAnimations

animations [<dir>]
{
	<alias> <file> [more arguments]
	[{ ... }]
}
========================
*/
void tikParser_t::ParseAnimations() {
	const int	start = cursor;
	idStr		animDir = pathDir;		// anim files are relative to the setup "path" unless "animations <dir>" says otherwise

	cursor++;

	if (cursor < tokens.Num() && !tokens[cursor].newLine && !IsBrace(cursor, '{') && !IsBrace(cursor, '}')) {
		TikNormalizeDir(tokens[cursor].text.c_str(), animDir);
		cursor++;
	}

	if (!IsBrace(cursor, '{')) {
		Warn(start, "animations without a block");
		return;
	}
	cursor++;

	while (cursor < tokens.Num()) {
		if (IsBrace(cursor, '}')) {
			cursor++;
			return;
		}
		if (IsBrace(cursor, '{')) {
			SkipBlock();
			continue;
		}

		const int	entry = cursor;
		idStrList	args;

		cursor++;
		GatherArgs(args);

		if (args.Num() >= 1 && out.anims.Num() >= TIK_MAX_ANIMS) {
			Warn(entry, "more than %i animations, ignoring the rest", TIK_MAX_ANIMS);
		}
		else if (args.Num() >= 1) {
			idTikFile::anim_t anim;

			anim.alias = tokens[entry].text.c_str();
			TikJoin(animDir, args[0].c_str(), anim.file);
			out.anims.Append(anim);
		}
		else {
			Warn(entry, "animation %s has no file", tokens[entry].text.c_str());
		}

		// the body of an animation (frame commands, server/client blocks...) is none of our business
		if (IsBrace(cursor, '{')) {
			SkipBlock();
		}
	}

	Warn(start, "animations block is never closed");
}

/*
========================
idTikFile::idTikFile
========================
*/
idTikFile::idTikFile() {
	loadAllAnims = false;
	timeStamp = 0;
}

/*
========================
idTikFile::Clear
========================
*/
void idTikFile::Clear() {
	skelModels.Clear();
	surfaces.Clear();
	replaces.Clear();
	anims.Clear();
	loadAllAnims = false;
	path.Clear();
	timeStamp = 0;
}

/*
========================
idTikFile::Parse
========================
*/
bool idTikFile::Parse(const char* fileName) {
	Clear();

	tikParser_t parser(*this);
	return parser.Run(fileName);
}

/*
========================
idTikFile::GetModelFile
========================
*/
bool idTikFile::GetModelFile(const char* fileName, idStr& modelFile) {
	idTikFile		tik;
	tikParser_t		parser(tik, true);

	if (!parser.Run(fileName) || tik.skelModels.Num() == 0) {
		return false;
	}
	modelFile = tik.skelModels[0];
	return true;
}

/*
========================
idTikFile::FindSurface
========================
*/
const idTikFile::surface_t* idTikFile::FindSurface(const char* name) const {
	for (int i = 0; i < surfaces.Num(); i++) {
		if (surfaces[i].name.Icmp(name) == 0) {
			return &surfaces[i];
		}
	}
	return NULL;
}

/*
========================
idTikFile::FindReplace
========================
*/
const idTikFile::replace_t* idTikFile::FindReplace(const char* oldSurface) const {
	for (int i = 0; i < replaces.Num(); i++) {
		if (replaces[i].oldSurface.Icmp(oldSurface) == 0) {
			return &replaces[i];
		}
	}
	return NULL;
}

/*
========================
idTikFile::GetAnimFiles
========================
*/
void idTikFile::GetAnimFiles(idStrList& files) const {
	files.Clear();

	for (int i = 0; i < anims.Num(); i++) {
		bool seen = false;

		for (int j = 0; j < files.Num(); j++) {
			if (files[j].Icmp(anims[i].file.c_str()) == 0) {
				seen = true;
				break;
			}
		}
		if (!seen) {
			files.Append(anims[i].file);
		}
	}
}