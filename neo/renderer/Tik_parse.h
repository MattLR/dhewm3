// Tik_parse.h
//
// Reader for the parts of a MOHAA / Elite Force 2 .tik file that matter to the renderer:
//
//   * the model(s)              setup { path ...  skelmodel ... }
//   * the surface materials     setup { texturepath ...  surface <name> shader <shader> [flags ...] }
//   * surface replacement       setup { replacesurface <old> <new> <skbfile> }
//   * the animation files       animations <dir> { <alias> <file.ska> [{ ... }] }
//
// Everything else in the file (init blocks, bonemapping, lod, scale, sounds, ...) is skipped
// without being interpreted, that part is left to the Doom 3 decl system.
//
// A .tik is a text file with an odd layout:
//   * "$include <file>" is plain textual inclusion and may appear anywhere, including
//     between the entries of a block
//   * commands are line based, their arguments run to the end of the line, but a block
//     ({ ... }) may follow on the next line
//   * the same command can be repeated and later lines inherit state from earlier ones
//     (a "texturepath" applies to every "surface" line after it)
//   * several "surface <name> shader ..." lines for one surface are alternative skins
//

#ifndef __TIK_PARSE_H__
#define __TIK_PARSE_H__

#define TIK_MAX_INCLUDE_DEPTH	8
#define TIK_MAX_TOKENS			262144
#define TIK_MAX_TOKEN_LEN		1024
#define TIK_MAX_FILE_SIZE		( 16 * 1024 * 1024 )	// per file, including every $include
#define TIK_MAX_SHADERS			4			// same as MAX_TIKI_SHADER in tiki_shared.h
#define TIK_MAX_SKELMODELS		12			// same as MAX_SKELMODELS in tiki_shared.h
#define TIK_MAX_ANIMS			4095		// same as MAX_TIKI_ALIASES in tiki_shared.h
#define TIK_MAX_SURFACES		1024		// surfaces and replacesurface entries, each. Keeps the name lookups cheap on hostile input

// Same bit values as the TIKI_SURF_* flags in tiki_shared.h, so they can be compared with
// engine data. NOCUSTOMSHADER has no TIKI_SURF_ equivalent there.
#define TIK_SURF_SKIN1			( 1 << 0 )
#define TIK_SURF_SKIN2			( 1 << 1 )
#define TIK_SURF_NODRAW			( 1 << 2 )
#define TIK_SURF_CROSSFADE		( 1 << 6 )
#define TIK_SURF_NODAMAGE		( 1 << 7 )
#define TIK_SURF_NOMIPMAPS		( 1 << 8 )
#define TIK_SURF_NOPICMIP		( 1 << 9 )
#define TIK_SURF_NOCUSTOMSHADER	( 1 << 16 )

class idTikFile {
public:
	// one "surface <name> shader <shader>" line
	struct shader_t {
		idStr			shader;				// exactly as written, e.g. "head.tga" or "dm/hazardteam/male/normal-red"
		idStr			texturePath;		// the "texturepath" in force on that line, with a trailing '/', may be empty
	};

	struct surface_t {
		idStr			name;				// "material2"
		idList<shader_t> shaders;			// [0] is the default, the rest are alternative skins (team colours...)
		int				flags;				// TIK_SURF_*
	};

	// replacesurface <oldSurface> <newSurface> <file>: draw surface <newSurface> of <file> in place of <oldSurface>
	struct replace_t {
		idStr			oldSurface;
		idStr			newSurface;
		idStr			file;				// full path as written
	};

	struct anim_t {
		idStr			alias;				// "dm_idle"
		idStr			file;				// full path: "models/char/base-male2/dm_idle_munro.ska" (the dir after "animations" if there is one, else the setup "path")
	};

	idTikFile();

	// Reads the file and everything it includes. Returns false only if the file itself
	// can't be read; problems inside it are reported with a warning and skipped, so a
	// missing include still gives you whatever the rest of the chain contained.
	bool					Parse(const char* fileName);
	void					Clear();

	// Cheap look at a .tik that only answers "which model file does this use?". Follows the
	// $includes, skips the animation lists, and stops at the first skelmodel. Warns about nothing
	// but an unreadable file. Meant for deciding which loader (.skb or .tan) should get the .tik.
	// Returns false if the file can't be read or names no model, modelFile is then left alone.
	static bool				GetModelFile(const char* fileName, idStr& modelFile);

	const surface_t* FindSurface(const char* name) const;
	const replace_t* FindReplace(const char* oldSurface) const;

	// every distinct animation file, in order of first appearance (several aliases can share one)
	void					GetAnimFiles(idStrList& files) const;

	idStrList				skelModels;		// full paths, in order of appearance, usually just one
	idList<surface_t>		surfaces;
	idList<replace_t>		replaces;
	idList<anim_t>			anims;
	bool					loadAllAnims;	// setup "loadAllAnims": every .ska in the path dir is an animation too
	idStr					path;			// setup "path", with a trailing '/' (empty if there is none)
	ID_TIME_T				timeStamp;		// of the file passed to Parse()
};

#endif /* !__TIK_PARSE_H__ */