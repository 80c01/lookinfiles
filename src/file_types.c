/* File type detection and icons, based on file names only.
 * Pure functions: no AppData, no widgets, no I/O (unit-tested in tests/). */
#include "file_types.h"

#include <string.h>

/* File extension ("" if none), ignoring dot-files initial dot */
const gchar* file_extension(const gchar* name) {
    if (name == NULL) {
        return "";
    }
    const gchar* dot = strrchr(name, '.');
    return (dot != NULL && dot != name) ? dot + 1 : "";
}

/* Return TRUE if file name indicates Markdown content */
gboolean is_markdown_file(const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    const gchar* ext = file_extension(name);
    return (g_ascii_strcasecmp(ext, "md") == 0 ||
            g_ascii_strcasecmp(ext, "markdown") == 0 ||
            g_ascii_strcasecmp(ext, "mdown") == 0 ||
            g_ascii_strcasecmp(ext, "mkd") == 0 ||
            g_ascii_strcasecmp(ext, "mkdn") == 0);
}

/* Return TRUE if file name indicates HTML content */
gboolean is_html_file(const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    const gchar* ext = file_extension(name);
    return (g_ascii_strcasecmp(ext, "html") == 0 ||
            g_ascii_strcasecmp(ext, "htm") == 0 ||
            g_ascii_strcasecmp(ext, "xhtml") == 0 ||
            g_ascii_strcasecmp(ext, "shtml") == 0);
}

/* Return TRUE if file name indicates Emacs Org-mode content */
gboolean is_org_file(const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    const gchar* ext = file_extension(name);
    return (g_ascii_strcasecmp(ext, "org") == 0 ||
            g_ascii_strcasecmp(ext, "org_archive") == 0);
}

/* Return TRUE if file name indicates an image format */
gboolean is_image_file(const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    const gchar* ext = file_extension(name);
    return (g_ascii_strcasecmp(ext, "png") == 0 ||
            g_ascii_strcasecmp(ext, "jpg") == 0 ||
            g_ascii_strcasecmp(ext, "jpeg") == 0 ||
            g_ascii_strcasecmp(ext, "gif") == 0 ||
            g_ascii_strcasecmp(ext, "svg") == 0 ||
            g_ascii_strcasecmp(ext, "svgz") == 0 ||
            g_ascii_strcasecmp(ext, "bmp") == 0 ||
            g_ascii_strcasecmp(ext, "ico") == 0 ||
            g_ascii_strcasecmp(ext, "webp") == 0 ||
            g_ascii_strcasecmp(ext, "tiff") == 0 ||
            g_ascii_strcasecmp(ext, "tif") == 0 ||
            g_ascii_strcasecmp(ext, "pnm") == 0 ||
            g_ascii_strcasecmp(ext, "pbm") == 0 ||
            g_ascii_strcasecmp(ext, "pgm") == 0 ||
            g_ascii_strcasecmp(ext, "ppm") == 0 ||
            g_ascii_strcasecmp(ext, "xpm") == 0 ||
            g_ascii_strcasecmp(ext, "xbm") == 0 ||
            g_ascii_strcasecmp(ext, "tga") == 0 ||
            g_ascii_strcasecmp(ext, "icns") == 0 ||
            g_ascii_strcasecmp(ext, "cur") == 0);
}

/* Return TRUE if file name indicates a PDF document */
gboolean is_pdf_file(const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    const gchar* ext = file_extension(name);
    return (g_ascii_strcasecmp(ext, "pdf") == 0);
}

/* Return TRUE if file name indicates a video file */
gboolean is_video_file(const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    const gchar* ext = file_extension(name);
    return (g_ascii_strcasecmp(ext, "mp4") == 0 ||
            g_ascii_strcasecmp(ext, "m4v") == 0 ||
            g_ascii_strcasecmp(ext, "mkv") == 0 ||
            g_ascii_strcasecmp(ext, "webm") == 0 ||
            g_ascii_strcasecmp(ext, "mov") == 0 ||
            g_ascii_strcasecmp(ext, "qt") == 0 ||
            g_ascii_strcasecmp(ext, "avi") == 0 ||
            g_ascii_strcasecmp(ext, "flv") == 0 ||
            g_ascii_strcasecmp(ext, "wmv") == 0 ||
            g_ascii_strcasecmp(ext, "asf") == 0 ||
            g_ascii_strcasecmp(ext, "ogv") == 0 ||
            g_ascii_strcasecmp(ext, "ts") == 0 ||
            g_ascii_strcasecmp(ext, "mts") == 0 ||
            g_ascii_strcasecmp(ext, "m2ts") == 0 ||
            g_ascii_strcasecmp(ext, "3gp") == 0 ||
            g_ascii_strcasecmp(ext, "vob") == 0 ||
            g_ascii_strcasecmp(ext, "mpg") == 0 ||
            g_ascii_strcasecmp(ext, "mpeg") == 0);
}

/* Return TRUE if file name indicates an audio file */
gboolean is_audio_file(const gchar* name) {
    if (name == NULL) {
        return FALSE;
    }
    const gchar* ext = file_extension(name);
    return (g_ascii_strcasecmp(ext, "mp3") == 0 ||
            g_ascii_strcasecmp(ext, "wav") == 0 ||
            g_ascii_strcasecmp(ext, "wave") == 0 ||
            g_ascii_strcasecmp(ext, "ogg") == 0 ||
            g_ascii_strcasecmp(ext, "oga") == 0 ||
            g_ascii_strcasecmp(ext, "opus") == 0 ||
            g_ascii_strcasecmp(ext, "flac") == 0 ||
            g_ascii_strcasecmp(ext, "aac") == 0 ||
            g_ascii_strcasecmp(ext, "m4a") == 0 ||
            g_ascii_strcasecmp(ext, "wma") == 0 ||
            g_ascii_strcasecmp(ext, "aiff") == 0 ||
            g_ascii_strcasecmp(ext, "aif") == 0 ||
            g_ascii_strcasecmp(ext, "alac") == 0 ||
            g_ascii_strcasecmp(ext, "mid") == 0 ||
            g_ascii_strcasecmp(ext, "midi") == 0);
}

/* Return TRUE if file content can be rendered richly in preview pane */
gboolean is_renderable_file(const gchar* name) {
    return is_markdown_file(name) || is_html_file(name) || is_org_file(name) || is_image_file(name) || is_pdf_file(name) || is_video_file(name) || is_audio_file(name);
}


/* VS Code-like emoji icon for a tree entry, based on name and extension */
const gchar* file_icon(const gchar* name, gboolean is_dir) {
    if (is_dir) {
        return "📁";
    }
    if (name == NULL || name[0] == '\0') {
        return "📄";
    }

    /* 1. Exact filename matches */
#define NAME_IS(x) (g_ascii_strcasecmp(name, (x)) == 0)
#define NAME_EXACT(x) (g_strcmp0(name, (x)) == 0)

    if (NAME_IS("Makefile") || NAME_IS("makefile") || NAME_IS("GNUmakefile") ||
        NAME_IS("CMakeLists.txt") || NAME_IS("meson.build")) {
        return "🛠️";
    }
    if (NAME_IS("Dockerfile") || NAME_IS("Containerfile") ||
        NAME_IS("docker-compose.yml") || NAME_IS("docker-compose.yaml") ||
        NAME_IS(".dockerignore")) {
        return "🐳";
    }
    if (NAME_IS(".gitignore") || NAME_IS(".gitattributes") ||
        NAME_IS(".gitmodules") || NAME_IS(".gitconfig")) {
        return "🔀";
    }
    if (NAME_IS("package.json")) {
        return "📦";
    }
    if (NAME_IS("package-lock.json") || NAME_IS("yarn.lock") ||
        NAME_IS("pnpm-lock.yaml") || NAME_IS("Cargo.lock") ||
        NAME_IS("Gemfile.lock") || NAME_IS("composer.lock") ||
        NAME_IS("poetry.lock") || NAME_IS("Pipfile.lock")) {
        return "🔒";
    }
    if (NAME_IS("LICENSE") || NAME_IS("LICENCE") || NAME_IS("COPYING")) {
        return "⚖️";
    }
    if (NAME_IS("README") || NAME_IS("README.md") || NAME_IS("README.txt") ||
        NAME_IS("CHANGELOG") || NAME_IS("CHANGELOG.md") ||
        NAME_IS("CONTRIBUTING") || NAME_IS("CONTRIBUTING.md")) {
        return "📖";
    }
    if (NAME_EXACT(".env") || g_str_has_prefix(name, ".env.")) {
        return "🔑";
    }
    if (NAME_IS("Cargo.toml")) {
        return "🦀";
    }
    if (NAME_IS("go.mod") || NAME_IS("go.sum")) {
        return "🐹";
    }
    if (NAME_IS("tsconfig.json") || NAME_IS("jsconfig.json")) {
        return "🟦";
    }
    if (NAME_IS(".editorconfig") || NAME_IS(".prettierrc") ||
        g_str_has_prefix(name, ".eslintrc")) {
        return "⚙️";
    }
    if (NAME_IS("Gemfile") || NAME_IS("Rakefile")) {
        return "💎";
    }
#undef NAME_IS
#undef NAME_EXACT

    /* 2. Extension matches */
    const gchar* dot = strrchr(name, '.');
    if (dot == NULL || dot == name) {
        if (name[0] == '.') {
            const gchar* base = name + 1;
            if (g_ascii_strcasecmp(base, "bashrc") == 0 ||
                g_ascii_strcasecmp(base, "zshrc") == 0 ||
                g_ascii_strcasecmp(base, "profile") == 0) {
                return "💻";
            }
            return "⚙️";
        }
        return "📄";
    }

#define EXT_IS(x) (g_ascii_strcasecmp(dot, (x)) == 0)
    /* C language: blue circle like VS Code C */
    if (EXT_IS(".c") || EXT_IS(".h")) {
        return "🔵";
    }
    /* C++: blue diamond */
    if (EXT_IS(".cc") || EXT_IS(".cpp") || EXT_IS(".cxx") ||
        EXT_IS(".hpp") || EXT_IS(".hh") || EXT_IS(".hxx") || EXT_IS(".inl")) {
        return "🔷";
    }
    /* C# */
    if (EXT_IS(".cs") || EXT_IS(".csx")) {
        return "🟩";
    }
    /* Rust */
    if (EXT_IS(".rs")) {
        return "🦀";
    }
    /* Go */
    if (EXT_IS(".go")) {
        return "🐹";
    }
    /* Python */
    if (EXT_IS(".py") || EXT_IS(".pyw") || EXT_IS(".pyx") || EXT_IS(".ipynb")) {
        return "🐍";
    }
    /* Java */
    if (EXT_IS(".java") || EXT_IS(".jar") || EXT_IS(".class")) {
        return "☕";
    }
    /* Kotlin */
    if (EXT_IS(".kt") || EXT_IS(".kts")) {
        return "🟣";
    }
    /* Swift */
    if (EXT_IS(".swift")) {
        return "🐦";
    }
    /* PHP */
    if (EXT_IS(".php")) {
        return "🐘";
    }
    /* Ruby */
    if (EXT_IS(".rb") || EXT_IS(".erb")) {
        return "💎";
    }
    /* React JSX / TSX */
    if (EXT_IS(".jsx") || EXT_IS(".tsx")) {
        return "⚛️";
    }
    /* TypeScript: blue square */
    if (EXT_IS(".ts") || EXT_IS(".mts") || EXT_IS(".cts")) {
        return "🟦";
    }
    /* JavaScript: yellow square */
    if (EXT_IS(".js") || EXT_IS(".mjs") || EXT_IS(".cjs")) {
        return "🟨";
    }
    /* Vue */
    if (EXT_IS(".vue")) {
        return "🟢";
    }
    /* Svelte */
    if (EXT_IS(".svelte")) {
        return "🔥";
    }
    /* HTML */
    if (EXT_IS(".html") || EXT_IS(".htm") || EXT_IS(".xhtml")) {
        return "🌐";
    }
    /* CSS */
    if (EXT_IS(".css")) {
        return "🎨";
    }
    /* SCSS / SASS: pink flower */
    if (EXT_IS(".scss") || EXT_IS(".sass")) {
        return "🌸";
    }
    if (EXT_IS(".less")) {
        return "🎨";
    }
    /* JSON */
    if (EXT_IS(".json") || EXT_IS(".jsonc") || EXT_IS(".json5")) {
        return "📋";
    }
    /* YAML */
    if (EXT_IS(".yaml") || EXT_IS(".yml")) {
        return "🧾";
    }
    /* TOML / INI / Conf */
    if (EXT_IS(".toml") || EXT_IS(".ini") || EXT_IS(".cfg") ||
        EXT_IS(".conf") || EXT_IS(".properties")) {
        return "⚙️";
    }
    /* XML */
    if (EXT_IS(".xml") || EXT_IS(".xsl") || EXT_IS(".xsd")) {
        return "🌐";
    }
    /* Shell / Scripting */
    if (EXT_IS(".sh") || EXT_IS(".bash") || EXT_IS(".zsh") ||
        EXT_IS(".fish") || EXT_IS(".ksh") || EXT_IS(".bat") ||
        EXT_IS(".cmd") || EXT_IS(".ps1")) {
        return "💻";
    }
    /* SQL / Database */
    if (EXT_IS(".sql") || EXT_IS(".db") || EXT_IS(".sqlite") ||
        EXT_IS(".sqlite3") || EXT_IS(".db3")) {
        return "🗄️";
    }
    /* Markdown / Documentation */
    if (EXT_IS(".md") || EXT_IS(".markdown") || EXT_IS(".mdown") ||
        EXT_IS(".rst")) {
        return "📝";
    }
    /* Emacs Org-mode */
    if (EXT_IS(".org") || EXT_IS(".org_archive")) {
        return "🦄";
    }
    /* PDF */
    if (EXT_IS(".pdf")) {
        return "📕";
    }
    /* Images */
    if (EXT_IS(".png") || EXT_IS(".jpg") || EXT_IS(".jpeg") || EXT_IS(".gif") ||
        EXT_IS(".svg") || EXT_IS(".bmp") || EXT_IS(".ico") || EXT_IS(".webp") ||
        EXT_IS(".tiff") || EXT_IS(".tif") || EXT_IS(".avif")) {
        return "🖼️";
    }
    /* Audio */
    if (is_audio_file(name)) {
        return "🎵";
    }
    /* Video */
    if (is_video_file(name)) {
        return "🎬";
    }
    /* Archives */
    if (EXT_IS(".tar") || EXT_IS(".gz") || EXT_IS(".tgz") || EXT_IS(".zip") ||
        EXT_IS(".rar") || EXT_IS(".7z") || EXT_IS(".xz") || EXT_IS(".bz2") ||
        EXT_IS(".zst") || EXT_IS(".tbz2")) {
        return "📦";
    }
    /* Compiled binaries / libraries */
    if (EXT_IS(".o") || EXT_IS(".so") || EXT_IS(".a") || EXT_IS(".bin") ||
        EXT_IS(".exe") || EXT_IS(".dylib") || EXT_IS(".dll") || EXT_IS(".out")) {
        return "⚙️";
    }
    /* Lua */
    if (EXT_IS(".lua")) {
        return "🌙";
    }
    /* Dart */
    if (EXT_IS(".dart")) {
        return "🎯";
    }
    /* Lock files */
    if (EXT_IS(".lock")) {
        return "🔒";
    }
    /* Build files */
    if (EXT_IS(".cmake") || EXT_IS(".mk")) {
        return "🛠️";
    }
    /* Text / Plain */
    if (EXT_IS(".txt") || EXT_IS(".log") || EXT_IS(".csv") ||
        EXT_IS(".tsv") || EXT_IS(".diff") || EXT_IS(".patch")) {
        return "📄";
    }
    return "📄";
#undef EXT_IS
}
